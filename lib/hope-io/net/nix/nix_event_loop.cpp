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
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
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
                //NAMED_SCOPE(Process);
                // TODO:: multiport option?
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

                while (m_running.load(std::memory_order_acquire)) {
                    NAMED_SCOPE(Tick);
                    m_poll_args.clear();
                    pollfd descriptor{ (int)m_acceptor->raw(), POLLIN, 0 };
                    m_poll_args.emplace_back(descriptor);
                    {
                        NAMED_SCOPE(FillConnections);
                        // fill active connections
                        for (auto&& it = begin(m_connections); it != end(m_connections);) {
                            // TODO:: maybe better to use bit flags and allow read and write at the same socket at the same time?
                            // NOTE:: read flag here, to ensure everything gonna be ok if someone will change state while if-else
                            auto pending_state = it->get_state();
                            if (pending_state == connection_state::die) {
                                ::close(it->descriptor);
                                it = m_connections.erase(it);
                                // connections.erase
                            }
                            else {
                                struct pollfd pfd = { it->descriptor, POLLERR, 0 };
                                if (pending_state == connection_state::read) {
                                    pfd.events |= POLLIN;
                                } 
                                else if (pending_state == connection_state::write) {
                                    pfd.events |= POLLOUT;
                                }
                                else {
                                    assert(false && "invalid state, state should be read|write|die");
                                }
                                m_poll_args.emplace_back(pfd);
                                ++it;
                            }
                        }
                    }
                    
                    int rv;
                    {
                        NAMED_SCOPE(Poll);
                        rv = poll(m_poll_args.data(), (nfds_t)m_poll_args.size(), 1000);
                    }
                    if (rv < 0 && errno != EINTR) {
                        // Ok here we have error
                        // since run() can be executed from worker thread, we will not thtow exception here
                        // instead we will just call cb to notify
                        connection dumb;
                        cb.on_err(dumb, "");
                        m_running = false;
                    } else if (rv > 0){
                        // listen socket is in first arg
                        if (m_poll_args[0].revents) {
                           handle_accept(cb);
                        }

                        for (std::size_t i = 1; i < m_poll_args.size(); ++i) {
                            uint32_t ready = m_poll_args[i].revents;
                            if (ready != 0) {
                                auto descriptor = m_poll_args[i].fd;
                                auto&& conn = (connection&)(*m_connections.find(descriptor));
                                if (ready & POLLIN) {
                                    handle_read(conn, cb);
                                }
                                if (ready & POLLOUT) {
                                    handle_write(conn, cb);
                                }

                                // close the socket from socket error or application logic
                                if ((ready & POLLERR) || conn.get_state() == connection_state::die) {
                                    if (ready & POLLERR) {
                                        cb.on_err(conn, "an error occuried on specified socket, closing the connection");
                                    }
                                    (void)close(conn.descriptor);
                                    if (conn.buffer) {
                                        m_pl.redeem(conn.buffer);
                                    }
                                    m_connections.erase(m_poll_args[i].fd);
                                }
                            }
                        }
                    }

                }
            }

            virtual void stop() override {
                m_running = false;
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
                        // TODO:: do I need to handle any error here? Or just break if any
                        break;
                    } else {
                        flags = flags | O_NONBLOCK;
                        if (fcntl(sock, F_SETFL, flags) == -1) {
                            connection dumb{ -1 };
                            // TODO:: add adress to message
                            cb.on_err(dumb, "Cannot set flags for connection, skip this one");
                            ::close(sock);
                            sock = -1;
                        }
                    }
                    if (sock != -1) {
                        auto&& conn = (connection&)*m_connections.emplace(sock).first;
                        conn.buffer = m_pl.allocate();
                        cb.on_connect(conn);
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

            std::unordered_set<connection, connection::hash> m_connections;
            std::vector<struct pollfd> m_poll_args;

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