/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#if PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#include <array>
#include <stdexcept>

#include "hope-io/net/win/tcp_stream.h"
#include "hope-io/net/init.h"
#include "hope-io/net/factory.h"

 // For internal use, since windows one is not acceptable
#undef INVALID_SOCKET
#define INVALID_SOCKET 0

namespace {

    auto throw_error(std::string msg, int code){
        throw std::runtime_error(msg + std::to_string(code));
    }

}

namespace hope::io {

    tcp_stream::tcp_stream(unsigned long long in_socket) {
        m_socket = in_socket;
    }

    tcp_stream::~tcp_stream() {
        tcp_stream::disconnect();
    }

    std::string tcp_stream::get_endpoint() const {
        struct sockaddr_in remote_sin{};
        int remote_sinlen = sizeof(remote_sin);
        if (getpeername(m_socket, (struct sockaddr*)&remote_sin, &remote_sinlen) == SOCKET_ERROR) {
            return "";
        }
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &remote_sin.sin_addr, addr_str, INET_ADDRSTRLEN);
        return std::string(addr_str);
    }

    int32_t tcp_stream::platform_socket() const {
        return (int32_t)m_socket;
    }

    void tcp_stream::set_options(const stream_options& options) {
        if (m_socket == INVALID_SOCKET) {
            return;
        }
        setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO,
                   (const char*)&options.read_timeout, sizeof(options.read_timeout));
        setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO,
                   (const char*)&options.write_timeout, sizeof(options.write_timeout));
    }

    void tcp_stream::connect(std::string_view ip, std::size_t port) {
        if (m_socket != INVALID_SOCKET)
            throw_error("hope-io/win_stream: had already been connected", WSAGetLastError());

        addrinfo* result_addr_info{ nullptr };
        addrinfo hints_addr_info{ };
        ZeroMemory(&hints_addr_info, sizeof(hints_addr_info));
        hints_addr_info.ai_family = AF_INET;
        hints_addr_info.ai_socktype = SOCK_STREAM;
        hints_addr_info.ai_protocol = IPPROTO_TCP;

        const int32_t result = getaddrinfo(ip.data(), std::to_string(port).c_str(), &hints_addr_info, &result_addr_info);
        struct free_address_info final {
            ~free_address_info() {
                if (addr_info) {
                    freeaddrinfo(addr_info);
                }
            }
            addrinfo* addr_info;
        } free_addr_info{ result_addr_info };

        if (result != 0) {
            throw_error("hope-io/win_stream: could not resolve address", WSAGetLastError());
        }

        m_socket = INVALID_SOCKET;

        for (const auto* address_info = result_addr_info;
            address_info != nullptr && m_socket == INVALID_SOCKET; address_info = address_info->ai_next) {
            m_socket = ::socket(address_info->ai_family, address_info->ai_socktype, address_info->ai_protocol);
            if (m_socket != INVALID_SOCKET)
            {
                u_long mode = 1;
                if (ioctlsocket(m_socket, FIONBIO, &mode) != 0) {
                    closesocket(m_socket);
                    m_socket = INVALID_SOCKET;
                } else {
                    int on = 1;
                    int error = setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));
                    if (error == NO_ERROR) {
                        error = ::connect(m_socket, address_info->ai_addr, (int)address_info->ai_addrlen);
                        if (WSAGetLastError() == WSAEWOULDBLOCK) {
                            error = NO_ERROR;
                            mode = 0;
                            error |= ioctlsocket(m_socket, FIONBIO, &mode);
                            fd_set write, err;
                            FD_ZERO(&write);
                            FD_ZERO(&err);
                            FD_SET(m_socket, &write);
                            FD_SET(m_socket, &err);

                            TIMEVAL timeout;
                            timeout.tv_sec = 3;
                            timeout.tv_usec = 0;
                            select(0, NULL, &write, &err, &timeout);			
                            if(!FD_ISSET(m_socket, &write)) {
                                error = SOCKET_ERROR;
                            }
                        }
                    }

                    if (error == SOCKET_ERROR) {
                        closesocket(m_socket);
                        m_socket = INVALID_SOCKET;
                    }
                }
            }
        }

        if (m_socket == INVALID_SOCKET) {
            throw_error("hope-io/win_stream: Could not connect socket", WSAGetLastError());
        }
    }

    void tcp_stream::disconnect() {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    void tcp_stream::write(const void* data, std::size_t length) {
        if (length == 0) return;

        WSABUF wsa_buf;
        wsa_buf.buf = const_cast<char*>(static_cast<const char*>(data));
        wsa_buf.len = static_cast<UINT>(length);

        DWORD bytes_sent = 0;
        const int res = WSASend(m_socket, &wsa_buf, 1, &bytes_sent, 0, nullptr, nullptr);
        if (res == SOCKET_ERROR) {
            throw_error("hope-io/win_stream: Failed to send data", WSAGetLastError());
        }
    }

    void tcp_stream::write_v(std::span<const std::span<const char>> buffers) {
        if (buffers.empty()) return;

        std::array<WSABUF, 1024> wsabufs{};

        for (std::size_t i = 0; i < buffers.size(); ++i) {
            wsabufs[i].buf = const_cast<char*>(buffers[i].data());
            wsabufs[i].len = static_cast<UINT>(buffers[i].size());
        }

        DWORD bytes_sent = 0;
        const int res = WSASend(m_socket, wsabufs.data(), static_cast<DWORD>(buffers.size()),
                                &bytes_sent, 0, nullptr, nullptr);
        if (res == SOCKET_ERROR) {
            throw_error("hope-io/win_stream: Failed to send data", WSAGetLastError());
        }
    }

    size_t tcp_stream::read(void* data, std::size_t length) {
        auto* buffer = (char*)data;
        while (length != 0) {
            const auto received = recv(m_socket, buffer, static_cast<int>(length), 0);
            if (received < 0) {
                throw_error("hope-io/win_stream: Failed to receive data", WSAGetLastError());
            }
            length -= received;
            buffer += received;
        }
        return length;
    }

    size_t tcp_stream::read_once(void* data, std::size_t length) {
        const auto received = recv(m_socket, (char*)data, length, 0);
        if (received < 0) {
            throw_error("hope-io/win_stream: Failed to receive data", WSAGetLastError());
        }
        return received;
    }

    void tcp_stream::stream_in(std::string& buffer) {
        assert(false && "Not implemented");
    }



}

#endif
