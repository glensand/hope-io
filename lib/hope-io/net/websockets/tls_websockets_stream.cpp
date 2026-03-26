/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#if WEBSOCK_ENABLE

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <random>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

#include "websockets.h"
#include "hope-io/net/tls/tls_stream.h"

#ifdef HOPE_IO_USE_OPENSSL

#include <openssl/err.h>

namespace {

    class client_tls_websockets_stream final : public hope::io::base_tls_stream {
    public:
        using base_tls_stream::base_tls_stream;

        virtual void connect(std::string_view ip, std::size_t port) override {
            m_tcp_stream->connect(ip, port);
            auto* context_method = TLS_client_method();
            m_context = SSL_CTX_new(context_method);
            if (m_context == nullptr) {
                throw std::runtime_error("hope-io/client_tls_websockets_stream: cannot create context");
            }
            m_ssl = SSL_new(m_context);
            SSL_set_fd(m_ssl, (int32_t)m_tcp_stream->platform_socket());

            // Many websocket endpoints require SNI during TLS handshake.
            host = ip;
            SSL_set_tlsext_host_name(m_ssl, host.c_str());

            // Avoid stale errors from other concurrent TLS handshakes confusing this connect.
            ERR_clear_error();
            if (SSL_connect(m_ssl) <= 0) {
                throw std::runtime_error("hope-io/client_tls_websockets_stream: cannot establish connection");
            }
        }

        virtual void write(const void* data, std::size_t length) override {
            if (!accept_handshake) {
                // First `write()` is treated as websocket URI (GET <uri> ...).
                const std::string_view request(static_cast<std::string_view::const_pointer>(data), length);
                const std::string uri(request);

                const auto&& handshake_header = hope::io::websockets::generate_handshake(host, uri);
                base_tls_stream::write(handshake_header.data(), handshake_header.length());

                // Read handshake response until HTTP headers are complete.
                // Some servers split the response across multiple TLS records.
                std::string header;
                header.reserve(512);
                std::array<char, 2> tmp{};
                while (header.find("\r\n\r\n") == std::string::npos && header.size() < 8192) {
                    // base_tls_stream::read_once() reads up to (length - 1), so pass 2 to read a single byte.
                    const auto read_bytes = base_tls_stream::read_once(tmp.data(), tmp.size());
                    if (read_bytes == 0) {
                        break;
                    }
                    header.push_back(tmp[0]);
                }

                if (!header.empty()) {
                    accept_handshake = hope::io::websockets::validate_handshake_response(header.data(), header.size());
                }
                return;
            }

            // After handshake: send a websocket TEXT frame (client-to-server frames must be masked).
            send_frame(hope::io::websockets::OPCODE_TEXT, data, length);
        }

        virtual void stream_in(std::string& out_stream) override {
            if (!accept_handshake) {
                // Handshake validation failed; let the caller retry/ignore without crashing.
                no_more_data = true;
                return;
            }

            if (no_more_data) {
                return;
            }

            // Keep reading websocket frames until we either:
            //  - produce a complete TEXT message for the caller, or
            //  - detect stream termination.
            bool assembling_text = false;
            while (true) {
                auto frame = hope::io::websockets::read_frame(this);
                if (frame.empty()) {
                    no_more_data = true;
                    return;
                }

                // Control frames (ping/pong/close) are handled internally.
                if (frame.control()) {
                    if (frame.header.op_code == hope::io::websockets::OPCODE_PING) {
                        // Read ping payload (if any) and reply with pong carrying same payload.
                        const std::size_t payload_len = frame.length;
                        std::string payload;
                        payload.resize(payload_len);

                        std::size_t total_read = 0;
                        std::array<uint8_t, 1024> tmp{};
                        while (total_read < payload_len) {
                            const std::size_t read_chunk = std::min<std::size_t>(payload_len - total_read, tmp.size());
                            const std::size_t read_bytes = base_tls_stream::read_bytes(tmp.data(), read_chunk);
                            if (read_bytes == 0) {
                                throw std::runtime_error("hope-io/tls_websockets_stream: unexpected EOF reading ws ping payload");
                            }
                            if (frame.masked()) {
                                for (std::size_t i = 0; i < read_bytes; ++i) {
                                    tmp[i] = tmp[i] ^ frame.mask[i % frame.mask.size()];
                                }
                            }
                            std::memcpy(payload.data() + total_read, tmp.data(), read_bytes);
                            total_read += read_bytes;
                        }

                        send_frame(hope::io::websockets::OPCODE_PONG, payload.data(), payload.size());
                        continue;
                    }

                    // Ignore pong; close ends the stream.
                    if (frame.header.op_code == hope::io::websockets::OPCODE_CLOSE) {
                        no_more_data = true;
                        return;
                    }

                    // For any other control frame: discard payload and continue.
                    discard_payload(frame.length, frame);
                    continue;
                }

                // Non-control frames.
                if (!assembling_text) {
                    // Single-frame message.
                    if (frame.complete_stream() && frame.header.op_code == hope::io::websockets::OPCODE_TEXT) {
                        read_text_payload(out_stream, frame);
                        return;
                    }

                    // Start fragmented message.
                    if (frame.begin_stream() && frame.header.op_code == hope::io::websockets::OPCODE_TEXT) {
                        out_stream.clear();
                        read_text_payload(out_stream, frame);
                        assembling_text = true;
                        continue;
                    }

                    // Unsupported non-text/non-start fragment.
                    discard_payload(frame.length, frame);
                    continue;
                }

                // We are assembling a fragmented TEXT message.
                if (frame.continue_stream() || frame.end_stream()) {
                    read_text_payload(out_stream, frame);
                    if (frame.end_stream()) {
                        return;
                    }
                    continue;
                }

                // Unexpected frame type while assembling: discard and reset.
                discard_payload(frame.length, frame);
                assembling_text = false;
            }
        }

