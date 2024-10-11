/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "hope-io/coredefs.h"

#ifdef ICARUS_NIX

#include "hope-io/net/stream.h"
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

    // TODO:: remove inheritance
    class nix_receiver final : public hope::io::stream {
    public:
        explicit nix_receiver(unsigned long long in_socket) {
            if (in_socket != 0)
                m_socket = in_socket;
        }

        virtual ~nix_receiver() override {
            disconnect();
        }

    private:
        virtual std::string get_endpoint() const override {
            struct sockaddr_in remote_sin{};
            socklen_t remote_sinlen = sizeof(remote_sin);
            getpeername(m_socket, (struct sockaddr*)&remote_sin, &remote_sinlen);
            char *peeraddrpresn = inet_ntoa(remote_sin.sin_addr);
            return peeraddrpresn;
        }

        [[nodiscard]] int32_t platform_socket() const override {
            return (int32_t)m_socket;
        }

        virtual void connect(const std::string_view ip, std::size_t port) override {
            struct hostent *host;
            if ((host = gethostbyname(ip.data())) == nullptr) {
                throw std::runtime_error("hope-io/nix_receiver: cannot resolve ip: " +
                                         std::string(strerror(errno)));
            }

            if (!m_socket && (m_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
                throw std::runtime_error("hope-io/nix_receiver: cannot create socket: " +
                                         std::string(strerror(errno)));
            }

            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(port);
            serv_addr.sin_addr = *((struct in_addr *)host->h_addr);
            bzero(&(serv_addr.sin_zero), 8);
        }

        virtual void disconnect() override {
            close(m_socket);
            m_socket = 0;
        }

        virtual void write(const void* data, std::size_t length) override {
            assert(false);
        }

        virtual size_t read(void* data, std::size_t length) override {
            socklen_t len;
            auto recv_bytes = recvfrom(m_socket, (char*)data, length, 0,
                                       (struct sockaddr *)&serv_addr, &len);
            if (recv_bytes == -1) {
                throw std::runtime_error("hope-io/nix_receiver: failed to read data: " +
                                         std::string(strerror(errno)));
            }

            return recv_bytes;
        }

        virtual size_t read_once(void* data, std::size_t length) override {
            assert(false && "Not implemented");
        }

        virtual void stream_in(std::string& buffer) override {
            assert(false && "Not implemented");
        }

        int m_socket{ 0 };

        struct sockaddr_in serv_addr{};
    };

}

namespace hope::io {

    stream* create_receiver(unsigned long long socket) {
        return new nix_receiver(socket);
    }

}

#endif
