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
#include <fcntl.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>

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
            std::size_t bytes_sent = 0;
            while (bytes_sent != length) {
                auto op_res = send(m_socket, (char*)data + bytes_sent, length - bytes_sent, 0);
                if (op_res == -1){
                    throw std::runtime_error("hope-io/nix_stream [tcp]: cannot write to stream: " +
                                         std::string(strerror(errno)));
                }
                bytes_sent += op_res;
            }
        }

        virtual size_t read(void* data, std::size_t length) override {
            std::size_t recv_bytes = 0;
            while (recv_bytes != length) {
                auto op_res = recv(m_socket, (char*)data + recv_bytes, length - recv_bytes, 0);
                if (op_res == -1) {
                    throw std::runtime_error("hope-io/nix_stream [tcp]: cannot read from stream: " +
                                         std::string(strerror(errno)));
                }
                recv_bytes += recv_bytes;
            }
            return recv_bytes;
        }

        virtual size_t read_once(void* data, std::size_t length) override {
            return recv(m_socket, (char*)data, length - 1, 0);
        }

        virtual void stream_in(std::string& buffer) override {
            assert(false && "Not implemented");
        }

        virtual void set_options(const hope::io::stream_options& opt) override {
            assert(m_socket != 0);
            struct timeval timeout;
            timeout.tv_sec = opt.write_timeout / 1000;
            timeout.tv_usec = 1000 * (opt.write_timeout - timeout.tv_sec * 1000);
            if (setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
                throw std::runtime_error("hope-io/nix_stream [tcp]: cannot set write timeout: " +
                                         std::string(strerror(errno)));
            }

            timeout.tv_sec = opt.read_timeout / 1000;
            timeout.tv_usec = 1000 * (opt.read_timeout - timeout.tv_sec * 1000);
            if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                throw std::runtime_error("hope-io/nix_stream [tcp]: cannot set read timeout: " +
                                         std::string(strerror(errno)));
            }
            
            int flags = fcntl(m_socket, F_GETFL, 0);
            if (flags == -1) {
                throw std::runtime_error("hope-io/nix_stream [tcp]: cannot get socket flags: " +
                                         std::string(strerror(errno)));
            }

            flags = opt.non_block_mode ? flags | O_NONBLOCK : flags & (~O_NONBLOCK);
            if (fcntl(m_socket, F_SETFL, flags) == -1) {
                throw std::runtime_error("hope-io/nix_stream [tcp]: cannot set non-block flag: " +
                                         std::string(strerror(errno)));
            }
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
