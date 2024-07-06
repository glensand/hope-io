/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "hope-io/coredefs.h"
#ifdef ICARUS_WIN

#include "hope-io/net/init.h"

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#include <stdexcept>
#include <mutex>

namespace hope::io {

    static int initialized{ 0 };
    static std::mutex guard;
    
    void init() {
        std::lock_guard lock(guard);
        if (initialized == 0) {
            WSADATA wsa_data;
            if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
                throw std::runtime_error("hope-io/win_init: cannot initialize WSA");
        }
        ++initialized;
    }

    void deinit() {
        std::lock_guard lock(guard);
        --initialized;
        if (initialized == 0) {
            WSACleanup();
        }
    }

}

#endif
