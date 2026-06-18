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
#include <span>
#include <atomic>

using namespace std::chrono_literals;

class WriteVTest : public ::testing::Test {
protected:
    void SetUp() override {
        acceptor = new hope::io::tcp_acceptor();
        acceptor->open(test_port);
    }

    void TearDown() override {
        if (acceptor) {
            acceptor->close();
            delete acceptor;
            acceptor = nullptr;
        }
        std::this_thread::sleep_for(50ms);
    }

    hope::io::acceptor* acceptor = nullptr;
    std::size_t test_port = 17000;
};

// Test write_v with a single buffer — equivalent to write()
TEST_F(WriteVTest, SingleBuffer) {
    const std::string msg = "Hello from write_v";
    std::string received;

    std::thread server_thread([this, &received, msg]() {
        auto* conn = acceptor->accept();
        char buffer[256] = {0};
        size_t n = conn->read(buffer, msg.length());
        received = std::string(buffer, n);
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = new hope::io::tcp_stream();
    client->connect("127.0.0.1", test_port);

    std::span<const char> buf{msg.data(), msg.size()};
    std::vector<std::span<const char>> bufs{buf};
    client->write_v(bufs);

    client->disconnect();
    delete client;

    server_thread.join();
    EXPECT_EQ(received, msg);
}

// Test write_v with multiple small buffers
TEST_F(WriteVTest, MultipleBuffers) {
    std::string s1 = "Hello, ";
    std::string s2 = "world";
    std::string s3 = "!";
    std::string expected = s1 + s2 + s3;

    std::string received;

    std::thread server_thread([this, &received, expected]() {
        auto* conn = acceptor->accept();
        char buffer[256] = {0};
        size_t n = conn->read(buffer, expected.length());
        received = std::string(buffer, n);
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = new hope::io::tcp_stream();
    client->connect("127.0.0.1", test_port);

    std::span<const char> b1{s1.data(), s1.size()};
    std::span<const char> b2{s2.data(), s2.size()};
    std::span<const char> b3{s3.data(), s3.size()};

    std::vector<std::span<const char>> bufs{b1, b2, b3};
    client->write_v(bufs);

    client->disconnect();
    delete client;

    server_thread.join();
    EXPECT_EQ(received, expected);
}

// Test write_v with empty buffer list — should be a no-op
TEST_F(WriteVTest, EmptyBuffers) {
    std::string received;

    std::thread server_thread([this, &received]() {
        auto* conn = acceptor->accept();
        char buffer[1] = {0};
        size_t n = conn->read_once(buffer, 1);
        received = (n > 0) ? std::string(buffer, n) : "";
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = new hope::io::tcp_stream();
    client->connect("127.0.0.1", test_port);

    // Empty write_v — no data sent, connection stays open
    std::vector<std::span<const char>> bufs;
    client->write_v(bufs);

    // Send a marker so the server can proceed
    const char marker = 'X';
    client->write(&marker, 1);

    client->disconnect();
    delete client;

    server_thread.join();
    EXPECT_EQ(received, "X");
}

// Test write_v with buffers containing zero-length spans
TEST_F(WriteVTest, WithEmptySpans) {
    std::string s1 = "Hello";
    std::string s2 = "World";
    std::string expected = s1 + s2;

    std::string received;

    std::thread server_thread([this, &received, expected]() {
        auto* conn = acceptor->accept();
        char buffer[256] = {0};
        size_t n = conn->read(buffer, expected.length());
        received = std::string(buffer, n);
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = new hope::io::tcp_stream();
    client->connect("127.0.0.1", test_port);

    std::span<const char> b1{s1.data(), s1.size()};
    std::span<const char> b0{"" , 0};  // empty span in the middle
    std::span<const char> b2{s2.data(), s2.size()};

    std::vector<std::span<const char>> bufs{b1, b0, b2};
    client->write_v(bufs);

    client->disconnect();
    delete client;

    server_thread.join();
    EXPECT_EQ(received, expected);
}

// Test write_v with many buffers
TEST_F(WriteVTest, ManyBuffers) {
    constexpr int num_buffers = 50;
    std::vector<std::string> parts;
    std::string expected;
    for (int i = 0; i < num_buffers; ++i) {
        parts.push_back("chunk");
        expected += "chunk";
    }

    std::string received;

    std::thread server_thread([this, &received, expected]() {
        auto* conn = acceptor->accept();
        std::vector<char> buffer(expected.length());
        size_t n = conn->read(buffer.data(), buffer.size());
        received = std::string(buffer.data(), n);
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = new hope::io::tcp_stream();
    client->connect("127.0.0.1", test_port);

    std::vector<std::span<const char>> bufs;
    bufs.reserve(parts.size());
    for (auto& p : parts) {
        bufs.emplace_back(p.data(), p.size());
    }

    client->write_v(bufs);

    client->disconnect();
    delete client;

    server_thread.join();
    EXPECT_EQ(received, expected);
}

// Test that write_v and write produce identical results
TEST_F(WriteVTest, ConsistencyWithWrite) {
    std::string s1 = "part-a";
    std::string s2 = "part-b";
    std::string expected = s1 + s2;

    std::string received_write_v;
    std::string received_write;

    // First: write_v
    std::thread server_thread1([this, &received_write_v, expected]() {
        auto* conn = acceptor->accept();
        char buffer[256] = {0};
        size_t n = conn->read(buffer, expected.length());
        received_write_v = std::string(buffer, n);
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client1 = new hope::io::tcp_stream();
    client1->connect("127.0.0.1", test_port);

    std::span<const char> b1{s1.data(), s1.size()};
    std::span<const char> b2{s2.data(), s2.size()};
    std::vector<std::span<const char>> bufs{b1, b2};
    client1->write_v(bufs);

    client1->disconnect();
    delete client1;
    server_thread1.join();

    // Second: write with concatenated data
    std::this_thread::sleep_for(50ms);

    std::thread server_thread2([this, &received_write, expected]() {
        auto* conn = acceptor->accept();
        char buffer[256] = {0};
        size_t n = conn->read(buffer, expected.length());
        received_write = std::string(buffer, n);
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client2 = new hope::io::tcp_stream();
    client2->connect("127.0.0.1", test_port);
    client2->write(expected.data(), expected.size());
    client2->disconnect();
    delete client2;
    server_thread2.join();

    EXPECT_EQ(received_write_v, expected);
    EXPECT_EQ(received_write, expected);
    EXPECT_EQ(received_write_v, received_write);
}
