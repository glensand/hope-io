/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

// ReSharper disable CppClangTidyCppcoreguidelinesSpecialMemberFunctions
#pragma once

#include <functional>
#include <string_view>

namespace icarus::io {

    class acceptor {
    public:
        using on_new_connection_t = std::function<void(class stream*)>;
        using on_error_t = std::function<void(const std::exception& e)>;

        virtual ~acceptor() = default;
        virtual void run(std::string_view port, on_new_connection_t&& on_new_connection) = 0;
        virtual void run_async(std::string_view port, on_new_connection_t&& on_new_connection, on_error_t&& on_error) = 0;
        virtual void stop() = 0;
    };

}
