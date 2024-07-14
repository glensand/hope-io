/* Copyright (C) 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#include "hope-io/proto/argument.h"
#include "hope-io/proto/argument_container.h"
#include "hope-io/proto/argument_file.h"
#include "hope-io/proto/argument_struct.h"
#include "hope-io/proto/argument_array.h"
#include "hope-io/proto/argument_factory.h"
#include "hope-io/proto/message.h"

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