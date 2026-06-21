cmake_minimum_required(VERSION 3.11)

project(hope-io)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(lib)
add_subdirectory(benchmark)
add_subdirectory(samples)
add_subdirectory(test)
