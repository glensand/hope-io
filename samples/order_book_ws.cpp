/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include "hope-io/net/tls/tls_init.h"
#include "hope-io/net/tls/tls_stream.h"
#include "hope-io/net/factory.h"
#include "hope-io/net/init.h"

#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <sstream>
#include <vector>
#include <algorithm>
#include <span>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <cassert>

// ============================================================================
// Base64 (RFC 4648) — just enough for the WebSocket handshake key
// ============================================================================
static const char b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len) v |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) v |= (uint32_t)data[i + 2];
        out += b64_alphabet[(v >> 18) & 0x3F];
        out += b64_alphabet[(v >> 12) & 0x3F];
        out += (i + 1 < len) ? b64_alphabet[(v >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? b64_alphabet[v & 0x3F] : '=';
    }
    return out;
}

// ============================================================================
// WebSocket frame helpers
// ============================================================================
struct ws_frame {
    uint8_t opcode;
    std::string payload;
};

static uint32_t masking_key() {
    return ((uint32_t)rand() & 0xFF) |
           (((uint32_t)rand() & 0xFF) << 8) |
           (((uint32_t)rand() & 0xFF) << 16) |
           (((uint32_t)rand() & 0xFF) << 24);
}

static void ws_send_frame(hope::io::stream* s, uint8_t opcode, const std::string& payload) {
    uint8_t hdr[10];
    size_t hdr_len = 2;
    hdr[0] = 0x80 | opcode;
    hdr[1] = 0x80;

    const size_t len = payload.size();
    if (len <= 125) {
        hdr[1] |= (uint8_t)len;
    } else if (len <= 0xFFFF) {
        hdr[1] |= 126;
        const uint16_t n = (uint16_t)len;
        hdr[2] = (uint8_t)(n >> 8);
        hdr[3] = (uint8_t)(n & 0xFF);
        hdr_len = 4;
    } else {
        hdr[1] |= 127;
        uint64_t n = (uint64_t)len;
        for (int i = 7; i >= 0; --i)
            hdr[hdr_len++] = (uint8_t)(n >> (i * 8));
    }

    const uint32_t key = masking_key();
    const uint8_t mk[4] = {
        (uint8_t)(key >> 24), (uint8_t)(key >> 16),
        (uint8_t)(key >> 8),  (uint8_t)(key)
    };

    // one contiguous buffer — one SSL_write — one send()
    uint8_t buf[2048];
    size_t offset = 0;
    std::memcpy(buf + offset, hdr, hdr_len); offset += hdr_len;
    std::memcpy(buf + offset, mk, 4);         offset += 4;
    for (size_t i = 0; i < len; ++i)
        buf[offset++] = (uint8_t)(payload[i] ^ mk[i % 4]);

    s->write(buf, offset);
}

static ws_frame ws_read_frame(hope::io::stream* s) {
    uint8_t b0, b1;
    s->read(&b0, 1);
    s->read(&b1, 1);

    const uint8_t opcode = b0 & 0x0F;
    size_t len = b1 & 0x7F;
    const bool masked = (b1 & 0x80) != 0;

    if (len == 126) {
        uint8_t buf[2];
        s->read(buf, 2);
        len = ((size_t)buf[0] << 8) | (size_t)buf[1];
    } else if (len == 127) {
        uint8_t buf[8];
        s->read(buf, 8);
        len = 0;
        for (int i = 0; i < 8; ++i)
            len = (len << 8) | (size_t)buf[i];
    }

    uint8_t mk[4] = { 0, 0, 0, 0 };
    if (masked) s->read(mk, 4);

    std::string payload(len, '\0');
    if (len > 0) {
        s->read(payload.data(), len);
        if (masked)
            for (size_t i = 0; i < len; ++i)
                payload[i] ^= mk[i % 4];
    }

    return { opcode, std::move(payload) };
}

