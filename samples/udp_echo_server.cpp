/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "message.h"
#include "hope-io/net/udp_builder.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"

#include <iostream>

int main(int argc, char *argv[]) {
    hope::io::init();

    auto* udp_builder = hope::io::create_udp_builder();
    try {
        udp_builder->init(1338);
        std::cout << "UDP server is initialized." << std::endl;
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
        std::terminate();
    }

    auto* receiver = hope::io::create_udp_receiver(udp_builder->platform_socket());
    try {
        receiver->connect("localhost", 1338);
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
        std::terminate();
    }

    auto* sender = hope::io::create_udp_sender(udp_builder->platform_socket());
    try {
        sender->connect("localhost", 1338);
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
        std::terminate();
    }

    message msg;
    msg.recv(*receiver);
    std::cout << "new msg[" << msg.name << ":" << msg.text << "]\n";
    msg.send(*sender);
    std::cout << "sent\n";

    delete receiver;
    delete sender;
}