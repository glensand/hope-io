cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Download and configure GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.12.1
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Test executable
add_executable(hope-io-test
    test_main.cpp
    test_tcp_stream.cpp
    test_tcp_acceptor.cpp
    test_udp.cpp
    test_event_loop.cpp
    test_tls.cpp
    test_tls_event_loop.cpp
    test_error_handling.cpp
    test_platform_compatibility.cpp
    test_write_v.cpp
)

target_include_directories(hope-io-test PRIVATE ../../lib)

target_link_libraries(hope-io-test
    PRIVATE
    hope-io ssl crypto
    GTest::gtest
    GTest::gmock
)

target_compile_definitions(hope-io-test PRIVATE HOPE_IO_USE_OPENSSL)
message("test/ Using BoringSSL, TLS tests are available")

# Platform definitions for the TLS event loop test (must match lib/CMakeLists.txt)
if(UNIX AND NOT APPLE)
    set_property(SOURCE test_tls_event_loop.cpp APPEND PROPERTY COMPILE_DEFINITIONS PLATFORM_LINUX=1 PLATFORM_APPLE=0 PLATFORM_WINDOWS=0)
elseif(APPLE)
    set_property(SOURCE test_tls_event_loop.cpp APPEND PROPERTY COMPILE_DEFINITIONS PLATFORM_LINUX=0 PLATFORM_APPLE=1 PLATFORM_WINDOWS=0)
else()
    set_property(SOURCE test_tls_event_loop.cpp APPEND PROPERTY COMPILE_DEFINITIONS PLATFORM_LINUX=0 PLATFORM_APPLE=0 PLATFORM_WINDOWS=1)
endif()

# Add test to CTest
include(GoogleTest)
gtest_discover_tests(hope-io-test)
