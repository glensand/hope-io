/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "tls_init.h"

#include "openssl/ssl.h"

#include <cassert>

namespace hope::io {

    void init_tls() {
        // OPENSSL_init_ssl() is idempotent and thread-safe on OpenSSL >= 1.1.0.
        // It replaces the deprecated SSL_library_init() / SSL_load_error_strings() / OpenSSL_add_all_algorithms().
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG, nullptr);
    }

    void deinit_tls() {
        // OpenSSL >= 1.1.0 auto-cleans via atexit.
        // Explicit cleanup (OPENSSL_cleanup) is unsafe with multiple callers.
        // Keep as no-op for API compatibility.
    }

}
