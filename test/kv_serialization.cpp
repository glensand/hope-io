/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "mock_stream.h"
#include "gtest/gtest.h"

#include "hope-io/proto/argument.h"
#include "hope-io/proto/argument_container.h"
#include "hope-io/proto/argument_file.h"
#include "hope-io/proto/argument_struct.h"
#include "hope-io/proto/argument_array.h"
#include "hope-io/proto/argument_factory.h"
#include "hope-io/proto/message.h"
#include "kv_misc.h"

TEST(Serialization, GetRequest)
{
    mock_stream stream;

    // write
    {
        hope::io::event_loop::fixed_size_buffer buffer;
        event_loop_stream_wrapper buffer_wrapper(buffer);
        get_request request("Test");
        buffer_wrapper.begin_write();
        request.write(buffer_wrapper);
        buffer_wrapper.end_write();
        stream.write(buffer.used_chunk().first, buffer.used_chunk().second);
    }

    // read 
    {
        auto stream_ptr = (hope::io::stream*)(&stream);
        stream_ptr->read<uint32_t>();

        auto proto_msg = std::unique_ptr<hope::proto::argument_struct>((hope::proto::argument_struct*)
            hope::proto::argument_factory::serialize(stream));
        auto&& got_key = proto_msg->field<std::string>("key");
        ASSERT_TRUE(got_key == "Test");   
    }
}

TEST(Serialization, SetRequest)
{
    mock_stream stream;

    // write
    {
        hope::io::event_loop::fixed_size_buffer buffer;
        event_loop_stream_wrapper buffer_wrapper(buffer);
        set_request request("test", new hope::proto::int32("value", 42));
        buffer_wrapper.begin_write();
        request.write(buffer_wrapper);
        buffer_wrapper.end_write();
        stream.write(buffer.used_chunk().first, buffer.used_chunk().second);

        std::cout << "\n\n[";
        auto used_part = buffer.used_chunk();
        std::cout.write((char*)used_part.first, used_part.second);
        std::cout << "]\n\n";
    }

    // read 
    {
        auto stream_ptr = (hope::io::stream*)(&stream);
        stream_ptr->read<uint32_t>();

        auto proto_msg = std::unique_ptr<hope::proto::argument_struct>((hope::proto::argument_struct*)
            hope::proto::argument_factory::serialize(stream));
        auto&& got_key = proto_msg->field<std::string>("key");
        auto&& value = proto_msg->field<int32_t>("value");
        ASSERT_TRUE(got_key == "test");
        ASSERT_TRUE(value == 42);
    }
}