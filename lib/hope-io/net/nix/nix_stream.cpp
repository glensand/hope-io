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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace {

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
        [[nodiscard]] int32_t platform_socket() const override {
            return (int32_t)m_socket;
        }

        virtual void connect(const std::string_view ip, std::size_t port) override {
            struct hostent *host;
            if((host = gethostbyname(ip.data())) == nullptr){
                throw std::runtime_error("hope-io/nix_stream: cannot resolve ip");
	        }

            if((m_socket = socket(AF_INET,SOCK_STREAM,0)) == -1){
                throw std::runtime_error("hope-io/nix_stream: cannot create socket");
	        }

            struct sockaddr_in serv_addr;
            serv_addr.sin_family = AF_INET;
	        serv_addr.sin_port = htons(port);
	        serv_addr.sin_addr = *((struct in_addr *)host->h_addr);
	        bzero(&(serv_addr.sin_zero), 8);

	        if(::connect(m_socket, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1){
                throw std::runtime_error("hope-io/nix_stream: cannot connect to host");
	        }
        }

        virtual void disconnect() override {
            close(m_socket);
            m_socket = 0;
        }

        virtual void write(const void* data, std::size_t length) override {
            auto bytes_sent = 0;
            while (bytes_sent != length){
                bytes_sent += send(m_socket, (char*)data + bytes_sent, length - bytes_sent, 0);
            }
        }

        virtual void read(void* data, std::size_t length) override {
            auto recvbytes = 0;
            while (recvbytes != length) {
                recvbytes += recv(m_socket, (char*)data + recvbytes, length - recvbytes, 0);
            }
        }

        std::size_t m_socket{ 0 };
    };

}

namespace hope::io {

    stream* create_stream(unsigned long long socket){
        return new nix_stream(socket);
    }

}

#endif
