cmake_minimum_required(VERSION 3.22)

project(hope-io)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)

file(GLOB CL_HEADERS
    hope-io/proto/*.h
    hope-io/proto2/*.h
    hope-io/net/*.h
    hope-io/net/win/*.h
    hope-io/net/nix/*.h
    hope-io/net/tls/*.h
)

file(GLOB CL_SOURCES
    hope-io/proto/*.cpp
    hope-io/proto2/*.cpp
    hope-io/net/*.cpp
    hope-io/net/win/*.cpp
    hope-io/net/nix/*.cpp
    hope-io/net/tls/*.cpp
)

add_library(hope-io STATIC ${CL_HEADERS} ${CL_SOURCES})

#set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

#add_executable(${PROJECT_NAME} ${SOURCE_FILES})
#target_link_libraries(${PROJECT_NAME} OpenSSL::Crypto)

target_include_directories(hope-io PUBLIC ../lib)
include_directories(${OPENSSL_INCLUDE_DIR})
