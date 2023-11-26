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
        std::size_t write(const TValue &val) {
            static_assert(std::is_trivial_v<std::decay_t<TValue>>,
                          "write(const TValue&) is only available for trivial types");
            return write(&val, sizeof(val));
        }

        template<>
        std::size_t write<std::string>(const std::string& val){
            return write(val.size())
                   && write(val.c_str(), val.size());
        }

        template<typename TValue>
        bool read(TValue& val) {
            static_assert(std::is_trivial_v < std::decay_t < TValue >> ,
                          "read() is only available for trivial types");
            return read(&val, sizeof(val)) == sizeof (val);
        }

        template<>
        bool read<std::string>(std::string& val) {
            std::size_t length{ 0 };
            auto&& read_buffer = [&]{
                bool success = true;
                if (length > 0) {
                    auto* buffer = new char [length];
                    if (success = read(buffer, length);success)
                        val = std::string(buffer, length);
                    delete[] buffer;
                }
                return success;
            };
            return read(length) && read_buffer();
        }
    };
}
