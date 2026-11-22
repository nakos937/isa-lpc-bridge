// SPDX-License-Identifier: GPL-2.0-or-later
// ISA bus master: CPU driven cycles through the 595/165 chain, the AHCT244 strobes and the LVC4245A data transceivers.
#include "isa.h"
#include "cfg.h"
#include "hw.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/structs/sio.h"
#include "hardware/gpio_coproc.h"
#include "hardware/spi.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/m33.h"

#define RAM_FUNC(f) __not_in_flash_func(f)

static uint32_t cyc_per_us;                 // sysclk in MHz
static uint32_t cur_word = 0xFFFFFFFFu;     // last word shifted into the 595s (cache)
static uint32_t irq_stream;
static bool cs16;                           // IOCS16# at cur_word
static uint32_t t_addr, t_idle;             // DWT stamps: 595 outputs latched, last strobe released

static inline uint32_t cyc(void) { return m33_hw->dwt_cyccnt; }
static inline void until(uint32_t t0, uint32_t ns) { uint32_t c = (ns * cyc_per_us) / 1000u; while (cyc() - t0 < c) {} }
static inline void bus_idle(void) { until(t_idle, dbg.cfg.t_hold_ns); }           // a read returns at once; its hold time is paid here
static inline void addr_setup(void) {                                             // free when the address did not change
    until(t_addr, dbg.cfg.t_addr_setup_ns);
    until(t_idle, dbg.cfg.t_hold_ns + dbg.cfg.t_addr_setup_ns);                   // strobe recovery, as with fixed delays
}

void RAM_FUNC(isa_ndelay)(uint32_t ns) {
    uint32_t c = (ns * cyc_per_us) / 1000u;
    if (c < 2) c = 2;
    busy_wait_at_least_cycles(c);
}

static inline void nops(uint32_t n) { while (n--) __asm volatile("nop"); }

// ---------------------------------------------------------------------------------------------
// chain: 24 bits, MSB first (bit23 ends in U7.QH ... bit0 = U5.QA = SA0). Word = SA[15:0] | SBHE#<<16 | SA[19:16]<<17.
// The 165s are loaded first (SH/LD# low), the stream read on MISO is IOCS16#,IRQ4,5,6,7,9,10,11,12,14,15,IOCHCK#,IRQ3.
// ---------------------------------------------------------------------------------------------
// Background IRQ scans: SPI0 mode 3 clocks two bytes out of the 165s (RX 4, SCK 6). RCLK stays high, so the 595 outputs keep
// the address while their shift registers take garbage; an address change shifts all 24 bits in anyway and abandons a
// running scan (the next one is 5 us away). SCK idles high in both modes, so switching the pin function makes no edge.
#define CHAIN_SPI_HZ 25000000u                   // clk_peri is 48 MHz (USB PLL after set_sys_clock): runs at 24 MHz
static bool chain_spi, chain_busy, pins_on_spi, scan_void, scan_skip;
static const uint scan_pins[] = {PIN_CH_MISO, PIN_CH_SCK};

static void RAM_FUNC(chain_pins_func)(bool spi) {
    for (unsigned i = 0; i < 2; i++) io_bank0_hw->io[scan_pins[i]].ctrl = spi ? GPIO_FUNC_SPI : GPIO_FUNC_SIO;
    pins_on_spi = spi;
}

static inline uint32_t rbit(uint32_t v) { uint32_t r; __asm volatile("rbit %0, %1" : "=r"(r) : "r"(v)); return r; }

static void RAM_FUNC(spi_kick)(void) {
    if (!pins_on_spi) chain_pins_func(true);
    gpioc_lo_out_clr(M(PIN_CH_SHLD));
    nops(4);
    gpioc_lo_out_set(M(PIN_CH_SHLD));
    spi_hw_t *h = spi_get_hw(spi0);
    h->dr = 0; h->dr = 0;
    chain_busy = true;
}

