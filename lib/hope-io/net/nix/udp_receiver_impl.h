/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/udp_receiver.h"

#if PLATFORM_LINUX || PLATFORM_APPLE

#include <netinet/in.h>

namespace hope::io {

    class udp_receiver_impl final : public udp_receiver {
    public:
        explicit udp_receiver_impl(unsigned long long in_socket = 0);
        virtual ~udp_receiver_impl() override;

        [[nodiscard]] int32_t platform_socket() const override;

        void connect(std::string_view ip, std::size_t port) override;
        void disconnect() override;

        size_t read(void* data, std::size_t length) override;

    private:
        int m_socket{ 0 };
        struct sockaddr_in serv_addr{};
    };

}

#endif
