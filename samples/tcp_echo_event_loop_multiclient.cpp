/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "message.h"

#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"

#include<sys/types.h>
#include<stdlib.h>
#include<sys/wait.h>

#include <iostream>
#include <cstdlib>

void do_client_stuff(bool recon) {
    hope::io::init();
    try {
        auto* stream = hope::io::create_stream();
        stream->connect("localhost", 1338);
        stream->set_options({});
        std::string msg = "Too few arguments were provided, please rerun app and provide name";
        for (auto i = 0; i < 100; ++i) {
            if (recon) {
                stream->connect("localhost", 1338);
            }
            stream->write(uint32_t(msg.size()));
            stream->write(msg.data(), msg.size());
            msg.resize(0);
            uint32_t size;
            stream->read(size);
            msg.resize(size);
            stream->read(msg.data(), size);
            if (recon) {
                stream->disconnect();
            }
        }
    }
    catch (const std::exception& ex) {
        std::cout << ex.what();
    }
}

int main(int argc, char *argv[]) {
    static const auto NumProcesses = 18;
    for (auto i = 0; i < NumProcesses - 1; ++i) {
        pid_t processId;
        if ((processId = fork()) == 0) {
            // child process, do client stuff
            do_client_stuff(std::rand() % 2 == 0);
            return 0;
        } else if (processId < 0) {
            perror("fork error");
            return -1;
        }
    }

    // do clint stuff from parent processs as well
    do_client_stuff(false);
    return 0;
}