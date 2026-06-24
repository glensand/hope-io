/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * ── Unified Event Loop Benchmark ────────────────────────────────────
 *
 * Tests all event loop backends (epoll, io_uring) × modes (tcp, tls, ktls)
 * in a single run. Server uses library event loops.
 * TCP client uses raw sockets via io_uring (non-blocking, N threads).
 * TLS/ktls client uses per-thread blocking streams (N connections × N threads).
 *
 * Usage:
 *   bench_event_loop [--payload 1024] [--connections 20]
 *                    [--duration 3] [--warmup 1] [--port 19300]
 *                    [--ktls]            # enable KTLS for "ktls" rows
 *                    [--cert path] [--key path]
 */

#include "hope-io/net/event_loop.h"
#include "hope-io/net/tls_event_loop.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/nix/tcp_stream.h"
#include "hope-io/net/tls/tcp_tls_stream.h"
#include "hope-io/net/linux/event_loop_impl.h"
#include "hope-io/net/linux/tls_event_loop_impl.h"
#include "hope-io/net/uring/uring_tcp_event_loop.h"
#include "hope-io/net/uring/uring_tls_event_loop.h"
#include "hope-io/net/init.h"
#include "hope-io/net/tls/tls_init.h"
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
#include <functional>
#include <cstring>

#if PLATFORM_LINUX
#include <liburing.h>
#endif

using namespace hope::io::el;

// ── Platform guard ────────────────────────────────────────────────────

#if !PLATFORM_LINUX
int main() {
    printf("bench_event_loop: Linux-only\n");
    return 0;
}
#else

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// ── Configuration ─────────────────────────────────────────────────────

struct bench_config {
    size_t      payload     = 64;
    int         connections = 20;
    int         duration_s  = 3;
    int         warmup_s    = 1;
    int         port        = 19300;
    int         num_threads = 4;
    bool        ktls_enable = false;
    std::string cert_path;
    std::string key_path;
};

// ── Benchmark runs ────────────────────────────────────────────────────

enum class el_backend { epoll, io_uring };

struct bench_run {
    const char* label;
    const char* mode;   // "tcp", "tls", "ktls"
    el_backend  backend;
};

static constexpr bench_run ALL_RUNS[] = {
    { "epoll tcp",    "tcp",  el_backend::epoll    },
    { "epoll tls",    "tls",  el_backend::epoll    },
    { "epoll ktls",   "ktls", el_backend::epoll    },
    { "io_uring tcp",  "tcp",  el_backend::io_uring },
    { "io_uring tls",  "tls",  el_backend::io_uring },
    { "io_uring ktls", "ktls", el_backend::io_uring },
};

static constexpr int NUM_RUNS = sizeof(ALL_RUNS) / sizeof(ALL_RUNS[0]);

// ── Per-thread latency buffer (atomic for multi-threaded clients) ────

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

struct run_result {
    const char* label;
    const char* mode;
    uint64_t    total_requests = 0;
    uint64_t    total_errors   = 0;
    double      rps            = 0;
    double      p50            = 0;
    double      p95            = 0;
    double      p99            = 0;
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

static bool find_file(std::string& out, const char* name) {
    const char* dirs[] = { "", "../test/certs/", "../../test/certs/", "test/certs/" };
    for (auto* d : dirs) {
        std::string p = std::string(d) + name;
        if (FILE* f = fopen(p.c_str(), "r")) { fclose(f); out = p; return true; }
    }
    return false;
}

// ── Server ────────────────────────────────────────────────────────────

struct server_guard {
    std::thread thread;
    std::function<void()> stop_fn;
    std::function<void()> destroy_fn;

    template<typename Loop, typename Config>
    void start(Loop* l, Config cfg) {
        stop_fn = [l] { l->stop(); };
        destroy_fn = [l] { delete l; };
        thread = std::thread([l, cfg = std::move(cfg)] { l->run(cfg); });
    }

    void stop() {
        if (stop_fn) stop_fn();
        if (thread.joinable()) thread.join();
        if (destroy_fn) destroy_fn();
        stop_fn = {};
        destroy_fn = {};
    }

