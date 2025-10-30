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
    hope-io/net/websockets/*.h
)

file(GLOB CL_SOURCES
    hope-io/net/*.cpp
    hope-io/net/win/*.cpp
    hope-io/net/nix/*.cpp
    hope-io/net/tls/*.cpp
    hope-io/net/websockets/*.cpp
    hope-io/net/linux/*.cpp
)

add_library(hope-io STATIC ${CL_HEADERS} ${CL_SOURCES})

find_package(OpenSSL)

if (OPENSSL_FOUND)
    message("Hope-io/ Found openssl, tls features are available")

    add_definitions(-DHOPE_IO_USE_OPENSSL)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    include_directories(${OPENSSL_INCLUDE_DIR})
else ()
    message("Hope-io/ OPenSSl not found, tls features is not available")
endif ()

if(UNIX AND NOT APPLE)
    add_definitions(-DPLATFORM_LINUX=1)
    add_definitions(-DPLATFORM_APPLE=0)
    add_definitions(-DPLATFORM_WINDOWS=0)
    message(STATUS ">>> Linux")
elseif(APPLE)
    add_definitions(-DPLATFORM_LINUX=0)
    add_definitions(-DPLATFORM_APPLE=1)
    add_definitions(-DPLATFORM_WINDOWS=0)
    message(STATUS ">>> Apple")
else()
    add_definitions(-DPLATFORM_LINUX=0)
    add_definitions(-DPLATFORM_APPLE=0)
    add_definitions(-DPLATFORM_WINDOWS=1)
    message(STATUS ">>> Windows")
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ../lib)
