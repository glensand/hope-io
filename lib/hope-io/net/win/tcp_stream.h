/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/stream.h"

#if PLATFORM_WINDOWS

namespace hope::io {

    class tcp_stream final : public stream {
    public:
        explicit tcp_stream(unsigned long long in_socket = static_cast<unsigned long long>(-1),
                            const stream_options& opts = stream_options{});
        virtual ~tcp_stream() override;

        [[nodiscard]] std::string get_endpoint() const override;
        [[nodiscard]] int32_t platform_socket() const override;

        void set_options(const stream_options&) override;

        void connect(std::string_view ip, std::size_t port) override;
        void disconnect() override;

        void write(const void *data, std::size_t length) override;
        void write_v(std::span<const std::span<const char>> buffers) override;

        size_t read(void *data, std::size_t length) override;
        size_t read_once(void* data, std::size_t length) override;

        void stream_in(std::string& buffer) override;

        using stream::write;
        using stream::read;

    private:
        void apply_constructor_options();

        unsigned long long m_socket{ 0 };
        stream_options m_options;
    };

}

#endif