    ~server_guard() { stop(); }
    server_guard() = default;
    server_guard(server_guard&& o) noexcept
        : thread(std::move(o.thread)), stop_fn(std::move(o.stop_fn)), destroy_fn(std::move(o.destroy_fn)) {}
    server_guard& operator=(server_guard&& o) noexcept {
        if (this != &o) { stop(); thread = std::move(o.thread); stop_fn = std::move(o.stop_fn); destroy_fn = std::move(o.destroy_fn); }
        return *this;
    }
    server_guard(const server_guard&) = delete;
    server_guard& operator=(const server_guard&) = delete;
};

// ── io_uring raw socket TCP client (multi-threaded) ─────────────────

static void run_uring_tcp_clients(
    const bench_config& cfg,
    const std::string& payload,
    double warmup_end,
    double bench_end,
    int port,
    std::vector<thread_buf>& bufs)
{
    int num_threads = std::min(cfg.num_threads, cfg.connections);
#if PLATFORM_LINUX
    std::vector<std::thread> workers;

    for (int t = 0; t < num_threads; ++t) {
        int start    = (t * cfg.connections) / num_threads;
        int end      = ((t + 1) * cfg.connections) / num_threads;
        int count    = end - start;
        if (count == 0) continue;

        workers.emplace_back([&cfg, &payload, warmup_end, bench_end, port, &bufs, start, count]() {
            // Per-thread io_uring ring
            struct io_uring ring;
            int ring_depth = std::max(count * 2, 16);
            if (io_uring_queue_init(ring_depth, &ring, 0) < 0) {
                for (int i = start; i < start + count; ++i) bufs[i].errors++;
                return;
            }

            // Per-connection state for this thread's partition
            struct conn_state {
                int fd = -1;
                int idx;
                std::string reply;
                double rtt_start = 0;
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
                sqe->user_data = (uint64_t)j;
                conns_ok++;
            }
            io_uring_submit(&ring);

            // Reap connect completions
            for (int reaped = 0; reaped < conns_ok; ) {
                struct io_uring_cqe* cqe;
                if (io_uring_wait_cqe(&ring, &cqe) < 0) break;
                int j = (int)cqe->user_data;
                if (cqe->res < 0) { bufs[start + j].errors++; conns[j].fd = -1; }
                io_uring_cqe_seen(&ring, cqe);
                reaped++;
            }

            double now = now_sec();

            // Submit initial batch of linked send+recv pairs
            int active = 0;
            for (int j = 0; j < count; ++j) {
                if (conns[j].fd < 0) continue;
                conn_state& cs = conns[j];
                cs.rtt_start = now;

                struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
                io_uring_prep_send(sqe, cs.fd, payload.data(), payload.size(), 0);
                sqe->flags |= IOSQE_IO_LINK;
                sqe->user_data = ((uint64_t)j << 1) | 0; // send tag

                sqe = io_uring_get_sqe(&ring);
                io_uring_prep_recv(sqe, cs.fd, cs.reply.data(), cs.reply.size(), 0);
                sqe->user_data = ((uint64_t)j << 1) | 1; // recv tag
                active++;
            }
            io_uring_submit(&ring);

            // Completion loop
            bool bench_mode = false;
            while (active > 0) {
                struct io_uring_cqe* cqe;
                if (io_uring_wait_cqe(&ring, &cqe) < 0) break;

                int j = (int)(cqe->user_data >> 1);
                int is_recv = (int)(cqe->user_data & 1);

                if (is_recv) {
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

                    cs.rtt_start = t1;

                    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
                    io_uring_prep_send(sqe, cs.fd, payload.data(), payload.size(), 0);
                    sqe->flags |= IOSQE_IO_LINK;
                    sqe->user_data = ((uint64_t)j << 1) | 0;

                    sqe = io_uring_get_sqe(&ring);
                    io_uring_prep_recv(sqe, cs.fd, cs.reply.data(), cs.reply.size(), 0);
                    sqe->user_data = ((uint64_t)j << 1) | 1;

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
#else
    (void)cfg;
    (void)payload;
    (void)warmup_end;
    (void)bench_end;
    (void)port;
    (void)num_threads;
    for (auto& b : bufs) b.errors++;
    fprintf(stderr, "io_uring not available on this platform\n");
#endif
}

// ── TLS blocking stream client (N threads × N connections) ─────────

static void run_tls_blocking_clients(
    const bench_config& cfg,
    const std::string& payload,
    double warmup_end,
    double bench_end,
    int port,
    std::vector<thread_buf>& bufs)
{
    std::vector<std::thread> workers;
    workers.reserve(cfg.connections);

    for (int i = 0; i < cfg.connections; ++i) {
        workers.emplace_back([&cfg, &payload, warmup_end, bench_end, &bufs, i, port]() {
            thread_buf& buf = bufs[i];
            hope::io::stream* s = nullptr;

            auto* tcp = new hope::io::tcp_stream();
            if (!tcp) { buf.errors++; return; }
            s = new hope::io::tcp_tls_stream(static_cast<hope::io::tcp_stream*>(tcp));

            if (!s) { buf.errors++; return; }

            try { s->connect("127.0.0.1", port); }
            catch (...) { buf.errors++; delete s; return; }

            std::string reply;
            reply.resize(cfg.payload);

            while (now_sec() < warmup_end) {
                try { s->write(payload.data(), payload.size()); s->read(reply.data(), reply.size()); }
                catch (...) { break; }
            }

            while (now_sec() < bench_end) {
                double t0 = now_sec();
                try { s->write(payload.data(), payload.size()); s->read(reply.data(), reply.size()); }
                catch (...) { buf.errors++; break; }
                buf.push(elapsed_us(t0, now_sec()));
            }

            s->disconnect();
            delete s;
        });
    }

    for (auto& t : workers) t.join();
}

// ── Run one configuration ─────────────────────────────────────────────

static run_result run_config(const bench_config& cfg, const bench_run& run, int port) {
    run_result result;
    result.label = run.label;
    result.mode = run.mode;

    bool needs_tls = (std::string(run.mode) != "tcp");
    bool ktls = (std::string(run.mode) == "ktls") && cfg.ktls_enable;

    // ── Start server ──────────────────────────────────────────────
    server_guard server;

    if (run.backend == el_backend::io_uring) {
        if (needs_tls) {
            auto* loop = new uring_tls_event_loop(
                [](connection&) { return el_connection_state::read; },
                [](connection&) { return el_connection_state::write; },
                [](connection&) { return el_connection_state::read; },
                [](connection&, const std::string&) { return el_connection_state::die; });
            tls_config scfg;
            scfg.port       = port;
            scfg.cert_path  = cfg.cert_path;
            scfg.key_path   = cfg.key_path;
            scfg.max_mutual_connections = 10000;
            scfg.max_accepts_per_tick   = 1000;
            scfg.epoll_timeout = 1000;
            scfg.enable_ktls  = ktls;
            server.start(loop, std::move(scfg));
        } else {
            auto* loop = new uring_tcp_event_loop(
                [](connection&) { return el_connection_state::read; },
                [](connection&) { return el_connection_state::write; },
                [](connection&) { return el_connection_state::read; },
                [](connection&, const std::string&) { return el_connection_state::die; });
            config ecfg;
            ecfg.port        = port;
            ecfg.max_mutual_connections = 10000;
            ecfg.max_accepts_per_tick   = 1000;
            ecfg.epoll_temeout = 1000;
            server.start(loop, std::move(ecfg));
        }
    } else {
        if (needs_tls) {
            auto* loop = new tls_event_loop_impl(
                [](connection&) { return el_connection_state::read; },
                [](connection&) { return el_connection_state::write; },
                [](connection&) { return el_connection_state::read; },
                [](connection&, const std::string&) { return el_connection_state::die; });
            tls_config scfg;
            scfg.port       = port;
            scfg.cert_path  = cfg.cert_path;
            scfg.key_path   = cfg.key_path;
            scfg.max_mutual_connections = 10000;
            scfg.max_accepts_per_tick   = 1000;
            scfg.epoll_timeout = 1000;
            scfg.enable_ktls  = ktls;
            server.start(loop, std::move(scfg));
        } else {
            auto* loop = new event_loop_impl_t(
                [](connection&) { return el_connection_state::read; },
                [](connection&) { return el_connection_state::write; },
                [](connection&) { return el_connection_state::read; },
                [](connection&, const std::string&) { return el_connection_state::die; });
            config ecfg;
            ecfg.port        = port;
            ecfg.max_mutual_connections = 10000;
            ecfg.max_accepts_per_tick   = 1000;
            ecfg.epoll_temeout = 1000;
            server.start(loop, std::move(ecfg));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::string payload(cfg.payload, 'x');

    double warmup_end = now_sec() + cfg.warmup_s;
    double bench_end  = warmup_end + cfg.duration_s;

    std::vector<thread_buf> bufs(cfg.connections);

    if (std::string(run.mode) == "tcp") {
        run_uring_tcp_clients(cfg, payload, warmup_end, bench_end, port, bufs);
    } else {
        run_tls_blocking_clients(cfg, payload, warmup_end, bench_end, port, bufs);
    }
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
    result.total_requests = total_requests;
    result.total_errors   = total_errors;
    result.rps            = (double)total_requests / elapsed;
    result.p50            = percentile(all.data(), all.size(), 50);
    result.p95            = percentile(all.data(), all.size(), 95);
    result.p99            = percentile(all.data(), all.size(), 99);

    return result;
}

// ── Main ──────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    hope::io::init();

    bench_config cfg;
    cfg.payload     = 64;
    cfg.connections = 20;
    cfg.duration_s  = 3;
    cfg.warmup_s    = 1;
    cfg.port        = 19300;
    cfg.ktls_enable = true;

    bool use_ecdsa = false;
    const char* cert_name = "cert.pem";
    const char* key_name  = "key.pem";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--ecdsa") == 0) {
            use_ecdsa = true;
            cert_name = "ec_cert.pem";
            key_name  = "ec_key.pem";
        } else if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc)
            cfg.payload = (size_t)atol(argv[++i]);
        else if (strcmp(argv[i], "--connections") == 0 && i + 1 < argc)
            cfg.connections = atoi(argv[++i]);
        else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc)
            cfg.duration_s = atoi(argv[++i]);
    }

    if (!find_file(cfg.cert_path, cert_name)) { fprintf(stderr, "cert not found: %s\n", cert_name); return 1; }
    if (!find_file(cfg.key_path,  key_name))  { fprintf(stderr, "key not found: %s\n", key_name);  return 1; }
    hope::io::init_tls();

    printf("\n");
    printf("─── Event Loop Benchmark ─────────────────────────────\n");
    printf("  payload       = %zu bytes\n",   cfg.payload);
    printf("  connections   = %d\n",           cfg.connections);
    printf("  duration      = %d s\n",         cfg.duration_s);
    if (use_ecdsa)
        printf("  cert          = ECDSA P-256\n");
    printf("──────────────────────────────────────────────────────\n");
    printf("\n");

    printf("%-20s %-8s %12s %8s %8s %6s\n", "Backend", "Mode", "RPS", "p50", "p99", "Errors");
    printf("%-20s %-8s %12s %8s %8s %6s\n", "──────", "────", "───", "───", "───", "──────");

    int port = cfg.port;
    for (int i = 0; i < NUM_RUNS; ++i) {
        auto r = run_config(cfg, ALL_RUNS[i], port++);
        printf("%-20s %-8s %12.0f %7.0f us %7.0f us %6llu\n",
               r.label, r.mode, r.rps, r.p50, r.p99,
               (unsigned long long)r.total_errors);
        fflush(stdout);
        if (r.total_errors > r.total_requests) {
            fprintf(stderr, "bench: too many errors in %s\n", r.label);
            return 1;
        }
    }

    printf("\n");
    return 0;
}
#endif
