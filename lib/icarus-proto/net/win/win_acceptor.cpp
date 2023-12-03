/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#include "win_acceptor.h"
#include "icarus-proto/net/win/win_stream.h"
#include "icarus-proto/net/acceptor.h"

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
        win_acceptor() {
            icarus::io::win::init();
        }

    private:
        virtual void run(std::string_view port, on_new_connection_t&& in_on_new_connection) override {
            on_new_connection = std::move(in_on_new_connection);
            connect(port);
            state = e_state::listen_sync;
            while(active.load(std::memory_order_acquire)) {
                listen();
            }
        }

        virtual void run_async(std::string_view port, on_new_connection_t&& in_on_new_connection, on_error_t&& in_on_error) override {
            on_new_connection = std::move(in_on_new_connection);
            connect(port);
            listen_thread = std::thread([this, in_on_error] {
                while (active.load(std::memory_order_acquire)) {
                    try {
                        listen();
                    }
                    catch (const std::exception& e) {
                        in_on_error(e);
                        return;
                    }
                }
            });
            state = e_state::listen_async;
        }

        virtual void stop() override {
            active.store(false, std::memory_order_release);
            state = e_state::inactive;
            closesocket(listen_socket);
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

        void listen() const {
            if (const auto connected = ::listen(listen_socket, SOMAXCONN); connected == SOCKET_ERROR) {
                // TODO:: add error
                throw std::runtime_error("Win acceptor: Fail while listening");
            }

            // Accept a client socket
            const auto new_socket = accept(listen_socket, nullptr, nullptr);
            if (new_socket == INVALID_SOCKET) {
                throw std::runtime_error("Win acceptor: accept failed");
            }

            on_new_connection(icarus::io::create_win_stream(new_socket));
        }

        enum class e_state {
            inactive,
            listen_sync,
            listen_async,
        };

        std::thread listen_thread;
        on_new_connection_t on_new_connection;
        std::atomic<bool> active{ false };
        SOCKET listen_socket{ INVALID_SOCKET };

        e_state state{ e_state::inactive };
    };

}

namespace icarus::io {

    acceptor* create_win_acceptor() {
        return new win_acceptor;
    }

}