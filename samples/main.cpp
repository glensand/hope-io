/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "hope-io/proto/argument.h"
#include "hope-io/proto/argument_container.h"
#include "hope-io/proto/argument_file.h"
#include "hope-io/proto/argument_struct.h"
#include "hope-io/proto/argument_array.h"
#include "hope-io/proto/argument_factory.h"
#include "hope-io/proto/message.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/factory.h"

#include <iostream>

struct message final {
    std::string name;
    std::string text;

    // todo:: make it more clear
    void send(hope::io::stream& stream){
        auto proto_msg = std::unique_ptr<hope::proto::argument>(
        hope::proto::struct_builder::create()
            .add<hope::proto::string>("name", name)
            .add<hope::proto::string>("text", text)
            .get("message"));
        proto_msg->write(stream);
    }

    void recv(hope::io::stream& stream) {
        auto proto_msg = std::unique_ptr<hope::proto::argument_struct>((hope::proto::argument_struct*)
                hope::proto::argument_factory::serialize(stream));
        name = proto_msg->field<std::string>("name");
        text = proto_msg->field<std::string>("text");
    }
};

void run_client(const std::string& name) {
    auto* stream = hope::io::create_stream();
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
    auto* acceptor = hope::io::create_acceptor(1338);
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