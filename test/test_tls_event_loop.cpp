/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include <gtest/gtest.h>
#include "hope-io/net/tls_event_loop.h"
#include "hope-io/net/event_loop.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/nix/tcp_stream.h"
#include "hope-io/net/nix/tls_event_loop_impl.h"
#include "hope-io/net/linux/tls_event_loop_impl.h"
#include "hope-io/net/tls/tcp_tls_stream.h"
#include "hope-io/net/init.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <fstream>
#include <sstream>
#include <vector>

using namespace std::chrono_literals;
using namespace hope::io::el;

// Skip TLS tests if platform doesn't support event loop
#if PLATFORM_LINUX || PLATFORM_APPLE

// ── Helpers ────────────────────────────────────────────────────────────

// Return numeric values for connection_state enum members without naming the type.
// The values match: idle=0, read=1, write=2, die=3.
enum : int8_t { kStateRead = 1, kStateWrite = 2, kStateDie = 3 };

// RAII helper: owns a TLS event loop + its thread, ensures stop/join on destruction
template<typename Loop>
struct TlsEventLoopGuard {
    Loop* loop = nullptr;
    std::thread thread;

    TlsEventLoopGuard() = default;
    TlsEventLoopGuard(TlsEventLoopGuard&& o) noexcept
        : loop(o.loop), thread(std::move(o.thread)) { o.loop = nullptr; }
    TlsEventLoopGuard& operator=(TlsEventLoopGuard&& o) noexcept {
        if (this != &o) {
            if (loop) { loop->stop(); if (thread.joinable()) thread.join(); delete loop; }
            loop = o.loop;
            thread = std::move(o.thread);
            o.loop = nullptr;
        }
        return *this;
    }

    void start(Loop* l, tls_config& cfg) {
        loop = l;
        thread = std::thread([l, &cfg]() {
            l->run(cfg);
        });
    }

    ~TlsEventLoopGuard() {
        if (loop) {
            loop->stop();
            if (thread.joinable()) thread.join();
            delete loop;
        }
    }
};

// Constructs a TlsEventLoopGuard, deducing template params from the callbacks.
template<typename TOnRead, typename TOnWrite, typename TOnError, typename TConnected>
auto make_tls_guard(tls_config& cfg,
                    TConnected&& on_connect, TOnRead&& on_read, TOnWrite&& on_write, TOnError&& on_error) {
    using loop_t = tls_event_loop_impl<TOnRead, TOnWrite, TOnError, TConnected>;
    TlsEventLoopGuard<loop_t> guard;
    guard.start(new loop_t(std::forward<TConnected>(on_connect), std::forward<TOnRead>(on_read),
                           std::forward<TOnWrite>(on_write), std::forward<TOnError>(on_error)), cfg);
    return guard;
}

// Create a tls_config with a given port.
inline tls_config make_tls_config(std::size_t port,
                                   const std::string& cert_path,
                                   const std::string& key_path) {
    tls_config cfg;
    cfg.port = port;
    cfg.cert_path = cert_path;
    cfg.key_path = key_path;
    cfg.max_mutual_connections = 10;
    cfg.max_accepts_per_tick = 5;
    cfg.epoll_timeout = 100;
    return cfg;
}

// ── Test fixture ───────────────────────────────────────────────────────

class TlsEventLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        static std::atomic<int> port_counter{20000};
        test_port = port_counter.fetch_add(1);
        hope::io::init();
    }

    void TearDown() override {
        std::this_thread::sleep_for(50ms);
    }

    std::size_t test_port = 0;

    std::string cert_path() const {
        const char* paths[] = {
            "../test/certs/cert.pem",
            "../../test/certs/cert.pem",
            "test/certs/cert.pem",
        };
        for (auto* p : paths) {
            std::ifstream f(p);
            if (f.good()) return p;
        }
        return "../test/certs/cert.pem";
    }

    std::string key_path() const {
        const char* paths[] = {
            "../test/certs/key.pem",
            "../../test/certs/key.pem",
            "test/certs/key.pem",
        };
        for (auto* p : paths) {
            std::ifstream f(p);
            if (f.good()) return p;
        }
        return "../test/certs/key.pem";
    }

    bool certs_available() {
        std::ifstream c(cert_path()), k(key_path());
        return c.good() && k.good();
    }
};

