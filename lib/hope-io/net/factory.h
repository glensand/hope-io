/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include <string_view>

namespace hope::io {

    // TCP stuff
    class acceptor* create_acceptor();
    /// Pass \p socket == (unsigned long long)-1 for a new client stream (not connected yet). Never use 0 here: fd 0 is stdin.
    class stream* create_stream(unsigned long long socket = static_cast<unsigned long long>(-1));
        
    // TLS stuff
    class acceptor* create_tls_acceptor(std::string_view key, std::string_view cert);
    class stream* create_tls_stream(stream* tcp_stream = nullptr);

    // UDP stuff
    class udp_receiver* create_udp_receiver(unsigned long long socket = 0);
    class udp_sender* create_udp_sender(unsigned long long socket = 0);
    class udp_builder* create_udp_builder();

    // server loop
    class event_loop* create_event_loop();

}

// ── Platform implementation headers ──────────────────────────────
#if PLATFORM_LINUX || PLATFORM_APPLE
#  include "hope-io/net/nix/tcp_stream.h"
#  include "hope-io/net/nix/tcp_acceptor.h"
#  include "hope-io/net/nix/udp_receiver_impl.h"
#  include "hope-io/net/nix/udp_sender_impl.h"
#  include "hope-io/net/nix/udp_builder_impl.h"
#  if PLATFORM_APPLE
#    include "hope-io/net/nix/event_loop_impl.h"
#    ifdef HOPE_IO_USE_OPENSSL
#      include "hope-io/net/nix/tls_event_loop_impl.h"
#    endif
#  elif PLATFORM_LINUX
#    include "hope-io/net/linux/event_loop_impl.h"
#    ifdef HOPE_IO_USE_OPENSSL
#      include "hope-io/net/linux/tls_event_loop_impl.h"
#    endif
#  endif
#endif

#if PLATFORM_WINDOWS
#  include "hope-io/net/win/tcp_stream.h"
#  include "hope-io/net/win/tcp_acceptor.h"
#  include "hope-io/net/win/udp_receiver_impl.h"
#  include "hope-io/net/win/udp_sender_impl.h"
#  include "hope-io/net/win/udp_builder_impl.h"
#  include "hope-io/net/win/event_loop_impl.h"
#endif

#ifdef HOPE_IO_USE_OPENSSL
#  include "hope-io/net/tls/tcp_tls_stream.h"
#  include "hope-io/net/tls/tls_server_stream.h"
#  include "hope-io/net/tls/tls_acceptor_impl.h"
#endif
