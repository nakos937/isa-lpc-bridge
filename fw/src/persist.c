// SPDX-License-Identifier: GPL-2.0-or-later
// Saved bridge config: dbg.cfg (port filter, IRQ list, timing, flags) in flash, so the bridge answers right after power-up.
// The PCH decode windows are not part of it: the BIOS resets those every boot and the host tool programs them again.
#include "persist.h"
#include "persist_hw.h"
#include <string.h>
#include "cfg.h"

#define PERSIST_MAGIC  0x53504C49u                // "ILPS"
#define PERSIST_LAYOUT 2u                         // bump when fw_cfg_t changes shape

typedef struct { uint32_t magic, layout, size, crc, sysclk_khz; fw_cfg_t cfg; } block_t;
_Static_assert(sizeof(block_t) <= PERSIST_BYTES, "saved block does not fit its flash area");

static volatile char req;
static uint8_t status;

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u)); }
    return ~c;
}

static bool usable(const block_t *b, uint32_t sysclk_khz) {
    return b->magic == PERSIST_MAGIC && b->layout == PERSIST_LAYOUT && b->size == sizeof(fw_cfg_t) && b->sysclk_khz == sysclk_khz
        && b->crc == crc32((const uint8_t *)&b->cfg, sizeof b->cfg);
}

bool persist_valid(void) {
    const block_t *b = (const block_t *)persist_hw_data();
    return usable(b, dbg.cfg.sysclk_khz);
}

// A saved config must never lock the host out: these flags and the config port itself are forced back.
static void sanitize(fw_cfg_t *c) {
    c->flags &= ~(F_STOP_LPC | F_NO_CFGPORT | F_HOLD_RESET);
    c->deny[0x4E >> 3] &= (uint8_t)~(3u << (0x4E & 7));
}

bool persist_load(void) {
    const block_t *b = (const block_t *)persist_hw_data();
    if (!usable(b, dbg.cfg.sysclk_khz)) return false;
    uint32_t build = dbg.cfg.build;
    memcpy(&dbg.cfg, &b->cfg, sizeof dbg.cfg);
    dbg.cfg.magic = FW_MAGIC; dbg.cfg.build = build;
    sanitize(&dbg.cfg);
    return true;
}

void persist_request(char cmd) {
    if (cmd != 'W' && cmd != 'X') return;
    status = PERSIST_STATUS_BUSY; req = cmd;
}

void persist_poll(void) {
    char c = req;
    if (!c) return;
    req = 0;
    bool ok;
    if (c == 'X') ok = persist_hw_erase();
    else {
        static uint8_t buf[PERSIST_BYTES];
        memset(buf, 0xFF, sizeof buf);
        block_t *b = (block_t *)buf;
        b->magic = PERSIST_MAGIC; b->layout = PERSIST_LAYOUT; b->size = sizeof(fw_cfg_t); b->sysclk_khz = dbg.cfg.sysclk_khz;
        memcpy(&b->cfg, &dbg.cfg, sizeof dbg.cfg);
        sanitize(&b->cfg);
        b->crc = crc32((const uint8_t *)&b->cfg, sizeof b->cfg);
        ok = persist_hw_write(buf);
    }
    status = ok ? PERSIST_STATUS_OK : PERSIST_STATUS_ERROR;
}

uint8_t persist_status(void) { return status; }
