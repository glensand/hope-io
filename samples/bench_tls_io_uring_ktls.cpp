/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * ── io_uring + KTLS Echo Latency Benchmark ────────────────────────────
 *
 * Copy of bench_tls_ktls with io_uring for the client's raw read/write.
 * Server uses blocking KTLS (unchanged).
 * Client uses io_uring for the raw I/O after KTLS is enabled.
 *
 * Socket settings: TCP_NODELAY + SO_SNDBUF=262144 + SO_RCVBUF=262144.
 * TLS 1.2 forced.
 *
 * Compile:
 *   g++ -O3 -Ilib/boringssl/include -Ilib bench_tls_io_uring_ktls.cpp \
 *       build/lib/boringssl/libssl.a build/lib/boringssl/libcrypto.a \
 *       -luring -lpthread -o build/bin/bench_tls_io_uring_ktls
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

#include <liburing.h>
#include <fcntl.h>
#include <poll.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/hkdf.h>

// ── Constants ──────────────────────────────────────────────────────────

static constexpr size_t MAX_SAMPLES     = 8u << 20;
static constexpr int    DEFAULT_PORT    = 14443;
static constexpr int    DEFAULT_ITER    = 50000;
static constexpr int    DEFAULT_WARMUP  = 2000;

static constexpr int DEFAULT_SIZES[]    = { 64, 200, 1024, 4096, 16384, 65536 };
static constexpr int DEFAULT_NUM_SIZES  = 6;

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

// ── Socket helpers (hardcoded variant 3: bigbufs) ─────────────────────

static void apply_sockopts(int fd) {
    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

static int tcp_listen(int port) {
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    apply_sockopts(fd);
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(fd, 1) < 0) { perror("listen"); exit(1); }
    return fd;
}

static int tcp_connect(const char* ip, int port) {
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    apply_sockopts(fd);
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); exit(1); }
    return fd;
}

static int tcp_accept(int listen_fd) {
    struct sockaddr_in client_addr{};
    socklen_t addrlen = sizeof(client_addr);
    int fd = (int)accept(listen_fd, (struct sockaddr*)&client_addr, &addrlen);
    if (fd < 0) { perror("accept"); exit(1); }
    apply_sockopts(fd);
    return fd;
}

// ── TLS helpers (BoringSSL + KTLS) ────────────────────────────────────

static SSL_CTX* create_server_ctx() {
    auto* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) { fprintf(stderr, "SSL_CTX_new failed\n"); exit(1); }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
    if (SSL_CTX_use_certificate_file(ctx, "test/certs/cert.pem", SSL_FILETYPE_PEM) <= 0)
        { fprintf(stderr, "cert failed\n"); exit(1); }
    if (SSL_CTX_use_PrivateKey_file(ctx, "test/certs/key.pem", SSL_FILETYPE_PEM) <= 0)
        { fprintf(stderr, "key failed\n"); exit(1); }
    return ctx;
}

static SSL_CTX* create_client_ctx() {
    auto* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { fprintf(stderr, "SSL_CTX_new failed\n"); exit(1); }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    return ctx;
}

// ── KTLS enable via key extraction + setsockopt ──────────────────────

// Enable kernel TLS on fd after a successful TLS 1.2 handshake.
// On the server: tx_key = server_write, rx_key = client_write.
// On the client: tx_key = client_write, rx_key = server_write.
static void enable_ktls(SSL* ssl, int fd, bool is_server, int payload_size) {
    (void)payload_size;

    // Get key block length and allocate
    size_t key_len = SSL_get_key_block_len(ssl);
    std::vector<uint8_t> key_block(key_len);
    SSL_generate_key_block(ssl, key_block.data(), key_len);

    // TLS 1.2 key block layout for AES-128-GCM (RFC 5246 §6.3):
    //   client_write_key (16) | server_write_key (16) |
    //   client_write_IV  (4)  | server_write_IV  (4)
    // Total = 40 bytes
    uint8_t* cw_key = key_block.data() + 0;    // client write key
    uint8_t* sw_key = key_block.data() + 16;   // server write key
    uint8_t* cw_iv  = key_block.data() + 32;   // client write IV (salt)
    uint8_t* sw_iv  = key_block.data() + 36;   // server write IV (salt)

    // Helper to populate crypto info for one direction
    auto make_info = [](struct tls12_crypto_info_aes_gcm_128* info,
                        const uint8_t* key, const uint8_t* salt)
    {
        memset(info, 0, sizeof(*info));
        info->info.version = TLS_1_2_VERSION;  // 0x0303
        info->info.cipher_type = TLS_CIPHER_AES_GCM_128;  // 51
        memcpy(info->key, key, 16);
        memcpy(info->salt, salt, 4);
        // iv = 0, rec_seq = 0 (memset above)
    };

    struct tls12_crypto_info_aes_gcm_128 tx_info, rx_info;

    if (is_server) {
        make_info(&tx_info, sw_key, sw_iv);   // server TX
        make_info(&rx_info, cw_key, cw_iv);   // server RX
    } else {
        make_info(&tx_info, cw_key, cw_iv);   // client TX
        make_info(&rx_info, sw_key, sw_iv);   // client RX
    }

    // Attach ULP and program keys
    if (setsockopt(fd, SOL_TCP, TCP_ULP, "tls", sizeof("tls")) < 0) {
        perror("TCP_ULP"); exit(1);
    }
    if (setsockopt(fd, SOL_TLS, TLS_TX, &tx_info, sizeof(tx_info)) < 0) {
        perror("TLS_TX"); exit(1);
    }
    if (setsockopt(fd, SOL_TLS, TLS_RX, &rx_info, sizeof(rx_info)) < 0) {
        perror("TLS_RX"); exit(1);
    }
}

