cmake_minimum_required(VERSION 3.22)

project(samples)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(binance)
add_subdirectory(proto)
add_subdirectory(telegram)
