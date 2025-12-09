/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <cstddef>

namespace hope::io {

    class udp_sender {
    public:
        virtual ~udp_sender() = default;

        [[nodiscard]] virtual int32_t platform_socket() const = 0;

        virtual void connect(std::string_view ip, std::size_t port) = 0;
        virtual void disconnect() = 0;

        virtual void write(const void* data, std::size_t length) = 0;
    };

}
