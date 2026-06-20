/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "tls_stream.h"
#include "hope-io/net/tls/tcp_tls_stream.h"
#include "hope-io/net/tls/ktls_enable.h"

#if PLATFORM_LINUX || PLATFORM_APPLE
#include "hope-io/net/nix/tcp_stream.h"
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/uio.h>
#elif PLATFORM_WINDOWS
#include "hope-io/net/win/tcp_stream.h"
#include <winsock2.h>
#endif

namespace hope::io {

    base_tls_stream::base_tls_stream(tcp_stream* tcp_str, const stream_options& opts)
        : m_tcp_stream(tcp_str)
        , m_options(opts) {
        if (m_tcp_stream == nullptr) {
            m_tcp_stream = new tcp_stream(static_cast<unsigned long long>(-1), m_options);
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
        HOPE_ASSERT(length > 0, "base_tls_stream::write: zero length");
#if PLATFORM_LINUX
        if (m_ktls_enabled) {
            std::size_t total = 0;
            while (total < length) {
                auto sent = ::send(m_tcp_stream->platform_socket(),
                                   (const char*)data + total, length - total, 0);
                if (sent <= 0) HOPE_THROW("tls_stream", "KTLS write failed");
                total += sent;
            }
            return;
        }
#endif
        std::size_t total = 0;
        while (total < length) {
            const auto sent = SSL_write(m_ssl, static_cast<const char*>(data) + total, (int)(length - total));
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
#if PLATFORM_LINUX
        if (m_ktls_enabled) {
            // KTLS: use writev directly — scatter-gather syscall, no gather buffer needed.
            std::array<iovec, 1024> iovs;
            auto count = buffers.size();
            for (auto i = 0u; i < count; ++i) {
                iovs[i] = iovec{const_cast<char*>(buffers[i].data()), buffers[i].size()};
            }
            auto op_res = writev(m_tcp_stream->platform_socket(), iovs.data(), (int)count);
            if (op_res == -1) HOPE_THROW_ERRNO("tls_stream", "KTLS writev failed");
            auto written = (std::size_t)op_res;
            if (written < total_size) {
                std::size_t skip = written;
                for (auto i = 0u; i < count; ++i) {
                    if (skip >= buffers[i].size()) { skip -= buffers[i].size(); continue; }
                    write(buffers[i].data() + skip, buffers[i].size() - skip);
                    skip = 0;
                }
            }
            return;
        }
#endif
        std::string gathered;
        gathered.reserve(total_size);
        for (auto& buf : buffers)
            gathered.append(buf.data(), buf.size());
        std::size_t written = 0;
        while (written < total_size) {
            const auto sent = SSL_write(m_ssl, gathered.data() + written, (int)(total_size - written));
            if (sent > 0) {
                written += sent;
            } else {
                handle_ssl_error("SSL_write", sent);
            }
        }
    }

    size_t base_tls_stream::read(void *data, std::size_t length) {
#if PLATFORM_LINUX
        if (m_ktls_enabled) {
            std::size_t total = 0;
            while (total < length) {
                auto received = ::recv(m_tcp_stream->platform_socket(),
                                       (char*)data + total, length - total, 0);
                if (received > 0) {
                    total += received;
                } else if (received == 0) {
                    HOPE_THROW("tls_stream", "KTLS read: connection closed by peer");
                } else {
                    HOPE_THROW("tls_stream", "KTLS read failed");
                }
            }
            return total;
        }
#endif
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
#if PLATFORM_LINUX
        if (m_ktls_enabled) {
            auto received = ::recv(m_tcp_stream->platform_socket(), (char*)data, length, 0);
            if (received < 0) return 0;
            return (std::size_t)received;
        }
#endif
        const auto received = SSL_read(m_ssl, (char*)data, (int)length);
        if (received > 0) return received;
        if (received == 0) return 0;
        auto err = SSL_get_error(m_ssl, received);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
        HOPE_THROW("tls_stream", "SSL_read (read_once) failed: SSL_get_error=" + std::to_string(err));
        return 0;
    }

    void base_tls_stream::stream_in(std::string& out_stream) {
#if PLATFORM_LINUX
        if (m_ktls_enabled) {
            constexpr static std::size_t BufferSize{ 1024 };
            char buffer[BufferSize];
            while (true) {
                auto bytes_read = ::recv(m_tcp_stream->platform_socket(), buffer, BufferSize, 0);
                if (bytes_read > 0) {
                    out_stream.append(buffer, bytes_read);
                } else if (bytes_read == 0) {
                    break;
                } else {
                    HOPE_THROW("tls_stream", "KTLS read (stream_in) failed");
                }
            }
            return;
        }
#endif
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
        m_options = opt;
        m_tcp_stream->set_options(opt);
    }

    void base_tls_stream::try_enable_ktls() {
#if PLATFORM_LINUX
        if (!m_ktls_enabled || !m_ssl) return;
        int fd = m_tcp_stream->platform_socket();
        if (fd < 0) return;
        m_ktls_enabled = try_enable_fd_ktls(m_ssl, fd, false);
#else
        m_ktls_enabled = false;
#endif
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
                SSL_CTX_set_verify(c, SSL_VERIFY_NONE, nullptr);
                SSL_CTX_set_session_cache_mode(c,
                    SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_LOOKUP);
                SSL_CTX_sess_set_cache_size(c, 128);

                // Optimise for speed: prefer ECDHE over DHE, prefer X25519
                SSL_CTX_set_cipher_list(c,
                    "TLS_AES_128_GCM_SHA256:"
                    "TLS_AES_256_GCM_SHA384:"
                    "ECDHE-ECDSA-AES128-GCM-SHA256:"
                    "ECDHE-ECDSA-AES256-GCM-SHA384:"
                    "ECDHE-RSA-AES128-GCM-SHA256:"
                    "ECDHE-RSA-AES256-GCM-SHA384");
                SSL_CTX_set1_curves_list(c, "X25519:prime256v1:secp384r1");
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

        // Attempt KTLS after successful handshake
        if (m_ktls_enabled) {
            try_enable_ktls();
        }
    }

    void tcp_tls_stream::disconnect() {
        base_tls_stream::disconnect();
        m_context = nullptr;
    }

}
