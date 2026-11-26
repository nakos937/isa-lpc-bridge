// SPDX-License-Identifier: GPL-2.0-or-later
// ISA <-> LPC bridge firmware for the RP2354B board (see fw/README.md).
#include <string.h>
#include "cfg.h"
#include "hw.h"
#include "isa.h"
#include "lpc.h"
#include "cfgport.h"
#include "persist.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "hardware/structs/qmi.h"
#include "hardware/regs/qmi.h"
#include "hardware/regs/addressmap.h"

// System clock: cmake -DFW_SYSCLK_KHZ=... [-DFW_VREG=VREG_VOLTAGE_x]; above 1.30 V the SDK needs vreg_disable_voltage_limit() (done here).
#ifndef SYSCLK_KHZ
#define SYSCLK_KHZ 400000            // 1200 MHz / 3; 1.30 V from the table below
#endif
// Simulator window (fw/sim, LCLK duty 37..63 %, core 1 claim within 30 sys clocks): fails below ~320 MHz (claim misses the
// SYNC deadline) and above ~500 MHz. 400 MHz also passes with a 40 clock claim.
#if (SYSCLK_KHZ < 340000 || SYSCLK_KHZ > 500000) && !defined(FW_ALLOW_ANY_CLK)
#error "sysclk outside the 340..500 MHz window the LPC engines work in (override: -DFW_ALLOW_ANY_CLK)"
#endif
#ifndef FW_VREG
  #if SYSCLK_KHZ <= 150000
  #define FW_VREG VREG_VOLTAGE_1_10
  #elif SYSCLK_KHZ <= 264000
  #define FW_VREG VREG_VOLTAGE_1_15
  #elif SYSCLK_KHZ <= 300000
  #define FW_VREG VREG_VOLTAGE_1_20
  #elif SYSCLK_KHZ <= 400000
  #define FW_VREG VREG_VOLTAGE_1_30
  #elif SYSCLK_KHZ <= 550000
  #define FW_VREG VREG_VOLTAGE_1_60      // reported working at 540 MHz (Raspberry Pi forum), not verified here
  #else
  #error "above 550 MHz pass -DFW_VREG=VREG_VOLTAGE_x (the voltage is your call)"
  #endif
#endif
#define FLASH_MAX_KHZ 66000          // in-package W25Q16JV, kept well below its 133 MHz limit
#define BUILD 22

// fixed RAM address, outside the linker's view (image is far below 0x2007C000, stacks sit at 0x20080000): survives a reload
extern void mailbox_poll(void);

static void defaults(void) {
    dbg.cfg.magic = FW_MAGIC;
    dbg.cfg.build = BUILD;
    dbg.cfg.t_cmd_ns = 400;
    dbg.cfg.t_addr_setup_ns = 120;
    dbg.cfg.t_hold_ns = 60;
    dbg.cfg.t_rdy_timeout_ns = 16000;       // Ampro table 1 row 21: a card may hold IOCHRDY low 15.6 us
    {   // 595/165 chain: at least 12 ns per half clock; 5 sys clocks are fixed, each extra turn adds about 4
        uint32_t n = (12u * (clock_get_hz(clk_sys) / 1000000u) + 999u) / 1000u;
        dbg.cfg.chain_nops = n > 5 ? (n - 2) / 4 : 0;
    }
    dbg.cfg.flags = F_DMA;
    dbg.cfg.irq_hold_us = 30;
    dbg.cfg.t_drq_ns = 300;
    dbg.cfg.t_cmd8_ns = 550;
    dbg.cfg.t_dma_cmd_ns = 800;
    dbg.cfg.t_dack_setup_ns = 330;
    dbg.cfg.t_dack_hold_ns = 180;
    dbg.cfg.t_cs16_ns = 200;                // 595 RCLK to Q 11.5 + IOCS16# from SA 122 + rise through 1k + 165 H to QH 17.8
    // never claimed: 0x00-0xFF (Super I/O config 2E/4E, KBC 60/64, POST 0x80) and the ranges below. Other Super I/O ranges the
    // BIOS forwards must be denied too: host/isalpc rewrites deny[] to "all but the selected cards" through the config port.
    dbg.cfg.irq_frames = (1u << 3) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 15) | (1u << 16);   // not 4 (COM1), 6, 12 (PS/2 mouse), 14
    dbg.cfg.word_rd[0] = 0x620u | 0xE83u << 16;      // EMU8000 data ports, bases 620-680
    dbg.cfg.word_rd[1] = 0x344u | 0x344u << 16;      // GUS PnP / InterWave data port I16DP at base 240 (base + 0x104); move with the base
    dbg.cfg.word_rd[2] = 0;                          // IDE data ports (168, 170, 1E8, 1F0) would go here; drives sit on the host SATA board
    // dbg.cfg.word_rd[2] = 0x220u | 0x245u << 16;   // ADI/Echo PSS (Orchid SoundWave 32, Cardinal DSP16): Linux pss.c inw at base 220/240 +0, +2, +4
    memset(dbg.cfg.deny, 0, sizeof dbg.cfg.deny);
    memset(dbg.cfg.deny, 0xFF, 0x100 / 8);
    dbg.cfg.deny[CFGPORT >> 3] &= (uint8_t)~(3u << (CFGPORT & 7));   // config port 4E/4F
    static const uint16_t sio[][2] = {{0x290, 0x2A0}, {0x2F8, 0x300}, {0x378, 0x380}, {0x3E8, 0x3F0}, {0x3F0, 0x400}, {0x778, 0x780}, {0xCF8, 0xD00}};
    for (unsigned i = 0; i < sizeof sio / sizeof sio[0]; i++)   // Super I/O hardware monitor, COM1-3, LPT, FDC; PCI config
        for (unsigned a = sio[i][0]; a < sio[i][1]; a++) dbg.cfg.deny[a >> 3] |= 1u << (a & 7);
    memset(&dbg.st, 0, sizeof dbg.st);
    dbg.trace_head = 0;
}

