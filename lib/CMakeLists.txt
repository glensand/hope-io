cmake_minimum_required(VERSION 3.11)

project(icarus-proto)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)

file(GLOB CL_HEADERS
    icarus-proto/protocol/*.h
    icarus-proto/net/*.h
)

add_library(icarus-proto INTERFACE ${CL_HEADERS})
target_include_directories(icarus-proto INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
