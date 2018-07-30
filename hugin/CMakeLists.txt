cmake_minimum_required(VERSION 3.9)
project(Hugin)

include_directories(../common/)

set (CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wunreachable-code -Wuninitialized -Wno-reorder")

add_library(Hugin_Scheduler CLI/scheduler_cli.cpp CLI/scheduler_cli.h CLI/scheduler_batch.cpp)
target_include_directories(Hugin_Scheduler PRIVATE thirdparty/rapidjson/include/ ../common/crypto/)
target_link_libraries(Hugin_Scheduler Scheduler Encoder bsdiff SchedulerTesting)

add_library(Hugin_Authentication CLI/authentication.cpp)
target_include_directories(Hugin_Authentication PRIVATE thirdparty/rapidjson/include/ ../common/ ../common/crypto/)

add_executable(Hugin hugin_core.cpp)
target_include_directories(Hugin PRIVATE ../common/crypto/)
target_link_libraries(Hugin Hugin_Authentication Hugin_Scheduler cryptoTools cryptoCLI)
