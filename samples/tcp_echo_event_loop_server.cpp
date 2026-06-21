/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */
#if PLATFORM_LINUX
#include "message.h"

#include "hope-io/net/event_loop.h"
#include "hope-io/net/linux/event_loop_impl.h"
#include "hope-io/net/init.h"

#include <iostream>
#include <thread>

int main(int argc, char *argv[]) {
    hope::io::init();

    const std::size_t port = 1338;  // Echo server port

    try {
        std::cout << "TCP Event Loop Server started on port " << port << std::endl;
        std::cout << "The server will automatically create a default TCP acceptor" << std::endl;

        using conn_state = hope::io::el::el_connection_state;

        // Set up callbacks for the event loop
        auto on_connect = [](auto& c) {
            std::cout << "[CONNECT] New connection fd: " << c.descriptor << std::endl;
            return conn_state::read;
        };

        auto on_read = [](auto& c) {
            // Print what we received
            const auto& buffer = c.buffer;
            const auto chunk = buffer->peek_used();
            std::cout << "[READ] Received " << chunk.second << " bytes from fd " << c.descriptor << std::endl;

            // Echo back the data: change to write state
            return conn_state::write;
        };

        auto on_write = [](auto& c) {
            std::cout << "[WRITE] Echoed data to fd: " << c.descriptor << std::endl;

            // Go back to read state for more data
            return conn_state::read;
        };

        auto on_err = [](auto& c, const std::string& error) {
            std::cout << "[ERROR] Connection fd " << c.descriptor << ": " << error << std::endl;
            return conn_state::die;
        };

        auto* event_loop = new hope::io::el::event_loop_impl_t(
            std::move(on_connect),
            std::move(on_read),
            std::move(on_write),
            std::move(on_err)
        );

        // Configure event loop - no custom_acceptor provided, so it creates a default TCP one
        hope::io::el::config cfg;
        cfg.port = port;
        cfg.max_mutual_connections = 100;
        cfg.max_accepts_per_tick = 10;
        cfg.epoll_temeout = 1000;

        // Run event loop in a separate thread
        std::thread loop_thread([event_loop, &cfg]() {
            event_loop->run(cfg);
        });

        std::cout << "Press Enter to stop the server..." << std::endl;
        std::cin.get();

        std::cout << "Stopping event loop..." << std::endl;
        event_loop->stop();

        loop_thread.join();

        delete event_loop;

        std::cout << "Server stopped." << std::endl;
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
#else
int main(int argc, char *argv[]) {}
#endif
