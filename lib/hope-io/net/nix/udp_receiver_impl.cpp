/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#if PLATFORM_LINUX || PLATFORM_APPLE

#include "hope-io/net/nix/udp_receiver_impl.h"
#include "hope-io/net/factory.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <stdexcept>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

namespace hope::io {

    udp_receiver_impl::udp_receiver_impl(unsigned long long in_socket) {
        if (in_socket != 0)
            m_socket = in_socket;
    }

    udp_receiver_impl::~udp_receiver_impl() {
        disconnect();
    }

    int32_t udp_receiver_impl::platform_socket() const {
        return (int32_t)m_socket;
    }

    void udp_receiver_impl::connect(const std::string_view ip, std::size_t port) {
        struct hostent *host;
        if ((host = gethostbyname(ip.data())) == nullptr) {
            throw std::runtime_error("hope-io/udp_receiver_impl: cannot resolve ip: " +
                                     std::string(strerror(errno)));
        }

        if (!m_socket && (m_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
            throw std::runtime_error("hope-io/udp_receiver_impl: cannot create socket: " +
                                     std::string(strerror(errno)));
        }

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        serv_addr.sin_addr = *((struct in_addr *)host->h_addr);
        bzero(&(serv_addr.sin_zero), 8);
    }

    void udp_receiver_impl::disconnect() {
        if (m_socket != 0) {
            close(m_socket);
            m_socket = 0;
        }
    }

    size_t udp_receiver_impl::read(void* data, std::size_t length) {
        socklen_t len = sizeof(serv_addr);
        auto recv_bytes = recvfrom(m_socket, (char*)data, length, 0,
                                   (struct sockaddr *)&serv_addr, &len);
        if (recv_bytes == -1) {
            throw std::runtime_error("hope-io/udp_receiver_impl: failed to read data: " +
                                     std::string(strerror(errno)));
        }

        return recv_bytes;
    }

    udp_receiver* create_udp_receiver(unsigned long long socket) {
        return new udp_receiver_impl(socket);
    }

}
#endif