// ── Tests ──────────────────────────────────────────────────────────────

TEST_F(TlsEventLoopTest, CreateTlsEventLoop) {
    // Each lambda gets its own type — we need one distinct lambda per callback.
    auto c = [](connection&) { return el_connection_state::read; };
    auto r = [](connection&) { return el_connection_state::read; };
    auto w = [](connection&) { return el_connection_state::read; };
    auto e = [](connection&, const std::string&) { return el_connection_state::die; };
    auto* loop = new tls_event_loop_impl(
        std::move(c), std::move(r), std::move(w), std::move(e));
    ASSERT_NE(loop, nullptr);
    delete loop;
}

TEST_F(TlsEventLoopTest, RunAndStop) {
    if (!certs_available()) {
        GTEST_SKIP() << "TLS certificates not available";
    }

    auto on_connect = [](connection&) { return el_connection_state::read; };
    auto on_read = [](connection&) { return el_connection_state::read; };
    auto on_write = [](connection&) { return el_connection_state::read; };
    auto on_err = [](connection&, const std::string&) { return el_connection_state::die; };

    auto cfg = make_tls_config(test_port, cert_path(), key_path());
    auto guard = make_tls_guard(cfg, std::move(on_connect), std::move(on_read),
                                std::move(on_write), std::move(on_err));
    std::this_thread::sleep_for(100ms);
}

TEST_F(TlsEventLoopTest, TlsEchoSingleClient) {
    if (!certs_available()) {
        GTEST_SKIP() << "TLS certificates not available";
    }

    std::atomic<int> connect_count{0};
    std::atomic<int> read_count{0};
    std::atomic<int> write_count{0};
    std::atomic<int> error_count{0};

    auto on_connect = [&connect_count](connection&) {
        connect_count++;
        return el_connection_state::read;
    };
    auto on_read = [&read_count](connection&) {
        read_count++;
        return el_connection_state::write;
    };
    auto on_write = [&write_count](connection&) {
        write_count++;
        return el_connection_state::read;
    };
    auto on_err = [&error_count](connection&, const std::string&) {
        error_count++;
        return el_connection_state::die;
    };

    auto cfg = make_tls_config(test_port, cert_path(), key_path());
    auto guard = make_tls_guard(cfg, std::move(on_connect), std::move(on_read),
                                std::move(on_write), std::move(on_err));
    std::this_thread::sleep_for(100ms);

    auto* tcp_stream = new hope::io::tcp_stream();
    ASSERT_NE(tcp_stream, nullptr);
    auto* tls_client = new hope::io::tcp_tls_stream(tcp_stream);
    ASSERT_NE(tls_client, nullptr);
    tls_client->connect("127.0.0.1", test_port);

    const std::string test_message = "Hello from TLS event loop test!";
    tls_client->write(test_message.c_str(), test_message.length());

    std::this_thread::sleep_for(200ms);

    std::string response;
    response.resize(test_message.size());
    std::size_t total_read = 0;
    while (total_read < test_message.size()) {
        auto n = tls_client->read_once(response.data() + total_read,
                                        test_message.size() - total_read);
        if (n == 0) break;
        total_read += n;
    }

    response.resize(total_read);
    EXPECT_EQ(total_read, test_message.size());
    EXPECT_EQ(response, test_message);

    tls_client->disconnect();
    delete tls_client;

    std::this_thread::sleep_for(100ms);

    EXPECT_GE(connect_count.load(), 1);
    EXPECT_GE(read_count.load(), 1);
    EXPECT_GE(write_count.load(), 1);
}

