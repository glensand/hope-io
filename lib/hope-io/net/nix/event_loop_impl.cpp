/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * terms of the MIT license.
 */
#include "hope-io/coredefs.h"
#if PLATFORM_APPLE
#include "hope-io/net/nix/event_loop_impl.h"
#include "hope-io/net/nix/tcp_acceptor.h"
#include "hope-io/net/acceptor.h"
#include <deque>
#include <unordered_set>
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
    std::function<void(const event_loop::connection& conn)> event_loop::connection::on_state_changed;

    event_loop_impl::event_loop_impl() {
        event_loop::connection::on_state_changed = [this](const event_loop::connection& conn) {
            struct kevent ev;
            if (conn.get_state() == event_loop::connection_state::write) {
                EV_SET(&ev, conn.descriptor, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
                kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
            } else if (conn.get_state() == event_loop::connection_state::read) {
                EV_SET(&ev, conn.descriptor, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
                kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
            }
        };
    }
    event_loop_impl::~event_loop_impl() {
        event_loop::connection::on_state_changed = nullptr;
    }
    event_loop::fixed_size_buffer* event_loop_impl::buffer_pool::allocate() {
        event_loop::fixed_size_buffer* allocated = nullptr;
        if (!m_impl.empty()) { allocated = m_impl.back(); m_impl.pop_back(); }
        else { allocated = new event_loop::fixed_size_buffer; }
        return allocated;
    }
    void event_loop_impl::buffer_pool::redeem(event_loop::fixed_size_buffer* b) { b->reset(); m_impl.emplace_back(b); }
    void event_loop_impl::buffer_pool::prepool(std::size_t count) {
        for (auto i = 0; i < count; ++i) m_impl.emplace_back(new event_loop::fixed_size_buffer);
    }
    void event_loop_impl::do_remove_connection(int32_t fd) {
        auto it = m_connections.find(fd);
        if (it != m_connections.end()) {
            auto& conn = (connection&)*it;
            ::close(fd);
            if (conn.buffer) m_pl.redeem(conn.buffer);
            m_connections.erase(fd);
        }
    }
    void event_loop_impl::run(const config& cfg, callbacks&& cb) {
        THREAD_SCOPE(EVENT_LOOP_THREAD);
        if (cfg.custom_acceptor != nullptr) {
            m_acceptor = cfg.custom_acceptor;
            m_owns_acceptor = false;
        } else {
            m_acceptor = new tcp_acceptor;
            m_acceptor->open(cfg.port);
            m_owns_acceptor = true;
        }
        stream_options opt;
        opt.non_block_mode = true;
        m_acceptor->set_options(opt);
        m_cfg = cfg;
        m_kq = kqueue();
        if (m_kq == -1) { connection dumb; cb.on_err(dumb, "kqueue() failed"); return; }
        struct kevent ev;
        EV_SET(&ev, (uint64_t)m_acceptor->raw(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
        if (kevent(m_kq, &ev, 1, nullptr, 0, nullptr) == -1) {
            connection dumb; cb.on_err(dumb, "kevent: cannot register listen socket"); return;
        }
        m_events.resize(cfg.max_mutual_connections);
        while (m_running.load(std::memory_order_acquire)) {
            NAMED_SCOPE(Tick);
            struct timespec timeout; timeout.tv_sec = 1; timeout.tv_nsec = 0;
            int nfds;
            { NAMED_SCOPE(Kevent); nfds = kevent(m_kq, nullptr, 0, m_events.data(), (int)m_events.size(), &timeout); }
            if (nfds < 0) { connection dumb; cb.on_err(dumb, "kevent() failed"); m_running = false; }
            else {
                for (int i = 0; i < nfds; ++i) {
                    auto& event = m_events[i];
                    auto fd = (int)event.ident;
                    if (event.flags & EV_EOF) {
                        auto it = m_connections.find(fd);
                        if (it != m_connections.end()) {
                            auto& conn = (connection&)*it;
                            ::close(fd);
                            if (conn.buffer) m_pl.redeem(conn.buffer);
                            m_connections.erase(fd);
                        }
                    } else if (fd == m_acceptor->raw()) { handle_accept(cb); }
                    else if (event.filter == EVFILT_READ) {
                        auto it = m_connections.find(fd);
                        if (it != m_connections.end()) {
                            handle_read((connection&)*it, cb);
                            if (it->get_state() == connection_state::die) do_remove_connection(fd);
                        }
                    } else if (event.filter == EVFILT_WRITE) {
                        auto it = m_connections.find(fd);
                        if (it != m_connections.end()) {
                            handle_write((connection&)*it, cb);
                            if (it->get_state() == connection_state::die) do_remove_connection(fd);
                        }
                    }
                }
            }
        }
    }
    void event_loop_impl::stop() {
        m_running = false;
        if (m_kq != -1) { close(m_kq); m_kq = -1; }
        if (m_owns_acceptor && m_acceptor != nullptr) { delete m_acceptor; m_acceptor = nullptr; }
    }
    void event_loop_impl::handle_accept(callbacks& cb) {
        NAMED_SCOPE(HandleAccept);
        for (auto i = 0; i < m_cfg.max_accepts_per_tick; ++i) {
            NAMED_SCOPE(AcceptOne);
            struct sockaddr_in client_addr = {};
            socklen_t socklen = sizeof(client_addr);
            int sock = accept(m_acceptor->raw(), (struct sockaddr *)&client_addr, &socklen);
            int flags = fcntl(sock, F_GETFL, 0);
            if (flags == -1) break;
            else {
                flags = flags | O_NONBLOCK;
                if (fcntl(sock, F_SETFL, flags) == -1) {
                    connection dumb{ -1 };
                    cb.on_err(dumb, "Cannot set flags for connection, skip this one");
                    ::close(sock); sock = -1;
                }
            }
            if (sock != -1) {
                apply_stream_options(sock, m_cfg.accepted_stream_options);
                struct kevent ev;
                EV_SET(&ev, sock, EVFILT_READ, EV_ADD, 0, 0, nullptr);
                if (kevent(m_kq, &ev, 1, nullptr, 0, nullptr) == -1) {
                    connection dumb{ -1 }; cb.on_err(dumb, "kevent: cannot register new connection");
                    ::close(sock); continue;
                }
                auto&& conn = (connection&)*m_connections.emplace(sock).first;
                conn.buffer = m_pl.allocate();
                cb.on_connect(conn);
            }
        }
    }
    void event_loop_impl::handle_read(connection& conn, callbacks& cb) {
        NAMED_SCOPE(HandleRead);
        assert(conn.get_state() == connection_state::read);
        bool error = false;
        std::size_t total_received = conn.buffer->consume_free([&](void* data, std::size_t size) -> std::size_t {
            auto received = ::recv(conn.descriptor, (char*)data, size, 0);
            if (received <= 0 && errno != EAGAIN) {
                cb.on_err(conn, "Cannot read from socket, close connection");
                conn.set_state(connection_state::die); error = true;
                return size;
            } else if (received <= 0) { return 0; }
            total_received += received;
            return (std::size_t)received;
        });
        if (total_received > 0 && !error) cb.on_read(conn);
    }
    void event_loop_impl::handle_write(connection& conn, callbacks& cb) {
        NAMED_SCOPE(HandleWrite);
        assert(conn.get_state() == connection_state::write);
        conn.buffer->consume_used([&](const void* data, std::size_t size) -> std::size_t {
            auto op_res = send(conn.descriptor, (char*)data, size, 0);
            if (op_res <= 0 && errno != EAGAIN) {
                cb.on_err(conn, "Cannot write to socket, close connection");
                conn.set_state(connection_state::die); return size;
            } else if (op_res <= 0) { return 0; }
            return (std::size_t)op_res;
        });
        if (conn.buffer->is_empty()) cb.on_write(conn);
    }


}
#endif
