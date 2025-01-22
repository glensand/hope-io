/* Copyright (C) 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#include "hope-io/net/stream.h"

struct message final {
    std::string name;
    std::string text;

    // todo:: make it more clear
    void send(hope::io::stream& stream) {
        stream.write(name);
        stream.write(text);
    }

    void recv(hope::io::stream& stream) {
        stream.read(name);
        stream.read(text);
    }
};