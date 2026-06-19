/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * ── Event Loop Benchmark ──────────────────────────────────────────────
 *
 * Single-process benchmark using coroutine streams for clients.
 * Server runs in a background thread, N client coroutines each run
 * in their own thread — one connection per coroutine.
 *
 * Usage:
 *   bench_event_loop [--mode tcp|tls] [--payload 1024] [--connections 50]
 *                    [--duration 5] [--warmup 2] [--port 19300]
 *                    [--cert path] [--key path]
 */

#include "hope-io/net/event_loop.h"
#include "hope-io/net/tls_event_loop.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
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

// ── Platform guard ────────────────────────────────────────────────────

#if !(PLATFORM_LINUX || PLATFORM_APPLE)
int main() {
    printf("bench_event_loop: not supported on this platform\n");
    return 0;
}
#else

// ── Configuration ─────────────────────────────────────────────────────

struct bench_config {
    std::string mode        = "tcp";
    size_t      payload     = 1024;
    int         connections = 40;
    int         duration_s  = 5;
    int         warmup_s    = 2;
    int         port        = 19300;
    std::string cert_path;
    std::string key_path;
};

// ── Per-thread latency buffer ─────────────────────────────────────────

constexpr uint64_t MAX_SAMPLES = 4u << 20;  // 4M per thread

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

static void parse_args(int argc, char** argv, bench_config& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (++i < argc) return argv[i];
            fprintf(stderr, "bench: missing value for %s\n", arg.c_str());
            exit(1);
        };
        if      (arg == "--mode")        cfg.mode        = next();
        else if (arg == "--payload")     cfg.payload     = (size_t)std::stol(next());
        else if (arg == "--connections") cfg.connections = std::stoi(next());
        else if (arg == "--duration")    cfg.duration_s  = std::stoi(next());
        else if (arg == "--warmup")      cfg.warmup_s    = std::stoi(next());
        else if (arg == "--port")        cfg.port        = std::stoi(next());
        else if (arg == "--cert")        cfg.cert_path   = next();
        else if (arg == "--key")         cfg.key_path    = next();
        else { fprintf(stderr, "bench: unknown %s\n", arg.c_str()); exit(1); }
    }
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
    void*       loop   = nullptr;
    bool        is_tls = false;

    template<typename Loop, typename Config, typename Cb>
    void start(bool tls, Loop* l, Config cfg, Cb cb) {
        is_tls = tls; loop = l;
        thread = std::thread([l, cfg = std::move(cfg), cb = std::move(cb)]() mutable {
            l->run(cfg, std::move(cb));
        });
    }

    void stop() {
        if (!loop) return;
        if (is_tls) static_cast<hope::io::tls_event_loop*>(loop)->stop();
        else        static_cast<hope::io::event_loop*>(loop)->stop();
        if (thread.joinable()) thread.join();
        if (is_tls) delete static_cast<hope::io::tls_event_loop*>(loop);
        else        delete static_cast<hope::io::event_loop*>(loop);
        loop = nullptr;
    }

    ~server_guard() { stop(); }
    server_guard() = default;
    server_guard(server_guard&& o) noexcept
        : thread(std::move(o.thread)), loop(o.loop), is_tls(o.is_tls) { o.loop = nullptr; }
    server_guard& operator=(server_guard&& o) noexcept {
        if (this != &o) { stop(); thread = std::move(o.thread); loop = o.loop; is_tls = o.is_tls; o.loop = nullptr; }
        return *this;
    }
    server_guard(const server_guard&) = delete;
    server_guard& operator=(const server_guard&) = delete;
};

static void run_server(const bench_config& cfg, server_guard& sg) {
    hope::io::event_loop::callbacks cb;
    cb.on_connect = [](hope::io::event_loop::connection& c) {
        c.set_state(hope::io::event_loop::connection_state::read);
    };
    cb.on_read = [](hope::io::event_loop::connection& c) {
        c.set_state(hope::io::event_loop::connection_state::write);
    };
    cb.on_write = [](hope::io::event_loop::connection& c) {
        c.set_state(hope::io::event_loop::connection_state::read);
    };
    cb.on_err = [](hope::io::event_loop::connection&, const std::string&) {};

    if (cfg.mode == "tls") {
        auto* loop = hope::io::create_tls_event_loop();
        hope::io::tls_event_loop::tls_config scfg;
        scfg.port       = cfg.port;
        scfg.cert_path  = cfg.cert_path;
        scfg.key_path   = cfg.key_path;
        scfg.max_mutual_connections = 10000;
        scfg.max_accepts_per_tick   = 1000;
        scfg.epoll_timeout = 1000;
        sg.start(true, loop, std::move(scfg), std::move(cb));
    } else {
        auto* loop = hope::io::create_event_loop();
        hope::io::event_loop::config ecfg;
        ecfg.port        = cfg.port;
        ecfg.max_mutual_connections = 10000;
        ecfg.max_accepts_per_tick   = 1000;
        ecfg.epoll_temeout = 1000;
        sg.start(false, loop, std::move(ecfg), std::move(cb));
    }
}

// ── Client worker (blocking stream, one per thread) ───────────────────

