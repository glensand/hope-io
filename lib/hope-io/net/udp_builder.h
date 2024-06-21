#pragma once

#include <cstddef>
#include <cstdint>

namespace hope::io {

    class udp_builder {
    public:
        virtual ~udp_builder() = default;

        [[nodiscard]] virtual int32_t platform_socket() const = 0;

        virtual void init(std::size_t port) = 0;
    };

}
