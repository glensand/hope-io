#include "hope-io/net/tls/tls_init.h"

#include <iostream>
#include <unordered_map>
#include <string>

#include "hope-io/request/request.h"

std::string generate_json(const std::unordered_map<std::string, std::string> &map, const std::string& add = "") {
    std::string buffer;
    buffer.push_back('{');
    for (auto &&[k, v]: map) {
        buffer.append(std::format("\"{}\" : \"{}\",", k, v));
    }
    if (!add.empty()) {
        buffer.append(add);
        buffer.push_back('}');
    } else {
        buffer.back() = '}';
    }
    return buffer;
}

std::string extract_json_string(const std::string &json, const std::string &key) {
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

std::string build_files_arr(const std::string &file_id, const std::string &file_name) {
    std::string json = "\"files\": [ { ";
    json += "\"id\": \"" + file_id + "\", ";
    json += "\"title\": \"" + file_name + "\"";
    json += " } ]";
    return json;
}

int main() {
    const std::string token = "";
    const std::string chat_id = "";

    hope::io::init_tls();;

    const char *host = "https://slack.com/api/chat.postMessage";
    const auto auth_header = std::format("Authorization: Bearer {}\r\n", token);
    auto resp = hope::io::http::post(host,
                                     generate_json({
                                         {"channel", chat_id},
                                         {"text", "Hope stack"}
                                     }),
                                     auth_header
    );

    const auto thread = extract_json_string(resp, "ts");
    std::cout << "thread id :: " << thread << std::endl;
    std::cout << resp;

    resp = hope::io::http::post(host,
                                generate_json({
                                    {"channel", chat_id},
                                    {"text", "Thread msg"},
                                    {"thread_ts", thread}
                                }),
                                auth_header
    );

    std::cout << resp;

    const std::string file_content = "Extra long file with meaningfull text";
    // post file
    // simple message
    const char *external_file_host = "https://slack.com/api/files.getUploadURLExternal";
    resp = hope::io::http::get(external_file_host,
                               {{"filename", "hope_stack.txt"}, {"length", std::to_string(file_content.size())}},
                               auth_header
    );

    std::cout << resp << std::endl;

    std::string upload_url = extract_json_string(resp, "upload_url");
    std::string file_id = extract_json_string(resp, "file_id");

    std::cout << upload_url << std::endl;
    std::cout << file_id << std::endl;

    auto url = hope::io::http::extract_url(upload_url);
    auto res = hope::io::http::upload_file(upload_url, file_content, "hope_stack.txt");
    std::cout << res << std::endl;

    const auto complete_upload_host = "https://slack.com/api/files.completeUploadExternal";
    resp = hope::io::http::post(complete_upload_host,
                                generate_json({
                                    {"channel_id", chat_id},
                                    {"initial_comment", "File"},
                                    {"thread_ts", thread},
                                }, build_files_arr(file_id, "hope_stack.txt")), auth_header
    );

    std::cout << resp;

    return 0;
}
