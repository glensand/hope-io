#include <algorithm>
#include <array>
#include <ranges>
#include <unordered_map>

#include "websockets.h"
#include "hope-io/net/tls/tls_stream.h"

#ifdef HOPE_IO_USE_OPENSSL

namespace {

    class client_tls_websockets_stream final : public hope::io::base_tls_stream {
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

            host = ip;
        }

        virtual void write(const void* data, std::size_t length) override {
            assert(!accept_handshake && "Already accepted");

            const std::string_view request(static_cast<std::string_view::const_pointer>(data), length);
            {
                auto&& handshake_header = hope::io::websockets::generate_handshake(host, request.data());
                base_tls_stream::write(handshake_header.data(), handshake_header.length());
            }
            {
                std::array<char, 8192> header_buffer;
                if (const auto read_bytes = base_tls_stream::read_bytes(header_buffer.data(), header_buffer.size()))
                {
                    accept_handshake = hope::io::websockets::validate_handshake_response(header_buffer.data(), read_bytes);
                }
            }
        }

        virtual void stream_in(std::string& out_stream) override {
            assert(accept_handshake && "Need to accept a handshake to read packages");

            auto&& frame = hope::io::websockets::read_frame([&](void* buffer, std::size_t length) {
	            return SSL_read(m_ssl, buffer, length);
            });


            // TODO: Critical (Process others frames (eof, long messages, ping/pong))
            if (!frame.complete_stream() || frame.header.op_code != hope::io::websockets::OPCODE_TEXT) {
	            return;
            }

            size_t package_length = frame.length;

            std::array<char, 1024> read_buffer;
            while (package_length > 0) {
                const size_t read_chunk = std::min<size_t>(package_length, read_buffer.size());
                const size_t read_bytes = base_tls_stream::/**/read_bytes(read_buffer.data(), read_chunk);

                out_stream.append(read_buffer.data(), read_bytes);

                package_length = package_length < read_bytes ? 0 : package_length - read_bytes;
            }
        }

        virtual void disconnect() override {
            base_tls_stream::disconnect();
            SSL_CTX_free(m_context);
        }

    private:
        std::string host;
        bool accept_handshake = false;
    };

}

namespace hope::io {

    stream* create_tls_websockets_stream(stream* tcp_stream) {
        return new client_tls_websockets_stream(tcp_stream);
    }

}

#else

stream* create_tls_websockets_stream(stream* tcp_stream) {
    assert(false && "hope-io/ OpenSSL is not available");
    return nullptr;
}

#endif