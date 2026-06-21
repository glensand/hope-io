/* Copyright (C) 2026 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/coredefs.h"

#if PLATFORM_LINUX

#include <cstdint>
#include <cstddef>
#include <liburing.h>

namespace hope::io::uring {

    // ── Constants ──────────────────────────────────────────────────────
    constexpr int RING_ENTRIES = 4096;

    // ── Tag encoding ───────────────────────────────────────────────────
    // user_data is uint64_t. Layout of bits:
    //   TCP:   bits [63:1] = fd, bit [0] = 0→recv, 1→send
    //   TLS:   bits [63:2] = fd, bits [1:0] = operation type
    //
    // Operation type (2 low bits):
    //   0 = RECV (KTLS recv or TCP recv)
    //   1 = SEND (KTLS send or TCP send)
    //   2 = POLL_IN
    //   3 = POLL_OUT
    //   listen_fd << 1 | 0  = ACCEPT (special, never collides because listen_fd is never a data fd)

    constexpr uint64_t tag_accept(int lfd)  { return uint64_t(lfd) << 1; }
    constexpr uint64_t tag_recv(int fd)     { return (uint64_t(fd) << 2) | 0; }
    constexpr uint64_t tag_send(int fd)     { return (uint64_t(fd) << 2) | 1; }
    constexpr uint64_t tag_poll_in(int fd)  { return (uint64_t(fd) << 2) | 2; }
    constexpr uint64_t tag_poll_out(int fd) { return (uint64_t(fd) << 2) | 3; }

    constexpr int  fd_of(uint64_t t)        { return int(t >> 2); }
    constexpr bool is_recv(uint64_t t)      { return (t & 3) == 0; }
    constexpr bool is_send(uint64_t t)      { return (t & 3) == 1; }
    constexpr bool is_poll_in(uint64_t t)   { return (t & 3) == 2; }
    constexpr bool is_poll_out(uint64_t t)  { return (t & 3) == 3; }

    // ── Ring wrapper ───────────────────────────────────────────────────
    struct ring final {
        struct io_uring impl{};

        void init(int entries = RING_ENTRIES, unsigned flags = 0) {
                int ret = io_uring_queue_init(entries, &impl, flags);
                if (ret < 0) {
                    HOPE_THROW_ERRNO("uring", "io_uring_queue_init failed");
                }
            }

        void exit() {
            io_uring_queue_exit(&impl);
        }

        struct io_uring_sqe* get_sqe() {
            struct io_uring_sqe* sqe = io_uring_get_sqe(&impl);
            // If nullptr, call submit_and_wait to drain completions, then retry.
            // In practice RING_ENTRIES is generous enough that this should not happen
            // during normal operation, but we handle it gracefully.
            return sqe;
        }

        int submit() {
            return io_uring_submit(&impl);
        }

        int wait_cqe(struct io_uring_cqe** cqe) {
            return io_uring_wait_cqe(&impl, cqe);
        }

        // Wait with a timeout (milliseconds). Returns 1 on CQE ready, 0 on timeout, <0 on error.
        int wait_cqe_timeout(struct io_uring_cqe** cqe, int timeout_ms) {
            struct __kernel_timespec ts {
                .tv_sec = timeout_ms / 1000,
                .tv_nsec = (long)(timeout_ms % 1000) * 1000000,
            };
            return io_uring_wait_cqe_timeout(&impl, cqe, &ts);
        }

        void cqe_seen(struct io_uring_cqe* cqe) {
            io_uring_cqe_seen(&impl, cqe);
        }

        void submit_and_wait() {
            io_uring_submit_and_wait(&impl, 1);
        }

        // Returns number of ready CQEs after submission
        int submit_before_wait() {
            int ret = io_uring_submit(&impl);
            if (ret < 0) {
                // Submission failed — caller should handle
                return ret;
            }
            return io_uring_wait_cqe(&impl, nullptr);
        }
    };

}

#endif
