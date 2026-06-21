/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * ── Raw TLS Echo Latency Benchmark ───────────────────────────────
 *
 * Raw POSIX sockets + raw TLS API (BoringSSL). TLS 1.3 forced.
 * Supports tuning variant presets to measure TCP/TLS parameter
 * impact on latency.
 *
 * Compile:
 *   g++ -O3 -Ilib/boringssl/include -Ilib bench_tls_raw.cpp \
 *       build/lib/boringssl/libssl.a build/lib/boringssl/libcrypto.a -lpthread
 *
 * Usage:
 *   ./bench_tls_raw [-n iterations] [-p port] [-w warmup] [-V variant]
 *                   [-s size1,size2,...]
 *
 * Example:
 *   ./bench_tls_raw -n 50000 -V 3 -s 200,4096
 */

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <cassert>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>
#include <sched.h>

#include <linux/tls.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

// ── Constants ──────────────────────────────────────────────────────────

static constexpr size_t MAX_SAMPLES     = 8u << 20;
static constexpr int    DEFAULT_PORT    = 14443;
static constexpr int    DEFAULT_ITER    = 50000;
static constexpr int    DEFAULT_WARMUP  = 2000;
static constexpr int    DEFAULT_VARIANT = 0;   // baseline (TCP_NODELAY only, default buffer sizes)

static constexpr int DEFAULT_SIZES[]    = { 64, 200, 1024, 4096, 16384, 65536 };
static constexpr int DEFAULT_NUM_SIZES  = 6;

// ── Variant description table ─────────────────────────────────────────

struct variant_desc {
    int     id;
    const char* name;
    const char* desc;
};

static constexpr variant_desc VARIANTS[] = {
    { 0,  "baseline",      "TCP_NODELAY, no extra opts" },
    { 1,  "rcvbuf",        "SO_RCVBUF=262144 both sides (best)" },
    { 2,  "sndbuf",        "SO_SNDBUF=262144 both sides" },
    { 3,  "bigbufs",       "SO_RCVBUF+SO_SNDBUF=262144" },
    { 4,  "ssl_modes",     "PARTIAL_WRITE+MOVING_WRITE+AUTO_RETRY" },
    { 5,  "read_ahead",    "SSL_set_read_ahead(ssl, 1)" },
    { 6,  "nodelay_off",   "TCP_NODELAY off (Nagle on)" },
    { 7,  "best_combo",    "bigbufs+ssl_modes+read_ahead" },
    { 8,  "pin_cores",     "thread affinity: server/core0 client/core1" },
    { 9,  "pin_rcvbuf",    "pin_cores + SO_RCVBUF=262144" },
};
static constexpr int NUM_VARIANTS = 10;

// ── Clock ─────────────────────────────────────────────────────────────

static inline int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

// ── Sample buffer ──────────────────────────────────────────────────────

struct alignas(64) sample_buf {
    std::atomic<uint64_t> count{0};
    int64_t* const        samples{nullptr};
    const uint64_t        capacity{0};

    explicit sample_buf(uint64_t cap = MAX_SAMPLES)
        : samples(new int64_t[cap]), capacity(cap) {}

    ~sample_buf() { delete[] samples; }

    sample_buf(const sample_buf&) = delete;
    sample_buf& operator=(const sample_buf&) = delete;
    sample_buf(sample_buf&&) = delete;
    sample_buf& operator=(sample_buf&&) = delete;

    void push(int64_t ns) noexcept {
        auto idx = count.fetch_add(1, std::memory_order_relaxed);
        if (idx < capacity) samples[idx] = ns;
    }

    [[nodiscard]] uint64_t size() const noexcept {
        return std::min(count.load(std::memory_order_acquire), capacity);
    }
};

// ── Config ─────────────────────────────────────────────────────────────

struct config {
    int            port        = DEFAULT_PORT;
    uint64_t       iterations  = DEFAULT_ITER;
    uint64_t       warmup      = DEFAULT_WARMUP;
    int            variant     = DEFAULT_VARIANT;
    std::vector<size_t> sizes;
};

