/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "hope-io/coredefs.h"

#ifdef ICARUS_NIX

#include <stdexcept>

#include "hope-io/net/stream.h"
#include "hope-io/net/init.h"
#include "hope-io/net/factory.h"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

namespace tcp {

    class nix_stream final : public hope::io::stream {
    public:
        explicit nix_stream(unsigned long long in_socket) {
            if (in_socket != 0)
                m_socket = in_socket;
        }

        virtual ~nix_stream() override {
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
                throw std::runtime_error("hope-io/nix_stream [tcp]: cannot resolve ip: " +
                                         std::string(strerror(errno)));
	        }

            if ((m_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
                throw std::runtime_error("hope-io/nix_stream [tcp]: cannot create socket: " +
                                         std::string(strerror(errno)));
	        }

            struct sockaddr_in serv_addr{};
            serv_addr.sin_family = AF_INET;
	        serv_addr.sin_port = htons(port);
	        serv_addr.sin_addr = *((struct in_addr *)host->h_addr);
	        bzero(&(serv_addr.sin_zero), 8);

	        if (::connect(m_socket, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1) {
                throw std::runtime_error("hope-io/nix_stream [tcp]: cannot connect to host: " +
                                         std::string(strerror(errno)));
	        }
        }

        virtual void disconnect() override {
            close(m_socket);
            m_socket = 0;
        }

        virtual void write(const void* data, std::size_t length) override {
            auto bytes_sent = 0;
            while (bytes_sent != length) {
                bytes_sent += send(m_socket, (char*)data + bytes_sent, length - bytes_sent, 0);
            }
        }

        virtual void read(void* data, std::size_t length) override {
            auto recv_bytes = 0;
            while (recv_bytes != length) {
                recv_bytes += recv(m_socket, (char*)data + recv_bytes, length - recv_bytes, 0);
            }
        }

        int m_socket{ 0 };
    };

}

namespace udp {

    class nix_stream final : public hope::io::stream {
    public:
        explicit nix_stream(unsigned long long in_socket) {
            if (in_socket != 0)
                m_socket = in_socket;
        }

        virtual ~nix_stream() override {
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
                throw std::runtime_error("hope-io/nix_stream [udp]: cannot resolve ip: " +
                                         std::string(strerror(errno)));
            }

            if ((m_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
                throw std::runtime_error("hope-io/nix_stream [udp]: cannot create socket: " +
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
            auto bytes_sent = sendto(m_socket, (char*)data, length, 0,
                                 (struct sockaddr *)&serv_addr, sizeof(serv_addr));
            if (bytes_sent == -1) {
                throw std::runtime_error("hope-io/nix_stream [udp]: failed to write data: " +
                                         std::string(strerror(errno)));
            }
        }

        virtual void read(void* data, std::size_t length) override {
            socklen_t len;
            auto recv_bytes = recvfrom(m_socket, (char*)data, length, 0,
                                   (struct sockaddr *)&serv_addr, &len);
            if (recv_bytes == -1) {
                throw std::runtime_error("hope-io/nix_stream [udp]: failed to read data: " +
                                         std::string(strerror(errno)));
            }
        }

        int m_socket{ 0 };

        struct sockaddr_in serv_addr{};
    };

}

namespace hope::io {

    stream* create_tcp_stream(unsigned long long socket) {
        return new tcp::nix_stream(socket);
    }

    stream* create_udp_stream(unsigned long long socket) {
        return new udp::nix_stream(socket);
    }

}

#endif
