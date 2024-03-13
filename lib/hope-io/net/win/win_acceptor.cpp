/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "hope-io/coredefs.h"

#ifdef ICARUS_WIN

#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
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

namespace {

    class win_acceptor final : public hope::io::acceptor {
    public:
        win_acceptor(std::string_view port) {
            hope::io::init();
            connect(port);
        }

        virtual ~win_acceptor() override {
            closesocket(m_listen_socket);
        }

    private:
        virtual hope::io::stream* accept() override {
            if (const auto connected = ::listen(m_listen_socket, SOMAXCONN); connected == SOCKET_ERROR) {
                // TODO:: add error
                throw std::runtime_error("Win acceptor: Fail while listening");
            }

            // Accept a client socket
            const auto new_socket = ::accept(m_listen_socket, nullptr, nullptr);
            if (new_socket == INVALID_SOCKET) {
                throw std::runtime_error("Win acceptor: accept failed");
            }

            return hope::io::create_stream(new_socket);
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

            m_listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
            if (m_listen_socket == INVALID_SOCKET) {
                freeaddrinfo(result);
                // TODO:: add error
                throw std::runtime_error("Win acceptor: Cannot create socket");
            }

            // Setup the TCP listening socket
            if (const auto bound = bind(m_listen_socket, result->ai_addr, (int)result->ai_addrlen); bound == SOCKET_ERROR) {
                freeaddrinfo(result);
                closesocket(m_listen_socket);
                // TODO:: add error
                throw std::runtime_error("Win acceptor: Cannot bind socket");
            }

            freeaddrinfo(result);
        }

        SOCKET m_listen_socket{ INVALID_SOCKET };
    };

}

namespace hope::io {

    acceptor* create_acceptor(std::size_t port) {
        return new win_acceptor(std::to_string(port));
    }

}

#endif