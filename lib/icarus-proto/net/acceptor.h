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

        virtual ~acceptor() = default;
        virtual class stream* accept() = 0;
    };

}
