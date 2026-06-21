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
#include "hope-io/net/linux/tls_event_loop_impl.h"
#include "hope-io/net/init.h"
#include "hope-io/net/tls/tls_init.h"

#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

int main(int argc, char *argv[]) {
    hope::io::init();
    hope::io::init_tls();

    // Use certificates from the crt folder (relative to executable)
    const std::string key_path = "../crt/key.pem";
    const std::string cert_path = "../crt/cert.pem";
    const std::size_t port = 1443;  // TLS echo server port

    try {
        std::cout << "TLS Event Loop Server started on port " << port << std::endl;

        using conn_state = hope::io::el::el_connection_state;

        // Set up callbacks for the event loop
        auto on_connect = [](auto& c) {
            std::cout << "New TLS connection fd: " << c.descriptor << std::endl;
            return conn_state::read;
        };

        auto on_read = [](auto& c) {
            std::cout << "Data received from fd: " << c.descriptor << std::endl;

            // Echo back the data: change to write state
            return conn_state::write;
        };

        auto on_write = [](auto& c) {
            std::cout << "Data sent to fd: " << c.descriptor << std::endl;

            // Go back to read state for more data
            return conn_state::read;
        };

        auto on_err = [](auto& c, const std::string& error) {
            std::cout << "Error on connection fd " << c.descriptor << ": " << error << std::endl;
            return conn_state::die;
        };

        // Define the concrete event loop type from the lambdas
        using loop_type = hope::io::el::tls_event_loop_impl<
            std::decay_t<decltype(on_read)>,
            std::decay_t<decltype(on_write)>,
            std::decay_t<decltype(on_err)>,
            std::decay_t<decltype(on_connect)>
        >;

        auto* g_event_loop = static_cast<loop_type*>(nullptr);
        auto* event_loop = new loop_type(
            std::move(on_connect),
            std::move(on_read),
            std::move(on_write),
            std::move(on_err)
        );
        g_event_loop = event_loop;

        // Configure TLS event loop with cert paths and port
        hope::io::el::tls_config cfg;
        cfg.cert_path = cert_path;
        cfg.key_path = key_path;
        cfg.port = port;
        cfg.max_mutual_connections = 100;
        cfg.max_accepts_per_tick = 10;
        cfg.epoll_timeout = 1000;

        // Run event loop in a separate thread so we can stop it from main
        std::thread loop_thread([event_loop, &cfg]() {
            event_loop->run(cfg);
        });

        // Simple shutdown after 30 seconds or on user input
        std::cout << "Press Enter to stop the server..." << std::endl;
        std::cin.get();

        std::cout << "Stopping event loop..." << std::endl;
        g_event_loop->stop();

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
