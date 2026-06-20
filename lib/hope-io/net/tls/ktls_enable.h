/* Copyright (C) 2026 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "openssl/ssl.h"

namespace hope::io {

/// Try to enable kernel TLS (KTLS) on a connected fd after a successful TLS handshake.
/// @param ssl  The SSL object after a successful handshake (SSL_connect or SSL_accept).
/// @param fd   The connected socket fd.
/// @param is_server  true if called on the server side (affects TX/RX key ordering).
/// Returns true if KTLS was enabled successfully, false on unsupported kernels/ciphers.
/// Only meaningful on Linux with TCP_ULP support.
bool try_enable_fd_ktls(SSL* ssl, int fd, bool is_server);

/// Returns true if the platform supports KTLS at compile time.
constexpr bool is_ktls_supported() {
#if defined(__linux__)
    return true;
#else
    return false;
#endif
}

}
