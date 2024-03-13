/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#include "hope-io/proto/argument.h"
#include "hope-io/proto/argument_factory.h"

#include <vector>

namespace hope::proto {

    class argument_container {
    public:
        argument_container() = default;
        explicit argument_container(std::vector<argument*>&& in_values)
            : values(std::move(in_values)){

        }

        virtual ~argument_container() {
            for (auto* v : values)
                delete v;
        }

        void write_values(io::stream& stream) const {
            stream.write(values.size());
            for (auto* v : values){
                v->write(stream);
            }
        }

        void read_values(io::stream& stream) {
            const auto size = stream.read<std::size_t>();
            for (std::size_t i{ 0 }; i < size; ++i){
                values.emplace_back(argument_factory::serialize(stream));
            }
        }

    protected:
        std::vector<argument*> values;
    };
}