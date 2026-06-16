/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#pragma once

#ifndef WEBSOCK_ENABLE
#if defined(__clang__)
#define WEBSOCK_ENABLE 0
#elif defined(__GNUC__) || defined(__GNUG__)
#if GCC_VERSION >= 10
#define WEBSOCK_ENABLE 0
#else
#define WEBSOCK_ENABLE 0
#endif
#elif defined(_MSC_VER)
#define WEBSOCK_ENABLE 0
#endif
#endif

// TODO:: move to cmake variable
//#define BUILD_WITH_EASY_PROFILER

// User error — programmer called the API wrong → assert in debug, noop in release
#define HOPE_ASSERT(cond, msg) assert((cond) && msg)

// System/environment error → always throws with context
#define HOPE_THROW(component, msg) \
    throw std::runtime_error(std::string("hope-io/") + component + ": " + msg)

// System error from errno → always throws with errno detail
#define HOPE_THROW_ERRNO(component, msg) \
    throw std::runtime_error(std::string("hope-io/") + component + ": " + msg + \
                             " [errno=" + std::to_string(errno) + " " + strerror(errno) + "]")

#ifdef BUILD_WITH_EASY_PROFILER
#include <easy/profiler.h>
#define __STR__(s) #s
#define THREAD_SCOPE(ThreadName) EASY_THREAD_SCOPE(__STR__(ThreadName))
#define NAMED_SCOPE(Name) EASY_BLOCK(__STR__(Name))
#define PROFILER_INIT EASY_PROFILER_ENABLE
#define PROFILER_START_LISTEN profiler::startListen();
#else
#define THREAD_SCOPE(ThreadName)
#define NAMED_SCOPE(Name)
#define PROFILER_INIT
#define PROFILER_START_LISTEN
#endif
