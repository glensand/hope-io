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
#include "hope-io/net/stream_options_util.h"
#include "hope-io/net/tls/tls_init.h"

#if PLATFORM_APPLE

#include "openssl/ssl.h"
#include "openssl/err.h"

#include <unordered_set>
#include <unordered_map>
#include <atomic>
#include <vector>
#include <sys/event.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <cstring>
#include <cerrno>

namespace hope::io::el {

    template<typename TOnRead, typename TOnWrite, typename TOnError, typename TConnected>
    class tls_event_loop_impl final
        : public tls_event_loop<TOnRead, TOnWrite, TOnError, TConnected> {
        using base = tls_event_loop<TOnRead, TOnWrite, TOnError, TConnected>;
    public:
        using callbacks = typename base::callbacks;
        tls_event_loop_impl(TConnected&& on_connect, TOnRead&& on_read, TOnWrite&& on_write, TOnError&& on_error)
            : m_callbacks{std::move(on_connect), std::move(on_read), std::move(on_write), std::move(on_error)} {}

        ~tls_event_loop_impl() override {
            if (m_ctx) {
                SSL_CTX_free(m_ctx);
            }
            if (m_kq != -1) {
                ::close(m_kq);
            }
            for (auto& [fd, tls] : m_tls_states) {
                if (tls.ssl) SSL_free(tls.ssl);
            }
            m_tls_states.clear();
            std::vector<fixed_size_buffer*> bufs;
            for (const auto& conn : m_connections) {
                if (conn.buffer) {
                    bufs.push_back(conn.buffer);
                }
            }
            m_connections.clear();
            for (auto* buf : bufs) {
                m_pl.redeem(buf);
            }
        }

        void run(const tls_config& cfg) override {
            THREAD_SCOPE(TLS_EVENT_LOOP_KQ);

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
            if (flags != -1) {
                fcntl(m_listen_socket, F_SETFL, flags | O_NONBLOCK);
            }
            listen(m_listen_socket, cfg.max_mutual_connections);

            m_kq = kqueue();
            if (m_kq == -1) {
                HOPE_THROW_ERRNO("tls_event_loop", "kqueue() failed");
            }

            struct kevent ev;
            EV_SET(&ev, m_listen_socket, EVFILT_READ, EV_ADD, 0, 0, nullptr);
            if (kevent(m_kq, &ev, 1, nullptr, 0, nullptr) == -1) {
                HOPE_THROW_ERRNO("tls_event_loop", "kevent register listen failed");
            }

            m_cfg = cfg;
            m_pl.prepool(cfg.max_mutual_connections);
            m_events.resize(cfg.max_mutual_connections);

            while (m_running.load(std::memory_order_acquire)) {
                NAMED_SCOPE(TlsKqTick);
                struct timespec timeout;
                timeout.tv_sec = cfg.epoll_timeout / 1000;
                timeout.tv_nsec = (cfg.epoll_timeout % 1000) * 1000000;

                int nfds;
                {
                    NAMED_SCOPE(TlsKevent);
                    nfds = kevent(m_kq, nullptr, 0, m_events.data(), (int)m_events.size(), &timeout);
                }

                if (nfds < 0) {
                    connection dumb;
                    m_callbacks.on_err(dumb, "tls_event_loop: kevent() failed");
                    break;
                }

                for (int i = 0; i < nfds; ++i) {
                    NAMED_SCOPE(TlsKqProcessOne);
                    auto& event = m_events[i];
                    auto fd = (int)event.ident;

                    if (event.flags & EV_EOF) {
                        remove_connection(fd);
                    } else if (fd == m_listen_socket) {
                        handle_accept();
                    } else if (m_pending_handshakes.count(fd)) {
                        retry_handshake(fd);
                    } else if (event.filter == EVFILT_READ) {
                        auto it = m_connections.find(fd);
                        if (it != m_connections.end()) {
                            handle_read(const_cast<connection&>(*it));
                        }
                    } else if (event.filter == EVFILT_WRITE) {
                        auto it = m_connections.find(fd);
                        if (it != m_connections.end()) {
                            handle_write(const_cast<connection&>(*it));
                        }
                    }
                }
            }
        }

        void stop() override {
            m_running = false;
        }

    private:
        struct tls_per_conn {
            SSL* ssl = nullptr;
        };



        // Apply connection state to kqueue:
        //   write -> add EVFILT_WRITE
        //   read  -> delete EVFILT_WRITE
        //   die   -> remove connection entirely
        void apply_state(connection& conn, connection_state state) {
            if (state == connection_state::die) {
                remove_connection(conn.descriptor);
                return;
            }
            conn.set_state(state);

            struct kevent ev;
            if (state == connection_state::write) {
                EV_SET(&ev, conn.descriptor, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
                kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
            } else if (state == connection_state::read) {
                EV_SET(&ev, conn.descriptor, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
                kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
            }
        }

        void handle_accept() {
            NAMED_SCOPE(TlsKqHandleAccept);
            for (auto i = 0; i < m_cfg.max_accepts_per_tick; ++i) {
                NAMED_SCOPE(TlsKqAcceptOne);
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
                    m_callbacks.on_err(dumb, "tls_event_loop: cannot set nonblock");
                    ::close(sock);
                    continue;
                }

                apply_stream_options(sock, m_cfg.accepted_stream_options);

                SSL* ssl = SSL_new(m_ctx);
                if (!ssl) {
                    connection dumb;
                    m_callbacks.on_err(dumb, "tls_event_loop: SSL_new failed");
                    ::close(sock);
                    continue;
                }
                SSL_set_fd(ssl, sock);
                SSL_set_accept_state(ssl);

                struct kevent ev[2];
                EV_SET(&ev[0], sock, EVFILT_READ, EV_ADD, 0, 0, nullptr);
                EV_SET(&ev[1], sock, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
                if (kevent(m_kq, ev, 2, nullptr, 0, nullptr) == -1) {
                    SSL_free(ssl);
                    ::close(sock);
                    continue;
                }

                int ret = SSL_do_handshake(ssl);
                if (ret == 1) {
                    register_connection(sock, ssl);
                    auto& conn = const_cast<connection&>(*m_connections.find(sock));
                    auto state = m_callbacks.on_connect(conn);
                    if (state == connection_state::die) {
                        remove_connection(sock);
                        continue;
                    }
                    apply_state(conn, state);
                } else {
                    int err = SSL_get_error(ssl, ret);
                    if (err == SSL_ERROR_WANT_READ) {
                        m_pending_handshakes.insert(sock);
                        m_tls_states[sock] = { ssl };
                    } else {
                        struct kevent del;
                        EV_SET(&del, sock, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
                        kevent(m_kq, &del, 1, nullptr, 0, nullptr);
                        SSL_free(ssl);
                        ::close(sock);
                    }
                }
            }
        }

        void retry_handshake(int32_t sock) {
            NAMED_SCOPE(TlsKqRetryHs);
            auto& tls = m_tls_states[sock];
            int ret = SSL_do_handshake(tls.ssl);
            if (ret == 1) {
                m_pending_handshakes.erase(sock);
                register_connection(sock, tls.ssl);
                auto& conn = const_cast<connection&>(*m_connections.find(sock));
                auto state = m_callbacks.on_connect(conn);
                if (state == connection_state::die) {
                    remove_connection(sock);
                    return;
                }
                apply_state(conn, state);
            } else {
                int err = SSL_get_error(tls.ssl, ret);
                if (err != SSL_ERROR_WANT_READ) {
                    SSL_free(tls.ssl);
                    tls.ssl = nullptr;
                    m_pending_handshakes.erase(sock);
                    m_tls_states.erase(sock);
                    struct kevent del;
                    EV_SET(&del, sock, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
                    kevent(m_kq, &del, 1, nullptr, 0, nullptr);
                    ::close(sock);
                    connection dumb;
                    m_callbacks.on_err(dumb, "tls_event_loop: handshake failed");
                }
            }
        }

        void register_connection(int32_t sock, SSL* ssl) {
            NAMED_SCOPE(TlsKqRegister);
            m_tls_states[sock] = { ssl };
            auto& conn = const_cast<connection&>(*m_connections.emplace(sock).first);
            conn.descriptor = sock;
            conn.buffer = m_pl.allocate();
        }

        void handle_read(connection& conn) {
            NAMED_SCOPE(TlsKqHandleRead);
            if (conn.get_state() != connection_state::read) return;

            bool error = false;
            bool got_data = false;

            while (true) {
                auto consumed = conn.buffer->consume_free([&](void* data, std::size_t size) -> std::size_t {
                    ERR_clear_error();
                    auto& tls = m_tls_states[conn.descriptor];
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
                    m_callbacks.on_err(conn, "SSL_read failed");
                    error = true;
                    return 0;
                });

                if (consumed == 0) break;
            }

            if (got_data && !error) {
                auto state = m_callbacks.on_read(conn);
                apply_state(conn, state);
            }
            if (error) {
                apply_state(conn, connection_state::die);
            }
        }

        void handle_write(connection& conn) {
            NAMED_SCOPE(TlsKqHandleWrite);
            if (conn.get_state() != connection_state::write) return;

            auto& tls = m_tls_states[conn.descriptor];

            conn.buffer->consume_used([&](const void* data, std::size_t size) -> std::size_t {
                ERR_clear_error();
                int sent = SSL_write(tls.ssl, data, (int)size);
                if (sent > 0) return (std::size_t)sent;

                int err = SSL_get_error(tls.ssl, sent);
                if (err == SSL_ERROR_WANT_WRITE) {
                    return 0;
                }
                m_callbacks.on_err(conn, "SSL_write failed");
                apply_state(conn, connection_state::die);
                return size;
            });

            if (conn.buffer->is_empty()) {
                auto state = m_callbacks.on_write(conn);
                apply_state(conn, state);
            }
        }

        void remove_connection(int32_t descriptor) {
            NAMED_SCOPE(TlsKqRemove);

            struct kevent ev;
            EV_SET(&ev, descriptor, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
            kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
            EV_SET(&ev, descriptor, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            kevent(m_kq, &ev, 1, nullptr, 0, nullptr);

            auto it = m_tls_states.find(descriptor);
            if (it != m_tls_states.end()) {
                if (it->second.ssl) {
                    SSL_free(it->second.ssl);
                }
                m_tls_states.erase(it);
            }

            m_pending_handshakes.erase(descriptor);
            ::close(descriptor);

            auto conn_it = m_connections.find(descriptor);
            if (conn_it != m_connections.end()) {
                auto& conn = const_cast<connection&>(*conn_it);
                if (conn.buffer) {
                    m_pl.redeem(conn.buffer);
                    conn.buffer = nullptr;
                }
                m_connections.erase(conn_it);
            }
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
        int m_kq = -1;
        SSL_CTX* m_ctx = nullptr;

        tls_config m_cfg;
        std::vector<struct kevent> m_events;
        std::unordered_set<connection, typename connection::hash> m_connections;
        std::unordered_map<int32_t, tls_per_conn> m_tls_states;
        std::unordered_set<int32_t> m_pending_handshakes;
        buffer_pool m_pl;
        std::atomic<bool> m_running = true;
        callbacks m_callbacks;
    };

}

#endif
