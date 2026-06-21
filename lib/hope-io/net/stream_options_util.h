/* Copyright (C) 2026 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 *
 * ── Single definition of apply_stream_options for all event loops ─────
 * Eliminates the duplicated copies that were spread across event loop
 * implementation headers (linux/, nix/, uring/).
 */

#pragma once

#include "hope-io/net/stream.h"
#include "hope-io/coredefs.h"

#if PLATFORM_LINUX || PLATFORM_APPLE
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

namespace hope::io {

inline void apply_stream_options(int fd, const stream_options& opt) {
    if (opt.tcp_nodelay) {
        int on = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    }

#ifdef TCP_USER_TIMEOUT
    if (opt.tcp_user_timeout >= 0) {
        setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &opt.tcp_user_timeout, sizeof(opt.tcp_user_timeout));
    }
#endif

    if (opt.keepalive) {
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
    }

#ifdef TCP_KEEPIDLE
    if (opt.keepidle >= 0) {
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &opt.keepidle, sizeof(opt.keepidle));
    }
#endif

#ifdef TCP_KEEPINTVL
    if (opt.keepintvl >= 0) {
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &opt.keepintvl, sizeof(opt.keepintvl));
    }
#endif

#ifdef TCP_KEEPCNT
    if (opt.keepcnt >= 0) {
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &opt.keepcnt, sizeof(opt.keepcnt));
    }
#endif

    if (opt.send_buffer_size >= 0) {
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &opt.send_buffer_size, sizeof(opt.send_buffer_size));
    }

    if (opt.recv_buffer_size >= 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &opt.recv_buffer_size, sizeof(opt.recv_buffer_size));
    }

    if (opt.linger_on) {
        struct linger l;
        l.l_onoff = opt.linger_on;
        l.l_linger = opt.linger_seconds;
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
    }

#ifdef IP_TTL
    if (opt.ttl >= 0) {
        setsockopt(fd, IPPROTO_IP, IP_TTL, &opt.ttl, sizeof(opt.ttl));
    }
#endif

#ifdef IP_TOS
    if (opt.tos >= 0) {
        setsockopt(fd, IPPROTO_IP, IP_TOS, &opt.tos, sizeof(opt.tos));
    }
#endif

#ifdef SO_MARK
    if (opt.mark >= 0) {
        setsockopt(fd, SOL_SOCKET, SO_MARK, &opt.mark, sizeof(opt.mark));
    }
#endif
}

} // namespace hope::io
