# ISA-LPC bridge firmware (RP2354B)

Status: builds clean, simulated, runs on the PASS_HG25 board.

## Architecture
- `pio/lpc_fe.pio` (PIO0): LPC front end. Samples LAD on the falling LCLK edge, parses I/O read/write and 8/16 bit DMA read/write
  cycles into one 32 bit word, raises IRQ 0 of PIO1.
- Core 1 (`core1_claim` in `src/lpc.c`): reads every front end word and claims ours (port not in `deny[]`, DMA channel we
  requested) by queueing a wait-state SYNC word within 3 LCLK; passes claimed words to core 0. Other cycles (the board's Super
  I/O: KBC, COM1, config ports, hardware monitor) are never driven.
- `pio/lpc_tx.pio` (PIO1): script player. Waits for the claim with LAD released, then drives the wait-state SYNC (0110) until core 0 queues a script (one TX FIFO word per LCLK: nibble, direction, flag), then plays it: SYNC, data, TAR, second byte of
  16 bit DMA, sampling of the host's second byte (SAMPLE2), back to wait states (RESUME). Releases the bus on host abort.
- `pio/lpc_aux.pio` (PIO2): LDRQ# messages and a SERIRQ slave that follows start/stop frames (continuous mode, see limits).
- `src/isa.c`: CPU driven ISA master. 595/165 chain: address changes bit-banged through the M33 GPIO coprocessor, the 5 us IRQ
  scan (16 bits of the 165s) on SPI0 in the background (mode 3, RCLK stays high; `F_CHAIN_GPIO` keeps it bit-banged too). Strobes and data through the
  coprocessor, IOCHRDY, DMA with DACK.
