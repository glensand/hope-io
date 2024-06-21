#include <iostream>
#include <string>
#include <cstring>

#include "hope-io/net/tls/tls_init.h"
#include "hope-io/net/tls/tls_stream.h"

int main() {
    hope::io::init_tls();
    auto* stream = hope::io::create_tls_stream();
    try {
        stream->connect("api.binance.com", 443);

        const char* request = "GET /api/v3/ticker/price?symbol=BTCUSDT HTTP/1.1\r\nHost: api.binance.com\r\nConnection: close\r\n\r\n";
        stream->write(request, strlen(request));
       
        std::string response;
        stream->stream_in(response);

        std::size_t pos = response.find("\"price\":\"");
        if (pos != std::string::npos) {
            pos += 9;
            std::size_t endPos = response.find("\"", pos);
            if (endPos != std::string::npos) {
                std::string btc_price = response.substr(pos, endPos - pos);
                std::cout << "Current BTC price: " << btc_price << std::endl;
            } else {
                std::cerr << "Failed to parse price from response." << std::endl;
            }
        } else {
            std::cerr << "Failed to find price in response." << std::endl;
        }
    }
    catch (const std::exception& ex){
        std::cout << ex.what();
    }
    
    delete stream;
    hope::io::deinit_tls();

    return 0;
}