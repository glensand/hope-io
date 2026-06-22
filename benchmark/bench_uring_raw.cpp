/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * ── Raw io_uring Echo Benchmark ─────────────────────
 *
 * Uses raw io_uring on BOTH sides (server and client) with
 * raw sockets — no hope-io abstractions. Measures bare-metal
 * io_uring echo throughput and latency for comparison with
 * bench_event_loop's io_uring TCP results.
 *
 * Usage:
 *   bench_uring_raw [--payload 64] [--connections 20]
 *                   [--duration 3] [--warmup 1] [--port 19400]
 *                   [--threads 4]
 */

#include "hope-io/coredefs.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <functional>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

// ── Platform guard ────────────────────────────────────────────────────

#if !PLATFORM_LINUX
int main() {
    printf("bench_uring_raw: Linux-only (requires io_uring)\n");
    return 0;
}
#else

#include <liburing.h>

// ── Configuration ─────────────────────────────────────────────────────

struct bench_config {
    int         payload     = 64;
    int         connections = 20;
    int         duration_s  = 3;
    int         warmup_s    = 1;
    int         port        = 19400;
    int         threads     = 4;   // client-side io_uring threads
};

// ── Tag encoding for io_uring user_data ──────────────────────────────

enum class uring_op : uint64_t { accept = 0, connect = 1, recv = 2, send = 3 };

constexpr uint64_t encode(int fd, uring_op op) { return (uint64_t(fd) << 4) | uint64_t(op); }
constexpr int      fd_of(uint64_t ud)          { return int(ud >> 4); }
constexpr uring_op op_of(uint64_t ud)          { return uring_op(ud & 0xF); }

// ── Per-thread latency buffer ─────────────────────────────────────────

constexpr uint64_t MAX_SAMPLES = 512 * 1024;

struct alignas(64) thread_buf {
    std::atomic<uint64_t> count{0};
    int64_t samples[MAX_SAMPLES];
    uint64_t errors{0};

    void push(int64_t us) noexcept {
        auto idx = count.fetch_add(1, std::memory_order_relaxed);
        if (idx < MAX_SAMPLES) samples[idx] = us;
    }

    uint64_t size() const noexcept { return std::min(count.load(), (uint64_t)MAX_SAMPLES); }
};

// ── Helpers ───────────────────────────────────────────────────────────

static double now_sec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static int64_t elapsed_us(double t0, double t1) {
    return (int64_t)((t1 - t0) * 1e6);
}

static double percentile(const int64_t* sorted, uint64_t n, double p) {
    if (n == 0) return 0;
    double idx = (p / 100.0) * (n - 1);
    uint64_t lo = (uint64_t)idx;
    uint64_t hi = std::min(lo + 1, n - 1);
    return (double)sorted[lo] + (idx - lo) * (sorted[hi] - sorted[lo]);
}

// ── Raw io_uring echo server ─────────────────────────────────────────

struct raw_echo_server {
    std::thread thread;
    std::atomic<bool> running{true};
    int m_listen_fd = -1;

    // Per-connection state: pending recv or send
    struct conn_state {
        std::string buf;
    };
    std::vector<conn_state> conns;

    void start(int port) {
        thread = std::thread([this, port] { run(port); });
    }

    void stop() {
        running = false;
        if (thread.joinable()) thread.join();
        if (m_listen_fd >= 0) close(m_listen_fd);
    }

