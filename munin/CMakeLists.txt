cmake_minimum_required(VERSION 3.9)
project(Munin)

set (CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wunreachable-code -Wuninitialized -Wno-reorder")

add_library(munin_bootloader core.c ../common/layout.h validation.c validation.h Bytecode/execution.c Bytecode/execution.h Bytecode/execution_utils.c core.h Delta/bsdiff.c Delta/lzfx_light.c Delta/lzfx_light.h Delta/bsdiff.h io_management.h driver_api.h)
target_include_directories(munin_bootloader PRIVATE ../crypto/ ../hugin/Scheduler/Encoding)
target_link_libraries(munin_bootloader cryptoTools Decoder)

add_library(munin_userland Userland/userland.c integration/mbedOS/network/network.cpp integration/mbedOS/network/network.h Userland/userland_utils.c Userland/userland.h Bytecode/execution_utils.c io_management.h driver_api.h)
target_include_directories(munin_userland PRIVATE FreescaleIAP network ../crypto)

add_executable(munin_K64F integration/mbedOS/main.cpp integration/drivers/K64F/driver.cpp integration/drivers/K64F/device_config.h)
target_link_libraries(munin_K64F munin_bootloader munin_userland)