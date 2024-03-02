//
// Created by Nikolay Fedotenko on 05.12.2023.
//

#include "mac_stream.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <stdexcept>
#include <charconv>

#include "icarus-proto/net/stream.h"

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

    class mac_stream final : public icarus::io::stream {
        constexpr static int invalid_socket = -1;

    public:
        explicit mac_stream(int in_socket) {
            if (in_socket != 0)
                socket = in_socket;
        }

        virtual ~mac_stream() override {
            mac_stream::disconnect();
        }

    private:
        virtual void connect(const std::string_view ip, const std::string_view port) override {
            if (socket > invalid_socket)
                throw std::runtime_error("Stream had already been connected");

            int port_from_chars;
            from_chars_throws(port.data(), port.data() + port.size(), port_from_chars);

            sockaddr_in sa{ };
            sa.sin_family = AF_INET;
            sa.sin_port = htons(port_from_chars);
            sa.sin_addr.s_addr = inet_addr(ip.data());

            socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

            if (socket > invalid_socket) {
                int error = ::connect(socket, (struct sockaddr*)(&sa), sizeof(sa));
                if (error != 0) {
                    //shutdown(listen_socket, SHUT_RDWR);
                    close(socket);
                    socket = invalid_socket;
                }
            }

            if (socket == invalid_socket) {
                // todo:: add addr to the exception, add log
                throw std::runtime_error("TCP error: Could not connect socket");
            }
        }

        virtual void disconnect() override {
            //shutdown(listen_socket, SHUT_RDWR);
            close(socket);
            socket = invalid_socket;
        }

        virtual void write(const void* data, std::size_t length) override {
            // todo add addr to the exception, add log
            const auto sent = send(socket, (const char*)data, (int)length, 0);
            if (sent == -1) {
                // TODO use errno
                throw std::runtime_error("TCP error: Failed to send data");
            }

            assert((std::size_t)sent == length);
        }

        virtual void read(void* data, std::size_t length) override {
            auto* buffer = (char*)data;
            while (length != 0) {
                const auto received = recv(socket, buffer, (int)length, 0);
                if (received < 0) {
                    // TODO use errno
                    throw std::runtime_error("TCP error: Failed to receive data");
                }
                length -= received;
                buffer += received;
            }
        }

        int socket{ invalid_socket };
    };

}

#pragma clang diagnostic pop

namespace icarus::io {

    stream* create_mac_stream(int socket) {
        return new mac_stream(socket);
    }

}
