cmake_minimum_required(VERSION 3.9)
project(RAVENS)

set (CMAKE_CXX_STANDARD 14)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

set (CMAKE_C_STANDARD 99)
set (CMAKE_C_STANDARD_REQUIRED ON)
set (CMAKE_C_EXTENSIONS OFF)

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wunreachable-code -Wuninitialized")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-c99-extensions -Wno-gnu-binary-literal")
endif()

add_subdirectory("hugin")
add_subdirectory("hugin/Scheduler")
add_subdirectory("munin")
add_subdirectory("common/crypto")