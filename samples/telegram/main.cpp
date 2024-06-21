#include "hope-io/net/tls/tls_init.h"
#include "hope-io/net/tls/tls_stream.h"

#include <iostream>

int main() {
    const std::string token = "YOUR TOKEN";
    const std::string chat_id = "YOUR CHAT ID";
    const std::string message = "TG via hope/io";

    hope::io::init_tls();
    auto* stream = hope::io::create_tls_stream();

    const char* host = "api.telegram.org";
    stream->connect(host, 443);

    const std::string url = "/bot" + token + "/sendMessage?chat_id=" + chat_id + "&text=" + message;
    const std::string request = "GET " + url + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
    stream->write(request.c_str(), request.size());

    std::string response;
    stream->stream_in(response);
    std::cout << response;

    return 0;
}