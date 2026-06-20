/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "message.h"

#include "hope-io/net/udp_builder.h"
#include "hope-io/net/nix/udp_sender_impl.h"
#include "hope-io/net/nix/udp_receiver_impl.h"
#include "hope-io/net/init.h"

#include <iostream>

int main(int argc, char *argv[]) {
    hope::io::init();
    auto* sender = new hope::io::udp_sender_impl();
    try {
        sender->connect("localhost", 1338);
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
        std::terminate();
    }

    auto* receiver = new hope::io::udp_receiver_impl(sender->platform_socket());
    try {
        receiver->connect("localhost", 1338);
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
        std::terminate();
    }

    message msg{ argv[1] };
    std::cin >> msg.text;
    msg.send(*sender);
    msg.recv(*receiver);
    std::cout << "recv msg[" << msg.name << ":" << msg.text << "]\n";

    delete sender;
    delete receiver;

    return 0;
}