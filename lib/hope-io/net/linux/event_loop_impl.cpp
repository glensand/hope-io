#if PLATFORM_LINUX
#include "hope-io/net/event_loop.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/coredefs.h"

#include <deque>
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

    event_loop* create_event_loop() {
        class event_loop_impl final : public event_loop {
        public:
            event_loop_impl() {
                event_loop::connection::on_state_changed = [this](const connection& conn) {
                    NAMED_SCOPE(SetStatte);
                    epoll_event ev;
                    ev.events = EPOLLRDHUP | EPOLLHUP | EPOLLET;
                    ev.data.fd = conn.descriptor;
                    if (conn.get_state() == connection_state::die) {
                        remove_connection(conn.descriptor);
                    } else if (conn.get_state() == connection_state::read) {
                        ev.events |= EPOLLIN;
                        // TODO:: use user space to control events?
                        epoll_ctl(m_epfd, EPOLL_CTL_MOD, conn.descriptor, &ev);
                    }
                    else if (conn.get_state() == connection_state::write) {
                        ev.events |= EPOLLOUT;
                        epoll_ctl(m_epfd, EPOLL_CTL_MOD, conn.descriptor, &ev);
                    }
                };
            }
            ~event_loop_impl() override {
                // Clear the static callback to avoid dangling pointer issues
                event_loop::connection::on_state_changed = nullptr;
            }

        private:
            struct buffer_pool final {
                fixed_size_buffer* allocate() {
                    fixed_size_buffer* allocated = nullptr;
                    if (!m_impl.empty()) {
                        allocated = m_impl.back();
                        m_impl.pop_back();
                    } else {
                        allocated = new fixed_size_buffer;
                    }
                    return allocated;
                }

                void redeem(fixed_size_buffer* b) {
                    b->reset();
                    m_impl.emplace_back(b);
                }

                void prepool(std::size_t count) {
                    for (auto i = 0; i < count; ++i)
                        m_impl.emplace_back(new fixed_size_buffer);
                }

            private:
                std::deque<fixed_size_buffer*> m_impl; // prepool?
            };

            virtual void run(const config& cfg, callbacks&& cb) override {
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

            virtual void stop() override {
                m_running = false;
            }

            void handle_accept(callbacks& cb) {
                NAMED_SCOPE(HandleAccept);
                for (auto i = 0; i < m_cfg.max_accepts_per_tick; ++i) {
                    NAMED_SCOPE(AcceptOne);
                    struct sockaddr_in client_addr = {};
                    socklen_t socklen = sizeof(client_addr);
                    int sock = accept(m_listen_socket, (struct sockaddr *)&client_addr, &socklen);
                    int flags = fcntl(sock, F_GETFL, 0);
                    if (flags == -1) {
                        // TODO:: do I need to handle any error here? Or just break if any
                        break;
                    } else {
                        flags = flags | O_NONBLOCK;
                        if (fcntl(sock, F_SETFL, flags) == -1) {
                            connection dumb;
                            // TODO:: add adress to message
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

            void handle_read(connection& conn, callbacks& cb) {
                NAMED_SCOPE(HandleRead);
                assert(conn.get_state() == connection_state::read);

                bool error = false;
                std::size_t total_received = conn.buffer->consume_free([&](void* data, std::size_t size) -> std::size_t {
                    auto received = ::recv(conn.descriptor, (char*)data, size, 0);
                    if (received <= 0 && errno != EAGAIN) {
                        cb.on_err(conn, "Cannot read from socket, close connection");
                        conn.set_state(connection_state::die);
                        error = true;
                        return size; // advance past all to stop
                    } else if (received <= 0) {
                        return 0; // EAGAIN, don't advance
                    }
                    total_received += received;
                    return (std::size_t)received;
                });

                // Fire callback once per tick with all accumulated data
                if (total_received > 0 && !error) {
                    cb.on_read(conn);
                }
            }

            void handle_write(connection& conn, callbacks& cb) {
                NAMED_SCOPE(HandleWrite);
                assert(conn.get_state() == connection_state::write);

                conn.buffer->consume_used([&](const void* data, std::size_t size) -> std::size_t {
                    auto op_res = send(conn.descriptor, (char*)data, size, 0);
                    if (op_res <= 0 && errno != EAGAIN) {
                        cb.on_err(conn, "Cannot write to socket, close connection");
                        conn.set_state(connection_state::die);
                        return size; // advance past all to stop
                    } else if (op_res <= 0) {
                        return 0; // EAGAIN
                    }
                    return (std::size_t)op_res;
                });

                if (conn.buffer->is_empty()) {
                    cb.on_write(conn);
                }
            }

            void epoll_ctl_add(int32_t epfd, int32_t fd, uint32_t events, callbacks& cb) {
                epoll_event ev;
                ev.events = events;
                ev.data.fd = fd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
                    connection dumb;
                    cb.on_err(dumb, std::string("epoll_ctl ADD failed: ") + strerror(errno));
                }
            }

            void remove_connection(int32_t descriptor) {
                epoll_ctl(m_epfd, EPOLL_CTL_DEL, descriptor, NULL);
                close(descriptor);
                m_pl.redeem(m_connections[descriptor].buffer);
                m_connections[descriptor].buffer = nullptr;
            }
            
            void push_new_connection(int32_t fd) {
                NAMED_SCOPE(PushNewConnection);
                assert(fd < m_connections.size());
                assert(m_connections[fd].buffer == nullptr);
                m_connections[fd].descriptor = fd;
                m_connections[fd].buffer = m_pl.allocate();
            }

            void throw_bind_err() {
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

            std::vector<epoll_event> m_events;

            // TODO:: sparse array?
            std::vector<connection> m_connections;

            int32_t m_listen_socket = -1;
            int32_t m_epfd = -1;

            config m_cfg;
            buffer_pool m_pl;
            std::atomic<bool> m_running = true;
        };

        return new event_loop_impl;
    } 

}
#endif