static void client_worker(const bench_config& cfg,
                          const std::string& payload,
                          double warmup_end,
                          double bench_end,
                          thread_buf& buf) {
    // Create + connect stream
    hope::io::stream* s = nullptr;
    if (cfg.mode == "tls") {
        auto* tcp = hope::io::create_stream();
        if (!tcp) { buf.errors++; return; }
        s = hope::io::create_tls_stream(tcp);
    } else {
        s = hope::io::create_stream();
    }
    if (!s) { buf.errors++; return; }

    try { s->connect("127.0.0.1", cfg.port); }
    catch (...) { buf.errors++; delete s; return; }

    std::string reply;
    reply.resize(cfg.payload);

    // Warmup
    while (now_sec() < warmup_end) {
        try { s->write(payload.data(), payload.size()); s->read(reply.data(), reply.size()); }
        catch (...) { break; }
    }

    // Measurement
    while (now_sec() < bench_end) {
        double t0 = now_sec();
        try { s->write(payload.data(), payload.size()); s->read(reply.data(), reply.size()); }
        catch (...) { buf.errors++; break; }
        buf.push(elapsed_us(t0, now_sec()));
    }

    s->disconnect();
    delete s;
}

// ── Stats ─────────────────────────────────────────────────────────────

static double percentile(const int64_t* sorted, uint64_t n, double p) {
    if (n == 0) return 0;
    double idx = (p / 100.0) * (n - 1);
    uint64_t lo = (uint64_t)idx;
    uint64_t hi = std::min(lo + 1, n - 1);
    return (double)sorted[lo] + (idx - lo) * (sorted[hi] - sorted[lo]);
}

// ── Main ──────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    hope::io::init();

    bench_config cfg;
    parse_args(argc, argv, cfg);

    if (cfg.mode == "tls") {
        if (cfg.cert_path.empty()) find_file(cfg.cert_path, "cert.pem");
        if (cfg.key_path.empty())  find_file(cfg.key_path,  "key.pem");
        FILE* c = fopen(cfg.cert_path.c_str(), "r");
        FILE* k = fopen(cfg.key_path.c_str(), "r");
        if (!c || !k) {
            fprintf(stderr, "bench: TLS cert/key not found. "
                    "Run: cd test/certs && sh gen_certs.sh\n");
            if (c) fclose(c);
            if (k) fclose(k);
            return 1;
        }
        fclose(c); fclose(k);
        hope::io::init_tls();
    }

    std::string payload(cfg.payload, 'x');

    // Start server
    server_guard server;
    run_server(cfg, server);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    double warmup_end = now_sec() + cfg.warmup_s;
    double bench_end  = warmup_end + cfg.duration_s;

    // Create per-thread buffers and launch client coroutine threads
    std::vector<thread_buf> bufs(cfg.connections);
    std::vector<std::thread> workers;
    workers.reserve(cfg.connections);

    for (int i = 0; i < cfg.connections; ++i) {
        workers.emplace_back([&cfg, &payload, warmup_end, bench_end, &bufs, i]() {
            client_worker(cfg, payload, warmup_end, bench_end, bufs[i]);
        });
    }

    // Join all workers
    for (auto& t : workers) t.join();

    // Aggregate results
    uint64_t total_requests = 0;
    uint64_t total_errors   = 0;
    for (auto& b : bufs) {
        total_requests += b.size();
        total_errors   += b.errors;
    }

    // Collect and sort all latencies
    std::vector<int64_t> all;
    all.reserve(total_requests);
    for (auto& b : bufs) {
        auto n = b.size();
        for (uint64_t i = 0; i < n; ++i) all.push_back(b.samples[i]);
    }
    std::sort(all.begin(), all.end());

    double elapsed = cfg.duration_s;
    double rps     = (double)total_requests / elapsed;
    double p50     = percentile(all.data(), all.size(), 50);
    double p95     = percentile(all.data(), all.size(), 95);
    double p99     = percentile(all.data(), all.size(), 99);
    double max_lat = all.empty() ? 0.0 : (double)all.back();

    printf("\n");
    printf("─── Event Loop Benchmark ─────────────────────────────\n");
    printf("  mode          = %s\n",          cfg.mode.c_str());
    printf("  payload       = %zu bytes\n",   cfg.payload);
    printf("  connections   = %d\n",          cfg.connections);
    printf("  duration      = %d s\n",        cfg.duration_s);
    printf("  warmup        = %d s\n",        cfg.warmup_s);
    printf("──────────────────────────────────────────────────────\n");
    printf("  total_requests = %-12llu\n",    (unsigned long long)total_requests);
    printf("  total_errors   = %-12llu\n",    (unsigned long long)total_errors);
    printf("  wall_time      = %.3f s\n",     elapsed);
    printf("  rps            = %-12.0f\n",    rps);
    printf("  p50_latency    = %.0f us\n",    p50);
    printf("  p95_latency    = %.0f us\n",    p95);
    printf("  p99_latency    = %.0f us\n",    p99);
    printf("  max_latency    = %.0f us\n",    max_lat);
    printf("──────────────────────────────────────────────────────\n");
    printf("\n");

    return total_errors > total_requests ? 1 : 0;
}

#endif // PLATFORM_LINUX || PLATFORM_APPLE
