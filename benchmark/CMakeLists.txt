cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

# Detect liburing (needed for io_uring event loop bench)
find_library(LIBURING_LIB uring)
if(LIBURING_LIB)
  message("benchmark/ liburing found")
else()
  message("benchmark/ liburing NOT found")
endif()

file(GLOB BENCH_SOURCES *.cpp)

foreach(source ${BENCH_SOURCES})
  get_filename_component(filename ${source} NAME)
  get_filename_component(target_name ${source} NAME_WE)

  add_executable(${target_name} ${filename})

  target_include_directories(${target_name} PUBLIC ../../lib)
  target_link_libraries(${target_name} hope-io ssl crypto)

  # bench_latency and bench_event_loop need liburing if available
  if(LIBURING_LIB AND (target_name MATCHES "bench_latency" OR target_name MATCHES "bench_event_loop"))
    target_link_libraries(${target_name} uring)
  endif()

  # Platform definitions (must match lib/CMakeLists.txt)
  if(UNIX AND NOT APPLE)
    target_compile_definitions(${target_name} PRIVATE PLATFORM_LINUX=1 PLATFORM_APPLE=0 PLATFORM_WINDOWS=0)
  elseif(APPLE)
    target_compile_definitions(${target_name} PRIVATE PLATFORM_LINUX=0 PLATFORM_APPLE=1 PLATFORM_WINDOWS=0)
  else()
    target_compile_definitions(${target_name} PRIVATE PLATFORM_LINUX=0 PLATFORM_APPLE=0 PLATFORM_WINDOWS=1)
  endif()


  message("benchmark/ ${target_name}: linking BoringSSL")
endforeach()
