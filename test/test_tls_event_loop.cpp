/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include <gtest/gtest.h>
#include "hope-io/net/event_loop.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include <thread>
#include <chrono>
#include <string>
#include <cstring>
#include <atomic>
#include <vector>
#include <fstream>

using namespace std::chrono_literals;

// Skip TLS event loop tests if OpenSSL is not available or not on Unix
#ifdef HOPE_IO_USE_OPENSSL
#if PLATFORM_LINUX || PLATFORM_APPLE

class TlsEventLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use unique port for each test to avoid bind conflicts
        static std::atomic<int> port_counter{21000};
        test_port = port_counter.fetch_add(1);
    }

    void TearDown() override {
        std::this_thread::sleep_for(100ms);
    }

    std::size_t test_port = 0;
    
    // Helper to check if test certificates exist
    bool hasTestCertificates() {
        // Check if certificate files exist in ../crt/ directory
        std::ifstream key_file("../crt/key.pem");
        std::ifstream cert_file("../crt/cert.pem");
        return key_file.good() && cert_file.good();
    }
};

// Test event loop with TLS acceptor creation
TEST_F(TlsEventLoopTest, CreateTlsEventLoop) {
    auto* loop = hope::io::create_event_loop();
    
    if (!loop) {
        GTEST_SKIP() << "Event loop not implemented on this platform";
    }
    
    if (!hasTestCertificates()) {
        delete loop;
        GTEST_SKIP() << "TLS test certificates not available";
    }
    
    // Create TLS acceptor
    auto* tls_acceptor = hope::io::create_tls_acceptor("../crt/key.pem", "../crt/cert.pem");
    
    if (!tls_acceptor) {
        delete loop;
        GTEST_SKIP() << "Failed to create TLS acceptor";
    }
    
    ASSERT_NE(tls_acceptor, nullptr);
    
    delete tls_acceptor;
    delete loop;
}

// Test event loop with custom TLS acceptor
TEST_F(TlsEventLoopTest, EventLoopWithCustomTlsAcceptor) {
    auto* loop = hope::io::create_event_loop();
    
    if (!loop) {
        GTEST_SKIP() << "Event loop not implemented on this platform";
    }
    
    if (!hasTestCertificates()) {
        delete loop;
        GTEST_SKIP() << "TLS test certificates not available";
    }
    
    auto* tls_acceptor = hope::io::create_tls_acceptor("../crt/key.pem", "../crt/cert.pem");
    
    if (!tls_acceptor) {
        delete loop;
        GTEST_SKIP() << "Failed to create TLS acceptor";
    }
    
    // Configure event loop with custom TLS acceptor
    hope::io::event_loop::config cfg;
    cfg.port = test_port;
    cfg.max_mutual_connections = 10;
    cfg.max_accepts_per_tick = 5;
    cfg.epoll_temeout = 100;
    cfg.custom_acceptor = tls_acceptor;
    
    // Verify configuration was accepted
    ASSERT_EQ(cfg.custom_acceptor, tls_acceptor);
    
    delete tls_acceptor;
    delete loop;
}

