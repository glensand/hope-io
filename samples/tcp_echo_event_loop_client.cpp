/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "message.h"

#include "hope-io/net/stream.h"
#include "hope-io/net/nix/tcp_stream.h"
#include "hope-io/net/init.h"

#include <iostream>

int main(int argc, char *argv[]) {
    if (argc < 2){
        std::cout << "Too few arguments were provided, please rerun app and provide name\n";
        return -1;
    }
    hope::io::init();
    try {
        auto* stream = new hope::io::tcp_stream();
        stream->connect("localhost", 1338);
        stream->set_options({});
        std::string msg = "Too few arguments were provided, please rerun app and provide name";
        for (auto i = 0; i < 10000; ++i) {
            stream->write(uint32_t(msg.size()));
            stream->write(msg.data(), msg.size());
            msg.resize(0);
            uint32_t size;
            stream->read(size);
            msg.resize(size);
            stream->read(msg.data(), size);
        }
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
    }
    return 0;
}