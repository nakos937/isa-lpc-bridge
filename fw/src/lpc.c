// SPDX-License-Identifier: GPL-2.0-or-later
// LPC target: PIO front end (lpc_fe), script player (lpc_tx), LDRQ# and SERIRQ engines (lpc_aux) and the CPU side that
// turns LPC cycles into ISA cycles and builds the reply scripts (format in pio/lpc_tx.pio).
#include "lpc.h"
#include "cfg.h"
#include "hw.h"
#include "isa.h"
#include "io16.h"
#include "cfgport.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "hardware/structs/busctrl.h"
#include "hardware/structs/m33.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/structs/pio.h"
#include "lpc_fe.pio.h"
#include "lpc_tx.pio.h"
#include "lpc_aux.pio.h"

#define RAM_FUNC(f) __not_in_flash_func(f)

#define PIO_FE   pio0
#define PIO_TX   pio1
#define PIO_AUX  pio2
#define SM_FE    0
#define SM_TX    0
#define SM_LDRQ  0
#define SM_SIRQ  1

static uint32_t last_mask = 0xFFFFFFFFu;
static volatile uint32_t drq_pending;           // channels with an LDRQ# request sent (bit n = channel n)
static volatile uint32_t drq_claim;             // channels requested and not finished: core 1 claims their DMA cycles
static uint32_t drq_quiet_from[8];              // DWT cycle count: LPC 1.1 needs 8 LCLK after the last DMA SYNC before a new LDRQ#
static uint32_t drq_quiet_cyc;
static inline uint32_t cyc(void) { return m33_hw->dwt_cyccnt; }
static inline void drq_done(unsigned ch) { drq_pending &= ~(1u << ch); drq_quiet_from[ch] = cyc(); }
static inline void drq_end(unsigned ch) { drq_done(ch); drq_claim &= ~(1u << ch); }   // after a withdraw the claim stays: a granted cycle may still come
static uint off_fe_idle, off_tx0, off_ldrq0, off_sirq0;

// ---- reply scripts: one TX FIFO word per LCLK ----
#define LD 0xFu                                   // LAD direction: drive
#define LR 0x0u                                   // release
#define W(nib, dirs, flag) (((nib) & 15u) | ((uint32_t)(dirs) << 4) | ((uint32_t)(flag) << 8))
#define SYNC_READY 0x0u
#define SYNC_MORE  0x9u
#define SYNC_ERROR 0xAu
#define SYNC_LONG  0x6u
#define NX       lpc_tx_offset_pullp            // flag = next address in the player (out pc, program at offset 0)
#define F_END    lpc_tx_offset_start
#define F_RESUME lpc_tx_offset_wloop
#define F_SAMP   lpc_tx_offset_samp

static inline bool fe_empty(void) { return (PIO_FE->fstat >> (PIO_FSTAT_RXEMPTY_LSB + SM_FE)) & 1u; }
static inline bool tx_rx_empty(void) { return (PIO_TX->fstat >> (PIO_FSTAT_RXEMPTY_LSB + SM_TX)) & 1u; }
static inline bool tx_empty(void) { return (PIO_TX->fstat >> (PIO_FSTAT_TXEMPTY_LSB + SM_TX)) & 1u; }
static uint32_t put_budget;                                   // ~20 us of polling: a player that stopped (host reset) must not hang core 0
static inline void put(uint32_t w) {                          // TX FIFO is 4 deep (not joined: the RX FIFO carries SAMPLE2 bytes)
    for (uint32_t n = put_budget; (PIO_TX->fstat >> (PIO_FSTAT_TXFULL_LSB + SM_TX)) & 1u; )
        if (!--n) { dbg.st.stale_tx++; return; }
    PIO_TX->txf[SM_TX] = w;
}

