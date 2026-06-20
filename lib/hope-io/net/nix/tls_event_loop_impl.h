/* Copyright (C) 2026 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/tls_event_loop.h"
#include "hope-io/net/event_loop.h"

#if PLATFORM_APPLE

#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <atomic>
#include <sys/event.h>

#include "openssl/ssl.h"

namespace hope::io {

    class tls_event_loop_impl final : public tls_event_loop {
    public:
        tls_event_loop_impl();
        ~tls_event_loop_impl() override;

        void run(const tls_config& cfg, event_loop::callbacks&& cb) override;
        void stop() override;

    private:
        struct tls_per_conn {
            SSL* ssl = nullptr;
        };

        struct buffer_pool final {
            event_loop::fixed_size_buffer* allocate();
            void redeem(event_loop::fixed_size_buffer* b);
            void prepool(std::size_t count);
        private:
            std::deque<event_loop::fixed_size_buffer*> m_impl;
        };

        void handle_accept(event_loop::callbacks& cb);
        void retry_handshake(int32_t fd, event_loop::callbacks& cb);
        void register_connection(int32_t sock, SSL* ssl, event_loop::callbacks& cb);
        void handle_read(event_loop::connection& conn, event_loop::callbacks& cb);
        void handle_write(event_loop::connection& conn, event_loop::callbacks& cb);
        void remove_connection(int32_t fd);
        void throw_bind_err();

        // --- State ---
        int32_t m_listen_socket = -1;
        int m_kq = -1;
        SSL_CTX* m_ctx = nullptr;

        tls_config m_cfg;
        std::vector<struct kevent> m_events;
        std::unordered_set<event_loop::connection, event_loop::connection::hash> m_connections;
        std::unordered_map<int32_t, tls_per_conn> m_tls_states;
        std::unordered_set<int32_t> m_pending_handshakes;
        buffer_pool m_pl;
        std::atomic<bool> m_running = true;
    };

}

#endif
