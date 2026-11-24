# Firmware review log

Scope: `fw/` (RP2354B, PIO LPC target, ISA master). Two passes, 2026-11-09. Nothing has run on silicon yet.

## Findings and fixes

1. **Critical, fixed.** `lpc_tx` joined the TX FIFO, which disables the RX FIFO. The SAMPLE2 byte of a 16-bit DMA host-to-card transfer could never be autopushed, so the player would stall and the LPC cycle hang. The TX FIFO is now 4 deep and not joined. `put()` waits for room. The simulator feeds the player one word per 4 cycles through a 4-deep FIFO.
2. **High, fixed.** `lpc_service_irq` called `pio_sm_drain_tx_fifo` on the SERIRQ engine mid-frame. The helper runs PULL through exec and overwrites the OSR holding the current frame mask. The mask is now updated only while the engine idles at `start`; otherwise it retries 20 us later.
3. **High, fixed.** The `.fwdbg` NOBITS section trick did not assemble. `dbg` is now a pointer to `0x2007C000` (outside the linked image, below the heap limit 0x20080000).
4. **Medium, fixed.** QMI `RXDELAY` counts half sys clocks; boot2 uses 2 at 150 MHz (6.7 ns). At 400 MHz that is 2.5 ns and XIP reads could corrupt. `main.c` sets the flash divider before the PLL change and scales `RXDELAY` after it (6 at 400 MHz). Unverified on silicon.
5. **Low, fixed.** `CMD_RESET` slept in the mailbox and stalled LPC service; the front end FIFO could then replay old cycles. It is now a deadline checked by later mailbox polls. `trace()` runs from RAM.
6. Build fixes: missing `hardware/clocks.h` include in `isa.c`; `PICO_DEFAULT_UART` in the board header so `pico_stdlib` compiles.

## Checked, no change
SDK API names and signatures, GPIO base translation, `PIO_FSTAT_*` macros, DRQ bit mapping, 595/165 bit order, front end dispatch table and nibble counts, word layouts and `serve()` classification, `lpc_reset` ordering, `tools/isa.tcl` offsets. The TX player starts driving at the SYNC clock at 264, 400 and 520 MHz over random LCLK phases.

## Known limits
- Front end takes START from the first LFRAME# clock only (LPC 1.1: the last one counts); a host that stretches LFRAME# over a changing LAD is not handled.
- No 32-bit DMA, no memory cycles (hardware limit). PIO instruction memory is full.
- Unverified on silicon: PIO timing against LCLK, pad delays, SERIRQ polarity, quiet-mode kick, flash timing at 400 MHz.
- Core 1 unused. A split (DRQ/mailbox on core 1) would not remove the shared 595/165 chain conflict; measure with the trace ring first.

## Next
Run `tools/build.sh ram` then `tools/ram.sh` with a CMSIS-DAP probe on J2. Check `fw_stats`, use the mailbox for ISA bring-up, then tune `t_cmd_ns` and friends live.

