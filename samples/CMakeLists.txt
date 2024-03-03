cmake_minimum_required(VERSION 3.11)

project(echo-client-server)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

file(GLOB SAMPLE_HEADERS
    *.h)

file(GLOB SAMPLE_SOURSES
    *.cpp)

add_executable(echo-client-server ${SAMPLE_HEADERS} ${SAMPLE_SOURSES})
target_link_libraries(echo-client-server icarus-proto)
target_include_directories(echo-client-server PUBLIC ../lib)
