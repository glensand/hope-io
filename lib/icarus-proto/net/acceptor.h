/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

// ReSharper disable CppClangTidyCppcoreguidelinesSpecialMemberFunctions

#pragma once

#include <functional>
#include <string_view>

namespace icarus::io {

    class acceptor {
    public:
        using on_new_connection_t = std::function<void(class stream*)>;

        virtual ~acceptor() = default;
        virtual void run(std::string_view port, on_new_connection_t&& on_new_connection) = 0;
        virtual void stop() = 0;
    };

}
