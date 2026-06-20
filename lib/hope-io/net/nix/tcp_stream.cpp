/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * terms of the MIT license.
 */
#include "hope-io/coredefs.h"
#if PLATFORM_LINUX || PLATFORM_APPLE
#include "hope-io/net/nix/tcp_stream.h"
#include <array>
#include <cassert>
#include <stdexcept>
#include <unistd.h>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>
#include <sys/time.h>

namespace hope::io {
    tcp_stream::tcp_stream(unsigned long long in_socket, const stream_options& opts)
        : m_options(opts)
    {
        HOPE_ASSERT(in_socket != 0, "tcp_stream: fd 0 is stdin, not a network socket");
        if (in_socket == static_cast<unsigned long long>(-1)) {
            m_socket = -1;
        } else {
            m_socket = static_cast<int>(in_socket);
        }
    }
    tcp_stream::~tcp_stream() { disconnect(); }
    std::string tcp_stream::get_endpoint() const {
        struct sockaddr_in remote_sin{};
        socklen_t remote_sinlen = sizeof(remote_sin);
        getpeername(m_socket, (struct sockaddr*)&remote_sin, &remote_sinlen);
        char *peeraddrpresn = inet_ntoa(remote_sin.sin_addr);
        return peeraddrpresn;
    }
    int32_t tcp_stream::platform_socket() const { return (int32_t)m_socket; }
    void tcp_stream::connect(std::string_view ip, std::size_t port) {
        HOPE_ASSERT(m_socket == -1, "tcp_stream: connect() called without prior disconnect()");
        struct addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* res;
        int err = getaddrinfo(ip.data(), nullptr, &hints, &res);
        if (err != 0) HOPE_THROW_ERRNO("tcp_stream", "cannot resolve ip");
        struct sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        auto* ipv4 = (struct sockaddr_in*)res->ai_addr;
        serv_addr.sin_addr = ipv4->sin_addr;
        freeaddrinfo(res);
        if ((m_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) HOPE_THROW_ERRNO("tcp_stream", "cannot create socket");

        // Apply pre-connect socket options (TCP_NODELAY, buffer sizes, keepalive, etc.)
        apply_constructor_options();

        if (::connect(m_socket, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1) HOPE_THROW_ERRNO("tcp_stream", "cannot connect to host");

        // non_block_mode applied post-connect (blocking connect needs to complete first)
        if (m_options.non_block_mode) {
            auto flags = fcntl(m_socket, F_GETFL, 0);
            if (flags != -1) {
                fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
            }
        }
    }
    void tcp_stream::disconnect() {
        if (m_socket >= 0) { close(m_socket); }
        m_socket = -1;
    }
    void tcp_stream::write(const void* data, std::size_t length) {
        std::size_t bytes_sent = 0;
        while (bytes_sent != length) {
            auto op_res = send(m_socket, (char*)data + bytes_sent, length - bytes_sent, 0);
            if (op_res == -1) HOPE_THROW_ERRNO("tcp_stream", "cannot write to stream");
            bytes_sent += op_res;
        }
    }
    void tcp_stream::write_v(std::span<const std::span<const char>> buffers) {
        if (buffers.empty()) return;
        std::array<iovec, 1024> iovs;
        std::size_t total = 0;
        auto count = buffers.size();
        for (auto i = 0u; i < count; ++i) {
            iovs[i] = iovec{const_cast<char*>(buffers[i].data()), buffers[i].size()};
            total += buffers[i].size();
        }
        auto op_res = writev(m_socket, iovs.data(), (int)count);
        if (op_res == -1) HOPE_THROW_ERRNO("tcp_stream", "cannot writev to stream");
        auto written = (std::size_t)op_res;
        if (written < total) {
            std::size_t skip = written;
            for (auto i = 0u; i < count; ++i) {
                if (skip >= buffers[i].size()) { skip -= buffers[i].size(); continue; }
                write(buffers[i].data() + skip, buffers[i].size() - skip);
                skip = 0;
            }
        }
    }
    size_t tcp_stream::read(void* data, std::size_t length) {
        std::size_t recv_bytes = 0;
        while (recv_bytes != length) {
            auto op_res = recv(m_socket, (char*)data + recv_bytes, length - recv_bytes, 0);
            if (op_res == -1) HOPE_THROW_ERRNO("tcp_stream", "cannot read from stream");
            if (op_res == 0) HOPE_THROW("tcp_stream", "connection closed by peer");
            recv_bytes += op_res;
        }
        return recv_bytes;
    }
    size_t tcp_stream::read_once(void* data, std::size_t length) {
        auto received = recv(m_socket, (char*)data, length, 0);
        if (received < 0) return 0;
        return (std::size_t)received;
    }
    void tcp_stream::stream_in(std::string& buffer) { assert(false && "Not implemented"); }

    void tcp_stream::apply_constructor_options() {
        // ── IPPROTO_TCP ────────────────────────────────────────
        if (m_options.tcp_nodelay) {
            int on = 1;
            setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        }
#ifdef TCP_USER_TIMEOUT
        if (m_options.tcp_user_timeout >= 0) {
            setsockopt(m_socket, IPPROTO_TCP, TCP_USER_TIMEOUT,
                       &m_options.tcp_user_timeout, sizeof(m_options.tcp_user_timeout));
        }
#endif

        // ── Keepalive ──────────────────────────────────────────
        if (m_options.keepalive) {
            int on = 1;
            setsockopt(m_socket, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
        }
#ifdef TCP_KEEPIDLE
        if (m_options.keepidle >= 0)
            setsockopt(m_socket, IPPROTO_TCP, TCP_KEEPIDLE, &m_options.keepidle, sizeof(m_options.keepidle));
#endif
#ifdef TCP_KEEPINTVL
        if (m_options.keepintvl >= 0)
            setsockopt(m_socket, IPPROTO_TCP, TCP_KEEPINTVL, &m_options.keepintvl, sizeof(m_options.keepintvl));
#endif
#ifdef TCP_KEEPCNT
        if (m_options.keepcnt >= 0)
            setsockopt(m_socket, IPPROTO_TCP, TCP_KEEPCNT, &m_options.keepcnt, sizeof(m_options.keepcnt));
#endif

        // ── Socket buffer ──────────────────────────────────────
        if (m_options.send_buffer_size > 0)
            setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, &m_options.send_buffer_size, sizeof(m_options.send_buffer_size));
        if (m_options.recv_buffer_size > 0)
            setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, &m_options.recv_buffer_size, sizeof(m_options.recv_buffer_size));

        // ── Socket behavior ────────────────────────────────────
        if (m_options.reuse_address) {
            int on = 1;
            setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        }
#ifdef SO_REUSEPORT
        if (m_options.reuse_port) {
            int on = 1;
            setsockopt(m_socket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
        }
#endif
        if (m_options.linger_on) {
            struct linger l;
            l.l_onoff = m_options.linger_on;
            l.l_linger = m_options.linger_seconds;
            setsockopt(m_socket, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
        }
#ifdef SO_PRIORITY
        if (m_options.priority >= 0)
            setsockopt(m_socket, SOL_SOCKET, SO_PRIORITY, &m_options.priority, sizeof(m_options.priority));
#endif

        // ── IP-level ───────────────────────────────────────────
        if (m_options.ttl >= 0)
            setsockopt(m_socket, IPPROTO_IP, IP_TTL, &m_options.ttl, sizeof(m_options.ttl));
#ifdef IP_TOS
        if (m_options.tos >= 0)
            setsockopt(m_socket, IPPROTO_IP, IP_TOS, &m_options.tos, sizeof(m_options.tos));
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

        // ── Timeouts ───────────────────────────────────────────
        struct timeval tv;
        tv.tv_sec = m_options.read_timeout / 1000;
        tv.tv_usec = (m_options.read_timeout % 1000) * 1000;
        setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        tv.tv_sec = m_options.write_timeout / 1000;
        tv.tv_usec = (m_options.write_timeout % 1000) * 1000;
        setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    void tcp_stream::set_options(const hope::io::stream_options& opt) {
        HOPE_ASSERT(m_socket >= 0, "tcp_stream: set_options() called before connect()");
        m_options = opt;
        apply_constructor_options();
        // Re-apply non_block_mode
        int flags = fcntl(m_socket, F_GETFL, 0);
        if (flags == -1) {
            HOPE_THROW_ERRNO("tcp_stream", "cannot get socket flags");
        }
        flags = opt.non_block_mode ? flags | O_NONBLOCK : flags & (~O_NONBLOCK);
        if (fcntl(m_socket, F_SETFL, flags) == -1) {
            HOPE_THROW_ERRNO("tcp_stream", "cannot set non-block flag");
        }
    }


}
#endif
