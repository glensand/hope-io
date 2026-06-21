cmake_minimum_required(VERSION 3.11)

project(hope-io)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -O3 -fPIC" )
set( CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -O3 -fPIC" )

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)

file(GLOB CL_HEADERS
    hope-io/net/*.h
    hope-io/net/win/*.h
    hope-io/net/nix/*.h
    hope-io/net/tls/*.h
    hope-io/net/linux/*.h
    hope-io/net/uring/*.h
)

file(GLOB CL_SOURCES
    hope-io/net/*.cpp
    hope-io/net/win/*.cpp
    hope-io/net/nix/*.cpp
    hope-io/net/tls/*.cpp
    hope-io/net/linux/*.cpp
    hope-io/net/uring/*.cpp
)

add_library(hope-io STATIC ${CL_HEADERS} ${CL_SOURCES})

# ── BoringSSL (git submodule at lib/boringssl) ─────────────────────────
add_subdirectory(boringssl)


target_link_libraries(hope-io ssl crypto)

# ── liburing (io_uring event loops, Linux-only) ────────────────────────
find_library(LIBURING_LIB uring)
if(LIBURING_LIB AND UNIX AND NOT APPLE)
    target_link_libraries(hope-io uring)
    target_compile_definitions(hope-io PRIVATE HOPE_IO_URING_ENABLED=1)
    message(STATUS "liburing FOUND — building io_uring event loops")
else()
    target_compile_definitions(hope-io PRIVATE HOPE_IO_URING_ENABLED=0)
    if(UNIX AND NOT APPLE)
        message(STATUS "liburing NOT found — skipping io_uring event loops (install liburing-dev)")
    endif()
endif()

message("Hope-io/ Using BoringSSL (submodule)")

if(UNIX AND NOT APPLE)
    target_compile_definitions(hope-io PUBLIC -DPLATFORM_LINUX=1)
    target_compile_definitions(hope-io PUBLIC -DPLATFORM_APPLE=0)
    target_compile_definitions(hope-io PUBLIC -DPLATFORM_WINDOWS=0)
    message(STATUS ">>> Linux")
elseif(APPLE)
    target_compile_definitions(hope-io PUBLIC -DPLATFORM_LINUX=0)
    target_compile_definitions(hope-io PUBLIC -DPLATFORM_APPLE=1)
    target_compile_definitions(hope-io PUBLIC -DPLATFORM_WINDOWS=0)
    message(STATUS ">>> Apple")
else()
    target_compile_definitions(hope-io PUBLIC -DPLATFORM_LINUX=0)
    target_compile_definitions(hope-io PUBLIC -DPLATFORM_APPLE=0)
    target_compile_definitions(hope-io PUBLIC -DPLATFORM_WINDOWS=1)
    message(STATUS ">>> Windows")
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ../lib)
