/* Copyright (C) 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/tls/tls_init.h"

#include "openssl/ssl.h"
#include "openssl/err.h"

namespace hope::io {

    class base_tls_stream : public stream {
    public:
        base_tls_stream(stream* tcp_stream) 
            : m_tcp_stream(tcp_stream) {
            if (m_tcp_stream == nullptr) {
                m_tcp_stream = hope::io::create_stream();
            }
            
            hope::io::init_tls();
        }

        virtual ~base_tls_stream() override {
            base_tls_stream::disconnect();
            delete m_tcp_stream;
            hope::io::deinit_tls();
        }
    protected:

        virtual int32_t platform_socket() const override {
            return m_tcp_stream->platform_socket();
        }

        virtual void disconnect() override {
            if (m_ssl != nullptr) {
                SSL_shutdown(m_ssl);
                SSL_free(m_ssl);
                m_ssl = nullptr;
            }

            m_tcp_stream->disconnect();
        }

        virtual void write(const void *data, std::size_t length) override {
            std::size_t total = 0;
            do
            {
                const auto sent = SSL_write(m_ssl, (char*)data + total, length - total);
                if (sent <= 0)
                    throw std::runtime_error("hope-io/tls_stream: cannot write to socket");
                total += sent;
            }
            while (total < length);
        }

        virtual void read(void *data, std::size_t length) override {
            std::size_t total = 0;
            do
            {
                const auto received = SSL_read(m_ssl, (char*)data + total, length - total);
                if (received <= 0)
                    throw std::runtime_error("hope-io/tls_stream: cannot read from socket");

                total += received;
            }
            while (total < length);
        }

        stream* m_tcp_stream{ nullptr };

        ssl_st* m_ssl{ nullptr };
        ssl_ctx_st* m_context{ nullptr };
    };

}

