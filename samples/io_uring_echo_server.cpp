// io_uring echo server — minimal, hardcoded, no deps on hope-io
// Build: g++ -std=c++20 -luring -O2 io_uring_echo_server.cpp -o io_uring_echo_server
// Usage: ./io_uring_echo_server [port]

#include <liburing.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/ip.h>

static constexpr int PORT     = 19300;
static constexpr int RING_SZ  = 4096;
static constexpr int BUF_SZ   = 1024 * 1024;

// Layout per-connection: [buffer | buffer]
// user_data encodes fd and operation:
//   (uint64_t(fd) << 1) | 0  = recv completion
//   (uint64_t(fd) << 1) | 1  = send completion
//   fd == listen_fd           = accept completion (tag uses listen_fd<<1|0 for accept)
static constexpr uint64_t tag_accept(int lfd) { return uint64_t(lfd) << 1; }
static constexpr uint64_t tag_recv(int fd)    { return (uint64_t(fd) << 1) | 0; }
static constexpr uint64_t tag_send(int fd)    { return (uint64_t(fd) << 1) | 1; }
static constexpr int      fd_of(uint64_t t)   { return int(t >> 1); }
static constexpr bool     is_send(uint64_t t) { return t & 1; }

struct conn {
    char* buf = nullptr;   // contiguous recv/send buffer
    size_t len = 0;        // last recv byte count
};

int main(int argc, char**) {
    // Socket
    int lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(lfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    bind(lfd, (sockaddr*)&addr, sizeof(addr));
    listen(lfd, 4096);

    // io_uring
    struct io_uring ring;
    io_uring_queue_init(RING_SZ, &ring, 0);

    // Per-connection state, indexed by fd
    conn* conns = (conn*)calloc(65536, sizeof(conn));

    // Submit first accept (tagged with listen_fd)
    auto* sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, lfd, nullptr, nullptr, SOCK_CLOEXEC);
    io_uring_sqe_set_data64(sqe, tag_accept(lfd));
    io_uring_submit(&ring);

    fprintf(stderr, "io_uring echo server on port %d\n", PORT);

    // Event loop
    for (;;) {
        struct io_uring_cqe* cqe;
        io_uring_wait_cqe(&ring, &cqe);

        unsigned head, count = 0;
        io_uring_for_each_cqe(&ring, head, cqe) {
            int res = cqe->res;
            uint64_t ud = io_uring_cqe_get_data64(cqe);
            count++;

            int fd = fd_of(ud);

            if (ud == tag_accept(lfd)) {
                // Accept completion
                if (res >= 0) {
                    int cfd = res;
                    fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL, 0) | O_NONBLOCK);
                    conns[cfd].buf = (char*)malloc(BUF_SZ);

                    // Submit recv
                    sqe = io_uring_get_sqe(&ring);
                    io_uring_prep_recv(sqe, cfd, conns[cfd].buf, BUF_SZ, 0);
                    io_uring_sqe_set_data64(sqe, tag_recv(cfd));
                }
                // Re-arm accept
                sqe = io_uring_get_sqe(&ring);
                io_uring_prep_accept(sqe, lfd, nullptr, nullptr, SOCK_CLOEXEC);
                io_uring_sqe_set_data64(sqe, tag_accept(lfd));
                continue;
            }

            if (res <= 0) {
                // Error or EOF
                if (conns[fd].buf) { free(conns[fd].buf); conns[fd].buf = nullptr; }
                ::close(fd);
                continue;
            }

            if (is_send(ud)) {
                // Send complete → submit recv
                sqe = io_uring_get_sqe(&ring);
                io_uring_prep_recv(sqe, fd, conns[fd].buf, BUF_SZ, 0);
                io_uring_sqe_set_data64(sqe, tag_recv(fd));
            } else {
                // Recv complete → submit send (echo)
                conns[fd].len = (size_t)res;
                sqe = io_uring_get_sqe(&ring);
                io_uring_prep_send(sqe, fd, conns[fd].buf, conns[fd].len, 0);
                io_uring_sqe_set_data64(sqe, tag_send(fd));
            }
        }
        io_uring_cq_advance(&ring, count);
        io_uring_submit(&ring);
    }
}
