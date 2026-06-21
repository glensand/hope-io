/* Copyright (C) 2026 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/event_loop.h"
#include "hope-io/net/stream_options_util.h"
#include "hope-io/net/uring/uring_core.h"

#if PLATFORM_LINUX && HOPE_IO_URING_ENABLED

#include <vector>
#include <atomic>
#include <cstdint>

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
    class uring_tcp_event_loop final
        : public event_loop<TOnRead, TOnWrite, TOnError, TConnected> {
    public:
        using base = event_loop<TOnRead, TOnWrite, TOnError, TConnected>;
        using callbacks = typename base::callbacks;

        uring_tcp_event_loop(TConnected&& on_connect, TOnRead&& on_read, TOnWrite&& on_write, TOnError&& on_error) {
            m_callbacks.on_connect = std::move(on_connect);
            m_callbacks.on_read = std::move(on_read);
            m_callbacks.on_write = std::move(on_write);
            m_callbacks.on_err = std::move(on_error);
        }

        ~uring_tcp_event_loop() override {
            m_pl.drain();
        }

        void run(const config& cfg) override {
            THREAD_SCOPE(EVENT_LOOP_THREAD);

            // Create listen socket
            m_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (m_listen_fd == -1) {
                throw_bind_err();
            }

            int reuse = 1;
            setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

            sockaddr_in srv_addr{};
            srv_addr.sin_family = AF_INET;
            srv_addr.sin_addr.s_addr = INADDR_ANY;
            srv_addr.sin_port = htons(cfg.port);
            if (bind(m_listen_fd, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) == -1) {
                throw_bind_err();
            }

            auto flags = fcntl(m_listen_fd, F_GETFL, 0);
            fcntl(m_listen_fd, F_SETFL, flags | O_NONBLOCK);
            listen(m_listen_fd, cfg.max_mutual_connections);

            // Init io_uring
            m_ring.init();

            m_cfg = cfg;
            m_pl.prepool(cfg.max_mutual_connections);
            m_connections.resize(cfg.max_mutual_connections + 1);

            // Submit initial accept
            rearm_accept();
            m_ring.submit();

            while (m_running.load(std::memory_order_acquire)) {
                NAMED_SCOPE(Tick);

                struct io_uring_cqe* cqe = nullptr;
                int ret = m_ring.wait_cqe_timeout(&cqe, 100);
                if (ret == -ETIME) continue; // timeout, recheck m_running
                if (ret < 0) {
                    connection dumb;
                    m_callbacks.on_err(dumb, "uring_tcp: io_uring_wait_cqe failed");
                    break;
                }

                unsigned head, count = 0;
                io_uring_for_each_cqe(&m_ring.impl, head, cqe) {
                    NAMED_SCOPE(ProcessOne);
                    int res = cqe->res;
                    uint64_t ud = io_uring_cqe_get_data64(cqe);
                    count++;

                    // ACCEPT completion
                    if (ud == uring::tag_accept(m_listen_fd)) {
                        if (res >= 0) {
                            int client_fd = res;
                            push_new_connection(client_fd);
                            auto& conn = m_connections[client_fd].conn;
                            auto state = m_callbacks.on_connect(conn);
                            if (state == el_connection_state::die) {
                                remove_connection(client_fd);
                            } else {
                                conn.set_state(state);
                                if (state == el_connection_state::read) {
                                    submit_recv(client_fd);
                                } else if (state == el_connection_state::write) {
                                    submit_send(client_fd);
                                }
                            }
                        }
                        rearm_accept();
                        continue;
                    }

                    int fd = uring::fd_of(ud);
                    if (fd < 0 || (std::size_t)fd >= m_connections.size()) continue;

                    // Error or EOF
                    if (res <= 0) {
                        remove_connection(fd);
                        continue;
                    }

                    if (uring::is_recv(ud)) {
                        handle_recv_completion(fd, res);
                    } else if (uring::is_send(ud)) {
                        handle_send_completion(fd, res);
                    }
                }
                io_uring_cq_advance(&m_ring.impl, count);

                // Submit all pending SQEs (including those added by completion handlers)
                m_ring.submit();
            }

            // Cleanup
            for (auto& cs : m_connections) {
                if (cs.conn.buffer) {
                    m_pl.redeem(cs.conn.buffer);
                    cs.conn.buffer = nullptr;
                }
            }
            ::close(m_listen_fd);
        }

        void stop() override {
            m_running = false;
        }

    private:
        enum class active_op : uint8_t {
            none,
            recv,
            send,
        };

        struct conn_state {
            connection conn;
            active_op op = active_op::none;
        };

        // ── Accept ────────────────────────────────────────────────────────
        void rearm_accept() {
            auto* sqe = m_ring.get_sqe();
            HOPE_ASSERT(sqe != nullptr, "uring_tcp: out of SQEs in rearm_accept");
            io_uring_prep_accept(sqe, m_listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
            io_uring_sqe_set_data64(sqe, uring::tag_accept(m_listen_fd));
        }

        // ── Recv / Send submissions ──────────────────────────────────────
        void submit_recv(int32_t fd) {
            if ((std::size_t)fd >= m_connections.size()) return;
            auto& cs = m_connections[fd];
            if (!cs.conn.buffer) return;

            auto [data, size] = cs.conn.buffer->get_free_region();
            if (size == 0) return; // buffer full

            auto* sqe = m_ring.get_sqe();
            if (!sqe) return; // ring full, will be retried on next tick
            io_uring_prep_recv(sqe, fd, data, size, 0);
            io_uring_sqe_set_data64(sqe, uring::tag_recv(fd));
            cs.op = active_op::recv;
        }

        void submit_send(int32_t fd) {
            if ((std::size_t)fd >= m_connections.size()) return;
            auto& cs = m_connections[fd];
            if (!cs.conn.buffer) return;

            auto [data, size] = cs.conn.buffer->get_used_region();
            if (size == 0) return; // nothing to send

            auto* sqe = m_ring.get_sqe();
            if (!sqe) return;
            io_uring_prep_send(sqe, fd, data, size, 0);
            io_uring_sqe_set_data64(sqe, uring::tag_send(fd));
            cs.op = active_op::send;
        }

        // ── Completion handlers ──────────────────────────────────────────
        void handle_recv_completion(int32_t fd, int res) {
            if ((std::size_t)fd >= m_connections.size()) return;
            auto& cs = m_connections[fd];

            // Ignore stale completion (state changed since submission)
            if (cs.op != active_op::recv) return;
            cs.op = active_op::none;

            cs.conn.buffer->advance_tail((std::size_t)res);
            auto state = m_callbacks.on_read(cs.conn);
            if (state == el_connection_state::die) {
                remove_connection(fd);
            } else if (state == el_connection_state::write) {
                cs.conn.set_state(el_connection_state::write);
                submit_send(fd);
            } else if (state == el_connection_state::read) {
                cs.conn.set_state(el_connection_state::read);
                submit_recv(fd);
            }
        }

        void handle_send_completion(int32_t fd, int res) {
            if ((std::size_t)fd >= m_connections.size()) return;
            auto& cs = m_connections[fd];

            // Ignore stale completion (state changed since submission)
            if (cs.op != active_op::send) return;
            cs.op = active_op::none;

            cs.conn.buffer->advance_head((std::size_t)res);

            // If buffer is fully drained, report write complete
            if (cs.conn.buffer->is_empty()) {
                auto state = m_callbacks.on_write(cs.conn);
                if (state == el_connection_state::die) {
                    remove_connection(fd);
                } else if (state == el_connection_state::read) {
                    cs.conn.set_state(el_connection_state::read);
                    submit_recv(fd);
                } else if (state == el_connection_state::write) {
                    cs.conn.set_state(el_connection_state::write);
                    submit_send(fd);
                }
            } else {
                // Partial send — submit another SQE to send remaining data
                submit_send(fd);
            }
        }

        // ── Connection management ────────────────────────────────────────
        void push_new_connection(int32_t fd) {
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags != -1) {
                fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            }

            apply_stream_options(fd, m_cfg.accepted_stream_options);

            if ((std::size_t)fd >= m_connections.size()) {
                m_connections.resize(fd + 1);
            }

            auto& cs = m_connections[fd];
            cs.conn.descriptor = fd;
            cs.conn.buffer = m_pl.allocate();
            cs.op = active_op::none;
        }

        void remove_connection(int32_t fd) {
            if ((std::size_t)fd >= m_connections.size()) return;
            auto& cs = m_connections[fd];
            if (cs.conn.buffer) {
                m_pl.redeem(cs.conn.buffer);
                cs.conn.buffer = nullptr;
            }
            cs.op = active_op::none;
            ::close(fd);
        }

        void throw_bind_err() {
            switch (errno) {
                case EACCES: HOPE_THROW("uring_tcp", "Permission denied (need root for ports < 1024)");
                case EADDRINUSE: HOPE_THROW("uring_tcp", "Address already in use");
                case EADDRNOTAVAIL: HOPE_THROW("uring_tcp", "Invalid address");
                case EBADF: HOPE_THROW("uring_tcp", "Invalid socket descriptor");
                case EINVAL: HOPE_THROW("uring_tcp", "Socket already bound");
                case ENOTSOCK: HOPE_THROW("uring_tcp", "Not a socket");
                default: HOPE_THROW_ERRNO("uring_tcp", "bind failed");
            }
        }

        uring::ring m_ring;
        int32_t m_listen_fd = -1;

        config m_cfg;
        buffer_pool m_pl;
        std::vector<conn_state> m_connections;
        std::atomic<bool> m_running = true;
        callbacks m_callbacks;
    };

}

#endif
