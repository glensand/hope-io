/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

// ReSharper disable CppClangTidyCppcoreguidelinesSpecialMemberFunctions

#pragma once

#include <cstddef>

namespace hope::io {

    class acceptor {
    public:

        virtual ~acceptor() = default;

        virtual void set_options(const struct stream_options& opt) = 0;
        virtual void open(std::size_t port) = 0;
        virtual class stream* accept() = 0;
    };

}