    ~raw_echo_server() { stop(); }

private:
    void run(int port) {
        // Listen socket
        m_listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        int reuse = 1;
        setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        bind(m_listen_fd, (struct sockaddr*)&addr, sizeof(addr));
        listen(m_listen_fd, 1024);

        // io_uring ring
        struct io_uring ring;
        io_uring_queue_init(4096, &ring, 0);

        // Pre-allocate connection state
        conns.resize(1024);

        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        // Submit initial accept
        submit_accept(&ring, &client_addr, &client_len);
        io_uring_submit(&ring);

        // Verify io_uring works with a NOP
        struct io_uring_sqe* nop_sqe = io_uring_get_sqe(&ring);
        io_uring_prep_nop(nop_sqe);
        io_uring_sqe_set_data64(nop_sqe, 0xDEAD);
        io_uring_submit(&ring);

        struct io_uring_cqe* nop_cqe;
        int nop_ret = io_uring_wait_cqe(&ring, &nop_cqe);
        fprintf(stderr, "raw_srv: nop ret=%d res=%ld ud=0x%lx\n", nop_ret, (long)nop_cqe->res, (unsigned long)nop_cqe->user_data);
        io_uring_cqe_seen(&ring, nop_cqe);

        // Now submit the real accept
        submit_accept(&ring, &client_addr, &client_len);
        io_uring_submit(&ring);
        fprintf(stderr, "raw_srv: accept submitted\n");

        while (running.load(std::memory_order_acquire)) {
            struct io_uring_cqe* cqe;
            int ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0) { fprintf(stderr, "raw_srv: wait_cqe error %d\n", ret); break; }

            unsigned head, count = 0;
            io_uring_for_each_cqe(&ring, head, cqe) {
                ++count;
                int fd = fd_of(cqe->user_data);
                uring_op op = op_of(cqe->user_data);
                int res = cqe->res;
                fprintf(stderr, "raw_srv: cqe op=%d fd=%d res=%d\n", (int)op, fd, res);
                if (res < 0) {
                    if (op == uring_op::accept) {
                        // Re-arm accept on transient errors
                        submit_accept(&ring, &client_addr, &client_len);
                        io_uring_submit(&ring);
                    }
                    continue;
                }

                switch (op) {
                case uring_op::accept: {
                    int client_fd = res;
                    fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL) | O_NONBLOCK);
                    // Ensure state slot exists
                    if ((size_t)client_fd >= conns.size())
                        conns.resize(client_fd + 1);
                    conns[client_fd].buf.resize(65536);
                    // Submit first recv
                    submit_recv(&ring, client_fd);
                    // Re-arm accept
                    submit_accept(&ring, &client_addr, &client_len);
                    break;
                }
                case uring_op::recv: {
                    if (res == 0) { close(fd); break; } // EOF
                    // Echo back
                    conns[fd].buf.resize((size_t)res);
                    submit_send(&ring, fd);
                    break;
                }
                case uring_op::send: {
                    // Done sending, wait for next recv
                    conns[fd].buf.resize(65536);
                    submit_recv(&ring, fd);
                    break;
                }
                default:
                    break;
                }
            }
            io_uring_cq_advance(&ring, count);
            io_uring_submit(&ring);
        }

        // Cleanup connections
        for (size_t i = 0; i < conns.size(); ++i) {
            if (!conns[i].buf.empty() && (int)i != m_listen_fd)
                close((int)i);
        }
        io_uring_queue_exit(&ring);
    }

    void submit_accept(struct io_uring* ring, struct sockaddr_in* addr, socklen_t* addrlen) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
        if (!sqe) return;
        io_uring_prep_accept(sqe, m_listen_fd, (struct sockaddr*)addr, addrlen, 0);
        io_uring_sqe_set_data64(sqe, encode(m_listen_fd, uring_op::accept));
    }

    void submit_recv(struct io_uring* ring, int fd) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
        if (!sqe) return;
        io_uring_prep_recv(sqe, fd, conns[fd].buf.data(), conns[fd].buf.size(), 0);
        io_uring_sqe_set_data64(sqe, encode(fd, uring_op::recv));
    }

    void submit_send(struct io_uring* ring, int fd) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
        if (!sqe) return;
        io_uring_prep_send(sqe, fd, conns[fd].buf.data(), conns[fd].buf.size(), 0);
        io_uring_sqe_set_data64(sqe, encode(fd, uring_op::send));
    }
};

// ── Raw io_uring client (multi-threaded, one ring per thread) ───────

