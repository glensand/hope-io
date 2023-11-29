/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#pragma once

#include "icarus-proto/protocol/argument_container.h"

namespace icarus::proto {

    class message final : public proto::argument_container {
    public:
        void write(io::stream& stream){
            stream.write(message_type);
            write_values(stream);
        }

        void read(io::stream& stream){
            stream.read(message_type);
            read_values(stream);
        }

    private:
        int message_type{ 0 };
    };

}