static void RAM_FUNC(spi_collect)(void) {
    spi_hw_t *h = spi_get_hw(spi0);
    uint32_t v = 0;
    for (int i = 0; i < 2; i++) {
        while (!(h->sr & SPI_SSPSR_RNE_BITS)) {}
        v = (v << 8) | (h->dr & 0xFFu);
    }
    while (h->sr & SPI_SSPSR_BSY_BITS) {}
    chain_busy = false;
    if (scan_void) { scan_void = false; return; }
    scan_skip = false;
    irq_stream = rbit(v) >> 16;                              // first bit in = stream bit 0
    dbg.st.irq_stream = irq_stream;
}

static void RAM_FUNC(chain_addr)(uint32_t w) {              // 595s only, no IRQ readout
    if (chain_busy && !scan_void) {                          // abandon a running scan, but never two in a row (IRQ latency <= 10 us)
        if ((spi_get_hw(spi0)->sr & SPI_SSPSR_BSY_BITS) && !scan_skip) scan_void = scan_skip = true;
        else spi_collect();
    }
    if (pins_on_spi) chain_pins_func(false);
    const uint32_t n = dbg.cfg.chain_nops;
    gpioc_lo_out_clr(M(PIN_CH_RCLK));
    // low phase 5 sys clocks (AHCT595: 5.5 ns pulse, 15.4 ns period through QH'); a nops() turn costs 4
#define SHIFT24(extra) for (int i = 23; i >= 0; i--) { \
        gpioc_bit_out_put(PIN_CH_MOSI, (w >> i) & 1u); \
        gpioc_lo_out_clr(M(PIN_CH_SCK)); __asm volatile("nop\nnop\nnop\nnop"); extra; \
        gpioc_lo_out_set(M(PIN_CH_SCK)); extra; }           /* high phase: the loop itself, 4 or more */
    if (n) SHIFT24(nops(n)) else SHIFT24((void)0)
#undef SHIFT24
    nops(2);                                                 // SRCLK rise 5 ns before RCLK rise
    gpioc_lo_out_set(M(PIN_CH_RCLK));
    cur_word = w; t_addr = cyc();
}

static void RAM_FUNC(sense_cs16)(void) {                    // U8.H = IOCS16#: QH (MISO) follows it while SH/LD# is low
    gpioc_lo_out_clr(M(PIN_CH_SHLD));
    until(t_addr, dbg.cfg.t_cs16_ns);
    cs16 = !((gpioc_lo_in_get() >> PIN_CH_MISO) & 1u);
    gpioc_lo_out_set(M(PIN_CH_SHLD));
}

uint32_t RAM_FUNC(isa_chain_raw)(uint32_t w) {              // full shift with IRQ readout: mailbox, F_CHAIN_GPIO
    if (chain_busy) spi_collect();
    if (pins_on_spi) chain_pins_func(false);
    const uint32_t n = dbg.cfg.chain_nops;
    uint32_t in = 0;
    gpioc_lo_out_clr(M(PIN_CH_SHLD));
    nops(3 + n);
    gpioc_lo_out_set(M(PIN_CH_SHLD));
    nops(3 + n);
    for (int i = 23; i >= 0; i--) {
        if ((w >> i) & 1) gpioc_lo_out_set(M(PIN_CH_MOSI)); else gpioc_lo_out_clr(M(PIN_CH_MOSI));
        in |= ((gpioc_lo_in_get() >> PIN_CH_MISO) & 1u) << (23 - i);
        gpioc_lo_out_clr(M(PIN_CH_SCK));
        nops(1 + n);
        gpioc_lo_out_set(M(PIN_CH_SCK));                  // rising edge shifts 595 and 165
        nops(3 + n);                                      // QH settles before the next sample
    }
    gpioc_lo_out_clr(M(PIN_CH_RCLK));
    nops(3 + n);
    gpioc_lo_out_set(M(PIN_CH_RCLK));                     // rising edge latches the 595 outputs
    bool moved = w != cur_word;
    cur_word = w; t_addr = cyc();
    if (moved) sense_cs16();
    irq_stream = in & 0xFFFFu;
    dbg.st.irq_stream = irq_stream;
    return irq_stream;
}

