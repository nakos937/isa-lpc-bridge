// SPDX-License-Identifier: GPL-2.0-or-later
// cc -Isrc -o persist_test test/persist_test.c && ./persist_test       (logic only: fake flash)
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define dbg test_dbg
#include "../src/cfg.h"
static fw_dbg_t test_dbg;
#include "../src/persist_hw.h"
#include "../src/persist.c"

static uint8_t flash[PERSIST_BYTES]; static int writes, erases; static int fail;
const uint8_t *persist_hw_data(void) { return flash; }
bool persist_hw_erase(void) { erases++; memset(flash, 0xFF, sizeof flash); return !fail; }
bool persist_hw_write(const uint8_t *b) { writes++; if (fail) return false; memcpy(flash, b, sizeof flash); return true; }

int main(void) {
    memset(flash, 0xFF, sizeof flash);
    test_dbg.cfg.sysclk_khz = 400000; test_dbg.cfg.build = 12; test_dbg.cfg.magic = FW_MAGIC;
    assert(!persist_valid() && !persist_load() && persist_status() == PERSIST_STATUS_OK);       // blank flash
    test_dbg.cfg.irq_frames = 0x1234; test_dbg.cfg.deny[0x330 >> 3] = 0xA5; test_dbg.cfg.t_cmd_ns = 777;
    test_dbg.cfg.flags = F_DMA | F_STOP_LPC | F_NO_CFGPORT | F_HOLD_RESET | F_TRACE;
    test_dbg.cfg.deny[0x4E >> 3] = 0xFF;                                                          // a config that would lock the host out
    persist_request('?'); persist_poll(); assert(writes == 0);                                    // unknown command ignored
    persist_request('W'); assert(persist_status() == PERSIST_STATUS_BUSY); persist_poll();
    assert(persist_status() == PERSIST_STATUS_OK && writes == 1 && persist_valid());
    memset(&test_dbg.cfg, 0, sizeof test_dbg.cfg); test_dbg.cfg.sysclk_khz = 400000; test_dbg.cfg.build = 12; test_dbg.cfg.magic = FW_MAGIC;
    assert(persist_load());
    assert(test_dbg.cfg.irq_frames == 0x1234 && test_dbg.cfg.deny[0x330 >> 3] == 0xA5 && test_dbg.cfg.t_cmd_ns == 777);
    assert(test_dbg.cfg.flags == (F_DMA | F_TRACE));                                              // lock-out flags dropped
    assert((test_dbg.cfg.deny[0x4E >> 3] & 0xC0) == 0 && test_dbg.cfg.deny[0x4E >> 3] == 0x3F);      // ports 4E and 4F claimed again
    assert(test_dbg.cfg.magic == FW_MAGIC && test_dbg.cfg.build == 12);
    test_dbg.cfg.sysclk_khz = 300000; assert(!persist_valid() && !persist_load()); test_dbg.cfg.sysclk_khz = 400000;   // other clock
    flash[100] ^= 1; assert(!persist_valid()); flash[100] ^= 1; assert(persist_valid());          // bit flip caught by the CRC
    ((block_t *)flash)->layout = PERSIST_LAYOUT + 1; assert(!persist_valid()); ((block_t *)flash)->layout = PERSIST_LAYOUT;
    persist_request('X'); persist_poll(); assert(persist_status() == PERSIST_STATUS_OK && erases >= 1 && !persist_valid());
    fail = 1; persist_request('W'); persist_poll(); assert(persist_status() == PERSIST_STATUS_ERROR);   // flash error is reported
    puts("persist ok");
    return 0;
}
