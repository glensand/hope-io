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
    target_link_libraries(${target_name} hope-io ssl crypto)

    # TLS library label for bench_tls_latency
    if(NOT TLS_LIB_LABEL)
        set(TLS_LIB_LABEL "BoringSSL")
    endif()
    target_compile_definitions(${target_name} PRIVATE TLS_LIB_LABEL="${TLS_LIB_LABEL}")

    # Platform definitions (must match lib/CMakeLists.txt)
    if(UNIX AND NOT APPLE)
        target_compile_definitions(${target_name} PRIVATE PLATFORM_LINUX=1 PLATFORM_APPLE=0 PLATFORM_WINDOWS=0)
    elseif(APPLE)
        target_compile_definitions(${target_name} PRIVATE PLATFORM_LINUX=0 PLATFORM_APPLE=1 PLATFORM_WINDOWS=0)
    else()
        target_compile_definitions(${target_name} PRIVATE PLATFORM_LINUX=0 PLATFORM_APPLE=0 PLATFORM_WINDOWS=1)
    endif()

    message("sample/ ${target_name}: linking BoringSSL")
endforeach()
