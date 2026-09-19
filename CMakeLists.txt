cmake_minimum_required(VERSION 3.20)

project(order_matching_engine VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(order_matching_benchmark main.cpp)

target_include_directories(order_matching_benchmark PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_options(order_matching_benchmark PRIVATE
    -O3
    -march=native
)