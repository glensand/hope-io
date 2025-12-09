/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/stream.h"
#include "hope-io/coredefs.h"
#include <coroutine>
#include <exception>
#include <utility>
#include <stdexcept>

#if PLATFORM_LINUX || PLATFORM_APPLE
#include <sys/select.h>
#include <poll.h>
#endif

namespace hope::io {

    // Platform-agnostic I/O readiness check
    class io_readiness {
    public:
        enum class mode {
            read,
            write
        };

        // Check if stream is ready for I/O without blocking
        static bool check_ready(int32_t socket, mode m, int timeout_ms = 0) noexcept {
#if PLATFORM_LINUX || PLATFORM_APPLE
            struct pollfd pfd = {socket, m == mode::read ? POLLIN : POLLOUT, 0};
            int result = poll(&pfd, 1, timeout_ms);
            return result > 0 && (pfd.revents & (m == mode::read ? POLLIN : POLLOUT));
#elif PLATFORM_WINDOWS
            // Windows: use select with timeval
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(socket, &fds);
            
            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            
            int result;
            if (m == mode::read) {
                result = select((int)socket + 1, &fds, nullptr, nullptr, &tv);
            } else {
                result = select((int)socket + 1, nullptr, &fds, nullptr, &tv);
            }
            return result > 0;
#else
            return false;
#endif
        }
    };

    // Awaitable for async read operations with I/O readiness checking
    class async_read_awaitable {
    public:
        async_read_awaitable(stream* str, void* buffer, std::size_t length, int timeout_ms = -1)
            : m_stream(str), m_buffer(buffer), m_length(length), 
              m_timeout_ms(timeout_ms), m_bytes_read(0) {}

        bool await_ready() const noexcept {
            // Check if data is already available without blocking
            int socket = m_stream->platform_socket();
            return io_readiness::check_ready(socket, io_readiness::mode::read, 0);
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            try {
                int socket = m_stream->platform_socket();
                
                // Wait until data is ready or timeout
                if (!io_readiness::check_ready(socket, io_readiness::mode::read, m_timeout_ms)) {
                    m_exception = std::make_exception_ptr(
                        std::runtime_error("hope-io/async_read: timeout waiting for data"));
                    return;
                }

                // Data is ready, perform the read
                m_bytes_read = m_stream->read_once(m_buffer, m_length);
            } catch (...) {
                m_exception = std::current_exception();
            }
        }

        [[nodiscard]] std::size_t await_resume() {
            if (m_exception) {
                std::rethrow_exception(m_exception);
            }
            return m_bytes_read;
        }

    private:
        stream* m_stream;
        void* m_buffer;
        std::size_t m_length;
        int m_timeout_ms;
        std::size_t m_bytes_read;
        std::exception_ptr m_exception;
    };

    // Awaitable for async write operations with I/O readiness checking
    class async_write_awaitable {
    public:
        async_write_awaitable(stream* str, const void* data, std::size_t length, int timeout_ms = -1)
            : m_stream(str), m_data(data), m_length(length), m_timeout_ms(timeout_ms) {}

        bool await_ready() const noexcept {
            // Check if socket is ready for writing
            int socket = m_stream->platform_socket();
            return io_readiness::check_ready(socket, io_readiness::mode::write, 0);
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            try {
                int socket = m_stream->platform_socket();
                
                // Wait until socket is writable or timeout
                if (!io_readiness::check_ready(socket, io_readiness::mode::write, m_timeout_ms)) {
                    m_exception = std::make_exception_ptr(
                        std::runtime_error("hope-io/async_write: timeout waiting to write"));
                    return;
                }

                // Socket is ready, perform the write
                m_stream->write(m_data, m_length);
            } catch (...) {
                m_exception = std::current_exception();
            }
        }

        void await_resume() {
            if (m_exception) {
                std::rethrow_exception(m_exception);
            }
        }

