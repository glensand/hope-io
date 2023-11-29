/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#pragma once

#include "icarus-proto/protocol/argument.h"
#include "icarus-proto/protocol/argument_factory.h"

#include <vector>

namespace icarus::proto {

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

        void write_values(io::stream& stream) {
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