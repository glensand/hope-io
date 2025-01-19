/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#include "hope-io/net/factory.h"
#include "hope-io/net/event_loop.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/acceptor.h"
#include "hope-io/net/factory.h"

#include "hope-io/proto/argument.h"
#include "hope-io/proto/argument_container.h"
#include "hope-io/proto/argument_file.h"
#include "hope-io/proto/argument_struct.h"
#include "hope-io/proto/argument_array.h"
#include "hope-io/proto/argument_factory.h"
#include "hope-io/proto/message.h"

struct event_loop_stream_wrapper final : public hope::io::stream {
    explicit event_loop_stream_wrapper(hope::io::event_loop::fixed_size_buffer& in_buffer)
        : buffer(in_buffer) { 
        }

    void begin_write() {
        buffer.reset();
        // seek buffer to 4 bytes, for event-loop it is important to know count of bytes we'll receive at this stage
        buffer.handle_write(sizeof(uint32_t));
    }

    void end_write() {
        const auto used_chunk = buffer.used_chunk();
        // write size before submit
        *(uint32_t*)used_chunk.first = used_chunk.second;
    }

    void begin_read() {
        // skip first 4 bytes, belongs to loop wrapper
        buffer.handle_read(sizeof(uint32_t));
    }

    void end_read() {

    }

    virtual void write(const void *data, std::size_t length) override {
        buffer.write(data, length);
    }

    virtual size_t read(void *data, std::size_t length) override {
        return buffer.read(data, length);
    }

    virtual std::string get_endpoint() const override { return {}; }
    virtual int32_t platform_socket() const override { return {}; }
    virtual void set_options(const hope::io::stream_options&) override {}
    virtual void connect(std::string_view ip, std::size_t port) override {}
    virtual void disconnect() override {}
    virtual size_t read_once(void* data, std::size_t length) override { return 0u; }
    virtual void stream_in(std::string& buffer) override {}
private:
    hope::io::event_loop::fixed_size_buffer& buffer;
};

enum class message_type : uint8_t {
    set = 0,
    get,
    full_log_dump,
    dump,
    count,
};

struct get_request final {
    explicit get_request(std::string in_key) 
        : key(std::move(in_key)) { }

    void write(hope::io::stream& stream) {
        auto proto_msg = std::unique_ptr<hope::proto::argument>(
        hope::proto::struct_builder::create()
            .add<hope::proto::string>("key", key)
            .add<hope::proto::int32>("type", int32_t(message_type::get))
            .get("message"));
        proto_msg->write(stream);
    }

private:
    std::string key;
};

struct set_request final {
    explicit set_request(std::string in_key, hope::proto::argument* in_value) 
        : key(std::move(in_key))
        , value(in_value) { }

    void write(hope::io::stream& stream) {
        auto proto_msg = std::unique_ptr<hope::proto::argument>(
            hope::proto::struct_builder::create()
                .add<hope::proto::string>("key", key)
                .add<hope::proto::int32>("type", int32_t(message_type::set))
                .add(value)
                .get("message")
        );
        proto_msg->write(stream);
    }

private:
    std::string key;
    hope::proto::argument* value = nullptr;
};

struct get_response final {
    explicit get_response(std::string in_key = {}, hope::proto::argument* in_value = {}) 
        : key(std::move(in_key))
        , value(in_value) { }

    void write(hope::io::stream& stream) {
        auto proto_msg = std::unique_ptr<hope::proto::argument>(
        hope::proto::struct_builder::create()
            .add<hope::proto::string>("key", key)
            .add(value)
            .get("message"));
        proto_msg->write(stream);
    }

    void read(hope::io::stream& stream) {
        auto proto_msg = std::unique_ptr<hope::proto::argument_struct>((hope::proto::argument_struct*)
            hope::proto::argument_factory::serialize(stream));
        value = proto_msg->release("value");
        key = proto_msg->field<hope::proto::string>("key").get();
    }

    std::string key;
    hope::proto::argument* value = nullptr;
};

struct set_response final {
    bool bOk = false;
    void write(hope::io::stream& stream) {
        auto proto_msg = std::unique_ptr<hope::proto::argument>(
        hope::proto::struct_builder::create()
            .add<hope::proto::int32>("OK", (int32_t)bOk)
            .get("message"));
        proto_msg->write(stream);
    }

    void read(hope::io::stream& stream) {
        auto proto_msg = std::unique_ptr<hope::proto::argument_struct>((hope::proto::argument_struct*)
            hope::proto::argument_factory::serialize(stream));
        bOk = proto_msg->field<hope::proto::int32>("OK").get() != 0;
    }
};
