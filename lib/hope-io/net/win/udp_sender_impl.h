/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/udp_sender.h"

#if PLATFORM_WINDOWS

namespace hope::io {

    class udp_sender_impl final : public udp_sender {
    public:
        udp_sender_impl() = default;

        [[nodiscard]] int32_t platform_socket() const override { return -1; }
        void connect(std::string_view, std::size_t) override {}
        void disconnect() override {}
        void write(const void*, std::size_t) override {}
    };

}

#endif