static inline void s_ack(uint32_t sync) {                    // SYNC, TAR   (I/O write, DMA read)
    put(W(sync, LD, NX)); put(W(0xF, LD, NX)); put(W(0, LR, F_END));
}
static inline void s_data8(uint32_t sync, uint8_t d) {       // SYNC, data, TAR   (I/O read, DMA write 8 bit)
    put(W(sync, LD, NX)); put(W(d & 15u, LD, NX)); put(W(d >> 4, LD, NX)); put(W(0xF, LD, NX)); put(W(0, LR, F_END));
}
static inline void s_dmaw16(uint32_t sync2, uint16_t d) {    // SYNC more, byte 0, SYNC, byte 1, TAR
    put(W(SYNC_MORE, LD, NX)); put(W(d & 15u, LD, NX)); put(W((d >> 4) & 15u, LD, NX));
    put(W(sync2, LD, NX)); put(W((d >> 8) & 15u, LD, NX)); put(W((d >> 12) & 15u, LD, NX));
    put(W(0xF, LD, NX)); put(W(0, LR, F_END));
}
static inline void s_dmar16_a(void) {                        // SYNC more, TAR, host byte 1 (SAMPLE2), host TAR, wait states
    put(W(SYNC_MORE, LD, NX)); put(W(0xF, LD, NX)); put(W(0, LR, NX)); put(W(0, LR, F_SAMP));
    put(W(0, LR, NX)); put(W(0, LR, NX)); put(W(SYNC_LONG, LD, F_RESUME));
}

static inline bool denied(uint16_t a) { return (dbg.cfg.deny[a >> 3] >> (a & 7)) & 1u; }

#ifdef FW_DEBUG
#define DEBUG_FLAGS (F_TRACE | F_NO_POST | F_STOP_LPC)
static void RAM_FUNC(trace)(uint32_t type, uint32_t addr, uint32_t data) {
    if (!(dbg.cfg.flags & F_TRACE)) return;
    uint32_t h = dbg.trace_head++ % TRACE_N;
    dbg.trace[h].ts = time_us_32();
    dbg.trace[h].info = (type << 28) | ((addr & 0xFFFFu) << 8) | (data & 0xFFu);
}
#else
#define DEBUG_FLAGS 0u
#define trace(type, addr, data) ((void)0)
#endif

static bool RAM_FUNC(rx_byte)(uint8_t *b) {                  // byte sampled by the player (SAMPLE2), up to 50 us
    for (int i = 0; i < 2000; i++) {
        if (!tx_rx_empty()) { *b = (uint8_t)(PIO_TX->rxf[SM_TX] >> 24); return true; }
        busy_wait_at_least_cycles(10);
    }
    return false;
}

static inline bool more(unsigned ch) {          // t_drq_ns counts from the strobe release; t_dack_hold_ns of it has passed
    uint32_t t = dbg.cfg.t_drq_ns, h = dbg.cfg.t_dack_hold_ns;
    if (t > h) isa_ndelay(t - h);
    return (isa_drq() >> ch) & 1u;
}

