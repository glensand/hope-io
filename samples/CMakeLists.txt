cmake_minimum_required(VERSION 3.11)

project(sample)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

file(GLOB SAMPLE_HEADERS
    *.h)

file(GLOB SAMPLE_SOURSES
    *.cpp)

add_executable(sample ${SAMPLE_HEADERS} ${SAMPLE_SOURSES})
target_link_libraries(icarus-proto)
target_include_directories(sample INTERFACE ../lib)
