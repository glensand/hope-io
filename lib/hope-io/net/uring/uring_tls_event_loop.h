/* Copyright (C) 2026 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/tls_event_loop.h"
#include "hope-io/net/event_loop.h"
#include "hope-io/net/stream_options_util.h"
#include "hope-io/net/uring/uring_core.h"

#if PLATFORM_LINUX && HOPE_IO_URING_ENABLED

#include <vector>
#include <unordered_set>
#include <atomic>
#include <cstdint>

#include <fcntl.h>
#include <poll.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <cstring>
#include <cerrno>

#include "openssl/ssl.h"
#include "openssl/err.h"

namespace hope::io::el {

    template<typename TOnRead, typename TOnWrite, typename TOnError, typename TConnected>
    class uring_tls_event_loop final
        : public tls_event_loop<TOnRead, TOnWrite, TOnError, TConnected> {
    public:
        using base = tls_event_loop<TOnRead, TOnWrite, TOnError, TConnected>;
        uring_tls_event_loop(TConnected&& on_connect, TOnRead&& on_read, TOnWrite&& on_write, TOnError&& on_error)
            : m_on_connect(std::move(on_connect))
            , m_on_read(std::move(on_read))
            , m_on_write(std::move(on_write))
            , m_on_err(std::move(on_error)) {}

        ~uring_tls_event_loop() override {
            if (m_ctx) {
                SSL_CTX_free(m_ctx);
            }
            for (auto& cs : m_connections) {
                if (cs.tls.ssl) {
                    SSL_free(cs.tls.ssl);
                    cs.tls.ssl = nullptr;
                }
            }
            m_pl.drain();
            m_pending_handshakes.clear();
        }

        void run(const tls_config& cfg) override {
            THREAD_SCOPE(TLS_EVENT_LOOP_THREAD);

            init_tls();
            auto* method = TLS_server_method();
            m_ctx = SSL_CTX_new(method);
            if (!m_ctx) {
                HOPE_THROW("uring_tls", "SSL_CTX_new failed");
            }

            if (SSL_CTX_use_certificate_file(m_ctx, cfg.cert_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
                SSL_CTX_free(m_ctx);
                m_ctx = nullptr;
                HOPE_THROW("uring_tls", "cannot load certificate: " + cfg.cert_path);
            }

            if (SSL_CTX_use_PrivateKey_file(m_ctx, cfg.key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
                SSL_CTX_free(m_ctx);
                m_ctx = nullptr;
                HOPE_THROW("uring_tls", "cannot load key: " + cfg.key_path);
            }

            SSL_CTX_set_session_cache_mode(m_ctx, SSL_SESS_CACHE_SERVER);
            SSL_CTX_sess_set_cache_size(m_ctx, 128);

            // Create listen socket
            m_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (m_listen_fd == -1) {
                SSL_CTX_free(m_ctx);
                m_ctx = nullptr;
                HOPE_THROW_ERRNO("uring_tls", "cannot create socket");
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
            if (flags != -1) {
                fcntl(m_listen_fd, F_SETFL, flags | O_NONBLOCK);
            }
            listen(m_listen_fd, cfg.max_mutual_connections);

            // Create epoll fd for accept monitoring
            int epfd = epoll_create1(0);
            if (epfd < 0) { throw std::runtime_error("epoll_create1 failed"); }
            {
                epoll_event ev;
                ev.events = EPOLLIN;
                ev.data.fd = m_listen_fd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, m_listen_fd, &ev);
            }

            // Init io_uring
            m_ring.init();
            m_pl.prepool(cfg.max_mutual_connections);
            m_connections.resize(cfg.max_mutual_connections + 1);
            m_cfg = cfg;

            while (m_running.load(std::memory_order_acquire)) {
                NAMED_SCOPE(TlsTick);

                // Non-blocking check for new connections via epoll
                {
                    epoll_event ev;
                    while (epoll_wait(epfd, &ev, 1, 0) > 0) {
                        if (ev.data.fd == m_listen_fd) {
                            handle_accept();
                        }
                    }
                }

                struct io_uring_cqe* cqe = nullptr;
                int ret = m_ring.wait_cqe_timeout(&cqe, 10);
                if (ret == -ETIME) continue;
                if (ret < 0) {
                    connection dumb;
                    m_on_err(dumb, "uring_tls: io_uring_wait_cqe failed");
                    break;
                }
                if (ret == 0) continue;

                unsigned head, count = 0;
                io_uring_for_each_cqe(&m_ring.impl, head, cqe) {
                    NAMED_SCOPE(TlsProcessOne);
                    int res = cqe->res;
                    uint64_t ud = io_uring_cqe_get_data64(cqe);
                    count++;

                    int fd = uring::fd_of(ud);
                    if (fd < 0 || (std::size_t)fd >= m_connections.size()) continue;

                    // Error or EOF
                    if (res < 0 && res != -EAGAIN) {
                        remove_connection(fd);
                        continue;
                    }

                    if (m_pending_handshakes.count(fd)) {
                        retry_handshake(fd);
                        continue;
                    }

                    if (uring::is_recv(ud)) {
                        // KTLS recv completion
                        if (res > 0) {
                            auto& cs = m_connections[fd];
                            if (cs.op == active_op::recv_ktls) {
                                cs.op = active_op::none;
                                cs.conn.buffer->advance_tail((std::size_t)res);
                                auto state = m_on_read(cs.conn);
                                apply_state(cs.conn, state);
                            }
                        }
                    } else if (uring::is_send(ud)) {
                        // KTLS send completion
                        if (res > 0) {
                            auto& cs = m_connections[fd];
                            if (cs.op == active_op::send_ktls) {
                                cs.op = active_op::none;
                                cs.conn.buffer->advance_head((std::size_t)res);
                                if (cs.conn.buffer->is_empty()) {
                                    auto state = m_on_write(cs.conn);
                                    apply_state(cs.conn, state);
                                } else {
                                    submit_send_ktls(fd);
                                }
                            }
                        }
                    } else if (uring::is_poll_in(ud)) {
                        auto& cs = m_connections[fd];
                        if (cs.op == active_op::poll_in) {
                            handle_read(cs.conn);
                        }
                    } else if (uring::is_poll_out(ud)) {
                        auto& cs = m_connections[fd];
                        if (cs.op == active_op::poll_out) {
                            handle_write(cs.conn);
                        }
                    }
                }
                io_uring_cq_advance(&m_ring.impl, count);

                // Submit all pending SQEs
                m_ring.submit();
            }

            // Cleanup
            for (auto& cs : m_connections) {
                if (cs.tls.ssl) {
                    SSL_free(cs.tls.ssl);
                    cs.tls.ssl = nullptr;
                }
                if (cs.conn.buffer) {
                    m_pl.redeem(cs.conn.buffer);
                    cs.conn.buffer = nullptr;
                }
            }
            m_pending_handshakes.clear();
            ::close(epfd);
            ::close(m_listen_fd);
        }

        void stop() override {
            m_running = false;
        }

    private:
        struct tls_per_conn {
            SSL* ssl = nullptr;
            bool ktls_active = false;
        };

        enum class active_op : uint8_t {
            none,
            recv_ktls,
            send_ktls,
            poll_in,
            poll_out,
            handshake_poll,
        };

        struct conn_state {
            connection conn;
            tls_per_conn tls;
            active_op op = active_op::none;
        };

        void apply_state(connection& conn, el_connection_state state) {
            if (state == el_connection_state::die) {
                remove_connection(conn.descriptor);
                return;
            }
            conn.set_state(state);
            if (state == el_connection_state::read) {
                auto& cs = m_connections[conn.descriptor];
                if (cs.tls.ktls_active) {
                    submit_recv_ktls(conn.descriptor);
                } else {
                    submit_poll_in(conn.descriptor);
                }
            } else if (state == el_connection_state::write) {
                auto& cs = m_connections[conn.descriptor];
                if (cs.tls.ktls_active) {
                    submit_send_ktls(conn.descriptor);
                } else {
                    submit_poll_out(conn.descriptor);
                }
            }
        }

        void handle_accept() {
            struct sockaddr_in client_addr{};
            socklen_t socklen = sizeof(client_addr);
            int sock = accept(m_listen_fd, (struct sockaddr*)&client_addr, &socklen);
            if (sock < 0) return;

            int flags = fcntl(sock, F_GETFL, 0);
            if (flags == -1) { ::close(sock); return; }
            flags |= O_NONBLOCK;
            if (fcntl(sock, F_SETFL, flags) == -1) { ::close(sock); return; }

            apply_stream_options(sock, m_cfg.accepted_stream_options);

            SSL* ssl = SSL_new(m_ctx);
            if (!ssl) { ::close(sock); return; }
            SSL_set_fd(ssl, sock);
            SSL_set_accept_state(ssl);

            if ((std::size_t)sock >= m_connections.size()) {
                m_connections.resize(sock + 1);
            }

            int ret = SSL_do_handshake(ssl);
            if (ret == 1) {
                register_connection(sock, ssl);
                if (m_cfg.enable_ktls) {
                    m_connections[sock].tls.ktls_active = try_enable_fd_ktls(ssl, sock, true);
                }
                auto state = m_on_connect(m_connections[sock].conn);
                if (state == el_connection_state::die) {
                    remove_connection(sock);
                } else {
                    apply_state(m_connections[sock].conn, state);
                    handle_read(m_connections[sock].conn);
                }
            } else {
                int err = SSL_get_error(ssl, ret);
                if (err == SSL_ERROR_WANT_READ) {
                    m_pending_handshakes.insert(sock);
                    m_connections[sock].tls.ssl = ssl;
                    m_connections[sock].conn.descriptor = sock;
                    m_connections[sock].op = active_op::handshake_poll;
                    submit_poll_in(sock);
                } else {
                    SSL_free(ssl);
                    ::close(sock);
                }
            }
        }

        void retry_handshake(int32_t fd) {
            NAMED_SCOPE(TlsUringRetryHs);
            auto& cs = m_connections[fd];
            int ret = SSL_do_handshake(cs.tls.ssl);
            if (ret == 1) {
                m_pending_handshakes.erase(fd);
                register_connection(fd, cs.tls.ssl);
                if (m_cfg.enable_ktls) {
                    cs.tls.ktls_active = try_enable_fd_ktls(cs.tls.ssl, fd, true);
                }
                auto state = m_on_connect(cs.conn);
                if (state == el_connection_state::die) {
                    remove_connection(fd);
                } else {
                    apply_state(cs.conn, state);
                    handle_read(cs.conn);
                }
            } else {
                int err = SSL_get_error(cs.tls.ssl, ret);
                if (err == SSL_ERROR_WANT_READ) {
                    cs.op = active_op::handshake_poll;
                    submit_poll_in(fd);
                } else {
                    SSL_free(cs.tls.ssl);
                    cs.tls.ssl = nullptr;
                    m_pending_handshakes.erase(fd);
                    ::close(fd);
                    connection dumb;
                    m_on_err(dumb, "uring_tls: handshake failed");
                }
            }
        }

        void register_connection(int32_t sock, SSL* ssl) {
            NAMED_SCOPE(TlsUringRegister);
            auto& cs = m_connections[sock];
            if (!cs.tls.ssl) {
                cs.tls.ssl = ssl;
            }
            cs.conn.descriptor = sock;
            cs.conn.buffer = m_pl.allocate();
            cs.op = active_op::none;
        }

        // ── I/O submissions ──────────────────────────────────────────────
        void submit_poll_in(int32_t fd) {
            if ((std::size_t)fd >= m_connections.size()) return;
            auto& cs = m_connections[fd];
            auto* sqe = m_ring.get_sqe();
            if (!sqe) return;
            io_uring_prep_poll_add(sqe, fd, POLLIN);
            io_uring_sqe_set_data64(sqe, uring::tag_poll_in(fd));
            cs.op = active_op::poll_in;
        }

        void submit_poll_out(int32_t fd) {
            if ((std::size_t)fd >= m_connections.size()) return;
            auto& cs = m_connections[fd];
            auto* sqe = m_ring.get_sqe();
            if (!sqe) return;
            io_uring_prep_poll_add(sqe, fd, POLLOUT);
            io_uring_sqe_set_data64(sqe, uring::tag_poll_out(fd));
            cs.op = active_op::poll_out;
        }

        void submit_recv_ktls(int32_t fd) {
            if ((std::size_t)fd >= m_connections.size()) return;
            auto& cs = m_connections[fd];
            if (!cs.conn.buffer) return;

            auto [data, size] = cs.conn.buffer->get_free_region();
            if (size == 0) return;

            auto* sqe = m_ring.get_sqe();
            if (!sqe) return;
            io_uring_prep_recv(sqe, fd, data, size, 0);
            io_uring_sqe_set_data64(sqe, uring::tag_recv(fd));
            cs.op = active_op::recv_ktls;
        }

        void submit_send_ktls(int32_t fd) {
            if ((std::size_t)fd >= m_connections.size()) return;
            auto& cs = m_connections[fd];
            if (!cs.conn.buffer) return;

            auto [data, size] = cs.conn.buffer->get_used_region();
            if (size == 0) return;

            auto* sqe = m_ring.get_sqe();
            if (!sqe) return;
            io_uring_prep_send(sqe, fd, data, size, 0);
            io_uring_sqe_set_data64(sqe, uring::tag_send(fd));
            cs.op = active_op::send_ktls;
        }

        // ── Read / Write handlers ────────────────────────────────────────
        void handle_read(connection& conn) {
            NAMED_SCOPE(TlsUringHandleRead);
            if (conn.get_state() != el_connection_state::read) return;

            auto& cs = m_connections[conn.descriptor];
            bool error = false;
            bool got_data = false;

            if (cs.tls.ktls_active) {
                got_data = !conn.buffer->is_empty();
            } else {
                conn.buffer->consume_free([&](void* data, std::size_t size) -> std::size_t {
                    ERR_clear_error();
                    int received = SSL_read(cs.tls.ssl, data, (int)size);
                    if (received > 0) {
                        got_data = true;
                        return (std::size_t)received;
                    }

                    int err = SSL_get_error(cs.tls.ssl, received);
                    if (err == SSL_ERROR_WANT_READ) {
                        return 0;
                    }
                    if (err == SSL_ERROR_ZERO_RETURN) {
                        return 0;
                    }
                    error = true;
                    return 0;
                });
            }

            if (got_data && !error) {
                auto state = m_on_read(conn);
                apply_state(conn, state);
            }
            if (error) {
                remove_connection(conn.descriptor);
            }
        }

        void handle_write(connection& conn) {
            NAMED_SCOPE(TlsUringHandleWrite);
            if (conn.get_state() != el_connection_state::write) return;

            auto& cs = m_connections[conn.descriptor];

            if (cs.tls.ktls_active) {
                if (conn.buffer->is_empty()) {
                    auto state = m_on_write(conn);
                    apply_state(conn, state);
                }
            } else {
                bool error = false;
                conn.buffer->consume_used([&](const void* data, std::size_t size) -> std::size_t {
                    ERR_clear_error();
                    int sent = SSL_write(cs.tls.ssl, data, (int)size);
                    if (sent > 0) return (std::size_t)sent;

                    int err = SSL_get_error(cs.tls.ssl, sent);
                    if (err == SSL_ERROR_WANT_WRITE) {
                        return 0;
                    }
                    auto err_state = m_on_err(conn, "SSL_write failed");
                    error = true;
                    apply_state(conn, err_state);
                    return size;
                });

                if (!error && conn.buffer->is_empty()) {
                    auto state = m_on_write(conn);
                    apply_state(conn, state);
                }
            }
        }

        // ── Connection management ────────────────────────────────────────
        void remove_connection(int32_t fd) {
            NAMED_SCOPE(TlsUringRemove);
            if ((std::size_t)fd >= m_connections.size()) return;
            auto& cs = m_connections[fd];

            if (cs.tls.ssl) {
                SSL_free(cs.tls.ssl);
                cs.tls.ssl = nullptr;
            }
            m_pending_handshakes.erase(fd);

            if (cs.conn.buffer) {
                m_pl.redeem(cs.conn.buffer);
                cs.conn.buffer = nullptr;
            }
            cs.op = active_op::none;
            ::close(fd);
        }

        void throw_bind_err() {
            switch (errno) {
                case EACCES: HOPE_THROW("uring_tls", "Permission denied (need root for ports < 1024)");
                case EADDRINUSE: HOPE_THROW("uring_tls", "Address already in use");
                case EADDRNOTAVAIL: HOPE_THROW("uring_tls", "Invalid address");
                case EBADF: HOPE_THROW("uring_tls", "Invalid socket descriptor");
                case EINVAL: HOPE_THROW("uring_tls", "Socket already bound");
                case ENOTSOCK: HOPE_THROW("uring_tls", "Not a socket");
                default: HOPE_THROW_ERRNO("uring_tls", "bind failed");
            }
        }

        uring::ring m_ring;
        int32_t m_listen_fd = -1;
        SSL_CTX* m_ctx = nullptr;

        tls_config m_cfg;
        buffer_pool m_pl;

        std::vector<conn_state> m_connections;
        std::unordered_set<int32_t> m_pending_handshakes;
        std::atomic<bool> m_running = true;
        TOnError m_on_err;
        TOnWrite m_on_write;
        TOnRead m_on_read;
        TConnected m_on_connect;
    };

}

#endif
