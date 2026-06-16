/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "tls_stream.h"

#ifdef HOPE_IO_USE_OPENSSL

#if PLATFORM_LINUX || PLATFORM_APPLE
#include <sys/select.h>
#elif PLATFORM_WINDOWS
#include <winsock2.h>
#endif

namespace hope::io {

    bool base_tls_stream::wait_for_ssl(int ssl_error, int timeout_ms) {
        auto fd = m_tcp_stream->platform_socket();
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int ret;
        if (ssl_error == SSL_ERROR_WANT_READ) {
            ret = select(fd + 1, &fds, nullptr, nullptr, &tv);
        } else {
            ret = select(fd + 1, nullptr, &fds, nullptr, &tv);
        }
        return ret > 0;
    }

    void base_tls_stream::handle_ssl_error(const char* op, int result) {
        auto err = SSL_get_error(m_ssl, result);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            // Non-fatal: the underlying socket is not ready.
            // Poll with a short timeout to avoid busy-waiting.
            wait_for_ssl(err, 10);
            return;
        }
        if (err == SSL_ERROR_ZERO_RETURN) {
            HOPE_THROW("tls_stream", std::string(op) + ": connection closed by peer");
        }
        HOPE_THROW("tls_stream", std::string(op) + " failed: SSL_get_error=" + std::to_string(err));
    }

}

namespace {

    // Shared SSL_CTX for all client connections — avoids per-connect SSL_CTX_new() allocation.
    // Constructed once (thread-safe static local init in C++20).
    // Includes session caching for faster reconnects.
    ssl_ctx_st* get_shared_client_context() {
        static ssl_ctx_st* ctx = [] {
            auto* method = TLS_client_method();
            auto* c = SSL_CTX_new(method);
            if (c) {
                // Enable session caching on the client side.
                // When reconnecting to the same host, this skips the asymmetric-key handshake.
                SSL_CTX_set_session_cache_mode(c,
                    SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_LOOKUP);
                SSL_CTX_sess_set_cache_size(c, 128);
            }
            return c;
        }();
        return ctx;
    }

    class client_tls_stream final : public hope::io::base_tls_stream {
    public:
        using base_tls_stream::base_tls_stream;

        virtual void connect(std::string_view ip, std::size_t port) override {
            m_tcp_stream->connect(ip, port);

            // Use the shared context — no per-connect SSL_CTX_new() allocation.
            m_context = get_shared_client_context();
            if (m_context == nullptr) {
                throw std::runtime_error("hope-io/client_tls_stream: cannot create context");
            }

            m_ssl = SSL_new(m_context);
            SSL_set_fd(m_ssl, (int32_t)m_tcp_stream->platform_socket());

            // Set the hostname for SNI (Server Name Indication)
            SSL_set_tlsext_host_name(m_ssl, ip.data());

            if (SSL_connect(m_ssl) <= 0) {
                throw std::runtime_error("hope-io/client_tls_stream: cannot establish connection");
            }
        }

        virtual void disconnect() override {
            base_tls_stream::disconnect();
            // m_context is the shared global — do NOT SSL_CTX_free() it.
            m_context = nullptr;
        }
    };

}

namespace hope::io {

    stream* create_tls_stream(stream* tcp_stream) {
        return new client_tls_stream(tcp_stream);
    }

}

#else

namespace hope::io {
    stream* create_tls_stream(stream* tcp_stream) {
        assert(false && "hope-io/ OpenSSL is not available");
        return nullptr;
    }
}

#endif
