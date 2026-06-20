/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include <functional>
#include <string>
#include <cassert>
#include <utility>
#include <array>
#include <cstring>
#include <type_traits>

#include "hope-io/net/stream.h"

namespace hope::io {

    class event_loop {
    public:

        // Ring buffer with unbounded head/tail counters (no modulo on hot path).
        // buffer_size is a power of 2, so masking (& mask) replaces modulo (% size).
        // Layout: [ ... used ... | ... free ... ]  or  [ ... free ... used ... ]
        struct fixed_size_buffer final {
            constexpr static auto buffer_size = 512 * 1024; // 512 KB = 2^19
            constexpr static auto buffer_mask = buffer_size - 1; // for &-based wrapping
            using buffer_impl = std::array<unsigned char, buffer_size>;

            std::size_t write(const void* data, std::size_t size) noexcept {
                auto available = free_space();
                if (size > available) size = available;

                auto t = m_tail & buffer_mask;
                auto first_part = std::min(size, buffer_size - t);
                std::copy((const unsigned char*)data,
                          (const unsigned char*)data + first_part,
                          m_impl.data() + t);
                if (first_part < size) {
                    std::copy((const unsigned char*)data + first_part,
                              (const unsigned char*)data + size,
                              m_impl.data());
                }
                m_tail += size;
                return size;
            }

            std::size_t read(void* data, std::size_t size) noexcept {
                auto available = count();
                if (size > available) size = available;

                auto h = m_head & buffer_mask;
                auto first_part = std::min(size, buffer_size - h);
                std::copy(m_impl.data() + h,
                          m_impl.data() + h + first_part,
                          (unsigned char*)data);
                if (first_part < size) {
                    std::copy(m_impl.data(),
                              m_impl.data() + (size - first_part),
                              (unsigned char*)data + first_part);
                }
                m_head += size;
                return size;
            }

            // Calls fn with each contiguous free region and advances tail by the return value.
            // The lambda receives (void* data, size_t capacity) and must return the number
            // of bytes written. Returning less than capacity stops iteration.
            // Returns total bytes written across all regions.
            template<typename F>
            std::size_t consume_free(F&& fn) noexcept {
                if (m_tail - m_head >= buffer_size) return 0; // full

                auto h = m_head & buffer_mask;
                auto t = m_tail & buffer_mask;

                // First contiguous free region: starts at physical tail
                auto chunk_size = (h > t) ? (h - t) : (buffer_size - t);
                std::size_t total = 0;
                if (chunk_size > 0) {
                    auto consumed = fn(m_impl.data() + t, chunk_size);
                    m_tail += consumed;
                    total += consumed;
                    if (consumed < chunk_size) return total;
                }

                // Second region: only if tail wrapped past buffer end and head > 0
                if (h > 0 && (m_tail & buffer_mask) == 0) {
                    auto consumed = fn(m_impl.data(), h);
                    m_tail += consumed;
                    total += consumed;
                }

                return total;
            }

            // Calls fn with each contiguous used region and advances head by the return value.
            // The lambda receives (const void* data, size_t length) and must return the number
            // of bytes consumed. Returning less than length stops iteration.
            // Returns total bytes consumed across all regions.
            template<typename F>
            std::size_t consume_used(F&& fn) noexcept {
                if (m_tail == m_head) return 0; // empty

                auto h = m_head & buffer_mask;
                auto t = m_tail & buffer_mask;

                // First contiguous used region: starts at physical head
                auto chunk_size = (h <= t) ? (t - h) : (buffer_size - h);
                std::size_t total = 0;
                if (chunk_size > 0) {
                    auto consumed = fn(static_cast<const void*>(m_impl.data() + h), chunk_size);
                    m_head += consumed;
                    total += consumed;
                    if (consumed < chunk_size) return total;
                }

                // Second region: only if head wrapped past buffer end and tail > 0
                if (t > 0 && (m_head & buffer_mask) == 0) {
                    auto consumed = fn(static_cast<const void*>(m_impl.data()), t);
                    m_head += consumed;
                    total += consumed;
                }

                return total;
            }

            // Returns the first contiguous used region without advancing head.
            // Useful for protocol parsers that peek at data before consuming.
            std::pair<const void*, std::size_t> peek_used() const noexcept {
                if (m_tail == m_head) return { nullptr, 0 };
                auto h = m_head & buffer_mask;
                auto t = m_tail & buffer_mask;
                if (h <= t) return { m_impl.data() + h, t - h };
                return { m_impl.data() + h, buffer_size - h };
            }

            void reset() noexcept {
                m_head = 0;
                m_tail = 0;
            }

            bool is_empty() const noexcept {
                return m_head == m_tail;
            }

            std::size_t count() const noexcept {
                auto c = m_tail - m_head;
                return c > buffer_size ? buffer_size : c;
            }

            std::size_t free_space() const noexcept {
                return buffer_size - count();
            }

            const buffer_impl& get_buffer() const noexcept { return m_impl; }

        private:
            buffer_impl m_impl;
            // Unbounded counters — grow monotonically, masked on access (& buffer_mask).
            std::size_t m_tail = 0;
            std::size_t m_head = 0;
        };

        enum class connection_state : int8_t {
            idle,
            read,
            write,
            die,
        };

        struct connection final {
            static std::function<void(const connection& conn)> on_state_changed;
            connection() = default;
            connection(int32_t in_descriptor) {
                descriptor = in_descriptor;
            }
            fixed_size_buffer* buffer = nullptr;
            int32_t descriptor = -1;

            auto get_state() const noexcept { return state; }
            void set_state(connection_state new_state) noexcept {
                assert(new_state != state);
                // check if switch from read or write buffer has to be empty
                state = new_state;
                if (on_state_changed) {
                    on_state_changed(*this);
                }
            }
            bool operator==(const connection& rhs) const {
                return descriptor == rhs.descriptor;
            }
            bool operator!=(const connection& rhs) const {
                return descriptor != rhs.descriptor;
            }
            struct hash final {
                std::size_t operator()(const connection& obj) const {
                    return std::hash<int32_t>()(obj.descriptor);
                }
            };
        private:
            connection_state state = connection_state::idle;
        };

        struct callbacks final {
            using on_connect_t = std::function<void(connection&)>;
            on_connect_t on_connect = [] (connection&){ assert(false); };

            // fired when done with reading, buffer contains received portion of bytes
            using on_read_t = std::function<void(connection&)>;
            on_read_t on_read = [] (connection&){ assert(false); };

            // fired when done with writing (full)
            using on_write_t = std::function<void(connection&)>;
            on_write_t on_write = [] (connection&){ assert(false); };

            // if any error, string contains "reason"
            using on_error_t = std::function<void(connection&, const std::string&)>;
            on_error_t on_err = [] (connection&, const std::string&){ assert(false); };
        };

        struct config final {
            std::size_t max_mutual_connections = 1024;
            std::size_t max_accepts_per_tick = 128;
            std::size_t port = 9393;
            int epoll_temeout = 1000;
            class acceptor* custom_acceptor = nullptr;  // If provided, this acceptor will be used instead of creating a default one
            stream_options accepted_stream_options;     // Socket options applied to each accepted connection
        };

        virtual ~event_loop() = default;
        // TODO:: do we need to split acceptor, stream and loop options?
        virtual void run(const config& cfg, callbacks&& cb) = 0;
        virtual void stop() = 0;
    };

}
