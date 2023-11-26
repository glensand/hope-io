/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#pragma once

#include <vector>
#include "ikarus-proto/protocol/argument.h"

namespace ikarus::proto {

    class argument_container {
    public:
        argument_container(std::vector<argument*>&& in_values)
            : values(std::move(in_values)){

        }

        virtual ~argument_container() {
            for (auto* v : values)
                delete v;
        }

        bool write_values(io::stream& stream) {
            bool success = stream.write(values.size());
            for (auto* v : values){
                success = v->write(stream);
                if (!success)
                    break;
            }
            return success;
        }

        bool read_values(io::stream& stream) {
            auto read_values_internal = [&]{
                return true;
            };
            std::size_t size;
            return stream.read(size) && read_values_internal();
        }
    protected:
        std::vector<argument*> values;
    };
}