/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "message.h"

#include "hope-io/net/stream.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include "hope-io/net/tls/tls_init.h"

#include <iostream>

int main(int argc, char *argv[]) {
    hope::io::init();
    hope::io::init_tls();

    auto* acceptor = hope::io::create_tls_acceptor("key.pem", "cert.pem");
    acceptor->open(1339);

    auto* connection = acceptor->accept();
    while (true) {
        message msg;
        msg.recv(*connection);
        std::cout << "new msg[" << msg.name << ":" << msg.text << "]\n"; 
        msg.send(*connection);
        std::cout << "sent\n";
        std::cout << "ip:" << connection->get_endpoint();
    }

    return 0;
}