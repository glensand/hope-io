/* Copyright (C) 2024 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace hope::io {

    class udp_builder {
    public:
        virtual ~udp_builder() = default;

        [[nodiscard]] virtual int32_t platform_socket() const = 0;

        virtual void init(std::size_t port) = 0;
    };

}
