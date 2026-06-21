/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * ── Unified I/O Latency Benchmark ───────────────────────────────────
 *
 * Tests raw I/O backends (blocking sockets, optionally io_uring) ×
 * security modes (tcp, tls, ktls) in a single run.
 * Single connection per config — measures pure I/O path latency.
 *
 * Usage:
 *   bench_latency [--iterations 5000] [--warmup 1000]
 *                 [--payload 1024] [--port 14443]
 *                 [--cert path] [--key path]
 */

#include "hope-io/coredefs.h"
#include "hope-io/net/tls/tls_init.h"
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
#include <cassert>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#if PLATFORM_LINUX
#include <linux/tls.h>
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

// ── Platform guard ────────────────────────────────────────────────────

#if !(PLATFORM_LINUX || PLATFORM_APPLE)
int main() { printf("bench_latency: not supported on this platform\n"); return 0; }
#else

// ── Configuration ─────────────────────────────────────────────────────

struct bench_config {
    uint64_t    iterations  = 5000;
    uint64_t    warmup      = 1000;
    size_t      payload     = 1024;
    int         port        = 14443;
    std::string cert_path   = "test/certs/cert.pem";
    std::string key_path    = "test/certs/key.pem";
};

// ── Benchmark runs ────────────────────────────────────────────────────

struct bench_run {
    const char* label;
    const char* mode;   // "tcp", "tls", or "ktls"
};

static constexpr bench_run ALL_RUNS[] = {
    { "blocking tcp",  "tcp"  },
    { "blocking tls",  "tls"  },
    { "blocking ktls", "ktls" },
};

static constexpr int NUM_RUNS = sizeof(ALL_RUNS) / sizeof(ALL_RUNS[0]);

// ── Helpers ───────────────────────────────────────────────────────────

static inline int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

// ── POSIX helpers ─────────────────────────────────────────────────────

static void set_tcp_nodelay(int fd) {
    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

static int tcp_listen(int port) {
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_tcp_nodelay(fd);
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(fd, 1) < 0) { perror("listen"); exit(1); }
    return fd;
}

static int tcp_accept(int listen_fd) {
    struct sockaddr_in client_addr{};
    socklen_t addrlen = sizeof(client_addr);
    int fd = (int)accept(listen_fd, (struct sockaddr*)&client_addr, &addrlen);
    if (fd < 0) { perror("accept"); exit(1); }
    set_tcp_nodelay(fd);
    return fd;
}

static int tcp_connect(int port) {
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    set_tcp_nodelay(fd);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); exit(1); }
    return fd;
}

