/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#pragma once

#include "win_init.h"

#include <winsock2.h>
#include <stdexcept>

namespace icarus::io::win {

    static bool initialized{ false };

    void init() {
        if (!initialized) {
            WSADATA wsa_data;
            if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
                throw std::runtime_error("Win error: cannot initialize WSA");
            initialized = true;
        }
    }

    void deinit() {
        if (initialized) {
            WSACleanup();
            initialized = false;
        }
    }

}
