#include "hope-io/net/event_loop.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/coredefs.h"

#include <deque>
#include <unordered_set>
#include <atomic>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
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
                        epoll_ctl(m_epfd, EPOLL_CTL_MOD, conn.descriptor, &ev);
                    }
                    else if (conn.get_state() == connection_state::write) {
                        ev.events |= EPOLLOUT;
                        epoll_ctl(m_epfd, EPOLL_CTL_MOD, conn.descriptor, &ev);
                    }
                };
            }
            ~event_loop_impl() override = default;

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

                sockaddr_in srv_addr;
                bzero((char *)&srv_addr, sizeof(struct sockaddr_in));
                srv_addr.sin_family = AF_INET;
                srv_addr.sin_addr.s_addr = INADDR_ANY;
                srv_addr.sin_port = htons(cfg.port);
                bind(m_listen_socket, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
            
                auto flags = fcntl(m_listen_socket, F_GETFL, 0);
                fcntl(m_listen_socket, F_SETFL, flags | O_NONBLOCK);
                listen(m_listen_socket, cfg.max_mutual_connections);
            
                m_epfd = epoll_create(1);
                epoll_ctl_add(m_epfd, m_listen_socket, EPOLLIN | EPOLLOUT | EPOLLET);

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
                            sock = -1;
                        }
                    }
                    if (sock != -1) {
                        push_new_connection(sock);
                        epoll_ctl_add(m_epfd, sock, EPOLLRDHUP | EPOLLHUP | EPOLLET);
                        cb.on_connect(m_connections[sock]);
                    }
                }
            }

            void handle_read(connection& conn, callbacks& cb) {
                NAMED_SCOPE(HandleRead);
                // probably we do not need to assert state here, we just need to check for read
                // perform actual reed and check one more time?
                assert(conn.get_state() == connection_state::read);
                // out buffer is limited in size and might be smaller then OS buffer
                // try to read untill failure(yep, not a best way because of syscalls)
                // may be we need to introduce settings for buffer size
                bool read = true;
                while (read) {
                    const auto span = conn.buffer->free_chunk();
                    auto received = ::recv(conn.descriptor, (char*)span.first, span.second, 0);
                    if (received <= 0 && errno != EAGAIN) {
                        cb.on_err(conn, "Cannot read from socket, close connection");
                        conn.set_state(connection_state::die);
                        read = false;
                    } else if (received <= 0 && errno == EAGAIN) {
                        // not an error, nothing to do here
                        read = false;
                    } else {
                        conn.buffer->handle_write(received);
                        cb.on_read(conn);
                        conn.buffer->shrink();
                        assert(conn.buffer->free_space() != 0);
                        assert(received > 0);
                                            
                        // if the application does not handle buffer, or we received less data
                        // then we can obtain, will try at the next time
                        if ((size_t)received < span.second || conn.get_state() != connection_state::read) {
                            read = false;
                        }
                    }
                }
            }

            void handle_write(connection& conn, callbacks& cb) {
                NAMED_SCOPE(HandleWrite);
                assert(conn.get_state() == connection_state::write);
                const auto span = conn.buffer->used_chunk();
                auto op_res = send(conn.descriptor, (char*)span.first, span.second, 0);
                if (op_res <= 0 && errno != EAGAIN) {
                    cb.on_err(conn, "Cannot write to socket, close connection");
                    conn.set_state(connection_state::die);
                } else if (op_res > 0) {
                    conn.buffer->handle_read(op_res);
                    conn.buffer->shrink();
                    assert((conn.buffer->count() == 0) == conn.buffer->is_empty());
                    if (conn.buffer->is_empty()) {
                        cb.on_write(conn);
                    }
                }
            }

            void epoll_ctl_add(int32_t epfd, int32_t fd, uint32_t events) {
	            epoll_event ev;
	            ev.events = events;
	            ev.data.fd = fd;
	            if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		            // todo:: cb
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