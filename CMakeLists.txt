cmake_minimum_required(VERSION 3.22)
project(minidb CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-g -fsanitize=address,undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(minidb src/pager.cpp src/node.cpp src/bptree.cpp src/lru.cpp src/buffer_pool.cpp)
target_include_directories(minidb PUBLIC src)

add_executable(pager_test tests/pager_test.cpp)
target_link_libraries(pager_test minidb)
add_executable(bptree_test tests/bptree_test.cpp)
target_link_libraries(bptree_test minidb)
add_executable(node_test tests/node_test.cpp)
target_link_libraries(node_test minidb)
add_executable(io_count_test tests/io_count_test.cpp)
target_link_libraries(io_count_test minidb)
add_executable(lru_test tests/lru_test.cpp)
target_link_libraries(lru_test minidb)
add_executable(buffer_pool_test tests/buffer_pool_test.cpp)
target_link_libraries(buffer_pool_test minidb)
add_executable(bench_pool tests/bench_pool.cpp)
target_link_libraries(bench_pool minidb)

# cmake -B build && cmake --build build
# build/*_test