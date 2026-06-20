/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "hope-io/net/event_loop.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/acceptor.h"
#if PLATFORM_LINUX
#include "hope-io/net/linux/event_loop_impl.h"
#endif
#if PLATFORM_APPLE
#include "hope-io/net/nix/event_loop_impl.h"
#endif
#if PLATFORM_WINDOWS
#include "hope-io/net/win/event_loop_impl.h"
#endif
#include "hope-io/net/init.h"
#include "hope-io/coredefs.h"

#include <iostream>
#include <utility>
#include <thread>

void on_connect(hope::io::event_loop::connection& c) {
    NAMED_SCOPE(OnConnect);
    c.set_state(hope::io::event_loop::connection_state::read);
}

void on_read(hope::io::event_loop::connection& c) {
    NAMED_SCOPE(OnRead);
    const auto local_data = c.buffer->peek_used();
    const auto message_length = local_data.second;
    if (message_length >= sizeof(uint32_t)) {
        // if we have enought data, lets try to read it
        auto string_length = *((uint32_t*)local_data.first);
        // so we have enough data to read whole message, read it now
        // otherwise will read later (need to wait a bit)
        if (string_length == message_length - sizeof(uint32_t)) {
            const char* p_text = ((char*)local_data.first + sizeof(uint32_t));
            std::string_view text(p_text, string_length);
            c.buffer->reset();
            // write the message back for echo
            c.buffer->write(local_data.first, local_data.second);
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

std::thread worker;

int main() {
    hope::io::init();
    auto* loop = new hope::io::event_loop_impl();
    try {
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

        hope::io::event_loop::config cfg;
        cfg.port = 1338;
        worker = std::thread([=]() mutable {
            loop->run(cfg, std::move(cb));
        });
    } catch(const std::exception& e) {
        std::cout << e.what();
    }
    int stub;
    std::cin >> stub;
    loop->stop();
    worker.join();
    return 0;
}