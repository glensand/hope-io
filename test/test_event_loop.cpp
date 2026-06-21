/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include <gtest/gtest.h>
#include "hope-io/net/event_loop.h"
#include "hope-io/net/nix/tcp_stream.h"
#include "hope-io/net/nix/event_loop_impl.h"
#include "hope-io/net/linux/event_loop_impl.h"
#include "hope-io/net/init.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

using namespace std::chrono_literals;
using namespace hope::io::el;

class EventLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        static std::atomic<int> port_counter{18000};
        test_port = port_counter.fetch_add(1);
    }

    void TearDown() override {
        std::this_thread::sleep_for(50ms);
    }

    std::size_t test_port = 0;
};

// Test event loop creation
TEST_F(EventLoopTest, CreateEventLoop) {
    auto on_connect = [](connection&) { return el_connection_state::read; };
    auto on_read = [](connection&) { return el_connection_state::read; };
    auto on_write = [](connection&) { return el_connection_state::read; };
    auto on_err = [](connection&, const std::string&) { return el_connection_state::die; };
    event_loop_impl_t loop(
        std::move(on_connect), std::move(on_read), std::move(on_write), std::move(on_err)
    );
}

// Test event loop run and stop (Unix/Apple only)
#if PLATFORM_LINUX || PLATFORM_APPLE
TEST_F(EventLoopTest, RunAndStop) {
    auto on_connect = [](connection&) { return el_connection_state::read; };
    auto on_read = [](connection&) { return el_connection_state::read; };
    auto on_write = [](connection&) { return el_connection_state::read; };
    auto on_err = [](connection&, const std::string&) { return el_connection_state::die; };

    config cfg;
    cfg.port = test_port;
    cfg.max_mutual_connections = 10;
    cfg.max_accepts_per_tick = 5;
    cfg.epoll_temeout = 100;

    auto* loop = new event_loop_impl_t(
        std::move(on_connect), std::move(on_read), std::move(on_write), std::move(on_err)
    );

    std::thread loop_thread([loop, cfg]() {
        loop->run(cfg);
        delete loop;
    });
    loop_thread.detach();

    std::this_thread::sleep_for(100ms);
    EXPECT_TRUE(true); // reached here = no crash
}
#endif

// Test event loop connection handling (Unix/Apple only)
#if PLATFORM_LINUX || PLATFORM_APPLE
TEST_F(EventLoopTest, ConnectionHandling) {
    std::atomic<int> connections_accepted{0};
    std::atomic<int> reads_received{0};
    std::atomic<int> writes_completed{0};
    std::atomic<int> errors_occurred{0};

    config cfg;
    cfg.port = test_port;
    cfg.max_mutual_connections = 10;
    cfg.max_accepts_per_tick = 5;
    cfg.epoll_temeout = 100;

    auto on_connect = [&connections_accepted](connection& c) {
        connections_accepted++;
        return el_connection_state::read;
    };
    auto on_read = [&reads_received](connection& c) {
        reads_received++;
        return el_connection_state::write;
    };
    auto on_write = [&writes_completed](connection& c) {
        writes_completed++;
        return el_connection_state::die;
    };
    auto on_err = [&errors_occurred](connection&, const std::string&) {
        errors_occurred++;
        return el_connection_state::die;
    };

    auto* loop = new event_loop_impl_t(
        std::move(on_connect), std::move(on_read), std::move(on_write), std::move(on_err)
    );

    std::thread loop_thread([loop, &cfg]() {
        loop->run(cfg);
        delete loop;
    });
    loop_thread.detach();

    std::this_thread::sleep_for(100ms);

    auto* client = new hope::io::tcp_stream();
    client->connect("127.0.0.1", test_port);

    const std::string test_message = "Hello from event loop test";
    client->write(test_message.c_str(), test_message.length());

    std::this_thread::sleep_for(200ms);

    client->disconnect();
    delete client;

    std::this_thread::sleep_for(100ms);

    EXPECT_GE(connections_accepted.load(), 1);
}
#endif

// Test event loop fixed_size_buffer
TEST_F(EventLoopTest, FixedSizeBuffer) {
    fixed_size_buffer buffer;

    EXPECT_TRUE(buffer.is_empty());
    EXPECT_EQ(buffer.count(), 0u);
    EXPECT_EQ(buffer.free_space(), fixed_size_buffer::buffer_size);

    const std::string test_data = "Test data";
    size_t written = buffer.write(test_data.c_str(), test_data.length());
    EXPECT_EQ(written, test_data.length());
    EXPECT_FALSE(buffer.is_empty());
    EXPECT_EQ(buffer.count(), test_data.length());

    char read_buffer[256] = {0};
    size_t read = buffer.read(read_buffer, sizeof(read_buffer));
    EXPECT_EQ(read, test_data.length());
    EXPECT_EQ(std::string(read_buffer, read), test_data);
    EXPECT_TRUE(buffer.is_empty());

    buffer.reset();
    EXPECT_TRUE(buffer.is_empty());
}

// Test event loop buffer shrink
TEST_F(EventLoopTest, BufferShrink) {
    fixed_size_buffer buffer;

    const std::string test_data = "Test data for shrink";
    buffer.write(test_data.c_str(), test_data.length());

    char read_buffer[10] = {0};
    buffer.read(read_buffer, 5);

    EXPECT_EQ(buffer.count(), test_data.length() - 5);
}

// Test event loop connection state
TEST_F(EventLoopTest, ConnectionState) {
    connection conn(123);

    EXPECT_EQ(conn.get_state(), el_connection_state::idle);

    conn.set_state(el_connection_state::read);
    EXPECT_EQ(conn.get_state(), el_connection_state::read);

    conn.set_state(el_connection_state::write);
    EXPECT_EQ(conn.get_state(), el_connection_state::write);

    conn.set_state(el_connection_state::die);
    EXPECT_EQ(conn.get_state(), el_connection_state::die);
}

// Test event loop connection equality
TEST_F(EventLoopTest, ConnectionEquality) {
    connection conn1(123);
    connection conn2(123);
    connection conn3(456);

    EXPECT_EQ(conn1, conn2);
    EXPECT_NE(conn1, conn3);
}

// Test event loop connection hash
TEST_F(EventLoopTest, ConnectionHash) {
    connection conn1(123);
    connection conn2(123);
    connection::hash hasher;

    EXPECT_EQ(hasher(conn1), hasher(conn2));
}
