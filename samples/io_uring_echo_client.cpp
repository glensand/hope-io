// io_uring echo client — N concurrent connections, single thread, hardcoded
// Build: g++ -std=c++20 -luring -O2 io_uring_echo_client.cpp -o io_uring_echo_client

#include <liburing.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <chrono>

static constexpr int   PORT        = 19300;
static constexpr int   PAYLOAD     = 64;
static constexpr int   CONNECTIONS = 50;
static constexpr int   WARMUP      = 500;    // rounds (each round = CONNECTIONS ops)
static constexpr int   ROUNDS      = 2000;   // benchmark rounds
static constexpr int   RING_SZ     = 4096;

struct conn {
    int   fd;
    char* tx;
    char* rx;
};

static double now_sec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static int make_conn(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
    if (connect(fd, (sockaddr*)&saddr, sizeof(saddr)) < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

int main(int, char**) {
    // Connect N
    conn* cs = (conn*)calloc(CONNECTIONS, sizeof(conn));
    for (int i = 0; i < CONNECTIONS; i++) {
        cs[i].fd = make_conn(PORT);
        if (cs[i].fd < 0) { fprintf(stderr, "conn %d failed\n", i); return 1; }
        cs[i].tx = (char*)malloc(PAYLOAD);
        cs[i].rx = (char*)malloc(PAYLOAD);
        memset(cs[i].tx, 'x', PAYLOAD);
    }

    struct io_uring ring;
    io_uring_queue_init(RING_SZ, &ring, 0);

    // Submit N SQEs, submit to kernel, drain N CQEs
    auto batch = [&](bool do_send) {
        for (int i = 0; i < CONNECTIONS; i++) {
            auto* sqe = io_uring_get_sqe(&ring);
            if (!sqe) { fprintf(stderr, "sqe\n"); exit(1); }
            if (do_send)
                io_uring_prep_send(sqe, cs[i].fd, cs[i].tx, PAYLOAD, 0);
            else
                io_uring_prep_recv(sqe, cs[i].fd, cs[i].rx, PAYLOAD, 0);
        }

        int ret = io_uring_submit(&ring);
        if (ret < 0) { fprintf(stderr, "submit\n"); exit(1); }

        int rem = CONNECTIONS;
        while (rem > 0) {
            struct io_uring_cqe* cqe;
            ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0) { fprintf(stderr, "wait\n"); exit(1); }
            if (cqe->res < 0) { fprintf(stderr, "op: %s\n", strerror(-cqe->res)); exit(1); }
            io_uring_cqe_seen(&ring, cqe);
            rem--;
        }
    };

    // Warmup
    for (int r = 0; r < WARMUP; r++) { batch(true); batch(false); }

    // Benchmark
    double t0 = now_sec();
    for (int r = 0; r < ROUNDS; r++) { batch(true); batch(false); }
    double t1 = now_sec();

    double elapsed = t1 - t0;
    double rps = (double)(CONNECTIONS * ROUNDS) / elapsed;

    printf("io_uring_client: connections=%d payload=%d rounds=%d elapsed=%.3fs rps=%.0f\n",
           CONNECTIONS, PAYLOAD, ROUNDS, elapsed, rps);

    for (int i = 0; i < CONNECTIONS; i++) { close(cs[i].fd); free(cs[i].tx); free(cs[i].rx); }
    free(cs);
    io_uring_queue_exit(&ring);
    return 0;
}
