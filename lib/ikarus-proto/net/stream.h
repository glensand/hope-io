/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#pragma once

#include <memory>
#include <cassert>
#include <string>
#include <type_traits>

namespace ikarus::io {

    class stream {
    public:
        virtual std::size_t write(const void *data, std::size_t length) = 0;
        virtual std::size_t read(void *data, std::size_t length) = 0;

        template<typename TValue>
        void write(const TValue &val) {
            static_assert(std::is_trivial_v<std::decay_t<TValue>>,
                          "write(const TValue&) is only available for trivial types");
            write(&val, sizeof(val));
        }

        template<>
        void write<std::string>(const std::string& val){
            write(val.size());
            write(val.c_str(), val.size());
        }

        template<typename TValue>
        void read(TValue& val) {
            static_assert(std::is_trivial_v <std::decay_t<TValue>> ,
                          "read() is only available for trivial types");
            read(&val, sizeof(val));
        }

        template<typename TValue>
        TValue read() {
            TValue val;
            read(&val, sizeof(val));
            return val;
        }

        template<>
        void read<std::string>(std::string& val) {
            const auto size = read<std::size_t>();
            if (size > 0) {
                auto* buffer = new char [size];
                read(buffer, size);
                val = std::string(buffer, size);
            }
        }
    };
}
