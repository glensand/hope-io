/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#include "argument_factory.h"

#include "icarus-proto/protocol/argument.h"
#include "icarus-proto/protocol/argument_generic.h"
#include "icarus-proto/protocol/argument_array.h"
#include "icarus-proto/protocol/argument_struct.h"

#include <unordered_map>
#include <functional>

namespace icarus::proto::argument_factory {

    argument* serialize(icarus::io::stream& stream) {
        using factory_impl_t = std::unordered_map<e_argument_type, std::function<argument*(io::stream&)>>;
        static factory_impl_t factory_impl;
        if (factory_impl.empty()) {
            auto register_type = [](auto type, auto v) {
                using arg_t = std::decay_t<decltype(v)>;
                factory_impl.emplace(type, [] (io::stream&) { return new arg_t(); });
            };

            // TODO:: use something like type-map
            register_type(e_argument_type::int32, int32{});
            register_type(e_argument_type::float64, float64{});
            register_type(e_argument_type::string, string{});
            register_type(e_argument_type::uint64, uint64{});
            register_type(e_argument_type::struct_value, argument_struct{});

            factory_impl.emplace(e_argument_type::array, [](io::stream& stream)-> argument* {
                auto sub_type = stream.read<e_argument_type>();
                if (sub_type == e_argument_type::int32)
                    return new array<int32_t>{};
                if (sub_type == e_argument_type::float64)
                    return new array<double>{};
                if (sub_type == e_argument_type::string)
                    return new array<std::string>{};
                if (sub_type == e_argument_type::uint64)
                    return new array<uint64_t>{};
                if (sub_type == e_argument_type::struct_value)
                    return new array<argument_struct*>{};
                return nullptr;
            });
        }

        auto type = stream.read<e_argument_type>();
        auto* argument = factory_impl[type](stream);
        assert(argument);
        argument->read(stream);
        return argument;
    }

}
