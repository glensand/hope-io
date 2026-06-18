/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/event_loop.h"

#if PLATFORM_WINDOWS

namespace hope::io {

    class event_loop_impl final : public event_loop {
    public:
        event_loop_impl() = default;

        void run(const config&, callbacks&&) override {}
        void stop() override {}
    };

}

#endif
