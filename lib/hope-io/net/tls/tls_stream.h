/* Copyright (C) 2024 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/stream.h"
#include "hope-io/net/tls/tls_init.h"
#include "hope-io/coredefs.h"

#ifdef HOPE_IO_USE_OPENSSL

namespace hope::io { class tcp_stream; }

#include "openssl/ssl.h"
#include "openssl/err.h"

#include <stdexcept>

namespace hope::io {

    class base_tls_stream : public stream {
    public:
        explicit base_tls_stream(tcp_stream* tcp_str = nullptr);
        virtual ~base_tls_stream();

        std::string get_endpoint() const override;
        int32_t platform_socket() const override;
        void disconnect() override;

        void write(const void *data, std::size_t length) override;
        void write_v(std::span<const std::span<const char>> buffers) override;
        size_t read(void *data, std::size_t length) override;
        size_t read_once(void* data, std::size_t length) override;
        void stream_in(std::string& buffer) override;
        void set_options(const stream_options& opt) override;

        size_t read_bytes(void* data, std::size_t length) const;

    protected:
        bool wait_for_ssl(int ssl_error, int timeout_ms);
        void handle_ssl_error(const char* op, int result);

        tcp_stream* m_tcp_stream{ nullptr };
        ssl_st* m_ssl{ nullptr };
        ssl_ctx_st* m_context{ nullptr };
    };

}

#endif
