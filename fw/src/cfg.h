// SPDX-License-Identifier: GPL-2.0-or-later
// Debug / tuning block at a fixed RAM address (0x2007C000, plain pointer, nothing is linked there).
// Everything here is read live, so timing, port filters and cycles can be changed from the debugger without a reflash:
//   openocd: mww 0x2007C008 500      (t_cmd_ns)       mdw 0x2007C000 16     (cfg + stats)
// The mailbox lets the debugger run ISA cycles with no LPC host at all (bring-up of cards and wiring).
#pragma once
#include <stdint.h>

#define FW_MAGIC   0x31415349u          // "ISA1"
#define FW_DBG_ADDR 0x2007C000u

enum {
    F_TRACE        = 1u << 0,           // log every LPC cycle into the trace ring
    F_DMA          = 1u << 1,           // serve DMA channels 0-3 (8 bit) and 5-7 (16 bit) via LDRQ#
    F_NO_SERIRQ    = 1u << 2,           // do not feed IRQ states to SERIRQ
    F_HOLD_RESET   = 1u << 3,           // keep ISA RESETDRV asserted
    F_NO_POST      = 1u << 4,           // I/O writes are acknowledged after the ISA cycle (default: before, posted); the low byte of a
                                        // word to an IOCS16# port is still acknowledged when held, its word cycle runs with the high byte
    F_SYNC_SHORT   = 1u << 5,           // wait-state SYNC 0101 instead of 0110 (needs a reboot or CMD_SYNC)
    F_STOP_LPC     = 1u << 6,           // ignore LPC words (debug)
    F_FAST_EDGES   = 1u << 7,           // SCK fast slew + 8 mA, IOR#/IOW# 8 mA (default: slow slew, 4 mA)
    F_CHAIN_GPIO   = 1u << 8,           // 595/165 chain bit-banged by the CPU instead of SPI0 (needs a reboot)
    F_NO_CFGPORT   = 1u << 9,           // ports 4E/4F go to the ISA slot instead of the config port (cfgport.c)
};

typedef struct {
    uint32_t magic;                     // +0x00
    uint32_t build;                     // +0x04 firmware build counter
    uint32_t t_cmd_ns;                  // +0x08 minimum IOR#/IOW# low time
    uint32_t t_addr_setup_ns;           // +0x0C SA valid before the strobe
    uint32_t t_hold_ns;                 // +0x10 data / address hold after the strobe
    uint32_t t_rdy_timeout_ns;          // +0x14 IOCHRDY wait limit
    uint32_t chain_nops;                // +0x18 extra delay turns (4 sys clocks) per half clock of the bit-banged chain
    uint32_t flags;                     // +0x1C
    uint32_t sysclk_khz;                // +0x20 (read only)
    uint32_t irq_hold_us;               // +0x24 pulse stretching: an IRQ seen high stays asserted in SERIRQ this long (default 30)
    uint32_t t_drq_ns;                  // +0x28 wait before DRQ is sampled after a DMA strobe (card needs time to drop it)
    uint32_t irq_frames;                // +0x2C SERIRQ frames this board serves (bit n = IRQ n, bit 16 IOCHCK#); others stay with the Super I/O
    uint32_t t_cs16_ns;                 // +0x30 IOCS16# sampled this long after the address is latched
    uint32_t word_rd[3];                // +0x34 port ranges (first | last << 16, 0 = unused) where an IN to an even IOCS16# port runs a
                                        //       word cycle and keeps the high byte for the IN to port + 1; elsewhere a byte cycle, as on an AT

    uint8_t  deny[8192];                // +0x40 port filter: bit set = cycle not claimed (left to the Super I/O or to the host abort)
} fw_cfg_t;

typedef struct {
    uint32_t ior, iow, dma_r, dma_w;    // cycles served
    uint32_t rdy_timeouts;              // IOCHRDY never came
    uint32_t denied;                    // cycles not claimed (port filter, DMA channel not requested)
    uint32_t lpc_words;                 // words from the front end
    uint32_t stale_tx;                  // stale replies drained
    uint32_t irq_changes;               // SERIRQ mask updates
    uint32_t ldrq_sent;
    uint32_t irq_stream;                // last 165 chain readout (bit0 IOCS16#, 1-10 IRQ4-7,9-12,14,15, 11 IOCHCK#, 12 IRQ3)
    uint32_t drq;                       // last DRQ sample (bit n = DRQn, bit4 = 0)
    uint32_t last_addr, last_data, last_type;
    uint32_t rx_overflow_hint;          // cycles dropped: core 1 -> core 0 ring full
    uint32_t loops;                     // main loop counter (watchdog proof)
    uint32_t resets;                    // PCIRST# edges handled
} fw_stats_t;

typedef struct {
    volatile uint32_t cmd;              // write last: 1 IOR, 2 IOW, 3 chain word, 4 IRQ scan, 5 DRQ, 6 reset pulse, 7 set sync
    volatile uint32_t a0, a1;
    volatile uint32_t r0, r1;
    volatile uint32_t seq;              // incremented when the command finished (cmd is cleared at the same time)
} fw_mbox_t;

enum { CMD_NONE = 0, CMD_IOR = 1, CMD_IOW = 2, CMD_CHAIN = 3, CMD_IRQ = 4, CMD_DRQ = 5, CMD_RESET = 6, CMD_SYNC = 7 };

typedef struct { uint32_t ts; uint32_t info; } fw_trace_t;   // info = type<<28 | addr<<8... see lpc.c

#define TRACE_N 256
typedef struct {
    fw_cfg_t   cfg;
    fw_stats_t st;
    fw_mbox_t  mbox;
    uint32_t   trace_head;
    fw_trace_t trace[TRACE_N];
} fw_dbg_t;

#ifndef dbg
#define dbg (*(fw_dbg_t *)FW_DBG_ADDR)
#endif