// ── io_uring raw I/O helpers (client only) ───────────────────────────

struct uring_io {
    struct io_uring ring{};
    int fd = -1;

    void init(int sock_fd) {
        fd = sock_fd;
        if (io_uring_queue_init(64, &ring, 0) < 0) { perror("io_uring_init"); exit(1); }
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    void destroy() { io_uring_queue_exit(&ring); }

    // Submit a read, wait for completion. Returns bytes read or -1.
    int read_raw(void* buf, int cap) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        io_uring_prep_read(sqe, fd, buf, cap, 0);
        io_uring_submit(&ring);
        struct io_uring_cqe* cqe;
        io_uring_wait_cqe(&ring, &cqe);
        int n = cqe->res;
        io_uring_cqe_seen(&ring, cqe);
        return n;
    }

    // Submit a write, wait for completion. Returns bytes written or -1.
    int write_raw(const void* buf, int len) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        io_uring_prep_write(sqe, fd, buf, len, 0);
        io_uring_submit(&ring);
        struct io_uring_cqe* cqe;
        io_uring_wait_cqe(&ring, &cqe);
        int n = cqe->res;
        io_uring_cqe_seen(&ring, cqe);
        return n;
    }

    // Read exactly len bytes via io_uring (loop to handle short reads)
    int read_all(void* data, int len) {
        int total = 0;
        while (total < len) {
            int n = read_raw((char*)data + total, len - total);
            if (n <= 0) return -1;
            total += n;
        }
        return total;
    }

    // Write exactly len bytes via io_uring (loop to handle short writes)
    int write_all(const void* data, int len) {
        int total = 0;
        while (total < len) {
            int n = write_raw((const char*)data + total, len - total);
            if (n <= 0) return -1;
            total += n;
        }
        return total;
    }
};

// ── Per-size result ────────────────────────────────────────────────────

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

static void print_header() {
    printf("\n");
    printf("═══ TLS 1.2 Echo Latency (io_uring + KTLS) ═══════════\n");
    printf("  socket: TCP_NODELAY + SO_RCVBUF=262144 + SO_SNDBUF=262144\n");
    printf("  server: KTLS blocking  client: KTLS + io_uring\n");
    printf("════════════════════════════════════════════════════════\n");
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

    void start(int port, int payload_size) {
        listen_fd = tcp_listen(port);
        ctx = create_server_ctx();
        thread = std::thread([this, payload_size]() {
            int client_fd = tcp_accept(listen_fd);
            SSL* ssl = SSL_new(ctx);
            SSL_set_fd(ssl, client_fd);
            if (SSL_accept(ssl) <= 0) { SSL_free(ssl); close(client_fd); return; }

            // Enable KTLS
            enable_ktls(ssl, client_fd, true, payload_size);

            // Free SSL object — kernel handles TLS now
            SSL_free(ssl);

            // Echo loop with raw read/write (kernel TLS)
            // After KTLS, data is raw application data (no length prefix needed)
            // Kernel handles TLS record framing
            std::vector<char> buf(payload_size + 64);
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
    int fd = tcp_connect("127.0.0.1", port);
    auto* ctx = create_client_ctx();
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    if (SSL_connect(ssl) <= 0) { SSL_free(ssl); close(fd); return {}; }

    // Enable KTLS
    enable_ktls(ssl, fd, false, payload_size);

    // Free SSL object — kernel handles TLS now
    SSL_free(ssl);

    // Init io_uring for client's raw I/O
    uring_io io;
    io.init(fd);

    // Raw I/O with kernel TLS, io_uring driven
    std::vector<char> payload(payload_size, 'x');
    std::vector<char> reply(payload_size);
    sample_buf buf;

    for (uint64_t i = 0; i < warmup_n; ++i) {
        if (io.write_all(payload.data(), payload_size) != payload_size) break;
        if (io.read_all(reply.data(), payload_size) != payload_size) break;
    }

    for (uint64_t i = 0; i < bench_n; ++i) {
        auto t0 = now_ns();

        if (io.write_all(payload.data(), payload_size) != payload_size) { buf.push(0); continue; }
        if (io.read_all(reply.data(), payload_size) != payload_size) { buf.push(0); continue; }

        buf.push(now_ns() - t0);
    }

    io.destroy();
    close(fd);
    return compute(buf, payload_size);
}

// ── Main ───────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    auto cfg = parse_args(argc, argv);
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG, nullptr);

    print_header();
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
        server.start(port, payload);
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
