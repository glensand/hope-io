cmake_minimum_required(VERSION 3.11)

project(icarus-proto)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)

file(GLOB CL_HEADERS
    icarus-proto/protocol/*.h
    icarus-proto/net/*.h
    icarus-proto/net/win/*.h
    icarus-proto/net/nix/*.h
)

file(GLOB CL_SOURCES
    icarus-proto/protocol/*.cpp
    icarus-proto/net/*.cpp
    icarus-proto/net/win/*.cpp
    icarus-proto/net/nix/*.cpp
)

add_library(icarus-proto STATIC ${CL_HEADERS} ${CL_SOURCES})
target_include_directories(icarus-proto PUBLIC ../lib)
