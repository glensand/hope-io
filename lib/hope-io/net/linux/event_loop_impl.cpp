/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#if PLATFORM_LINUX
#include "hope-io/net/linux/event_loop_impl.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/coredefs.h"

#include <deque>
#include <vector>
#include <atomic>

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

    std::function<void(const event_loop::connection& conn)> event_loop::connection::on_state_changed;

    event_loop_impl::event_loop_impl() {
        event_loop::connection::on_state_changed = [this](const connection& conn) {
            NAMED_SCOPE(SetStatte);
            epoll_event ev;
            ev.events = EPOLLRDHUP | EPOLLHUP | EPOLLET;
            ev.data.fd = conn.descriptor;
            if (conn.get_state() == connection_state::die) {
                remove_connection(conn.descriptor);
            } else if (conn.get_state() == connection_state::read) {
                ev.events |= EPOLLIN;
                epoll_ctl(m_epfd, EPOLL_CTL_MOD, conn.descriptor, &ev);
            }
            else if (conn.get_state() == connection_state::write) {
                ev.events |= EPOLLOUT;
                epoll_ctl(m_epfd, EPOLL_CTL_MOD, conn.descriptor, &ev);
            }
        };
    }

    event_loop_impl::~event_loop_impl() {
        event_loop::connection::on_state_changed = nullptr;
        m_pl.drain();
    }

    event_loop::fixed_size_buffer* event_loop_impl::buffer_pool::allocate() {
        event_loop::fixed_size_buffer* allocated = nullptr;
        if (!m_impl.empty()) {
            allocated = m_impl.back();
            m_impl.pop_back();
        } else {
            allocated = new event_loop::fixed_size_buffer;
        }
        return allocated;
    }

    void event_loop_impl::buffer_pool::redeem(event_loop::fixed_size_buffer* b) {
        b->reset();
        m_impl.emplace_back(b);
    }

    void event_loop_impl::buffer_pool::prepool(std::size_t count) {
        for (auto i = 0; i < count; ++i)
            m_impl.emplace_back(new event_loop::fixed_size_buffer);
    }

    void event_loop_impl::run(const config& cfg, callbacks&& cb) {
        THREAD_SCOPE(EVENT_LOOP_THREAD);
        m_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_listen_socket == -1) {
            throw std::runtime_error(std::string("Cannot create socker") + strerror(errno));
        }
        sockaddr_in srv_addr;
        bzero((char *)&srv_addr, sizeof(struct sockaddr_in));
        srv_addr.sin_family = AF_INET;
        srv_addr.sin_addr.s_addr = INADDR_ANY;
        srv_addr.sin_port = htons(cfg.port);
        if (-1 == bind(m_listen_socket, (struct sockaddr *)&srv_addr, sizeof(srv_addr))) {
            throw_bind_err();
        }
        auto flags = fcntl(m_listen_socket, F_GETFL, 0);
        fcntl(m_listen_socket, F_SETFL, flags | O_NONBLOCK);
        listen(m_listen_socket, cfg.max_mutual_connections);
    
        m_epfd = epoll_create(1);
        epoll_ctl_add(m_epfd, m_listen_socket, EPOLLIN | EPOLLOUT | EPOLLET, cb);

        m_pl.prepool(cfg.max_mutual_connections);
        m_events.resize(cfg.max_mutual_connections);
        m_connections.resize(cfg.max_mutual_connections);
        while (m_running.load(std::memory_order_acquire)) {
            NAMED_SCOPE(Tick);
            auto nfds = 0;
            {
                NAMED_SCOPE(Epoll);
                nfds = epoll_wait(m_epfd, m_events.data(), m_events.size(), cfg.epoll_temeout);
            }
            for (auto i = 0; i < nfds; i++) {
                NAMED_SCOPE(ProcessOneEvent);
                auto&& event = m_events[i];
                auto sock = event.data.fd;
                if (sock == m_listen_socket) {
                    handle_accept(cb);
                } else if (event.events & POLLIN) {
                    handle_read(m_connections[sock], cb);
                } else if (event.events & POLLOUT) {
                    handle_write(m_connections[sock], cb);
                }
                else if (event.events & (EPOLLRDHUP | EPOLLHUP | POLLERR)) {
                    remove_connection(sock);
                }
            }
        }
        close(m_listen_socket);
    }

    void event_loop_impl::stop() {
        m_running = false;
    }

    void event_loop_impl::handle_accept(callbacks& cb) {
        NAMED_SCOPE(HandleAccept);
        for (auto i = 0; i < m_cfg.max_accepts_per_tick; ++i) {
            NAMED_SCOPE(AcceptOne);
            struct sockaddr_in client_addr = {};
            socklen_t socklen = sizeof(client_addr);
            int sock = accept(m_listen_socket, (struct sockaddr *)&client_addr, &socklen);
            int flags = fcntl(sock, F_GETFL, 0);
            if (flags == -1) {
                break;
            } else {
                flags = flags | O_NONBLOCK;
                if (fcntl(sock, F_SETFL, flags) == -1) {
                    connection dumb;
                    cb.on_err(dumb, "Cannot set flags for connection, skip this one");
                    ::close(sock);
                    sock = -1;
                }
            }
            if (sock != -1) {
                push_new_connection(sock);
                epoll_ctl_add(m_epfd, sock, EPOLLRDHUP | EPOLLHUP | EPOLLET, cb);
                cb.on_connect(m_connections[sock]);
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
                conn.set_state(connection_state::die);
                error = true;
                return size;
            } else if (received <= 0) {
                return 0;
            }
            total_received += received;
            return (std::size_t)received;
        });
        if (total_received > 0 && !error) {
            cb.on_read(conn);
        }
    }

    void event_loop_impl::handle_write(connection& conn, callbacks& cb) {
        NAMED_SCOPE(HandleWrite);
        assert(conn.get_state() == connection_state::write);
        conn.buffer->consume_used([&](const void* data, std::size_t size) -> std::size_t {
            auto op_res = send(conn.descriptor, (char*)data, size, 0);
            if (op_res <= 0 && errno != EAGAIN) {
                cb.on_err(conn, "Cannot write to socket, close connection");
                conn.set_state(connection_state::die);
                return size;
            } else if (op_res <= 0) {
                return 0;
            }
            return (std::size_t)op_res;
        });
        if (conn.buffer->is_empty()) {
            cb.on_write(conn);
        }
    }

    void event_loop_impl::epoll_ctl_add(int32_t epfd, int32_t fd, uint32_t events, callbacks& cb) {
        epoll_event ev;
        ev.events = events;
        ev.data.fd = fd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
            connection dumb;
            cb.on_err(dumb, std::string("epoll_ctl ADD failed: ") + strerror(errno));
        }
    }

    void event_loop_impl::remove_connection(int32_t descriptor) {
        epoll_ctl(m_epfd, EPOLL_CTL_DEL, descriptor, NULL);
        close(descriptor);
        m_pl.redeem(m_connections[descriptor].buffer);
        m_connections[descriptor].buffer = nullptr;
    }

    void event_loop_impl::push_new_connection(int32_t fd) {
        NAMED_SCOPE(PushNewConnection);
        if ((std::size_t)fd >= m_connections.size()) {
            m_connections.resize(fd + 1);
        }
        m_connections[fd].descriptor = fd;
        m_connections[fd].buffer = m_pl.allocate();
    }

    void event_loop_impl::throw_bind_err() {
        switch (errno) {
            case EACCES: throw std::runtime_error("Permission denied (need root for ports < 1024)");
            case EADDRINUSE: throw std::runtime_error("Address already in use (another process bound to this port).");
            case EADDRNOTAVAIL: throw std::runtime_error("Invalid address (not assigned to this machine).");
            case EBADF: throw std::runtime_error("Invalid socket descriptor");
            case EINVAL: throw std::runtime_error("Socket already bound, or bad parameters.");
            case ENOTSOCK: throw std::runtime_error("File descriptor is not a socket.");
            default: std::runtime_error("Unknown bind error.");
        }
    }


}
#endif
