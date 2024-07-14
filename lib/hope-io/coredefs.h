/* Copyright (C) 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#if defined(_WIN32) || defined(_WIN64)
#define ICARUS_WIN
#else
#define ICARUS_NIX
#endif

#if defined(__clang__)
#define WEBSOCK_ENABLE 1
#elif defined(__GNUC__) || defined(__GNUG__)
#if GCC_VERSION >= 10
#define WEBSOCK_ENABLE 1
#else
#define WEBSOCK_ENABLE 0
#endif
#elif defined(_MSC_VER)
#define WEBSOCK_ENABLE 1
#endif
