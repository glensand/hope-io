/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/stream_coro.h"
#include "hope-io/net/stream.h"

namespace hope::io {

    // Convenient wrapper for using coroutines with streams
    class async_stream {
    public:
        explicit async_stream(stream* str) : m_stream(str) {
            if (!str) {
                throw std::runtime_error("hope-io/async_stream: stream pointer is null");
            }
            // Enable non-blocking mode for async operations
            stream_options opts;
            opts.non_block_mode = true;
            m_stream->set_options(opts);
        }

        // Asynchronous read operation with I/O readiness checking
        [[nodiscard]] async_read_awaitable async_read(void* buffer, std::size_t length, int timeout_ms = -1) {
            return async_read_awaitable(m_stream, buffer, length, timeout_ms);
        }

        // Asynchronous write operation with I/O readiness checking
        [[nodiscard]] async_write_awaitable async_write(const void* data, std::size_t length, int timeout_ms = -1) {
            return async_write_awaitable(m_stream, data, length, timeout_ms);
        }

        // Asynchronous connect operation
        [[nodiscard]] async_connect_awaitable async_connect(std::string_view ip, std::size_t port, int timeout_ms = 3000) {
            return async_connect_awaitable(m_stream, ip, port, timeout_ms);
        }

        // Synchronous operations (for use in non-coroutine code)
        void connect(std::string_view ip, std::size_t port) {
            m_stream->connect(ip, port);
        }

        void disconnect() {
            m_stream->disconnect();
        }

        [[nodiscard]] std::string get_endpoint() const {
            return m_stream->get_endpoint();
        }

        [[nodiscard]] int32_t platform_socket() const {
            return m_stream->platform_socket();
        }

        void set_options(const stream_options& opt) {
            // Ensure non-blocking mode is enabled
            stream_options opts = opt;
            opts.non_block_mode = true;
            m_stream->set_options(opts);
        }

        // Get underlying stream pointer
        [[nodiscard]] stream* get() const noexcept {
            return m_stream;
        }

    private:
        stream* m_stream;
    };

}  // namespace hope::io
