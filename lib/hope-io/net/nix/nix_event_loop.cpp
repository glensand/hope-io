/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#if PLATFORM_APPLE
#include "hope-io/net/event_loop.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/coredefs.h"

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

namespace hope::io {

    std::function<void(const event_loop::connection& conn)> event_loop::connection::on_state_changed;

    event_loop* create_event_loop() {
        class event_loop_impl final : public event_loop {
        public:
            event_loop_impl() = default;
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
                if (cfg.custom_acceptor != nullptr) {
                    m_acceptor = cfg.custom_acceptor;
                    m_owns_acceptor = false;
                } else {
                    m_acceptor = create_acceptor();
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
                    cb.on_err(dumb, "kqueue() failed");
                    return;
                }

                // Register listen socket
                struct kevent ev;
                EV_SET(&ev, (uint64_t)m_acceptor->raw(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
                if (kevent(m_kq, &ev, 1, nullptr, 0, nullptr) == -1) {
                    connection dumb;
                    cb.on_err(dumb, "kevent: cannot register listen socket");
                    return;
                }

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
                        cb.on_err(dumb, "kevent() failed");
                        m_running = false;
                    } else {
                        for (int i = 0; i < nfds; ++i) {
                            auto& event = m_events[i];
                            auto fd = (int)event.ident;
                            if (event.flags & EV_EOF) {
                                // Connection closed
                                auto it = m_connections.find(fd);
                                if (it != m_connections.end()) {
                                    auto& conn = (connection&)*it;
                                    ::close(fd);
                                    if (conn.buffer) {
                                        m_pl.redeem(conn.buffer);
                                    }
                                    m_connections.erase(fd);
                                }
                            } else if (fd == m_acceptor->raw()) {
                                handle_accept(cb);
                            } else if (event.filter == EVFILT_READ) {
                                auto it = m_connections.find(fd);
                                if (it != m_connections.end()) {
                                    handle_read((connection&)*it, cb);
                                }
                            } else if (event.filter == EVFILT_WRITE) {
                                auto it = m_connections.find(fd);
                                if (it != m_connections.end()) {
                                    handle_write((connection&)*it, cb);
                                }
                            }
                        }
                    }
                }
            }

            virtual void stop() override {
                m_running = false;
                if (m_kq != -1) {
                    close(m_kq);
                    m_kq = -1;
                }
                if (m_owns_acceptor && m_acceptor != nullptr) {
                    delete m_acceptor;
                    m_acceptor = nullptr;
                }
            }

            void handle_accept(callbacks& cb) {
                NAMED_SCOPE(HandleAccept);
                for (auto i = 0; i < m_cfg.max_accepts_per_tick; ++i) {
                    NAMED_SCOPE(AcceptOne);
                    struct sockaddr_in client_addr = {};
                    socklen_t socklen = sizeof(client_addr);
                    int sock = accept(m_acceptor->raw(), (struct sockaddr *)&client_addr, &socklen);
                    int flags = fcntl(sock, F_GETFL, 0);
                    if (flags == -1) {
                        break;
                    } else {
                        flags = flags | O_NONBLOCK;
                        if (fcntl(sock, F_SETFL, flags) == -1) {
                            connection dumb{ -1 };
                            cb.on_err(dumb, "Cannot set flags for connection, skip this one");
                            ::close(sock);
                            sock = -1;
                        }
                    }
                    if (sock != -1) {
                        // Register with kqueue
                        struct kevent ev;
                        EV_SET(&ev, sock, EVFILT_READ, EV_ADD, 0, 0, nullptr);
                        if (kevent(m_kq, &ev, 1, nullptr, 0, nullptr) == -1) {
                            connection dumb{ -1 };
                            cb.on_err(dumb, "kevent: cannot register new connection");
                            ::close(sock);
                            continue;
                        }
                        auto&& conn = (connection&)*m_connections.emplace(sock).first;
                        conn.buffer = m_pl.allocate();
                        cb.on_connect(conn);
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

            std::unordered_set<connection, connection::hash> m_connections;
            int m_kq = -1;
            std::vector<struct kevent> m_events;

            config m_cfg;
            buffer_pool m_pl;
            hope::io::acceptor* m_acceptor = nullptr;
            bool m_owns_acceptor = false;
            std::atomic<bool> m_running = true;
        };
        return new event_loop_impl;
    }
}
#endif
