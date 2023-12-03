/* Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#include "icarus-proto/protocol/argument_struct.h"

namespace icarus::proto {

    class message final {
    public:
        explicit message(argument_struct* in_message_impl)
            : message_impl(in_message_impl) {
            if (message_impl == nullptr) {
                message_impl = new argument_struct;
            }
        }

        ~message() {
            delete message_impl;
        }

        void write(io::stream& stream) const {
            message_impl->write(stream);
        }

        void read(io::stream& stream) const {
            message_impl->read(stream);
        }

        [[nodiscard]] const std::string& get_id() const noexcept { return message_impl->get_name(); }
    private:
        argument_struct* message_impl{ nullptr };
    };

}
