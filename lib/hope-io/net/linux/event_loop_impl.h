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

#if PLATFORM_LINUX

#include <vector>
#include <atomic>
#include <sys/epoll.h>

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
            if (m_epfd != -1) ::close(m_epfd);
            if (m_listen_socket != -1) ::close(m_listen_socket);
            m_pl.drain();
        }

        void run(const config& cfg) override {
            THREAD_SCOPE(EVENT_LOOP_THREAD);

            m_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (m_listen_socket == -1) {
                throw_bind_err();
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
            epoll_ctl_add(m_epfd, m_listen_socket, EPOLLIN | EPOLLOUT | EPOLLET);

            m_pl.prepool(cfg.max_mutual_connections);
            m_events.resize(cfg.max_mutual_connections);
            m_connections.resize(cfg.max_mutual_connections);
            m_cfg = cfg;

            while (m_running.load(std::memory_order_acquire)) {
                NAMED_SCOPE(Tick);
                auto nfds = 0;
                {
                    NAMED_SCOPE(Epoll);
                    nfds = epoll_wait(m_epfd, m_events.data(), (int)m_events.size(), cfg.epoll_temeout);
                }
                for (auto i = 0; i < nfds; i++) {
                    NAMED_SCOPE(ProcessOneEvent);
                    auto&& event = m_events[i];
                    auto sock = event.data.fd;
                    if (sock == m_listen_socket) {
                        handle_accept();
                    } else if (event.events & EPOLLIN) {
                        handle_read(m_connections[sock]);
                    } else if (event.events & EPOLLOUT) {
                        handle_write(m_connections[sock]);
                    } else if (event.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                        remove_connection(sock);
                    }
                }
            }

            ::close(m_listen_socket);
            m_listen_socket = -1;
        }

        void stop() override {
            m_running = false;
        }

    private:
        using buffer_pool = hope::io::el::buffer_pool;

        void epoll_ctl_add(int32_t epfd, int32_t fd, uint32_t events) {
            epoll_event ev;
            ev.events = events;
            ev.data.fd = fd;
            if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
                connection dumb;
                m_on_err(dumb, std::string("epoll_ctl ADD failed: ") + strerror(errno));
            }
        }

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
            NAMED_SCOPE(HandleAccept);
            for (auto i = 0; i < m_cfg.max_accepts_per_tick; ++i) {
                NAMED_SCOPE(AcceptOne);
                struct sockaddr_in client_addr = {};
                socklen_t socklen = sizeof(client_addr);
                int sock = accept(m_listen_socket, (struct sockaddr *)&client_addr, &socklen);
                if (sock == -1) break;

                int flags = fcntl(sock, F_GETFL, 0);
                if (flags == -1) {
                    ::close(sock);
                    break;
                }
                flags |= O_NONBLOCK;
                if (fcntl(sock, F_SETFL, flags) == -1) {
                    connection dumb;
                    m_on_err(dumb, "Cannot set flags for connection, skip this one");
                    ::close(sock);
                    continue;
                }

                apply_stream_options(sock, m_cfg.accepted_stream_options);
                push_new_connection(sock);
                auto state = m_on_connect(m_connections[sock]);

                if (state == el_connection_state::die) {
                    remove_connection(sock);
                    continue;
                }
                m_connections[sock].set_state(state);

                uint32_t epoll_events = EPOLLRDHUP | EPOLLHUP | EPOLLET;
                if (state == el_connection_state::read) {
                    epoll_events |= EPOLLIN;
                } else if (state == el_connection_state::write) {
                    epoll_events |= EPOLLOUT;
                }
                epoll_ctl_add(m_epfd, sock, epoll_events);
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

        void remove_connection(int32_t descriptor) {
            epoll_ctl(m_epfd, EPOLL_CTL_DEL, descriptor, NULL);
            ::close(descriptor);
            m_pl.redeem(m_connections[descriptor].buffer);
            m_connections[descriptor].buffer = nullptr;
        }

        void push_new_connection(int32_t fd) {
            NAMED_SCOPE(PushNewConnection);
            if ((std::size_t)fd >= m_connections.size()) {
                m_connections.resize(fd + 1);
            }
            m_connections[fd].descriptor = fd;
            m_connections[fd].buffer = m_pl.allocate();
        }

        void throw_bind_err() {
            switch (errno) {
                case EACCES: HOPE_THROW("event_loop", "Permission denied (need root for ports < 1024)");
                case EADDRINUSE: HOPE_THROW("event_loop", "Address already in use (another process bound to this port).");
                case EADDRNOTAVAIL: HOPE_THROW("event_loop", "Invalid address (not assigned to this machine).");
                case EBADF: HOPE_THROW("event_loop", "Invalid socket descriptor");
                case EINVAL: HOPE_THROW("event_loop", "Socket already bound, or bad parameters.");
                case ENOTSOCK: HOPE_THROW("event_loop", "File descriptor is not a socket.");
                default: HOPE_THROW_ERRNO("event_loop", "bind failed");
            }
        }

        std::vector<epoll_event> m_events;
        std::vector<connection> m_connections;

        int32_t m_listen_socket = -1;
        int32_t m_epfd = -1;

        config m_cfg;
        buffer_pool m_pl;
        std::atomic<bool> m_running = true;
        TOnError m_on_err;
        TOnWrite m_on_write;
        TOnRead m_on_read;
        TConnected m_on_connect;
    };

}

#endif
