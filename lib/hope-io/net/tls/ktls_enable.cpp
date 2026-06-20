/* Copyright (C) 2026 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/net/tls/ktls_enable.h"

#if defined(__linux__)

#include <linux/tls.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <cstring>
#include <vector>

namespace hope::io {

bool try_enable_fd_ktls(SSL* ssl, int fd, bool is_server) {
    if (!ssl || fd < 0) return false;

    const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl);
    if (!cipher) return false;

    size_t key_len = SSL_get_key_block_len(ssl);
    if (key_len == 0) return false;

    std::vector<uint8_t> key_block(key_len);
    SSL_generate_key_block(ssl, key_block.data(), key_len);

    // TLS 1.2 key block layout for AES-128-GCM (RFC 5246 §6.3):
    //   client_write_key (16) | server_write_key (16) |
    //   client_write_IV  (4)  | server_write_IV  (4)
    uint8_t* cw_key = key_block.data() + 0;
    uint8_t* sw_key = key_block.data() + 16;
    uint8_t* cw_iv  = key_block.data() + 32;
    uint8_t* sw_iv  = key_block.data() + 36;

    struct tls12_crypto_info_aes_gcm_128 tx_info, rx_info;
    std::memset(&tx_info, 0, sizeof(tx_info));
    std::memset(&rx_info, 0, sizeof(rx_info));

    auto make_info = [](struct tls12_crypto_info_aes_gcm_128* info,
                        const uint8_t* key, const uint8_t* salt)
    {
        info->info.version = TLS_1_2_VERSION;
        info->info.cipher_type = TLS_CIPHER_AES_GCM_128;
        std::memcpy(info->key, key, 16);
        std::memcpy(info->salt, salt, 4);
    };

    if (is_server) {
        // Server: TX = server_write, RX = client_write
        make_info(&tx_info, sw_key, sw_iv);
        make_info(&rx_info, cw_key, cw_iv);
    } else {
        // Client: TX = client_write, RX = server_write
        make_info(&tx_info, cw_key, cw_iv);
        make_info(&rx_info, sw_key, sw_iv);
    }

    if (setsockopt(fd, SOL_TCP, TCP_ULP, "tls", sizeof("tls")) < 0) {
        return false;
    }

    if (setsockopt(fd, SOL_TLS, TLS_TX, &tx_info, sizeof(tx_info)) < 0) {
        return false;
    }

    if (setsockopt(fd, SOL_TLS, TLS_RX, &rx_info, sizeof(rx_info)) < 0) {
        return false;
    }

    return true;
}

}
#else

namespace hope::io {

bool try_enable_fd_ktls(SSL* /*ssl*/, int /*fd*/, bool /*is_server*/) {
    return false;
}

}
#endif
