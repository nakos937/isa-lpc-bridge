// SPDX-License-Identifier: GPL-2.0-or-later
// Pin map of PASS_HG25 (checked against the KiCad RP2354B symbol).
#pragma once
#include <stdint.h>

// --- DMA acknowledge: 74AHCT138 (A,B,C = channel number, G1 = AEN) ---
#define PIN_DACK_A      0
#define PIN_DACK_B      1
#define PIN_DACK_C      2
#define PIN_AEN         3       // via 74AHCT244 to ISA AEN and to the 138 enable
// --- address / IRQ chain: 3 x 74AHCT595 (SA0..19, SBHE#) and 2 x 74LVC165 (IOCS16#, IRQ3-7, 9-12, 14, 15, IOCHCK#) ---
#define PIN_CH_MISO     4
#define PIN_CH_RCLK     5
#define PIN_CH_SCK      6
#define PIN_CH_MOSI     7
#define PIN_CH_SHLD     8
// --- command strobes via 74AHCT244 (active low on ISA) ---
#define PIN_IOR_N       9
#define PIN_IOW_N       10
#define PIN_SD_OE_N     11      // data transceivers 74LVC4245A, low = enabled
#define PIN_IOCHRDY     12      // input, 4.7k pull-up (3.3 V) on the board
#define PIN_TC          13      // via 74AHCT244 to ISA TC
#define PIN_ISA_RUN     14      // low = RESETDRV asserted (R14 pulls low), high = bus released (if PCIRST# is high too)
#define PIN_PCIRST_N    15      // input, host PCI reset, mirrors the reset input of the NAND
// --- ISA data bus SD0..SD15 through the transceivers ---
#define PIN_SD0         16      // GPIO16..31
// --- DMA requests, active high, external 4.7k pull-downs ---
#define PIN_DRQ0        32
#define PIN_DRQ1        33
#define PIN_DRQ2        34
#define PIN_DRQ3        35
#define PIN_DRQ5        36
#define PIN_DRQ6        37
#define PIN_DRQ7        38
#define PIN_SD_DIR      39      // transceiver direction: 1 = slot -> MCU (read), 0 = MCU -> slot (write)
// --- LPC (TPM header J4) ---
#define PIN_LAD0        40      // LAD0..LAD3 = GPIO40..43
#define PIN_LFRAME_N    44
#define PIN_LCLK        45
#define PIN_SERIRQ      46
#define PIN_LDRQ_N      47

// GPIO bank 0 masks (SIO gpio_* registers); bank 1 (gpio_hi_*) bit = gpio - 32
#define M(p)            (1u << (p))
#define MH(p)           (1u << ((p) - 32))
#define SD_MASK         (0xFFFFu << PIN_SD0)
