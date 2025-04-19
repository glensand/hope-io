cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

file(GLOB SAMPLE_CORE_SOURCES
        *.cpp)

foreach(source ${SAMPLE_CORE_SOURCES})
    get_filename_component(filename ${source} NAME)
    get_filename_component(target_name ${source} NAME_WE)

    add_executable(${target_name} ${filename})

    target_include_directories(${target_name} PUBLIC ../../lib)
    target_include_directories(${target_name} PUBLIC ../third-party/easy_profiler/easy_profiler_core/include)

    target_link_libraries(${target_name} easy_profiler)
    target_link_libraries(${target_name} hope-io)

endforeach()

# Example frontend dir for http_server.cpp sample
execute_process(
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMAND git clone https://github.com/rasskazovpavel/onpoint)
