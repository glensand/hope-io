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
bool perform_test(const TValue& val) {
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
    ASSERT_TRUE(perform_test<int32>(555));
    ASSERT_TRUE(perform_test<float64>(555.0));
    ASSERT_TRUE(perform_test<string>(std::string("meme string")));
    ASSERT_TRUE(perform_test<uint64>(10u));
}


TEST(Serialization, Array_int)
{
    ASSERT_TRUE(perform_test<int32>(555));
    ASSERT_TRUE(perform_test<float64>(555.0));
    ASSERT_TRUE(perform_test<string>(std::string("meme string")));
    ASSERT_TRUE(perform_test<uint64>(10u));
}
