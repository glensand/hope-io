#include "hope-io/net/event_loop.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"

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

namespace hope::io { 

    event_loop* create_event_loop() {
        class event_loop_linux final : public event_loop {
        public:
            event_loop_linux() = default;
            ~event_loop_linux() override {

            }

        private:

            struct buffer_pool final {
                buffer* allocate() {
                    buffer* allocated = nullptr;
                    if (!m_impl.empty()) {
                        allocated = m_impl.back();
                        m_impl.pop_back();
                    } else {
                        allocated = new buffer;
                    }
                    return allocated;
                }

                void redeem(buffer* b) {
                    b->reset();
                    m_impl.emplace_back(b);
                }

            private:
                std::deque<buffer*> m_impl; // prepool?
            };
            
            virtual void run(std::size_t port, callbacks&& cb) override {
                // TODO:: multiport option?
                auto acceptor = create_acceptor();
                acceptor->open(port);
                stream_options opt;
                opt.non_block_mode = true;
                acceptor->set_options(opt);

                buffer_pool pl;
                std::unordered_set<connection, connection::hash> connections;
                std::vector<struct pollfd> poll_args;

                while (m_running.load(std::memory_order_acquire)) {
                    poll_args.clear();
                    poll_args.emplace_back(acceptor->raw(), POLLIN, 0);

                    // fill active connections
                    for (auto&& it = begin(connections); it != end(connections);) {
                        // TODO:: maybe better to use bit flags and allow read and write at the same socket at the same time?

                        // NOTE:: read flag here, to ensure everything gonna be ok if someone will change state while if-else
                        auto pending_state = it->get_state();
                        if (pending_state == connection_state::die) {
                            ::close(it->descriptor);
                            it = connections.erase(it);
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
                            poll_args.emplace_back(pfd);
                            ++it;
                        }
                    }
                    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
                    if (rv < 0 && errno != EINTR) {
                        // Ok here we have error
                        // since run() can be executed from worker thread, we will not thtow exception here
                        // instead we will just call cb to notify
                        connection dumb{ -1 };
                        cb.on_err(dumb, "");
                        m_running = false;
                    } else if (rv > 0){
                        if (poll_args[0].revents) {
                            struct sockaddr_in client_addr = {};
                            socklen_t socklen = sizeof(client_addr);
                            // TODO:: do it in loop?
                            int sock = accept(acceptor->raw(), (struct sockaddr *)&client_addr, &socklen);
                            int flags = fcntl(sock, F_GETFL, 0);
                            if (flags == -1) {
                                connection dumb{ -1 };
                                // TODO:: add adress to message
                                cb.on_err(dumb, "Cannot get flags for connection, skip this one");
                                sock = -1;
                            } else {
                                flags = flags | O_NONBLOCK;
                                if (fcntl(sock, F_SETFL, flags) == -1) {
                                    connection dumb{ -1 };
                                    // TODO:: add adress to message
                                    cb.on_err(dumb, "Cannot set flags for connection, skip this one");
                                    sock = -1;
                                }
                            }
                            if (sock != -1) {
                                auto&& conn = (connection&)*connections.emplace(sock).first;
                                conn.buffer = pl.allocate();
                                cb.on_connect(conn);
                            }
                        }

                        for (std::size_t i = 1; i < poll_args.size(); ++i) {
                            uint32_t ready = poll_args[i].revents;
                            if (ready != 0) {
                                auto&& conn = (connection&)(*connections.find(poll_args[i].fd));
                                if (ready & POLLIN) {
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

                                            // if the application does not handle buffer, or we received less data
                                            // then we can obtain, will try at the next time
                                            if (received < span.second || conn.get_state() != connection_state::read) {
                                                read = false;
                                            }
                                        }
                                    }
                                }
                                if (ready & POLLOUT) {
                                    assert(conn.get_state() == connection_state::write);
                                    const auto span = conn.buffer->used_chunk();
                                    auto op_res = send(conn.descriptor, (char*)span.first, span.second, 0);
                                    if (op_res <= 0 && errno != EAGAIN) {
                                        cb.on_err(conn, "Cannot write to socket, close connection");
                                        conn.set_state(connection_state::die);
                                    } else if (op_res > 0){
                                        conn.buffer->handle_read(op_res);
                                        conn.buffer->shrink();
                                        std::cout << "written:" << op_res << "bytes" << std::endl;
                                        assert((conn.buffer->count() == 0) == conn.buffer->is_empty());
                                        if (conn.buffer->is_empty()) {
                                            cb.on_write(conn);
                                        }
                                    }
                                }

                                // close the socket from socket error or application logic
                                if ((ready & POLLERR) || conn.get_state() == connection_state::die) {
                                    if (ready & POLLERR) {
                                        cb.on_err(conn, "an error occuried on specified socket, closing the connection");
                                    }
                                    (void)close(conn.descriptor);
                                    if (conn.buffer) {
                                        pl.redeem(conn.buffer);
                                    }
                                    connections.erase(poll_args[i].fd);
                                }
                            }
                        }
                    }

                }
            }

            virtual void stop() override {
                m_running = false;
            }

            std::atomic<bool> m_running = true;
        };
        return new event_loop_linux;
    } 

}