// front end word layout: see pio/lpc_fe.pio (top bits are ones, the position of the zero tells the cycle type)
static void RAM_FUNC(serve)(uint32_t w) {
    if (!tx_rx_empty()) (void)PIO_TX->rxf[SM_TX];            // stale sampled byte

    if (((w >> 24) & 0xFu) != 0xFu) {
        // I/O write: [27:24]=2 [23:8]=addr [7:4]=data lo nibble [3:0]=data hi nibble
        uint16_t a = (uint16_t)(w >> 8);
        uint8_t d = (uint8_t)(((w & 0xFu) << 4) | ((w >> 4) & 0xFu));
        dbg.st.iow++; dbg.st.last_addr = a; dbg.st.last_data = d; dbg.st.last_type = 2;
        if ((a & ~1u) == CFGPORT && !(dbg.cfg.flags & F_NO_CFGPORT)) { s_ack(SYNC_READY); cfgport_write(a, d); return; }
        const bool post = !(dbg.cfg.flags & DEBUG_FLAGS & F_NO_POST);
        if (post) s_ack(SYNC_READY);
        io16_write(a, d, time_us_32());
        if (!post) s_ack(SYNC_READY);
        trace(2, a, d);
        return;
    }
    uint32_t t = (w >> 16) & 0xFu;
    if (t != 0) io16_flush();                                // DMA: a held byte goes out first
    if (t == 0) {                                            // I/O read
        uint16_t a = (uint16_t)w;
        dbg.st.ior++;
        uint8_t d = (a & ~1u) == CFGPORT && !(dbg.cfg.flags & F_NO_CFGPORT) ? cfgport_read(a) : io16_read(a, time_us_32());
        s_data8(SYNC_READY, d);
        dbg.st.last_addr = a; dbg.st.last_data = d; dbg.st.last_type = 1;
        trace(1, a, d);
    } else if (t == 8) {                                     // DMA read (host -> card): [15:12] ch|TC<<3 [11:8] size [7:4] lo [3:0] hi
        unsigned ch = (w >> 12) & 7u; bool tc = (w >> 15) & 1u;
        uint32_t size = (w >> 8) & 3u;                       // SIZE bits 3:2 are reserved (LPC 1.1 table 14)
        uint8_t b0 = (uint8_t)(((w >> 4) & 0xFu) | ((w & 0xFu) << 4));
        if (!(dbg.cfg.flags & F_DMA) || ch == 4) { drq_end(ch); s_ack(SYNC_READY); return; }   // error SYNC is a SERR at the PCH (x99pch 5.5.1.7)
        if (ch <= 3) {                                       // a wider SIZE on an 8 bit channel: one byte, then READY ends it (LPC 1.1 6.4.3)
            isa_dma_write(ch, tc && size == 0, b0);
            bool m = size == 0 && more(ch); if (!m) drq_end(ch);
            s_ack(m ? SYNC_MORE : SYNC_READY);
            dbg.st.dma_r++; dbg.st.last_addr = ch; dbg.st.last_data = b0; dbg.st.last_type = 8;
            trace(8, ch, b0);
        } else if (size != 0) {                              // 32 bit on a 16 bit channel: one word, then READY
            s_dmar16_a();                                    // acknowledge byte 0, the player samples byte 1
            uint8_t b1;
            if (!rx_byte(&b1)) { dbg.st.rdy_timeouts++; drq_end(ch); s_ack(SYNC_READY); return; }
            isa_dma16_write(ch, tc && size == 1, (uint16_t)(b0 | ((uint16_t)b1 << 8)));
            bool m = size == 1 && more(ch); if (!m) drq_end(ch);
            s_ack(m ? SYNC_MORE : SYNC_READY);
            dbg.st.dma_r++; dbg.st.last_addr = ch; dbg.st.last_data = b0 | (b1 << 8); dbg.st.last_type = 8;
            trace(8, ch, b0);
        } else {
            drq_end(ch); s_ack(SYNC_READY);
        }
    } else if (t == 0xFu && ((w >> 8) & 0xFu) == 0xAu) {     // DMA write (card -> host): [7:4] ch|TC<<3 [3:0] size
        unsigned ch = (w >> 4) & 7u; bool tc = (w >> 7) & 1u;
        uint32_t size = w & 3u;
        if (!(dbg.cfg.flags & F_DMA) || ch == 4) { drq_end(ch); s_data8(SYNC_READY, 0xFF); return; }
        if (ch <= 3) {
            uint8_t d = isa_dma_read(ch, tc && size == 0);
            bool m = size == 0 && more(ch); if (!m) drq_end(ch);
            s_data8(m ? SYNC_MORE : SYNC_READY, d);
            dbg.st.dma_w++; dbg.st.last_addr = ch; dbg.st.last_data = d; dbg.st.last_type = 10;
            trace(10, ch, d);
        } else if (size != 0) {
            uint16_t d = isa_dma16_read(ch, tc && size == 1);
            bool m = size == 1 && more(ch); if (!m) drq_end(ch);
            s_dmaw16(m ? SYNC_MORE : SYNC_READY, d);
            dbg.st.dma_w++; dbg.st.last_addr = ch; dbg.st.last_data = d; dbg.st.last_type = 10;
            trace(10, ch, d & 0xFF);
        } else {
            drq_end(ch); s_data8(SYNC_READY, 0xFF);
        }
    } else {
        s_ack(SYNC_ERROR);
    }
}

