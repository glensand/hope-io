# hope-io

Small cross-platform C++ networking library for TCP, UDP, and TLS communication.

The project focuses on a lightweight API with minimal setup:
- create a socket object from the factory
- connect/open endpoint
- read and write bytes or trivial types

This repository also includes ready-to-run samples and tests.

## Key features

- TCP client/server primitives (`stream`, `acceptor`)
- UDP sender/receiver utilities (`udp_sender`, `udp_receiver`, `udp_builder`)
- TLS support when OpenSSL is available (`create_tls_stream`, `create_tls_acceptor`)
- Event loop API for high-throughput server side processing
- Linux, macOS, and Windows platform abstraction

## Requirements

- CMake `>= 3.11`
- C++17 or newer compiler (the project CMake files use C++17/C++20 depending on module)
- OpenSSL (optional, required only for TLS features)

## Build

```bash
git clone https://github.com/glensand/hope-io.git
cd hope-io
cmake -S . -B build
cmake --build build -j
```

Artifacts:
- static library: linked into sample/test targets
- sample binaries: `build/bin/*`
- tests binary: `build/bin/hope-io-test`

## Run tests

```bash
ctest --test-dir build --output-on-failure
```

## Samples

All sample applications are in `samples/` and are built automatically.

Common examples:
- `tcp_echo_server`
- `tcp_echo_client`
- `tls_echo_server`
- `tls_echo_client`
- `udp_echo_server`
- `udp_echo_client`
- `tcp_echo_event_loop_server`

Run any sample from the build output directory:

```bash
./build/bin/tcp_echo_server
./build/bin/tcp_echo_client alice
```

## Usage examples

### 1) TCP client

```cpp
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include "hope-io/net/stream.h"

#include <cstdint>
#include <string>

int main() {
    hope::io::init();

    auto* stream = hope::io::create_stream();
    stream->connect("127.0.0.1", 1338);
    stream->set_options({});

    std::string payload = "hello";
    stream->write(payload); // writes uint16_t size + bytes

    std::string reply;
    stream->read(reply); // reads uint16_t size + bytes

    delete stream;
    hope::io::deinit();
    return 0;
}
```

### 2) TCP echo server

```cpp
#include "hope-io/net/acceptor.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include "hope-io/net/stream.h"

int main() {
    hope::io::init();

    auto* acceptor = hope::io::create_acceptor();
    acceptor->open(1338);

    auto* conn = acceptor->accept();
    conn->set_options({});

    for (;;) {
        uint32_t size = 0;
        conn->read(size);
        std::string msg(size, '\0');
        conn->read(msg.data(), msg.size());

        conn->write(size);
        conn->write(msg.data(), msg.size());
    }
}
```

### 3) TLS client (OpenSSL required)

```cpp
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include "hope-io/net/tls/tls_init.h"

int main() {
    hope::io::init();
    hope::io::init_tls();

    auto* tls_stream = hope::io::create_tls_stream();
    tls_stream->connect("127.0.0.1", 1339);
    tls_stream->write(std::string("secure hello"));

    delete tls_stream;
    hope::io::deinit();
    return 0;
}
```

### 4) UDP sender/receiver

```cpp
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include "hope-io/net/udp_builder.h"

int main() {
    hope::io::init();

    auto* sender = hope::io::create_udp_sender();
    sender->connect("127.0.0.1", 1338);

    auto* receiver = hope::io::create_udp_receiver(sender->platform_socket());
    receiver->connect("127.0.0.1", 1338);

    const char data[] = "ping";
    sender->write(data, sizeof(data));

    char buffer[64] = {};
    receiver->read(buffer, sizeof(buffer));

    delete receiver;
    delete sender;
    hope::io::deinit();
    return 0;
}
```

### 5) Event loop server skeleton

```cpp
#include "hope-io/net/event_loop.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"

int main() {
    hope::io::init();

    hope::io::event_loop::config cfg;
    cfg.port = 1338;

    hope::io::event_loop::callbacks cb;
    cb.on_connect = [](hope::io::event_loop::connection& c) {
        c.set_state(hope::io::event_loop::connection_state::read);
    };
    cb.on_read = [](hope::io::event_loop::connection& c) {
        c.set_state(hope::io::event_loop::connection_state::write);
    };
    cb.on_write = [](hope::io::event_loop::connection& c) {
        c.set_state(hope::io::event_loop::connection_state::read);
    };
    cb.on_err = [](hope::io::event_loop::connection& c, const std::string&) {
        c.set_state(hope::io::event_loop::connection_state::die);
    };

    auto* loop = hope::io::create_event_loop();
    loop->run(cfg, std::move(cb));
}
```

## API entry points

Main headers:
- `lib/hope-io/net/init.h`
- `lib/hope-io/net/factory.h`
- `lib/hope-io/net/stream.h`
- `lib/hope-io/net/acceptor.h`
- `lib/hope-io/net/event_loop.h`
- `lib/hope-io/net/tls/tls_init.h`
- `lib/hope-io/net/udp_builder.h`

## Notes

- TLS-related functionality is compiled only when OpenSSL is found by CMake.
- For production usage, ensure timeouts and socket options are explicitly configured
