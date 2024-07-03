#include "tls_init.h"

#ifdef HOPE_IO_USE_OPENSSL
#include "openssl/ssl.h"
#include "openssl/err.h"
#endif

#include <mutex>
#include <cassert>

namespace hope::io {

    static std::mutex guard;
    static int initialized = 0;
    
    void init_tls() {
#ifdef HOPE_IO_USE_OPENSSL
        std::lock_guard lock(guard);
        if (initialized == 0){
            SSL_library_init();
	        SSL_load_error_strings();
	        OpenSSL_add_all_algorithms();
        }
        ++initialized;
#else
        assert(false && "hope-io/ OpenSSL is not available");
#endif
    }

    void deinit_tls() {
#ifdef HOPE_IO_USE_OPENSSL
        std::lock_guard lock(guard);
        --initialized;
        if (initialized == 0){
            ERR_free_strings();
	        EVP_cleanup();
        }
#else
        assert(false && "hope-io/ OpenSSL is not available");
#endif
    }

}
