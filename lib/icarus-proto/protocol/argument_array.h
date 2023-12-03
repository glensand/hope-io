/* Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#include "icarus-proto/protocol/argument_generic.h"

#include <vector>

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
        constexpr static bool is_trivial = !std::is_base_of_v<argument, std::remove_pointer_t<TValue>>;
    public:

        explicit array() = default;
        array(std::string in_name, std::vector<TValue> in_value)
                : base(std::move(in_name), std::move(in_value)) {
            
        }

        virtual ~array() override {
            if constexpr (std::is_same_v<TValue, argument*>){
                for (auto v : base::val)
                    delete v;
            }
        }

        virtual void write(io::stream& stream) override {
            stream.write(argument::argument_type);
            stream.write(array_value_type);
            stream.write(argument::name);
            write_value(stream);
        }

    private:
        constexpr static e_argument_type get_type()  {
            using clear_t = std::decay_t<TValue>;
            if constexpr (std::is_same_v<clear_t, int32_t>)
                return e_argument_type::int32;
            if constexpr (std::is_same_v<clear_t, uint64_t>)
                return e_argument_type::uint64;
            if constexpr (std::is_same_v<clear_t, std::string>)
                return e_argument_type::string;
            if constexpr (std::is_base_of_v<argument, TValue>)
                return std::remove_all_extents_t<TValue>::type;
            if constexpr (std::is_same_v<clear_t, double>)
                return e_argument_type::float64;
            return e_argument_type::count;
        }

        virtual void write_value(io::stream& stream) override {
            stream.write((std::size_t)base::val.size());
            for (const auto& v : base::val) {
                if constexpr(is_trivial){
                    stream.write(v);
                }
                if constexpr (!is_trivial){
                    v->write_value(stream);
                }
            }
        }

        virtual void read_value(io::stream& stream) override {
            auto size = stream.read<std::size_t>();
            base::val.reserve(size);
            for (std::size_t i { 0 }; i < size; ++i){
                if constexpr (is_trivial) {
                    auto v = stream.read<TValue>();
                    base::val.emplace_back(v);
                }
                if constexpr (!is_trivial) {
                    auto* argument = new std::remove_pointer_t<TValue>();
                    base::val.emplace_back(argument);
                    argument->read_value(stream);
                }
            }
        }

        e_argument_type array_value_type{ get_type() };
    };
}
