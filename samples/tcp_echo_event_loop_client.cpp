/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "message.h"

#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"

#include <iostream>

int main(int argc, char *argv[]) {
    if (argc < 2){
        std::cout << "Too few arguments were provided, please rerun app and provide name\n";
        return -1;
    }
    hope::io::init();
    auto* stream = hope::io::create_stream();
    try {
        stream->connect("localhost", 1338);
        stream->set_options({});
        std::string msg;
        std::cin >> msg;
        for (; msg != "exit"; std::cin >> msg) {
            stream->write(uint32_t(msg.size()));
            stream->write(msg.data(), msg.size());
            std::cout << "all bytes written" << std::endl;
            msg.resize(0);
            uint32_t size;
            stream->read(size);
            std::cout << "message size: " << size << std::endl;
            msg.resize(size);
            stream->read(msg.data(), size);
            std::cout << "recv msg[" << msg << ":" << msg << "]\n"; 
        }
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
    }
    return 0;
}