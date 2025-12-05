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
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>

using namespace std::chrono_literals;

// Skip TLS tests if OpenSSL is not available
#ifdef HOPE_IO_USE_OPENSSL

class TlsTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_port = 15000 + (std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000);
        
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
        // In a real test environment, you would check for certificate files
        // For now, we'll skip TLS tests as they require proper setup
        return false;
    }
};

// Test TLS acceptor creation
TEST_F(TlsTest, CreateTlsAcceptor) {
    if (!hasTestCertificates()) {
        GTEST_SKIP() << "TLS test certificates not available";
    }
    
    // This would require actual certificate files
    // auto* acceptor = hope::io::create_tls_acceptor("key.pem", "cert.pem");
    // ASSERT_NE(acceptor, nullptr);
    // delete acceptor;
}

// Test TLS stream creation
TEST_F(TlsTest, CreateTlsStream) {
    auto* tcp_stream = hope::io::create_stream();
    auto* tls_stream = hope::io::create_tls_stream(tcp_stream);
    
    // TLS stream creation might fail if OpenSSL is not properly initialized
    // Just check it doesn't crash
    if (tls_stream) {
        delete tls_stream;
    } else {
        delete tcp_stream;
    }
}

// Test TLS websockets stream creation
TEST_F(TlsTest, CreateTlsWebsocketsStream) {
    auto* tcp_stream = hope::io::create_stream();
    auto* ws_stream = hope::io::create_tls_websockets_stream(tcp_stream);
    
    // Websockets stream creation might fail if not properly configured
    // Just check it doesn't crash
    if (ws_stream) {
        delete ws_stream;
    } else {
        delete tcp_stream;
    }
}

#else

// If OpenSSL is not available, skip all TLS tests
class TlsTest : public ::testing::Test {};

TEST_F(TlsTest, TlsNotAvailable) {
    GTEST_SKIP() << "OpenSSL not available, TLS tests skipped";
}

#endif // HOPE_IO_USE_OPENSSL

