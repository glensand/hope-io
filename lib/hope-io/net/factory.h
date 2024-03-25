/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#include <string_view>

namespace hope::io {

    class acceptor* create_acceptor();
    class udp_builder* create_udp_builder();
    class acceptor* create_tls_acceptor(std::string_view key, std::string_view cert);

    class stream* create_stream(unsigned long long socket = 0);
    class stream* create_receiver(unsigned long long socket = 0);
    class stream* create_sender(unsigned long long socket = 0);
    class stream* create_tls_stream(stream* tcp_stream = nullptr);

}
