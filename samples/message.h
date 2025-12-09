/* Copyright (C) 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#include <string>
#include <cstdint>
#include "hope-io/net/stream.h"
#include "hope-io/net/udp_sender.h"
#include "hope-io/net/udp_receiver.h"

struct message final {
    std::string name;
    std::string text;

    // TCP/TLS stream interface
    void send(hope::io::stream& stream) {
        stream.write(name);
        stream.write(text);
    }

    void recv(hope::io::stream& stream) {
        stream.read(name);
        stream.read(text);
    }

    // UDP sender interface
    void send(hope::io::udp_sender& sender) {
        auto name_len = static_cast<uint32_t>(name.size());
        auto text_len = static_cast<uint32_t>(text.size());
        sender.write(&name_len, sizeof(name_len));
        sender.write(name.data(), name.size());
        sender.write(&text_len, sizeof(text_len));
        sender.write(text.data(), text.size());
    }

    // UDP receiver interface
    void recv(hope::io::udp_receiver& receiver) {
        uint32_t name_len = 0;
        receiver.read(&name_len, sizeof(name_len));
        name.resize(name_len);
        receiver.read(name.data(), name_len);
        
        uint32_t text_len = 0;
        receiver.read(&text_len, sizeof(text_len));
        text.resize(text_len);
        receiver.read(text.data(), text_len);
    }
};