- `src/io16.c`: 16 bit I/O on ports that answer IOCS16# (sensed through the 165 chain after each address change, HG25): the PCH's
  two byte cycles to a port pair become one 16 bit ISA cycle (SBHE# low); a lone byte uses the AT byte lane. Writes pair on
  every such port; reads only in the `word_rd` ranges (EMU8000 and InterWave base 240 data ports by default; `isalpc` sets them from the presets), since a word read also reads
  port + 1.
  Test: `cc -Isrc -o /tmp/t test/io16_test.c && /tmp/t`.
- `src/lpc.c` (core 0): serves the claimed words: I/O writes are acknowledged first (posted) and executed after; reads and DMA wait
  with long SYNC until the ISA cycle is done.
- `src/mailbox.c`: run ISA cycles from the debugger, no host needed.
- `src/persist.c`, `persist_hw.c`: `ISALPC SAVE` keeps the bridge config (port filter, IRQ list, timing, flags) in the last 16 KB of the flash; it is loaded at boot when the clock matches and a CRC is good. Flags that could lock the host out (`F_STOP_LPC`, `F_NO_CFGPORT`, `F_HOLD_RESET`) and the config port itself are never restored. A save blocks the bridge for 0.1 to 1 s. `ISALPC FORGET` erases it; from a debug probe: `flash erase_sector 0 0x1fc 0x1fe`. Test: `cc -Isrc -o /tmp/t test/persist_test.c && /tmp/t`.
- `src/cfgport.c`: config port at 4E/4F for the host tool (`host/isalpc.c`). Test: `cc -Isrc -o /tmp/t test/cfgport_test.c && /tmp/t`.
- GPIO base 16 on all three PIOs (LAD..LDRQ = GPIO40..47 are PIO pins 24..31).

## Build
The flash image is `copy_to_ram`: the boot copies everything to SRAM, nothing runs from flash afterwards. Core 1 (claim loop)
runs from scratch X with bus priority.

    export PICO_SDK_PATH=/path/to/pico-sdk        # SDK 2.1 or newer
    tools/build.sh            # flash image: build/isa_lpc.uf2/.elf
    tools/build.sh ram        # RAM image: build-ram/isa_lpc.elf

## Clock
Default 400 MHz at 1.30 V (PLL 1200 MHz / 3, XIP flash at 57 MHz). Allowed window 340..500 MHz (`main.c` stops the build outside it, `-DFW_ALLOW_ANY_CLK` overrides): in `fw/sim` (`DUTY=0.37..0.63`, core 1 claim latency up to 30 sys clocks) cycles fail below ~320 MHz and above ~500 MHz. At 400 MHz the claim may take 40 clocks. Other values: `cmake -S . -B build -G Ninja -DFW_SYSCLK_KHZ=792000 -DFW_VREG=VREG_VOLTAGE_1_60`
(see the table in `src/main.c`; above 550 MHz the voltage is yours to choose). With a 12 MHz crystal 800 MHz is not reachable
exactly, 792 MHz (VCO 1584 MHz / 2) is the nearest. The code raises the vreg limit when needed (only above 400 MHz), sets the XIP flash divider
first (SCK <= 66 MHz) and scales the 595/165 chain delays and every ns timing with the clock.
The core and internal regulator get hot at high voltage: a heatsink helps, and check the 3.3 V LDO and the 3.3 uH
inductor (L2) current at the chosen voltage. References downloaded to
the Raspberry Pi forum thread 375975 and the code gist it links.

## Not flashing
Only SWD is wired (J2), no BOOTSEL/USB. To avoid flashing:
- `tools/ram.sh` loads the RAM build over SWD and runs it. Nothing is written to flash; edit, rebuild, reload in seconds.
- Everything tunable lives in a RAM block at 0x2007C000 (`src/cfg.h`): ISA strobe timing, IOCHRDY timeout, chain speed,
  port filter bitmap, feature flags, counters, a trace ring, and a command mailbox. Change with openocd `mww`, or
  `source tools/isa.tcl` for `isa_inb 0x220`, `isa_outb`, `isa_irq`, `isa_drq`, `fw_set t_cmd_ns 800`, `fw_stats`.
- Run ISA cycles from the probe (mailbox) to test wiring and cards before any LPC host is connected.

## Hardware coverage (what the board can do, and what the firmware does with it)
| Board function | Firmware |
|---|---|
| ISA 8 bit I/O read/write (SA0-15, IOR#/IOW#, IOCHRDY wait, 15 us timeout) | LPC I/O cycles, writes posted; port filter bitmap |
| IRQ3-7, 9-12, 14, 15, IOCHCK# (165 chain) | SERIRQ frames 3-15 and 16 (IOCHCK#), pulse stretching (`irq_hold_us`, 30 us) |
| DRQ0-3 (8 bit), DRQ5-7 (16 bit), DACK0-7, TC, AEN | LDRQ# messages, 8 and 16 bit DMA both directions, TC, "more" SYNC from the DRQ state, 8 LCLK rule |
| RESETDRV = NAND(ISA_RUN, PCIRST#) | held until the chain is loaded; PCIRST# edges restart all PIO engines and forget DMA/IRQ state |
| LPC clock, LAD, LFRAME#, LDRQ#, SERIRQ, PCIRST# | front end, script player, LDRQ#, SERIRQ, reset handling |
| SWD only | RAM load, live tuning block, mailbox for ISA cycles from the probe |

Not possible with this hardware (no driver or no readable input on the board): ISA memory cycles (MEMR#, MEMW#, SMEMR#,
SMEMW# have pull-ups only), LA17-23, bus mastering (MASTER# only pulled up), MEMCS16#/OWS# (not readable), REFRESH#,
BALE control (held high), OSC (B30 = 14.318 MHz oscillator, not firmware controlled), BCLK (fixed OSC/2 = 7.16 MHz), CLKRUN#, LPCPD#.

## Known limits
- LPC I/O cycles are 8 bit. DMA: 8 bit on channels 0-3, 16 bit on channels 5-7 (SBHE# low, SD0-15, DACK5-7). 32 bit transfers
  and channel 4 (bus master) are refused with SYNC error; the host does not use them unless the BIOS enables them.
- SERIRQ: continuous mode only (`isalpc` sets it). The bridge sends no start pulse, so in quiet mode (BIOS default on the
  X99M) card IRQs never reach the host. Polarity per the X99 PCH datasheet: an IRQ frame is driven low while the ISA IRQ
  line is low, released while it is high; IOCHCK# frame low = asserted. Only frames in `irq_frames` are served (default 3, 5,
  7, 9, 10, 11, 15, IOCHCK#): the Super I/O owns the rest (COM1 IRQ4, PS/2 mouse IRQ12).
  PIO instruction memory: FE 32, TX 27, AUX 29 of 32, further LPC features need a redesign.
- No memory cycles, no bus master, no firmware hub cycles.
- Wait-state SYNC is always 0110 (long, no host timeout in LPC 1.1 4.2.1.9).
- 400 MHz default: working window 340..500 MHz in the simulator. Core 1 claim latency not measured on silicon (trace the SYNC
  start clock with a logic analyser).
- RP2350-E9: DRQ lines rely on the external 4.7k pull-downs, the firmware never enables internal pull-downs.

## Host setup
The PCH forwards only decoded ranges to LPC, and the bridge must claim only the ports of the card in the slot. `host/isalpc`
does both, on DOS (`ISALPC.EXE`, in AUTOEXEC.BAT before the sound drivers) and on Linux (as root):

    isalpc sb16                 Sound Blaster 16 at 220, MPU 330, OPL 388, game port
    isalpc orpheus2 force       Orpheus II: needs all four GEN ranges, so BIOS ranges are replaced
    isalpc gus=220 com1 fdc     presets take a base; ports may also be given as lo-hi
    isalpc                      show the decode ranges, SERIRQ, LDRQ1#, PCI IRQs and the bridge
    isalpc list                 presets

- PCH (D31:F0): GEN1-4 decode ranges with address masks, the fixed COMA/COMB/LPT/FDD/game decodes that are free, 4E/4F,
  SERIRQ in continuous mode, optionally LDRQ1# instead of GPIO23 (`ldrq1`) and the DMA controller
  reset (`dma`, default on DOS). Ranges the BIOS set are kept unless `force` is given; then as few as needed are replaced,
  GEN4 first, and a replaced range stays forwarded when one of the new ranges can cover it. The BIOS COM/LPT/floppy
  decodes are taken last. The tool prints what it replaced. `nopirq` also switches off the PCI interrupt routing (PIRQ A-H)
  of the IRQs the bridge serves and sets them back to edge triggered (ELCR), for systems whose PCI devices do not use
  interrupts. It needs the bridge to answer (its IRQ list decides). A card that needs ports inside the PCH GPIO block (WSS at 530 with
  GPIOBASE 500, as on the X99M Killer) gets the block moved to 680 (`gpio=` picks another place). That is for DOS: ACPI
  systems expect the old address. Ports inside PMBASE are refused.
- Bridge, through its config port 4E/4F (`src/cfgport.c`: index port takes 'I' 'S' 'A' and a 16 bit offset, the data port
  then reads or writes `fw_dbg_t` bytes): `deny[]` becomes "everything but the ports of the selected cards", `irq_frames`
  gets the IRQs of the presets (or `irq=` exactly), `word_rd[]` the 16 bit data ports of the presets (IDE, NE2000,
  AHA-152x, EMU8000, GUS at the given base). The settings live in RAM: run the tool after every power-up.
  `F_NO_CFGPORT` gives 4E/4F back to the slot.
- Four GEN ranges cover one xx00-xxFF block each. Cards with ports in more blocks than that do not fit: Pro Audio Spectrum 16
  (19 blocks, Linux `pas2_*.c`), AWE32 together with ISA PnP. The tool says which blocks are left over.
- COM, LPT and floppy cards: disable the onboard port in the BIOS first, or both answer.
`make -C host test` runs the tool against a fake PCH and the firmware's config port code.

## Simulation
`python3 sim/test_lpc.py` (I/O read/write, 8 and 16 bit DMA read/write) and `python3 sim/test_aux.py` (LDRQ#, SERIRQ) run the PIO programs in a small cycle simulator (own code,
`sim/pio_sim.py`, own assembler) against a behavioural LPC host with random LCLK phase, launch delay, core 1 claim latency (`CLAIM_MAX`, default 40 sys clocks)
and CPU latency, plus cycles of other devices that must stay undriven.
They check cycle timing, SYNC/data/TAR positions and bus contention. The simulator is not the silicon: confirm with a
logic analyser on LAD0-3/LFRAME#/LCLK before trusting the timing margins.
