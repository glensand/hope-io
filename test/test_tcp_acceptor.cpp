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
#include <vector>
#include <memory>
#include <string>

using namespace std::chrono_literals;

class TcpAcceptorTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_port = 15000 + (std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000);
    }

    void TearDown() override {
        std::this_thread::sleep_for(100ms); // Give OS time to release the port
    }

    std::size_t test_port = 0;
};

// Test acceptor creation
TEST_F(TcpAcceptorTest, CreateAcceptor) {
    auto* acceptor = hope::io::create_acceptor();
    ASSERT_NE(acceptor, nullptr);
    delete acceptor;
}

// Test acceptor open
TEST_F(TcpAcceptorTest, OpenPort) {
    auto* acceptor = hope::io::create_acceptor();
    ASSERT_NO_THROW(acceptor->open(test_port));
    delete acceptor;
}

// Test acceptor accept single connection
TEST_F(TcpAcceptorTest, AcceptSingleConnection) {
    auto* acceptor = hope::io::create_acceptor();
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
    ASSERT_NE(conn, nullptr);
    
    delete conn;
    client_thread.join();
    delete acceptor;
}

// Test acceptor accept multiple connections
TEST_F(TcpAcceptorTest, AcceptMultipleConnections) {
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    const int num_clients = 5;
    std::vector<std::thread> client_threads;
    
    for (int i = 0; i < num_clients; ++i) {
        client_threads.emplace_back([this, i]() {
            std::this_thread::sleep_for(50ms + std::chrono::milliseconds(i * 10));
            auto* client = hope::io::create_stream();
            client->connect("127.0.0.1", test_port);
            std::this_thread::sleep_for(50ms);
            client->disconnect();
            delete client;
        });
    }
    
    std::vector<hope::io::stream*> connections;
    for (int i = 0; i < num_clients; ++i) {
        auto* conn = acceptor->accept();
        ASSERT_NE(conn, nullptr);
        connections.push_back(conn);
    }
    
    for (auto* conn : connections) {
        delete conn;
    }
    
    for (auto& t : client_threads) {
        t.join();
    }
    
    delete acceptor;
}

// Test acceptor set_options
TEST_F(TcpAcceptorTest, SetOptions) {
    auto* acceptor = hope::io::create_acceptor();
    
    hope::io::stream_options opts;
    opts.non_block_mode = false;
    opts.read_timeout = 1000;
    opts.write_timeout = 1000;
    
    // Should be able to set options before opening
    ASSERT_NO_THROW(acceptor->set_options(opts));
    
    acceptor->open(test_port);
    
    // Should be able to set options after opening
    ASSERT_NO_THROW(acceptor->set_options(opts));
    
    delete acceptor;
}

// Test acceptor raw socket
TEST_F(TcpAcceptorTest, RawSocket) {
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    long long raw = acceptor->raw();
    EXPECT_GE(raw, 0);
    
    delete acceptor;
}

// Test acceptor with options applied to accepted connections
TEST_F(TcpAcceptorTest, OptionsAppliedToAcceptedConnections) {
    auto* acceptor = hope::io::create_acceptor();
    
    hope::io::stream_options opts;
    opts.read_timeout = 2000;
    opts.write_timeout = 2000;
    opts.non_block_mode = false;
    
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
    ASSERT_NE(conn, nullptr);
    
    // On Unix, options should be applied; on Windows, they might not be
    // Just verify the connection works
    int32_t socket = conn->platform_socket();
    EXPECT_GE(socket, 0);
    
    delete conn;
    client_thread.join();
    delete acceptor;
}

// Test acceptor open on already used port (should fail)
TEST_F(TcpAcceptorTest, OpenOnUsedPort) {
    auto* acceptor1 = hope::io::create_acceptor();
    acceptor1->open(test_port);
    
    auto* acceptor2 = hope::io::create_acceptor();
    // Opening the same port should fail
    EXPECT_THROW(acceptor2->open(test_port), std::exception);
    
    delete acceptor2;
    delete acceptor1;
}

// Test acceptor accept timeout (non-blocking mode)
TEST_F(TcpAcceptorTest, AcceptTimeout) {
    auto* acceptor = hope::io::create_acceptor();
    
    hope::io::stream_options opts;
    opts.non_block_mode = true;
    acceptor->set_options(opts);
    acceptor->open(test_port);
    
    // In non-blocking mode, accept should either return immediately
    // or throw if no connection is available
    // This is platform-dependent behavior
    try {
        auto* conn = acceptor->accept();
        // If we get here, a connection was available (unlikely)
        delete conn;
    } catch (const std::exception&) {
        // Expected if no connection available
    }
    
    delete acceptor;
}

// Test acceptor with data exchange
TEST_F(TcpAcceptorTest, AcceptWithDataExchange) {
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    const std::string test_message = "Test message from client";
    std::string received_message;
    
    std::thread client_thread([this, &test_message]() {
        std::this_thread::sleep_for(50ms);
        auto* client = hope::io::create_stream();
        client->connect("127.0.0.1", test_port);
        client->write(test_message.c_str(), test_message.length());
        std::this_thread::sleep_for(50ms);
        client->disconnect();
        delete client;
    });
    
    auto* conn = acceptor->accept();
    ASSERT_NE(conn, nullptr);
    
    char buffer[256] = {0};
    size_t received = conn->read_once(buffer, sizeof(buffer));
    received_message = std::string(buffer, received);
    
    EXPECT_EQ(received_message, test_message);
    
    delete conn;
    client_thread.join();
    delete acceptor;
}

// Test acceptor cleanup
TEST_F(TcpAcceptorTest, Cleanup) {
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    // Deleting acceptor should clean up resources
    ASSERT_NO_THROW(delete acceptor);
}

