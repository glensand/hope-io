/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"
#include <string>
#include <sys/errno.h>

#if PLATFORM_LINUX || PLATFORM_APPLE

#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>

namespace {

    class nix_acceptor final : public hope::io::acceptor {
    public:
        nix_acceptor() = default;

    private:
        virtual hope::io::stream* accept() override {
            int client_socket;
            struct sockaddr_in client_sockaddr{};
            unsigned int sin_size = sizeof(struct sockaddr);
            if ((client_socket = ::accept(m_socket, (struct sockaddr *)&client_sockaddr, &sin_size)) == -1) {
                HOPE_THROW_ERRNO("nix_acceptor", "cannot accept connection");
            }
            return hope::io::create_stream((unsigned long long)client_socket);
        }

        virtual void open(std::size_t port) override {
            HOPE_ASSERT(m_socket == -1, "nix_acceptor: open() called on already-open acceptor");
            if ((m_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
                HOPE_THROW_ERRNO("nix_acceptor", "cannot create socket");
            }
            int no_delay = 1;
            if (setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &no_delay, sizeof(no_delay)) == -1) {
                // SO_REUSEADDR failure is rare (kernel config issue) but should not be silent.
                assert(false && "nix_acceptor: setsockopt SO_REUSEADDR failed");
            }

            struct sockaddr_in server_sockaddr{};
	        server_sockaddr.sin_family = AF_INET;
	        server_sockaddr.sin_port = htons(port);
	        server_sockaddr.sin_addr.s_addr = INADDR_ANY;
	        memset(&(server_sockaddr.sin_zero),0,8);

		    if ((bind(m_socket, (struct sockaddr *)&server_sockaddr, sizeof(struct sockaddr))) == -1) {
                HOPE_THROW_ERRNO("nix_acceptor",
                    "cannot bind socket port:" + std::to_string(port));
            }

            auto backlog = 10; // maximum length for the queue of pending connections
            if (listen(m_socket, backlog) == -1) {
                HOPE_THROW_ERRNO("nix_acceptor", "listen failed");
            }

        }

        virtual void close() override {
            ::close(m_socket);
        }

        // NOTE: set_options() must be called after open(). Options are applied
        // immediately to the acceptor socket for subsequent accepts.
        virtual void set_options(const hope::io::stream_options& opt) override {
            HOPE_ASSERT(m_socket != -1, "nix_acceptor: set_options() called before open()");
            int flags = fcntl(m_socket, F_GETFL, 0);
            if (flags == -1) {
                HOPE_THROW_ERRNO("nix_acceptor", "cannot get socket flags");
            }

            flags = opt.non_block_mode ? flags | O_NONBLOCK : flags & (~O_NONBLOCK);
            if (fcntl(m_socket, F_SETFL, flags) == -1) {
                HOPE_THROW_ERRNO("nix_acceptor", "cannot set non-block flag");
            }
        }

        virtual long long raw() const override { return m_socket; }

        int m_socket{ -1 };
    };

}

namespace hope::io {

    acceptor* create_acceptor() {
        return new nix_acceptor;
    }

}

#endif