## Adversarial pass 3 (2026-11-09)
Fixed: `tools/ram.sh` now sets MSP/PC/VTOR from the vector table (a reset starts the bootrom, not the RAM image); sysclk window 280..420 MHz enforced at build time, default 320 MHz (simulator sweep with LCLK duty 37..63 %); `dack_off()` drops AEN before the 138 selects (DACK0# glitch); PCIRST# gets the internal pull-up (floats with no host, RP2350-E9); chipset ports 0x00-0xFF and 0xCF8-0xCFF denied by default (`deny[]`); DRQ sampled `t_drq_ns` (300) after a DMA strobe; `isa_irq_stream`, `isa_pcirst_n`, `send_ldrq` in RAM and `pio_sm_clear_fifos` instead of the flash-resident `pio_sm_drain_tx_fifo`; no watchdog in RAM builds.
Not fixed (no PIO space or needs silicon): TX FIFO underrun is not detected (a late word repeats the previous nibble); input synchronizers are bypassed on LCLK/LAD (metastability risk accepted for the latency); SERIRQ engine locks to the first low pulse (a restart while the host is mid-frame misaligns until the next reset).
Also: SCK/IOR#/IOW# edges default to slow slew and 4 mA (`F_FAST_EDGES` restores fast/8 mA): the SCK net is 147 mm to five loads with no series resistor, so fast edges risk ringing and a double shift (wrong SA/IRQ readout). Needs a scope check of SCK and RCLK at U5-U9.

## Review 2026-11-10
Fixed:
- SERIRQ polarity was inverted. X99 PCH datasheet (research text/x99pch.txt line 7575): an IRQ frame is low while the ISA IRQ line is low, released while high; the IOCHCK# frame is low when asserted. Only frames in the new `irq_frames` (+0x2C, default 3, 5, 7, 9, 10, 11, 15, IOCHCK#) are driven: a driven-low frame would mask the Super I/O's COM1/PS/2 mouse interrupt.
- The player drove SYNC on every I/O cycle, so it collided with the board's Super I/O (KBC 60/64, 2E/4E, COM1, hardware monitor). Core 1 now claims a cycle (port not denied, or a DMA channel we requested) by queueing the first wait-state SYNC; unclaimed cycles are never driven. SYNC starts at N+3..N+5, inside the 3 clock limit of LPC 1.1 4.2.1.9. The player checks LFRAME# at the falling edge so a one clock START returns it to idle.
- CMake cached 400 MHz while main.c said 320. New window from the simulator: 340..500 MHz, default 400 MHz at 1.30 V; build.sh passes the clock.
- LAD 8 mA fast slew, SERIRQ/LDRQ# 8 mA. `tools/lpc_windows.sh` (now `host/isalpc`) writes the PCH GEN1-4 decode windows (none existed). Stale comments fixed.
- Player: after the falling-edge check the `pull` can land after the rising edge at 63 % LCLK duty; the word now plays at once instead of a clock late (SYNC was late by one clock). SAMPLE2 took its second nibble in the same clock when LCLK was still high (63 % duty, 400 MHz): both nibbles are now sampled at falling edges.
Simulator: claim latency (`CLAIM_MAX`), LCLK duty (`DUTY`, it was fixed at 50 %), unclaimed-cycle test. 0 failures from 320 to 500 MHz at duty 0.37, 0.5, 0.63 (claim <= 30 clocks), and at 400 MHz with claim <= 40. Core 1 critical path is about 19 instructions from the FIFO read to the TX FIFO write; not measured on silicon.
- Default `deny[]` also covers the usual Super I/O ranges: 0x290-0x29F (hardware monitor), COM 0x2F8/0x3E8/0x3F8, LPT 0x378/0x778, FDC 0x3F0. LDRQ#: the PCH datasheet forbids sharing one LDRQ# between two peripherals (x99pch.txt line 6855); wire J4.13 to LDRQ1# (ball AA35, native by default, 20k internal pull-up) unless LDRQ0# is proven unused by the Super I/O.

## Latency pass 2026-11-10
- Flash image `copy_to_ram`; core 1 claim loop in scratch X, `busctrl` priority to core 1.
- PIO: front end loops with `.wrap` (one jump and the `push [1]` delay gone), player releases LAD with side-set on the abort jump, SERIRQ turnaround with side-set. Simulator assembler gained side-set; all sweeps still 0 failures (320..500 MHz, duty 0.37..0.63).
- PIO2 (LDRQ#, SERIRQ) keeps the input synchronizers; LAD hysteresis off (sampled mid-clock), LCLK/LFRAME# keep it. LAD/SERIRQ/LDRQ# 12 mA fast slew.
- 595/165 chain on SPI0 (the pin map already has RX/CSn/SCK/TX on GPIO 4-7): the 20 us IRQ scan no longer blocks core 0. Unverified on hardware: CSn must stay low over the 3 bytes (PL022 SPH=1) so RCLK latches once; `F_CHAIN_GPIO` restores the bit-bang.
- 16 bit I/O pairing (`src/io16.c`, host test `test/io16_test.c`).
Not applicable here: clk_sys from LCLK (PLL reference is the crystal, LCLK is not on a GPIN pin); HSTX (GPIO 12-19 only); a PIO address filter (no instruction room, and the chain/strobe pins GPIO 0-15 sit outside the GPIO-base-16 window all three PIOs need for LAD); DMA feeding the player (scripts are not on the deadline path, and an abort would leave DMA pushing stale words).
OpenOCD: Raspberry Pi fork built to a local OpenOCD build (bundled jimtcl; brew jimtcl 0.84 breaks it); both cores SMP by default, so `ram.sh`'s resume also starts core 1. Not run against hardware: no probe attached.
- Chain address changes bit-banged again (SPI at 25 MHz took about 1 us, twice the coprocessor loop); SPI0 only runs the background scan. SCK and RCLK idle high in both modes, so the pin-function switch makes no edge. IRQ scan every 5 us (was 20).
- DMA: quiet time before a new LDRQ# counted in DWT cycles (400 ns = 8 LCLK plus the queued TAR) instead of 2 us of the 1 us timer; `t_drq_ns` now counts from the strobe release (the hold time is part of it). Build -O3. Per-card port timing not done (generic design).

## Software review, 5 rounds (2026-11-10)
1. DMA: a cycle the host had already started when we withdrew LDRQ# was not claimed (PCH abort); core 1 now claims every channel requested since reset (`drq_claim`). 16 bit DMA re-shifted the chain twice per word; SBHE# now stays low until the next I/O address. Held io16 bytes are dropped on PCIRST#.
2. Player rewritten around `out pc`: the flag field is the next address (program at offset 0), the first word of a claim or script drives pins and direction in one cycle by side-set. 30 -> 27 instructions, first SYNC one cycle earlier, one cycle less per word. Simulator: it wrapped from any instruction past `.wrap` (hardware: only from the `.wrap` instruction), fixed; its assembler now matches pioasm opcode for opcode on all four programs. Sweeps 320..500 MHz x duty 0.37..0.63: 0 failures.
3. A late claim left in the TX FIFO would answer the next, possibly foreign, cycle: core 1 clears it on unclaimed cycles too. `put()` is bounded (about 16 us) so a stopped player cannot hang core 0 past a PCIRST# edge.
4. Odd-address byte cycles use AT byte lanes for any card: SBHE# low, byte on both lanes, reads AND the lanes (the undriven lane floats to 0xFF). Pairing even+odd into word cycles still needs `io16[]` because IOCS16# is not readable (hardware round).
5. `ram.sh`/`isa.tcl` use `read_memory` (mem2array is a deprecated shim in this OpenOCD); Tcl checked on the dummy adapter; RAM image vector table (MSP 0x20082000, reset 0x20000196) matches what `ram.sh` loads.
6. Latency: address shifts skip the 165 readout and use inline nops (12 sys clocks per bit by the disassembly, 0.72 us for 24 bits; the nop loops cost 4 clocks per turn, so the old shift took about 38 per bit); the scan clocks 16 bits with RCLK high, so it no longer rewrites the 595 latches and an address change abandons it instead of waiting up to 1 us. Setup and hold are DWT deadlines: an unchanged address pays no setup time, a read returns at the strobe release and the next cycle waits out the rest of the hold. Estimates from the code, not measured.

## Bug hunt 2026-11-10
- Time wrap: IRQ hold deadlines were compared signed and never refreshed, so 35.8 min after the last activity every served frame, IOCHCK# (NMI) included, read as asserted for the next 35.8 min. Now a held-frame bitmap. The DRQ quiet deadline (DWT cycles) blocked a new LDRQ# for 5.4 s out of every 10.7 s; now an elapsed-time test.
- Front end ignored START: a TPM cycle (START 0101, sent by the BIOS on this very header) decoded as I/O to 0x0000-0x4FFF and was claimed above 0xFF (TPM DID/VID 0xF00 aliases to 0x300 on 10 bit cards). START must be 0000 now; the dispatch table moved to slots 16..31 (`mov pc, isr`), program still 32 instructions. Simulator: `tpm` trial.
- SERIRQ mask was queued only while the engine sat at `start`; in continuous mode that is a few clocks per cycle, so an IRQ change could wait many scans. It is queued at once (`pio_sm_clear_fifos` does not touch the OSR) and the quiet-mode kick repeats while the word is not taken. Idle gap of the PCH in continuous mode: not in the datasheet, unverified.
- Regressions of the latency pass, fixed: strobe recovery on back-to-back cycles to one address fell to about 100 ns (now hold + setup from the last strobe again); an address change could void every IRQ scan under steady I/O (now PL022 BSY decides, never two voided scans in a row).
- LDRQ# FIFO joined (8 words): seven channels changing in one pass overflowed the 4 word FIFO and the lost message left the channel pending forever.

## HG25 firmware (BUILD 9, 2026-11-10)
- Needs PASS_HG25 (165 stream: IOCS16#, IRQ4-7, 9-12, 14, 15, IOCHCK#, IRQ3). HG22 to HG24 boards: commit 3e8d360.
- IOCS16# is sensed after every address change (SH/LD# low, MISO follows U8.H; `t_cs16_ns` 200 = 595 delay 11.5 + ISA 122 max + release through 1k + 165 delay 17.8). `io16[]` ranges are gone: an even byte to a port that answers IOCS16# waits 5 us for its partner, reads fetch the word once. Other ports get plain AT byte cycles (SBHE# low on odd addresses, data lane by IOCS16#).
- All writes drive SD0..15 (the high transceiver inputs floated on byte writes); IOCHRDY timeout counted in DWT cycles; `F_HOLD_RESET` releases RESETDRV when cleared.
- Limits: two byte writes to a and a + 1 of an IOCS16# port within 5 us go out as one word; a byte read at an even IOCS16# port reads the word; IOCS16# is not sensed again while the address stays the same.

## Pass 4: HG25 board against firmware (2026-11-11)
Board pads (PASS_HG25) checked against `hw.h`, `docs/pinmap.py` and the bit layouts in `isa.c`: 595 and 165 chain order, 244/138/4245/2G74/1G125/1G00 wiring, TPM header, pull resistors. No mismatch. `netcheck.py` passes.

Fixed in build 10 (`test/io16_test.c` and `sim/test_aux.py` pass, flash and RAM images build):
1. Medium. `io16_read` ran a word cycle for an 8-bit IN to any even IOCS16# port, so the card also saw a read of port + 1 (a real AT runs a byte cycle). Reads now pair only in `cfg.word_rd` (+0x34, three ranges first | last << 16). Defaults: 0x620-0xE83 (EMU8000 bases 620-680), 0x344 (InterWave I16DP at GUS base 240, Orpheus II default). Writes still pair on every IOCS16# port (a lone byte goes out after 5 us).
2. Low. `F_NO_POST` acknowledged the low byte of a word to an IOCS16# port before its ISA cycle. Inherent (the high byte only arrives after the ack); the flag comment now says so.
3. Low. The SERIRQ engine took any low after frame 16 as the stop pulse, so a device driving frames 17 and up desynced it. The engine now checks the middle of the next clock: high again = a frame, still low = the stop pulse. `jmp_pin` = SERIRQ. The simulator runs two cycles with random drives in frames 17-20; the old program fails it.
4. Low. `lpc_reset` could clear TX and the ring while core 1 was still handling a word. It now waits 2 us after stopping the front end.
5. Low, debug only. `CMD_RESET` and boot released RESETDRV with `F_HOLD_RESET` set. Both now follow the flag; `isa_release_reset` is gone.
6. Comment: chain SPI runs at 24 MHz (clk_peri 48 MHz).

Adversarial pass (qwen3.8): 45 claims, none new. Rejected after checking against code and board: AEN polarity (ISA AEN is active high, same sense as 138 G1), 4245 direction (A port is the ISA side, DIR high = slot to MCU), 595/165 bit order (first bit shifted ends in U7.QH; first bit read is U8.H), `addr_word` SBHE# sense, `DMA_WORD` (bit 23 is the spare U7.QH, SBHE# = 0), 165 parallel read (SH/LD# low passes H to QH), `irq_stream` order in `isa_chain_raw` vs `spi_collect` (both put the first bit in bit 0), SERIRQ mask polarity, frame 16 (IOCHCK# is frame 16), `mov isr, !null`, dispatch and `out pc` decoding (built and simulated), ring barriers (`__dmb` present), io16 cross-core race (io16 runs on core 0 only), RXDELAY clamp (the 340-500 MHz window gives at most 7, the clamp is never hit).

## Pass 5: second review, host tool (2026-11-11, build 11)
Board: all 98 socket pins match the ISA pinout, memory strobes pulled inactive. No finding. ISA memory cycles stay out (the
only cards that need them, Turtle Beach MultiSound/Pinnacle/Fiji, want a window below 1 MB that UEFI hosts occupy).

Fixed:
1. Medium. `wait_rdy` sampled data and ended the strobe as soon as IOCHRDY rose. ISA timing row 22 asks for 125 ns of command hold after it; that delay is now added when a wait happened.
2. Low. The read pairing window started before the ISA cycle, so a cycle stretched by IOCHRDY could use it up. It now starts at the first `io16_tick` after the cycle.
3. Low. A paired word cycle loaded the address chain twice (sense with SBHE# high, cycle with SBHE# low). Ports in `word_rd` are now sensed with the word address (`isa_iocs16w`).
4. No way to configure the bridge without a probe: config port 4E/4F (`cfgport.c`, `F_NO_CFGPORT`), used by `host/isalpc`.
5. `tools/lpc_windows.sh` knew contiguous blocks and SB16 only: replaced by `host/isalpc` (DOS and Linux, presets, address masks, fixed decodes, bridge port filter and IRQ list).

Host limits found: four GEN ranges, one xx00-xxFF block each. PAS16 needs 19 blocks and cannot work on an Intel PCH. WSS at 530 collides with GPIOBASE on boards that put it at 500 (the tool refuses). Port 279 is read only through the LPT decode, so ISA PnP needs a GEN range.

Not verified: `ISALPC.EXE` is not built or run yet (Open Watcom), the config port and the tool have only met each other in `host/test_isalpc.c`.

## Pass 6: BIOS of the host board (2026-11-11)
`docs/bios-x99m-killer-p4.01.md`. Five things break or block the adapter with BIOS P4.01: GPIO block at 500-57F under WSS
530/534, three of four GEN ranges taken, Serial IRQ Mode Quiet, Super I/O decode over 280-2BF. (A fifth, LDRQ1# as GPIO23,
was a misread GPIO table and is withdrawn.)
Done about it: `isalpc` moves the GPIO block when a preset needs those ports (written only after the decode plan fits),
`ldrq1` switches the pin, `force` replaces as few BIOS ranges as needed (GEN4 first) and keeps a replaced one forwarded
where a new range covers it, SERIRQ is set to continuous mode.

## Pass 7: review of the day's work (2026-11-11, evening)
1. High, fixed. The BIOS patch changed the wrong GPIO: the SBPEI table is keyed by the pin number in the low byte (pin 8 has
   no entry), so entry 0x0318 is GPIO24 (ME recovery strap), not GPIO23. GPIO23/LDRQ1# is native in this BIOS. Patch removed,
   image regenerated (7 bytes, commit bb25985); the claim "LDRQ1# is GPIO23" is withdrawn in the BIOS notes.
2. Medium, fixed. `isalpc nopirq` left the freed IRQs level triggered in the ELCR (4D0/4D1); an ISA card's edge interrupt
   would retrigger. The bits are cleared now, and the "PCI too" marks follow.
3. Low, fixed. `nopirq` did nothing without a bridge answer and did not say so.
4. Low, fixed. `order-HG25.md` named 15k/47n for the ADP5071 compensation; the board has 22n (C27).
Read again without finding: `cfgport.c` and its hook in `serve()`, offsets used by `isalpc` (build 4, sysclk 20, irq_frames 2C,
deny 40) against `cfg.h`, the decode planner, the GPIO block move, the three remaining BIOS patch sites, the power input
and negative rail values on the board (EN2 divider 2.28 V, FB 1M/62k, 79M05 from -12.1 V).
Still unverified: everything on hardware; `ISALPC.EXE` (DOS port access code never compiled); the patched BIOS (never booted).

## Build 12: saved bridge config (2026-11-13)
`ISALPC SAVE` writes dbg.cfg to flash (config port offset F000, command 'W'; 'X' forgets; status at F000, valid flag at F001).
Loaded at boot after the defaults if magic, layout, size, sysclk and CRC match. Sanitised on load and on save: no `F_STOP_LPC`,
`F_NO_CFGPORT`, `F_HOLD_RESET`, ports 4E/4F never denied. Erase and program run from RAM (copy_to_ram: all code is in RAM, checked
with nm), interrupts off during each call, watchdog kicked between sectors, QMI timing restored afterwards (`flash_timing_restore`
in main.c). The ROM resetting the XIP timing after a flash operation at 400 MHz is assumed from the SDK, not verified; if flash
reads fail after a save the symptom would be a hang right after it.
Tests: `fw/test/persist_test.c` (fake flash: CRC, layout, clock mismatch, lock-out flags, error status), `cfgport_test.c`
(command and status routing), `host/test_isalpc.c` (SAVE, FORGET, dry run). Not run on hardware.


## Review 2026-11-22 (build 13)
Adversarial pass after the HG25 hardware test. DRC 0/0, netlist-to-board parity 189/189 nets, BOM = CPL = board (153), all tests pass.
1. Fixed. `isalpc` compared arguments case sensitive: `ISALPC SB16 SAVE` (README, DOS habit) failed with "unknown preset".
   Arguments are lowered now; test added.
2. Fixed. Strobe timing below the Ampro tables (Ampro "ISA Bus Timing Diagrams", receiver minimums): 8 bit I/O
   command 400 ns vs 530 (table 1, 8d); DMA command 400 vs 489/786 (table 2, 12/11a), DACK setup 120 vs 310 (1b), DACK hold 60 vs
   162 (13b). New fields after deny[]: `t_cmd8_ns` 540 (cycles without IOCS16#), `t_dma_cmd_ns` 800, `t_dack_setup_ns` 320,
   `t_dack_hold_ns` 170. `t_cmd_ns` (400) now applies to IOCS16# cycles only. Saved-config layout 2 (old saves are ignored).
   Cost: a DMA cycle takes about 1.3 us instead of 0.6 us.
3. Fixed. The SERIRQ start-pulse kick also fired in continuous mode: the host ignores it, but our engine took it as a start and
   counted frames from it (wrong frames driven until it fell back in step). The engine raises PIO2 IRQ 0 after every stop frame;
   the CPU kicks only after 50 us without one. Simulator checks the flag.
4. Fixed. `isalpc` warns when a requested port is also forwarded by a BIOS COM/LPT/floppy decode (both would answer).
5. Fixed. `drq_claim` stayed set after a transfer ended, so onboard Super I/O DMA on that channel was also claimed. Cleared when
   the transfer ends by SYNC; kept after an LDRQ# withdraw (a granted cycle may still come, LPC 6.3).
6. Fixed. `sysclk_khz` was writable through the config port; duplicate line in `isalpc` main.
Not changed (author): -12 V is the ADP5071 output without post-filter; README LDRQ# advice (shared Super I/O line) stays.
Checked from the SDK 2.3.1 source: flash erase/program re-enters XIP through the bootrom's saved setup function, then
`flash_timing_restore` reapplies the overclock divider; all code runs from RAM, so the build 12 assumption holds on paper.
Not run on hardware: build 13.

## Build 17 (2026-11-24): SERIRQ quiet mode removed
Author decision. `isalpc` always sets continuous mode, and the PCH decodes are reset on every boot, so `isalpc` must run each boot
anyway; the quiet-mode start pulse only served `keepsirq`, which had no known user. Removed: `serirq_kick` and its 50 us
gate (`lpc.c`), the stop-frame `irq set 0` in the SERIRQ engine (AUX 29 of 32 instructions), the `keepsirq` option in `isalpc`.
In quiet mode (BIOS default on the X99M) card IRQs now never reach the host. Simulator and `make -C host test` pass.
`ISALPC.EXE` must be rebuilt (`make -B -C isa-lpc-bridge/host ISALPC.EXE`).

## Build 18 (2026-11-24): SPI chain only, debug flags behind FW_DEBUG
Author decision. `F_CHAIN_GPIO` removed: the IRQ scan always runs on SPI0 (works on HG25); `isa_chain_raw` stays for the
mailbox and the first load. `F_TRACE`, `F_NO_POST` and `F_STOP_LPC` act only in a build with `FW_DEBUG=ON tools/build.sh [ram]`;
otherwise the bits are ignored, writes are always posted and the trace ring stays empty. `fw_dbg_t` layout unchanged (config
port and saved-config offsets). Flash image 28328 bytes, debug build 28812. Simulator, `fw/test` and `make -C host test` pass.
