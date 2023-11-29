/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#pragma once

#include "icarus-proto/protocol/argument_generic.h"

namespace icarus::proto {

    template<typename TValue>
    class array final : public argument_generic<std::vector<TValue>, e_argument_type::array> {
        static_assert(
                std::is_same_v<double, TValue>
                || std::is_same_v<int32_t , TValue>
                || std::is_same_v<uint64_t , TValue>
                || std::is_same_v<std::string, TValue>
                || std::is_base_of_v<argument, std::remove_pointer_t<TValue>>,
                "Only specified types are allowed"
            );

        using base = argument_generic<std::vector<TValue>, e_argument_type::array>;
        constexpr static bool is_trivial = !std::is_same_v<argument*, TValue>;
    public:
        explicit array(e_argument_type in_array_value_type)
            : array_value_type(in_array_value_type){}

        array(std::string in_name, std::vector<TValue> in_value, e_argument_type type)
                : base(std::move(in_name), std::move(in_value))
                , array_value_type(type) {}

        virtual ~array(){
            if constexpr (std::is_same_v<TValue, argument*>){
                for (auto v : base::val)
                    delete v;
            }
        }

    private:
        virtual void write_value(io::stream& stream) {
            stream.write(array_value_type);
            stream.write(base::val.size());
            for (const auto v : base::val) {
                if constexpr(is_trivial){
                    stream.write(v);
                }
                if constexpr (!is_trivial){
                    v.write_value(stream);
                }
            }
        }

        virtual void read_value(io::stream& stream) {
            auto size = stream.read<std::size_t>();
            base::val.reserve(size);
            for (std::size_t i { 0 }; i < size; ++i){
                if constexpr (is_trivial) {
                    auto v = stream.read<TValue>();
                    base::val.emplace_back(v);
                }
                if constexpr (!is_trivial) {
                    auto* argument = new TValue();
                    base::val.emplace_back(argument);
                    argument->read_value(stream);
                }
            }
        }

        e_argument_type array_value_type;
    };
}
