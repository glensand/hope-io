#include "hope-io/net/event_loop.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"

#include <deque>
#include <unordered_set>
#include <atomic>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <iostream>

namespace hope::io { 

    event_loop* create_event_loop2() {
        return nullptr;
    }

}