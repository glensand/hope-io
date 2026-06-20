/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#if PLATFORM_WINDOWS

#include "hope-io/net/win/tcp_acceptor.h"
#include "hope-io/net/win/tcp_stream.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/init.h"

#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdexcept>
#include <thread>
#include <atomic>

#pragma comment (lib, "Ws2_32.lib")

namespace hope::io {

    tcp_acceptor::tcp_acceptor(const stream_options& opts)
        : m_options(opts) {}

    tcp_acceptor::~tcp_acceptor() {
        closesocket(m_listen_socket);
    }

    void tcp_acceptor::open(std::size_t port) {
        addrinfo* result = nullptr;
        addrinfo hints;

        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        if (const auto got = getaddrinfo(nullptr, std::to_string(port).data(), &hints, &result); got != 0) {
            throw std::runtime_error("hope-io/win_acceptor: Cannot resolve address and port");
        }

        m_listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (m_listen_socket == INVALID_SOCKET) {
            freeaddrinfo(result);
            throw std::runtime_error("hope-io/win_acceptor: Cannot create socket");
        }

        // Apply pre-bind socket options
        if (m_options.tcp_nodelay) {
            int on = 1;
            setsockopt(m_listen_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(on));
        }
        if (m_options.reuse_address) {
            int on = 1;
            setsockopt(m_listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
        }
        if (m_options.send_buffer_size > 0)
            setsockopt(m_listen_socket, SOL_SOCKET, SO_SNDBUF, (const char*)&m_options.send_buffer_size, sizeof(m_options.send_buffer_size));
        if (m_options.recv_buffer_size > 0)
            setsockopt(m_listen_socket, SOL_SOCKET, SO_RCVBUF, (const char*)&m_options.recv_buffer_size, sizeof(m_options.recv_buffer_size));

        if (const auto bound = bind(m_listen_socket, result->ai_addr, (int)result->ai_addrlen); bound == SOCKET_ERROR) {
            freeaddrinfo(result);
            closesocket(m_listen_socket);
            throw std::runtime_error("hope-io/win_acceptor: Cannot bind socket");
        }

        freeaddrinfo(result);

        if (const auto connected = ::listen(m_listen_socket, SOMAXCONN); connected == SOCKET_ERROR) {
            throw std::runtime_error("hope-io/win_acceptor: listen failed");
        }
    }

    stream* tcp_acceptor::accept() {
        const auto new_socket = ::accept(m_listen_socket, nullptr, nullptr);
        if (new_socket == INVALID_SOCKET) {
            throw std::runtime_error("hope-io/win_acceptor: accept failed");
        }

        return new tcp_stream(new_socket, m_options);
    }

    long long tcp_acceptor::raw() const {
        return m_listen_socket;
    }

    void tcp_acceptor::set_options(const stream_options& opt) {
        m_options = opt;
    }



}
#endif
