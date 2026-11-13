// SPDX-License-Identifier: GPL-2.0-or-later
// Flash access for the saved bridge config: last 16 KB of the in-package flash. The whole image runs from RAM (copy_to_ram),
// so XIP can be switched off for an erase; core 1 runs from scratch X and never reads flash.
#include "persist_hw.h"
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#define PERSIST_OFFSET (PICO_FLASH_SIZE_BYTES - 0x4000u)
#define PERSIST_SECTORS 3u                        // 3 x 4 KB hold PERSIST_BYTES

extern void flash_timing_restore(void);           // main.c: the ROM resets the QMI timing for the overclock

const uint8_t *persist_hw_data(void) { return (const uint8_t *)(XIP_BASE + PERSIST_OFFSET); }

static void kick(void) {
#ifndef FW_RAM_BUILD
    watchdog_update();                            // a sector erase takes 45 ms typical, 400 ms worst; the watchdog is 500 ms
#endif
}

bool persist_hw_erase(void) {
    for (uint32_t s = 0; s < PERSIST_SECTORS; s++) {
        kick();
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(PERSIST_OFFSET + s * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
        restore_interrupts(ints);
    }
    flash_timing_restore();
    kick();
    for (uint32_t i = 0; i < PERSIST_BYTES; i++) if (persist_hw_data()[i] != 0xFF) return false;
    return true;
}

bool persist_hw_write(const uint8_t *buf) {
    if (!persist_hw_erase()) return false;
    for (uint32_t o = 0; o < PERSIST_BYTES; o += FLASH_PAGE_SIZE * 16) {
        uint32_t n = PERSIST_BYTES - o < FLASH_PAGE_SIZE * 16 ? PERSIST_BYTES - o : FLASH_PAGE_SIZE * 16;
        kick();
        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(PERSIST_OFFSET + o, buf + o, n);
        restore_interrupts(ints);
    }
    flash_timing_restore();
    kick();
    return memcmp(persist_hw_data(), buf, PERSIST_BYTES) == 0;
}
