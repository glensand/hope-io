/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include <memory>
#include <cassert>
#include <string>
#include <span>
#include <type_traits>

namespace hope::io {

    struct stream_options final {
        // ── Connection / timeout ──────────────────────────────
        uint32_t connection_timeout = 3000;  // msec
        uint32_t read_timeout = 3000;        // msec: SO_RCVTIMEO
        uint32_t write_timeout = 3000;       // msec: SO_SNDTIMEO
        bool non_block_mode = false;         // O_NONBLOCK via fcntl/ioctlsocket

        // ── TCP-level (IPPROTO_TCP) ────────────────────────────
        bool   tcp_nodelay          = true;    // TCP_NODELAY — disable Nagle
        int    tcp_user_timeout     = -1;      // TCP_USER_TIMEOUT msec (Linux, -1=off)

        // ── Keepalive (SOL_SOCKET + TCP_KEEP*) ─────────────────
        bool   keepalive            = false;   // SO_KEEPALIVE
        int    keepidle             = -1;      // TCP_KEEPIDLE sec (-1=sysctl default)
        int    keepintvl            = -1;      // TCP_KEEPINTVL sec
        int    keepcnt              = -1;      // TCP_KEEPCNT probes

        // ── Socket buffer (SOL_SOCKET) ─────────────────────────
        int    send_buffer_size     = -1;      // SO_SNDBUF bytes (-1=leave default)
        int    recv_buffer_size     = -1;      // SO_RCVBUF bytes (-1=leave default)

        // ── Socket behavior (SOL_SOCKET) ───────────────────────
        bool   reuse_address        = true;    // SO_REUSEADDR
        bool   reuse_port           = false;   // SO_REUSEPORT (Linux)
        int    linger_on              = 0;       // SO_LINGER: enable (0=off, 1=on)
        int    linger_seconds         = 0;       // SO_LINGER: timeout in seconds
        int    priority             = -1;      // SO_PRIORITY (-1=leave default)

        // ── IP-level (IPPROTO_IP) ──────────────────────────────
        int    ttl                  = -1;      // IP_TTL (-1=leave default)
        int    tos                  = -1;      // IP_TOS / DSCP field (-1=leave default)
        int    mark                 = -1;      // SO_MARK — socket mark for policy routing
        std::string bind_device;               // SO_BINDTODEVICE — bind to interface name
    };

    // TODO:: need to split streams somehow, add sync/async versions
    // async version should have program side store buffer, and possibility to submit buffer data with single batch
    class stream {
    public:
        virtual ~stream() = default;

        [[nodiscard]] virtual std::string get_endpoint() const = 0;
        [[nodiscard]] virtual int32_t platform_socket() const = 0;

        virtual void set_options(const stream_options&) = 0;

        virtual void connect(std::string_view ip, std::size_t port) = 0;
        virtual void disconnect() = 0;

        virtual void write(const void *data, std::size_t length) = 0;

        /// Write multiple non-contiguous buffers in a single syscall (writev / WSASend).
        /// Falls back to sequential write() on platforms that do not support scatter-gather.
        virtual void write_v(std::span<const std::span<const char>> buffers) = 0;

        virtual size_t read(void *data, std::size_t length) = 0;
        virtual size_t read_once(void* data, std::size_t length) = 0;

        // TODO:: need to be removed in future
        virtual void stream_in(std::string& buffer) = 0;

        template<typename TValue>
        void write(const TValue &val) {
            static_assert(std::is_trivial_v<std::decay_t<TValue>>,
                          "write(const TValue&) is only available for trivial types");
            write(&val, sizeof(val));
        }

        template<typename TValue>
        void read(TValue& val) {
            static_assert(std::is_trivial_v <std::decay_t<TValue>> ,
                          "read() is only available for trivial types");
            read(&val, sizeof(val));
        }

        template<typename TValue>
        TValue read() {
            TValue val;
            read(val);
            return val;
        }
    };

    template <>
    inline void stream::read<std::string>(std::string& val) {
        const auto size = read<uint16_t>();
        if (size > 0) { 
            val.resize(size);
            read(val.data(), size);
        }
    }

    template <>
    inline void stream::write<std::string>(const std::string& val) {
        write((uint16_t)val.size());
        write(val.c_str(), val.size());
    }
}
