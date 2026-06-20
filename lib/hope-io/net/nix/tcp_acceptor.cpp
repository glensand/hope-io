/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"
#include <string>
#include <sys/errno.h>

#if PLATFORM_LINUX || PLATFORM_APPLE

#include "hope-io/net/nix/tcp_acceptor.h"
#include "hope-io/net/nix/tcp_stream.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/init.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>

namespace hope::io {

    tcp_acceptor::tcp_acceptor(const stream_options& opts)
        : m_options(opts) {}

    stream* tcp_acceptor::accept() {
        int client_socket;
        struct sockaddr_in client_sockaddr{};
        unsigned int sin_size = sizeof(struct sockaddr);
        if ((client_socket = ::accept(m_socket, (struct sockaddr *)&client_sockaddr, &sin_size)) == -1) {
            HOPE_THROW_ERRNO("tcp_acceptor", "cannot accept connection");
        }
        return new tcp_stream((unsigned long long)client_socket, m_options);
    }

    void tcp_acceptor::open(std::size_t port) {
        HOPE_ASSERT(m_socket == -1, "tcp_acceptor: open() called on already-open acceptor");
        if ((m_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            HOPE_THROW_ERRNO("tcp_acceptor", "cannot create socket");
        }

        // Apply pre-bind socket options (reuse, nodelay, etc.)
        {
            int on = 1;
            setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        }
        if (m_options.tcp_nodelay) {
            int on = 1;
            setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        }
#ifdef SO_REUSEPORT
        if (m_options.reuse_port) {
            int on = 1;
            setsockopt(m_socket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
        }
#endif
#ifdef SO_PRIORITY
        if (m_options.priority >= 0)
            setsockopt(m_socket, SOL_SOCKET, SO_PRIORITY, &m_options.priority, sizeof(m_options.priority));
#endif
#ifdef SO_MARK
        if (m_options.mark >= 0)
            setsockopt(m_socket, SOL_SOCKET, SO_MARK, &m_options.mark, sizeof(m_options.mark));
#endif
#ifdef SO_BINDTODEVICE
        if (!m_options.bind_device.empty())
            setsockopt(m_socket, SOL_SOCKET, SO_BINDTODEVICE,
                       m_options.bind_device.c_str(), m_options.bind_device.size());
#endif
        if (m_options.ttl >= 0)
            setsockopt(m_socket, IPPROTO_IP, IP_TTL, &m_options.ttl, sizeof(m_options.ttl));
        if (m_options.send_buffer_size > 0)
            setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, &m_options.send_buffer_size, sizeof(m_options.send_buffer_size));
        if (m_options.recv_buffer_size > 0)
            setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, &m_options.recv_buffer_size, sizeof(m_options.recv_buffer_size));

        struct sockaddr_in server_sockaddr{};
        server_sockaddr.sin_family = AF_INET;
        server_sockaddr.sin_port = htons(port);
        server_sockaddr.sin_addr.s_addr = INADDR_ANY;
        memset(&(server_sockaddr.sin_zero), 0, 8);

        if ((bind(m_socket, (struct sockaddr *)&server_sockaddr, sizeof(struct sockaddr))) == -1) {
            HOPE_THROW_ERRNO("tcp_acceptor",
                "cannot bind socket port:" + std::to_string(port));
        }

        // Non-block mode after bind
        if (m_options.non_block_mode) {
            int flags = fcntl(m_socket, F_GETFL, 0);
            if (flags != -1)
                fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
        }

        auto backlog = 10;
        if (listen(m_socket, backlog) == -1) {
            HOPE_THROW_ERRNO("tcp_acceptor", "listen failed");
        }
    }

    void tcp_acceptor::close() {
        ::close(m_socket);
    }

    void tcp_acceptor::set_options(const hope::io::stream_options& opt) {
        HOPE_ASSERT(m_socket != -1, "tcp_acceptor: set_options() called before open()");
        m_options = opt;
        int flags = fcntl(m_socket, F_GETFL, 0);
        if (flags == -1) {
            HOPE_THROW_ERRNO("tcp_acceptor", "cannot get socket flags");
        }
        flags = opt.non_block_mode ? flags | O_NONBLOCK : flags & (~O_NONBLOCK);
        if (fcntl(m_socket, F_SETFL, flags) == -1) {
            HOPE_THROW_ERRNO("tcp_acceptor", "cannot set non-block flag");
        }
    }

    long long tcp_acceptor::raw() const { return m_socket; }



}
#endif
