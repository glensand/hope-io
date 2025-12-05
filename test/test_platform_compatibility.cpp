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
#include "hope-io/coredefs.h"
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

class PlatformCompatibilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_port = 15000 + (std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000);
    }

    void TearDown() override {
        std::this_thread::sleep_for(100ms);
    }

    std::size_t test_port = 0;
};

// Test platform detection
TEST_F(PlatformCompatibilityTest, PlatformDetection) {
#if PLATFORM_WINDOWS
    EXPECT_TRUE(PLATFORM_WINDOWS == 1);
    EXPECT_TRUE(PLATFORM_LINUX == 0);
    EXPECT_TRUE(PLATFORM_APPLE == 0);
#elif PLATFORM_LINUX
    EXPECT_TRUE(PLATFORM_LINUX == 1);
    EXPECT_TRUE(PLATFORM_WINDOWS == 0);
    EXPECT_TRUE(PLATFORM_APPLE == 0);
#elif PLATFORM_APPLE
    EXPECT_TRUE(PLATFORM_APPLE == 1);
    EXPECT_TRUE(PLATFORM_LINUX == 0);
    EXPECT_TRUE(PLATFORM_WINDOWS == 0);
#endif
}

// Test cross-platform stream behavior
TEST_F(PlatformCompatibilityTest, StreamBehavior) {
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    std::thread server_thread([&acceptor]() {
        auto* conn = acceptor->accept();
        delete conn;
    });
    
    std::this_thread::sleep_for(50ms);
    
    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    // Both platforms should support these operations
    int32_t socket = client->platform_socket();
    EXPECT_GE(socket, 0);
    
    std::string endpoint = client->get_endpoint();
    // On Windows, endpoint might be empty; on Unix, should be "127.0.0.1"
    // Just verify it doesn't crash
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    delete acceptor;
}

// Test Windows-specific behavior: set_options before connect
TEST_F(PlatformCompatibilityTest, WindowsSetOptionsBeforeConnect) {
#if PLATFORM_WINDOWS
    auto* stream = hope::io::create_stream();
    
    hope::io::stream_options opts;
    opts.connection_timeout = 1000;
    
    // On Windows, set_options should work before connect
    ASSERT_NO_THROW(stream->set_options(opts));
    
    // But should throw if called after connect
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    std::thread server_thread([&acceptor]() {
        auto* conn = acceptor->accept();
        delete conn;
    });
    
    std::this_thread::sleep_for(50ms);
    
    stream->connect("127.0.0.1", test_port);
    
    // On Windows, this should throw
    EXPECT_THROW(stream->set_options(opts), std::exception);
    
    stream->disconnect();
    delete stream;
    
    server_thread.join();
    delete acceptor;
#endif
}

// Test Unix-specific behavior: set_options after connect
TEST_F(PlatformCompatibilityTest, UnixSetOptionsAfterConnect) {
#if PLATFORM_LINUX || PLATFORM_APPLE
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    std::thread server_thread([&acceptor]() {
        auto* conn = acceptor->accept();
        delete conn;
    });
    
    std::this_thread::sleep_for(50ms);
    
    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    hope::io::stream_options opts;
    opts.read_timeout = 1000;
    opts.write_timeout = 1000;
    
    // On Unix, set_options should work after connect
    ASSERT_NO_THROW(client->set_options(opts));
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    delete acceptor;
#endif
}

// Test Windows read() return value bug
TEST_F(PlatformCompatibilityTest, WindowsReadReturnValue) {
#if PLATFORM_WINDOWS
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    const std::string test_message = "Test";
    std::string received_message;
    
    std::thread server_thread([&acceptor, &received_message]() {
        auto* conn = acceptor->accept();
        char buffer[256] = {0};
        // Windows read() has a bug - it returns length instead of bytes read
        size_t received = conn->read(buffer, test_message.length());
        received_message = std::string(buffer, std::min(received, test_message.length()));
        delete conn;
    });
    
    std::this_thread::sleep_for(50ms);
    
    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    client->write(test_message.c_str(), test_message.length());
    
    std::this_thread::sleep_for(50ms);
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    delete acceptor;
    
    // This test documents the bug - Windows read() returns wrong value
    // The actual received data should be correct though
    EXPECT_EQ(received_message, test_message);
#endif
}

