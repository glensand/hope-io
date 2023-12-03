/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#include <stdexcept>

#include "icarus-proto/net/stream.h"
#include "icarus-proto/net/win/win_init.h"

namespace {

    class win_stream final : public icarus::io::stream {
    public:
        explicit win_stream(unsigned long long in_socket) {
            icarus::io::win::init();

            if (in_socket != 0)
                socket = in_socket;
        }

        virtual ~win_stream() override {
            win_stream::disconnect();
        }

    private:
        virtual void connect(const std::string_view ip, const std::string_view port) override {
            // just clear entire structures
            if (socket != INVALID_SOCKET)
                throw std::runtime_error("Stream had already been connected");

            addrinfo* result_addr_info{ nullptr };
            addrinfo hints_addr_info{ };
            ZeroMemory(&hints_addr_info, sizeof(hints_addr_info));
            hints_addr_info.ai_family = AF_INET;
            hints_addr_info.ai_socktype = SOCK_STREAM;
            hints_addr_info.ai_protocol = IPPROTO_TCP;

            // Resolve the server address and port
            const int32_t result = getaddrinfo(ip.data(), port.data(), &hints_addr_info, &result_addr_info);
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
                throw std::runtime_error("TCP error: Could not resolve address");
            }

            socket = INVALID_SOCKET;

            // Attempt to connect to an address until one succeeds
            for (const auto* address_info = result_addr_info;
                address_info != nullptr && socket == INVALID_SOCKET; address_info = address_info->ai_next) {
                socket = ::socket(address_info->ai_family, address_info->ai_socktype, address_info->ai_protocol);
                if (socket != INVALID_SOCKET)
                {
                    int on = 1;
                    int error = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));
                    if (error == 0) {
                        error = ::connect(socket, address_info->ai_addr, (int)address_info->ai_addrlen);
                    }

                    if (error == SOCKET_ERROR) {
                        closesocket(socket);
                        socket = INVALID_SOCKET;
                    }
                }
            }

            if (socket == INVALID_SOCKET) {
                // todo:: add addr to the exception, add log
                throw std::runtime_error("TCP error: Could not connect socket");
            }
        }

        virtual void disconnect() override {
            closesocket(socket);
            socket = INVALID_SOCKET;
        }

        virtual void write(const void* data, std::size_t length) override {
            // todo a
            const auto sent = send(socket, (const char*)data, (int)length, 0);
            if (sent == SOCKET_ERROR) {
                // TODO use WSAGetLastError
                throw std::runtime_error("TCP error: Failed to send data");
            }

            assert((std::size_t)sent == length);
        }

        virtual void read(void* data, std::size_t length) override {
            auto* buffer = (char*)data;
            while (length != 0) {
                const auto received = recv(socket, buffer, (int)length, 0);
                if (received < 0) {
                    // TODO use WSAGetLastError
                    throw std::runtime_error("TCP error: Failed to receive data");
                }
                length -= received;
                buffer += received;
            }
        }

        SOCKET socket{ INVALID_SOCKET };
    };

}

namespace icarus::io {

    stream* create_win_stream(unsigned long long socket = 0) {
        return new win_stream(socket);
    }

}
