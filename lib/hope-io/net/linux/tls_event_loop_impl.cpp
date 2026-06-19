/* Copyright (C) 2026 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#if PLATFORM_LINUX

#include "hope-io/net/linux/tls_event_loop_impl.h"
#include "hope-io/net/event_loop.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/tls/tls_init.h"

#include "openssl/ssl.h"
#include "openssl/err.h"

#include <deque>
#include <unordered_set>
#include <atomic>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

namespace hope::io {

    tls_event_loop_impl::tls_event_loop_impl() {
        event_loop::connection::on_state_changed = [this](const event_loop::connection& conn) {
            NAMED_SCOPE(TlsSetState);
            if (conn.get_state() == event_loop::connection_state::die) {
                remove_connection(conn.descriptor);
            } else {
                // Re-arm epoll with the correct event interest for the new state.
                // Edge-triggered (EPOLLET) means we must re-arm when the state transitions.
                epoll_event ev;
                ev.data.fd = conn.descriptor;
                ev.events = EPOLLRDHUP | EPOLLHUP | EPOLLET;
                if (conn.get_state() == event_loop::connection_state::read) {
                    ev.events |= EPOLLIN;
                } else if (conn.get_state() == event_loop::connection_state::write) {
                    ev.events |= EPOLLOUT;
                }
                epoll_ctl(m_epfd, EPOLL_CTL_MOD, conn.descriptor, &ev);
            }
        };
    }

    tls_event_loop_impl::~tls_event_loop_impl() {
        event_loop::connection::on_state_changed = nullptr;
        if (m_ctx) {
            SSL_CTX_free(m_ctx);
        }
        // Free any remaining prepooled/redeemed buffers
        m_pl.drain();
    }

    event_loop::fixed_size_buffer* tls_event_loop_impl::buffer_pool::allocate() {
        event_loop::fixed_size_buffer* allocated = nullptr;
        if (!m_impl.empty()) {
            allocated = m_impl.back();
            m_impl.pop_back();
        } else {
            allocated = new event_loop::fixed_size_buffer;
        }
        return allocated;
    }

    void tls_event_loop_impl::buffer_pool::redeem(event_loop::fixed_size_buffer* b) {
        b->reset();
        m_impl.emplace_back(b);
    }

    void tls_event_loop_impl::buffer_pool::prepool(std::size_t count) {
        for (auto i = 0; i < count; ++i)
            m_impl.emplace_back(new event_loop::fixed_size_buffer);
    }

    void tls_event_loop_impl::run(const tls_config& cfg, event_loop::callbacks&& cb) {
        THREAD_SCOPE(TLS_EVENT_LOOP_THREAD);

        init_tls();
        auto* method = TLS_server_method();
        m_ctx = SSL_CTX_new(method);
        if (!m_ctx) {
            throw std::runtime_error("tls_event_loop: SSL_CTX_new failed");
        }

        if (SSL_CTX_use_certificate_file(m_ctx, cfg.cert_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
            SSL_CTX_free(m_ctx);
            m_ctx = nullptr;
            throw std::runtime_error("tls_event_loop: cannot load certificate: " + cfg.cert_path);
        }

        if (SSL_CTX_use_PrivateKey_file(m_ctx, cfg.key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
            SSL_CTX_free(m_ctx);
            m_ctx = nullptr;
            throw std::runtime_error("tls_event_loop: cannot load key: " + cfg.key_path);
        }

        SSL_CTX_set_session_cache_mode(m_ctx, SSL_SESS_CACHE_SERVER);
        SSL_CTX_sess_set_cache_size(m_ctx, 128);

        m_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_listen_socket == -1) {
            SSL_CTX_free(m_ctx);
            m_ctx = nullptr;
            throw std::runtime_error(std::string("tls_event_loop: cannot create socket: ") + strerror(errno));
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
            throw std::runtime_error("tls_event_loop: epoll_create failed");
        }

        epoll_ctl_add(m_epfd, m_listen_socket, EPOLLIN | EPOLLET, cb);

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
                    handle_accept(cb);
                } else if (event.events & (EPOLLRDHUP | EPOLLHUP | POLLERR)) {
                    remove_connection(sock);
                } else if (m_pending_handshakes.count(sock)) {
                    retry_handshake(sock, cb);
                } else if (event.events & POLLIN) {
                    handle_read(m_connections[sock], cb);
                } else if (event.events & POLLOUT) {
                    handle_write(m_connections[sock], cb);
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

    void tls_event_loop_impl::stop() {
        m_running = false;
    }

    void tls_event_loop_impl::handle_accept(event_loop::callbacks& cb) {
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
                event_loop::connection dumb;
                cb.on_err(dumb, "tls_event_loop: cannot set nonblock");
                ::close(sock);
                continue;
            }

            SSL* ssl = SSL_new(m_ctx);
            if (!ssl) {
                event_loop::connection dumb;
                cb.on_err(dumb, "tls_event_loop: SSL_new failed");
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
                register_connection(sock, ssl, cb);
                cb.on_connect(m_connections[sock]);
            } else {
                int err = SSL_get_error(ssl, ret);
                if (err == SSL_ERROR_WANT_READ) {
                    epoll_ctl_add(m_epfd, sock, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLET, cb);
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

    void tls_event_loop_impl::retry_handshake(int32_t sock, event_loop::callbacks& cb) {
        NAMED_SCOPE(TlsRetryHandshake);
        auto& tls = m_tls_states[sock];
        int ret = SSL_do_handshake(tls.ssl);
        if (ret == 1) {
            m_pending_handshakes.erase(sock);
            register_connection(sock, tls.ssl, cb);
            cb.on_connect(m_connections[sock]);
        } else {
            int err = SSL_get_error(tls.ssl, ret);
            if (err != SSL_ERROR_WANT_READ) {
                SSL_free(tls.ssl);
                tls.ssl = nullptr;
                m_pending_handshakes.erase(sock);
                ::close(sock);
                event_loop::connection dumb;
                cb.on_err(dumb, "tls_event_loop: handshake failed");
            }
        }
    }

    void tls_event_loop_impl::register_connection(int32_t sock, SSL* ssl, event_loop::callbacks&) {
        NAMED_SCOPE(TlsRegisterConn);
        auto& tls = m_tls_states[sock];
        // ssl may already be set if the handshake was deferred (SSL_ERROR_WANT_READ in handle_accept).
        if (tls.ssl) {
            // Already set from the pending handshake path — nothing to do.
        } else {
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

    void tls_event_loop_impl::handle_read(event_loop::connection& conn, event_loop::callbacks& cb) {
        NAMED_SCOPE(TlsHandleRead);
        if (conn.get_state() != event_loop::connection_state::read) return;

        auto& tls = m_tls_states[conn.descriptor];
        bool error = false;
        std::size_t total_received = conn.buffer->consume_free([&](void* data, std::size_t size) -> std::size_t {
            ERR_clear_error();
            int received = SSL_read(tls.ssl, data, (int)size);
            if (received > 0) return (std::size_t)received;

            int err = SSL_get_error(tls.ssl, received);
            if (err == SSL_ERROR_WANT_READ) {
                return 0;
            }
            if (err == SSL_ERROR_ZERO_RETURN) {
                conn.set_state(event_loop::connection_state::die);
                error = true;
                return size;
            }
            cb.on_err(conn, "SSL_read failed");
            conn.set_state(event_loop::connection_state::die);
            error = true;
            return size;
        });

        if (total_received > 0 && !error) {
            cb.on_read(conn);
        }
    }

    void tls_event_loop_impl::handle_write(event_loop::connection& conn, event_loop::callbacks& cb) {
        NAMED_SCOPE(TlsHandleWrite);
        if (conn.get_state() != event_loop::connection_state::write) return;

        auto& tls = m_tls_states[conn.descriptor];
        conn.buffer->consume_used([&](const void* data, std::size_t size) -> std::size_t {
            ERR_clear_error();
            int sent = SSL_write(tls.ssl, data, (int)size);
            if (sent > 0) return (std::size_t)sent;

            int err = SSL_get_error(tls.ssl, sent);
            if (err == SSL_ERROR_WANT_WRITE) {
                return 0;
            }
            cb.on_err(conn, "SSL_write failed");
            conn.set_state(event_loop::connection_state::die);
            return size;
        });

        if (conn.buffer->is_empty()) {
            cb.on_write(conn);
        }
    }

    void tls_event_loop_impl::remove_connection(int32_t descriptor) {
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

    event_loop::connection& tls_event_loop_impl::connection_for_fd(int32_t fd) {
        if ((std::size_t)fd >= m_connections.size()) {
            m_connections.resize(fd + 1);
            m_tls_states.resize(fd + 1);
        }
        return m_connections[fd];
    }

    void tls_event_loop_impl::epoll_ctl_add(int32_t epfd, int32_t fd, uint32_t events, event_loop::callbacks& cb) {
        epoll_event ev;
        ev.events = events;
        ev.data.fd = fd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
            event_loop::connection dumb;
            cb.on_err(dumb, std::string("tls_event_loop: epoll_ctl ADD failed: ") + strerror(errno));
        }
    }

    void tls_event_loop_impl::throw_bind_err() {
        switch (errno) {
            case EACCES: throw std::runtime_error("Permission denied (need root for ports < 1024)");
            case EADDRINUSE: throw std::runtime_error("Address already in use");
            case EADDRNOTAVAIL: throw std::runtime_error("Invalid address");
            case EBADF: throw std::runtime_error("Invalid socket descriptor");
            case EINVAL: throw std::runtime_error("Socket already bound");
            case ENOTSOCK: throw std::runtime_error("Not a socket");
            default: throw std::runtime_error("Unknown bind error");
        }
    }

    tls_event_loop* create_tls_event_loop() { return new tls_event_loop_impl; }

}
#endif // PLATFORM_LINUX
