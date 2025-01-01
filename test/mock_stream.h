/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#include "hope-io/net/stream.h"
#include <sstream>

class mock_stream final : public hope::io::stream {
public:
    mock_stream() = default;

    virtual std::string get_endpoint() const override { return ""; }
    virtual int32_t platform_socket() const override { return 0; }
    virtual void connect(std::string_view ip, std::size_t port) override {}
    virtual void disconnect() override {}
    virtual void stream_in(std::string& buffer) override {}
    virtual void set_options(const hope::io::stream_options&) override {}
    virtual size_t read_once(void* data, std::size_t length) override { return 0; }
    virtual void write(const void* data, std::size_t length) override {
        stream_impl.write((const char*)data, length);
    }

    virtual size_t read(void* data, std::size_t length) override {
        stream_impl.read((char*)data, length);
        return length;
    }

private:

    std::stringstream stream_impl;
};