// ---- core 1: claims our cycles within the SYNC deadline (3 LCLK), passes them to core 0 ----
#define RING_N 16u
static uint32_t ring[RING_N];
static volatile uint32_t ring_head, ring_tail;

static inline bool pending(uint32_t flags, unsigned ch) { return (flags & F_DMA) && ((drq_claim >> ch) & 1u); }

static inline bool ours(uint32_t w, uint32_t flags) {
    if (((w >> 24) & 0xFu) != 0xFu) return !denied((uint16_t)(w >> 8));
    uint32_t t = (w >> 16) & 0xFu;
    if (t == 0) return !denied((uint16_t)w);
    if (t == 8) return pending(flags, (w >> 12) & 7u);
    if (t == 0xFu && ((w >> 8) & 0xFu) == 0xAu) return pending(flags, (w >> 4) & 7u);
    return false;
}

// Critical path: front end push to the claim word in the TX FIFO, budget about 3 LCLK (fw/sim CLAIM_MAX). Everything that
// does not depend on the word is read before waiting for it.
static void __scratch_x("core1") core1_claim(void) {       // scratch X: own SRAM bank, core 1 stack sits there too
    for (;;) {
        const uint32_t claim = W(SYNC_LONG, LD, F_RESUME);
        const uint32_t flags = dbg.cfg.flags;
        const uint32_t h = ring_head;
        const bool open = h - ring_tail < RING_N && !(flags & DEBUG_FLAGS & F_STOP_LPC);
        while (fe_empty()) {}
        uint32_t w = PIO_FE->rxf[SM_FE];
        if (open && ours(w, flags)) {
            if (!tx_empty()) { pio_sm_clear_fifos(PIO_TX, SM_TX); dbg.st.stale_tx++; }   // script left by an aborted cycle
            PIO_TX->txf[SM_TX] = claim;
            ring[h % RING_N] = w;
            __dmb();
            ring_head = h + 1;
        } else {
            if (!tx_empty()) { pio_sm_clear_fifos(PIO_TX, SM_TX); dbg.st.stale_tx++; }   // a late claim must not answer someone else's cycle
            if (h - ring_tail >= RING_N) dbg.st.rx_overflow_hint++; else dbg.st.denied++;
        }
        dbg.st.lpc_words++;
    }
}

void RAM_FUNC(lpc_poll)(void) {
    while (ring_tail != ring_head) {
        __dmb();
        uint32_t w = ring[ring_tail % RING_N];
        ring_tail++;
        serve(w);
    }
    io16_tick(time_us_32());
}

// ---------------------------------------------------------------------------------------------
static void RAM_FUNC(send_ldrq)(unsigned ch, unsigned act) {
    uint32_t w = (((ch >> 2) & 1u) << 1) | (((ch >> 1) & 1u) << 2) | ((ch & 1u) << 3) | (act << 4) | (1u << 5);
    pio_sm_put(PIO_AUX, SM_LDRQ, w);
    dbg.st.ldrq_sent++;
}

static uint32_t irq_hold_until[17], irq_held;   // irq_held: frames inside their hold time (a bare deadline reads as pending after 2^31 us)

