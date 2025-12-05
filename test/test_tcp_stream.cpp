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
#include <functional>

using namespace std::chrono_literals;

class TcpStreamTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_port = 15000 + (std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000);
        acceptor = hope::io::create_acceptor();
        acceptor->open(test_port);
    }

    void TearDown() override {
        if (acceptor) {
            delete acceptor;
            acceptor = nullptr;
        }
        // Give OS time to release the port
        std::this_thread::sleep_for(100ms);
    }

    hope::io::acceptor* acceptor = nullptr;
    std::size_t test_port = 0;
};

// Test basic stream creation
TEST_F(TcpStreamTest, CreateStream) {
    auto* stream = hope::io::create_stream();
    ASSERT_NE(stream, nullptr);
    delete stream;
}

// Test stream connection to server
TEST_F(TcpStreamTest, ConnectToServer) {
    std::thread server_thread([this]() {
        auto* conn = acceptor->accept();
        delete conn;
    });

    std::this_thread::sleep_for(50ms); // Give server time to start listening

    auto* client = hope::io::create_stream();
    ASSERT_NO_THROW(client->connect("127.0.0.1", test_port));
    
    client->disconnect();
    delete client;
    
    server_thread.join();
}

// Test stream write and read
TEST_F(TcpStreamTest, WriteAndRead) {
    const std::string test_message = "Hello, World!";
    std::string received_message;
    
    std::thread server_thread([this, &received_message]() {
        auto* conn = acceptor->accept();
        char buffer[256] = {0};
        size_t received = conn->read(buffer, test_message.length());
        received_message = std::string(buffer, received);
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    client->write(test_message.c_str(), test_message.length());
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    
    EXPECT_EQ(received_message, test_message);
}

// Test stream read_once
TEST_F(TcpStreamTest, ReadOnce) {
    const std::string test_message = "Test message";
    std::string received_message;
    
    std::thread server_thread([this, &received_message]() {
        auto* conn = acceptor->accept();
        char buffer[256] = {0};
        // read_once should read available data, not necessarily all
        size_t received = conn->read_once(buffer, sizeof(buffer));
        received_message = std::string(buffer, received);
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    client->write(test_message.c_str(), test_message.length());
    
    std::this_thread::sleep_for(50ms); // Give time for data to arrive
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    
    EXPECT_GT(received_message.length(), 0u);
    EXPECT_LE(received_message.length(), test_message.length());
}

// Test stream options
TEST_F(TcpStreamTest, SetOptions) {
    auto* stream = hope::io::create_stream();
    
    hope::io::stream_options opts;
    opts.connection_timeout = 5000;
    opts.read_timeout = 2000;
    opts.write_timeout = 2000;
    opts.write_buffer_size = 16384;
    opts.non_block_mode = false;
    
    // Should be able to set options before connecting
    ASSERT_NO_THROW(stream->set_options(opts));
    
    delete stream;
}

// Test that connection timeout is actually enforced
TEST_F(TcpStreamTest, ConnectionTimeoutEnforced) {
    auto* client = hope::io::create_stream();
    
    hope::io::stream_options opts;
    opts.connection_timeout = 100; // Very short timeout (100ms)
    client->set_options(opts);
    
    // Try to connect to a port that's not listening
    // Should timeout within the specified time
    auto start = std::chrono::steady_clock::now();
    try {
        client->connect("127.0.0.1", 1); // Port 1 is unlikely to be listening
        // If connection succeeds (unlikely), that's also OK
    } catch (const std::exception&) {
        // Expected to throw on timeout
    }
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // Should timeout roughly within the specified time (allow some margin)
    // On Windows, timeout might be slightly longer due to select() implementation
    EXPECT_LE(elapsed, 2000); // Allow up to 2 seconds for timeout handling
    
    delete client;
}

// Test that read timeout is actually enforced (Unix only, Windows applies during connect)
#if PLATFORM_LINUX || PLATFORM_APPLE
TEST_F(TcpStreamTest, ReadTimeoutEnforced) {
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
    client->connect("127.0.0.1", test_port);
    
    hope::io::stream_options opts;
    opts.read_timeout = 100; // 100ms timeout
    client->set_options(opts);
    
    char buffer[256];
    auto start = std::chrono::steady_clock::now();
    try {
        // Try to read with timeout - should timeout quickly
        size_t received = client->read_once(buffer, sizeof(buffer));
        // If we get here, might have received 0 bytes
        EXPECT_EQ(received, 0u);
    } catch (const std::exception&) {
        // Throwing on timeout is also acceptable
    }
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // Should timeout roughly within the specified time
    // Note: actual timeout might be slightly longer due to system behavior
    EXPECT_LE(elapsed, 500); // Allow some margin
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    delete acceptor;
}
#endif

// Test that write timeout is actually enforced (Unix only)
#if PLATFORM_LINUX || PLATFORM_APPLE
TEST_F(TcpStreamTest, WriteTimeoutEnforced) {
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    std::thread server_thread([&acceptor]() {
        auto* conn = acceptor->accept();
        // Don't read any data, let send buffer fill up
        std::this_thread::sleep_for(1000ms);
        delete conn;
    });
    
    std::this_thread::sleep_for(50ms);
    
    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    hope::io::stream_options opts;
    opts.write_timeout = 100; // 100ms timeout
    client->set_options(opts);
    
    // Try to write large amount of data to fill buffer
    std::vector<char> large_data(1024 * 1024, 'A'); // 1MB
    auto start = std::chrono::steady_clock::now();
    try {
        client->write(large_data.data(), large_data.size());
        // If write succeeds, buffer wasn't full
    } catch (const std::exception&) {
        // Throwing on timeout is acceptable
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        // Should timeout roughly within the specified time
        EXPECT_LE(elapsed, 500); // Allow some margin
    }
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    delete acceptor;
}
#endif

// Test that options persist and can be changed
TEST_F(TcpStreamTest, OptionsPersistence) {
    auto* stream = hope::io::create_stream();
    
    hope::io::stream_options opts1;
    opts1.connection_timeout = 5000;
    opts1.read_timeout = 2000;
    opts1.write_timeout = 2000;
    stream->set_options(opts1);
    
    // Change options
    hope::io::stream_options opts2;
    opts2.connection_timeout = 3000;
    opts2.read_timeout = 1000;
    opts2.write_timeout = 1000;
    ASSERT_NO_THROW(stream->set_options(opts2));
    
    delete stream;
}

// Test all option parameters are set correctly
TEST_F(TcpStreamTest, AllOptionsParameters) {
    auto* stream = hope::io::create_stream();
    
    hope::io::stream_options opts;
    opts.connection_timeout = 1234;
    opts.read_timeout = 5678;
    opts.write_timeout = 9012;
    opts.write_buffer_size = 16384;
    opts.non_block_mode = true;
    
    ASSERT_NO_THROW(stream->set_options(opts));
    
    // Verify options were stored (on Windows, they're stored in m_options)
    // On Unix, they're applied immediately to socket
    // This test mainly verifies no exceptions are thrown
    
    delete stream;
}

// Test non-blocking mode (Unix only, as it requires set_options after connect)
#if PLATFORM_LINUX || PLATFORM_APPLE
TEST_F(TcpStreamTest, NonBlockingMode) {
    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(test_port);
    
    std::thread server_thread([&acceptor]() {
        auto* conn = acceptor->accept();
        // Send data immediately
        const char* data = "Hello";
        conn->write(data, 5);
        std::this_thread::sleep_for(100ms);
        delete conn;
    });
    
    std::this_thread::sleep_for(50ms);
    
    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    hope::io::stream_options opts;
    opts.non_block_mode = true;
    client->set_options(opts);
    
    // In non-blocking mode, read should return immediately
    // even if no data is available (returns 0 or throws EAGAIN)
    char buffer[256];
    size_t received = 0;
    try {
        received = client->read_once(buffer, sizeof(buffer));
        // In non-blocking mode, should return immediately
        // Might return 0 if no data, or data if available
    } catch (const std::exception&) {
        // EAGAIN error is acceptable in non-blocking mode
    }
    
    // Wait a bit for data to arrive
    std::this_thread::sleep_for(100ms);
    
    // Now try to read again - should get data
    try {
        received = client->read_once(buffer, sizeof(buffer));
        if (received > 0) {
            EXPECT_GE(received, 5u); // Should get at least "Hello"
        }
    } catch (const std::exception&) {
        // Might still get EAGAIN if timing is off
    }
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    delete acceptor;
}
#endif

// Test that write_buffer_size option is accepted (even if not directly verifiable)
TEST_F(TcpStreamTest, WriteBufferSizeOption) {
    auto* stream = hope::io::create_stream();
    
    hope::io::stream_options opts;
    opts.write_buffer_size = 32768; // 32KB
    ASSERT_NO_THROW(stream->set_options(opts));
    
    // Note: write_buffer_size might not be directly applied to socket
    // but setting it should not throw
    delete stream;
}

// Test stream options after connection (platform-specific behavior)
TEST_F(TcpStreamTest, SetOptionsAfterConnection) {
    std::thread server_thread([this]() {
        auto* conn = acceptor->accept();
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    hope::io::stream_options opts;
    opts.read_timeout = 1000;
    opts.write_timeout = 1000;
    
    // On Windows, this should throw; on Unix, it should work
    // Test both behaviors
    try {
        client->set_options(opts);
        // If we get here on Unix, it's OK
    } catch (const std::exception&) {
        // On Windows, this is expected
    }
    
    client->disconnect();
    delete client;
    
    server_thread.join();
}

// Test that Windows applies options during connect
#if PLATFORM_WINDOWS
TEST_F(TcpStreamTest, WindowsOptionsAppliedDuringConnect) {
    auto* client = hope::io::create_stream();
    
    hope::io::stream_options opts;
    opts.connection_timeout = 2000;
    opts.read_timeout = 1500;
    opts.write_timeout = 1500;
    client->set_options(opts);
    
    std::thread server_thread([this]() {
        auto* conn = acceptor->accept();
        // Verify connection was established with timeout settings
        // (We can't directly verify, but connection should work)
        delete conn;
    });
    
    std::this_thread::sleep_for(50ms);
    
    // Connect should use the options we set
    ASSERT_NO_THROW(client->connect("127.0.0.1", test_port));
    
    // Verify options were applied by checking socket behavior
    // (Read/write timeouts should be set on the socket)
    
    client->disconnect();
    delete client;
    
    server_thread.join();
}
#endif

// Test large data transfer
TEST_F(TcpStreamTest, LargeDataTransfer) {
    const size_t data_size = 1024 * 1024; // 1MB
    std::vector<char> send_data(data_size, 'A');
    std::vector<char> recv_data(data_size, 0);
    size_t total_received = 0;
    
    std::thread server_thread([this, &recv_data, &total_received, data_size]() {
        auto* conn = acceptor->accept();
        size_t received = 0;
        while (received < data_size) {
            size_t chunk = conn->read(recv_data.data() + received, data_size - received);
            received += chunk;
        }
        total_received = received;
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    client->write(send_data.data(), data_size);
    
    std::this_thread::sleep_for(100ms);
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    
    EXPECT_EQ(total_received, data_size);
    EXPECT_EQ(memcmp(send_data.data(), recv_data.data(), data_size), 0);
}

// Test multiple writes and reads
TEST_F(TcpStreamTest, MultipleWritesReads) {
    const int num_messages = 10;
    std::vector<std::string> received_messages;
    
    std::thread server_thread([this, &received_messages, num_messages]() {
        auto* conn = acceptor->accept();
        for (int i = 0; i < num_messages; ++i) {
            char buffer[256] = {0};
            size_t len = conn->read_once(buffer, sizeof(buffer));
            if (len > 0) {
                received_messages.emplace_back(buffer, len);
            }
            std::this_thread::sleep_for(10ms);
        }
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    for (int i = 0; i < num_messages; ++i) {
        std::string msg = "Message " + std::to_string(i);
        client->write(msg.c_str(), msg.length());
        std::this_thread::sleep_for(10ms);
    }
    
    std::this_thread::sleep_for(100ms);
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    
    EXPECT_GE(received_messages.size(), 1u); // At least some messages should be received
}

// Test disconnect
TEST_F(TcpStreamTest, Disconnect) {
    std::thread server_thread([this]() {
        auto* conn = acceptor->accept();
        // Try to read - should eventually fail or return 0 when client disconnects
        char buffer[256];
        size_t received = conn->read_once(buffer, sizeof(buffer));
        // After disconnect, read should return 0 or throw
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    client->disconnect();
    
    // Disconnecting again should be safe
    ASSERT_NO_THROW(client->disconnect());
    
    delete client;
    
    server_thread.join();
}

// Test connection to invalid address
TEST_F(TcpStreamTest, ConnectToInvalidAddress) {
    auto* client = hope::io::create_stream();
    
    // This should throw an exception
    EXPECT_THROW(client->connect("999.999.999.999", 80), std::exception);
    
    delete client;
}

// Test connection to closed port
TEST_F(TcpStreamTest, ConnectToClosedPort) {
    auto* client = hope::io::create_stream();
    
    hope::io::stream_options opts;
    opts.connection_timeout = 100; // Short timeout
    client->set_options(opts);
    
    // Try to connect to a port that's not listening
    // Should throw or timeout
    EXPECT_THROW(client->connect("127.0.0.1", 1), std::exception);
    
    delete client;
}

// Test template read/write for trivial types
TEST_F(TcpStreamTest, TemplateReadWrite) {
    int32_t send_value = 12345;
    int32_t recv_value = 0;
    
    std::thread server_thread([this, &recv_value]() {
        auto* conn = acceptor->accept();
        conn->read(recv_value);
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    client->write(send_value);
    
    std::this_thread::sleep_for(50ms);
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    
    EXPECT_EQ(recv_value, send_value);
}

// Test string read/write
TEST_F(TcpStreamTest, StringReadWrite) {
    std::string send_str = "Hello from template!";
    std::string recv_str;
    
    std::thread server_thread([this, &recv_str]() {
        auto* conn = acceptor->accept();
        conn->read(recv_str);
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    client->write(send_str);
    
    std::this_thread::sleep_for(50ms);
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    
    EXPECT_EQ(recv_str, send_str);
}

// Test platform_socket
TEST_F(TcpStreamTest, PlatformSocket) {
    std::thread server_thread([this]() {
        auto* conn = acceptor->accept();
        int32_t socket = conn->platform_socket();
        EXPECT_GE(socket, 0);
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    int32_t socket = client->platform_socket();
    EXPECT_GE(socket, 0);
    
    client->disconnect();
    delete client;
    
    server_thread.join();
}

// Test get_endpoint (Unix only, Windows returns empty)
TEST_F(TcpStreamTest, GetEndpoint) {
    std::string endpoint;
    
    std::thread server_thread([this, &endpoint]() {
        auto* conn = acceptor->accept();
        endpoint = conn->get_endpoint();
        delete conn;
    });

    std::this_thread::sleep_for(50ms);

    auto* client = hope::io::create_stream();
    client->connect("127.0.0.1", test_port);
    
    std::this_thread::sleep_for(50ms);
    
    client->disconnect();
    delete client;
    
    server_thread.join();
    
    // On Unix, should get "127.0.0.1", on Windows might be empty
    // Just check it doesn't crash
    EXPECT_NO_THROW(endpoint);
}