static inline uint32_t addr_word(uint16_t a) { return (uint32_t)a | ((~a & 1u) << 16); }   // byte cycle: SBHE# low on odd addresses
static inline void set_word(uint32_t w) { if (w == cur_word) return; chain_addr(w); sense_cs16(); }

bool RAM_FUNC(isa_iocs16)(uint16_t a) { bus_idle(); set_word(addr_word(a)); return cs16; }
bool RAM_FUNC(isa_iocs16w)(uint16_t a) { bus_idle(); set_word(a); return cs16; }   // SBHE# low: a word cycle follows without a reload
void isa_addr_forget(void) { cur_word = 0xFFFFFFFFu; }       // host reset: the cards' decode may change

void RAM_FUNC(isa_chain_refresh)(void) {
    if (!chain_spi) { isa_chain_raw(cur_word); return; }
    if (chain_busy) spi_collect();
    spi_kick();                                               // collected by the next isa_irq_stream()
}
uint32_t RAM_FUNC(isa_irq_stream)(void) { if (chain_busy) spi_collect(); return irq_stream; }

// stream bit -> ISA IRQ number (frame index); bit 0 = IOCS16#, bit 11 = IOCHCK# (active low), frame 16
uint32_t RAM_FUNC(isa_serirq_mask)(uint32_t s) {
    static const uint8_t frame[13] = {0, 4, 5, 6, 7, 9, 10, 11, 12, 14, 15, 0, 3};
    uint32_t m = 0;
    for (int i = 1; i < 13; i++) if (frame[i] && ((s >> i) & 1u)) m |= 1u << frame[i];
    if (!((s >> 11) & 1u)) m |= 1u << 16;
    return m;
}

uint32_t RAM_FUNC(isa_drq)(void) {
    uint32_t h = gpioc_hi_in_get();        // bit0 = GPIO32 = DRQ0
    return (h & 0xFu) | (((h >> 4) & 0x7u) << 5);   // DRQ5..7 are GPIO36..38 -> bits 5..7
}

bool RAM_FUNC(isa_pcirst_n)(void) { return (gpioc_lo_in_get() >> PIN_PCIRST_N) & 1u; }

// ---------------------------------------------------------------------------------------------
// strobe helpers
// ---------------------------------------------------------------------------------------------
static inline bool rdy(void) { return (gpioc_lo_in_get() >> PIN_IOCHRDY) & 1u; }

static void RAM_FUNC(wait_rdy)(void) {
    if (rdy()) return;
    uint32_t t0 = cyc(), c = (dbg.cfg.t_rdy_timeout_ns * cyc_per_us) / 1000u;
    while (!rdy()) if (cyc() - t0 >= c) { dbg.st.rdy_timeouts++; return; }
    isa_ndelay(125);                                          // command hold from IOCHRDY (ISA timing 22): the card may count on it
}

static inline void bus_read_dir(void)  { gpioc_hi_out_set(MH(PIN_SD_DIR)); }   // slot -> MCU
static inline void bus_write_dir(void) { gpioc_hi_out_clr(MH(PIN_SD_DIR)); }   // MCU -> slot

// byte cycle with AT byte lanes: at an odd address a 16 bit port (IOCS16#) answers on SD8..15; writes carry the byte on both lanes
uint8_t RAM_FUNC(isa_io_read)(uint16_t addr) {
    bus_idle();
    set_word(addr_word(addr));
    addr_setup();
    gpioc_lo_out_clr(M(PIN_IOR_N));
    isa_ndelay(cs16 ? dbg.cfg.t_cmd_ns : dbg.cfg.t_cmd8_ns);
    wait_rdy();
    uint32_t v = gpioc_lo_in_get() >> PIN_SD0;
    gpioc_lo_out_set(M(PIN_IOR_N));
    t_idle = cyc();
    return (uint8_t)((addr & 1) && cs16 ? v >> 8 : v);
}

