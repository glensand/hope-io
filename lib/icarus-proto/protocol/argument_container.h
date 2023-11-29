/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#pragma once

#include <vector>
#include "icarus-proto/protocol/argument.h"

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
            // todo read...
        }
    protected:
        std::vector<argument*> values;
    };
}