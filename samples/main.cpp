/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "icarus-proto/protocol/argument.h"
#include "icarus-proto/protocol/argument_container.h"
#include "icarus-proto/protocol/argument_file.h"
#include "icarus-proto/protocol/argument_struct.h"
#include "icarus-proto/protocol/argument_array.h"
#include "icarus-proto/protocol/argument_factory.h"
#include "icarus-proto/protocol/message.h"
#include "icarus-proto/net/stream.h"
#include "icarus-proto/net/acceptor.h"
#include "icarus-proto/net/factory.h"

#include <iostream>

struct message final {
    std::string name;
    std::string text;

    // todo:: make it more clear
    void send(icarus::io::stream& stream){
        auto proto_msg = std::unique_ptr<icarus::proto::argument>(
        icarus::proto::struct_builder::create()
            .add<icarus::proto::string>("name", name)
            .add<icarus::proto::string>("text", text)
            .get("message"));
        proto_msg->write(stream);
    }

    void recv(icarus::io::stream& stream) {
        auto proto_msg = std::unique_ptr<icarus::proto::argument_struct>((icarus::proto::argument_struct*)
                icarus::proto::argument_factory::serialize(stream));
        name = proto_msg->field<std::string>("name");
        text = proto_msg->field<std::string>("text");
    }
};

void run_client(const std::string& name) {
    auto* stream = icarus::io::create_stream();
    try {
        stream->connect("localhost", 1338);
    }
    catch (const std::exception& ex){
        std::cout << ex.what();
        std::terminate();
    }

    message msg{ name };
    std::cin >> msg.text;
    msg.send(*stream);
    msg.recv(*stream);
    std::cout << "recv msg[" << msg.name << ":" << msg.text << "]\n"; 
    delete stream;
}

void run_server() {
    auto* acceptor = icarus::io::create_acceptor(1338);
    auto* connection = acceptor->accept();
    message msg;
    std::cout << "listen\n";
    msg.recv(*connection);
    std::cout << "new msg[" << msg.name << ":" << msg.text << "]\n"; 
    msg.send(*connection);
    std::cout << "sent";
}

int main(int argc, char *argv[]) {
    if (argv[1] == std::string("server"))
        run_server();
    else
        run_client(argv[1]);

    return 0;
}