    private:
        stream* m_stream;
        const void* m_data;
        std::size_t m_length;
        int m_timeout_ms;
        std::exception_ptr m_exception;
    };

    // Awaitable for async connect operations
    class async_connect_awaitable {
    public:
        async_connect_awaitable(stream* str, std::string_view ip, std::size_t port, int timeout_ms = 3000)
            : m_stream(str), m_ip(ip), m_port(port), m_timeout_ms(timeout_ms) {}

        bool await_ready() const noexcept {
            return false;  // Connect always needs to suspend
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            try {
                m_stream->connect(m_ip, m_port);
            } catch (...) {
                m_exception = std::current_exception();
            }
        }

        void await_resume() {
            if (m_exception) {
                std::rethrow_exception(m_exception);
            }
        }

    private:
        stream* m_stream;
        std::string_view m_ip;
        std::size_t m_port;
        int m_timeout_ms;
        std::exception_ptr m_exception;
    };

    // Helper result type for coroutines
    template<typename T = void>
    class coro_task {
    public:
        struct promise_type {
            T value;
            std::exception_ptr exception;

            coro_task get_return_object() {
                return coro_task{std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_never initial_suspend() { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }

            void unhandled_exception() {
                exception = std::current_exception();
            }

            template<typename U>
            requires std::is_convertible_v<U, T>
            void return_value(U&& u) {
                value = std::forward<U>(u);
            }
        };

        coro_task() = default;

        coro_task(std::coroutine_handle<promise_type> h) : m_handle(h) {}

        ~coro_task() {
            if (m_handle) {
                m_handle.destroy();
            }
        }

        coro_task(const coro_task&) = delete;
        coro_task& operator=(const coro_task&) = delete;

        coro_task(coro_task&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
        coro_task& operator=(coro_task&& other) noexcept {
            if (this != &other) {
                if (m_handle) {
                    m_handle.destroy();
                }
                m_handle = std::exchange(other.m_handle, {});
            }
            return *this;
        }

        [[nodiscard]] T get() {
            if (m_handle && !m_handle.done()) {
                m_handle.resume();
            }
            if (m_handle) {
                if (m_handle.promise().exception) {
                    std::rethrow_exception(m_handle.promise().exception);
                }
                return m_handle.promise().value;
            }
            return T{};
        }

        [[nodiscard]] bool done() const noexcept {
            return !m_handle || m_handle.done();
        }

        void resume() {
            if (m_handle && !m_handle.done()) {
                m_handle.resume();
            }
        }

    private:
        std::coroutine_handle<promise_type> m_handle;
    };

    // Specialization for void return type
    template<>
    class coro_task<void> {
    public:
        struct promise_type {
            std::exception_ptr exception;

            coro_task get_return_object() {
                return coro_task{std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_never initial_suspend() { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }

            void unhandled_exception() {
                exception = std::current_exception();
            }

            void return_void() {}
        };

        coro_task() = default;

        coro_task(std::coroutine_handle<promise_type> h) : m_handle(h) {}

        ~coro_task() {
            if (m_handle) {
                m_handle.destroy();
            }
        }

        coro_task(const coro_task&) = delete;
        coro_task& operator=(const coro_task&) = delete;

        coro_task(coro_task&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
        coro_task& operator=(coro_task&& other) noexcept {
            if (this != &other) {
                if (m_handle) {
                    m_handle.destroy();
                }
                m_handle = std::exchange(other.m_handle, {});
            }
            return *this;
        }

        void get() {
            if (m_handle && !m_handle.done()) {
                m_handle.resume();
            }
            if (m_handle && m_handle.promise().exception) {
                std::rethrow_exception(m_handle.promise().exception);
            }
        }

        [[nodiscard]] bool done() const noexcept {
            return !m_handle || m_handle.done();
        }

        void resume() {
            if (m_handle && !m_handle.done()) {
                m_handle.resume();
            }
        }

    private:
        std::coroutine_handle<promise_type> m_handle;
    };

}  // namespace hope::io
