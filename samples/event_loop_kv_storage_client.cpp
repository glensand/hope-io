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

    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
    }
    return 0;
}