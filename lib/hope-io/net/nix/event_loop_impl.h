/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/coredefs.h"
#include "hope-io/net/event_loop.h"
#include "hope-io/net/stream_options_util.h"
#include "hope-io/net/nix/tcp_acceptor.h"

#if PLATFORM_APPLE

#include <vector>
#include <unordered_set>
#include <atomic>
#include <sys/event.h>
#include <sys/time.h>

#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

namespace hope::io::el {

    template<typename TOnRead, typename TOnWrite, typename TOnError, typename TConnected>
    class event_loop_impl_t final
        : public event_loop<TOnRead, TOnWrite, TOnError, TConnected> {
    public:
        using base = event_loop<TOnRead, TOnWrite, TOnError, TConnected>;

        event_loop_impl_t(TConnected&& on_connect, TOnRead&& on_read, TOnWrite&& on_write, TOnError&& on_error)
            : m_on_connect(std::move(on_connect))
            , m_on_read(std::move(on_read))
            , m_on_write(std::move(on_write))
            , m_on_err(std::move(on_error)) {}

        ~event_loop_impl_t() override {
            if (m_kq != -1) ::close(m_kq);
            if (m_owns_acceptor && m_acceptor != nullptr) { delete m_acceptor; m_acceptor = nullptr; }
            m_pl.drain();
        }

        void run(const config& cfg) override {
            THREAD_SCOPE(EVENT_LOOP_THREAD);

            if (cfg.custom_acceptor != nullptr) {
                m_acceptor = cfg.custom_acceptor;
                m_owns_acceptor = false;
            } else {
                m_acceptor = new hope::io::tcp_acceptor;
                m_acceptor->open(cfg.port);
                m_owns_acceptor = true;
            }

            stream_options opt;
            opt.non_block_mode = true;
            m_acceptor->set_options(opt);

            m_cfg = cfg;
            m_kq = kqueue();
            if (m_kq == -1) {
                connection dumb;
                m_on_err(dumb, "kqueue() failed");
                return;
            }

            struct kevent ev;
            EV_SET(&ev, (uint64_t)m_acceptor->raw(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
            if (kevent(m_kq, &ev, 1, nullptr, 0, nullptr) == -1) {
                connection dumb;
                m_on_err(dumb, "kevent: cannot register listen socket");
                return;
            }

            m_pl.prepool(cfg.max_mutual_connections);
            m_events.resize(cfg.max_mutual_connections);

            while (m_running.load(std::memory_order_acquire)) {
                NAMED_SCOPE(Tick);
                struct timespec timeout;
                timeout.tv_sec = 1;
                timeout.tv_nsec = 0;

                int nfds;
                {
                    NAMED_SCOPE(Kevent);
                    nfds = kevent(m_kq, nullptr, 0, m_events.data(), (int)m_events.size(), &timeout);
                }

                if (nfds < 0) {
                    connection dumb;
                    m_on_err(dumb, "kevent() failed");
                    m_running = false;
                } else {
                    for (int i = 0; i < nfds; ++i) {
                        auto& event = m_events[i];
                        auto fd = (int)event.ident;

                        if (event.flags & EV_EOF) {
                            auto it = m_connections.find(fd);
                            if (it != m_connections.end()) {
                                auto& conn = const_cast<connection&>(*it);
                                ::close(fd);
                                if (conn.buffer) m_pl.redeem(conn.buffer);
                                m_connections.erase(fd);
                            }
                        } else if (fd == m_acceptor->raw()) {
                            handle_accept();
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
        }

        void stop() override {
            m_running = false;
            if (m_kq != -1) { ::close(m_kq); m_kq = -1; }
            if (m_owns_acceptor && m_acceptor != nullptr) { delete m_acceptor; m_acceptor = nullptr; }
        }

    private:


        void apply_state(connection& conn, el_connection_state state) {
            if (state == el_connection_state::die) {
                do_remove_connection(conn.descriptor);
                return;
            }
            conn.set_state(state);

            struct kevent ev;
            if (state == el_connection_state::write) {
                EV_SET(&ev, conn.descriptor, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
                kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
            } else if (state == el_connection_state::read) {
                EV_SET(&ev, conn.descriptor, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
                kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
            }
        }

        void do_remove_connection(int32_t fd) {
            auto it = m_connections.find(fd);
            if (it != m_connections.end()) {
                auto& conn = const_cast<connection&>(*it);
                ::close(fd);
                if (conn.buffer) m_pl.redeem(conn.buffer);
                m_connections.erase(fd);
            }
        }

        void handle_accept() {
            NAMED_SCOPE(HandleAccept);
            for (auto i = 0; i < m_cfg.max_accepts_per_tick; ++i) {
                NAMED_SCOPE(AcceptOne);
                struct sockaddr_in client_addr = {};
                socklen_t socklen = sizeof(client_addr);
                int sock = accept(m_acceptor->raw(), (struct sockaddr *)&client_addr, &socklen);
                if (sock == -1) break;

                int flags = fcntl(sock, F_GETFL, 0);
                if (flags == -1) {
                    ::close(sock);
                    break;
                }
                flags |= O_NONBLOCK;
                if (fcntl(sock, F_SETFL, flags) == -1) {
                    connection dumb{ -1 };
                    m_on_err(dumb, "Cannot set flags for connection, skip this one");
                    ::close(sock);
                    continue;
                }

                apply_stream_options(sock, m_cfg.accepted_stream_options);

                struct kevent ev;
                EV_SET(&ev, sock, EVFILT_READ, EV_ADD, 0, 0, nullptr);
                if (kevent(m_kq, &ev, 1, nullptr, 0, nullptr) == -1) {
                    connection dumb{ -1 };
                    m_on_err(dumb, "kevent: cannot register new connection");
                    ::close(sock);
                    continue;
                }

                auto& conn = const_cast<connection&>(*m_connections.emplace(sock).first);
                conn.buffer = m_pl.allocate();
                auto state = m_on_connect(conn);
                if (state == el_connection_state::die) {
                    do_remove_connection(sock);
                    continue;
                }
                conn.set_state(state);
                if (state == el_connection_state::write) {
                    EV_SET(&ev, sock, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
                    kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
                }
            }
        }

        void handle_read(connection& conn) {
            NAMED_SCOPE(HandleRead);
            assert(conn.get_state() == el_connection_state::read);
            bool error = false;
            conn.buffer->consume_free([&](void* data, std::size_t size) -> std::size_t {
                auto received = ::recv(conn.descriptor, (char*)data, size, 0);
                if (received <= 0 && errno != EAGAIN) {
                    auto err_state = m_on_err(conn, "Cannot read from socket, close connection");
                    error = true;
                    apply_state(conn, err_state);
                    return size;
                } else if (received <= 0) {
                    return 0;
                }
                return (std::size_t)received;
            });
            if (!error && !conn.buffer->is_empty()) {
                auto state = m_on_read(conn);
                if (state != el_connection_state::idle) {
                    apply_state(conn, state);
                }
            }
        }

        void handle_write(connection& conn) {
            NAMED_SCOPE(HandleWrite);
            assert(conn.get_state() == el_connection_state::write);
            conn.buffer->consume_used([&](const void* data, std::size_t size) -> std::size_t {
                auto op_res = send(conn.descriptor, (char*)data, size, 0);
                if (op_res <= 0 && errno != EAGAIN) {
                    auto err_state = m_on_err(conn, "Cannot write to socket, close connection");
                    apply_state(conn, err_state);
                    return size;
                } else if (op_res <= 0) {
                    return 0;
                }
                return (std::size_t)op_res;
            });
            if (conn.buffer->is_empty()) {
                auto state = m_on_write(conn);
                if (state != el_connection_state::idle) {
                    apply_state(conn, state);
                }
            }
        }

        std::unordered_set<connection, typename connection::hash> m_connections;
        int m_kq = -1;
        std::vector<struct kevent> m_events;

        config m_cfg;
        buffer_pool m_pl;
        hope::io::acceptor* m_acceptor = nullptr;
        bool m_owns_acceptor = false;
        std::atomic<bool> m_running = true;
        TOnError m_on_err;
        TOnWrite m_on_write;
        TOnRead m_on_read;
        TConnected m_on_connect;
    };

}

#endif
