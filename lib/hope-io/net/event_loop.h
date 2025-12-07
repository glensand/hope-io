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

namespace hope::io {

    class event_loop {    
    public:

        // fixed size buffer, intended to be used for communication with application
        // it may looks like that:
        // | unsued spacer | payload | slack |                  
        struct fixed_size_buffer final {
            constexpr static auto buffer_size = 512 * 1024; // 512 KB
            using buffer_impl = std::array<unsigned char, buffer_size>;
            // tries to write specified amount of data to the buffer
            // returns count actually written
            std::size_t write(const void* data, std::size_t size) noexcept {
                const auto free_space = m_impl.size() - m_tail;
                if (size > free_space) {
                    size = free_space;
                }
                auto* begin = m_impl.data() + m_tail;
                std::copy((unsigned char*)data , (unsigned char*)data + size, begin);
                m_tail += size;
                return size;
            }
            
            std::size_t read(void* data, std::size_t size) noexcept {
                if (count() < size) {
                    size = count();
                }
                auto* begin = m_impl.data() + m_head;
                std::copy(begin, begin + size, (unsigned char*)data);
                m_head += size;
                return size;
            }

            std::pair<const void*, std::size_t> free_chunk() const noexcept {
                return { m_impl.data() + m_tail, m_impl.size() - m_tail };
            }

            std::pair<const void*, std::size_t> used_chunk() const noexcept {
                return { m_impl.data() + m_head, count() };
            }

            void shrink() noexcept {
                if (m_head > 0 && m_tail > 0) {
                    // todo:: memmove for overlapping regions?
                    for (std::size_t i = 0; i < count(); ++i) {
                        m_impl[i] = m_impl[i  + m_head];
                    }
                    m_tail -= m_head;
                    m_head = 0;
                }
            }

            void reset() noexcept {
                m_head = 0;
                m_tail = 0;
            }

            bool is_empty() const noexcept {
                return count() == 0;
            }

            void handle_write(std::size_t bytes) noexcept {
                m_tail += bytes;
            }

            void handle_read(std::size_t bytes) noexcept {
                m_head += bytes;
            }

            std::size_t count() const noexcept { return m_tail - m_head; }
            std::size_t free_space() const noexcept { return m_impl.size() - m_tail; }
            const buffer_impl& get_buffer() const noexcept { return m_impl; }

        private:
            buffer_impl m_impl;
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
        };

        virtual ~event_loop() = default;
        // TODO:: do we need to split acceptor, stream and loop options?
        virtual void run(const config& cfg, callbacks&& cb) = 0;
        virtual void stop() = 0;
    };

}
