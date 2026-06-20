/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * ── TLS Echo Latency Benchmark ───────────────────────────────────
 *
 * Measures round-trip TLS latency: client sends 200 bytes, server
 * echoes them back. Latency = client's begin_write → client's
 * end_read (ns). Server does read→write echo loop.
 *
 * Build with OpenSSL or BoringSSL to compare.
 *
 * Usage:
 *   bench_tls_latency [-n iterations] [-p port] [-w warmup]
 *                      [-c cert_path] [-k key_path]
 *
 * Example:
 *   bench_tls_latency -n 50000 -p 14443
 */

#include "hope-io/net/init.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/nix/tcp_stream.h"
#include "hope-io/net/tls/tcp_tls_stream.h"
#include "hope-io/net/tls/tls_init.h"
#include "hope-io/net/tls/tls_acceptor_impl.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <string>

// ── Library label (set via -DTLS_LIB_LABEL="..." at build time) ──────
#ifndef TLS_LIB_LABEL
#  define TLS_LIB_LABEL "OpenSSL"
#endif

// ── Constants ──────────────────────────────────────────────────────────

static constexpr size_t PAYLOAD_SIZE   = 200;
static constexpr size_t MAX_SAMPLES    = 8u << 20;   // 8M samples
static constexpr int    DEFAULT_PORT   = 14443;
static constexpr int    DEFAULT_ITER   = 50000;
static constexpr int    DEFAULT_WARMUP = 2000;

// ── Clock ─────────────────────────────────────────────────────────────

static inline int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

// ── Sample buffer (heap-allocated, cache-line padded) ──────────────────

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
    int         port        = DEFAULT_PORT;
    uint64_t    iterations  = DEFAULT_ITER;
    uint64_t    warmup      = DEFAULT_WARMUP;
    std::string cert_path;
    std::string key_path;
};

static config parse_args(int argc, char** argv) {
    config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (++i < argc) return argv[i];
            fprintf(stderr, "missing value for %s\n", a.c_str());
            exit(1);
        };
        if      (a == "-n" || a == "--iterations") cfg.iterations = (uint64_t)std::stol(next());
        else if (a == "-p" || a == "--port")       cfg.port       = std::stoi(next());
        else if (a == "-w" || a == "--warmup")     cfg.warmup     = (uint64_t)std::stol(next());
        else if (a == "-c" || a == "--cert")       cfg.cert_path  = next();
        else if (a == "-k" || a == "--key")        cfg.key_path   = next();
        else { fprintf(stderr, "unknown arg: %s\n", a.c_str()); exit(1); }
    }
    return cfg;
}

static bool find_file(std::string& out, const char* name) {
    const char* dirs[] = { "", "../test/certs/", "../../test/certs/", "test/certs/" };
    for (auto* d : dirs) {
        std::string p = std::string(d) + name;
        if (FILE* f = fopen(p.c_str(), "r")) { fclose(f); out = p; return true; }
    }
    return false;
}

// ── Stats / report ─────────────────────────────────────────────────────

struct result {
    std::string label;
    uint64_t    count   = 0;
    uint64_t    errors  = 0;
    int64_t     min_ns  = 0;
    int64_t     max_ns  = 0;
    double      avg_ns  = 0.0;
    double      p50_ns  = 0.0;
    double      p95_ns  = 0.0;
    double      p99_ns  = 0.0;
};

static double percentile(const int64_t* sorted, uint64_t n, double p) {
    if (n == 0) return 0;
    double idx = (p / 100.0) * (n - 1);
    uint64_t lo = (uint64_t)idx;
    uint64_t hi = std::min(lo + 1, n - 1);
    return (double)sorted[lo] + (idx - lo) * (double)(sorted[hi] - sorted[lo]);
}

static void print_report(const result& r) {
    printf("\n");
    printf("═══ TLS Echo Latency Report ══════════════════════════\n");
    printf("  library       = %s\n",        r.label.c_str());
    printf("  payload       = %zu bytes\n",  PAYLOAD_SIZE);
    printf("  requests      = %llu\n",       (unsigned long long)r.count);
    printf("  errors        = %llu\n",       (unsigned long long)r.errors);
    printf("──────────────────────────────────────────────────────\n");
    printf("  min_latency   = %'10lld ns  (%'6.2f us)\n",
           (long long)r.min_ns, (double)r.min_ns / 1e3);
    printf("  p50_latency   = %'10.0f ns  (%'6.2f us)\n",
           r.p50_ns, r.p50_ns / 1e3);
    printf("  p95_latency   = %'10.0f ns  (%'6.2f us)\n",
           r.p95_ns, r.p95_ns / 1e3);
    printf("  p99_latency   = %'10.0f ns  (%'6.2f us)\n",
           r.p99_ns, r.p99_ns / 1e3);
    printf("  max_latency   = %'10lld ns  (%'6.2f us)\n",
           (long long)r.max_ns, (double)r.max_ns / 1e3);
    printf("  avg_latency   = %'10.0f ns  (%'6.2f us)\n",
           r.avg_ns, r.avg_ns / 1e3);
    printf("══════════════════════════════════════════════════════\n");
    printf("\n");
}

