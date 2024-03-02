/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#include <type_traits>
#include <string>

#include "icarus-proto/net/stream.h"

namespace icarus::proto {

    enum class e_argument_type : uint8_t {
        int32,
        uint64,
        float64,
        string,
        array,
        struct_value,
        file,
        count
    };

    class argument {
    public:
        explicit argument(e_argument_type in_type)
            : argument_type(in_type){}

        argument(std::string in_name, e_argument_type in_type)
            : name(std::move(in_name))
            , argument_type(in_type){}

        virtual ~argument() = default;

        [[nodiscard]] const std::string& get_name() const { return name; }
        [[nodiscard]] e_argument_type get_type() const { return argument_type; }

        template<typename TValue>
        const TValue& as() const { return *(TValue*)get_value_internal(); }

        virtual void write(io::stream& stream) {
            stream.write(argument_type);
            stream.write(name);
            write_value(stream);
        }

        virtual void read(io::stream& stream) {
            stream.read(name);
            read_value(stream);
        }

    protected:
        virtual void write_value(io::stream& stream) = 0;
        virtual void read_value(io::stream& stream) = 0;
        [[nodiscard]] virtual void* get_value_internal() const = 0;

        std::string name;
        e_argument_type argument_type;
    };
}
