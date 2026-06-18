/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include "hope-io/net/tls/tls_init.h"
#include "hope-io/net/tls/tls_stream.h"
#include "hope-io/net/tls/tls_server_stream.h"
#include "hope-io/net/tls/tls_acceptor_impl.h"
#include <array>
#include <unistd.h>

#ifdef HOPE_IO_USE_OPENSSL

namespace hope::io {

    tls_server_stream::tls_server_stream(tcp_stream* tcp_stream, SSL_CTX* context)
        : base_tls_stream(tcp_stream) {
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
    }

    tls_acceptor_impl::tls_acceptor_impl(std::string_view key, std::string_view cert)
        : m_key(key.data())
        , m_cert(cert.data()) {
        init_tls();
    }

    tls_acceptor_impl::~tls_acceptor_impl() {
        deinit_tls();
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

        m_tcp_acceptor = create_acceptor();
        m_tcp_acceptor->open(port);
    }

    void tls_acceptor_impl::close() {
        if (m_tcp_acceptor){
            m_tcp_acceptor->close();
        }
    }

    stream* tls_acceptor_impl::accept() {
        auto* tcp = m_tcp_acceptor->accept();
        auto* tls_stream = new tls_server_stream(static_cast<tcp_stream*>(tcp), m_context);
        tls_stream->accept_tls();
        return tls_stream;
    }

    void tls_acceptor_impl::set_options(const stream_options& opt) {
        assert(m_tcp_acceptor);
        m_tcp_acceptor->set_options(opt);
    }

    long long tls_acceptor_impl::raw() const {
        return m_tcp_acceptor->raw();
    }

    acceptor* create_tls_acceptor(std::string_view key, std::string_view cert) {
        return new tls_acceptor_impl(key, cert);
    }

}

#else

namespace hope::io {

    acceptor* create_tls_acceptor(std::string_view, std::string_view) {
        assert(false && "hope-io/ OpenSSL is not available");
        return nullptr;
    }

}

#endif
