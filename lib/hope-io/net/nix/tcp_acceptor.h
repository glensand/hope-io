/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/acceptor.h"

#if PLATFORM_LINUX || PLATFORM_APPLE

namespace hope::io {

    class tcp_acceptor final : public acceptor {
    public:
        tcp_acceptor() = default;

        stream* accept() override;
        void open(std::size_t port) override;
        void close() override;
        void set_options(const stream_options& opt) override;
        long long raw() const override;

    private:
        int m_socket{ -1 };
    };

}

#endif
