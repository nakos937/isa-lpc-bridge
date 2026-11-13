// SPDX-License-Identifier: GPL-2.0-or-later
// Host config port, after the Fintek F85226 scheme: index 0x4E, data 0x4F. A DOS or Linux tool sets the bridge up through it
// (host/isalpc.c), no debug probe needed. Index port: 'I' 'S' 'A', offset low, offset high; then every data port access
// reads or writes one byte of fw_dbg_t at the offset and steps it. cfg is writable (not magic/build), stats are read only.
#include "cfgport.h"
#include "cfg.h"

static uint8_t st;                  // 0..2 key bytes seen, 3 offset low next, 4 offset high next, 5 open
static uint16_t off;

void cfgport_write(uint16_t a, uint8_t d) {
    if (!(a & 1)) {
        static const uint8_t key[3] = {'I', 'S', 'A'};
        if (st == 3) { off = d; st = 4; }
        else if (st == 4) { off |= (uint16_t)(d << 8); st = 5; }
        else st = (st < 3 && d == key[st]) ? st + 1 : (d == key[0]);
        return;
    }
    if (st != 5) return;
    // shortcut: 32 bit fields change one byte at a time, a cycle between two bytes sees a mixed value; add a latch if that bites
    if (off >= 8 && off < sizeof(fw_cfg_t)) ((uint8_t *)&dbg.cfg)[off] = d;
    off++;
}

uint8_t cfgport_read(uint16_t a) {
    if (!(a & 1) || st != 5) return 0xFF;
    uint8_t v = off < sizeof(fw_cfg_t) + sizeof(fw_stats_t) ? ((const uint8_t *)&dbg)[off] : 0xFF;
    off++;
    return v;
}