static config parse_args(int argc, char** argv) {
    config cfg;
    cfg.sizes.assign(DEFAULT_SIZES, DEFAULT_SIZES + DEFAULT_NUM_SIZES);

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (++i < argc) return argv[i];
            fprintf(stderr, "missing value for %s\n", a.c_str());
            exit(1);
        };
        if (a == "-n" || a == "--iterations") {
            cfg.iterations = (uint64_t)std::stol(next());
        } else if (a == "-p" || a == "--port") {
            cfg.port = std::stoi(next());
        } else if (a == "-w" || a == "--warmup") {
            cfg.warmup = (uint64_t)std::stol(next());
        } else if (a == "-V" || a == "--variant") {
            cfg.variant = std::stoi(next());
            if (cfg.variant < 0 || cfg.variant >= NUM_VARIANTS) {
                fprintf(stderr, "variant must be 0-%d\n", NUM_VARIANTS - 1);
                exit(1);
            }
        } else if (a == "-s" || a == "--sizes") {
            cfg.sizes.clear();
            auto s = next();
            size_t pos = 0;
            while (pos < s.size()) {
                auto comma = s.find(',', pos);
                auto token = s.substr(pos, comma == std::string::npos ? comma : comma - pos);
                cfg.sizes.push_back((size_t)std::stol(token));
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        } else if (a == "--list-variants") {
            printf("Variants:\n");
            for (int i = 0; i < NUM_VARIANTS; ++i)
                printf("  %2d  %-16s  %s\n", VARIANTS[i].id, VARIANTS[i].name, VARIANTS[i].desc);
            exit(0);
        } else {
            fprintf(stderr, "unknown arg: %s\n", a.c_str());
            exit(1);
        }
    }
    return cfg;
}

// ── Thread pinning ────────────────────────────────────────────────────

static void pin_current_thread(int core_id) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core_id, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

// ── POSIX helpers ─────────────────────────────────────────────────────

static void apply_sockopts(int fd, int variant) {
    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

static int tcp_listen(int port, int variant) {
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    apply_sockopts(fd, variant);
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(fd, 1) < 0) { perror("listen"); exit(1); }
    return fd;
}

static int tcp_connect(const char* ip, int port, int variant) {
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    apply_sockopts(fd, variant);
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); exit(1); }
    return fd;
}

static int tcp_accept(int listen_fd, int variant) {
    (void)variant;
    struct sockaddr_in client_addr{};
    socklen_t addrlen = sizeof(client_addr);
    int fd = (int)accept(listen_fd, (struct sockaddr*)&client_addr, &addrlen);
    if (fd < 0) { perror("accept"); exit(1); }
    // Apply socket options to accepted fd too
    apply_sockopts(fd, variant);
    return fd;
}

// ── TLS helpers ────────────────────────────────────────────────────────