void RAM_FUNC(lpc_service_irq)(void) {
    if (dbg.cfg.flags & F_NO_SERIRQ) return;
    uint32_t raw = isa_serirq_mask(isa_irq_stream());
    uint32_t now = time_us_32();
    uint32_t asserted = 0;
    for (unsigned f = 0; f < 17; f++) {
        if ((raw >> f) & 1u) { irq_hold_until[f] = now + dbg.cfg.irq_hold_us; irq_held |= 1u << f; }   // stretch short pulses so a SERIRQ cycle sees them
        else if ((irq_held >> f) & 1u && (int32_t)(irq_hold_until[f] - now) <= 0) irq_held &= ~(1u << f);
    }
    asserted = irq_held;
    // PCH: low in an IRQ frame = active-high IRQ not requested, low in the IOCHCK# frame = asserted (x99pch.txt 7575)
    const uint32_t iochck = 1u << 16;
    uint32_t m = ((~asserted & ~iochck) | (asserted & iochck)) & dbg.cfg.irq_frames;
    if (m != last_mask) {                                              // queued at once: frames in progress keep their mask, the next cycle takes this one
        last_mask = m;
        pio_sm_clear_fifos(PIO_AUX, SM_SIRQ);
        pio_sm_put(PIO_AUX, SM_SIRQ, m);
        dbg.st.irq_changes++;
    }
}

void RAM_FUNC(lpc_service_drq)(void) {
    if (!(dbg.cfg.flags & F_DMA)) return;
    uint32_t d = isa_drq();
    dbg.st.drq = d;
    if (d == drq_pending) return;                                    // nothing to request or withdraw
    uint32_t now = cyc();
    static const uint8_t chans[] = {0, 1, 2, 3, 5, 6, 7};
    for (unsigned i = 0; i < sizeof chans; i++) {
        unsigned ch = chans[i];
        bool req = (d >> ch) & 1u;
        bool pend = (drq_pending >> ch) & 1u;
        if (pio_sm_is_tx_fifo_full(PIO_AUX, SM_LDRQ)) break;          // a lost message would leave the host's view stale; next pass sends it
        if (req && !pend && now - drq_quiet_from[ch] >= drq_quiet_cyc) {   // elapsed, not a deadline: a stale stamp must not block
            drq_pending |= 1u << ch;
            drq_claim |= 1u << ch;                                   // before the message: core 1 claims the host's DMA cycle by it
            send_ldrq(ch, 1);
        } else if (!req && pend) {
            send_ldrq(ch, 0);                                        // card withdrew the request
            drq_done(ch);
        }
    }
}

static void fe_set_y(void) {                                     // Y = 0xFFFFFFF0: the START 0000 pattern lpc_fe compares against
    pio_sm_exec(PIO_FE, SM_FE, pio_encode_set(pio_y, 15));
    pio_sm_exec(PIO_FE, SM_FE, pio_encode_mov_not(pio_y, pio_y));
}