static inline void write_bus_begin(uint16_t d) {   // all 16 lines: an undriven lane would float the transceiver inputs
    gpioc_lo_out_set(M(PIN_SD_OE_N));              // transceivers off
    bus_write_dir();
    gpioc_lo_out_clr(SD_MASK); gpioc_lo_out_set((uint32_t)d << PIN_SD0);     // transceivers are off here
    gpioc_lo_oe_set(SD_MASK);
    nops(4);
    gpioc_lo_out_clr(M(PIN_SD_OE_N));              // transceivers on, MCU -> slot
}

static inline void write_bus_end(void) {
    gpioc_lo_out_set(M(PIN_SD_OE_N));
    gpioc_lo_oe_clr(SD_MASK);
    bus_read_dir();
    nops(4);
    gpioc_lo_out_clr(M(PIN_SD_OE_N));              // idle: slot -> MCU, so the MCU inputs never float
}

void RAM_FUNC(isa_io_write)(uint16_t addr, uint8_t d) {
    bus_idle();
    set_word(addr_word(addr));
    write_bus_begin((uint16_t)(d * 0x101u));
    addr_setup();
    gpioc_lo_out_clr(M(PIN_IOW_N));
    isa_ndelay(cs16 ? dbg.cfg.t_cmd_ns : dbg.cfg.t_cmd8_ns);
    wait_rdy();
    gpioc_lo_out_set(M(PIN_IOW_N));
    t_idle = cyc();
    isa_ndelay(dbg.cfg.t_hold_ns);
    write_bus_end();
}

// 16 bit I/O (AT byte lanes, SBHE# low): even address = word on SD0..15, odd address = byte on SD8..15
uint16_t RAM_FUNC(isa_io_read16)(uint16_t addr) {
    bus_idle();
    set_word(addr);
    addr_setup();
    gpioc_lo_out_clr(M(PIN_IOR_N));
    isa_ndelay(dbg.cfg.t_cmd_ns);
    wait_rdy();
    uint16_t v = (uint16_t)(gpioc_lo_in_get() >> PIN_SD0);
    gpioc_lo_out_set(M(PIN_IOR_N));
    t_idle = cyc();
    return v;
}

void RAM_FUNC(isa_io_write16)(uint16_t addr, uint16_t d) {
    bus_idle();
    set_word(addr);
    write_bus_begin(d);
    addr_setup();
    gpioc_lo_out_clr(M(PIN_IOW_N));
    isa_ndelay(dbg.cfg.t_cmd_ns);
    wait_rdy();
    gpioc_lo_out_set(M(PIN_IOW_N));
    t_idle = cyc();
    isa_ndelay(dbg.cfg.t_hold_ns);
    write_bus_end();
}

// ---------------------------------------------------------------------------------------------
// DMA: DACK n# = 74AHCT138 output n, enabled by AEN. Strobe with AEN high, then release in reverse order.
// ---------------------------------------------------------------------------------------------
static inline void dack_on(unsigned ch) {
    const uint32_t m = M(PIN_DACK_A) | M(PIN_DACK_B) | M(PIN_DACK_C);
    gpioc_lo_out_clr(m);
    gpioc_lo_out_set(((uint32_t)(ch & 7) << PIN_DACK_A));
    nops(2);
    gpioc_lo_out_set(M(PIN_AEN));
}
// drop the 138 enable (AEN) before the select lines, or DACK0# glitches
static inline void dack_off(void) {
    gpioc_lo_out_clr(M(PIN_AEN));
    nops(8);
    gpioc_lo_out_clr(M(PIN_DACK_A) | M(PIN_DACK_B) | M(PIN_DACK_C));
}

