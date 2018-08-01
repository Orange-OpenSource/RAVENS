cmake_minimum_required(VERSION 3.9)
project(Scheduler)

include_directories(../../common/)

set (CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wunreachable-code -Wuninitialized -Wno-reorder")

add_library(Scheduler graph.cpp scheduler.cpp scheduler.h scheduler_passes.cpp scheduler_utils.cpp Address.h Token.h Block.h DetailedBlock.h scheduler_codegen.cpp networks.cpp network.h config.h cache_management.cpp public_command.h validation.cpp validation.h bsdiff_testing.cpp virtual_machine.cpp scheduler_codegen_optim.cpp VirtualMemory.h)
target_include_directories(Scheduler PRIVATE ../../common/crypto/)

add_library(Decoder ../../common/decoding/decoder.c ../../common/decoding/decoder.h ../../common/decoding/decoder_config.h)
add_library(Encoder Encoding/encoder.cpp Encoding/encoder.h Encoding/encoder_utils.cpp)
target_include_directories(Encoder PRIVATE ../../common/decoding/)
target_link_libraries(Encoder Decoder)

add_library(bsdiff bsdiff/bsdiff.cpp bsdiff/bsdiff_utils.c bsdiff/bsdiff.h ../../common/lzfx-4k/lzfx.c ../../common/lzfx-4k/lzfx.h)

add_library(SchedulerTesting static_tests.cpp dynamic_tests.cpp)