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
                            event_loop_stream_wrapper stream(*c.buffer); 
                            std::cout << "\n\n[";
                            auto used_part = c.buffer->used_chunk();
                            std::cout.write((char*)used_part.first, used_part.second);
                            std::cout << "]\n\n";
                            stream.begin_read();
                            auto proto_msg = std::unique_ptr<hope::proto::argument_struct>((hope::proto::argument_struct*)
                                hope::proto::argument_factory::serialize(stream));
                            stream.end_read();
                            // TODO:: ensure all data were consumed
                            auto type = (message_type)proto_msg->field<int32_t>("type");
                            if (type != message_type::get && type != message_type::set) {
                                c.set_state(hope::io::event_loop::connection_state::die);
                                assert(false);
                            }
                            else {
                                auto&& key = proto_msg->field<std::string>("key");
                                stream.begin_write();
                                if (type == message_type::get) {
                                    const auto it = storage.find(key);
                                    get_response response{ key, nullptr };
                                    if (it != end(storage)) {
                                        response.value = it->second.value;
                                    }
                                    response.write(stream);
                                } else {
                                    auto&& entry = storage[key];
                                    delete entry.value;
                                    entry.value = proto_msg->release("value");
                                    set_response response{ true };   
                                    response.write(stream);
                                }
                                stream.end_write();
                                c.set_state(hope::io::event_loop::connection_state::write);
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