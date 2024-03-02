cmake_minimum_required(VERSION 3.11)

project(icarus-proto)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)

if (WIN32)
    set(NET_PLATFORM_HEADERS icarus-proto/net/win/*.h)
    set(NET_PLATFORM_SOURCES icarus-proto/net/win/*.cpp)
elseif (APPLE) # todo: UNIX?
    set(NET_PLATFORM_HEADERS icarus-proto/net/mac/*.h)
    set(NET_PLATFORM_SOURCES icarus-proto/net/mac/*.cpp)
else()
    message(FATAL_ERROR "Platform ${CMAKE_SYSTEM_NAME} is not supported.")
endif()

file(GLOB CL_HEADERS
    icarus-proto/protocol/*.h
    icarus-proto/net/*.h
    ${NET_PLATFORM_HEADERS}
)

file(GLOB CL_SOURCES
    icarus-proto/protocol/*.cpp
    icarus-proto/net/*.cpp
    ${NET_PLATFORM_SOURCES}
)

add_library(icarus-proto STATIC ${CL_HEADERS} ${CL_SOURCES})
target_include_directories(icarus-proto PUBLIC ../lib)
