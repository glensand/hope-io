/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#pragma once

#include "argument.h"

namespace ikarus::proto {

    template<typename TValue, e_argument_type Type>
    class argument_generic : public argument {
        constexpr static bool is_trivial = std::is_trivial_v<TValue> || std::is_same_v<std::string, TValue>;
    public:
        argument_generic(std::string&& in_name, TValue&& in_val)
                : argument(std::move(in_name), Type)
                , val(std::move(in_val)) {}

        argument_generic(std::string&& in_name, const TValue& in_val)
                : argument(std::move(in_name), Type)
                , val(in_val) {}

        const TValue& get() const { return val; }

    protected:
        virtual bool write_value(io::stream& stream) override {
            if constexpr(is_trivial) {
                return stream.write(val);
            }
            assert(false);
            return false;
        }

        virtual bool read_value(io::stream& stream) override {
            if constexpr(is_trivial) {
                return stream.read(val);
            }
            assert(false);
            return false;
        }

        virtual void* get_value_internal() const override {
            return &val;
        }

        TValue val;
    };

    using int32 = argument_generic<int32_t, e_argument_type::int32>;
    using uint64 = argument_generic<uint64_t, e_argument_type::uint64>;
    using float64 = argument_generic<double, e_argument_type::float64>;
    using string = argument_generic<std::string, e_argument_type::string>;
}