// Write all bytes (blocking)
static int write_all(int fd, const void* data, int len) {
    int total = 0;
    while (total < len) {
        int r = (int)::write(fd, (const char*)data + total, (size_t)(len - total));
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

// Read exactly len bytes (blocking)
static int read_all(int fd, void* data, int len) {
    int total = 0;
    while (total < len) {
        int r = (int)::read(fd, (char*)data + total, (size_t)(len - total));
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

// ── TLS helpers ────────────────────────────────────────────────────────

static int ssl_write_all(SSL* ssl, const void* data, int len) {
    int total = 0;
    while (total < len) {
        int r = SSL_write(ssl, (const char*)data + total, len - total);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

static int ssl_read_all(SSL* ssl, void* data, int len) {
    int total = 0;
    while (total < len) {
        int r = SSL_read(ssl, (char*)data + total, len - total);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

static SSL_CTX* create_ctx(bool is_server, const bench_config& cfg) {
    auto* method = is_server ? TLS_server_method() : TLS_client_method();
    auto* ctx = SSL_CTX_new(method);
    if (!ctx) { fprintf(stderr, "SSL_CTX_new failed\n"); exit(1); }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_cipher_list(ctx, "AES128-GCM-SHA256");
    if (is_server) {
        if (SSL_CTX_use_certificate_file(ctx, cfg.cert_path.c_str(), SSL_FILETYPE_PEM) <= 0)
            { fprintf(stderr, "cert failed\n"); exit(1); }
        if (SSL_CTX_use_PrivateKey_file(ctx, cfg.key_path.c_str(), SSL_FILETYPE_PEM) <= 0)
            { fprintf(stderr, "key failed\n"); exit(1); }
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    }
    return ctx;
}

// ── KTLS enable (Linux only) ───────────────────────────────────────────
#if PLATFORM_LINUX
static void enable_ktls(SSL* ssl, int fd, bool is_server) {
    size_t key_len = SSL_get_key_block_len(ssl);
    std::vector<uint8_t> key_block(key_len);
    SSL_generate_key_block(ssl, key_block.data(), key_len);

    uint8_t* cw_key = key_block.data() + 0;
    uint8_t* sw_key = key_block.data() + 16;
    uint8_t* cw_iv  = key_block.data() + 32;
    uint8_t* sw_iv  = key_block.data() + 36;

    auto make_info = [](struct tls12_crypto_info_aes_gcm_128* info,
                        const uint8_t* key, const uint8_t* salt) {
        memset(info, 0, sizeof(*info));
        info->info.version = TLS1_2_VERSION;
        info->info.cipher_type = TLS_CIPHER_AES_GCM_128;
        memcpy(info->key, key, 16);
        memcpy(info->salt, salt, 4);
    };

    struct tls12_crypto_info_aes_gcm_128 tx_info, rx_info;
    if (is_server) {
        make_info(&tx_info, sw_key, sw_iv);
        make_info(&rx_info, cw_key, cw_iv);
    } else {
        make_info(&tx_info, cw_key, cw_iv);
        make_info(&rx_info, sw_key, sw_iv);
    }

    if (setsockopt(fd, SOL_TCP, TCP_ULP, "tls", sizeof("tls")) < 0) { perror("TCP_ULP"); throw std::runtime_error("KTLS not supported"); }
    if (setsockopt(fd, SOL_TLS, TLS_TX, &tx_info, sizeof(tx_info)) < 0) { perror("TLS_TX"); throw std::runtime_error("KTLS TX failed"); }
    if (setsockopt(fd, SOL_TLS, TLS_RX, &rx_info, sizeof(rx_info)) < 0) { perror("TLS_RX"); throw std::runtime_error("KTLS RX failed"); }
}
#endif

// ── Per-configuration server/client threads ──────────────────────────

struct run_result {
    const char* label;
    const char* mode;
    uint64_t    count     = 0;
    uint64_t    errors    = 0;
    double      avg_ns    = 0;
    double      p50_ns    = 0;
    double      p95_ns    = 0;
    double      p99_ns    = 0;
    double      max_ns    = 0;
};

constexpr uint64_t MAX_SAMPLES = 4u << 20;

struct sample_buf {
    std::atomic<uint64_t> count{0};
    uint64_t errors{0};

    // Samples stored as a dynamically allocated vector to avoid 32 MB stack allocation.
    int64_t* samples = nullptr;

    void init() {
        samples = new int64_t[MAX_SAMPLES];
    }

    ~sample_buf() {
        delete[] samples;
    }

    void push(int64_t ns) noexcept {
        auto idx = count.fetch_add(1, std::memory_order_relaxed);
        if (idx < MAX_SAMPLES) samples[idx] = ns;
    }
};

static void server_thread(int listen_fd, const bench_run& run, SSL_CTX* ctx) {
    int client_fd = tcp_accept(listen_fd);

    SSL* ssl = nullptr;
    if (std::string(run.mode) != "tcp") {
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client_fd);
        if (SSL_accept(ssl) <= 0) { SSL_free(ssl); close(client_fd); return; }

#if PLATFORM_LINUX
        if (std::string(run.mode) == "ktls") {
            try {
                enable_ktls(ssl, client_fd, true);
                // After KTLS, raw read/write on fd
            } catch (...) {
                SSL_free(ssl); close(client_fd); return;
            }
        }
#endif
    }

    std::vector<char> buf(65536);

    while (true) {
        int n;
        if (ssl && std::string(run.mode) == "tls") {
            // Standard TLS — use SSL_read/SSL_write
            n = SSL_read(ssl, buf.data(), (int)buf.size());
        } else {
            // Raw TCP or KTLS — use read/write
            n = (int)::read(client_fd, buf.data(), buf.size());
        }
        if (n <= 0) break;

        int written;
        if (ssl && std::string(run.mode) == "tls") {
            written = ssl_write_all(ssl, buf.data(), n);
        } else {
            written = write_all(client_fd, buf.data(), n);
        }
        if (written <= 0) break;
    }

    if (ssl) SSL_free(ssl);
    close(client_fd);
}

static run_result run_config(const bench_config& cfg, const bench_run& run, int port) {
    run_result result;
    result.label = run.label;
    result.mode = run.mode;

    bool needs_tls = (std::string(run.mode) != "tcp");

    // Server context
    SSL_CTX* s_ctx = nullptr;
    if (needs_tls) s_ctx = create_ctx(true, cfg);

    // Listen + accept
    int listen_fd = tcp_listen(port);

    std::thread server(server_thread, listen_fd, run, s_ctx);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Client connect
    int client_fd = tcp_connect(port);

    SSL* c_ssl = nullptr;
    SSL_CTX* c_ctx = nullptr;
    if (needs_tls) {
        c_ctx = create_ctx(false, cfg);
        c_ssl = SSL_new(c_ctx);
        SSL_set_fd(c_ssl, client_fd);
        SSL_set_connect_state(c_ssl);
        if (SSL_do_handshake(c_ssl) <= 0) {
            fprintf(stderr, "client SSL handshake failed\n");
            exit(1);
        }
#if PLATFORM_LINUX
        if (std::string(run.mode) == "ktls") {
            try {
                enable_ktls(c_ssl, client_fd, false);
            } catch (...) {
                SSL_free(c_ssl); SSL_CTX_free(c_ctx);
                close(client_fd); close(listen_fd);
                server.join();
                result.errors = 1;
                result.label = run.label;
                result.mode = run.mode;
                return result;
            }
        }
#endif
    }

    std::string payload(cfg.payload, 'x');
    std::vector<char> reply(cfg.payload);

    sample_buf buf;
    buf.init();

    // Warmup
    for (uint64_t i = 0; i < cfg.warmup; ++i) {
        if (c_ssl && std::string(run.mode) == "tls") {
            ssl_write_all(c_ssl, payload.data(), (int)payload.size());
            ssl_read_all(c_ssl, reply.data(), (int)reply.size());
        } else {
            write_all(client_fd, payload.data(), (int)payload.size());
            read_all(client_fd, reply.data(), (int)reply.size());
        }
    }

    // Measurement
    for (uint64_t i = 0; i < cfg.iterations; ++i) {
        int64_t t0 = now_ns();
        int r;
        if (c_ssl && std::string(run.mode) == "tls") {
            r = ssl_write_all(c_ssl, payload.data(), (int)payload.size());
            if (r > 0) r = ssl_read_all(c_ssl, reply.data(), (int)reply.size());
        } else {
            r = write_all(client_fd, payload.data(), (int)payload.size());
            if (r > 0) r = read_all(client_fd, reply.data(), (int)reply.size());
        }
        if (r <= 0) { buf.errors++; break; }
        buf.push(now_ns() - t0);
    }

    // Cleanup
    if (c_ssl) SSL_free(c_ssl);
    if (c_ctx) SSL_CTX_free(c_ctx);
    close(client_fd);
    close(listen_fd);
    server.join();
    if (s_ctx) SSL_CTX_free(s_ctx);

    // Aggregate
    uint64_t n = buf.count.load();
    result.count = n;
    result.errors = buf.errors;

    if (n > 0) {
        std::vector<int64_t> samples_v(n);
        for (uint64_t i = 0; i < n; ++i) samples_v[i] = buf.samples[i];
        std::sort(samples_v.begin(), samples_v.end());

        auto percentile = [&](double p) -> double {
            double idx = (p / 100.0) * (n - 1);
            uint64_t lo = (uint64_t)idx;
            uint64_t hi = std::min(lo + 1, n - 1);
            return (double)samples_v[lo] + (idx - lo) * (samples_v[hi] - samples_v[lo]);
        };

        double sum = 0;
        for (auto v : samples_v) sum += v;
        result.avg_ns = sum / n;
        result.p50_ns = percentile(50);
        result.p95_ns = percentile(95);
        result.p99_ns = percentile(99);
        result.max_ns = (double)samples_v.back();
    }

    return result;
}

// ── Main ──────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    bench_config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (++i < argc) return argv[i];
            fprintf(stderr, "missing value for %s\n", a.c_str());
            exit(1);
        };
        if      (a == "-n" || a == "--iterations") cfg.iterations = (uint64_t)std::stol(next());
        else if (a == "-w" || a == "--warmup")     cfg.warmup     = (uint64_t)std::stol(next());
        else if (a == "-p" || a == "--port")       cfg.port       = std::stoi(next());
        else if (a == "-s" || a == "--payload")    cfg.payload    = (size_t)std::stol(next());
        else if (a == "--cert")                    cfg.cert_path  = next();
        else if (a == "--key")                     cfg.key_path   = next();
        else { fprintf(stderr, "unknown arg: %s\n", a.c_str()); exit(1); }
    }

    // OpenSSL can't be fully unloaded, so we never free these.
    hope::io::init_tls();

    printf("\n");
    printf("─── I/O Latency Benchmark ────────────────────────────\n");
    printf("  payload     = %zu bytes\n",   cfg.payload);
    printf("  iterations  = %llu\n",        (unsigned long long)cfg.iterations);
    printf("  warmup      = %llu\n",        (unsigned long long)cfg.warmup);
    printf("──────────────────────────────────────────────────────\n");
    printf("\n");
    printf("%-20s %-8s %10s %10s %10s %10s %10s %6s\n",
           "Backend", "Mode", "Avg", "p50", "p95", "p99", "Max", "Err");
    printf("%-20s %-8s %10s %10s %10s %10s %10s %6s\n",
           "──────", "────", "───", "───", "───", "───", "───", "───");

    int port = cfg.port;
    for (int i = 0; i < NUM_RUNS; ++i) {
        auto r = run_config(cfg, ALL_RUNS[i], port++);

        printf("%-20s %-8s %9.0f ns %9.0f ns %9.0f ns %9.0f ns %9.0f ns %6llu\n",
               r.label, r.mode,
               r.avg_ns, r.p50_ns, r.p95_ns, r.p99_ns, r.max_ns,
               (unsigned long long)r.errors);
        fflush(stdout);
    }

    printf("\n");
    return 0;
}
#endif
