/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "hope-io/coredefs.h"

#ifdef ICARUS_NIX

#include "hope-io/net/udp_builder.h"
#include "hope-io/net/factory.h"

#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>

namespace {

    class nix_udp_builder final : public hope::io::udp_builder {
    public:
        nix_udp_builder() = default;

    private:
        [[nodiscard]] int32_t platform_socket() const override {
            return (int32_t)m_socket;
        }

        virtual void init(std::size_t port) override {
            if ((m_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
                throw std::runtime_error("hope-io/nix_udp_builder: cannot create socket");
            }
            int no_delay = 1;
            setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &no_delay, sizeof(no_delay));

            struct sockaddr_in server_sockaddr{};
            server_sockaddr.sin_family = AF_INET;
            server_sockaddr.sin_port = htons(port);
            server_sockaddr.sin_addr.s_addr = INADDR_ANY;
            memset(&(server_sockaddr.sin_zero),0,8);

            if ((bind(m_socket, (struct sockaddr *)&server_sockaddr, sizeof(struct sockaddr))) == -1) {
                throw std::runtime_error("hope-io/nix_udp_builder: cannot bind socket");
            }
        }

        int m_socket{ -1 };
    };

}

namespace hope::io {

    udp_builder* create_udp_builder() {
        return new nix_udp_builder;
    }

}

#endif
