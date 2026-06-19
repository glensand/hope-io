/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/event_loop.h"

#if PLATFORM_LINUX

#include <deque>
#include <atomic>
#include <sys/epoll.h>

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
            void drain() {
                for (auto* buf : m_impl) delete buf;
                m_impl.clear();
            }
        private:
            std::deque<fixed_size_buffer*> m_impl;
        };

        void handle_accept(callbacks& cb);
        void handle_read(connection& conn, callbacks& cb);
        void handle_write(connection& conn, callbacks& cb);
        void epoll_ctl_add(int32_t epfd, int32_t fd, uint32_t events, callbacks& cb);
        void remove_connection(int32_t descriptor);
        void push_new_connection(int32_t fd);
        void throw_bind_err();

        std::vector<epoll_event> m_events;
        std::vector<connection> m_connections;

        int32_t m_listen_socket = -1;
        int32_t m_epfd = -1;

        config m_cfg;
        buffer_pool m_pl;
        std::atomic<bool> m_running = true;
    };

}

#endif
