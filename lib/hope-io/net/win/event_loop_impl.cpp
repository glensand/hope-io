/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#if PLATFORM_WINDOWS

#include "hope-io/net/win/event_loop_impl.h"
#include "hope-io/net/factory.h"

namespace hope::io {

    event_loop* create_event_loop() {
        return nullptr;
    }

}

#endif
