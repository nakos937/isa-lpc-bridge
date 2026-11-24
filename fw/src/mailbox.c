// SPDX-License-Identifier: GPL-2.0-or-later
// Debugger mailbox: run ISA cycles from the SWD probe without an LPC host.
//   openocd:  mww 0x2007D?? ...   (see tools/isa.tcl, which wraps these)
#include "cfg.h"
#include "isa.h"
#include "lpc.h"
#include "pico/stdlib.h"

static bool rst_active;
static uint32_t rst_until;

void mailbox_poll(void) {
    uint32_t cmd = dbg.mbox.cmd;
    if (!cmd) return;
    if (cmd == CMD_RESET) {                                  // never sleep: the LPC service must keep running
        uint32_t now = time_us_32();
        if (!rst_active) {
            isa_hold_reset(true);
            rst_until = now + 1000u * (dbg.mbox.a0 ? dbg.mbox.a0 : 10);
            rst_active = true;
            return;
        }
        if ((int32_t)(now - rst_until) < 0) return;
        isa_hold_reset(dbg.cfg.flags & F_HOLD_RESET);
        rst_active = false;
        dbg.mbox.cmd = 0;
        dbg.mbox.seq++;
        return;
    }
    switch (cmd) {
    case CMD_IOR:   dbg.mbox.r0 = isa_io_read((uint16_t)dbg.mbox.a0); break;
    case CMD_IOW:   isa_io_write((uint16_t)dbg.mbox.a0, (uint8_t)dbg.mbox.a1); break;
    case CMD_CHAIN: dbg.mbox.r0 = isa_chain_raw(dbg.mbox.a0 & 0xFFFFFFu); break;
    case CMD_IRQ:   isa_chain_refresh(); dbg.mbox.r0 = isa_irq_stream(); break;   // waits for this scan
    case CMD_DRQ:   dbg.mbox.r0 = isa_drq(); break;
    default: dbg.mbox.r0 = 0xDEADBEEFu; break;
    }
    dbg.mbox.cmd = 0;
    dbg.mbox.seq++;
}
