/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/coredefs.h"

#if WEBSOCK_ENABLE

#include <cassert>

#include "websockets.h"
#include "hope-io/net/stream.h"

#include <random>
#include <unordered_map>
#include <ranges>
#include <algorithm>
#include <cctype>

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
}

namespace hope::io::websockets {
    std::string build_request(
        const std::string& uri,
        const std::string& web_version,
        const std::string& host,
        const std::string& generated_key,
        const std::string& socket_version)
    {
        std::string request;

        request.reserve(256); // optional, avoids reallocations

        request += "GET ";
        request += uri;
        request += " ";
        request += web_version;
        request += "\r\nHost: ";
        request += host;
        request += "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ";
        request += generated_key;
        request += "\r\nSec-WebSocket-Version: ";
        request += socket_version;
        request += "\r\n\r\n";

        return request;
    }

	std::string generate_handshake(const std::string& host, const std::string& uri) {
        constexpr auto web_version = "HTTP/1.1";
        constexpr auto socket_version = "13";

        constexpr auto key_length = 0x10;

        const auto generated_key = base64_key_encode(key_length);
        return build_request(uri, web_version, host, generated_key, socket_version);
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
            if (it == headers.cend()) {
                return false;
            }

            // Case-insensitive substring compare for websocket handshake values.
            const auto a = it->second;
            if (a.empty() || value.empty()) {
                return false;
            }

            auto to_lower = [](std::string_view s) {
                std::string out;
                out.resize(s.size());
                for (std::size_t i = 0; i < s.size(); ++i) {
                    out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
                }
                return out;
            };

            const auto a_lower = to_lower(a);
            const auto v_lower = to_lower(value);
            return a_lower.find(v_lower) != std::string::npos;
        };

        static auto&& check_header_has_value = [](auto&& headers, auto&& key)
        {
            auto&& it = headers.find(key);
            return it != headers.cend() && !it->second.empty();
        };

        // HTTP header names are case-insensitive; some CDNs send sec-websocket-accept in lowercase.
        static auto&& check_header_has_value_ci = [](const auto& headers, std::string_view key_lower_ascii)
        {
            for (const auto& kv : headers) {
                if (kv.first.size() != key_lower_ascii.size()) {
                    continue;
                }
                bool match = true;
                for (std::size_t i = 0; i < key_lower_ascii.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(kv.first[i]))
                        != static_cast<unsigned char>(key_lower_ascii[i])) {
                        match = false;
                        break;
                    }
                }
                if (match && !kv.second.empty()) {
                    return true;
                }
            }
            return false;
        };

        auto&& headers = split_headers(response);

        using namespace std::literals;
        if (check_header_has_value(headers, "Sec-WebSocket-Accept"sv)
            || check_header_has_value_ci(headers, "sec-websocket-accept")) {
            return true;
        }

        // Fallback: some proxies/CDNs produce header lines that split_headers() misses; scan raw bytes.
        constexpr std::string_view needle = "sec-websocket-accept";
        for (std::size_t i = 0; i + needle.size() + 1u < response.size(); ++i) {
            bool match = true;
            for (std::size_t j = 0; j < needle.size(); ++j) {
                if (std::tolower(static_cast<unsigned char>(response[i + j]))
                    != static_cast<unsigned char>(needle[j])) {
                    match = false;
                    break;
                }
            }
            if (match && response[i + needle.size()] == ':') {
                return true;
            }
        }

        return false;
	}

    websocket_frame read_frame(stream* stream) {
		assert(stream && "stream must be valid");
        websocket_frame frame;
        std::array<std::uint8_t, websocket_frame::header_size> header_bytes{};
        if (stream->read(header_bytes.data(), header_bytes.size()) == header_bytes.size()) {
            const std::uint8_t byte1 = header_bytes[0];
            const std::uint8_t byte2 = header_bytes[1];

            // Assign bitfields explicitly to avoid compiler/endianness differences.
            frame.header.op_code = static_cast<std::uint8_t>(byte1 & 0x0Fu);
            frame.header.flags = static_cast<std::uint8_t>((byte1 >> 4u) & 0x07u);
            frame.header.is_eof = static_cast<std::uint8_t>((byte1 >> 7u) & 0x01u);
            frame.header.package_length = static_cast<std::uint8_t>(byte2 & 0x7Fu);
            frame.header.mask = static_cast<std::uint8_t>((byte2 >> 7u) & 0x01u);

            static auto&& read_package_length = [&](size_t& out_package_length) {
                std::uint8_t extra_length_bytes = 0;
                const auto package_length = frame.header.package_length;
                if (package_length == 126u) {
                    extra_length_bytes = 0x2;
                }
                else if (package_length == 127u) {
                    extra_length_bytes = 0x8;
                }

                out_package_length = package_length;

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
                else {
                    out_package_length = package_length;
                }

                return true;
                };

            size_t package_length;
            if (read_package_length(package_length)) {
                frame.length = package_length;
            }

            if (frame.masked()) {
                stream->read(frame.mask.data(), frame.mask.size());
            }
        }

        return frame;
    }

}
#endif
