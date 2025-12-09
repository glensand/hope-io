/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "message.h"

#include "hope-io/net/event_loop.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"

#include <iostream>
#include <thread>

int main(int argc, char *argv[]) {
    hope::io::init();

    const std::size_t port = 1338;  // Echo server port

    try {
        std::cout << "TCP Event Loop Server started on port " << port << std::endl;
        std::cout << "The server will automatically create a default TCP acceptor" << std::endl;

        // Configure event loop - no custom_acceptor provided, so it creates a default TCP one
        hope::io::event_loop::config cfg;
        cfg.port = port;
        cfg.max_mutual_connections = 100;
        cfg.max_accepts_per_tick = 10;
        cfg.epoll_temeout = 1000;

        // Set up callbacks for the event loop
        hope::io::event_loop::callbacks callbacks;

        callbacks.on_connect = [](hope::io::event_loop::connection& conn) {
            std::cout << "[CONNECT] New connection from: " << conn.get_endpoint() << std::endl;
            conn.set_state(hope::io::event_loop::connection_state::read);
        };

        callbacks.on_read = [](hope::io::event_loop::connection& conn) {
            // Print what we received
            const auto& buffer = conn.buffer;
            const auto chunk = buffer->used_chunk();
            std::cout << "[READ] Received " << chunk.second << " bytes from " << conn.get_endpoint() << std::endl;
            
            // Echo back the data: change to write state
            conn.set_state(hope::io::event_loop::connection_state::write);
        };

        callbacks.on_write = [](hope::io::event_loop::connection& conn) {
            std::cout << "[WRITE] Echoed data to: " << conn.get_endpoint() << std::endl;
            
            // Go back to read state for more data
            conn.set_state(hope::io::event_loop::connection_state::read);
        };

        callbacks.on_err = [](hope::io::event_loop::connection& conn, const std::string& error) {
            std::cout << "[ERROR] Connection " << conn.get_endpoint() << ": " << error << std::endl;
            conn.set_state(hope::io::event_loop::connection_state::die);
        };

        auto* event_loop = hope::io::create_event_loop();

        // Run event loop in a separate thread
        std::thread loop_thread([event_loop, &cfg, &callbacks]() {
            event_loop->run(cfg, std::move(callbacks));
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