// ============================================================================
// WebSocket handshake (HTTP Upgrade)
// ============================================================================
static void ws_handshake(hope::io::stream* s,
                         const std::string& host,
                         const std::string& path)
{
    uint8_t key_data[16];
    for (auto& k : key_data) k = (uint8_t)(rand() & 0xFF);
    const std::string key_b64 = b64_encode(key_data, 16);

    const std::string req =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + key_b64 + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Origin: https://www.bybit.com\r\n"
        "\r\n";

    s->write(req.data(), req.size());

    std::string resp;
    char c;
    while (resp.size() < 4 ||
           resp.substr(resp.size() - 4) != "\r\n\r\n") {
        s->read(&c, 1);
        resp += c;
    }

    if (resp.find("101") == std::string::npos)
        throw std::runtime_error("ws_handshake: expected 101, got:\n" + resp);
}

// ============================================================================
// Minimal JSON extraction — no third-party libs
// ============================================================================
namespace {

    const char* skip_ws(const char* p, const char* end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;
        return p;
    }

    const char* find_key(std::string_view json, std::string_view key) {
        std::string needle = "\"";
        needle += key;
        needle += "\":";
        auto pos = json.find(needle);
        if (pos == std::string_view::npos) return nullptr;
        const char* p = json.data() + pos + needle.size();
        return skip_ws(p, json.data() + json.size());
    }

} // anonymous namespace

static std::string_view json_extract(std::string_view json, std::string_view key) {
    const char* p = find_key(json, key);
    if (!p) return {};
    const char* const end = json.data() + json.size();
    if (p >= end) return {};

    if (*p == '"') {
        ++p;
        auto start = p;
        while (p < end && *p != '"') {
            if (*p == '\\') ++p;
            ++p;
        }
        return std::string_view(start, p - start);
    }

    auto start = p;
    while (p < end && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
        ++p;
    }
    return std::string_view(start, p - start);
}

static std::string_view json_extract_bracketed(std::string_view json,
                                           std::string_view key,
                                           char open, char close)
{
    const char* p = find_key(json, key);
    if (!p) return {};
    const char* const end = json.data() + json.size();
    if (p >= end || *p != open) return {};

    int depth = 0;
    const char* start = p;
    while (p < end) {
        if (*p == '"') {
            ++p;
            while (p < end && (*p != '"' || *(p - 1) == '\\')) ++p;
        } else if (*p == open) {
            ++depth;
        } else if (*p == close) {
            --depth;
            if (depth == 0) {
                ++p;
                return std::string_view(start, p - start);
            }
        }
        ++p;
    }
    return {};
}

static std::string_view json_extract_array(std::string_view json, std::string_view key) {
    return json_extract_bracketed(json, key, '[', ']');
}

static std::string_view json_extract_object(std::string_view json, std::string_view key) {
    return json_extract_bracketed(json, key, '{', '}');
}

// ============================================================================
// Order book — efficient vector-based snapshot + delta
// ============================================================================
class order_book final {
public:
    struct level final {
        double prx{ };
        double qt{ };

        friend bool operator==(const level& a, const level& b) noexcept {
            return a.prx == b.prx && a.qt == b.qt;
        }
        friend bool operator!=(const level& a, const level& b) noexcept {
            return !(a == b);
        }
    };

    explicit order_book(std::size_t max_orders = 50) {
        m_asks.reserve(max_orders * 2);
        m_bids.reserve(max_orders * 2);
    }

    [[nodiscard]] const auto& bids() const noexcept { return m_bids; }
    [[nodiscard]] const auto& asks() const noexcept { return m_asks; }

