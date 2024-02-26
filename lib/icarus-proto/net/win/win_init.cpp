#include "icarus-proto/coredefs.h"

#ifdef ICARUS_WIN

#include "icarus-proto/net/init.h"

#include <winsock2.h>
#include <stdexcept>

namespace icarus::io {

    static bool initialized{ false };

    void init() {
        if (!initialized) {
            WSADATA wsa_data;
            if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
                throw std::runtime_error("Win error: cannot initialize WSA");
            initialized = true;
        }
    }

    void deinit() {
        if (initialized) {
            WSACleanup();
            initialized = false;
        }
    }

}

#endif
