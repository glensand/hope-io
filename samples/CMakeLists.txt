cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

file(GLOB SAMPLE_CORE_SOURCES
        *.cpp)

find_package(OpenSSL)

foreach(source ${SAMPLE_CORE_SOURCES})
    get_filename_component(filename ${source} NAME)
    get_filename_component(target_name ${source} NAME_WE)

    add_executable(${target_name} ${filename})

    target_include_directories(${target_name} PUBLIC ../../lib)

    if (OPENSSL_FOUND)
        message("sample/ Found openssl, tls features are available")

        set(OPENSSL_USE_STATIC_LIBS TRUE)
        target_link_libraries(${target_name} ${OPENSSL_LIBRARIES} hope-io)

        message(${OPENSSL_LIBRARIES})
        message(${OPENSSL_INCLUDE_DIR})
    else ()
        target_link_libraries(${target_name} PRIVATE hope-io)
        message("sample/ OPenSSl not found, tls features is not available")
    endif ()
endforeach()
