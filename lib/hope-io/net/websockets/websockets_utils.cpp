#include "hope-io/coredefs.h"

#if WEBSOCK_ENABLE

#include <cassert>
#include <format>

#include "websockets.h"
#include "hope-io/net/stream.h"

#include <random>
#include <unordered_map>
#include <ranges>
#include <algorithm>
#include <sstream>

#ifdef HOPE_IO_USE_OPENSSL
#include "openssl/bio.h"
#include "openssl/buffer.h"
#include "openssl/evp.h"
#endif

namespace {
    std::string random_bytes(const size_t bytes) {
    	thread_local std::random_device rd;
        thread_local std::mt19937 generator(rd());

        std::string out_result;
        out_result.reserve(bytes);

        using namespace std::literals;
        constexpr auto values = "0123456789abcdefABCDEFGHIJKLMNOPQRSTUVEXYZ"sv;

        std::uniform_int_distribution<> distribution(0, values.length() - 1);

        for (size_t i = 0; i < bytes; i++) {
            out_result += values[distribution(generator)];
        }

        return out_result;
    }

    std::string base64_encode(const std::string& input) {
#ifdef HOPE_IO_USE_OPENSSL
        BUF_MEM* bufferPtr;

        BIO* b64 = BIO_new(BIO_f_base64());
        BIO* bio = BIO_new(BIO_s_mem());
        bio = BIO_push(b64, bio);

        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // Ignore newlines - write everything in one line
        BIO_write(bio, input.c_str(), input.length());
        BIO_flush(bio);
        BIO_get_mem_ptr(bio, &bufferPtr);
        BIO_set_close(bio, BIO_NOCLOSE);
        BIO_free_all(bio);

        std::string encoded(bufferPtr->data, bufferPtr->length);
        BUF_MEM_free(bufferPtr);
        return encoded;
#else
        assert(false && "hope-io/ OpenSSL is not available");
        return {};
#endif
    }

    std::string base64_key_encode(size_t length) {
        return base64_encode(random_bytes(length));
    }

    constexpr auto minimal_package_content_length = 126u;

    size_t read_content_length(hope::io::stream* stream, uint8_t package_length)
    {
        size_t out_package_length = package_length;

        std::uint8_t extra_length_bytes = 0;
        if (package_length == minimal_package_content_length) {
            extra_length_bytes = 0x2;
        }
        else if (package_length == 127u) {
            extra_length_bytes = 0x8;
        }

        if (extra_length_bytes > 0) {
            std::vector<std::uint8_t> data_length_buffer(extra_length_bytes);
            if (stream->read(data_length_buffer.data(), data_length_buffer.size()) != data_length_buffer.size()) {
                return false;
            }

            out_package_length = 0ull;
            for (auto&& i = 0; i < extra_length_bytes; i++) {
                out_package_length = (out_package_length << 0x8) + data_length_buffer[i];
            }
        }

        return out_package_length;
    }
}

namespace hope::io::websockets {
	std::string generate_handshake(const std::string& host, const std::string& uri) {
        constexpr auto web_version = "HTTP/1.1";
        constexpr auto socket_version = "13";

        constexpr auto key_length = 0x10;

        constexpr auto request_format =
            "GET {} {}\r\n"
            "Host: {}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: {}\r\n"
            "Sec-WebSocket-Version: {}\r\n"
            "\r\n";

        const auto generated_key = base64_key_encode(key_length);

        return std::format(request_format, uri, web_version, host, generated_key, socket_version);
	}

    bool validate_handshake_response(const void* data, std::size_t length) {
        assert(length > 0 && "Invalid data");

        const std::string_view response(static_cast<std::string_view::const_pointer>(data), length);

        static auto&& split_headers = [](const std::string_view& in_value, char in_delimiter = '\n') {
            std::unordered_map<std::string_view, std::string_view> out_values;
            for (const auto value : std::views::split(in_value, in_delimiter)) {
                const std::string_view key_value(value.begin(), value.end());
                if (key_value.length() > 1) {
                    const auto key_value_separator_index = key_value.find_first_of(':');
                    if (key_value_separator_index != std::string_view::npos) {
                        const auto value_source_offset = std::min<size_t>(key_value_separator_index + 2, key_value.length() - 1);

                        const std::string_view result_key(key_value.data(), key_value_separator_index);
                        const std::string_view result_value(key_value.data() + value_source_offset, key_value.length() - value_source_offset - 1);

                        out_values.insert({ result_key, result_value });
                    }
                }
            }
            return out_values;
        };

        static auto&& check_header_value = [](auto&& headers, auto&& key, auto&& value)
        {
            auto&& it = headers.find(key);
            return it != headers.cend() && std::ranges::equal(it->second, value);
        };

        static auto&& check_header_has_value = [](auto&& headers, auto&& key)
        {
            auto&& it = headers.find(key);
            return it != headers.cend() && !it->second.empty();
        };

        auto&& headers = split_headers(response);

        using namespace std::literals;
        if (!check_header_value(headers, "Connection"sv, "upgrade"sv)) {
            return false;
        }
        if (!check_header_value(headers, "Upgrade"sv, "websocket"sv)) {
            return false;
        }
        if (!check_header_has_value(headers, "Sec-WebSocket-Accept"sv)) {
            return false;
        }

        return true;
	}

    websocket_frame read_frame(stream* stream) {
		assert(stream && "stream must be valid");
        websocket_frame frame{};
        if (stream->read(&frame.header, websocket_frame::header_size) == websocket_frame::header_size) {
			
            frame.length = read_content_length(stream, frame.header.package_length);

            if (frame.masked()) {
                stream->read(frame.mask.data(), frame.mask.size());
            }
        }

        return frame;
    }

    size_t read_data(const websocket_frame& frame, stream* stream, std::string& out_data) {
        size_t package_length = frame.length;

        std::array<uint8_t, 1024> read_buffer;
        while (package_length > 0) {
            const size_t read_chunk = std::min<size_t>(package_length, read_buffer.size());
            const size_t read_bytes = stream->read(read_buffer.data(), read_chunk);

            if (frame.masked()) {
                for (size_t i = 0; i < read_bytes; ++i) {
                    read_buffer[i] = read_buffer[i] ^ frame.mask[i % frame.mask.size()];
                }
            }

            out_data.append(reinterpret_cast<char*>(read_buffer.data()), read_bytes);

            package_length = package_length < read_bytes ? 0 : package_length - read_bytes;
        }

        return frame.length - package_length;
    }

    std::string generate_package(const std::string& data, opcode_e code, bool is_eof, bool masked) {
		    std::stringstream out_package;
        assert(data.size() < minimal_package_content_length && "Supports small content only");
        if (data.size() < minimal_package_content_length) {
            websocket_frame::header_t header{};
            header.op_code = cast_opcode(code);
            header.is_eof = is_eof;
            header.package_length = static_cast<uint8_t>(data.size());
            header.mask = masked;
            out_package << std::string_view(reinterpret_cast<const char*>(&header), websocket_frame::header_size);
        }
        assert(!masked && "Doesn't support masked");
        out_package << data;
        return out_package.str();
    }
}
#endif