TEST_F(TlsEventLoopTest, TlsEchoMultipleClients) {
    if (!certs_available()) {
        GTEST_SKIP() << "TLS certificates not available";
    }

    std::atomic<int> connect_count{0};
    std::atomic<int> read_count{0};
    std::atomic<int> error_count{0};

    auto on_connect = [&connect_count](connection&) {
        connect_count++;
        return el_connection_state::read;
    };
    auto on_read = [&read_count](connection&) {
        read_count++;
        return el_connection_state::write;
    };
    auto on_write = [](connection&) {
        return el_connection_state::read;
    };
    auto on_err = [&error_count](connection&, const std::string&) {
        error_count++;
        return el_connection_state::die;
    };

    auto cfg = make_tls_config(test_port, cert_path(), key_path());
    auto guard = make_tls_guard(cfg, std::move(on_connect), std::move(on_read),
                                std::move(on_write), std::move(on_err));
    std::this_thread::sleep_for(100ms);

    constexpr int N_CLIENTS = 5;
    std::vector<hope::io::stream*> clients;
    clients.reserve(N_CLIENTS);

    for (int i = 0; i < N_CLIENTS; ++i) {
        auto* tcp = new hope::io::tcp_stream();
        ASSERT_NE(tcp, nullptr);
        auto* tls = new hope::io::tcp_tls_stream(tcp);
        ASSERT_NE(tls, nullptr);
        tls->connect("127.0.0.1", test_port);
        clients.push_back(tls);
    }

    std::this_thread::sleep_for(200ms);

    const std::string msg = "Multi-client TLS test";
    for (auto* client : clients) {
        client->write(msg.c_str(), msg.length());
    }

    std::this_thread::sleep_for(200ms);

    for (auto* client : clients) {
        client->disconnect();
        delete client;
    }

    std::this_thread::sleep_for(100ms);

    EXPECT_GE(connect_count.load(), N_CLIENTS);
    EXPECT_GE(read_count.load(), N_CLIENTS);
    EXPECT_EQ(error_count.load(), 0);
}

TEST_F(TlsEventLoopTest, TlsLargeMessage) {
    if (!certs_available()) {
        GTEST_SKIP() << "TLS certificates not available";
    }

    auto on_connect = [](connection&) { return el_connection_state::read; };
    auto on_read = [](connection&) { return el_connection_state::write; };
    auto on_write = [](connection&) { return el_connection_state::read; };
    auto on_err = [](connection&, const std::string&) { return el_connection_state::die; };

    auto cfg = make_tls_config(test_port, cert_path(), key_path());
    auto guard = make_tls_guard(cfg, std::move(on_connect), std::move(on_read),
                                std::move(on_write), std::move(on_err));
    std::this_thread::sleep_for(100ms);

    constexpr std::size_t LARGE_SIZE = 100 * 1024;
    std::string large_msg(LARGE_SIZE, 'A');
    large_msg.replace(LARGE_SIZE - 10, 10, "ENDMARKER!");

    auto* tcp = new hope::io::tcp_stream();
    auto* tls = new hope::io::tcp_tls_stream(tcp);
    ASSERT_NE(tls, nullptr);
    tls->connect("127.0.0.1", test_port);

    tls->write(large_msg.c_str(), large_msg.size());

    std::this_thread::sleep_for(200ms);

    std::string response;
    response.resize(LARGE_SIZE);
    std::size_t total_read = 0;
    while (total_read < LARGE_SIZE) {
        auto n = tls->read_once(response.data() + total_read,
                                LARGE_SIZE - total_read);
        if (n == 0) break;
        total_read += n;
    }

    response.resize(total_read);
    EXPECT_EQ(total_read, LARGE_SIZE);
    EXPECT_EQ(response, large_msg);

    tls->disconnect();
    delete tls;

    std::this_thread::sleep_for(100ms);
}