        virtual void disconnect() override {
            base_tls_stream::disconnect();
            SSL_CTX_free(m_context);
            accept_handshake = false;
        }

    private:
        std::string host;
        bool accept_handshake = false;
        bool no_more_data = false;

        void discard_payload(std::size_t payload_len, const hope::io::websockets::websocket_frame& frame) {
            (void)frame; // payload is server-to-client data; we just discard it.
            std::array<uint8_t, 1024> tmp{};
            std::size_t remaining = payload_len;
            while (remaining > 0) {
                const std::size_t chunk = std::min<std::size_t>(remaining, tmp.size());
                const std::size_t read_bytes = base_tls_stream::read_bytes(tmp.data(), chunk);
                if (read_bytes == 0) {
                    throw std::runtime_error("hope-io/tls_websockets_stream: unexpected EOF discarding ws payload");
                }
                remaining -= read_bytes;
            }
        }

        void read_text_payload(std::string& out_stream, const hope::io::websockets::websocket_frame& frame) {
            std::size_t package_length = frame.length;
            std::array<uint8_t, 1024> read_buffer{};

            while (package_length > 0) {
                const std::size_t read_chunk = std::min<std::size_t>(package_length, read_buffer.size());
                const std::size_t read_bytes = base_tls_stream::read_bytes(read_buffer.data(), read_chunk);
                if (read_bytes == 0) {
                    throw std::runtime_error("hope-io/tls_websockets_stream: unexpected EOF reading ws text payload");
                }

                if (frame.masked()) {
                    for (std::size_t i = 0; i < read_bytes; ++i) {
                        read_buffer[i] = read_buffer[i] ^ frame.mask[i % frame.mask.size()];
                    }
                }

                out_stream.append(reinterpret_cast<char*>(read_buffer.data()), read_bytes);
                package_length = package_length < read_bytes ? 0 : package_length - read_bytes;
            }
        }

        void send_frame(std::uint8_t opcode, const void* data, std::size_t length) {
            // Websocket client-to-server frames MUST be masked.
            std::array<std::uint8_t, 4> mask{};
            {
                static thread_local std::mt19937 rng{ std::random_device{}() };
                std::uniform_int_distribution<std::uint32_t> dist(0, 0xFF);
                for (auto& b : mask) {
                    b = static_cast<std::uint8_t>(dist(rng));
                }
            }

            const auto* payload = static_cast<const std::uint8_t*>(data);

            std::vector<std::uint8_t> out;
            out.reserve(2 + 8 + 4 + length);

            // Byte 1: FIN=1, RSV=0, opcode.
            out.push_back(static_cast<std::uint8_t>(0x80u | (opcode & 0x0Fu)));

            // Byte 2: MASK=1 + payload length (or length marker).
            if (length <= 125u) {
                out.push_back(static_cast<std::uint8_t>(0x80u | static_cast<std::uint8_t>(length)));
            } else if (length <= 0xFFFFu) {
                out.push_back(static_cast<std::uint8_t>(0x80u | 126u));
                out.push_back(static_cast<std::uint8_t>((length >> 8u) & 0xFFu));
                out.push_back(static_cast<std::uint8_t>(length & 0xFFu));
            } else {
                out.push_back(static_cast<std::uint8_t>(0x80u | 127u));
                for (int i = 7; i >= 0; --i) {
                    out.push_back(static_cast<std::uint8_t>((static_cast<std::uint64_t>(length) >> (8u * i)) & 0xFFu));
                }
            }

            // Masking key
            out.insert(out.end(), mask.begin(), mask.end());

            // Mask payload
            if (length > 0u) {
                for (std::size_t i = 0; i < length; ++i) {
                    out.push_back(static_cast<std::uint8_t>(payload[i] ^ mask[i % mask.size()]));
                }
            }

            base_tls_stream::write(out.data(), out.size());
        }

    };

}

namespace hope::io {

    stream* create_tls_websockets_stream(stream* tcp_stream) {
        return new client_tls_websockets_stream(tcp_stream);
    }

}

#else

namespace hope::io {

    stream* create_tls_websockets_stream(stream* tcp_stream) {
        assert(false && "hope-io/ OpenSSL is not available");
        return nullptr;
    }

}
#endif

#endif
