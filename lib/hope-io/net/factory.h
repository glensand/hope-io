/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include <string_view>

namespace hope::io {

    // TCP stuff
    class acceptor* create_acceptor();
    class stream* create_stream(unsigned long long socket = 0);
        
    // TLS stuff
    class acceptor* create_tls_acceptor(std::string_view key, std::string_view cert);
    class stream* create_tls_stream(stream* tcp_stream = nullptr);
    class stream* create_tls_websockets_stream(stream* tcp_stream = nullptr);

    // UDP stuff
    class udp_receiver* create_udp_receiver(unsigned long long socket = 0);
    class udp_sender* create_udp_sender(unsigned long long socket = 0);
    class udp_builder* create_udp_builder();

    // server loop
    class event_loop* create_event_loop();

}