// ---------------------------------------------------------------------------------------------
void lpc_init(void) {
    drq_quiet_cyc = clock_get_hz(clk_sys) / 2500000u;               // 400 ns: 8 LCLK plus the TAR still queued in the player
    put_budget = clock_get_hz(clk_sys) / 250000u;                   // loop iterations, at least 4 cycles each: >= 16 us
    for (unsigned i = 0; i < 8; i++) drq_quiet_from[i] = cyc() - drq_quiet_cyc;
    pio_set_gpio_base(PIO_FE, 16);
    pio_set_gpio_base(PIO_TX, 16);
    pio_set_gpio_base(PIO_AUX, 16);

    for (uint p = PIN_LAD0; p < PIN_LAD0 + 4; p++) pio_gpio_init(PIO_TX, p);   // LAD driven by the player
    pio_gpio_init(PIO_FE, PIN_LFRAME_N);
    pio_gpio_init(PIO_FE, PIN_LCLK);
    pio_gpio_init(PIO_AUX, PIN_SERIRQ);
    pio_gpio_init(PIO_AUX, PIN_LDRQ_N);
    for (uint p = PIN_LAD0; p <= PIN_LDRQ_N; p++) {
        gpio_disable_pulls(p);                                                  // the host header has the pull-ups
        gpio_set_drive_strength(p, GPIO_DRIVE_STRENGTH_12MA);
    }
    for (uint p = PIN_LAD0; p <= PIN_LDRQ_N; p++) gpio_set_slew_rate(p, GPIO_SLEW_RATE_FAST);
    for (uint p = PIN_LAD0; p < PIN_LAD0 + 4; p++) gpio_set_input_hysteresis_enabled(p, false);   // LAD is sampled mid-clock; LCLK/LFRAME# keep it (edges)
    const uint64_t lpc_pins = 0xFFull << PIN_LAD0;
    pio_set_input_sync_bypass_with_mask64(PIO_FE, lpc_pins, lpc_pins);
    pio_set_input_sync_bypass_with_mask64(PIO_TX, lpc_pins, lpc_pins);
    // PIO2 (LDRQ#, SERIRQ) keeps the synchronizers: 2 cycles later is irrelevant against a 30 ns LCLK

    // ---- front end (must sit at offset 0: dispatch table) ----
    int off_fe = pio_add_program_at_offset(PIO_FE, &lpc_fe_program, 0);
    pio_sm_config c = lpc_fe_program_get_default_config(off_fe);
    sm_config_set_in_pin_base(&c, PIN_LAD0);
    sm_config_set_jmp_pin(&c, PIN_LFRAME_N);
    sm_config_set_in_shift(&c, false, false, 32);          // ISR shifts left, no autopush
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    off_fe_idle = off_fe + lpc_fe_offset_idle;
    pio_sm_init(PIO_FE, SM_FE, off_fe_idle, &c);
    fe_set_y();

    // ---- script player: ISR right shift with autopush at 8 bits (sampled byte = word >> 24) ----
    int off_tx = pio_add_program_at_offset(PIO_TX, &lpc_tx_program, 0);   // flags are absolute addresses
    c = lpc_tx_program_get_default_config(off_tx);
    sm_config_set_in_pin_base(&c, PIN_LAD0);
    sm_config_set_out_pins(&c, PIN_LAD0, 4);
    sm_config_set_set_pins(&c, PIN_LAD0, 4);
    sm_config_set_jmp_pin(&c, PIN_LFRAME_N);
    sm_config_set_sideset_pins(&c, PIN_LAD0);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_in_shift(&c, true, true, 8);
    sm_config_set_mov_status(&c, STATUS_TX_LESSTHAN, 1);
    pio_sm_set_consecutive_pindirs(PIO_TX, SM_TX, PIN_LAD0, 4, false);
    off_tx0 = off_tx;
    pio_sm_init(PIO_TX, SM_TX, off_tx, &c);

    // ---- LDRQ# ----
    int off_l = pio_add_program(PIO_AUX, &ldrq_program);
    c = ldrq_program_get_default_config(off_l);
    sm_config_set_in_pin_base(&c, PIN_LAD0);
    sm_config_set_out_pins(&c, PIN_LDRQ_N, 1);
    sm_config_set_set_pins(&c, PIN_LDRQ_N, 1);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);                     // 8 words: all 7 channels may change in one pass
    off_ldrq0 = off_l;
    pio_sm_init(PIO_AUX, SM_LDRQ, off_l, &c);
    pio_sm_exec(PIO_AUX, SM_LDRQ, pio_encode_set(pio_pins, 1));
    pio_sm_exec(PIO_AUX, SM_LDRQ, pio_encode_set(pio_pindirs, 1));

    // ---- SERIRQ ----
    int off_s = pio_add_program(PIO_AUX, &serirq_program);
    c = serirq_program_get_default_config(off_s);
    sm_config_set_in_pin_base(&c, PIN_LAD0);
    sm_config_set_out_pins(&c, PIN_SERIRQ, 1);
    sm_config_set_set_pins(&c, PIN_SERIRQ, 1);
    sm_config_set_sideset_pins(&c, PIN_SERIRQ);
    sm_config_set_jmp_pin(&c, PIN_SERIRQ);
    sm_config_set_out_shift(&c, true, false, 32);
    pio_sm_set_consecutive_pindirs(PIO_AUX, SM_SIRQ, PIN_SERIRQ, 1, false);
    off_sirq0 = off_s;
    pio_sm_init(PIO_AUX, SM_SIRQ, off_s, &c);
    pio_sm_exec(PIO_AUX, SM_SIRQ, pio_encode_set(pio_pins, 0));
    pio_sm_exec(PIO_AUX, SM_SIRQ, pio_encode_set(pio_x, 0));

    // response engine and aux first, front end last (it raises the IRQ the engine waits for)
    pio_sm_set_enabled(PIO_TX, SM_TX, true);
    pio_sm_set_enabled(PIO_AUX, SM_LDRQ, true);
    pio_sm_set_enabled(PIO_AUX, SM_SIRQ, true);
    pio_sm_set_enabled(PIO_FE, SM_FE, true);
    busctrl_hw->priority = BUSCTRL_BUS_PRIORITY_PROC1_BITS;           // core 1 claim path wins bus arbitration
    multicore_launch_core1(core1_claim);
}

