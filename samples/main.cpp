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
#include "hope-io/net/udp_builder.h"
#include "hope-io/net/factory.h"

#include <iostream>

static bool tcp = false; // will use UDP protocol if set to false
static bool tls = tcp;

auto* create_acceptor(std::size_t port) {
    auto* acceptor = tls ?
            hope::io::create_tls_acceptor("key.pem", "cert.pem")
            : hope::io::create_acceptor();
    acceptor->open(port);
    return acceptor;
}

auto* create_stream() {
    return tls ? hope::io::create_tls_stream() : hope::io::create_stream();
}

struct message final {
    std::string name;
    std::string text;

    // todo:: make it more clear
    void send(hope::io::stream& stream) {
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

void run_tcp_client(const std::string& name) {
    auto* stream = create_stream();
    try {
        stream->connect("localhost", 1338);
    }
    catch (const std::exception& ex) {
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

void run_udp_client(const std::string& name) {
    auto* sender = hope::io::create_sender();
    try {
        sender->connect("localhost", 1338);
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
        std::terminate();
    }

    auto* receiver = hope::io::create_receiver(sender->platform_socket());
    try {
        receiver->connect("localhost", 1338);
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
        std::terminate();
    }

    message msg{ name };
    std::cin >> msg.text;
    msg.send(*sender);
    msg.recv(*receiver);
    std::cout << "recv msg[" << msg.name << ":" << msg.text << "]\n";

    delete sender;
    delete receiver;
}

void run_tcp_server() {
    auto* acceptor = create_acceptor(1338);
    auto* connection = acceptor->accept();

    message msg;
    std::cout << "listen\n";
    msg.recv(*connection);
    std::cout << "new msg[" << msg.name << ":" << msg.text << "]\n"; 
    msg.send(*connection);
    std::cout << "sent\n";
    std::cout << "ip:" << connection->get_endpoint();
}

void run_udp_server() {
    auto* udp_builder = hope::io::create_udp_builder();
    try {
        udp_builder->init(1338);
        std::cout << "UDP server is initialized." << std::endl;
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
        std::terminate();
    }

    auto* receiver = hope::io::create_receiver(udp_builder->platform_socket());
    try {
        receiver->connect("localhost", 1338);
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
        std::terminate();
    }

    auto* sender = hope::io::create_sender(udp_builder->platform_socket());
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

int main(int argc, char *argv[]) {
    if (argv[1] == std::string("server"))
        tcp ? run_tcp_server() : run_udp_server();
    else
        tcp ? run_tcp_client(argv[1]) : run_udp_client(argv[1]);

    return 0;
}