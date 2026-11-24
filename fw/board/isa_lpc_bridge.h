// SPDX-License-Identifier: GPL-2.0-or-later
// Board definition for the RP2354B ISA-LPC bridge (pcb/krt/PASS_HG25). No UART/LED/USB: only SWD (J2).
#ifndef _BOARDS_ISA_LPC_BRIDGE_H
#define _BOARDS_ISA_LPC_BRIDGE_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define PICO_RP2350A 0                       // QFN-80, 48 GPIOs

#ifndef XOSC_HZ
#define XOSC_HZ 12000000
#endif

#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)   // RP2354B in-package W25Q16JV
#define PICO_FLASH_SPI_CLKDIV 4                   // boot value; main.c sets the divider for the overclock

#define PICO_DEFAULT_UART 0                       // never initialised (stdio off); only keeps pico_stdlib compiling
#define PICO_DEFAULT_UART_TX_PIN 0
#define PICO_DEFAULT_UART_RX_PIN 1

#endif
