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
#include "hope-io/net/factory.h"

#include <cassert>
#include <unistd.h>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

namespace {

    class nix_stream final : public hope::io::stream {
    public:
        explicit nix_stream(unsigned long long in_socket) {
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
            struct hostent* host;
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

        virtual size_t read(void* data, std::size_t length) override {
            auto recv_bytes = 0;
            while (recv_bytes != length) {
                recv_bytes += recv(m_socket, (char*)data + recv_bytes, length - recv_bytes, 0);
            }
            return recv_bytes;
        }

        virtual size_t read_once(void* data, std::size_t length) override {
            return recv(m_socket, (char*)data, length - 1, 0);
        }

        virtual void stream_in(std::string& buffer) override {
            assert(false && "Not implemented");
        }

        int m_socket{ 0 };
    };

}

namespace hope::io {

    stream* create_stream(unsigned long long socket) {
        return new nix_stream(socket);
    }

}

#endif
