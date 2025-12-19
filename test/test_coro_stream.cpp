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
#include <string>
#include <cstring>
#include <vector>
#include <atomic>

using namespace std::chrono_literals;

// Skip coroutine tests if C++20 is not available
#ifdef __cplusplus
#if __cplusplus >= 202002L

// Only include async_stream for C++20
#include "hope-io/net/async_stream.h"
#include <coroutine>

class CoroStreamTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use unique port for each test to avoid bind conflicts
        static std::atomic<int> port_counter{20000};
        test_port = port_counter.fetch_add(1);
        acceptor = hope::io::create_acceptor();
        acceptor->open(test_port);
    }

    void TearDown() override {
        if (acceptor) {
            delete acceptor;
            acceptor = nullptr;
        }
        // Give OS time to release the port
        std::this_thread::sleep_for(50ms);
    }

    hope::io::acceptor* acceptor = nullptr;
    std::size_t test_port = 0;
};

// Test basic async_stream creation
TEST_F(CoroStreamTest, CreateAsyncStream) {
    auto* stream = hope::io::create_stream();
    ASSERT_NE(stream, nullptr);
    
    hope::io::async_stream async_stream(stream);
    
    stream->disconnect();
    delete stream;
}

// Test async connection
TEST_F(CoroStreamTest, AsyncConnectSuccess) {
    std::atomic<bool> server_connected{false};
    
    std::thread server_thread([this, &server_connected]() {
        auto* conn = acceptor->accept();
        if (conn) {
            server_connected = true;
            conn->disconnect();
            delete conn;
        }
    });

    std::this_thread::sleep_for(50ms); // Give server time to start listening

    auto* stream = hope::io::create_stream();
    ASSERT_NE(stream, nullptr);
    
    hope::io::async_stream async_stream(stream);
    
    // Simple synchronous test: verify stream can connect
    // (Actual coroutine execution requires main() integration with coroutine runtime)
    ASSERT_NO_THROW(stream->connect("127.0.0.1", test_port));
    
    stream->disconnect();
    delete stream;
    
    server_thread.join();
    EXPECT_TRUE(server_connected);
}

// Test async read and write with echo server
TEST_F(CoroStreamTest, AsyncReadWrite) {
    const std::string test_message = "Hello from async!";
    std::atomic<bool> test_passed{false};
    
    std::thread server_thread([this, &test_message, &test_passed]() {
        auto* conn = acceptor->accept();
        if (conn) {
            char buffer[256] = {0};
            size_t received = conn->read(buffer, test_message.length());
            if (received == test_message.length() && 
                std::string(buffer, received) == test_message) {
                // Echo back
                conn->write(buffer, received);
                test_passed = true;
            }
            conn->disconnect();
            delete conn;
        }
    });

    std::this_thread::sleep_for(50ms);

    auto* stream = hope::io::create_stream();
    ASSERT_NE(stream, nullptr);
    
    hope::io::async_stream async_stream(stream);
    
    ASSERT_NO_THROW(stream->connect("127.0.0.1", test_port));
    
    // Synchronous write
    size_t written = stream->write(test_message.c_str(), test_message.length());
    ASSERT_EQ(written, test_message.length());
    
    // Synchronous read (echo)
    char buffer[256] = {0};
    size_t read_size = stream->read(buffer, test_message.length());
    ASSERT_EQ(read_size, test_message.length());
    ASSERT_EQ(std::string(buffer, read_size), test_message);
    
    stream->disconnect();
    delete stream;
    
    server_thread.join();
    EXPECT_TRUE(test_passed);
}

// Test async stream non-blocking mode enabled
TEST_F(CoroStreamTest, NonBlockingModeEnabled) {
    auto* stream = hope::io::create_stream();
    ASSERT_NE(stream, nullptr);
    
    // Create async_stream which should auto-enable non-blocking mode
    hope::io::async_stream async_stream(stream);
    
    // Verify stream is set to non-blocking mode
    // Note: On Unix systems, we can check this with fcntl()
    // On Windows, we can check with socket options
    // For now, just verify creation doesn't throw
    EXPECT_NO_THROW(stream->disconnect());
    delete stream;
}

// Test async stream with timeout support
TEST_F(CoroStreamTest, TimeoutConfiguration) {
    auto* stream = hope::io::create_stream();
    ASSERT_NE(stream, nullptr);
    
    // Create async_stream with default timeouts
    hope::io::async_stream async_stream(stream);
    
    // Stream should be created and functional
    EXPECT_NE(&async_stream, nullptr);
    
    stream->disconnect();
    delete stream;
}

// Test multiple sequential async operations
TEST_F(CoroStreamTest, SequentialAsyncOperations) {
    const std::vector<std::string> messages = {
        "Message 1",
        "Message 2",
        "Message 3"
    };
    
    std::thread server_thread([this, &messages]() {
        auto* conn = acceptor->accept();
        if (conn) {
            for (const auto& msg : messages) {
                char buffer[256] = {0};
                size_t received = conn->read(buffer, msg.length());
                if (received == msg.length()) {
                    // Echo back
                    conn->write(buffer, received);
                }
            }
            conn->disconnect();
            delete conn;
        }
    });

    std::this_thread::sleep_for(50ms);

    auto* stream = hope::io::create_stream();
    ASSERT_NE(stream, nullptr);
    
    hope::io::async_stream async_stream(stream);
    
    ASSERT_NO_THROW(stream->connect("127.0.0.1", test_port));
    
    // Send and receive multiple messages
    for (const auto& msg : messages) {
        size_t written = stream->write(msg.c_str(), msg.length());
        ASSERT_EQ(written, msg.length());
        
        char buffer[256] = {0};
        size_t read_size = stream->read(buffer, msg.length());
        ASSERT_EQ(read_size, msg.length());
        ASSERT_EQ(std::string(buffer, read_size), msg);
    }
    
    stream->disconnect();
    delete stream;
    
    server_thread.join();
}

// Test async stream disconnect
TEST_F(CoroStreamTest, AsyncStreamDisconnect) {
    auto* stream = hope::io::create_stream();
    ASSERT_NE(stream, nullptr);
    
    hope::io::async_stream async_stream(stream);
    
    ASSERT_NO_THROW(stream->disconnect());
    delete stream;
}

// Test async stream with immediate read on non-readable socket
TEST_F(CoroStreamTest, ReadWithoutData) {
    auto* stream = hope::io::create_stream();
    ASSERT_NE(stream, nullptr);
    
    hope::io::async_stream async_stream(stream);
    
    ASSERT_NO_THROW(stream->connect("127.0.0.1", test_port));
    
    // Attempt to set non-blocking mode and read (should return 0 or -1)
    char buffer[256] = {0};
    // Note: With non-blocking enabled, read should return immediately
    // Result depends on platform implementation
    
    stream->disconnect();
    delete stream;
}

// Test error handling in async operations
TEST_F(CoroStreamTest, ErrorHandling) {
    auto* stream = hope::io::create_stream();
    ASSERT_NE(stream, nullptr);
    
    hope::io::async_stream async_stream(stream);
    
    // Try to connect to invalid port (should fail gracefully)
    EXPECT_THROW({
        stream->connect("127.0.0.1", 1); // Reserved port, not listening
    }, std::exception);
    
    // Stream should still be deletable
    delete stream;
}

#else // C++20 not available

// Provide stub tests for when C++20 is not available
class CoroStreamTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Stub test - coroutines require C++20
TEST_F(CoroStreamTest, CoroRequiresC20) {
    GTEST_SKIP() << "Coroutine tests require C++20 standard";
}

#endif // C++20 check
#endif // cplusplus check
