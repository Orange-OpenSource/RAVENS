cmake_minimum_required(VERSION 3.9)
project(Hermes)

set (CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wunreachable-code -Wuninitialized -Wno-reorder")

add_subdirectory("hugin")
add_subdirectory("hugin/Scheduler")
add_subdirectory("munin")
add_subdirectory("common/crypto")