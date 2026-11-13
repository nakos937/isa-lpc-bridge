# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.13)

# usage:  export PICO_SDK_PATH=~/work/toolchain/pico-sdk   (SDK 2.1 or newer)
#         cmake -S . -B build -G Ninja [-DFW_RAM=ON] && ninja -C build
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/board)
set(PICO_BOARD isa_lpc_bridge)
set(PICO_PLATFORM rp2350)

include(pico_sdk_import.cmake)
project(isa_lpc_fw C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(isa_lpc
    src/main.c
    src/isa.c
    src/lpc.c
    src/mailbox.c
    src/io16.c
    src/cfgport.c
)

pico_generate_pio_header(isa_lpc ${CMAKE_CURRENT_LIST_DIR}/pio/lpc_fe.pio)
pico_generate_pio_header(isa_lpc ${CMAKE_CURRENT_LIST_DIR}/pio/lpc_tx.pio)
pico_generate_pio_header(isa_lpc ${CMAKE_CURRENT_LIST_DIR}/pio/lpc_aux.pio)

target_link_libraries(isa_lpc
    pico_stdlib pico_multicore hardware_pio hardware_spi hardware_gpio hardware_clocks hardware_vreg hardware_watchdog hardware_sync hardware_resets)

target_compile_options(isa_lpc PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(isa_lpc PRIVATE PICO_STACK_SIZE=0x800 PICO_USE_STACK_GUARDS=0)


set(FW_SYSCLK_KHZ 400000 CACHE STRING "system clock in kHz, 340000..500000 (fw/sim window)")
set(FW_VREG "" CACHE STRING "vreg enum, e.g. VREG_VOLTAGE_1_30 (empty = table in main.c)")
target_compile_definitions(isa_lpc PRIVATE SYSCLK_KHZ=${FW_SYSCLK_KHZ})
if(NOT FW_VREG STREQUAL "")
    target_compile_definitions(isa_lpc PRIVATE FW_VREG=${FW_VREG})
endif()

option(FW_RAM "build for RAM only (load with the debugger, no flash writes)" OFF)
if(FW_RAM)
    pico_set_binary_type(isa_lpc no_flash)
    target_compile_definitions(isa_lpc PRIVATE FW_RAM_BUILD=1)
else()
    pico_set_binary_type(isa_lpc copy_to_ram)              # boot copies the image to SRAM; nothing runs from flash
endif()

pico_enable_stdio_uart(isa_lpc 0)
pico_enable_stdio_usb(isa_lpc 0)
pico_add_extra_outputs(isa_lpc)
