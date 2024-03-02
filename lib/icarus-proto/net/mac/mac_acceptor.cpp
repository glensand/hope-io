//
// Created by Nikolay Fedotenko on 05.12.2023.
//

#include "mac_acceptor.h"
#include "icarus-proto/net/mac/mac_stream.h"
#include "icarus-proto/net/acceptor.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <stdexcept>
#include <thread>
#include <atomic>
#include <charconv>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "modernize-use-override"

namespace {

    template <class T, class... Args>
    void from_chars_throws(const char* first, const char* last, T &t, Args... args) {
        std::from_chars_result res = std::from_chars(first, last, t, args... );

        // These two exceptions reflect the behavior of std::stoi.
        if (res.ec == std::errc::invalid_argument) {
            throw std::invalid_argument{"invalid_argument"};
        }
        else if (res.ec == std::errc::result_out_of_range) {
            throw std::out_of_range{"out_of_range"};
        }
    }

    class mac_acceptor final : public icarus::io::acceptor {
        constexpr static int invalid_socket = -1;

    public:
        mac_acceptor() = default;

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
            close(listen_socket);
        }

        void connect(std::string_view port) {
//            if (listen_socket > invalid_socket)
//                throw std::runtime_error("Acceptor had already been connected");

            int port_from_chars;
            from_chars_throws(port.data(), port.data() + port.size(), port_from_chars);

            sockaddr_in sa{ };
            sa.sin_family = AF_INET;
            sa.sin_port = htons(port_from_chars);
            sa.sin_addr.s_addr = htonl(INADDR_ANY);

            listen_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

            if (listen_socket <= invalid_socket) {
                // TODO:: add error
                throw std::runtime_error("Mac acceptor: Cannot create socket");
            }

            // Set up the TCP listening socket
            if (const auto bound = bind(listen_socket, (struct sockaddr*)(&sa), sizeof(sa)); bound != 0) {
                // TODO:: Shutdown should be called with close only on the server
                //shutdown(listen_socket, SHUT_RDWR);
                close(listen_socket);
                // TODO:: add error
                throw std::runtime_error("Mac acceptor: Cannot bind socket");
            }
        }

        void listen() const {
            if (const auto connected = ::listen(listen_socket, SOMAXCONN); connected != 0) {
                // TODO:: add error
                throw std::runtime_error("Mac acceptor: Fail while listening");
            }

            // Accept a client socket
            const auto new_socket = accept(listen_socket, nullptr, nullptr);
            if (new_socket == invalid_socket)
                throw std::runtime_error("Mac acceptor: accept failed");

            on_new_connection(icarus::io::create_mac_stream(new_socket));
        }

        enum class e_state {
            inactive,
            listen_sync,
            listen_async,
        };

        std::thread listen_thread;
        on_new_connection_t on_new_connection;
        std::atomic<bool> active{ false };
        int listen_socket{ invalid_socket };

        e_state state{ e_state::inactive };
    };

}

#pragma clang diagnostic pop

namespace icarus::io {

    acceptor* create_mac_acceptor() {
        return new mac_acceptor;
    }

}
