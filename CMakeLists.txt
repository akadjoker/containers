cmake_minimum_required(VERSION 3.14)
project(ct_containers CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(ct INTERFACE)
target_include_directories(ct INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(ct_tests
  tests/test_vector.cpp
  tests/test_arena.cpp
  tests/test_pool.cpp
  tests/test_string.cpp
  tests/test_maps.cpp
  tests/test_array_sort.cpp
  tests/test_deque.cpp
  tests/test_stack.cpp
  tests/test_queue.cpp
  tests/test_json.cpp
  tests/test_span.cpp
  tests/test_slotmap.cpp
  tests/test_ptr.cpp
  tests/test_xml.cpp
  tests/test_function.cpp
  tests/test_variant.cpp
  tests/test_limits.cpp)
target_link_libraries(ct_tests PRIVATE ct gtest_main)
include(GoogleTest)
gtest_discover_tests(ct_tests)

set(CT_PUBLIC_HEADERS
  ct/arena.hpp
  ct/array.hpp
  ct/deque.hpp
  ct/flatmap.hpp
  ct/function.hpp
  ct/hashmap.hpp
  ct/hashset.hpp
  ct/json.hpp
  ct/pool.hpp
  ct/ptr.hpp
  ct/queue.hpp
  ct/slotmap.hpp
  ct/sort.hpp
  ct/span.hpp
  ct/stack.hpp
  ct/string.hpp
  ct/treemap.hpp
  ct/variant.hpp
  ct/vector.hpp
  ct/xml.hpp)

set(CT_HEADER_TARGETS)
foreach(header IN LISTS CT_PUBLIC_HEADERS)
  string(MAKE_C_IDENTIFIER "${header}" target_suffix)
  set(target "ct_header_${target_suffix}")
  add_library(${target} OBJECT tests/header_standalone.cpp)
  target_link_libraries(${target} PRIVATE ct)
  target_compile_definitions(${target} PRIVATE CT_HEADER="${header}")
  list(APPEND CT_HEADER_TARGETS ${target})
endforeach()
add_custom_target(ct_headers DEPENDS ${CT_HEADER_TARGETS})

# ---- Benchmarks ----
add_executable(ct_bench
  bench/bench_vector.cpp)
target_link_libraries(ct_bench PRIVATE ct)
target_compile_options(ct_bench PRIVATE -O3 -march=native)

add_executable(ct_bench_arena
  bench/bench_arena.cpp)
target_link_libraries(ct_bench_arena PRIVATE ct)
target_compile_options(ct_bench_arena PRIVATE -O3 -march=native)

add_executable(ct_bench_pool
  bench/bench_pool.cpp)
target_link_libraries(ct_bench_pool PRIVATE ct)
target_compile_options(ct_bench_pool PRIVATE -O3 -march=native)

add_executable(ct_bench_string
  bench/bench_string.cpp)
target_link_libraries(ct_bench_string PRIVATE ct)
target_compile_options(ct_bench_string PRIVATE -O3 -march=native)

add_executable(ct_bench_map
  bench/bench_map.cpp)
target_link_libraries(ct_bench_map PRIVATE ct)
target_compile_options(ct_bench_map PRIVATE -O3 -march=native)

add_executable(ct_bench_json
  bench/bench_json.cpp)
target_link_libraries(ct_bench_json PRIVATE ct)
target_compile_options(ct_bench_json PRIVATE -O3 -march=native)

add_executable(ct_bench_slotmap
  bench/bench_slotmap.cpp)
target_link_libraries(ct_bench_slotmap PRIVATE ct)
target_compile_options(ct_bench_slotmap PRIVATE -O3 -march=native)

add_executable(ct_torture
  tests/torture.cpp)
target_link_libraries(ct_torture PRIVATE ct)

add_executable(ct_bench_sort
  bench/bench_sort.cpp)
target_link_libraries(ct_bench_sort PRIVATE ct)
target_compile_options(ct_bench_sort PRIVATE -O3 -march=native)

add_executable(ct_bench_deque
  bench/bench_deque.cpp)
target_link_libraries(ct_bench_deque PRIVATE ct)
target_compile_options(ct_bench_deque PRIVATE -O3 -march=native)

add_executable(ct_bench_stack
  bench/bench_stack.cpp)
target_link_libraries(ct_bench_stack PRIVATE ct)
target_compile_options(ct_bench_stack PRIVATE -O3 -march=native)

add_executable(ct_bench_queue
  bench/bench_queue.cpp)
target_link_libraries(ct_bench_queue PRIVATE ct)
target_compile_options(ct_bench_queue PRIVATE -O3 -march=native)

# comparação opcional contra o BlockAllocator do projeto phys (box3d)
set(PHYS_DIR "/media/projectos/projects/cpp/vibecoding/fisica/phys")
if(EXISTS "${PHYS_DIR}/src/block_allocator.cpp")
  target_sources(ct_bench_pool PRIVATE "${PHYS_DIR}/src/block_allocator.cpp")
  target_include_directories(ct_bench_pool PRIVATE "${PHYS_DIR}/include")
  target_compile_definitions(ct_bench_pool PRIVATE CT_BENCH_HAVE_PHYS)
endif()
