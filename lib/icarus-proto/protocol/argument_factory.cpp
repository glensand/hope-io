#include "argument_factory.h"

#include "icarus-proto/protocol/argument.h"
#include "icarus-proto/protocol/argument_generic.h"
#include "icarus-proto/protocol/argument_array.h"
#include "icarus-proto/protocol/argument_struct.h"

#include <unordered_map>
#include <functional>

namespace icarus::proto {

    argument* argument_factory::serialize(io::stream& stream) {
        using factory_impl_t = std::unordered_map<e_argument_type, std::function<argument*()>>;
        static factory_impl_t factory_impl;
        if (factory_impl.empty()) {
            auto register_type = [&](auto type, auto v) {
                using arg_t = std::decay_t<decltype(v)>;
                factory_impl.emplace(type, [] { return new arg_t(); });
            };

            // TODO:: use something like type-map
            register_type(e_argument_type::int32, icarus::proto::int32{});
            register_type(e_argument_type::float64, icarus::proto::float64{});
            register_type(e_argument_type::string, icarus::proto::string{});
            register_type(e_argument_type::uint64, icarus::proto::uint64{});
            register_type(e_argument_type::struct_value, icarus::proto::argument_struct{});

            factory_impl.emplace(e_argument_type::array, [&]()-> argument* {
                auto sub_type = stream.read<e_argument_type>();
                if (sub_type == e_argument_type::int32)
                    return new icarus::proto::array<int32_t>{e_argument_type::int32};
                if (sub_type == e_argument_type::float64)
                    return new icarus::proto::array<double>{e_argument_type::float64};
                if (sub_type == e_argument_type::string)
                    return new icarus::proto::array<std::string>{e_argument_type::string};
                if (sub_type == e_argument_type::uint64)
                    return new icarus::proto::array<uint64_t>{e_argument_type::uint64};
                if (sub_type == e_argument_type::struct_value)
                    return new icarus::proto::array<argument_struct*>{e_argument_type::struct_value};
                return nullptr;
            });
        }

        auto type = stream.read<e_argument_type>();
        auto* argument = factory_impl[type]();
        assert(argument);
        argument->read(stream);
        return argument;
    }

}
