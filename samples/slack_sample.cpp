#include "hope-io/net/tls/tls_init.h"
#include "hope-io/net/stream.h"

#include <iostream>
#include <unordered_map>

#include "hope-io/request/request.h"

std::string generate_json(const std::unordered_map<std::string, std::string>& map) {
    std::string buffer;
    buffer.push_back('{');
    for (auto&&[k, v] : map) {
        buffer.append(std::format("\"{}\" : \"{}\",", k, v));
    }
    buffer.back() = '}';
    return buffer;
}

std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\":\"";
    size_t start = json.find(pattern);
    if (start == std::string::npos) return "";

    start += pattern.length();
    size_t end = json.find("\"", start);
    if (end == std::string::npos) return "";

    std::string value = json.substr(start, end - start);

    // Unescape \\/ → /
    size_t pos = 0;
    while ((pos = value.find("\\/", pos)) != std::string::npos) {
        value.replace(pos, 2, "/");
        pos += 1;
    }

    return value;
}

int main() {
    const std::string token = "xoxb-";
    const std::string chat_id = "";

    hope::io::init_tls();;

    if (false)
    {
        // simple message
        const char* host = "https://slack.com/api/chat.postMessage";
        const auto auth_header = std::format("Authorization: Bearer {}\r\n", token);
        auto resp = hope::io::http::post(host, 
        generate_json({
            {"channel", chat_id},
            {"text", "Hope stack"}
        }),
        auth_header
        );
        std::cout << resp;
    }

    {
        const std::string file_content = "Extra long file with meaningfull text";
        // post file
        // simple message
        const char* host = "https://slack.com/api/files.getUploadURLExternal";
        const auto auth_header = std::format("Authorization: Bearer {}\r\n", token);
        auto resp = hope::io::http::get(host, 
        {{"filename", "hope_stack.txt"}, {"length", std::to_string(file_content.size())}},
        auth_header
        );

        std::cout << resp << std::endl;

        std::string upload_url = extract_json_string(resp, "upload_url");
        std::string file_id = extract_json_string(resp, "file_id");

        std::cout << upload_url << std::endl;
        std::cout << file_id << std::endl;

        auto res = hope::io::http::upload_file(upload_url, file_content, "hope_stack.txt");
        std::cout << res << std::endl;
    }

    return 0;
}