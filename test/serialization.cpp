/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "mock_stream.h"
#include "gtest/gtest.h"

#include "icarus-proto/protocol/argument.h"
#include "icarus-proto/protocol/argument_container.h"
#include "icarus-proto/protocol/argument_file.h"
#include "icarus-proto/protocol/argument_struct.h"
#include "icarus-proto/protocol/argument_array.h"
#include "icarus-proto/protocol/argument_factory.h"
#include "icarus-proto/protocol/message.h"

using namespace icarus::proto;

template<typename TClass, typename TValue>
bool perform_trivial_test(const TValue& val) {
    mock_stream stream;
    TClass arg("Base", val);
    arg.write(stream);
    auto* deserialized = argument_factory::serialize(stream);
    return dynamic_cast<TClass*>(deserialized) != nullptr
        && deserialized->get_type() == TClass::type
        && deserialized->get_name() == "Base"
        && deserialized->as<TValue>() == val;
}

TEST(Serialization, PrimitiveTypes)
{
    ASSERT_TRUE(perform_trivial_test<int32>(555));
    ASSERT_TRUE(perform_trivial_test<float64>(555.0));
    ASSERT_TRUE(perform_trivial_test<string>(std::string("meme string")));
    ASSERT_TRUE(perform_trivial_test<uint64>(10u));
}

TEST(Serialization, Array_int)
{
    std::vector<int32_t> ar;
    ar.emplace_back(13);
    ar.emplace_back(14);
    ar.emplace_back(88);
    array arg("arr_arg", ar);
    mock_stream stream;
    arg.write(stream);
    auto* deserialized = argument_factory::serialize(stream);
    ASSERT_TRUE(dynamic_cast<array<int32_t>*>(deserialized) != nullptr);
    ASSERT_TRUE(deserialized->get_type() == e_argument_type::array);
    ASSERT_TRUE(deserialized->get_name() == "arr_arg");
    for (std::size_t i{ 0 }; i < ar.size(); ++i) {
        auto&& dv = deserialized->as<std::vector<int32_t>>();
        ASSERT_TRUE(dv[i] == ar[i]);
    }
}
