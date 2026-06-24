/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/init.h"
#include "hope-io/net/tls/tls_stream.h"
#include "hope-io/net/tls/tls_server_stream.h"
#include "hope-io/net/tls/tls_acceptor_impl.h"
#include "hope-io/net/tls/ktls_enable.h"

#if PLATFORM_LINUX || PLATFORM_APPLE
#include "hope-io/net/nix/tcp_acceptor.h"
#include "hope-io/net/nix/tcp_stream.h"
#elif PLATFORM_WINDOWS
#include "hope-io/net/win/tcp_acceptor.h"
#include "hope-io/net/win/tcp_stream.h"
#endif
#include <array>
#include <unistd.h>

namespace hope::io {

    tls_server_stream::tls_server_stream(tcp_stream* tcp_stream, SSL_CTX* context, const stream_options& opts)
        : base_tls_stream(tcp_stream, opts) {
        m_context = context;
    }

    void tls_server_stream::connect(std::string_view, std::size_t) {
        assert(false);
    }

    void tls_server_stream::disconnect() {
        SSL_shutdown(m_ssl);
        SSL_free(m_ssl);
        m_tcp_stream->disconnect();
    }

    void tls_server_stream::accept_tls() {
        m_ssl = SSL_new(m_context);
        SSL_set_fd(m_ssl, m_tcp_stream->platform_socket());
        if (SSL_accept(m_ssl) <= 0) {
            throw std::runtime_error("hope-io/tls_server_stream: cannot accept tls connection");
        }

        // Attempt KTLS after successful handshake
        if (m_ktls_enabled) {
            try_enable_ktls();
        }
    }

    tls_acceptor_impl::tls_acceptor_impl(std::string_view key, std::string_view cert, const stream_options& opts)
        : m_key(key.data())
        , m_cert(cert.data())
        , m_opts(opts) {
        hope::io::init();
    }

    tls_acceptor_impl::~tls_acceptor_impl() {
    }

    void tls_acceptor_impl::open(std::size_t port) {
        auto* method = TLS_server_method();
        m_context = SSL_CTX_new(method);

        if (SSL_CTX_use_certificate_file(m_context, m_cert.c_str(), SSL_FILETYPE_PEM) <= 0) {
            throw std::runtime_error("hope-io/tls_acceptor: cannot create cert");
        }

        if (SSL_CTX_use_PrivateKey_file(m_context, m_key.c_str(), SSL_FILETYPE_PEM) <= 0 ) {
             throw std::runtime_error("hope-io/tls_acceptor: cannot create key");
        }

        m_tcp_acceptor = new tcp_acceptor(m_opts);
        m_tcp_acceptor->open(port);
    }

    void tls_acceptor_impl::close() {
        if (m_tcp_acceptor){
            m_tcp_acceptor->close();
        }
    }

    stream* tls_acceptor_impl::accept() {
        auto* tcp = m_tcp_acceptor->accept();
        auto* tls_stream = new tls_server_stream(static_cast<tcp_stream*>(tcp), m_context, m_opts);
        tls_stream->set_ktls_enabled(m_ktls_enabled);
        tls_stream->accept_tls();
        return tls_stream;
    }

    void tls_acceptor_impl::set_options(const stream_options& opt) {
        assert(m_tcp_acceptor);
        m_opts = opt;
        m_tcp_acceptor->set_options(opt);
    }

    long long tls_acceptor_impl::raw() const {
        return m_tcp_acceptor->raw();
    }

}
