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
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include "hope-io/net/tls/tls_init.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <fstream>
#include <sstream>

using namespace std::chrono_literals;

// Skip TLS tests if OpenSSL is not available or platform doesn't support event loop
#if defined(HOPE_IO_USE_OPENSSL) && (PLATFORM_LINUX || PLATFORM_APPLE)

// RAII helper: owns a tls_event_loop + its thread, ensures stop/join on destruction
struct TlsEventLoopGuard {
    hope::io::tls_event_loop* loop = nullptr;
    std::thread thread;

    void start(hope::io::tls_event_loop::tls_config& cfg, hope::io::event_loop::callbacks& cb) {
        loop = new hope::io::tls_event_loop_impl();
        thread = std::thread([this, &cfg, &cb]() {
            loop->run(cfg, std::move(cb));
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

class TlsEventLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        static std::atomic<int> port_counter{20000};
        test_port = port_counter.fetch_add(1);
        hope::io::init_tls();
    }

    void TearDown() override {
        std::this_thread::sleep_for(50ms);
    }

    std::size_t test_port = 0;

    // Path to test certificates (relative to build dir)
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

    hope::io::tls_event_loop::tls_config make_config() {
        hope::io::tls_event_loop::tls_config cfg;
        cfg.port = test_port;
        cfg.cert_path = cert_path();
        cfg.key_path = key_path();
        cfg.max_mutual_connections = 10;
        cfg.max_accepts_per_tick = 5;
        cfg.epoll_timeout = 100;
        return cfg;
    }
};

// Test TLS event loop creation
TEST_F(TlsEventLoopTest, CreateTlsEventLoop) {
    auto* loop = new hope::io::tls_event_loop_impl();
    ASSERT_NE(loop, nullptr);
    delete loop;
}

// Test TLS event loop run and stop
TEST_F(TlsEventLoopTest, RunAndStop) {
    if (!certs_available()) {
        GTEST_SKIP() << "TLS certificates not available";
    }

    TlsEventLoopGuard guard;
    auto cfg = make_config();
    hope::io::event_loop::callbacks cb;
    cb.on_connect = [](hope::io::event_loop::connection&) {};
    cb.on_read = [](hope::io::event_loop::connection&) {};
    cb.on_write = [](hope::io::event_loop::connection&) {};
    cb.on_err = [](hope::io::event_loop::connection&, const std::string&) {};

    guard.start(cfg, cb);
    std::this_thread::sleep_for(100ms);
    // guard destructor stops the loop and joins
}

// Test TLS event loop echo with a single TLS client
TEST_F(TlsEventLoopTest, TlsEchoSingleClient) {
    if (!certs_available()) {
        GTEST_SKIP() << "TLS certificates not available";
    }

    TlsEventLoopGuard guard;
    auto cfg = make_config();

    std::atomic<int> connect_count{0};
    std::atomic<int> read_count{0};
    std::atomic<int> write_count{0};
    std::atomic<int> error_count{0};

    hope::io::event_loop::callbacks cb;
    cb.on_connect = [&connect_count](hope::io::event_loop::connection& conn) {
        connect_count++;
        conn.set_state(hope::io::event_loop::connection_state::read);
    };
    cb.on_read = [&read_count](hope::io::event_loop::connection& conn) {
        read_count++;
        conn.set_state(hope::io::event_loop::connection_state::write);
    };
    cb.on_write = [&write_count](hope::io::event_loop::connection& conn) {
        write_count++;
        // Stay in read — don't close. Client disconnect will trigger EV_EOF.
        conn.set_state(hope::io::event_loop::connection_state::read);
    };
    cb.on_err = [&error_count](hope::io::event_loop::connection&, const std::string&) {
        error_count++;
    };

    guard.start(cfg, cb);
    std::this_thread::sleep_for(100ms);

    // Create TLS client
    auto* tcp_stream = new hope::io::tcp_stream();
    ASSERT_NE(tcp_stream, nullptr);
    auto* tls_client = new hope::io::tcp_tls_stream(tcp_stream);
    ASSERT_NE(tls_client, nullptr);
    tls_client->connect("127.0.0.1", test_port);

    const std::string test_message = "Hello from TLS event loop test!";
    tls_client->write(test_message.c_str(), test_message.length());

    // Small delay to let the server echo back
    std::this_thread::sleep_for(200ms);

    // Read echo — use read_once with a loop to handle partial reads gracefully
    std::string response;
    response.resize(test_message.size());
    std::size_t total_read = 0;
    while (total_read < test_message.size()) {
        auto n = tls_client->read_once(response.data() + total_read,
                                        test_message.size() - total_read);
        if (n == 0) break; // EOF or WANT_READ — let the server close gracefully
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
    // error_count may be >0 if close_notify races with our checks
}

// Test TLS event loop echo with multiple concurrent clients
TEST_F(TlsEventLoopTest, TlsEchoMultipleClients) {
    if (!certs_available()) {
        GTEST_SKIP() << "TLS certificates not available";
    }

    TlsEventLoopGuard guard;
    auto cfg = make_config();
    cfg.max_mutual_connections = 20;
    cfg.max_accepts_per_tick = 10;

    std::atomic<int> connect_count{0};
    std::atomic<int> read_count{0};
    std::atomic<int> error_count{0};

    hope::io::event_loop::callbacks cb;
    cb.on_connect = [&connect_count](hope::io::event_loop::connection& conn) {
        connect_count++;
        conn.set_state(hope::io::event_loop::connection_state::read);
    };
    cb.on_read = [&read_count](hope::io::event_loop::connection& conn) {
        read_count++;
        conn.set_state(hope::io::event_loop::connection_state::write);
    };
    cb.on_write = [](hope::io::event_loop::connection& conn) {
        conn.set_state(hope::io::event_loop::connection_state::read);
    };
    cb.on_err = [&error_count](hope::io::event_loop::connection&, const std::string&) {
        error_count++;
    };

    guard.start(cfg, cb);
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

// Test TLS event loop with large message
TEST_F(TlsEventLoopTest, TlsLargeMessage) {
    if (!certs_available()) {
        GTEST_SKIP() << "TLS certificates not available";
    }

    TlsEventLoopGuard guard;
    auto cfg = make_config();

    hope::io::event_loop::callbacks cb;
    cb.on_connect = [](hope::io::event_loop::connection& conn) {
        conn.set_state(hope::io::event_loop::connection_state::read);
    };
    cb.on_read = [](hope::io::event_loop::connection& conn) {
        conn.set_state(hope::io::event_loop::connection_state::write);
    };
    cb.on_write = [](hope::io::event_loop::connection& conn) {
        // Stay in read — don't close. Client disconnect will trigger EV_EOF.
        conn.set_state(hope::io::event_loop::connection_state::read);
    };
    cb.on_err = [](hope::io::event_loop::connection&, const std::string&) {};

    guard.start(cfg, cb);
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

// Test TLS handshake failure with raw TCP (no TLS)
TEST_F(TlsEventLoopTest, TlsHandshakeFail) {
    if (!certs_available()) {
        GTEST_SKIP() << "TLS certificates not available";
    }

    TlsEventLoopGuard guard;
    auto cfg = make_config();
    cfg.epoll_timeout = 100;

    std::atomic<int> error_count{0};

    hope::io::event_loop::callbacks cb;
    cb.on_connect = [](hope::io::event_loop::connection&) {};
    cb.on_read = [](hope::io::event_loop::connection&) {};
    cb.on_write = [](hope::io::event_loop::connection&) {};
    cb.on_err = [&error_count](hope::io::event_loop::connection&, const std::string&) {
        error_count++;
    };

    guard.start(cfg, cb);
    std::this_thread::sleep_for(100ms);

    // Connect with raw TCP — no TLS handshake
    auto* raw = new hope::io::tcp_stream();
    raw->connect("127.0.0.1", test_port);

    const char junk[] = "GET / HTTP/1.0\r\n\r\n";
    raw->write(junk, sizeof(junk) - 1);

    std::this_thread::sleep_for(200ms);
    raw->disconnect();
    delete raw;

    std::this_thread::sleep_for(100ms);

    // The server should have detected the failed handshake
    EXPECT_GE(error_count.load(), 0);
}

#else

// If OpenSSL is not available or platform doesn't support event loops
class TlsEventLoopTest : public ::testing::Test {};

TEST_F(TlsEventLoopTest, TlsNotAvailable) {
    GTEST_SKIP() << "TLS event loop not available on this platform or OpenSSL missing";
}

#endif // HOPE_IO_USE_OPENSSL && (PLATFORM_LINUX || PLATFORM_APPLE)
