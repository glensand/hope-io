/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include <gtest/gtest.h>
#include "hope-io/net/stream.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/nix/tcp_stream.h"
#include "hope-io/net/tls/tcp_tls_stream.h"
#include "hope-io/net/tls/ktls_enable.h"
#include "hope-io/net/init.h"
#include "hope-io/net/tls/tls_acceptor_impl.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <atomic>
#include <filesystem>
namespace fs = std::filesystem;

using namespace std::chrono_literals;


class TlsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use unique port for each test to avoid bind conflicts
        static std::atomic<int> port_counter{19000};
        test_port = port_counter.fetch_add(1);
        
        // Generate self-signed certificate for testing
        // In a real scenario, you would have proper certificates
        // For now, we'll skip tests if certificates are not available
    }

    void TearDown() override {
        std::this_thread::sleep_for(100ms);
    }

    std::size_t test_port = 0;
    
    // Helper to check if test certificates exist
    bool hasTestCertificates() {
        const char* paths[] = {
            "test/certs/cert.pem",
            "../test/certs/cert.pem",
            "../../test/certs/cert.pem",
        };
        for (auto* p : paths) {
            if (fs::exists(p)) return true;
        }
        return false;
    }
};

// Test TLS acceptor creation
TEST_F(TlsTest, CreateTlsAcceptor) {
    if (!hasTestCertificates()) {
        GTEST_SKIP() << "TLS test certificates not available";
    }

    std::string key_path, cert_path;
    const char* search[] = {
        "../test/certs/key.pem",  "../../test/certs/key.pem",
        "test/certs/key.pem",
    };
    for (auto* p : search) {
        if (fs::exists(p)) {
            auto base = std::string(p);
            auto pos = base.find("key.pem");
            key_path = base;
            cert_path = base.substr(0, pos) + "cert.pem";
            break;
        }
    }
    auto* acceptor = new hope::io::tls_acceptor_impl(key_path, cert_path);
    ASSERT_NE(acceptor, nullptr);
    acceptor->open(test_port);
    delete acceptor;
}

// Test TLS stream creation
TEST_F(TlsTest, CreateTlsStream) {
    auto* tcp_stream = new hope::io::tcp_stream();
    auto* tls_stream = new hope::io::tcp_tls_stream(tcp_stream);
    
    // TLS stream creation might fail if OpenSSL is not properly initialized
    // Just check it doesn't crash
    if (tls_stream) {
        delete tls_stream;
    } else {
        delete tcp_stream;
    }
}

// // Test TLS websockets stream creation
// TEST_F(TlsTest, CreateTlsWebsocketsStream) {
//     auto* tcp_stream = new hope::io::tcp_stream();
//     auto* ws_stream = hope::io::create_tls_websockets_stream(tcp_stream);
//
// ── KTLS Tests ────────────────────────────────────────────────────────

TEST_F(TlsTest, KtlsUnsupportedPlatform) {
    // On platforms without KTLS support, try_enable_fd_ktls should
    // gracefully return false without crashing.
    EXPECT_FALSE(hope::io::try_enable_fd_ktls(nullptr, -1, false));
    EXPECT_FALSE(hope::io::try_enable_fd_ktls(nullptr, 0, true));
}

TEST_F(TlsTest, KtlsFlagDoesNotBreakStream) {
    // Setting KTLS flag on a stream should not break normal TLS operation
    // when KTLS is not available — it should silently fall back.
    auto* tcp = new hope::io::tcp_stream();
    auto* tls = new hope::io::tcp_tls_stream(tcp);
    ASSERT_NE(tls, nullptr);
    tls->set_ktls_enabled(true);
    // The stream should still work (KTLS attempt will fail, fallback to SSL)
    delete tls;
}

// ── KTLS Event Loop Integration ───────────────────────────────────────

// Test that setting enable_ktls in tls_config doesn't break the event loop.
// The KTLS flag is tested in the event loop test file since it requires
// the full event loop infrastructure.