uint8_t RAM_FUNC(isa_dma_read)(unsigned ch, bool tc) {
    bus_idle();
    dack_on(ch);
    if (tc) gpioc_lo_out_set(M(PIN_TC));
    isa_ndelay(dbg.cfg.t_dack_setup_ns);
    gpioc_lo_out_clr(M(PIN_IOR_N));
    isa_ndelay(dbg.cfg.t_dma_cmd_ns);
    wait_rdy();
    uint8_t v = (uint8_t)(gpioc_lo_in_get() >> PIN_SD0);
    gpioc_lo_out_set(M(PIN_IOR_N));
    t_idle = cyc();
    isa_ndelay(dbg.cfg.t_dack_hold_ns);
    gpioc_lo_out_clr(M(PIN_TC));
    dack_off();
    return v;
}

void RAM_FUNC(isa_dma_write)(unsigned ch, bool tc, uint8_t d) {
    bus_idle();
    write_bus_begin((uint16_t)(d * 0x101u));
    dack_on(ch);
    if (tc) gpioc_lo_out_set(M(PIN_TC));
    isa_ndelay(dbg.cfg.t_dack_setup_ns);
    gpioc_lo_out_clr(M(PIN_IOW_N));
    isa_ndelay(dbg.cfg.t_dma_cmd_ns);
    wait_rdy();
    gpioc_lo_out_set(M(PIN_IOW_N));
    t_idle = cyc();
    isa_ndelay(dbg.cfg.t_dack_hold_ns);
    gpioc_lo_out_clr(M(PIN_TC));
    dack_off();
    write_bus_end();
}

// 16 bit DMA (channels 5..7): word transfer on SD0..15, SBHE# low, A0 = 0. DACK n# = 138 output n.
#define DMA_WORD (1u << 23)                 // SA = 0, SBHE# = 0; spare bit (U7.QH) so no I/O word equals it: DMA skips the IOCS16# sense
static inline void sbhe_low(void) { if (cur_word != DMA_WORD) chain_addr(DMA_WORD); }   // kept until the next I/O address

uint16_t RAM_FUNC(isa_dma16_read)(unsigned ch, bool tc) {
    bus_idle();
    sbhe_low();
    dack_on(ch);
    if (tc) gpioc_lo_out_set(M(PIN_TC));
    isa_ndelay(dbg.cfg.t_dack_setup_ns);
    gpioc_lo_out_clr(M(PIN_IOR_N));
    isa_ndelay(dbg.cfg.t_dma_cmd_ns);
    wait_rdy();
    uint16_t v = (uint16_t)(gpioc_lo_in_get() >> PIN_SD0);
    gpioc_lo_out_set(M(PIN_IOR_N));
    t_idle = cyc();
    isa_ndelay(dbg.cfg.t_dack_hold_ns);
    gpioc_lo_out_clr(M(PIN_TC));
    dack_off();
    return v;
}

void RAM_FUNC(isa_dma16_write)(unsigned ch, bool tc, uint16_t d) {
    bus_idle();
    sbhe_low();
    write_bus_begin(d);
    dack_on(ch);
    if (tc) gpioc_lo_out_set(M(PIN_TC));
    isa_ndelay(dbg.cfg.t_dack_setup_ns);
    gpioc_lo_out_clr(M(PIN_IOW_N));
    isa_ndelay(dbg.cfg.t_dma_cmd_ns);
    wait_rdy();
    gpioc_lo_out_set(M(PIN_IOW_N));
    t_idle = cyc();
    isa_ndelay(dbg.cfg.t_dack_hold_ns);
    gpioc_lo_out_clr(M(PIN_TC));
    dack_off();
    write_bus_end();
}

