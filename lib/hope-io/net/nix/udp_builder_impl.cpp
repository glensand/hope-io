/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#if PLATFORM_LINUX || PLATFORM_APPLE

#include "hope-io/net/nix/udp_builder_impl.h"
#include "hope-io/net/factory.h"

#include <exception>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>

namespace hope::io {

    int32_t udp_builder_impl::platform_socket() const {
        return (int32_t)m_socket;
    }

    void udp_builder_impl::init(std::size_t port) {
        if ((m_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
            throw std::runtime_error("hope-io/udp_builder_impl: cannot create socket");
        }
        int no_delay = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &no_delay, sizeof(no_delay));

        struct sockaddr_in server_sockaddr{};
        server_sockaddr.sin_family = AF_INET;
        server_sockaddr.sin_port = htons(port);
        server_sockaddr.sin_addr.s_addr = INADDR_ANY;
        memset(&(server_sockaddr.sin_zero), 0, 8);

        if ((bind(m_socket, (struct sockaddr *)&server_sockaddr, sizeof(struct sockaddr))) == -1) {
            throw std::runtime_error("hope-io/udp_builder_impl: cannot bind socket");
        }
    }



}
#endif
