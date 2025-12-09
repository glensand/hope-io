/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/net/stream.h"
#include "hope-io/net/stream_coro.h"
#include "hope-io/net/async_stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"

#include <iostream>
#include <string>

// Example coroutine that demonstrates async read/write operations with I/O readiness checking
hope::io::coro_task<> async_echo_client(const std::string& server_ip, std::size_t port) {
    hope::io::init();

    auto* tcp_stream = hope::io::create_stream();
    hope::io::async_stream stream(tcp_stream);

    try {
        std::cout << "Connecting to " << server_ip << ":" << port << std::endl;
        
        // Asynchronously connect to server (timeout 5 seconds)
        co_await stream.async_connect(server_ip, port, 5000);
        std::cout << "Connected to: " << stream.get_endpoint() << std::endl;

        // Send a message (timeout 3 seconds)
        const std::string message = "Hello from async coroutine!";
        std::cout << "Sending: " << message << std::endl;
        co_await stream.async_write(message.c_str(), message.length(), 3000);

        // Read response (timeout 3 seconds, uses epoll/poll for readiness)
        char buffer[256] = {0};
        std::size_t bytes_read = co_await stream.async_read(buffer, sizeof(buffer) - 1, 3000);
        std::cout << "Received (" << bytes_read << " bytes): " << buffer << std::endl;

        stream.disconnect();
        delete tcp_stream;
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        delete tcp_stream;
    }
}

// Example coroutine demonstrating multiple async operations
hope::io::coro_task<> async_multi_message_client(const std::string& server_ip, std::size_t port) {
    auto* tcp_stream = hope::io::create_stream();
    hope::io::async_stream stream(tcp_stream);

    try {
        co_await stream.async_connect(server_ip, port, 5000);
        std::cout << "Connected, sending multiple messages..." << std::endl;

        const std::string messages[] = {
            "First message",
            "Second message",
            "Third message"
        };

        for (const auto& msg : messages) {
            std::cout << "Sending: " << msg << std::endl;
            co_await stream.async_write(msg.c_str(), msg.length(), 3000);

            char buffer[256] = {0};
            std::size_t bytes_read = co_await stream.async_read(buffer, sizeof(buffer) - 1, 3000);
            std::cout << "Echo: " << buffer << std::endl;
        }

        stream.disconnect();
        delete tcp_stream;
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        delete tcp_stream;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <server_ip> <port>" << std::endl;
        std::cout << "Example: " << argv[0] << " 127.0.0.1 1338" << std::endl;
        return 1;
    }

    const std::string server_ip = argv[1];
    const std::size_t port = std::stoul(argv[2]);

    std::cout << "=== Coroutine Stream Example ===" << std::endl;
    std::cout << "This example demonstrates async read/write with C++20 coroutines" << std::endl;
    std::cout << std::endl;

    try {
        // Run the first coroutine
        std::cout << "[Example 1] Single message exchange" << std::endl;
        auto task1 = async_echo_client(server_ip, port);
        task1.get();

        std::cout << std::endl;

        // Run the second coroutine
        std::cout << "[Example 2] Multiple message exchanges" << std::endl;
        auto task2 = async_multi_message_client(server_ip, port);
        task2.get();

        std::cout << std::endl << "All coroutines completed successfully!" << std::endl;
    }
    catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
