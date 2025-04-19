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
#include "hope-io/coredefs.h"

#include <iostream>
#include <utility>

void on_connect(hope::io::event_loop::connection& c) {
    NAMED_SCOPE(OnConnect);
    c.set_state(hope::io::event_loop::connection_state::read);
}

void on_read(hope::io::event_loop::connection& c) {
    NAMED_SCOPE(OnRead);
    const auto local_data = c.buffer->used_chunk();
    const auto message_length = local_data.second;
    if (message_length >= sizeof(uint32_t)) {
        // if we have enought data, lets try to read it
        auto string_length = *((uint32_t*)local_data.first);
        // so we have enough data to read whole message, read it now
        // otherwise will read later (need to wait a bit)
        if (string_length == message_length - sizeof(uint32_t)) {
            const char* p_text = ((char*)local_data.first + sizeof(uint32_t));
            std::string_view text(p_text, string_length);
            std::cout << "Got message:" << text << "\n";

            c.buffer->reset();
            // set buffer pointer to read pos to echo msg
            c.buffer->handle_write(local_data.second);
            c.set_state(hope::io::event_loop::connection_state::write);
        } 
    }
}

void on_write(hope::io::event_loop::connection& c) {
    NAMED_SCOPE(OnWrite);
    c.set_state(hope::io::event_loop::connection_state::read);
}

void on_err(hope::io::event_loop::connection& c, const std::string& what) {
    NAMED_SCOPE(OnError);
    std::cout << "Err occured:" << what << "\n";
}

int main() {
    EASY_PROFILER_ENABLE;
    profiler::startListen();
    try {
        hope::io::init();
        auto* loop = hope::io::create_event_loop();
        
        hope::io::event_loop::callbacks cb{
            [](auto& c) {
                on_connect(c);
            },
            [](auto& c) {
                on_read(c);
            },
            [](auto& c) {
                on_write(c);
            },
            [](auto& c, const std::string& what) {
                on_err(c, what);
            },
        };

        loop->run(1338, std::move(cb));
    } catch(const std::exception& e) {
        std::cout << e.what();
    }
    return 0;
}