/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#pragma once
#pragma clang diagnostic push
#pragma ide diagnostic ignored "modernize-use-override"

#include "icarus-proto/protocol/argument_container.h"

namespace icarus::proto{

    class argument_struct final : public argument, public argument_container {
    public:
        argument_struct()
            : argument(e_argument_type::struct_value){}

    private:
        argument_struct(std::string&& name, std::vector<argument*>&& args)
            : argument(std::move(name), e_argument_type::struct_value)
            , argument_container(std::move(args)){

        }

        virtual void write_value(io::stream& stream) override {
            argument_container::write_values(stream);
        }

        virtual void read_value(io::stream& stream) override {
            argument_container::read_values(stream);
        }

        [[nodiscard]] virtual void* get_value_internal() const override {
            return (void*)&values;
        }

        friend class struct_builder;
    };

    class struct_builder final {
    public:
        static struct_builder create() {
            return struct_builder{};
        }

        template<typename TValue, typename... Ts>
        struct_builder& add(Ts&&... args) {
            values.emplace_back(new TValue(std::forward<Ts>(args)...));
        }

        argument_struct* get(std::string&& name) {
            return new argument_struct(std::move(name), std::move(values));
        }

        virtual ~struct_builder(){
            assert(values.empty());
        }
    private:
        std::vector<argument*> values;
        struct_builder() = default;
    };

}
#pragma clang diagnostic pop