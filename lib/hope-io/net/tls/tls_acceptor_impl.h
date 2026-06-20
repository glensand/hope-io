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

#include <string>
#include "openssl/ssl.h"

namespace hope::io {

    class tls_acceptor_impl final : public acceptor {
    public:
        tls_acceptor_impl(std::string_view key, std::string_view cert,
                          const stream_options& opts = stream_options{});
        virtual ~tls_acceptor_impl() override;

        void open(std::size_t port) override;
        void close() override;
        stream* accept() override;
        void set_options(const stream_options& opt) override;
        long long raw() const override;

        void set_ktls_enabled(bool enabled) { m_ktls_enabled = enabled; }

    private:
        std::string m_key;
        std::string m_cert;

        acceptor* m_tcp_acceptor{ nullptr };
        SSL_CTX* m_context{ nullptr };
        bool m_ktls_enabled = false;
        stream_options m_opts;
    };

}
