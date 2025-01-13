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

struct kv_service final {
    explicit kv_service(int32_t port) {
        auto* loop = hope::io::create_event_loop();
        // TODO:: need to add more type safety to run() method, but for now just comments
        loop->run(port, 
            hope::io::event_loop::callbacks {
                // connection
                [this] (hope::io::event_loop::connection& c) {
                    c.set_state(hope::io::event_loop::connection_state::read);
                },
                // on_read
                [this] (hope::io::event_loop::connection& c) {
                    if (c.buffer->count() > sizeof(uint32_t)) {
                        const auto used_chunk = c.buffer->used_chunk();
                        const auto message_length = *(uint32_t*)(used_chunk.first);
                        if (message_length == used_chunk.second) {
                            auto* stream = event_loop_stream_wrapper::get(c);
                            stream->begin_read();
                            auto proto_msg = std::unique_ptr<hope::proto::argument_struct>((hope::proto::argument_struct*)
                                hope::proto::argument_factory::serialize(*stream));
                            stream->end_read();
                            auto type = (message_type)proto_msg->field<hope::proto::int32>("type").get();
                            auto&& key = proto_msg->field<hope::proto::string>("key").get();
                            if (type == message_type::get) {
                                const auto it = storage.find(key);
                                get_response response{ key, nullptr };
                                if (it != end(storage)) {
                                    response.value = it->second.value;
                                }
                                stream->begin_write();
                                response.write(*stream);
                                stream->end_write();
                                c.set_state(hope::io::event_loop::connection_state::write);
                            } else if (type == message_type::set) {
                                auto&& entry = storage[key];
                                delete entry.value;
                                entry.value = proto_msg->release("value");
                                set_response response{ true };
                                stream->begin_write();
                                response.write(*stream);
                                stream->end_write();
                                c.set_state(hope::io::event_loop::connection_state::write);
                            } else {
                                c.set_state(hope::io::event_loop::connection_state::die);
                                assert(false);
                            }
                        }
                    }
                },
                // on_write
                [this] (hope::io::event_loop::connection& c) {
                    c.set_state(hope::io::event_loop::connection_state::die);
                },
                // on_error
                [this] (hope::io::event_loop::connection& c, const std::string& err) {
                    if (c.buffer->count() > sizeof(uint32_t)) {
                        
                    }
                },
            }
        );
    }

    struct entry final {
        // TODO:: use it for optimization purposes
        std::vector<uint8_t> serialized_value;

        hope::proto::argument* value = nullptr;
    };

    // NOTE:: use argument as key?
    std::unordered_map<std::string, entry> storage;
};

int main() {
    try {
        kv_service serv(1400);
    } catch(const std::exception& e) {
        std::cout << e.what();
    }
    return 0;
}