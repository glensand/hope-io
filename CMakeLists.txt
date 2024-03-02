cmake_minimum_required(VERSION 3.11)

project(icarus-proto-main)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(lib)

add_subdirectory(lib)
add_subdirectory(samples)
add_subdirectory(test)
