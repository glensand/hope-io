/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"

#if PLATFORM_WINDOWS

namespace hope::io {

    class tcp_acceptor final : public acceptor {
    public:
        explicit tcp_acceptor(const stream_options& opts = stream_options{});
        virtual ~tcp_acceptor() override;

        void open(std::size_t port) override;
        stream* accept() override;
        long long raw() const override;
        void set_options(const stream_options&) override;

    private:
        unsigned long long m_listen_socket{ 0 };
        stream_options m_options;
    };

}

#endif
