/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/event_loop.h"

#if PLATFORM_APPLE

#include <deque>
#include <unordered_set>
#include <atomic>
#include <sys/event.h>

namespace hope::io {

    class event_loop_impl final : public event_loop {
    public:
        event_loop_impl();
        ~event_loop_impl() override;

        void run(const config& cfg, callbacks&& cb) override;
        void stop() override;

    private:
        struct buffer_pool final {
            fixed_size_buffer* allocate();
            void redeem(fixed_size_buffer* b);
            void prepool(std::size_t count);
        private:
            std::deque<fixed_size_buffer*> m_impl;
        };

        void do_remove_connection(int32_t fd);
        void handle_accept(callbacks& cb);
        void handle_read(connection& conn, callbacks& cb);
        void handle_write(connection& conn, callbacks& cb);

        std::unordered_set<connection, connection::hash> m_connections;
        int m_kq = -1;
        std::vector<struct kevent> m_events;

        config m_cfg;
        buffer_pool m_pl;
        hope::io::acceptor* m_acceptor = nullptr;
        bool m_owns_acceptor = false;
        std::atomic<bool> m_running = true;
    };

}

#endif
