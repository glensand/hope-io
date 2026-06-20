/* Copyright (C) 2026 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#if PLATFORM_APPLE

#include "hope-io/net/nix/tls_event_loop_impl.h"
#include "hope-io/net/tls_event_loop.h"
#include "hope-io/net/event_loop.h"
#include "hope-io/net/tls/tls_init.h"

#include "openssl/ssl.h"
#include "openssl/err.h"

#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <atomic>

#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

namespace hope::io {

namespace {

void apply_stream_options(int fd, const stream_options& opt) {
    if (opt.tcp_nodelay) {
        int on = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    }

    if (opt.keepalive) {
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
    }

#ifdef TCP_KEEPIDLE
    if (opt.keepidle >= 0) {
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &opt.keepidle, sizeof(opt.keepidle));
    }
#endif

#ifdef TCP_KEEPINTVL
    if (opt.keepintvl >= 0) {
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &opt.keepintvl, sizeof(opt.keepintvl));
    }
#endif

#ifdef TCP_KEEPCNT
    if (opt.keepcnt >= 0) {
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &opt.keepcnt, sizeof(opt.keepcnt));
    }
#endif

    if (opt.send_buffer_size >= 0) {
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &opt.send_buffer_size, sizeof(opt.send_buffer_size));
    }

    if (opt.recv_buffer_size >= 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &opt.recv_buffer_size, sizeof(opt.recv_buffer_size));
    }

    if (opt.linger_on) {
        struct linger l;
        l.l_onoff = opt.linger_on;
        l.l_linger = opt.linger_seconds;
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
    }

#ifdef IP_TTL
    if (opt.ttl >= 0) {
        setsockopt(fd, IPPROTO_IP, IP_TTL, &opt.ttl, sizeof(opt.ttl));
    }
#endif

#ifdef IP_TOS
    if (opt.tos >= 0) {
        setsockopt(fd, IPPROTO_IP, IP_TOS, &opt.tos, sizeof(opt.tos));
    }
#endif
}

} // anonymous namespace

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

    tls_event_loop_impl::tls_event_loop_impl() {
        event_loop::connection::on_state_changed = [this](const event_loop::connection& conn) {
            NAMED_SCOPE(TlsSetStateKq);
            if (conn.get_state() == event_loop::connection_state::die) {
                remove_connection(conn.descriptor);
            }
        };
    }

    tls_event_loop_impl::~tls_event_loop_impl() {
        event_loop::connection::on_state_changed = nullptr;
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
        std::vector<event_loop::fixed_size_buffer*> bufs;
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

    void tls_event_loop_impl::run(const tls_config& cfg, event_loop::callbacks&& cb) {
        THREAD_SCOPE(TLS_EVENT_LOOP_KQ);

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
        if (flags != -1) {
            fcntl(m_listen_socket, F_SETFL, flags | O_NONBLOCK);
        }
        listen(m_listen_socket, cfg.max_mutual_connections);

        m_kq = kqueue();
        if (m_kq == -1) {
            throw std::runtime_error("tls_event_loop: kqueue() failed");
        }

        struct kevent ev;
        EV_SET(&ev, m_listen_socket, EVFILT_READ, EV_ADD, 0, 0, nullptr);
        if (kevent(m_kq, &ev, 1, nullptr, 0, nullptr) == -1) {
            throw std::runtime_error("tls_event_loop: kevent register listen failed");
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
                event_loop::connection dumb;
                cb.on_err(dumb, "tls_event_loop: kevent() failed");
                break;
            }

            for (int i = 0; i < nfds; ++i) {
                NAMED_SCOPE(TlsKqProcessOne);
                auto& event = m_events[i];
                auto fd = (int)event.ident;

                if (event.flags & EV_EOF) {
                    remove_connection(fd);
                } else if (fd == m_listen_socket) {
                    handle_accept(cb);
                } else if (m_pending_handshakes.count(fd)) {
                    retry_handshake(fd, cb);
                } else if (event.filter == EVFILT_READ) {
                    auto it = m_connections.find(fd);
                    if (it != m_connections.end()) {
                        handle_read((event_loop::connection&)*it, cb);
                    }
                } else if (event.filter == EVFILT_WRITE) {
                    auto it = m_connections.find(fd);
                    if (it != m_connections.end()) {
                        handle_write((event_loop::connection&)*it, cb);
                    }
                }
            }
        }
    }

    void tls_event_loop_impl::stop() {
        m_running = false;
    }

    void tls_event_loop_impl::handle_accept(event_loop::callbacks& cb) {
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
                event_loop::connection dumb;
                cb.on_err(dumb, "tls_event_loop: cannot set nonblock");
                ::close(sock);
                continue;
            }

            apply_stream_options(sock, m_cfg.accepted_stream_options);

            SSL* ssl = SSL_new(m_ctx);
            if (!ssl) {
                event_loop::connection dumb;
                cb.on_err(dumb, "tls_event_loop: SSL_new failed");
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
                register_connection(sock, ssl, cb);
                cb.on_connect((event_loop::connection&)*m_connections.find(sock));
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

    void tls_event_loop_impl::retry_handshake(int32_t sock, event_loop::callbacks& cb) {
        NAMED_SCOPE(TlsKqRetryHs);
        auto& tls = m_tls_states[sock];
        int ret = SSL_do_handshake(tls.ssl);
        if (ret == 1) {
            m_pending_handshakes.erase(sock);
            register_connection(sock, tls.ssl, cb);
            cb.on_connect((event_loop::connection&)*m_connections.find(sock));
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
                event_loop::connection dumb;
                cb.on_err(dumb, "tls_event_loop: handshake failed");
            }
        }
    }

    void tls_event_loop_impl::register_connection(int32_t sock, SSL* ssl, event_loop::callbacks&) {
        NAMED_SCOPE(TlsKqRegister);
        m_tls_states[sock] = { ssl };
        auto&& conn = (event_loop::connection&)*m_connections.emplace(sock).first;
        conn.descriptor = sock;
        conn.buffer = m_pl.allocate();
    }

    void tls_event_loop_impl::handle_read(event_loop::connection& conn, event_loop::callbacks& cb) {
        NAMED_SCOPE(TlsKqHandleRead);
        if (conn.get_state() != event_loop::connection_state::read) return;

        bool error = false;
        std::size_t total_received = conn.buffer->consume_free([&](void* data, std::size_t size) -> std::size_t {
            ERR_clear_error();
            auto& tls = m_tls_states[conn.descriptor];
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
        NAMED_SCOPE(TlsKqHandleWrite);
        if (conn.get_state() != event_loop::connection_state::write) return;

        conn.buffer->consume_used([&](const void* data, std::size_t size) -> std::size_t {
            ERR_clear_error();
            auto& tls = m_tls_states[conn.descriptor];
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
            auto& conn = (event_loop::connection&)*conn_it;
            if (conn.buffer) {
                m_pl.redeem(conn.buffer);
                conn.buffer = nullptr;
            }
            m_connections.erase(conn_it);
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
#endif