// XIP flash clock divider, executed from RAM: SCK = sysclk / div must stay below the flash limit when the core is overclocked
static void __unused __no_inline_not_in_flash_func(flash_div)(uint32_t div, uint32_t rxdelay) {
    uint32_t t = qmi_hw->m[0].timing;
    t &= ~(QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
    qmi_hw->m[0].timing = t | (div << QMI_M0_TIMING_CLKDIV_LSB) | (rxdelay << QMI_M0_TIMING_RXDELAY_LSB);
    (void)*(volatile uint32_t *)XIP_NOCACHE_NOALLOC_BASE;        // dummy access so the new divider is in use
}

static uint32_t __unused fl_div, fl_rx;                           // what flash_div set last: a flash erase or program makes the ROM reset it
void __no_inline_not_in_flash_func(flash_timing_restore)(void) {
#ifndef FW_RAM_BUILD
    if (fl_div) flash_div(fl_div, fl_rx);
#endif
}

int main(void) {
#if SYSCLK_KHZ > 400000 || defined(FW_VREG_UNLIMITED)
    vreg_disable_voltage_limit();
#endif
    vreg_set_voltage(FW_VREG);
    busy_wait_ms(10);
#ifndef FW_RAM_BUILD
    uint32_t fdiv = (SYSCLK_KHZ + FLASH_MAX_KHZ - 1) / FLASH_MAX_KHZ;
    if (fdiv < 4) fdiv = 4;
    flash_div(fdiv, 2);                                  // divider before the clock goes up (rxdelay as boot2 left it)
#endif
    set_sys_clock_khz(SYSCLK_KHZ, false);
#ifndef FW_RAM_BUILD
    // RXDELAY counts half sys clocks; boot2 uses 2 at 150 MHz (6.7 ns pad round trip): keep that time at the new clock
    uint32_t rx = (2u * SYSCLK_KHZ + 149999u) / 150000u;
    fl_div = fdiv; fl_rx = rx > 7 ? 7 : rx;
    flash_div(fl_div, fl_rx);
#endif

    // a debugger may have written the config before the reset: keep it when the magic is intact
    dbg.cfg.sysclk_khz = clock_get_hz(clk_sys) / 1000u;
    if (dbg.cfg.magic != FW_MAGIC || dbg.cfg.build != BUILD) {
        defaults();
        dbg.cfg.sysclk_khz = clock_get_hz(clk_sys) / 1000u;
        persist_load();                                  // config saved with ISALPC SAVE, if any (same clock only)
    }

    isa_init();
    lpc_init();
    bool held = dbg.cfg.flags & F_HOLD_RESET;
    isa_hold_reset(held);                   // RESETDRV = NAND(ISA_RUN, PCIRST#): follows the host reset from now on unless held

#ifndef FW_RAM_BUILD
    watchdog_enable(500, true);              // a reset would boot the (empty) flash, not the RAM image: RAM builds run without
#endif
    uint32_t next_chain = 0;
    bool rst_prev = isa_pcirst_n();
    for (;;) {
        lpc_poll();                                 // LPC cycles first, always
        persist_poll();                             // a save or forget request from the config port (blocks up to 1.3 s)
        lpc_service_drq();                          // DRQ -> LDRQ#: cheap (one register read), every iteration
        bool r = isa_pcirst_n();
        if (r != rst_prev) {                        // host reset edge: flush every engine, the cards see RESETDRV in hardware
            rst_prev = r;
            lpc_reset();
            dbg.st.resets++;
        }
        uint32_t now = time_us_32();
        if ((int32_t)(now - next_chain) >= 0) {
            next_chain = now + 5;                   // IRQ scan period; the SPI scan runs in the background
            bool hold = dbg.cfg.flags & F_HOLD_RESET;
            if (hold != held) isa_hold_reset(held = hold);
            lpc_service_irq();                      // uses the previous scan
            lpc_poll();                             // a cycle claimed meanwhile waits for one step, not the whole block
            isa_chain_refresh();                    // starts the next one (SPI runs on its own)
            mailbox_poll();
#ifndef FW_RAM_BUILD
            watchdog_update();
#endif
            dbg.st.loops++;
        }
    }
}
