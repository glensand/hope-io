/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#pragma once

#include <type_traits>
#include <string>
#include <cassert>

#include "ikarus-proto/net/stream.h"

namespace ikarus::proto {

    enum class e_argument_type : uint8_t {
        int32,
        uint64,
        float64,
        string,
        array,
        struct_value,
        file,
    };

    class argument {
    public:
        argument(std::string in_name, e_argument_type in_type)
            : name(std::move(in_name))
            , type(in_type){}

        virtual ~argument() = default;

        [[nodiscard]] const std::string& get_name() const { return name; }
        [[nodiscard]] e_argument_type get_type() const { return type; }

        template<typename TValue>
        const TValue& as() const { return *(TValue*)get_value_internal(); }

        void write(io::stream& stream) {
            stream.write(type);
            stream.write(name);
            write_value(stream);
        }

        void read(io::stream& stream){
            stream.read(name);
            read_value(stream);
        }

    protected:
        virtual void write_value(io::stream& stream) = 0;
        virtual void read_value(io::stream& stream) = 0;
        [[nodiscard]] virtual void* get_value_internal() const = 0;

        std::string name;
        e_argument_type type;
    };
}
