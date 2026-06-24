/* Copyright (C) 2026 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/coredefs.h"
#include "hope-io/net/tls_event_loop.h"
#include "hope-io/net/event_loop.h"
#include "hope-io/net/linux/event_loop_impl.h"
#include "hope-io/net/stream_options_util.h"
#include "hope-io/net/tls/ktls_enable.h"

#if PLATFORM_LINUX

#include "openssl/ssl.h"
#include "openssl/err.h"

#include <unordered_set>
#include <atomic>
#include <vector>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <cstring>
#include <cerrno>
#include <cstdlib>

namespace hope::io::el {

    template<typename TOnRead, typename TOnWrite, typename TOnError, typename TConnected>
    class tls_event_loop_impl final
        : public tls_event_loop<TOnRead, TOnWrite, TOnError, TConnected> {
        using base = tls_event_loop<TOnRead, TOnWrite, TOnError, TConnected>;
    public:
        tls_event_loop_impl(TConnected&& on_connect, TOnRead&& on_read, TOnWrite&& on_write, TOnError&& on_error)
            : m_on_connect(std::move(on_connect))
            , m_on_read(std::move(on_read))
            , m_on_write(std::move(on_write))
            , m_on_err(std::move(on_error)) {}

        ~tls_event_loop_impl() override {
            if (m_ctx) {
                SSL_CTX_free(m_ctx);
            }
            m_pl.drain();
        }

        void run(const tls_config& cfg) override {
            THREAD_SCOPE(TLS_EVENT_LOOP_THREAD);

            init_tls();
            auto* method = TLS_server_method();
            m_ctx = SSL_CTX_new(method);
            if (!m_ctx) {
                HOPE_THROW("tls_event_loop", "SSL_CTX_new failed");
            }

            if (SSL_CTX_use_certificate_file(m_ctx, cfg.cert_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
                SSL_CTX_free(m_ctx);
                m_ctx = nullptr;
                HOPE_THROW("tls_event_loop", "cannot load certificate: " + cfg.cert_path);
            }

            if (SSL_CTX_use_PrivateKey_file(m_ctx, cfg.key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
                SSL_CTX_free(m_ctx);
                m_ctx = nullptr;
                HOPE_THROW("tls_event_loop", "cannot load key: " + cfg.key_path);
            }

            SSL_CTX_set_session_cache_mode(m_ctx, SSL_SESS_CACHE_SERVER);
            SSL_CTX_sess_set_cache_size(m_ctx, 128);

            // Optimise for speed: prefer ECDHE over DHE, prefer X25519
            SSL_CTX_set_cipher_list(m_ctx,
                "TLS_AES_128_GCM_SHA256:"
                "TLS_AES_256_GCM_SHA384:"
                "ECDHE-ECDSA-AES128-GCM-SHA256:"
                "ECDHE-ECDSA-AES256-GCM-SHA384:"
                "ECDHE-RSA-AES128-GCM-SHA256:"
                "ECDHE-RSA-AES256-GCM-SHA384");
            SSL_CTX_set1_curves_list(m_ctx, "X25519:prime256v1:secp384r1");

            m_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (m_listen_socket == -1) {
                SSL_CTX_free(m_ctx);
                m_ctx = nullptr;
                HOPE_THROW_ERRNO("tls_event_loop", "cannot create socket");
            }

            int reuse = 1;
            setsockopt(m_listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

            sockaddr_in srv_addr{};
            srv_addr.sin_family = AF_INET;
            srv_addr.sin_addr.s_addr = INADDR_ANY;
            srv_addr.sin_port = htons(cfg.port);
            if (bind(m_listen_socket, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) == -1) {
                throw_bind_err();
            }

            auto flags = fcntl(m_listen_socket, F_GETFL, 0);
            fcntl(m_listen_socket, F_SETFL, flags | O_NONBLOCK);
            listen(m_listen_socket, cfg.max_mutual_connections);

            m_epfd = epoll_create(1);
            if (m_epfd == -1) {
                HOPE_THROW_ERRNO("tls_event_loop", "epoll_create failed");
            }

            {
                epoll_event ev;
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = m_listen_socket;
                epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_listen_socket, &ev);
            }

            m_cfg = cfg;
            m_pl.prepool(cfg.max_mutual_connections);
            m_events.resize(cfg.max_mutual_connections);
            m_connections.resize(cfg.max_mutual_connections + 1);
            m_tls_states.resize(cfg.max_mutual_connections + 1);

            while (m_running.load(std::memory_order_acquire)) {
                NAMED_SCOPE(TlsTick);
                auto nfds = 0;
                {
                    NAMED_SCOPE(TlsEpoll);
                    nfds = epoll_wait(m_epfd, m_events.data(), (int)m_events.size(), cfg.epoll_timeout);
                }

                for (auto i = 0; i < nfds; ++i) {
                    NAMED_SCOPE(TlsProcessOne);
                    auto& event = m_events[i];
                    auto sock = event.data.fd;

                    if (sock == m_listen_socket) {
                        handle_accept();
                    } else if (event.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                        remove_connection(sock);
                    } else if (m_pending_handshakes.count(sock)) {
                        retry_handshake(sock);
                    } else if (event.events & EPOLLIN) {
                        handle_read(m_connections[sock]);
                    } else if (event.events & EPOLLOUT) {
                        handle_write(m_connections[sock]);
                    }
                }
            }

            // Cleanup all remaining connections
            for (auto& tls : m_tls_states) {
                if (tls.ssl) {
                    SSL_free(tls.ssl);
                    tls.ssl = nullptr;
                }
            }
            for (auto& conn : m_connections) {
                if (conn.buffer) {
                    m_pl.redeem(conn.buffer);
                    conn.buffer = nullptr;
                }
            }
            m_pending_handshakes.clear();

            close(m_listen_socket);
            close(m_epfd);
        }

        void stop() override {
            m_running = false;
        }

    private:
        struct tls_per_conn {
            SSL* ssl = nullptr;
            bool ktls_active = false;
        };



        void apply_state(connection& conn, el_connection_state state) {
                    if (state == el_connection_state::die) {
                        remove_connection(conn.descriptor);
                        return;
                    }
                    conn.set_state(state);
                    epoll_event ev;
                    ev.events = EPOLLRDHUP | EPOLLHUP | EPOLLET;
                    ev.data.fd = conn.descriptor;
                    if (state == el_connection_state::read) {
                        ev.events |= EPOLLIN;
                    } else if (state == el_connection_state::write) {
                        ev.events |= EPOLLOUT;
                    }
                    epoll_ctl(m_epfd, EPOLL_CTL_MOD, conn.descriptor, &ev);
                }

        void handle_accept() {
            NAMED_SCOPE(TlsHandleAccept);
            for (auto i = 0; i < m_cfg.max_accepts_per_tick; ++i) {
                NAMED_SCOPE(TlsAcceptOne);
                struct sockaddr_in client_addr{};
                socklen_t socklen = sizeof(client_addr);
                int sock = accept(m_listen_socket, (struct sockaddr*)&client_addr, &socklen);
                if (sock == -1) break;

                int flags = fcntl(sock, F_GETFL, 0);
                if (flags == -1) {
                    ::close(sock);
                    break;
                }
                flags |= O_NONBLOCK;
                if (fcntl(sock, F_SETFL, flags) == -1) {
                    connection dumb;
                    m_on_err(dumb, "tls_event_loop: cannot set nonblock");
                    ::close(sock);
                    continue;
                }

                apply_stream_options(sock, m_cfg.accepted_stream_options);

                SSL* ssl = SSL_new(m_ctx);
                if (!ssl) {
                    connection dumb;
                    m_on_err(dumb, "tls_event_loop: SSL_new failed");
                    ::close(sock);
                    continue;
                }
                SSL_set_fd(ssl, sock);
                SSL_set_accept_state(ssl);

                // Ensure vectors are large enough for this fd
                if ((std::size_t)sock >= m_connections.size()) {
                    m_connections.resize(sock + 1);
                }
                if ((std::size_t)sock >= m_tls_states.size()) {
                    m_tls_states.resize(sock + 1);
                }

                int ret = SSL_do_handshake(ssl);
                if (ret == 1) {
                    // Handshake completed immediately
                    register_connection(sock, ssl);
                    if (m_cfg.enable_ktls) {
                        m_tls_states[sock].ktls_active = try_enable_fd_ktls(ssl, sock, true);
                    }

                    auto& conn = m_connections[sock];
                    auto state = m_on_connect(conn);
                    if (state == el_connection_state::die) {
                        remove_connection(sock);
                        continue;
                    }
                    apply_state(conn, state);

                    // Drain any application data that arrived during the handshake
                    // (edge-triggered epoll won't fire EPOLLIN again for it).
                    if (state == el_connection_state::read) {
                        handle_read(conn);
                    }
                } else {
                    int err = SSL_get_error(ssl, ret);
                    if (err == SSL_ERROR_WANT_READ) {
                        epoll_event ev;
                        ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLET;
                        ev.data.fd = sock;
                        epoll_ctl(m_epfd, EPOLL_CTL_ADD, sock, &ev);
                        m_pending_handshakes.insert(sock);
                        auto& tls = m_tls_states[sock];
                        tls.ssl = ssl;
                        connection_for_fd(sock).descriptor = sock;
                    } else {
                        SSL_free(ssl);
                        ::close(sock);
                    }
                }
            }
        }

        void retry_handshake(int32_t sock) {
            NAMED_SCOPE(TlsRetryHandshake);
            auto& tls = m_tls_states[sock];
            int ret = SSL_do_handshake(tls.ssl);
            if (ret == 1) {
                m_pending_handshakes.erase(sock);
                register_connection(sock, tls.ssl);
                if (m_cfg.enable_ktls) {
                    tls.ktls_active = try_enable_fd_ktls(tls.ssl, sock, true);
                }

                auto& conn = m_connections[sock];
                auto state = m_on_connect(conn);
                if (state == el_connection_state::die) {
                    remove_connection(sock);
                    return;
                }
                apply_state(conn, state);

                // Drain any application data that arrived during the deferred handshake
                if (state == el_connection_state::read) {
                    handle_read(conn);
                }
            } else {
                int err = SSL_get_error(tls.ssl, ret);
                if (err != SSL_ERROR_WANT_READ) {
                    SSL_free(tls.ssl);
                    tls.ssl = nullptr;
                    m_pending_handshakes.erase(sock);
                    ::close(sock);
                    connection dumb;
                    m_on_err(dumb, "tls_event_loop: handshake failed");
                }
            }
        }

        void register_connection(int32_t sock, SSL* ssl) {
            NAMED_SCOPE(TlsRegisterConn);
            auto& tls = m_tls_states[sock];
            // ssl may already be set if the handshake was deferred (SSL_ERROR_WANT_READ in handle_accept).
            if (!tls.ssl) {
                tls.ssl = ssl;
            }

            auto& conn = connection_for_fd(sock);
            conn.descriptor = sock;
            conn.buffer = m_pl.allocate();

            epoll_event ev;
            ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLET;
            ev.data.fd = sock;
            // Socket may already be in epoll (from a pending handshake) or not yet added
            // (from an immediate handshake success). Try ADD first; if EEXIST, use MOD.
            if (epoll_ctl(m_epfd, EPOLL_CTL_ADD, sock, &ev) == -1 && errno == EEXIST) {
                epoll_ctl(m_epfd, EPOLL_CTL_MOD, sock, &ev);
            }
        }

        void handle_read(connection& conn) {
            NAMED_SCOPE(TlsHandleRead);
            if (conn.get_state() != el_connection_state::read) return;

            auto& tls = m_tls_states[conn.descriptor];
            bool error = false;
            bool got_data = false;

            if (tls.ktls_active) {
                // KTLS path: raw recv() — kernel handles decryption.
                // Branch predicted well since all connections share the same mode.
                while (true) {
                    auto consumed = conn.buffer->consume_free([&](void* data, std::size_t size) -> std::size_t {
                        auto received = ::recv(conn.descriptor, (char*)data, size, 0);
                        if (received > 0) {
                            got_data = true;
                            return (std::size_t)received;
                        }
                        if (received == 0 || (errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                            return 0;
                        }
                        error = true;
                        return 0;
                    });
                    if (consumed == 0) break;
                }
            } else {
                // Standard SSL path: use SSL_read
                while (true) {
                    auto consumed = conn.buffer->consume_free([&](void* data, std::size_t size) -> std::size_t {
                        ERR_clear_error();
                        int received = SSL_read(tls.ssl, data, (int)size);
                        if (received > 0) {
                            got_data = true;
                            return (std::size_t)received;
                        }

                        int err = SSL_get_error(tls.ssl, received);
                        if (err == SSL_ERROR_WANT_READ) {
                            return 0;
                        }
                        if (err == SSL_ERROR_ZERO_RETURN) {
                            return 0;
                        }
                        error = true;
                        return 0;
                    });

                    if (consumed == 0) break;
                }
            }

            if (got_data && !error) {
                auto state = m_on_read(conn);
                apply_state(conn, state);
            }
            if (error) {
                apply_state(conn, el_connection_state::die);
            }
        }

        void handle_write(connection& conn) {
            NAMED_SCOPE(TlsHandleWrite);
            if (conn.get_state() != el_connection_state::write) return;

            auto& tls = m_tls_states[conn.descriptor];

            if (tls.ktls_active) {
                // KTLS path: raw send() — kernel handles encryption.
                // Branch predicted well since all connections share the same mode.
                conn.buffer->consume_used([&](const void* data, std::size_t size) -> std::size_t {
                    auto sent = ::send(conn.descriptor, (const char*)data, size, 0);
                    if (sent > 0) return (std::size_t)sent;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        return 0;
                    }
                    m_on_err(conn, "KTLS write failed");
                    apply_state(conn, el_connection_state::die);
                    return size;
                });
            } else {
                // Standard SSL path: use SSL_write
                conn.buffer->consume_used([&](const void* data, std::size_t size) -> std::size_t {
                    ERR_clear_error();
                    int sent = SSL_write(tls.ssl, data, (int)size);
                    if (sent > 0) return (std::size_t)sent;

                    int err = SSL_get_error(tls.ssl, sent);
                    if (err == SSL_ERROR_WANT_WRITE) {
                        return 0;
                    }
                    m_on_err(conn, "SSL_write failed");
                    apply_state(conn, el_connection_state::die);
                    return size;
                });
            }

            if (conn.buffer->is_empty()) {
                auto state = m_on_write(conn);
                apply_state(conn, state);
            }
        }

        void remove_connection(int32_t descriptor) {
            NAMED_SCOPE(TlsRemoveConn);
            epoll_ctl(m_epfd, EPOLL_CTL_DEL, descriptor, nullptr);

            auto& tls = m_tls_states[descriptor];
            if (tls.ssl) {
                SSL_free(tls.ssl);
                tls.ssl = nullptr;
            }
            m_pending_handshakes.erase(descriptor);

            close(descriptor);

            auto& conn = connection_for_fd(descriptor);
            if (conn.buffer) {
                m_pl.redeem(conn.buffer);
                conn.buffer = nullptr;
            }
        }

        connection& connection_for_fd(int32_t fd) {
            if ((std::size_t)fd >= m_connections.size()) {
                m_connections.resize(fd + 1);
                m_tls_states.resize(fd + 1);
            }
            return m_connections[fd];
        }

        void throw_bind_err() {
            switch (errno) {
                case EACCES: HOPE_THROW("tls_event_loop", "Permission denied (need root for ports < 1024)");
                case EADDRINUSE: HOPE_THROW("tls_event_loop", "Address already in use");
                case EADDRNOTAVAIL: HOPE_THROW("tls_event_loop", "Invalid address");
                case EBADF: HOPE_THROW("tls_event_loop", "Invalid socket descriptor");
                case EINVAL: HOPE_THROW("tls_event_loop", "Socket already bound");
                case ENOTSOCK: HOPE_THROW("tls_event_loop", "Not a socket");
                default: HOPE_THROW_ERRNO("tls_event_loop", "bind failed");
            }
        }

        int32_t m_listen_socket = -1;
        int32_t m_epfd = -1;
        SSL_CTX* m_ctx = nullptr;

        tls_config m_cfg;
        std::vector<epoll_event> m_events;
        std::vector<connection> m_connections;
        std::vector<tls_per_conn> m_tls_states;
        std::unordered_set<int32_t> m_pending_handshakes;
        buffer_pool m_pl;
        std::atomic<bool> m_running = true;
        TOnError m_on_err;
        TOnWrite m_on_write;
        TOnRead m_on_read;
        TConnected m_on_connect;
    };

}

#endif