static result compute_result(const sample_buf& buf, std::string label) {
    result r;
    r.label = std::move(label);
    auto n = buf.size();
    if (n == 0) return r;

    int64_t sum = 0;
    r.min_ns = buf.samples[0];
    r.max_ns = buf.samples[0];
    for (uint64_t i = 0; i < n; ++i) {
        auto v = buf.samples[i];
        if (v == 0) { r.errors++; continue; }
        sum += v;
        if (v < r.min_ns) r.min_ns = v;
        if (v > r.max_ns) r.max_ns = v;
    }

    std::vector<int64_t> sorted;
    sorted.reserve(n);
    for (uint64_t i = 0; i < n; ++i)
        if (buf.samples[i] != 0) sorted.push_back(buf.samples[i]);
    std::sort(sorted.begin(), sorted.end());

    r.count  = (uint64_t)sorted.size();
    if (r.count == 0) return r;
    r.avg_ns = (double)sum / (double)r.count;
    r.p50_ns = percentile(sorted.data(), r.count, 50);
    r.p95_ns = percentile(sorted.data(), r.count, 95);
    r.p99_ns = percentile(sorted.data(), r.count, 99);
    r.max_ns = sorted.back();
    return r;
}

// ── Echo server ────────────────────────────────────────────────────────
// Accepts one client, echoes back every 200B payload it receives.

struct server_state {
    std::thread                    thread;
    hope::io::tls_acceptor_impl*   acceptor = nullptr;
    std::atomic<bool>              stop{false};

    void start(int port, const std::string& cert, const std::string& key) {
        acceptor = new hope::io::tls_acceptor_impl(key, cert);
        acceptor->open(port);
        thread = std::thread([this]() {
            try {
                auto* conn = acceptor->accept();
                if (!conn) return;
                char buf[PAYLOAD_SIZE];
                while (!stop.load(std::memory_order_relaxed)) {
                    auto n = conn->read(buf, PAYLOAD_SIZE);
                    if (n == 0) break;
                    conn->write(buf, n);
                }
                delete conn;
            } catch (const std::exception&) {
                // client disconnect → expected
            }
        });
    }

    void stop_and_join() {
        stop.store(true, std::memory_order_relaxed);
        if (thread.joinable()) thread.join();
        if (acceptor) {
            acceptor->close();
            delete acceptor;
            acceptor = nullptr;
        }
    }

    ~server_state() { stop_and_join(); }
    server_state() = default;
    server_state(const server_state&) = delete;
    server_state& operator=(const server_state&) = delete;
};

// ── Client benchmark ───────────────────────────────────────────────────
// Connect, warmup, then measure round-trip: write → read (echo).

static void run_client(const config& cfg, const std::string& payload,
                       uint64_t warmup_n, uint64_t bench_n, sample_buf& buf)
{
    auto* tcp = new hope::io::tcp_stream();
    auto* tls = new hope::io::tcp_tls_stream(
        static_cast<hope::io::tcp_stream*>(tcp));
    try {
        tls->connect("127.0.0.1", cfg.port);
    } catch (const std::exception& e) {
        fprintf(stderr, "connect failed: %s\n", e.what());
        delete tls;
        return;
    }

    char reply[PAYLOAD_SIZE];

    // warmup
    for (uint64_t i = 0; i < warmup_n; ++i) {
        try {
            tls->write(payload.data(), PAYLOAD_SIZE);
            tls->read(reply, PAYLOAD_SIZE);
        } catch (const std::exception&) { break; }
    }

    // measurement: round-trip = write begin → read end
    for (uint64_t i = 0; i < bench_n; ++i) {
        auto t0 = now_ns();
        try {
            tls->write(payload.data(), PAYLOAD_SIZE);
            tls->read(reply, PAYLOAD_SIZE);
        } catch (const std::exception&) {
            buf.push(0);
            continue;
        }
        buf.push(now_ns() - t0);
    }

    delete tls;
}

// ── Main ───────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    hope::io::init();
    auto cfg = parse_args(argc, argv);

    if (cfg.cert_path.empty()) find_file(cfg.cert_path, "cert.pem");
    if (cfg.key_path.empty())  find_file(cfg.key_path,  "key.pem");
    if (cfg.cert_path.empty() || cfg.key_path.empty()) {
        fprintf(stderr, "TLS cert/key not found\n");
        return 1;
    }

    hope::io::init_tls();

    std::string payload(PAYLOAD_SIZE, 'x');

    server_state server;
    server.start(cfg.port, cfg.cert_path, cfg.key_path);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    sample_buf buf;
    run_client(cfg, payload, cfg.warmup, cfg.iterations, buf);
    server.stop_and_join();

    auto r = compute_result(buf, TLS_LIB_LABEL);
    print_report(r);
    return r.errors > r.count ? 1 : 0;
}
