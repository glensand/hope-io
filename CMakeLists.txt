cmake_minimum_required(VERSION 3.22)

project(icarus-proto-main)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(lib)
add_subdirectory(samples)
add_subdirectory(test)
