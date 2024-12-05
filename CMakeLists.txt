cmake_minimum_required(VERSION 3.31)
project(Lab5 C)

set(CMAKE_C_STANDARD 11)

add_executable(Lab5 benchmark.c)
target_link_libraries(Lab5 m)