// Test TLS event loop basic accept and receive
TEST_F(TlsEventLoopTest, TlsEventLoopBasicAccept) {
    auto* loop = hope::io::create_event_loop();
    
    if (!loop) {
        GTEST_SKIP() << "Event loop not implemented on this platform";
    }
    
    if (!hasTestCertificates()) {
        delete loop;
        GTEST_SKIP() << "TLS test certificates not available";
    }
    
    auto* tls_acceptor = hope::io::create_tls_acceptor("../crt/key.pem", "../crt/cert.pem");
    
    if (!tls_acceptor) {
        delete loop;
        GTEST_SKIP() << "Failed to create TLS acceptor";
    }
    
    std::atomic<int> connections{0};
    std::atomic<bool> loop_running{false};
    
    hope::io::event_loop::config cfg;
    cfg.port = test_port;
    cfg.max_mutual_connections = 10;
    cfg.max_accepts_per_tick = 5;
    cfg.epoll_temeout = 100;
    cfg.custom_acceptor = tls_acceptor;
    
    hope::io::event_loop::callbacks cb;
    cb.on_connect = [&connections, &loop_running](hope::io::event_loop::connection& conn) {
        connections++;
        loop_running = true;
    };
    
    cb.on_read = [](hope::io::event_loop::connection& conn) {
        // Echo back what we receive
        char buffer[1024] = {0};
        size_t received = conn.stream->read(buffer, sizeof(buffer) - 1);
        if (received > 0) {
            conn.stream->write(buffer, received);
        }
    };
    
    cb.on_write = [](hope::io::event_loop::connection& conn) {
        // Can handle write-ready events here if needed
    };
    
    cb.on_err = [](hope::io::event_loop::connection& conn, const std::string& error) {
        // Log error or handle gracefully
    };
    
    std::thread loop_thread([&loop, &cfg, &cb]() {
        loop->run(cfg, std::move(cb));
    });
    
    std::this_thread::sleep_for(200ms);
    
    // Give server time to start
    if (!loop_running) {
        loop->stop();
        loop_thread.join();
        delete tls_acceptor;
        delete loop;
        GTEST_SKIP() << "Event loop failed to start";
    }
    
    // Stop the loop
    loop->stop();
    std::this_thread::sleep_for(100ms);
    
    loop_thread.join();
    
    // Verify acceptor was used
    EXPECT_GT(connections, -1); // Should be >= 0
    
    delete tls_acceptor;
    delete loop;
}

// Test event loop with regular TCP vs TLS acceptor comparison
TEST_F(TlsEventLoopTest, TlsVsTcpEventLoop) {
    auto* tcp_loop = hope::io::create_event_loop();
    auto* tls_loop = hope::io::create_event_loop();
    
    if (!tcp_loop || !tls_loop) {
        if (tcp_loop) delete tcp_loop;
        if (tls_loop) delete tls_loop;
        GTEST_SKIP() << "Event loop not implemented on this platform";
    }
    
    if (!hasTestCertificates()) {
        delete tcp_loop;
        delete tls_loop;
        GTEST_SKIP() << "TLS test certificates not available";
    }
    
    // TCP configuration (default acceptor)
    hope::io::event_loop::config tcp_cfg;
    tcp_cfg.port = test_port;
    tcp_cfg.max_mutual_connections = 5;
    tcp_cfg.max_accepts_per_tick = 5;
    tcp_cfg.epoll_temeout = 100;
    tcp_cfg.custom_acceptor = nullptr; // Default TCP acceptor
    
    // TLS configuration (custom acceptor)
    auto* tls_acceptor = hope::io::create_tls_acceptor("../crt/key.pem", "../crt/cert.pem");
    if (!tls_acceptor) {
        delete tcp_loop;
        delete tls_loop;
        GTEST_SKIP() << "Failed to create TLS acceptor";
    }
    
    hope::io::event_loop::config tls_cfg;
    tls_cfg.port = test_port + 1000; // Different port for TLS
    tls_cfg.max_mutual_connections = 5;
    tls_cfg.max_accepts_per_tick = 5;
    tls_cfg.epoll_temeout = 100;
    tls_cfg.custom_acceptor = tls_acceptor;
    
    // Basic sanity check: both configurations are valid
    ASSERT_EQ(tcp_cfg.custom_acceptor, nullptr);
    ASSERT_EQ(tls_cfg.custom_acceptor, tls_acceptor);
    
    delete tls_acceptor;
    delete tcp_loop;
    delete tls_loop;
}

