/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "tls_stream.h"
#include "hope-io/net/tls/tcp_tls_stream.h"
#include "hope-io/net/factory.h"

#ifdef HOPE_IO_USE_OPENSSL

#if PLATFORM_LINUX || PLATFORM_APPLE
#include <sys/select.h>
#elif PLATFORM_WINDOWS
#include <winsock2.h>
#endif

namespace hope::io {

    base_tls_stream::base_tls_stream(tcp_stream* tcp_str)
        : m_tcp_stream(tcp_str) {
        if (m_tcp_stream == nullptr) {
            m_tcp_stream = new tcp_stream();
        }
    }

    base_tls_stream::~base_tls_stream() {
        base_tls_stream::disconnect();
        delete m_tcp_stream;
    }

    std::string base_tls_stream::get_endpoint() const {
        return m_tcp_stream->get_endpoint();
    }

    int32_t base_tls_stream::platform_socket() const {
        return m_tcp_stream->platform_socket();
    }

    void base_tls_stream::disconnect() {
        if (m_ssl != nullptr) {
            SSL_shutdown(m_ssl);
            SSL_free(m_ssl);
            m_ssl = nullptr;
        }
        m_tcp_stream->disconnect();
    }

    void base_tls_stream::write(const void *data, std::size_t length) {
        if (length == 0) return;
        std::size_t total = 0;
        while (total < length) {
            const auto sent = SSL_write(m_ssl, static_cast<const char*>(data) + total, length - total);
            if (sent > 0) {
                total += sent;
            } else {
                handle_ssl_error("SSL_write", sent);
            }
        }
    }

    void base_tls_stream::write_v(std::span<const std::span<const char>> buffers) {
        std::size_t total_size = 0;
        for (auto& buf : buffers) total_size += buf.size();
        if (total_size == 0) return;
        std::string gathered;
        gathered.reserve(total_size);
        for (auto& buf : buffers)
            gathered.append(buf.data(), buf.size());
        std::size_t written = 0;
        while (written < total_size) {
            const auto sent = SSL_write(m_ssl, gathered.data() + written, total_size - written);
            if (sent > 0) {
                written += sent;
            } else {
                handle_ssl_error("SSL_write", sent);
            }
        }
    }

    size_t base_tls_stream::read(void *data, std::size_t length) {
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

    size_t base_tls_stream::read_once(void* data, std::size_t length) {
        const auto received = SSL_read(m_ssl, (char*)data, (int)length);
        if (received > 0) return received;
        if (received == 0) return 0;
        auto err = SSL_get_error(m_ssl, received);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
        HOPE_THROW("tls_stream", "SSL_read (read_once) failed: SSL_get_error=" + std::to_string(err));
        return 0;
    }

    void base_tls_stream::stream_in(std::string& out_stream) {
        constexpr static std::size_t BufferSize{ 1024 };
        char buffer[BufferSize];
        while (true) {
            auto bytes_read = SSL_read(m_ssl, buffer, BufferSize);
            if (bytes_read > 0) {
                out_stream.append(buffer, bytes_read);
            } else if (bytes_read == 0) {
                break;
            } else {
                auto err = SSL_get_error(m_ssl, bytes_read);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                    wait_for_ssl(err, 3000);
                    continue;
                }
                HOPE_THROW("tls_stream", "SSL_read (stream_in) failed: SSL_get_error=" + std::to_string(err));
            }
        }
    }

    size_t base_tls_stream::read_bytes(void* data, std::size_t length) const {
        return SSL_read(m_ssl, data, (int)length);
    }

    void base_tls_stream::set_options(const stream_options& opt) {
        assert(m_tcp_stream);
        m_tcp_stream->set_options(opt);
    }

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

    // Shared SSL_CTX for all client connections.
    ssl_ctx_st* get_shared_client_context() {
        static ssl_ctx_st* ctx = [] {
            auto* method = TLS_client_method();
            auto* c = SSL_CTX_new(method);
            if (c) {
                SSL_CTX_set_session_cache_mode(c,
                    SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_LOOKUP);
                SSL_CTX_sess_set_cache_size(c, 128);
            }
            return c;
        }();
        return ctx;
    }

}

namespace hope::io {

    void tcp_tls_stream::connect(std::string_view ip, std::size_t port) {
        m_tcp_stream->connect(ip, port);

        m_context = get_shared_client_context();
        if (m_context == nullptr) {
            throw std::runtime_error("hope-io/tcp_tls_stream: cannot create context");
        }

        m_ssl = SSL_new(m_context);
        SSL_set_fd(m_ssl, (int32_t)m_tcp_stream->platform_socket());

        SSL_set_tlsext_host_name(m_ssl, ip.data());

        if (SSL_connect(m_ssl) <= 0) {
            throw std::runtime_error("hope-io/tcp_tls_stream: cannot establish connection");
        }
    }

    void tcp_tls_stream::disconnect() {
        base_tls_stream::disconnect();
        m_context = nullptr;
    }

    stream* create_tls_stream(stream* tcp_str) {
        return new tcp_tls_stream(static_cast<tcp_stream*>(tcp_str));
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
