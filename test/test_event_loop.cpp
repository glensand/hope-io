/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include <gtest/gtest.h>
#include "hope-io/net/event_loop.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

using namespace std::chrono_literals;

class EventLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_port = 15000 + (std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000);
    }

    void TearDown() override {
        std::this_thread::sleep_for(100ms);
    }

    std::size_t test_port = 0;
};

// Test event loop creation
TEST_F(EventLoopTest, CreateEventLoop) {
    auto* loop = hope::io::create_event_loop();
    
#if PLATFORM_WINDOWS
    // On Windows, event loop is not implemented
    EXPECT_EQ(loop, nullptr);
#else
    ASSERT_NE(loop, nullptr);
    delete loop;
#endif
}

// Test event loop creation with max connections
TEST_F(EventLoopTest, CreateEventLoop2) {
    auto* loop = hope::io::create_event_loop2(100);
    
#if PLATFORM_WINDOWS
    // On Windows, event loop is not implemented
    EXPECT_EQ(loop, nullptr);
#else
    ASSERT_NE(loop, nullptr);
    delete loop;
#endif
}

// Test event loop run and stop (Unix/Apple only)
#if PLATFORM_LINUX || PLATFORM_APPLE
TEST_F(EventLoopTest, RunAndStop) {
    auto* loop = hope::io::create_event_loop();
    if (!loop) {
        GTEST_SKIP() << "Event loop not implemented on this platform";
    }
    
    hope::io::event_loop::config cfg;
    cfg.port = test_port;
    cfg.max_mutual_connections = 10;
    cfg.max_accepts_per_tick = 5;
    cfg.epoll_temeout = 100;
    
    std::atomic<bool> loop_started{false};
    std::atomic<bool> loop_stopped{false};
    
    hope::io::event_loop::callbacks cb;
    cb.on_connect = [&loop_started](hope::io::event_loop::connection&) {
        loop_started = true;
    };
    cb.on_read = [](hope::io::event_loop::connection&) {};
    cb.on_write = [](hope::io::event_loop::connection&) {};
    cb.on_err = [](hope::io::event_loop::connection&, const std::string&) {};
    
    std::thread loop_thread([&loop, &cfg, &cb]() {
        loop->run(cfg, std::move(cb));
    });
    
    std::this_thread::sleep_for(100ms);
    
    // Stop the loop
    loop->stop();
    
    std::this_thread::sleep_for(100ms);
    
    loop_thread.join();
    
    delete loop;
}
#endif

// Test event loop connection handling (Unix/Apple only)
#if PLATFORM_LINUX || PLATFORM_APPLE
TEST_F(EventLoopTest, ConnectionHandling) {
    auto* loop = hope::io::create_event_loop();
    if (!loop) {
        GTEST_SKIP() << "Event loop not implemented on this platform";
    }
    
    hope::io::event_loop::config cfg;
    cfg.port = test_port;
    cfg.max_mutual_connections = 10;
    cfg.max_accepts_per_tick = 5;
    cfg.epoll_temeout = 100;
    
    std::atomic<int> connections_accepted{0};
    std::atomic<int> reads_received{0};
    std::atomic<int> writes_completed{0};
    std::atomic<int> errors_occurred{0};
    
    hope::io::event_loop::callbacks cb;
    cb.on_connect = [&connections_accepted](hope::io::event_loop::connection& conn) {
        connections_accepted++;
        conn.set_state(hope::io::event_loop::connection_state::read);
    };
    cb.on_read = [&reads_received](hope::io::event_loop::connection& conn) {
        reads_received++;
        // Switch to write state after reading
        conn.set_state(hope::io::event_loop::connection_state::write);
    };
    cb.on_write = [&writes_completed](hope::io::event_loop::connection& conn) {
        writes_completed++;
        conn.set_state(hope::io::event_loop::connection_state::die);
    };
    cb.on_err = [&errors_occurred](hope::io::event_loop::connection&, const std::string&) {
        errors_occurred++;
    };
    
    std::thread loop_thread([&loop, &cfg, &cb]() {
        loop->run(cfg, std::move(cb));
    });
    
    std::this_thread::sleep_for(100ms);
    
    // Create a client connection
    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    const std::string test_message = "Hello from event loop test";
    client->write(test_message.c_str(), test_message.length());
    
    std::this_thread::sleep_for(200ms);
    
    client->disconnect();
    delete client;
    
    std::this_thread::sleep_for(100ms);
    
    loop->stop();
    loop_thread.join();
    
    EXPECT_GE(connections_accepted.load(), 1);
    
    delete loop;
}
#endif

// Test event loop fixed_size_buffer
TEST_F(EventLoopTest, FixedSizeBuffer) {
    hope::io::event_loop::fixed_size_buffer buffer;
    
    EXPECT_TRUE(buffer.is_empty());
    EXPECT_EQ(buffer.count(), 0u);
    EXPECT_EQ(buffer.free_space(), hope::io::event_loop::fixed_size_buffer::buffer_size);
    
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
    hope::io::event_loop::fixed_size_buffer buffer;
    
    // Write some data
    const std::string test_data = "Test data for shrink";
    buffer.write(test_data.c_str(), test_data.length());
    
    // Read part of it
    char read_buffer[10] = {0};
    buffer.read(read_buffer, 5);
    
    // Shrink should move remaining data to start
    buffer.shrink();
    
    EXPECT_EQ(buffer.count(), test_data.length() - 5);
}

// Test event loop connection state
TEST_F(EventLoopTest, ConnectionState) {
    hope::io::event_loop::connection conn(123);
    
    EXPECT_EQ(conn.get_state(), hope::io::event_loop::connection_state::idle);
    
    conn.set_state(hope::io::event_loop::connection_state::read);
    EXPECT_EQ(conn.get_state(), hope::io::event_loop::connection_state::read);
    
    conn.set_state(hope::io::event_loop::connection_state::write);
    EXPECT_EQ(conn.get_state(), hope::io::event_loop::connection_state::write);
    
    conn.set_state(hope::io::event_loop::connection_state::die);
    EXPECT_EQ(conn.get_state(), hope::io::event_loop::connection_state::die);
}

// Test event loop connection equality
TEST_F(EventLoopTest, ConnectionEquality) {
    hope::io::event_loop::connection conn1(123);
    hope::io::event_loop::connection conn2(123);
    hope::io::event_loop::connection conn3(456);
    
    EXPECT_EQ(conn1, conn2);
    EXPECT_NE(conn1, conn3);
}

// Test event loop connection hash
TEST_F(EventLoopTest, ConnectionHash) {
    hope::io::event_loop::connection conn1(123);
    hope::io::event_loop::connection conn2(123);
    hope::io::event_loop::connection::hash hasher;
    
    EXPECT_EQ(hasher(conn1), hasher(conn2));
}

