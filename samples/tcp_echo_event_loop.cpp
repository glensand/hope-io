/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "hope-io/net/factory.h"
#include "hope-io/net/event_loop.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"

#include <iostream>

int main() {
    #if 0
        try {
        hope::io::init();
        auto* loop = hope::io::create_event_loop();
        //hope::io
        loop->open(1338);

        auto* connection = acceptor->accept();
        hope::io::stream_options options;
        connection->set_options(options);
        while (true) {
            message msg;
            msg.recv(*connection);
            std::cout << "new msg[" << msg.name << ":" << msg.text << "]\n";
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(10s);
            msg.send(*connection);
            std::cout << "sent\n";
        }
        delete connection;
    } catch(const std::exception& e) {
        std::cout << e.what();
    }
    #endif
}