static void run_uring_raw_clients(
    const bench_config& cfg,
    const std::string& payload,
    double warmup_end,
    double bench_end,
    int port,
    std::vector<thread_buf>& bufs)
{
    int num_threads = std::min(cfg.threads, cfg.connections);
    std::vector<std::thread> workers;

    for (int t = 0; t < num_threads; ++t) {
        int start    = (t * cfg.connections) / num_threads;
        int end      = ((t + 1) * cfg.connections) / num_threads;
        int count    = end - start;
        if (count == 0) continue;

        workers.emplace_back([&cfg, &payload, warmup_end, bench_end, port, &bufs, start, count]() {
            // Per-thread io_uring ring
            struct io_uring ring;
            int ring_depth = std::max(count * 4, 16);
            if (io_uring_queue_init(ring_depth, &ring, 0) < 0) {
                for (int i = start; i < start + count; ++i) bufs[i].errors++;
                return;
            }

            // Per-connection state
            struct conn_state {
                int fd = -1;
                int idx;
                std::string reply;
                double rtt_start = 0;
                bool connected = false;
            };

            std::vector<conn_state> conns(count);
            struct sockaddr_in server_addr{};
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

            // Create sockets and initiate async connects
            int conns_ok = 0;
            for (int j = 0; j < count; ++j) {
                int global_idx = start + j;
                conns[j].idx = global_idx;
                conns[j].reply.resize(cfg.payload);
                conns[j].fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
                if (conns[j].fd < 0) { bufs[global_idx].errors++; continue; }

                struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
                io_uring_prep_connect(sqe, conns[j].fd,
                                      (struct sockaddr*)&server_addr, sizeof(server_addr));
                io_uring_sqe_set_data64(sqe, encode(j, uring_op::connect));
                conns_ok++;
            }
            io_uring_submit(&ring);

            // Reap all connect completions
            for (int reaped = 0; reaped < conns_ok; ) {
                struct io_uring_cqe* cqe;
                if (io_uring_wait_cqe(&ring, &cqe) < 0) break;
                int j = (int)cqe->user_data >> 4;
                if (cqe->res < 0) { bufs[start + j].errors++; conns[j].fd = -1; }
                else { conns[j].connected = true; }
                io_uring_cqe_seen(&ring, cqe);
                reaped++;
            }

            double now = now_sec();

            // Submit initial batch of send+recv pairs (linked)
            int active = 0;
            for (int j = 0; j < count; ++j) {
                if (conns[j].fd < 0) continue;
                conn_state& cs = conns[j];
                cs.rtt_start = now;

                struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
                io_uring_prep_send(sqe, cs.fd, payload.data(), payload.size(), 0);
                sqe->flags |= IOSQE_IO_LINK;
                io_uring_sqe_set_data64(sqe, encode(j, uring_op::send));

                sqe = io_uring_get_sqe(&ring);
                io_uring_prep_recv(sqe, cs.fd, cs.reply.data(), cs.reply.size(), 0);
                io_uring_sqe_set_data64(sqe, encode(j, uring_op::recv));
                active++;
            }
            io_uring_submit(&ring);

            // Completion loop
            bool bench_mode = false;
            while (active > 0) {
                struct io_uring_cqe* cqe;
                if (io_uring_wait_cqe(&ring, &cqe) < 0) break;

                int j = (int)(cqe->user_data >> 4);
                uring_op op = op_of(cqe->user_data);

                if (op == uring_op::recv) {
                    double t1 = now_sec();

                    if (cqe->res < 0) {
                        bufs[start + j].errors++;
                        active--;
                        io_uring_cqe_seen(&ring, cqe);
                        continue;
                    }

                    conn_state& cs = conns[j];

                    if (bench_mode) {
                        bufs[cs.idx].push(elapsed_us(cs.rtt_start, t1));
                    }

                    if (t1 >= bench_end) {
                        active--;
                        io_uring_cqe_seen(&ring, cqe);
                        continue;
                    }

                    if (!bench_mode && t1 >= warmup_end) {
                        bench_mode = true;
                    }

                    // Submit next send+recv pair
                    cs.rtt_start = t1;

                    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
                    io_uring_prep_send(sqe, cs.fd, payload.data(), payload.size(), 0);
                    sqe->flags |= IOSQE_IO_LINK;
                    io_uring_sqe_set_data64(sqe, encode(j, uring_op::send));

                    sqe = io_uring_get_sqe(&ring);
                    io_uring_prep_recv(sqe, cs.fd, cs.reply.data(), cs.reply.size(), 0);
                    io_uring_sqe_set_data64(sqe, encode(j, uring_op::recv));

                    io_uring_submit(&ring);
                }

                io_uring_cqe_seen(&ring, cqe);
            }

            // Cleanup
            for (auto& cs : conns) {
                if (cs.fd >= 0) close(cs.fd);
            }
            io_uring_queue_exit(&ring);
        });
    }

    for (auto& t : workers) t.join();
}

// ── Run one configuration ─────────────────────────────────────────────

static void run_config(const bench_config& cfg, int port) {
    // Start server
    raw_echo_server server;
    server.start(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::string payload(cfg.payload, 'x');
    double warmup_end = now_sec() + cfg.warmup_s;
    double bench_end  = warmup_end + cfg.duration_s;

    std::vector<thread_buf> bufs(cfg.connections);

    run_uring_raw_clients(cfg, payload, warmup_end, bench_end, port, bufs);

    server.stop();

    // Aggregate results
    uint64_t total_requests = 0;
    uint64_t total_errors   = 0;
    for (auto& b : bufs) {
        total_requests += b.size();
        total_errors   += b.errors;
    }

    std::vector<int64_t> all;
    all.reserve(total_requests);
    for (auto& b : bufs) {
        auto n = b.size();
        for (uint64_t i = 0; i < n; ++i) all.push_back(b.samples[i]);
    }
    std::sort(all.begin(), all.end());

    double elapsed = cfg.duration_s;

    printf("raw io_uring  tcp       %12.0f %7.0f us %7.0f us %6llu\n",
           (double)total_requests / elapsed,
           percentile(all.data(), all.size(), 50),
           percentile(all.data(), all.size(), 99),
           (unsigned long long)total_errors);
    fflush(stdout);
}

// ── Main ──────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    bench_config cfg;

    // Parse simplified args
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc)
            cfg.payload = atoi(argv[++i]);
        else if (strcmp(argv[i], "--connections") == 0 && i + 1 < argc)
            cfg.connections = atoi(argv[++i]);
        else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc)
            cfg.duration_s = atoi(argv[++i]);
        else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
            cfg.warmup_s = atoi(argv[++i]);
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            cfg.port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
            cfg.threads = atoi(argv[++i]);
    }

    printf("\n");
    printf("─── Raw io_uring Benchmark ────────────────────────\n");
    printf("  payload       = %d bytes\n",   cfg.payload);
    printf("  connections   = %d\n",           cfg.connections);
    printf("  duration      = %d s\n",         cfg.duration_s);
    printf("  threads       = %d\n",           cfg.threads);
    printf("──────────────────────────────────────────────────────\n");
    printf("\n");

    printf("%-20s %-8s %12s %8s %8s %6s\n", "Backend", "Mode", "RPS", "p50", "p99", "Errors");
    printf("%-20s %-8s %12s %8s %8s %6s\n", "──────", "────", "───", "───", "───", "──────");

    run_config(cfg, cfg.port);

    printf("\n");
    return 0;
}
#endif
