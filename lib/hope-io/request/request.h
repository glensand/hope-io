/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#include "hope-io/net/factory.h"
#include "hope-io/net/stream.h"
#include <cstddef>
#include <string>
#include <string>
#include <regex>
#include <stdexcept>
#include <utility>
#include <format>

namespace hope::io::http {

    struct url_t final {
        std::string protocol; // "http" or "https"
        std::string hostname;
        std::string path;
        int port;
    };

    inline url_t extract_url(const std::string& url) {
        std::regex url_regex(
            R"((https?)://([^:/]+)(?::(\d+))?(/?[^?#]*)?)",
            std::regex_constants::icase
        );

        std::smatch matches;
        if (!std::regex_match(url, matches, url_regex)) {
            throw std::invalid_argument("Invalid URL format");
        }

        url_t parts;
        parts.protocol = matches[1].str();
        std::transform(
            parts.protocol.begin(), 
            parts.protocol.end(), 
            parts.protocol.begin(), 
            ::tolower
        );

        parts.hostname = matches[2].str();

        // Set default ports (80 for HTTP, 443 for HTTPS)
        if (matches[3].length() > 0) {
            parts.port = std::stoi(matches[3].str());
        } else {
            parts.port = (parts.protocol == "https") ? 443 : 80;
        }

        // Default path is "/" if not specified
        parts.path = matches[4].str().empty() ? "/" : matches[4].str();

        return parts;
    }

    inline std::string post(const std::string& endpoint, const std::string& payload, 
        const std::string& header_data = {}) {
        auto url = extract_url(endpoint);
        hope::io::stream* stream = nullptr;
        if (url.protocol == "http") {
            stream = hope::io::create_stream();
        } else {
            stream = hope::io::create_tls_stream();
        }
        auto request_header = std::format("POST {} HTTP/1.1\r\n"
                                             "Host: {}\r\n"
                                             "Content-Type: application/json;charset=UTF-8\r\n"
                                             "Content-Length: {}\r\n"
                                             "Connection: close\r\n",
                                             url.path,
                                             url.hostname,
                                             std::to_string(payload.size()));
        // if user provide additional data, we should include it to header
        if (!header_data.empty()) {
            request_header += header_data;
        }
        request_header += "\r\n";
        stream->connect(url.hostname, url.port);
        stream->write(request_header.data(), request_header.size());
        stream->write(payload.data(), payload.size());
        std::string buffer;
        stream->stream_in(buffer);
        stream->disconnect();
        delete stream;
        return buffer;
    }

    inline std::string get(const std::string& endpoint, const std::vector<std::pair<std::string, std::string>>& params, 
        const std::string& header_data = {}) {
        auto url = extract_url(endpoint);
        hope::io::stream* stream = nullptr;
        if (url.protocol == "http") {
            stream = hope::io::create_stream();
        } else {
            stream = hope::io::create_tls_stream();
        }
        std::string body;
        for (auto&& [k, v] : params) {
            body+= k;
            body.push_back('=');
            body += v;
            body.push_back('&');
        }
        body.pop_back();

        std::string request =
            "GET " + url.path + "?" + body +  " HTTP/1.1\r\n" +
            "Host: " + url.hostname + "\r\n" +
            header_data + 
            "Connection: close\r\n\r\n";
        std::cout<<request;
        stream->connect(url.hostname, url.port);
        stream->write(request.data(), request.size());
        std::string buffer;
        stream->stream_in(buffer);
        stream->disconnect();
        delete stream;
        return buffer;
    }

    inline std::string upload_file(const std::string& endpoint, std::string_view payload,
        const std::string file_name,
        const std::string& header_data = {}) {
        auto url = extract_url(endpoint);
        hope::io::stream* stream = nullptr;
        if (url.protocol == "http") {
            stream = hope::io::create_stream();
        } else {
            stream = hope::io::create_tls_stream();
        }

        const std::string boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
        const auto body_begin = std::format(
            "--{}\r\n"
            "Content-Disposition: form-data; name=\"file\"; filename=\"{}\"\r\n"
            "Content-Type: text/plain\r\n\r\n",
            boundary,
            file_name);
        const auto body_end = std::format("\r\n--{}--\r\n", boundary);
        const auto body_size = body_begin.size() + payload.size() + body_end.size();
        const auto request_begin = std::format("POST {} HTTP/1.1\r\n"
                                             "Host: {}\r\n"
                                             "User-Agent: RawUploader/1.0\r\n"
                                             "Content-Type: multipart/form-data; boundary={}\r\n"
                                             "Content-Length: {}\r\n"
                                             "Connection: close\r\n",
                                             url.path,
                                             url.hostname,
                                             boundary,
                                             body_size);
        stream->connect(url.hostname, url.port);
        stream->write(request_begin.data(), request_begin.size());
        stream->write(body_begin.data(), body_begin.size());
        stream->write(payload.data(), payload.size());
        stream->write(body_end.data(), body_end.size());
        std::string buffer;
        stream->stream_in(buffer);
        stream->disconnect();
        delete stream;
        return buffer;
    }
}
