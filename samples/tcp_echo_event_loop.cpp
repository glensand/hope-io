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
#include "hope-io/net/init.h"
#include "hope-io/coredefs.h"

#include <iostream>
#include <thread>
#include <string_view>

std::thread worker;

using conn_state = hope::io::el::el_connection_state;

auto on_connect = [](auto&) { return conn_state::read; };

auto on_read = [](auto& c) {
    NAMED_SCOPE(OnRead);
    const auto local_data = c.buffer->peek_used();
    const auto message_length = local_data.second;
    if (message_length >= sizeof(uint32_t)) {
        auto string_length = *((uint32_t*)local_data.first);
        if (string_length == message_length - sizeof(uint32_t)) {
            const char* p_text = ((char*)local_data.first + sizeof(uint32_t));
            std::string_view text(p_text, string_length);
            c.buffer->reset();
            c.buffer->write(local_data.first, local_data.second);
            return conn_state::write;
        }
    }
    return conn_state::read;
};

auto on_write = [](auto&) { return conn_state::read; };

auto on_err = [](auto&, const std::string& what) {
    NAMED_SCOPE(OnError);
    std::cout << "Err occured:" << what << "\n";
    return conn_state::die;
};

int main() {
    hope::io::init();

    auto* loop = new hope::io::el::event_loop_impl_t(
        std::move(on_connect), std::move(on_read), std::move(on_write), std::move(on_err)
    );
    try {
        hope::io::el::config cfg;
        cfg.port = 1338;
        worker = std::thread([loop, cfg]() mutable {
            loop->run(cfg);
        });
    } catch(const std::exception& e) {
        std::cout << e.what();
        return 1;
    }
    int stub;
    std::cin >> stub;
    loop->stop();
    worker.join();
    return 0;
}
