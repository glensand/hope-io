/* Copyright (C) 2026 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/event_loop.h"
#include "hope-io/net/stream.h"
#include <string>

namespace hope::io {

    class tls_event_loop {
    public:
        struct tls_config final {
            std::string cert_path;               // server cert (PEM)
            std::string key_path;                // server key (PEM)
            std::size_t port = 443;
            std::size_t max_mutual_connections = 1024;
            std::size_t max_accepts_per_tick = 128;
            int epoll_timeout = 1000;            // ms
            bool verify_peer = false;            // optional mTLS
            bool enable_ktls = false;            // attempt KTLS on each accepted connection
            stream_options accepted_stream_options;  // socket options applied to each accepted connection
        };

        virtual ~tls_event_loop() = default;
        virtual void run(const tls_config& cfg, event_loop::callbacks&& cb) = 0;
        virtual void stop() = 0;
    };

    tls_event_loop* create_tls_event_loop();

}
