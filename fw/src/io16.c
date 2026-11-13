// SPDX-License-Identifier: GPL-2.0-or-later
// 16 bit ISA I/O from LPC byte cycles: the PCH splits a word access into byte cycles to a and a + 1. On a port that answers
// IOCS16# the two become one word cycle.
#include "io16.h"
#include "isa.h"
#include "cfg.h"

#define IO16_PAIR_US 5u

static struct { uint16_t addr; uint8_t data; bool valid, fresh; uint32_t t; } wbuf, rbuf;   // fresh: t is set by the next tick

static bool word_rd(uint16_t a) {
    for (unsigned i = 0; i < 3; i++) {
        uint32_t r = dbg.cfg.word_rd[i];
        if (r && a >= (r & 0xFFFFu) && a <= (r >> 16)) return true;
    }
    return false;
}

void io16_flush(void) {
    if (wbuf.valid) { wbuf.valid = false; isa_io_write(wbuf.addr, wbuf.data); }
    rbuf.valid = false;
}

void io16_drop(void) { wbuf.valid = false; rbuf.valid = false; isa_addr_forget(); }

void io16_tick(uint32_t now) {
    if (wbuf.valid && now - wbuf.t >= IO16_PAIR_US) io16_flush();
    if (rbuf.valid && rbuf.fresh) { rbuf.t = now; rbuf.fresh = false; }   // the window starts after the ISA cycle (IOCHRDY may stretch it)
    else if (rbuf.valid && now - rbuf.t >= IO16_PAIR_US) rbuf.valid = false;
}

void io16_write(uint16_t a, uint8_t d, uint32_t now) {
    rbuf.valid = false;
    if (wbuf.valid && a == (uint16_t)(wbuf.addr + 1) && now - wbuf.t < IO16_PAIR_US) {
        wbuf.valid = false;
        isa_io_write16(wbuf.addr, (uint16_t)(wbuf.data | (d << 8)));
        return;
    }
    io16_flush();
    if (!(a & 1) && (word_rd(a) ? isa_iocs16w(a) : isa_iocs16(a))) { wbuf.addr = a; wbuf.data = d; wbuf.t = now; wbuf.valid = true; }   // wait for the high byte
    else isa_io_write(a, d);
}

uint8_t io16_read(uint16_t a, uint32_t now) {
    if (wbuf.valid) io16_flush();
    if (rbuf.valid && rbuf.addr == a && (rbuf.fresh || now - rbuf.t < IO16_PAIR_US)) { rbuf.valid = false; return rbuf.data; }
    rbuf.valid = false;
    if ((a & 1) || !word_rd(a) || !isa_iocs16w(a)) return isa_io_read(a);   // a word read also reads port + 1: listed ports only
    uint16_t v = isa_io_read16(a);
    rbuf.addr = a + 1; rbuf.data = (uint8_t)(v >> 8); rbuf.fresh = true; rbuf.valid = true;
    return (uint8_t)v;
}