    void apply_message(
        const std::span<level>& bids,
        const std::span<level>& asks,
        bool is_full_state
    ) {
        if (is_full_state) [[unlikely]] {
            if (asks.empty()) {
                m_asks.clear();
            } else {
                m_asks.resize(asks.size());
                std::copy_n(begin(asks), asks.size(), rbegin(m_asks));
            }
            if (bids.empty()) {
                m_bids.clear();
            } else {
                m_bids.resize(bids.size());
                std::copy_n(begin(bids), bids.size(), rbegin(m_bids));
            }
            return;
        }

        if (!bids.empty()) apply_bids(bids);
        if (!asks.empty()) apply_asks(asks);
    }

private:
    // bids come in descending order. stored ascending.
    void apply_bids(const std::span<level>& bids) {
        assert(!bids.empty());
        if (m_bids.empty()) {
            for (const auto b : bids) {
                if (b.qt != 0)
                    m_bids.insert(begin(m_bids), b);
            }
            return;
        }
        for (const auto b : bids) {
            auto iter = rbegin(m_bids);
            for (; iter < rend(m_bids); ++iter) {
                if (iter->prx < b.prx) {
                    if (b.qt != 0)
                        m_bids.insert(iter.base(), b);
                    break;
                }
                if (iter->prx == b.prx) {
                    if (b.qt == 0)
                        m_bids.erase(std::next(iter).base());
                    else
                        iter->qt += b.qt;
                    break;
                }
            }
            if (iter == rend(m_bids)) [[unlikely]] {
                if (b.qt != 0)
                    m_bids.insert(begin(m_bids), b);
            }
        }
    }

    // asks come in ascending order. stored descending.
    void apply_asks(const std::span<level>& asks) {
        assert(!asks.empty());
        if (m_asks.empty()) {
            for (const auto a : asks) {
                if (a.qt != 0)
                    m_asks.insert(begin(m_asks), a);
            }
            return;
        }
        for (const auto a : asks) {
            bool handled = false;
            for (auto iter = begin(m_asks); iter != end(m_asks); ++iter) {
                if (iter->prx == a.prx) {
                    if (a.qt == 0)
                        m_asks.erase(iter);
                    else
                        iter->qt += a.qt;
                    handled = true;
                    break;
                }
            }
            if (handled || a.qt == 0) continue;

            auto pos = begin(m_asks);
            for (; pos != end(m_asks); ++pos) {
                if (pos->prx < a.prx)
                    break;
            }
            m_asks.insert(pos, a);
        }
    }

    // bids stored in price ascending order
    std::vector<level> m_bids;
    // asks stored in price descending order
    std::vector<level> m_asks;
};

// Parse [price,qty] entries from a JSON array into a pre-allocated buffer.
// Returns the number of levels written.
static size_t parse_levels(std::string_view arr, std::span<order_book::level> out) {
    if (arr.empty() || arr[0] != '[') return 0;

    const char* p = arr.data() + 1;
    const char* const end = arr.data() + arr.size();
    size_t count = 0;

    while (p < end && *p != ']' && count < out.size()) {
        p = skip_ws(p, end);
        if (p >= end || *p != '[') break;
        ++p;

        p = skip_ws(p, end);
        double price = 0;
        if (p < end && *p == '"') {
            ++p;
            char* endptr = nullptr;
            price = std::strtod(p, &endptr);
            if (endptr != p) p = endptr;
            if (p < end && *p == '"') ++p;
        }
        p = skip_ws(p, end);
        if (p < end && *p == ',') ++p;
        p = skip_ws(p, end);

        double qty = 0;
        if (p < end && *p == '"') {
            ++p;
            char* endptr = nullptr;
            qty = std::strtod(p, &endptr);
            if (endptr != p) p = endptr;
            if (p < end && *p == '"') ++p;
        }

        out[count++] = { price, qty };

        p = skip_ws(p, end);
        if (p < end && *p == ']') ++p;
        p = skip_ws(p, end);
        if (p < end && *p == ',') ++p;
    }

    return count;
}

