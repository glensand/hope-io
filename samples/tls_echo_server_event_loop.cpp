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
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include "hope-io/net/tls/tls_init.h"

#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

// Global event loop pointer for stop signal
hope::io::event_loop* g_event_loop = nullptr;

int main(int argc, char *argv[]) {
    hope::io::init();
    hope::io::init_tls();

    // Use certificates from the crt folder (relative to executable)
    const std::string key_path = "../crt/key.pem";
    const std::string cert_path = "../crt/cert.pem";
    const std::size_t port = 1443;  // TLS echo server port

    try {
        // Create TLS acceptor (wraps TCP acceptor, adds TLS layer)
        auto* tls_acceptor = hope::io::create_tls_acceptor(key_path, cert_path);
        tls_acceptor->open(port);

        std::cout << "TLS Event Loop Server started on port " << port << std::endl;

        // Configure event loop to use our TLS acceptor
        hope::io::event_loop::config cfg;
        cfg.custom_acceptor = tls_acceptor;  // Use the TLS acceptor instead of default TCP
        cfg.max_mutual_connections = 100;
        cfg.max_accepts_per_tick = 10;
        cfg.epoll_temeout = 1000;

        // Set up callbacks for the event loop
        hope::io::event_loop::callbacks callbacks;

        callbacks.on_connect = [](hope::io::event_loop::connection& conn) {
            std::cout << "New TLS connection from: " << conn.get_endpoint() << std::endl;
            conn.set_state(hope::io::event_loop::connection_state::read);
        };

        callbacks.on_read = [](hope::io::event_loop::connection& conn) {
            std::cout << "Data received from: " << conn.get_endpoint() << std::endl;
            
            // Echo back the data: change to write state
            conn.set_state(hope::io::event_loop::connection_state::write);
        };

        callbacks.on_write = [](hope::io::event_loop::connection& conn) {
            std::cout << "Data sent to: " << conn.get_endpoint() << std::endl;
            
            // Go back to read state for more data
            conn.set_state(hope::io::event_loop::connection_state::read);
        };

        callbacks.on_err = [](hope::io::event_loop::connection& conn, const std::string& error) {
            std::cout << "Error on connection " << conn.get_endpoint() << ": " << error << std::endl;
            conn.set_state(hope::io::event_loop::connection_state::die);
        };

        auto* event_loop = hope::io::create_event_loop();
        g_event_loop = event_loop;

        // Run event loop in a separate thread so we can stop it from main
        std::thread loop_thread([event_loop, &cfg, &callbacks]() {
            event_loop->run(cfg, std::move(callbacks));
        });

        // Simple shutdown after 30 seconds or on user input
        std::cout << "Press Enter to stop the server..." << std::endl;
        std::cin.get();

        std::cout << "Stopping event loop..." << std::endl;
        event_loop->stop();

        loop_thread.join();

        delete event_loop;
        delete tls_acceptor;

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
