#include "tls_stream.h"

#ifdef HOPE_IO_USE_OPENSSL

namespace {

    class client_tls_stream final : public hope::io::base_tls_stream {
    public:
        using base_tls_stream::base_tls_stream;

        virtual void connect(std::string_view ip, std::size_t port) override {
            m_tcp_stream->connect(ip, port);
            auto* context_method = TLS_client_method();
            m_context = SSL_CTX_new(context_method);
            if (m_context == nullptr) {
                throw std::runtime_error("hope-io/client_tls_stream: cannot create context");
            }
            m_ssl = SSL_new(m_context);
            SSL_set_fd(m_ssl, (int32_t)m_tcp_stream->platform_socket());

            if (SSL_connect(m_ssl) <= 0) {
                throw std::runtime_error("hope-io/client_tls_stream: cannot establish connection");
            }
        }

        virtual void disconnect() override {
            base_tls_stream::disconnect();
            SSL_CTX_free(m_context);
        }
    };

}

namespace hope::io {

    stream* create_tls_stream(stream* tcp_stream) {
        return new client_tls_stream(tcp_stream);
    }

}

#else

    stream* create_tls_stream(stream* tcp_stream) {
        assert(false && "hope-io/ OpenSSL is not available");
        return nullptr;
    }

#endif