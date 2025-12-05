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

using namespace std::chrono_literals;

class ErrorHandlingTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_port = 15000 + (std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000);
    }

    void TearDown() override {
        std::this_thread::sleep_for(100ms);
    }

    std::size_t test_port = 0;
};

// Test reading from disconnected stream
TEST_F(ErrorHandlingTest, ReadFromDisconnectedStream) {
    auto* stream = hope::io::create_stream();
    
    // Try to read from unconnected stream - should throw or return 0
    char buffer[256];
    try {
        size_t received = stream->read_once(buffer, sizeof(buffer));
        // If we get here, it returned 0 (which is acceptable)
        EXPECT_EQ(received, 0u);
    } catch (const std::exception&) {
        // Throwing is also acceptable
    }
    
    delete stream;
}

// Test writing to disconnected stream
TEST_F(ErrorHandlingTest, WriteToDisconnectedStream) {
    auto* stream = hope::io::create_stream();
    
    const char* data = "test";
    // Writing to unconnected stream should throw
    EXPECT_THROW(stream->write(data, 4), std::exception);
    
    delete stream;
}

// Test reading after server disconnect
TEST_F(ErrorHandlingTest, ReadAfterServerDisconnect) {
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    std::thread server_thread([&acceptor]() {
        auto* conn = acceptor->accept();
        // Immediately disconnect
        delete conn;
    });
    
    std::this_thread::sleep_for(50ms);
    
    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    std::this_thread::sleep_for(50ms);
    
    // Try to read after server disconnected
    char buffer[256];
    try {
        size_t received = client->read_once(buffer, sizeof(buffer));
        // Should return 0 or throw
        EXPECT_LE(received, 0u);
    } catch (const std::exception&) {
        // Throwing is acceptable
    }
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    delete acceptor;
}

// Test writing to closed connection
TEST_F(ErrorHandlingTest, WriteToClosedConnection) {
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    std::thread server_thread([&acceptor]() {
        auto* conn = acceptor->accept();
        delete conn; // Close immediately
    });
    
    std::this_thread::sleep_for(50ms);
    
    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    std::this_thread::sleep_for(100ms); // Wait for server to close
    
    // Try to write after server closed
    const char* data = "test";
    try {
        client->write(data, 4);
        // If we get here, the write might have succeeded before close
        // or the error wasn't detected immediately
    } catch (const std::exception&) {
        // Throwing is acceptable
    }
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    delete acceptor;
}

// Test double disconnect
TEST_F(ErrorHandlingTest, DoubleDisconnect) {
    auto* stream = hope::io::create_stream();
    
    // Disconnecting twice should be safe
    ASSERT_NO_THROW(stream->disconnect());
    ASSERT_NO_THROW(stream->disconnect());
    
    delete stream;
}

// Test invalid port number
TEST_F(ErrorHandlingTest, InvalidPort) {
    auto* acceptor = hope::io::create_acceptor();
    
    // Port 0 is typically invalid
    EXPECT_THROW(acceptor->open(0), std::exception);
    
    delete acceptor;
}

// Test very large port number
TEST_F(ErrorHandlingTest, VeryLargePort) {
    auto* acceptor = hope::io::create_acceptor();
    
    // Port > 65535 is invalid
    EXPECT_THROW(acceptor->open(70000), std::exception);
    
    delete acceptor;
}

// Test connection timeout
TEST_F(ErrorHandlingTest, ConnectionTimeout) {
    auto* client = hope::io::create_stream();
    
    hope::io::stream_options opts;
    opts.connection_timeout = 100; // Very short timeout
    client->set_options(opts);
    
    // Try to connect to a port that's not listening
    // Should timeout or throw quickly
    EXPECT_THROW(client->connect("127.0.0.1", 1), std::exception);
    
    delete client;
}

// Test read timeout
TEST_F(ErrorHandlingTest, ReadTimeout) {
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    std::thread server_thread([&acceptor]() {
        auto* conn = acceptor->accept();
        // Don't send any data, just wait
        std::this_thread::sleep_for(500ms);
        delete conn;
    });
    
    std::this_thread::sleep_for(50ms);
    
    auto* client = hope::io::create_stream();
    
    hope::io::stream_options opts;
    opts.read_timeout = 100; // Short timeout
    client->set_options(opts);
    
    client->connect("127.0.0.1", test_port);
    
    // Try to read with timeout
    char buffer[256];
    try {
        size_t received = client->read_once(buffer, sizeof(buffer));
        // Should return 0 or throw on timeout
        EXPECT_LE(received, 0u);
    } catch (const std::exception&) {
        // Throwing on timeout is acceptable
    }
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    delete acceptor;
}

// Test write timeout
TEST_F(ErrorHandlingTest, WriteTimeout) {
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    std::thread server_thread([&acceptor]() {
        auto* conn = acceptor->accept();
        // Don't read any data, let buffer fill up
        std::this_thread::sleep_for(500ms);
        delete conn;
    });
    
    std::this_thread::sleep_for(50ms);
    
    auto* client = hope::io::create_stream();
    
    hope::io::stream_options opts;
    opts.write_timeout = 100; // Short timeout
    client->set_options(opts);
    
    client->connect("127.0.0.1", test_port);
    
    // Try to write large amount of data to fill buffer
    std::vector<char> large_data(1024 * 1024, 'A');
    try {
        client->write(large_data.data(), large_data.size());
        // Might succeed or timeout depending on buffer size
    } catch (const std::exception&) {
        // Throwing on timeout is acceptable
    }
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    delete acceptor;
}

// Test null pointer handling in factory functions
TEST_F(ErrorHandlingTest, NullPointerHandling) {
    // These should not crash, but behavior is implementation-defined
    auto* stream = hope::io::create_stream(0);
    if (stream) {
        delete stream;
    }
}

// Test memory cleanup on exception
TEST_F(ErrorHandlingTest, MemoryCleanupOnException) {
    auto* acceptor = hope::io::create_acceptor();
    
    try {
        acceptor->open(test_port);
        // Open again on same port should fail
        acceptor->open(test_port);
    } catch (const std::exception&) {
        // Exception caught, should still be able to delete
    }
    
    // Should be able to clean up even after exception
    ASSERT_NO_THROW(delete acceptor);
}

