/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/udp_builder.h"

#if PLATFORM_LINUX || PLATFORM_APPLE

namespace hope::io {

    class udp_builder_impl final : public udp_builder {
    public:
        udp_builder_impl() = default;

        [[nodiscard]] int32_t platform_socket() const override;
        void init(std::size_t port) override;

    private:
        int m_socket{ -1 };
    };

}

#endif