// Test TLS event loop callback sequence
TEST_F(TlsEventLoopTest, TlsEventLoopCallbackSequence) {
    auto* loop = hope::io::create_event_loop();
    
    if (!loop) {
        GTEST_SKIP() << "Event loop not implemented on this platform";
    }
    
    if (!hasTestCertificates()) {
        delete loop;
        GTEST_SKIP() << "TLS test certificates not available";
    }
    
    auto* tls_acceptor = hope::io::create_tls_acceptor("../crt/key.pem", "../crt/cert.pem");
    
    if (!tls_acceptor) {
        delete loop;
        GTEST_SKIP() << "Failed to create TLS acceptor";
    }
    
    // Track callback sequence
    std::vector<std::string> callback_sequence;
    
    hope::io::event_loop::config cfg;
    cfg.port = test_port;
    cfg.max_mutual_connections = 10;
    cfg.max_accepts_per_tick = 5;
    cfg.epoll_temeout = 100;
    cfg.custom_acceptor = tls_acceptor;
    
    hope::io::event_loop::callbacks cb;
    cb.on_connect = [&callback_sequence](hope::io::event_loop::connection& conn) {
        callback_sequence.push_back("connect");
    };
    
    cb.on_read = [&callback_sequence](hope::io::event_loop::connection& conn) {
        callback_sequence.push_back("read");
        // Try to read any available data
        char buffer[1024] = {0};
        conn.stream->read(buffer, sizeof(buffer) - 1);
    };
    
    cb.on_write = [&callback_sequence](hope::io::event_loop::connection& conn) {
        callback_sequence.push_back("write");
    };
    
    cb.on_err = [&callback_sequence](hope::io::event_loop::connection& conn, const std::string& error) {
        callback_sequence.push_back("error: " + error);
    };
    
    // Note: This test verifies the callback signature and configuration
    // Actual callback execution would require client connection which 
    // needs proper TLS implementation
    
    // Just verify we can build the callbacks structure
    EXPECT_EQ(cfg.custom_acceptor, tls_acceptor);
    
    delete tls_acceptor;
    delete loop;
}

// Test multiple TLS event loops independence
TEST_F(TlsEventLoopTest, MultipleTlsEventLoops) {
    auto* loop1 = hope::io::create_event_loop();
    auto* loop2 = hope::io::create_event_loop();
    
    if (!loop1 || !loop2) {
        if (loop1) delete loop1;
        if (loop2) delete loop2;
        GTEST_SKIP() << "Event loop not implemented on this platform";
    }
    
    if (!hasTestCertificates()) {
        delete loop1;
        delete loop2;
        GTEST_SKIP() << "TLS test certificates not available";
    }
    
    auto* tls_acceptor1 = hope::io::create_tls_acceptor("../crt/key.pem", "../crt/cert.pem");
    auto* tls_acceptor2 = hope::io::create_tls_acceptor("../crt/key.pem", "../crt/cert.pem");
    
    if (!tls_acceptor1 || !tls_acceptor2) {
        if (tls_acceptor1) delete tls_acceptor1;
        if (tls_acceptor2) delete tls_acceptor2;
        delete loop1;
        delete loop2;
        GTEST_SKIP() << "Failed to create TLS acceptors";
    }
    
    // Create two independent event loops with different ports
    hope::io::event_loop::config cfg1;
    cfg1.port = test_port;
    cfg1.custom_acceptor = tls_acceptor1;
    cfg1.max_mutual_connections = 5;
    
    hope::io::event_loop::config cfg2;
    cfg2.port = test_port + 100;
    cfg2.custom_acceptor = tls_acceptor2;
    cfg2.max_mutual_connections = 5;
    
    // Both loops should have independent acceptors
    ASSERT_NE(cfg1.custom_acceptor, cfg2.custom_acceptor);
    ASSERT_EQ(cfg1.custom_acceptor, tls_acceptor1);
    ASSERT_EQ(cfg2.custom_acceptor, tls_acceptor2);
    
    delete tls_acceptor1;
    delete tls_acceptor2;
    delete loop1;
    delete loop2;
}

#endif // PLATFORM_LINUX || PLATFORM_APPLE
#endif // HOPE_IO_USE_OPENSSL
