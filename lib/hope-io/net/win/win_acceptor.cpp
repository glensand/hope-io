/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#ifdef ICARUS_WIN

#include "hope-io/net/event_loop.h"
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

namespace hope::io {
    std::function<void(const event_loop::connection& conn)> event_loop::connection::on_state_changed;
}

namespace {

    class win_acceptor final : public hope::io::acceptor {
    public:
        win_acceptor() = default;

        virtual ~win_acceptor() override {
            closesocket(m_listen_socket);
        }

    private:

        virtual void open(std::size_t port) override {
            addrinfo* result = nullptr;
            addrinfo hints;

            ZeroMemory(&hints, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            hints.ai_flags = AI_PASSIVE;

            if (const auto got = getaddrinfo(nullptr, std::to_string(port).data(), &hints, &result); got != 0) {
                // TODO:: add port + address
                throw std::runtime_error("hope-io/win_acceptor: Cannot resolve address and port");
            }

            m_listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
            if (m_listen_socket == INVALID_SOCKET) {
                freeaddrinfo(result);
                throw std::runtime_error("hope-io/win_acceptor: Cannot create socket");
            }

            if (const auto bound = bind(m_listen_socket, result->ai_addr, (int)result->ai_addrlen); bound == SOCKET_ERROR) {
                freeaddrinfo(result);
                closesocket(m_listen_socket);
                // TODO:: add port
                throw std::runtime_error("hope-io/win_acceptor: Cannot bind socket");
            }

            freeaddrinfo(result);

            if (const auto connected = ::listen(m_listen_socket, SOMAXCONN); connected == SOCKET_ERROR) {
                throw std::runtime_error("hope-io/win_acceptor: listen failed");
            }
        }

        virtual hope::io::stream* accept() override {
            const auto new_socket = ::accept(m_listen_socket, nullptr, nullptr);
            if (new_socket == INVALID_SOCKET) {
                throw std::runtime_error("hope-io/win_acceptor: accept failed");
            }

            return hope::io::create_stream(new_socket);
        }

        virtual long long raw() const override {
            return m_listen_socket;
        }

        virtual void set_options(const struct hope::io::stream_options&) override {
            // TODO:: implement
        }

        SOCKET m_listen_socket{ INVALID_SOCKET };
    };

}

namespace hope::io {

    acceptor* create_acceptor() {
        return new win_acceptor();
    }

    event_loop* create_event_loop() {
        return nullptr;
    }

    event_loop* create_event_loop2(std::size_t max_concurrent_connections) {
        return nullptr;
    }
    
}

#endif