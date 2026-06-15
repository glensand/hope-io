/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#if PLATFORM_LINUX || PLATFORM_APPLE

#include "hope-io/net/init.h"

#include <csignal>

namespace hope::io {

    void init() {
        // Prevent SIGPIPE from killing the process when send() targets a closed socket.
        // Without this, write() throws never fire — the process dies silently.
        signal(SIGPIPE, SIG_IGN);
    }

    void deinit() {

    }

}

#endif