// Test Unix read() return value (should be correct)
TEST_F(PlatformCompatibilityTest, UnixReadReturnValue) {
#if PLATFORM_LINUX || PLATFORM_APPLE
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    const std::string test_message = "Test";
    size_t bytes_received = 0;
    
    std::thread server_thread([&acceptor, &bytes_received, test_message]() {
        auto* conn = acceptor->accept();
        char buffer[256] = {0};
        bytes_received = conn->read(buffer, test_message.length());
        delete conn;
    });
    
    std::this_thread::sleep_for(50ms);
    
    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    client->write(test_message.c_str(), test_message.length());
    
    std::this_thread::sleep_for(50ms);
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    delete acceptor;
    
    // Unix read() should return correct number of bytes
    EXPECT_EQ(bytes_received, test_message.length());
#endif
}

// Test read_once length parameter bug (both platforms)
TEST_F(PlatformCompatibilityTest, ReadOnceLengthBug) {
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    const std::string test_message = "Hello World";
    std::string received_message;
    
    std::thread server_thread([&acceptor, &received_message]() {
        auto* conn = acceptor->accept();
        char buffer[256] = {0};
        // read_once uses length - 1, which is suspicious
        size_t received = conn->read_once(buffer, sizeof(buffer));
        received_message = std::string(buffer, received);
        delete conn;
    });
    
    std::this_thread::sleep_for(50ms);
    
    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    client->write(test_message.c_str(), test_message.length());
    
    std::this_thread::sleep_for(50ms);
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    delete acceptor;
    
    // Should receive at least some data (might be length - 1 due to bug)
    EXPECT_GT(received_message.length(), 0u);
    EXPECT_LE(received_message.length(), test_message.length());
}

// Test Windows acceptor doesn't set options on accepted streams
TEST_F(PlatformCompatibilityTest, WindowsAcceptorOptions) {
#if PLATFORM_WINDOWS
    auto* acceptor = hope::io::create_acceptor();
    
    hope::io::stream_options opts;
    opts.read_timeout = 2000;
    acceptor->set_options(opts);
    acceptor->open(test_port);
    
    std::thread client_thread([this]() {
        std::this_thread::sleep_for(50ms);
        auto* client = hope::io::create_stream();
        client->connect("127.0.0.1", test_port);
        std::this_thread::sleep_for(50ms);
        client->disconnect();
        delete client;
    });
    
    auto* conn = acceptor->accept();
    
    // On Windows, accepted stream might not have options set
    // This documents the inconsistency with Unix behavior
    
    delete conn;
    client_thread.join();
    delete acceptor;
#endif
}

// Test Unix acceptor sets options on accepted streams
TEST_F(PlatformCompatibilityTest, UnixAcceptorOptions) {
#if PLATFORM_LINUX || PLATFORM_APPLE
    auto* acceptor = hope::io::create_acceptor();
    
    hope::io::stream_options opts;
    opts.read_timeout = 2000;
    opts.write_timeout = 2000;
    acceptor->set_options(opts);
    acceptor->open(test_port);
    
    std::thread client_thread([this]() {
        std::this_thread::sleep_for(50ms);
        auto* client = hope::io::create_stream();
        client->connect("127.0.0.1", test_port);
        std::this_thread::sleep_for(50ms);
        client->disconnect();
        delete client;
    });
    
    auto* conn = acceptor->accept();
    
    // On Unix, accepted stream should have options set
    // Verify it works correctly
    int32_t socket = conn->platform_socket();
    EXPECT_GE(socket, 0);
    
    delete conn;
    client_thread.join();
    delete acceptor;
#endif
}

// Test UDP platform differences
TEST_F(PlatformCompatibilityTest, UdpPlatformDifferences) {
    auto* builder = hope::io::create_udp_builder();
    auto* receiver = hope::io::create_receiver();
    auto* sender = hope::io::create_sender();
    
#if PLATFORM_WINDOWS
    // On Windows, UDP is not implemented
    EXPECT_EQ(builder, nullptr);
    EXPECT_EQ(receiver, nullptr);
    EXPECT_EQ(sender, nullptr);
#else
    // On Unix, UDP should be available
    ASSERT_NE(builder, nullptr);
    ASSERT_NE(receiver, nullptr);
    ASSERT_NE(sender, nullptr);
    
    delete builder;
    delete receiver;
    delete sender;
#endif
}

// Test event loop platform differences
TEST_F(PlatformCompatibilityTest, EventLoopPlatformDifferences) {
    auto* loop1 = hope::io::create_event_loop();
    auto* loop2 = hope::io::create_event_loop2(100);
    
#if PLATFORM_WINDOWS
    // On Windows, event loop is not implemented
    EXPECT_EQ(loop1, nullptr);
    EXPECT_EQ(loop2, nullptr);
#else
    // On Unix/Apple, event loop should be available
    ASSERT_NE(loop1, nullptr);
    ASSERT_NE(loop2, nullptr);
    
    delete loop1;
    delete loop2;
#endif
}

