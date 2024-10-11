/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/net/stream.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <filesystem>

// Port to listen on
const int PORT = 8080;

// Frontend directory
const std::string FRONTEND_DIR = "onpoint";

// Get the MIME type based on file extension
std::string get_content_type(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".css")) return "text/css";
    if (path.ends_with(".js")) return "application/javascript";
    if (path.ends_with(".png")) return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".gif")) return "image/gif";
    if (path.ends_with(".svg")) return "image/svg+xml";
    if (path.ends_with(".ico")) return "image/x-icon";
    return "application/octet-stream";
}

// Read the entire contents of a file into a string
std::string read_file(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open file: " + file_path);
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

// URL-decode a string
std::string url_decode(const std::string& url_s) {
    std::string out;
    out.clear();
    out.reserve(url_s.size());
    for (std::size_t i = 0; i < url_s.size(); ++i) {
        if (url_s[i] == '%') {
            if (i + 3 <= url_s.size()) {
                int value = 0;
                std::istringstream is(url_s.substr(i + 1, 2));
                if (is >> std::hex >> value) {
                    out += static_cast<char>(value);
                    i += 2;
                } else {
                    throw std::invalid_argument("Invalid Hex number");
                }
            } else {
                throw std::invalid_argument("Invalid URL token");
            }
        }
        else if (url_s[i] == '+') {
            out += ' ';
        } else {
            out += url_s[i];
        }
    }
    return out;
}

// Handle a client connection
void handle_client(hope::io::stream* connection) {
    const size_t buffer_size = 4096;
    char buffer[buffer_size];

    size_t received = connection->read_once(buffer, buffer_size);
    buffer[received] = '\0';

    std::istringstream request_stream(buffer);
    std::string request_method;
    std::string request_path;
    std::string http_version;

    request_stream >> request_method >> request_path >> http_version;

    // NOTE:: Only handle GET requests for now
    if (request_method != "GET") {
        std::string response = "HTTP/1.1 405 Method Not Allowed\r\n"
                               "Content-Length: 0\r\n"
                               "Connection: close\r\n\r\n";
        connection->write(response.c_str(), response.size());
        connection->disconnect();
        return;
    }

    request_path = url_decode(request_path);

    // Prevent directory traversal attacks
    if (request_path.find("..") != std::string::npos) {
        std::string response = "HTTP/1.1 403 Forbidden\r\n"
                               "Content-Length: 0\r\n"
                               "Connection: close\r\n\r\n";
        connection->write(response.c_str(), response.size());
        connection->disconnect();
        return;
    }

    // Default to /index.html
    if (request_path == "/") {
        request_path = "/index.html";
    }

    std::string file_path = FRONTEND_DIR + request_path;

    if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path)) {
        std::string response = "HTTP/1.1 404 Not Found\r\n"
                               "Content-Length: 0\r\n"
                               "Connection: close\r\n\r\n";
        connection->write(response.c_str(), response.size());
        connection->disconnect();
        return;
    }

    try {
        std::string content = read_file(file_path);
        std::string content_type = get_content_type(file_path);

        std::ostringstream response_stream;
        response_stream << "HTTP/1.1 200 OK\r\n"
                        << "Content-Type: " << content_type << "\r\n"
                        << "Content-Length: " << content.size() << "\r\n"
                        << "Connection: close\r\n\r\n"
                        << content;

        std::string response = response_stream.str();

        connection->write(response.c_str(), response.size());
    } catch (const std::exception& e) {
        std::cerr << "Error reading file: " << e.what() << "\n";
        std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
                               "Content-Length: 0\r\n"
                               "Connection: close\r\n\r\n";
        connection->write(response.c_str(), response.size());
    }

    connection->disconnect();
}


int main() {
    try {
        hope::io::init();

        auto* acceptor = hope::io::create_acceptor();
        acceptor->open(PORT);

        std::cout << "Server is listening on port " << PORT << "...\n";

        while (true) {
            if (auto* connection = acceptor->accept()) {
                handle_client(connection);
                delete connection;
            } else {
                std::cerr << "Failed to accept a connection" << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << e.what();
    }

    return 0;
}
