/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "icarus-proto/coredefs.h"

#ifdef ICARUS_WIN

#include "win_acceptor.h"
#include "icarus-proto/net/win/win_stream.h"
#include "icarus-proto/net/acceptor.h"
#include "icarus-proto/coredefs.h"
#include "icarus-proto/factory.h"

#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdexcept>
#include <thread>
#include <atomic>

#include "win_init.h"

#pragma comment (lib, "Ws2_32.lib")

namespace {

    class win_acceptor final : public icarus::io::acceptor {
    public:
        win_acceptor(std::string_view port) {
            icarus::io::win::init();
            connect(port);
        }

        virtual ~win_acceptor() override {
            closesocket(listen_socket);
        }

    private:
        virtual void accept() override {
            if (const auto connected = ::listen(listen_socket, SOMAXCONN); connected == SOCKET_ERROR) {
                // TODO:: add error
                throw std::runtime_error("Win acceptor: Fail while listening");
            }

            // Accept a client socket
            const auto new_socket = accept(listen_socket, nullptr, nullptr);
            if (new_socket == INVALID_SOCKET) {
                throw std::runtime_error("Win acceptor: accept failed");
            }

            return icarus::io::create_win_stream(new_socket);
        }

        void connect(std::string_view port) {
            addrinfo* result = nullptr;
            addrinfo hints;

            ZeroMemory(&hints, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            hints.ai_flags = AI_PASSIVE;

            if (const auto got = getaddrinfo(nullptr, port.data(), &hints, &result); got != 0) {
                // TODO:: add error
                throw std::runtime_error("Win Acceptor: Cannot resolve address and port");
            }

            listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
            if (listen_socket == INVALID_SOCKET) {
                freeaddrinfo(result);
                // TODO:: add error
                throw std::runtime_error("Win acceptor: Cannot create socket");
            }

            // Setup the TCP listening socket
            if (const auto bound = bind(listen_socket, result->ai_addr, (int)result->ai_addrlen); bound == SOCKET_ERROR) {
                freeaddrinfo(result);
                closesocket(listen_socket);
                // TODO:: add error
                throw std::runtime_error("Win acceptor: Cannot bind socket");
            }

            freeaddrinfo(result);
        }

        SOCKET listen_socket{ INVALID_SOCKET };
    };

}

namespace icarus::io {

    acceptor* create_acceptor(std::size_t port) {
        return new win_acceptor(std::to_string(port));
    }

}

#endif