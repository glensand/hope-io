/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * terms of the MIT license.
 */
#include "hope-io/coredefs.h"
#if PLATFORM_LINUX || PLATFORM_APPLE
#include "hope-io/net/nix/tcp_stream.h"
#include "hope-io/net/factory.h"
#include <array>
#include <cassert>
#include <stdexcept>
#include <unistd.h>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>
#include <sys/time.h>

namespace hope::io {
    tcp_stream::tcp_stream(unsigned long long in_socket) {
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
        if (::connect(m_socket, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1) HOPE_THROW_ERRNO("tcp_stream", "cannot connect to host");
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
    size_t tcp_stream::read_once(void* data, std::size_t length) { return recv(m_socket, (char*)data, length, 0); }
    void tcp_stream::stream_in(std::string& buffer) { assert(false && "Not implemented"); }
    void tcp_stream::set_options(const hope::io::stream_options& opt) {
        HOPE_ASSERT(m_socket >= 0, "tcp_stream: set_options() called before connect()");
        struct timeval timeout;
        timeout.tv_sec = opt.write_timeout / 1000;
        timeout.tv_usec = 1000 * (opt.write_timeout - timeout.tv_sec * 1000);
        if (setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) HOPE_THROW_ERRNO("tcp_stream", "cannot set write timeout");
        timeout.tv_sec = opt.read_timeout / 1000;
        timeout.tv_usec = 1000 * (opt.read_timeout - timeout.tv_sec * 1000);
        if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) HOPE_THROW_ERRNO("tcp_stream", "cannot set read timeout");
        int flags = fcntl(m_socket, F_GETFL, 0);
        if (flags == -1) HOPE_THROW_ERRNO("tcp_stream", "cannot get socket flags");
        flags = opt.non_block_mode ? flags | O_NONBLOCK : flags & (~O_NONBLOCK);
        if (fcntl(m_socket, F_SETFL, flags) == -1) HOPE_THROW_ERRNO("tcp_stream", "cannot set non-block flag");
    }

    stream* create_stream(unsigned long long socket) { return new tcp_stream(socket); }
}
#endif
