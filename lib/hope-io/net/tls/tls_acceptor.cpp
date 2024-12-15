#include "hope-io/net/acceptor.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"
#include "hope-io/net/tls/tls_init.h"
#include "hope-io/net/tls/tls_stream.h"

#ifdef HOPE_IO_USE_OPENSSL

namespace {

    class tls_server_stream final : public hope::io::base_tls_stream {
    public:
        tls_server_stream(hope::io::stream* tcp_stream, SSL_CTX* context)
            : base_tls_stream(tcp_stream) {
            m_context = context;
        }

        virtual void connect(std::string_view ip, std::size_t port) override {
            assert(false);
        }

        virtual void disconnect() override {
            SSL_shutdown(m_ssl);
            SSL_free(m_ssl);
            m_tcp_stream->disconnect();
        }

        void accept_tls() {
            m_ssl = SSL_new(m_context);
            SSL_set_fd(m_ssl, m_tcp_stream->platform_socket());
            if (SSL_accept(m_ssl) <= 0) {
                throw std::runtime_error("hope-io/tls_server_stream: cannot accept tls connection");
            }
        }
    };

    class tls_acceptor final : public hope::io::acceptor {
    public:
        tls_acceptor(std::string_view key, std::string_view cert) 
            : m_key(key.data())
            , m_cert(cert.data()) {
            hope::io::init_tls();
        }

        virtual ~tls_acceptor() override {
            hope::io::deinit_tls();
        }

    private:
        virtual void open(std::size_t port) override {
            auto* method = TLS_server_method();
            m_context = SSL_CTX_new(method);

            if (SSL_CTX_use_certificate_file(m_context, m_cert.c_str(), SSL_FILETYPE_PEM) <= 0) {
                throw std::runtime_error("hope-io/tls_acceptor: cannot create cert");
            }

            if (SSL_CTX_use_PrivateKey_file(m_context, m_key.c_str(), SSL_FILETYPE_PEM) <= 0 ) {
                 throw std::runtime_error("hope-io/tls_acceptor: cannot create key");
            }

            m_tcp_acceptor = hope::io::create_acceptor();
            m_tcp_acceptor->open(port);
        }

        virtual hope::io::stream* accept() override {
            auto* tcp_stream = m_tcp_acceptor->accept();
            auto* tls_stream = new tls_server_stream(tcp_stream, m_context);
            tls_stream->accept_tls();
            return tls_stream;
        }

        virtual void set_options(const hope::io::stream_options& opt) override {
            assert(m_tcp_acceptor);
            m_tcp_acceptor->set_options(opt);
        }

        std::string m_key;
        std::string m_cert;

        hope::io::acceptor* m_tcp_acceptor{ nullptr };
        SSL_CTX* m_context{ nullptr };
    };
}

namespace hope::io {

    acceptor* create_tls_acceptor(std::string_view key, std::string_view cert) {
        return new tls_acceptor(key, cert);
    }
    
}

#else

namespace hope::io {

    acceptor* create_tls_acceptor(std::string_view key, std::string_view cert) {
        assert(false && "hope-io/ OpenSSL is not available");
        return nullptr;
    }

}

#endif
