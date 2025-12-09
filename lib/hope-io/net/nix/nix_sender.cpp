/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#if PLATFORM_LINUX || PLATFORM_APPLE

#include "hope-io/net/udp_sender.h"
#include "hope-io/net/factory.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

namespace {

    class nix_udp_sender final : public hope::io::udp_sender {
    public:
        explicit nix_udp_sender(unsigned long long in_socket) {
            if (in_socket != 0)
                m_socket = in_socket;
        }

        virtual ~nix_udp_sender() override {
            disconnect();
        }

    private:
        [[nodiscard]] int32_t platform_socket() const override {
            return (int32_t)m_socket;
        }

        virtual void connect(const std::string_view ip, std::size_t port) override {
            struct hostent *host;
            if ((host = gethostbyname(ip.data())) == nullptr) {
                throw std::runtime_error("hope-io/nix_udp_sender: cannot resolve ip: " +
                                         std::string(strerror(errno)));
            }

            if (!m_socket && (m_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
                throw std::runtime_error("hope-io/nix_udp_sender: cannot create socket: " +
                                         std::string(strerror(errno)));
            }

            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(port);
            serv_addr.sin_addr = *((struct in_addr *)host->h_addr);
            bzero(&(serv_addr.sin_zero), 8);
        }

        virtual void disconnect() override {
            if (m_socket != 0) {
                close(m_socket);
                m_socket = 0;
            }
        }

        virtual void write(const void* data, std::size_t length) override {
            auto bytes_sent = sendto(m_socket, (char*)data, length, 0,
                                     (struct sockaddr *)&serv_addr, sizeof(serv_addr));
            if (bytes_sent == -1) {
                throw std::runtime_error("hope-io/nix_udp_sender: failed to write data: " +
                                         std::string(strerror(errno)));
            }
        }
        
        int m_socket{ 0 };

        struct sockaddr_in serv_addr{};
    };

}

namespace hope::io {

    udp_sender* create_udp_sender(unsigned long long socket) {
        return new nix_udp_sender(socket);
    }

}

#endif
