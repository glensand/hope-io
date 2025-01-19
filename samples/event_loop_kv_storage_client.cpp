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

#include "hope-io/proto/argument.h"
#include "hope-io/proto/message.h"

#include "kv_misc.h"

#include <iostream>
#include <utility>
#include <unordered_map>
#include "message.h"

int main() {
    hope::io::init();
    auto* stream = hope::io::create_stream();
    try {
        // TODO:: buffer is not suitable for client...
        hope::io::event_loop::fixed_size_buffer buffer;
        event_loop_stream_wrapper buffer_wrapper(buffer);
        constexpr static auto num = 100;
        for (auto i = 0; i < num; ++i) {
            stream->connect("localhost", 1400);
            set_request request(std::to_string(i), new hope::proto::int32("value", i));
            buffer_wrapper.begin_write();
            request.write(buffer_wrapper);
            buffer_wrapper.end_write();
            auto used_part = buffer.used_chunk();
            stream->write(used_part.first, used_part.second);

            // read responce
            uint32_t stub;
            stream->read(stub);
            set_response{}.read(*stream);
            stream->disconnect();
            std::cout << "write:" << i << "\n";
        }

        for (auto i = 0; i < num; ++i) {
            stream->connect("localhost", 1400);
            buffer.reset();
            get_request request(std::to_string(i));
            buffer_wrapper.begin_write();
            request.write(buffer_wrapper);
            buffer_wrapper.end_write();
            auto used_part = buffer.used_chunk();
            stream->write(used_part.first, used_part.second);

            // read responce
            uint32_t stub;
            stream->read(stub);
            get_response r;
            r.read(*stream);
            stream->disconnect();

            std::cout << "read:" << i << r.value->as<int32_t>() << "\n";
        }
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
    }
    return 0;
}