// ============================================================================
// Format a single exchange's order book as one string
// ============================================================================
static std::string format_book(const std::string& exchange,
                               std::string_view seq_label,
                               std::string_view seq_value,
                               const order_book& book,
                               int64_t parse_ns)
{
    std::ostringstream os;
    os << "[" << exchange << "]  " << seq_label << ": " << seq_value
       << "  [" << parse_ns << "ns]\n"
       << "   Bids\n";

    auto& bids = book.bids();
    int n = 0;
    for (auto it = bids.rbegin(); it != bids.rend() && n < 3; ++it, ++n) {
        char prx_buf[32], qt_buf[32];
        snprintf(prx_buf, sizeof(prx_buf), "%.2f", it->prx);
        snprintf(qt_buf, sizeof(qt_buf), "%.8f", it->qt);
        os << "     " << (n + 1) << ": " << prx_buf << "  @ " << qt_buf << "\n";
    }

    os << "   Asks\n";
    auto& asks = book.asks();
    n = 0;
    for (auto it = asks.rbegin(); it != asks.rend() && n < 3; ++it, ++n) {
        char prx_buf[32], qt_buf[32];
        snprintf(prx_buf, sizeof(prx_buf), "%.2f", it->prx);
        snprintf(qt_buf, sizeof(qt_buf), "%.8f", it->qt);
        os << "     " << (n + 1) << ": " << prx_buf << "  @ " << qt_buf << "\n";
    }

    os << "\n";
    return os.str();
}

// ============================================================================
// Per-exchange thread
// ============================================================================
static void exchange_listener(const std::string& name,
                              const std::string& host, int port,
                              const std::string& path,
                              const std::string& subscribe_msg,
                              int max_frames)
{
    auto* stream = hope::io::create_tls_stream();
    try {
        stream->connect(host, port);
        ws_handshake(stream, host, path);

        if (!subscribe_msg.empty())
            ws_send_frame(stream, 0x1, subscribe_msg);

        order_book book;

        for (int i = 0; i < max_frames; ++i) {
            auto frame = ws_read_frame(stream);

            switch (frame.opcode) {
            case 0x8:
                return;
            case 0x9:
                ws_send_frame(stream, 0xA, frame.payload);
                [[fallthrough]];
            case 0xA:
                continue;
            default:
                break;
            }

            std::string_view seq_label;
            std::string_view seq_value;

            order_book::level bids_buf[200];
            order_book::level asks_buf[200];

            auto t0 = std::chrono::high_resolution_clock::now();

            if (name == "Binance") {
                auto bids_cnt = parse_levels(json_extract_array(frame.payload, "bids"), bids_buf);
                auto asks_cnt = parse_levels(json_extract_array(frame.payload, "asks"), asks_buf);
                book.apply_message({bids_buf, bids_cnt}, {asks_buf, asks_cnt}, true);
                seq_label = "lastUpdateId";
                seq_value = json_extract(frame.payload, "lastUpdateId");
            } else {
                auto data = json_extract_object(frame.payload, "data");
                if (data.empty()) continue;

                auto type = json_extract(frame.payload, "type");
                auto bids_cnt = parse_levels(json_extract_array(data, "b"), bids_buf);
                auto asks_cnt = parse_levels(json_extract_array(data, "a"), asks_buf);
                book.apply_message({bids_buf, bids_cnt}, {asks_buf, asks_cnt}, type == "snapshot");

                seq_label = "seq";
                seq_value = json_extract(data, "seq");
            }

            auto t1 = std::chrono::high_resolution_clock::now();

            std::cout << format_book(name, seq_label, seq_value, book,
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }
    } catch (const std::exception& e) {
        std::cerr << "[" << name << "] Error: " << e.what() << std::endl;
    }

    delete stream;
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::srand((unsigned)std::time(nullptr));

    hope::io::init();
    hope::io::init_tls();

    constexpr int kMaxFrames = 100;

    std::thread binance_thr(exchange_listener,
        "Binance", "stream.binance.com", 9443,
        "/ws/btcusdt@depth20@100ms", "", kMaxFrames);

    std::thread bybit_thr(exchange_listener,
        "Bybit", "stream.bybit.com", 443,
        "/v5/public/linear",
        R"({"op":"subscribe","args":["orderbook.200.BTCUSDT"]})",
        kMaxFrames);

    binance_thr.join();
    bybit_thr.join();

    hope::io::deinit_tls();
    hope::io::deinit();

    return 0;
}
