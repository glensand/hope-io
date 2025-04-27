/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#ifdef ICARUS_WIN

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#include <stdexcept>

#include "hope-io/net/stream.h"
#include "hope-io/net/init.h"
#include "hope-io/net/factory.h"

 // For internal use, since windows one is not acceptable
#undef INVALID_SOCKET
#define INVALID_SOCKET 0

namespace {

    auto throw_error(std::string msg, int code){
        throw std::runtime_error(msg + std::to_string(code));
    }

    class win_stream final : public hope::io::stream {
    public:
        explicit win_stream(unsigned long long in_socket) {
            m_socket = in_socket;
        }

        virtual ~win_stream() override {
            win_stream::disconnect();
        }

    private:
        virtual std::string get_endpoint() const override {
            assert(false && "not implemented");
            return "";
        }

        [[nodiscard]] int32_t platform_socket() const override {
            return (int32_t)m_socket;
        }

        virtual void set_options(const options& options) override {
            if (m_socket != INVALID_SOCKET)
                throw_error("hope-io/win_stream: cannot set options when the socket is connected:", WSAGetLastError());
            m_options = options;
        }

        virtual void connect(const std::string_view ip, std::size_t port) override {
            // just clear entire structures
            if (m_socket != INVALID_SOCKET)
                throw_error("hope-io/win_stream: had already been connected", WSAGetLastError());

            addrinfo* result_addr_info{ nullptr };
            addrinfo hints_addr_info{ };
            ZeroMemory(&hints_addr_info, sizeof(hints_addr_info));
            hints_addr_info.ai_family = AF_INET;
            hints_addr_info.ai_socktype = SOCK_STREAM;
            hints_addr_info.ai_protocol = IPPROTO_TCP;

            // Resolve the server address and port
            const int32_t result = getaddrinfo(ip.data(), std::to_string(port).c_str(), &hints_addr_info, &result_addr_info);
            struct free_address_info final {  // NOLINT(cppcoreguidelines-special-member-functions)
                ~free_address_info() {
                    if (addr_info) {
                        freeaddrinfo(addr_info);
                    }
                }
                addrinfo* addr_info;
            } free_addr_info{ result_addr_info };

            if (result != 0) {
                // todo:: add addr to the exception, add log
                throw_error("hope-io/win_stream: could not resolve address", WSAGetLastError());
            }

            m_socket = INVALID_SOCKET;

            // Attempt to connect to an address until one succeeds
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
                        error |= setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&m_options.read_timeout, sizeof(m_options.read_timeout));
                        error |= setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&m_options.write_timeout, sizeof(m_options.write_timeout));
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
                                timeout.tv_sec = m_options.connection_timeout / 1000;
                                timeout.tv_usec = m_options.connection_timeout - timeout.tv_sec * 1000;
                                // check if the socket is ready
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
                // todo:: add addr to the exception, add log
                throw_error("hope-io/win_stream: Could not connect socket", WSAGetLastError());
            }
        }

        virtual void disconnect() override {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }

        virtual void write(const void* data, std::size_t length) override {
            // todo a
            const auto sent = send(m_socket, (const char*)data, (int)length, 0);
            if (sent == SOCKET_ERROR) {
                // TODO use WSAGetLastError
                throw_error("hope-io/win_stream: Failed to send data", WSAGetLastError());
            }

            assert(static_cast<std::size_t>(sent) == length);
        }

        virtual size_t read(void* data, std::size_t length) override {
            auto* buffer = (char*)data;
            while (length != 0) {
                const auto received = recv(m_socket, buffer, static_cast<int>(length), 0);
                if (received < 0) {
                    // TODO use WSAGetLastError
                    throw_error("hope-io/win_stream: Failed to receive data", WSAGetLastError());
                }
                length -= received;
                buffer += received;
            }
            return length;
        }

        virtual size_t read_once(void* data, std::size_t length) override {
            const auto received = recv(m_socket, (char*)data, length - 1, 0);
            if (received < 0) {
                // TODO use WSAGetLastError
                throw_error("hope-io/win_stream: Failed to receive data", WSAGetLastError());
            }
            return received;
        }

        virtual void stream_in(std::string& buffer) override {
            assert(false && "Not implemented");
        }

        SOCKET m_socket{ INVALID_SOCKET };
        options m_options;
    };

}

namespace hope::io {

    stream* create_stream(unsigned long long socket) {
        return new win_stream(socket);
    }

}

#endif
