/* Copyright (C) 2024 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/tls/tls_init.h"
#include "hope-io/coredefs.h"

#ifdef HOPE_IO_USE_OPENSSL

#include "openssl/ssl.h"
#include "openssl/err.h"

#include <stdexcept>

namespace hope::io {

    class base_tls_stream : public stream {
    public:
        explicit base_tls_stream(stream* tcp_stream)
            : m_tcp_stream(tcp_stream) {
            if (m_tcp_stream == nullptr) {
                m_tcp_stream = hope::io::create_stream();
            }
        }

        virtual ~base_tls_stream() override {
            base_tls_stream::disconnect();
            delete m_tcp_stream;
        }
    protected:

        virtual std::string get_endpoint() const override {
            return m_tcp_stream->get_endpoint();
        }

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

        // Wait until the underlying socket is ready for the given SSL operation.
        // Uses select() which is available on all platforms.
        // Returns false on timeout.
        bool wait_for_ssl(int ssl_error, int timeout_ms);

        // Handle an SSL error from SSL_read/SSL_write:
        //   - WANT_READ/WANT_WRITE → poll and retry (caller loops)
        //   - EOF (SSL_read returns 0) → handled before calling this
        //   - real errors → throw
        void handle_ssl_error(const char* op, int result);

        virtual void write(const void *data, std::size_t length) override {
            if (length == 0) return;

            std::size_t total = 0;
            while (total < length) {
                const auto sent = SSL_write(m_ssl, static_cast<const char*>(data) + total,
                                            length - total);
                if (sent > 0) {
                    total += sent;
                } else {
                    handle_ssl_error("SSL_write", sent);
                }
            }
        }

        virtual void write_v(std::span<const std::span<const char>> buffers) override {
            // OpenSSL SSL_write takes a single buffer, so we fall back to sequential writes.
            // The underlying TCP stream could use writev, but TLS frames must be contiguous.
            for (auto& buf : buffers) {
                std::size_t total = 0;
                while (total < buf.size()) {
                    const auto sent = SSL_write(m_ssl, buf.data() + total, buf.size() - total);
                    if (sent > 0) {
                        total += sent;
                    } else {
                        handle_ssl_error("SSL_write", sent);
                    }
                }
            }
        }

        virtual size_t read(void *data, std::size_t length) override {
            std::size_t total = 0;
            while (total < length) {
                const auto received = SSL_read(m_ssl, (char*)data + total, (int)(length - total));
                if (received > 0) {
                    total += received;
                } else if (received == 0) {
                    HOPE_THROW("tls_stream", "SSL_read: connection closed by peer (close_notify)");
                } else {
                    handle_ssl_error("SSL_read", received);
                }
            }
            return total;
        }

        virtual size_t read_once(void* data, std::size_t length) override {
            const auto received = SSL_read(m_ssl, (char*)data, (int)length);
            if (received > 0) {
                return received;
            }
            if (received == 0) {
                return 0;  // orderly shutdown, no data
            }
            // WANT errors or real errors
            auto err = SSL_get_error(m_ssl, received);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                return 0;  // caller can poll and retry
            }
            HOPE_THROW("tls_stream", "SSL_read (read_once) failed: SSL_get_error=" + std::to_string(err));
            return 0;
        }

        virtual void stream_in(std::string& out_stream) override {
            constexpr static std::size_t BufferSize{ 1024 };
            char buffer[BufferSize];
            while (true) {
                auto bytes_read = SSL_read(m_ssl, buffer, BufferSize);
                if (bytes_read > 0) {
                    out_stream.append(buffer, bytes_read);
                } else if (bytes_read == 0) {
                    break;  // orderly shutdown
                } else {
                    auto err = SSL_get_error(m_ssl, bytes_read);
                    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                        // Poll and retry (blocking mode expected for stream_in)
                        wait_for_ssl(err, 3000);
                        continue;
                    }
                    HOPE_THROW("tls_stream", "SSL_read (stream_in) failed: SSL_get_error=" + std::to_string(err));
                }
            }
        }

        size_t read_bytes(void* data, std::size_t length) const {
            return SSL_read(m_ssl, data, (int)length);
        }

        virtual void set_options(const hope::io::stream_options& opt) override {
            assert(m_tcp_stream);
            m_tcp_stream->set_options(opt);
        }
        stream* m_tcp_stream{ nullptr };

        ssl_st* m_ssl{ nullptr };
        ssl_ctx_st* m_context{ nullptr };
    };

}

#endif