// Host reset (PCIRST# edge): the LPC bus is idle or restarting, so every engine may sit in the middle of a cycle or a frame.
// Stop all, flush FIFOs and IRQ flags, put the program counters back and start again (front end last).
void lpc_reset(void) {
    pio_sm_set_enabled(PIO_FE, SM_FE, false);
    pio_sm_set_enabled(PIO_TX, SM_TX, false);
    pio_sm_set_enabled(PIO_AUX, SM_LDRQ, false);
    pio_sm_set_enabled(PIO_AUX, SM_SIRQ, false);

    pio_sm_clear_fifos(PIO_FE, SM_FE); pio_sm_restart(PIO_FE, SM_FE);
    pio_sm_exec(PIO_FE, SM_FE, pio_encode_jmp(off_fe_idle));
    fe_set_y();
    busy_wait_us_32(2);                       // core 1 finishes a word it already took (well under 1 us), then idles on the empty FIFO

    pio_sm_clear_fifos(PIO_TX, SM_TX); pio_sm_restart(PIO_TX, SM_TX);
    pio_sm_set_consecutive_pindirs(PIO_TX, SM_TX, PIN_LAD0, 4, false);   // release LAD
    pio_sm_exec(PIO_TX, SM_TX, pio_encode_jmp(off_tx0));
    pio_interrupt_clear(PIO_TX, 0);
    pio_interrupt_clear(PIO_TX, 1);

    pio_sm_clear_fifos(PIO_AUX, SM_LDRQ); pio_sm_restart(PIO_AUX, SM_LDRQ);
    pio_sm_exec(PIO_AUX, SM_LDRQ, pio_encode_jmp(off_ldrq0));
    pio_sm_exec(PIO_AUX, SM_LDRQ, pio_encode_set(pio_pins, 1));
    pio_sm_exec(PIO_AUX, SM_LDRQ, pio_encode_set(pio_pindirs, 1));

    pio_sm_clear_fifos(PIO_AUX, SM_SIRQ); pio_sm_restart(PIO_AUX, SM_SIRQ);
    pio_sm_set_consecutive_pindirs(PIO_AUX, SM_SIRQ, PIN_SERIRQ, 1, false);
    pio_sm_exec(PIO_AUX, SM_SIRQ, pio_encode_jmp(off_sirq0));
    pio_sm_exec(PIO_AUX, SM_SIRQ, pio_encode_set(pio_pins, 0));
    pio_sm_exec(PIO_AUX, SM_SIRQ, pio_encode_set(pio_x, 0));

    drq_pending = 0;
    drq_claim = 0;
    io16_drop();
    ring_tail = ring_head;                                    // drop words of cycles the host abandoned
    last_mask = 0xFFFFFFFFu;                                  // resend the IRQ mask
    for (unsigned i = 0; i < 8; i++) drq_quiet_from[i] = cyc() - drq_quiet_cyc;
    irq_held = 0;

    pio_sm_set_enabled(PIO_TX, SM_TX, true);
    pio_sm_set_enabled(PIO_AUX, SM_LDRQ, true);
    pio_sm_set_enabled(PIO_AUX, SM_SIRQ, true);
    pio_sm_set_enabled(PIO_FE, SM_FE, true);
}
