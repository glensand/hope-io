/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "tls_init.h"

#ifdef HOPE_IO_USE_OPENSSL
#include "openssl/ssl.h"
#include "openssl/err.h"
#endif

#include <mutex>
#include <cassert>

namespace hope::io {

    static std::mutex tls_guard;
    static int tls_initialized = 0;
    
    void init_tls() {
#ifdef HOPE_IO_USE_OPENSSL
        std::lock_guard lock(tls_guard);
        if (tls_initialized == 0){
            SSL_library_init();
	        SSL_load_error_strings();
	        OpenSSL_add_all_algorithms();
        }
        ++tls_initialized;
#else
        assert(false && "hope-io/ OpenSSL is not available");
#endif
    }

    void deinit_tls() {
#ifdef HOPE_IO_USE_OPENSSL
        std::lock_guard lock(tls_guard);
        --tls_initialized;
        if (tls_initialized == 0){
            ERR_free_strings();
	        EVP_cleanup();
        }
#else
        assert(false && "hope-io/ OpenSSL is not available");
#endif
    }

}
