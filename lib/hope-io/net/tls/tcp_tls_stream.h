/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/tls/tls_stream.h"

namespace hope::io {

    class tcp_tls_stream final : public base_tls_stream {
    public:
        using base_tls_stream::base_tls_stream;

        void connect(std::string_view ip, std::size_t port) override;
        void disconnect() override;
    };

}
