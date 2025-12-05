/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include <gtest/gtest.h>
#include "hope-io/net/stream.h"
#include "hope-io/net/udp_builder.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include <thread>
#include <chrono>
#include <cstring>

using namespace std::chrono_literals;

class UdpTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_port = 15000 + (std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000);
    }

    void TearDown() override {
        std::this_thread::sleep_for(100ms);
    }

    std::size_t test_port = 0;
};

// Test UDP builder creation
TEST_F(UdpTest, CreateUdpBuilder) {
    auto* builder = hope::io::create_udp_builder();
    
#if PLATFORM_WINDOWS
    // On Windows, UDP builder is not implemented
    EXPECT_EQ(builder, nullptr);
#else
    ASSERT_NE(builder, nullptr);
    delete builder;
#endif
}

// Test UDP builder init (Unix only)
#if PLATFORM_LINUX || PLATFORM_APPLE
TEST_F(UdpTest, UdpBuilderInit) {
    auto* builder = hope::io::create_udp_builder();
    if (builder) {
        ASSERT_NO_THROW(builder->init(test_port));
        int32_t socket = builder->platform_socket();
        EXPECT_GE(socket, 0);
        delete builder;
    }
}
#endif

// Test UDP receiver creation
TEST_F(UdpTest, CreateReceiver) {
    auto* receiver = hope::io::create_receiver();
    
#if PLATFORM_WINDOWS
    // On Windows, receiver is not implemented
    EXPECT_EQ(receiver, nullptr);
#else
    ASSERT_NE(receiver, nullptr);
    delete receiver;
#endif
}

// Test UDP sender creation
TEST_F(UdpTest, CreateSender) {
    auto* sender = hope::io::create_sender();
    
#if PLATFORM_WINDOWS
    // On Windows, sender is not implemented
    EXPECT_EQ(sender, nullptr);
#else
    ASSERT_NE(sender, nullptr);
    delete sender;
#endif
}

// Test UDP send/receive (Unix only)
#if PLATFORM_LINUX || PLATFORM_APPLE
TEST_F(UdpTest, SendReceive) {
    auto* builder = hope::io::create_udp_builder();
    if (!builder) {
        GTEST_SKIP() << "UDP builder not implemented on this platform";
    }
    
    builder->init(test_port);
    int32_t server_socket = builder->platform_socket();
    
    auto* sender = hope::io::create_sender();
    if (!sender) {
        delete builder;
        GTEST_SKIP() << "UDP sender not implemented on this platform";
    }
    
    auto* receiver = hope::io::create_receiver(server_socket);
    if (!receiver) {
        delete sender;
        delete builder;
        GTEST_SKIP() << "UDP receiver not implemented on this platform";
    }
    
    const std::string test_message = "UDP test message";
    std::string received_message;
    
    std::thread server_thread([&receiver, &received_message]() {
        char buffer[256] = {0};
        size_t received = receiver->read(buffer, sizeof(buffer));
        received_message = std::string(buffer, received);
    });
    
    std::this_thread::sleep_for(50ms);
    
    sender->connect("127.0.0.1", test_port);
    sender->write(test_message.c_str(), test_message.length());
    
    std::this_thread::sleep_for(100ms);
    
    server_thread.join();
    
    EXPECT_EQ(received_message, test_message);
    
    delete receiver;
    delete sender;
    delete builder;
}
#endif

// Test UDP multiple packets (Unix only)
#if PLATFORM_LINUX || PLATFORM_APPLE
TEST_F(UdpTest, MultiplePackets) {
    auto* builder = hope::io::create_udp_builder();
    if (!builder) {
        GTEST_SKIP() << "UDP builder not implemented on this platform";
    }
    
    builder->init(test_port);
    int32_t server_socket = builder->platform_socket();
    
    auto* sender = hope::io::create_sender();
    if (!sender) {
        delete builder;
        GTEST_SKIP() << "UDP sender not implemented on this platform";
    }
    
    auto* receiver = hope::io::create_receiver(server_socket);
    if (!receiver) {
        delete sender;
        delete builder;
        GTEST_SKIP() << "UDP receiver not implemented on this platform";
    }
    
    const int num_packets = 5;
    std::vector<std::string> received_messages;
    
    std::thread server_thread([&receiver, &received_messages, num_packets]() {
        for (int i = 0; i < num_packets; ++i) {
            char buffer[256] = {0};
            size_t received = receiver->read_once(buffer, sizeof(buffer));
            if (received > 0) {
                received_messages.emplace_back(buffer, received);
            }
            std::this_thread::sleep_for(10ms);
        }
    });
    
    std::this_thread::sleep_for(50ms);
    
    sender->connect("127.0.0.1", test_port);
    
    for (int i = 0; i < num_packets; ++i) {
        std::string msg = "Packet " + std::to_string(i);
        sender->write(msg.c_str(), msg.length());
        std::this_thread::sleep_for(10ms);
    }
    
    std::this_thread::sleep_for(200ms);
    
    server_thread.join();
    
    EXPECT_GE(received_messages.size(), 1u); // At least some packets should be received
    
    delete receiver;
    delete sender;
    delete builder;
}
#endif

// Test UDP receiver with uninitialized socket
#if PLATFORM_LINUX || PLATFORM_APPLE
TEST_F(UdpTest, ReceiverUninitializedSocket) {
    auto* receiver = hope::io::create_receiver();
    if (!receiver) {
        GTEST_SKIP() << "UDP receiver not implemented on this platform";
    }
    
    // Receiver should be able to create socket on connect
    ASSERT_NO_THROW(receiver->connect("127.0.0.1", test_port));
    
    delete receiver;
}
#endif

// Test UDP sender disconnect
#if PLATFORM_LINUX || PLATFORM_APPLE
TEST_F(UdpTest, SenderDisconnect) {
    auto* sender = hope::io::create_sender();
    if (!sender) {
        GTEST_SKIP() << "UDP sender not implemented on this platform";
    }
    
    sender->connect("127.0.0.1", test_port);
    ASSERT_NO_THROW(sender->disconnect());
    
    // Disconnecting again should be safe
    ASSERT_NO_THROW(sender->disconnect());
    
    delete sender;
}
#endif

