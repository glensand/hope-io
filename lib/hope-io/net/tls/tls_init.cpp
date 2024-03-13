#include "tls_init.h"

#include "openssl/ssl.h"
#include "openssl/err.h"
#include <mutex>

namespace hope::io {

    static std::mutex guard;
    static int initialized = 0;
    
    void init_tls() {
        std::lock_guard lock(guard);
        if (initialized == 0){
            SSL_library_init();
	        SSL_load_error_strings();
	        OpenSSL_add_all_algorithms();
        }
        ++initialized;
    }

    void deinit_tls() {
        std::lock_guard lock(guard);
        --initialized;
        if (initialized == 0){
            ERR_free_strings();
	        EVP_cleanup();
        }
    }

}
