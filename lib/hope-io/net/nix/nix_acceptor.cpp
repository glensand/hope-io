/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "hope-io/coredefs.h"

#ifdef ICARUS_NIX

#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>

namespace {

    class nix_acceptor final : public hope::io::acceptor {
    public:
        nix_acceptor() = default;

    private:
        virtual hope::io::stream* accept() override {
            int client_socket = -1;
            struct sockaddr_in client_sockaddr;
            unsigned int sin_size = sizeof(struct sockaddr);
            if((client_socket = ::accept(m_socket,(struct sockaddr *)&client_sockaddr, &sin_size)) == -1){
                throw std::runtime_error("hope-io/nix_acceptor: cannot accept connection");
            }
            return hope::io::create_stream((unsigned long long)client_socket);
        }

        virtual void open(std::size_t port) override {
            if ((m_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1){
                throw std::runtime_error("hope-io/nix_acceptor: cannot create socket");
            }
            int nodelay = 1;
	        setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &nodelay, sizeof(nodelay));

            struct sockaddr_in server_sockaddr,client_sockaddr;
	        int sin_size,recvbytes,sendbytes;
	        server_sockaddr.sin_family = AF_INET;
	        server_sockaddr.sin_port = htons(port);
	        server_sockaddr.sin_addr.s_addr = INADDR_ANY;
	        memset(&(server_sockaddr.sin_zero),0,8);

	        if((bind(m_socket,(struct sockaddr *)&server_sockaddr,sizeof(struct sockaddr))) == -1) {
                throw std::runtime_error("hope-io/nix_acceptor: cannot bind socket");
            }

            auto backlog= 10; // what dows it mean
            listen(m_socket, backlog);
        }

        int m_socket{ -1 };
    };

}

namespace hope::io {

    acceptor* create_acceptor() {
        return new nix_acceptor;
    }

}

#endif