// Write all bytes, handling partial writes
static int ssl_write_all(SSL* ssl, const void* data, int len) {
    int total = 0;
    while (total < len) {
        int r = SSL_write(ssl, (const char*)data + total, len - total);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

// Read exactly len bytes (loop to fill buffer)
static int ssl_read_all(SSL* ssl, void* data, int len) {
    int total = 0;
    while (total < len) {
        int r = SSL_read(ssl, (char*)data + total, len - total);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

static void apply_ssl_ctx_opts(SSL_CTX* ctx, int variant) {
    if (variant == 4 || variant == 7) {
        SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
        SSL_CTX_set_mode(ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);
    }
}

static void apply_ssl_opts(SSL* ssl, int variant) {
    if (variant == 5 || variant == 7) {
        SSL_set_read_ahead(ssl, 1);
    }
}

static SSL_CTX* create_server_ctx(int variant) {
    auto* method = TLS_server_method();
    auto* ctx = SSL_CTX_new(method);
    if (!ctx) { fprintf(stderr, "SSL_CTX_new failed\n"); exit(1); }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_cipher_list(ctx, "AES128-GCM-SHA256");
    if (SSL_CTX_use_certificate_file(ctx, "test/certs/cert.pem", SSL_FILETYPE_PEM) <= 0)
        { fprintf(stderr, "cert failed\n"); exit(1); }
    if (SSL_CTX_use_PrivateKey_file(ctx, "test/certs/key.pem", SSL_FILETYPE_PEM) <= 0)
        { fprintf(stderr, "key failed\n"); exit(1); }
    apply_ssl_ctx_opts(ctx, variant);
    return ctx;
}

static SSL_CTX* create_client_ctx(int variant) {
    auto* method = TLS_client_method();
    auto* ctx = SSL_CTX_new(method);
    if (!ctx) { fprintf(stderr, "SSL_CTX_new failed\n"); exit(1); }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_cipher_list(ctx, "AES128-GCM-SHA256");
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    apply_ssl_ctx_opts(ctx, variant);
    return ctx;
}

// ── KTLS enable via key extraction + setsockopt ──────────────────────

static void enable_ktls(SSL* ssl, int fd, bool is_server, int payload_size) {
    (void)payload_size;
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

    if (setsockopt(fd, SOL_TCP, TCP_ULP, "tls", sizeof("tls")) < 0) { perror("TCP_ULP"); exit(1); }
    if (setsockopt(fd, SOL_TLS, TLS_TX, &tx_info, sizeof(tx_info)) < 0) { perror("TLS_TX"); exit(1); }
    if (setsockopt(fd, SOL_TLS, TLS_RX, &rx_info, sizeof(rx_info)) < 0) { perror("TLS_RX"); exit(1); }
}

// ─── per-size result ──────────────────────────────────────────────────

struct run_result {
    size_t  payload = 0;
    uint64_t count   = 0;
    double  p50_us   = 0;
    double  p95_us   = 0;
    double  p99_us   = 0;
    double  avg_us   = 0;
    double  min_us   = 0;
    double  max_us   = 0;
};

static double percentile(const int64_t* sorted, uint64_t n, double p) {
    if (n == 0) return 0;
    double idx = (p / 100.0) * (n - 1);
    uint64_t lo = (uint64_t)idx;
    uint64_t hi = std::min(lo + 1, n - 1);
    return (double)sorted[lo] + (idx - lo) * (double)(sorted[hi] - sorted[lo]);
}

static run_result compute(const sample_buf& buf, size_t payload) {
    run_result r;
    r.payload = payload;
    auto n = buf.size();
    if (n == 0) return r;

    int64_t min_v = buf.samples[0];
    int64_t max_v = buf.samples[0];
    int64_t sum   = 0;
    uint64_t good = 0;
    for (uint64_t i = 0; i < n; ++i) {
        auto v = buf.samples[i];
        if (v == 0) continue;
        ++good;
        sum += v;
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }

    std::vector<int64_t> sorted;
    sorted.reserve(good);
    for (uint64_t i = 0; i < n; ++i)
        if (buf.samples[i] != 0) sorted.push_back(buf.samples[i]);
    std::sort(sorted.begin(), sorted.end());

    r.count  = good;
    if (good == 0) return r;
    r.min_us = (double)min_v / 1e3;
    r.max_us = (double)max_v / 1e3;
    r.avg_us = (double)sum / (double)good / 1e3;
    r.p50_us = percentile(sorted.data(), good, 50) / 1e3;
    r.p95_us = percentile(sorted.data(), good, 95) / 1e3;
    r.p99_us = percentile(sorted.data(), good, 99) / 1e3;
    return r;
}

static void print_header(const char* variant_name, const char* variant_desc) {
    printf("\n");
    printf("═══ KTLS Echo Latency (blocking r/w) ═══════════════════\n");
    printf("  variant: %s\n", variant_name);
    printf("  desc:    %s\n", variant_desc);
    printf("══════════════════════════════════════════════════════\n");
}

static void print_run(size_t idx, size_t total, size_t payload, int port, uint64_t iters, const run_result& r) {
    printf("  [%zu/%zu] payload=%zu port=%d n=%llu ... done  (%llu ok, p50=%.1f us)\n",
           idx, total, payload, port, (unsigned long long)iters,
           (unsigned long long)r.count, r.p50_us);
}

static void print_table(const std::vector<run_result>& results) {
    printf("─────────────────────────────────────────────────────────\n");
    printf("  %-8s  %10s  %8s  %8s  %8s  %8s  %8s\n",
           "payload", "requests", "p50(us)", "p95(us)", "p99(us)",
           "avg(us)", "max(us)");
    printf("─────────────────────────────────────────────────────────\n");
    for (auto& r : results) {
        printf("  %-8zu  %10llu  %8.1f  %8.1f  %8.1f  %8.1f  %8.1f\n",
               r.payload,
               (unsigned long long)r.count,
               r.p50_us, r.p95_us, r.p99_us, r.avg_us, r.max_us);
    }
    printf("─────────────────────────────────────────────────────────\n");
}

// ── Server ─────────────────────────────────────────────────────────────

struct server_state {
    std::thread              thread;
    int                      listen_fd{-1};
    SSL_CTX*                 ctx{nullptr};
    std::atomic<bool>        stop{false};

    void start(int port) {
        listen_fd = tcp_listen(port, 3);  // variant 3 = bigbufs
        ctx = create_server_ctx(3);
        thread = std::thread([this]() {
            int client_fd = tcp_accept(listen_fd, 3);
            SSL* ssl = SSL_new(ctx);
            SSL_set_fd(ssl, client_fd);
            if (SSL_accept(ssl) <= 0) { SSL_free(ssl); close(client_fd); return; }

            // Enable KTLS
            enable_ktls(ssl, client_fd, true, 65536);
            SSL_free(ssl);

            // Raw read/write echo (kernel TLS)
            std::vector<char> buf(65536);
            while (!stop.load(std::memory_order_relaxed)) {
                ssize_t n = read(client_fd, buf.data(), buf.size());
                if (n <= 0) break;
                write(client_fd, buf.data(), (size_t)n);
            }
            close(client_fd);
        });
    }

    void stop_and_join() {
        stop.store(true, std::memory_order_relaxed);
        if (thread.joinable()) thread.join();
        if (listen_fd >= 0) close(listen_fd);
    }

    ~server_state() { stop_and_join(); }
    server_state() = default;
    server_state(const server_state&) = delete;
    server_state& operator=(const server_state&) = delete;
};

// ── Single-size client run ─────────────────────────────────────────────

static run_result run_one_size(const config& cfg, size_t payload_size,
                               int port, uint64_t warmup_n, uint64_t bench_n)
{
    int fd = tcp_connect("127.0.0.1", port, 3);
    auto* ctx = create_client_ctx(3);

    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    if (SSL_connect(ssl) <= 0) { SSL_free(ssl); close(fd); return {}; }

    // Enable KTLS on client too
    enable_ktls(ssl, fd, false, payload_size);
    SSL_free(ssl);

    // Raw blocking read/write (kernel TLS)
    std::vector<char> payload(payload_size, 'x');
    std::vector<char> reply(payload_size);
    sample_buf buf;

    for (uint64_t i = 0; i < warmup_n; ++i) {
        if ((size_t)write(fd, payload.data(), payload_size) != payload_size) break;
        if ((size_t)read(fd, reply.data(), payload_size) != payload_size) break;
    }

    for (uint64_t i = 0; i < bench_n; ++i) {
        auto t0 = now_ns();
        if ((size_t)write(fd, payload.data(), payload_size) != payload_size) { buf.push(0); continue; }
        if ((size_t)read(fd, reply.data(), payload_size) != payload_size) { buf.push(0); continue; }
        buf.push(now_ns() - t0);
    }

    close(fd);
    return compute(buf, payload_size);
}

// ── Main ───────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    auto cfg = parse_args(argc, argv);
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG, nullptr);

    auto& v = VARIANTS[cfg.variant];
    print_header(v.name, v.desc);
    printf("  iterations: %llu   warmup: %llu\n",
           (unsigned long long)cfg.iterations,
           (unsigned long long)cfg.warmup);
    printf("\n");

    std::vector<run_result> results;
    results.reserve(cfg.sizes.size());

    for (size_t idx = 0; idx < cfg.sizes.size(); ++idx) {
        auto payload = cfg.sizes[idx];
        int port = cfg.port + (int)idx;

        uint64_t iters = cfg.iterations;
        uint64_t warm  = cfg.warmup;
        if (payload >= 65536)       { iters = 1000;  warm = 200; }
        else if (payload >= 16384)  { iters = 5000;  warm = 1000; }
        else if (payload >= 4096)   { iters /= 4;    warm /= 2;  }

        server_state server;
        server.start(port);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        printf("  [%zu/%zu] payload=%zu port=%d n=%llu ... ",
               idx + 1, cfg.sizes.size(), payload, port,
               (unsigned long long)iters);
        fflush(stdout);

        auto r = run_one_size(cfg, payload, port, warm, iters);
        server.stop_and_join();

        printf("done  (%llu ok, p50=%.1f us)\n",
               (unsigned long long)r.count, r.p50_us);
        results.push_back(r);
    }

    print_table(results);
    return 0;
}