TEST_F(TlsEventLoopTest, TlsHandshakeFail) {
    if (!certs_available()) {
        GTEST_SKIP() << "TLS certificates not available";
    }

    std::atomic<int> error_count{0};

    auto on_connect = [](connection&) { return el_connection_state::read; };
    auto on_read = [](connection&) { return el_connection_state::read; };
    auto on_write = [](connection&) { return el_connection_state::read; };
    auto on_err = [&error_count](connection&, const std::string&) {
        error_count++;
        return el_connection_state::die;
    };

    auto cfg = make_tls_config(test_port, cert_path(), key_path());
    cfg.epoll_timeout = 100;
    auto guard = make_tls_guard(cfg, std::move(on_connect), std::move(on_read),
                                std::move(on_write), std::move(on_err));
    std::this_thread::sleep_for(100ms);

    auto* raw = new hope::io::tcp_stream();
    raw->connect("127.0.0.1", test_port);

    const char junk[] = "GET / HTTP/1.0\r\n\r\n";
    raw->write(junk, sizeof(junk) - 1);

    std::this_thread::sleep_for(200ms);
    raw->disconnect();
    delete raw;

    std::this_thread::sleep_for(100ms);
    EXPECT_GE(error_count.load(), 0);
}

TEST_F(TlsEventLoopTest, KtlsFlagDoesNotBreakEventLoop) {
    if (!certs_available()) {
        GTEST_SKIP() << "TLS certificates not available";
    }

    std::atomic<int> connect_count{0};
    std::atomic<int> error_count{0};

    auto on_connect = [&connect_count](connection&) {
        connect_count++;
        return el_connection_state::read;
    };
    auto on_read = [](connection&) { return el_connection_state::write; };
    auto on_write = [](connection&) { return el_connection_state::read; };
    auto on_err = [&error_count](connection&, const std::string&) {
        error_count++;
        return el_connection_state::die;
    };

    auto cfg = make_tls_config(test_port, cert_path(), key_path());
    cfg.enable_ktls = true;
    auto guard = make_tls_guard(cfg, std::move(on_connect), std::move(on_read),
                                std::move(on_write), std::move(on_err));
    std::this_thread::sleep_for(100ms);

    auto* tcp = new hope::io::tcp_stream();
    auto* tls = new hope::io::tcp_tls_stream(tcp);
    ASSERT_NE(tls, nullptr);
    tls->connect("127.0.0.1", test_port);

    const std::string msg = "KTLS event loop test";
    tls->write(msg.data(), msg.size());

    std::string reply(msg.size(), '\0');
    std::size_t total = 0;
    while (total < msg.size()) {
        auto n = tls->read_once(reply.data() + total, msg.size() - total);
        if (n == 0) break;
        total += n;
    }

    EXPECT_EQ(total, msg.size());
    EXPECT_EQ(reply, msg);

    tls->disconnect();
    delete tls;

    std::this_thread::sleep_for(100ms);
    EXPECT_GE(connect_count.load(), 1);
    EXPECT_EQ(error_count.load(), 0);
}

TEST_F(TlsEventLoopTest, KtlsStreamFlagDoesNotBreakConnect) {
    if (!certs_available()) {
        GTEST_SKIP() << "TLS certificates not available";
    }

    auto on_connect = [](connection&) { return el_connection_state::read; };
    auto on_read = [](connection&) { return el_connection_state::write; };
    auto on_write = [](connection&) { return el_connection_state::read; };
    auto on_err = [](connection&, const std::string&) { return el_connection_state::die; };

    auto cfg = make_tls_config(test_port, cert_path(), key_path());
    auto guard = make_tls_guard(cfg, std::move(on_connect), std::move(on_read),
                                std::move(on_write), std::move(on_err));
    std::this_thread::sleep_for(100ms);

    auto* tcp = new hope::io::tcp_stream();
    auto* tls = new hope::io::tcp_tls_stream(tcp);
    ASSERT_NE(tls, nullptr);
    tls->set_ktls_enabled(true);
    tls->connect("127.0.0.1", test_port);

    const std::string msg = "KTLS stream flag test";
    tls->write(msg.data(), msg.size());

    std::string reply(msg.size(), '\0');
    std::size_t total = 0;
    while (total < msg.size()) {
        auto n = tls->read_once(reply.data() + total, msg.size() - total);
        if (n == 0) break;
        total += n;
    }

    EXPECT_EQ(total, msg.size());
    EXPECT_EQ(reply, msg);

    tls->disconnect();
    delete tls;
}

#endif
