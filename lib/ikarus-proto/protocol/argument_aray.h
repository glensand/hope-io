/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#pragma once

#include "ikarus-proto/protocol/argument_generic.h"

namespace ikarus::proto {

    template<typename TValue>
    class array final : public argument_generic<std::vector<TValue>, e_argument_type::array> {
        static_assert(
                std::is_same_v<double, TValue>
                || std::is_same_v<int32_t , TValue>
                || std::is_same_v<uint64_t , TValue>
                || std::is_same_v<std::string, TValue>
                || std::is_same_v<argument*, TValue>,
                "Only specified types are allowed"
            );

        using base = argument_generic<std::vector<TValue>, e_argument_type::array>;
        constexpr static bool is_trivial = !std::is_same_v<argument*, TValue>;
    public:
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
        virtual bool write_value(io::stream& stream) {
            bool success = true;
            success &= stream.write(array_value_type);
            success &= stream.write(base::val.size());
            for (const auto v : base::val) {
                if constexpr(is_trivial){
                    success &= stream.write(v);
                }
                if constexpr (!is_trivial){
                    success &= v.write_value(stream);
                }
                if (!success) break;
            }
            return success;
        }

        virtual bool read_value(io::stream& stream) {
            std::size_t size { 0 };
            bool success = stream.read(size);
            base::val.reserve(size);
            for (std::size_t i { 0 }; i < size && success; ++i){
                if constexpr (is_trivial) {
                    TValue value;
                    success &= stream.read(value);
                    base::val.emplace_back(value);
                }
                // todo read non trivial vals
            }
            return success;
        }

        e_argument_type array_value_type;
    };
}