// ---------------------------------------------------------------------------------------------
void isa_hold_reset(bool hold) {
    if (hold) gpioc_lo_out_clr(M(PIN_ISA_RUN)); else gpioc_lo_out_set(M(PIN_ISA_RUN));
}

static void out_pin(uint pin, bool level) {
    gpio_init(pin);
    gpio_put(pin, level);
    gpio_set_dir(pin, GPIO_OUT);
}

void isa_init(void) {
    cyc_per_us = clock_get_hz(clk_sys) / 1000000u;
    m33_hw->demcr |= M33_DEMCR_TRCENA_BITS;                  // DWT cycle counter: bus timing here, DRQ quiet time in lpc.c
    m33_hw->dwt_ctrl |= M33_DWT_CTRL_CYCCNTENA_BITS;
    t_addr = t_idle = cyc();
    // outputs first with their idle level, then direction (the board pulls hold sane levels until here)
    out_pin(PIN_ISA_RUN, false);            // RESETDRV stays asserted, 595 outputs off
    out_pin(PIN_IOR_N, true);
    out_pin(PIN_IOW_N, true);
    out_pin(PIN_SD_OE_N, true);
    out_pin(PIN_SD_DIR, true);
    out_pin(PIN_AEN, false);
    out_pin(PIN_TC, false);
    out_pin(PIN_DACK_A, false);
    out_pin(PIN_DACK_B, false);
    out_pin(PIN_DACK_C, false);
    out_pin(PIN_CH_SCK, true);
    out_pin(PIN_CH_RCLK, true);
    out_pin(PIN_CH_MOSI, false);
    out_pin(PIN_CH_SHLD, true);
    chain_spi = !(dbg.cfg.flags & F_CHAIN_GPIO);
    if (chain_spi) {
        spi_init(spi0, CHAIN_SPI_HZ);
        spi_set_format(spi0, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
        for (unsigned i = 0; i < 2; i++) gpio_set_function(scan_pins[i], GPIO_FUNC_SPI);
        pins_on_spi = true;
    }
    if (dbg.cfg.flags & F_FAST_EDGES) {
        gpio_set_slew_rate(PIN_CH_SCK, GPIO_SLEW_RATE_FAST);
        gpio_set_drive_strength(PIN_CH_SCK, GPIO_DRIVE_STRENGTH_8MA);
        gpio_set_drive_strength(PIN_IOR_N, GPIO_DRIVE_STRENGTH_8MA);
        gpio_set_drive_strength(PIN_IOW_N, GPIO_DRIVE_STRENGTH_8MA);
    }
    // inputs: external pulls only (RP2350-E9: the internal pull-down is not reliable)
    const uint ins[] = {PIN_CH_MISO, PIN_IOCHRDY, PIN_DRQ0, PIN_DRQ1, PIN_DRQ2, PIN_DRQ3, PIN_DRQ5, PIN_DRQ6, PIN_DRQ7};
    for (unsigned i = 0; i < sizeof ins / sizeof ins[0]; i++) {
        gpio_init(ins[i]);
        gpio_set_dir(ins[i], GPIO_IN);
        gpio_disable_pulls(ins[i]);
    }
    gpio_init(PIN_PCIRST_N);                // host drives it push-pull; the pull-up keeps it out of the E9 leakage band with no host attached
    gpio_set_dir(PIN_PCIRST_N, GPIO_IN);
    gpio_pull_up(PIN_PCIRST_N);
    for (uint p = PIN_SD0; p < PIN_SD0 + 16; p++) {
        gpio_init(p);
        gpio_set_dir(p, GPIO_IN);
        gpio_disable_pulls(p);
    }
    gpioc_lo_out_clr(M(PIN_SD_OE_N));      // idle: transceivers enabled slot -> MCU (the 8.2k pull-ups make 0xFF)
    isa_chain_raw(addr_word(0));            // first load; SA outputs stay off until RESETDRV is released
    isa_addr_forget();                      // IOCS16# was sensed with the outputs off
}
