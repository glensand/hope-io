/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
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

namespace {

    class win_stream final : public hope::io::stream {
    public:
        explicit win_stream(unsigned long long in_socket) {
            hope::io::init();

            if (in_socket != 0)
                m_socket = in_socket;
        }

        virtual ~win_stream() override {
            win_stream::disconnect();
            hope::io::deinit();
        }

    private:
        virtual std::string get_endpoint() const override {
            assert(false && "not implemented");
            return "";
        }

        [[nodiscard]] int32_t platform_socket() const override {
            return (int32_t)m_socket;
        }

        virtual void connect(const std::string_view ip, std::size_t port) override {
            // just clear entire structures
            if (m_socket != INVALID_SOCKET)
                throw std::runtime_error("hope-io/win_stream: had already been connected");

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
                throw std::runtime_error("hope-io/win_stream: could not resolve address");
            }

            m_socket = INVALID_SOCKET;

            // Attempt to connect to an address until one succeeds
            for (const auto* address_info = result_addr_info;
                address_info != nullptr && m_socket == INVALID_SOCKET; address_info = address_info->ai_next) {
                m_socket = ::socket(address_info->ai_family, address_info->ai_socktype, address_info->ai_protocol);
                if (m_socket != INVALID_SOCKET)
                {
                    int on = 1;
                    int error = setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));
                    if (error == 0) {
                        error = ::connect(m_socket, address_info->ai_addr, (int)address_info->ai_addrlen);
                    }

                    if (error == SOCKET_ERROR) {
                        closesocket(m_socket);
                        m_socket = INVALID_SOCKET;
                    }
                }
            }

            if (m_socket == INVALID_SOCKET) {
                // todo:: add addr to the exception, add log
                throw std::runtime_error("hope-io/win_stream: Could not connect socket");
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
                throw std::runtime_error("hope-io/win_stream: Failed to send data");
            }

            assert((std::size_t)sent == length);
        }

        virtual void read(void* data, std::size_t length) override {
            auto* buffer = (char*)data;
            while (length != 0) {
                const auto received = recv(m_socket, buffer, (int)length, 0);
                if (received < 0) {
                    // TODO use WSAGetLastError
                    throw std::runtime_error("hope-io/win_stream: Failed to receive data");
                }
                length -= received;
                buffer += received;
            }
        }

        SOCKET m_socket{ INVALID_SOCKET };
    };

}

namespace hope::io {

    stream* create_stream(unsigned long long socket) {
        return new win_stream(socket);
    }

}

#endif
