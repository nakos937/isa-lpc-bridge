# Order sheet PASS_HG25 (JLCPCB, 2026-11-10)

Files: `fab/HG25/PASS_HG25_gerbers.zip`, `fab/HG25/bom.csv` (53 rows, LCSC numbers, JLC class and stock columns), `fab/HG25/cpl.csv` (153 parts).

## PCB options
4 layers, 1.6 mm, 240.1 x 36.1 mm, green mask, HASL or ENIG, 1 oz outer. **Via covering: epoxy filled and capped (POFV)**: 89 vias sit in SMD pads (user decision 2026-11-08, price from the quote). Stack-up JLC04161H-7628 (standard).

Order form choices (2026-11-12):
- Min via hole size/diameter: the default "0.3mm/(0.4/0.45mm)". JLC's capability page charges extra only for 0.2/0.25 mm
  holes in vias under 0.45 mm; this board is 0.2 mm in 0.45 mm. Picking "0.2mm/(0.3/0.35mm)" added 17.61 USD and forced the
  4-wire Kelvin test (17.11 USD). If JLC's file review asks for the smaller tier, pay the difference.
- Via covering: epoxy filled and capped stays (101 vias in SMD pads down to 0.25 mm wide); it brings "Horizontal Electroless
  Copper Plating" with it.
- Surface finish: lead-free HASL is acceptable (user decision; ENIG costs 17.60 USD more and is only a margin on the 0.4 mm
  pitch MCU). Assemble 2 of the 5 boards. Keep "Confirm production file" and "Confirm parts placement".
- Order remark: L2 dot note (see below). J1 is keyed and the user checked it against the drawing.
- Quote seen with 5 assembled boards, ENIG, 0.2 mm tier: PCB 108.32 (engineering 25.00, via covering 17.61, surface finish
  17.60, Kelvin 17.11, via size 17.61, board 5.40, material 3.52, plating 3.42, confirm 1.05), PCBA 212.98 (components
  106.43, extended components fee 74.16 for 29 lines, manual assembly 11.53, setup 8.24, SMT 6.55, hand soldering 3.61,
  stencil 1.55, nitrogen 0.91).

## PCB assembly options
Economic, single-sided (top), 5 pcs suggested. Upload BOM and CPL. In the BOM tool:
- J2 is now BOOMELE C91552 (cheapest with JLC in-house stock; no connector exists in the Basic/Preferred library). U8/U9 (C33901164) shows pre-order (global sourcing, not in the JLC assembly library): click Pre-order to attach it.
- Pre-orders expected for J3, L1, U5-U7, U11 (JLC's own stock is below need; minimums 8, 301, 23, 41).
- 2026-11-11 stock check (JLC parts API): FB1/FB2/FB4 changed to C237603 BLM31KN601SH1L (same Murata bead, 215 in stock; C668306 had 2, three are needed per board). Thin but enough for 5 boards: F1 C151171 (8), J3 C5173323 (29), U2 C481889 (143), Y1 C20625731 (207).
- Placement preview: check J3 (origin is 63.5 mm off its pad centre), J1, J2, J4, U3, every IC rotation, the L2 polarity dot (the part's white dot must sit at the silkscreen dot = the left pad, the +1V1 side, away from VREG_LX; the CPL angle is 180, so a JLC model with its dot on the left at 0 degrees shows it on the wrong side until turned) and the cathode band of D1-D4 (JLC diode models are often turned 180 degrees against KiCad; D3 and D4 sit on a custom SOD-123FL land, so the tool has no footprint hint); fix offsets in the preview (one arrow press = 0.0635 mm) before paying.

## Before powering
1. J1: the plug's red wire (+5 V) must land on the footprint pin marked +5V. Meter check with the PSU unplugged from the mains: push the floppy plug into J1, then measure continuity from a red wire (any Molex plug of the same PSU) to TP1 (+5V_SLOT) and from a yellow wire to TP2 (+12V_SLOT). If red reaches TP2 instead, do not power the board: swap the two outer contacts in the plug housing. The two middle pins are ground either way.
2. Plug the PSU connector with the PSU off.
3. Scope DRQ0-7 and IOCHRDY at the MCU pins: if peaks exceed 5.4 V with a real card, add a clamp or series resistor.
4. Support the 240 mm board (spacers or tape) before inserting a card (about 0.4 kgf per contact pair).
5. Check -12 V loop (ripple and start-up), -5 V, and 3.3 V rails before connecting the MCU side.
6. Never run the PC with J4 (TPMS1) connected and J1 unplugged: the LPC pins GPIO40-47 are not 5 V tolerant pads (limit IOVDD + 0.5 V), so the host's 3.3 V on an unpowered board back-feeds them.

## Open
Firmware built and simulated, not run on silicon (see fw/README.md); firmware from BUILD 9 needs this board (HG22 to HG24: commit 3e8d360); AM4 may not work (see review-30rounds.md rounds 4-5); -12 V loop compensation extrapolated (round 23).

## HG25 second and third pass (2026-11-10, `pcb/krt/scripts/eco_hg25b.py` + `eco_hg25c.py`, router nets `+1V1 +3V3_OSC M_PCIRST_N +3V3 GND M_LAD3`, then `M_LCLK M_LAD3`)
- D4 (DSK24, SOD-123FL) land `D_SOD-123` to `D_SOD-123F` (same as D3): the datasheet asks for pads at least 1.0 mm long and 1.5 mm wide, at most 1.9 mm apart; the old land was 0.9 mm pads 2.4 mm apart (lead 0.4 to 0.9 mm long, so the overlap could drop to 0.4 mm). Then (`eco_hg25c.py`) the D3 and D4 pads were widened from 1.1 to 1.5 mm across the diode (1.1 x 1.5 mm, footprint renamed `D_SOD-123FL_1.1x1.5` on the board, in `sch/build.py` and `fab/lcsc_map.json`); the SMF12A sheet (`datasheets/parts/C19077506`) has the same SOD-123FL outline table. M_LCLK (157.7 to 157.9 mm) and M_LAD3 (72.4 to 72.6 mm) were rerouted around the wider pads, same via counts.
- C68 100n at U4.32 (+1V1, 4.1 mm; the nearest capacitor was C35 at 8.0 mm) and C69 100n at Y2.4 (+3V3_OSC, 4.2 mm; was C62 at 8 mm). Nearest free 0402 spots on F.Cu (raster scan, 0.1 mm clearance); the RP2350 hardware guide asks for one 100 nF per power pin and accepts a shared capacitor where space is short.
- R27 10k from M_PCIRST_N to +3V3 at (98.65, 16.37): with J4 unplugged the U16 NAND input sat on the MCU's internal pull-up only. RESETDRV stays asserted either way (R14 holds ISA_RUN low until the firmware releases it).
- N$1/N$2 clearance to the +3V3 pour stays 0.1 mm: IPC-2221 (February 1998) Table 6-1, p. 39, row 0-15 V: B1 internal 0.05 mm, B2 external uncoated 0.1 mm, B4 external with permanent polymer coating 0.05 mm (`isa-lpc-bridge-research/text/ipc2221_src/IPC-2221-tortai.pdf`; the 12 V input sits in that row).
- Gates: KiCad DRC 0 (zones filled by KiCad, this file), 0 unconnected, BLOCKING 0, pad nets equal `sch/out/isa_bridge.net` (only the power flags and the unnumbered U3 pad differ, as before), B.Cu zone outline counts equal the first HG25 pass (+3V3 5, +5V_SLOT 1), 89 vias in SMD pads all capped and filled, no bottom parts. `tools/netcheck.py` PASS (expected-NC list and `M_x_o` nets added).
- Not changed: OSC on ISA B30 stays the 3.3 V oscillator output through R26 (TTL VIH 2.0 V; no room for a 5 V buffer, see HG23), DVDD 1.30 V at 400 MHz (user's choice), IRQ pull-downs (user decision), SA0-19/SBHE# floating while RESETDRV is high.

## HG25 changes against HG24 (2026-11-10, `pcb/krt/scripts/eco_hg25.py`)
- Power entry: N$1 (J1.1 +5 V to F1.1) and N$2 (J1.4 +12 V to F2.1) were 0.10 mm in HG18 to HG24 (fuse at 2.6 A / 1.5 A hold). Now B.Cu, 1.0 mm x 27.2 mm (13.4 mohm, 35 mV, 12 K rise at 2.6 A by IPC-2221) and 0.8 mm x 51.5 mm (31.6 mohm, 47 mV, 5 K rise at 1.5 A), three filled and capped 0.6/0.3 vias in each fuse pad.
- IOCS16# readable: U8.H (pin 6, first bit of the 165 chain, visible on MISO while SH/LD# is low) = IOCS16#; IRQ3 moved to U9.D (pin 14, was GND). No part change: BOM and CPL equal HG24.
- Router touched IRQ3 and IOCS16# only (`route_hg23.sh ... "IRQ3 IOCS16#"`); trunks kept so no pour is cut (B.Cu zone outline counts equal HG24).
- Gates: KiCad DRC 0 (zones filled by KiCad), 0 unconnected, BLOCKING 0, pad nets equal the netlist (0 differences; against HG24 only U8.6 and U9.14 changed), 87 vias in SMD pads all capped and filled; mask, paste, silkscreen, outline and NPTH gerbers equal HG24.

## HG24 changes against HG23 (review 2026-11-10, `pcb/krt/scripts/eco_hg24.py`)
- R26 33 ohm 0402 at (91.1, 27.35) between OSC_RAW (oscillator, U14 clock input) and the new net OSC_SLOT (ISA B30): reflections from the slot and the card no longer reach the BCLK divider. The branch from U14 to J3.30 was ripped and rerouted by the router (OSC_RAW 35 mm, OSC_SLOT 29 mm). TP4 label narrowed and moved off the R26 pads.
- C25 land 1210 to 1206: the BOM part C13585 is a 1206 body.
- One duplicated 0.0998 mm +5V_LOG neck at (48.6, 21.4) removed and set to 0.1 mm.
- Gates: KiCad DRC 0 (zones filled), 0 unconnected, BLOCKING 0, pad nets equal the netlist (0 differences), 80 vias in SMD pads all capped and filled, 0 untented vias, no bottom parts, fiducials unchanged.

## HG23 changes against HG22 (adversarial review, 2026-11-09)
- R22 to R25, 33 ohm 0402: source termination at the MCU pins of CHAIN_SCK (147 mm net, five loads), IOR# and IOW# (135 mm to the 244) and LCLK (160 mm). MCU side nets M_x_o, far side M_x. The IOR#/IOW# 4.7k pull-ups (R9, R10) moved to the MCU side so the far net does not cross under the MCU.
- RN5, RN16, RN17 (DRQ pull-downs): 8.2k to 4.7k arrays (same part as RN3/RN4, C29586). 8.2k +-5 % can reach 8.6 kohm, above the 8.2 kohm RP2350-E9 needs. 10k would be worse and cannot be used.
- ISA B30 (OSC) now connects to the 14.318 MHz oscillator output (OSC_RAW), no buffer: the F.Cu has no room for a SOT-23-5 anywhere near it. It is a 3.3 V CMOS signal on a 5 V TTL pin.
- Rerouted by the router around the new parts (all KiCad DRC clean, 0 unconnected): M_CHAIN_SCK, M_IOR_N, M_IOW_N, M_LCLK, OSC_RAW and, as neighbours, SD3, SD11, SD14, LDRQ_N, DRQ7.
- Firmware: SCK/IOR#/IOW# default to slow slew and 4 mA (`F_FAST_EDGES` restores fast).
- Not changed: -12 V without post-regulator (user decision), TPMS1 pin 13 (LDRQ# taps the key position, wire to the chipset point).

## Hardware review, checked without change (2026-11-09)
- ISA socket J3 against the PC/AT 8+16 bit pinout: B1-B31, A1-A31, D1-D18 (pins 63-80) and C1-C18 (pins 81-98) all match.
- ADP5071 (LFCSP) pin map against Table 5: all 20 pins correct (boost unused, EN1/FB1 low, SEQ open, SLEW and SYNC low). VNEG = 0.8 - 0.8 x 1M/62k = -12.10 V. Compensation 15k/22n (C27; 47n is the Table 11 row, see the BOM section) for 12 V in at 1.2 MHz (-24 V out); scaling the crossover formula to -12 V puts the crossover about 3x higher than that row (a few kHz), far below the right-half-plane zero (about 130 kHz at 0.5 A, 420 kHz at 150 mA). Not a stability risk; transient response is untested.
- 3.3 V LDO: about 0.2 A at 320 MHz, 0.3 W dissipated in SOT-223, no thermal issue expected.
- Logic pinouts (595, 165, 244, 138, 4245A, 1G125, 1G00, 2G74) against the TI datasheets, TPMS1 header against the X99M manual image: correct.

## BOM classes (JLC library, queried 2026-11-09)
18 Basic, 6 Preferred, 29 Extended rows. Swapped to Basic against HG22: C25 (10u/50V, C13585, 1206 body on the 1210 footprint), C27 (47n to 22n, C1532; the zero moves from 230 Hz to 480 Hz, still below a quarter of the crossover), R3 (1M, C26083). R4 is 62k (C37825, Extended): 1M/62k gives VNEG = -12.10 V; no Basic/Preferred pair reaches -12 V (1M/68k gives -10.9 V, 1M/56k -13.5 V). Extended rows went from 33 to 29 types.
Extended with no Basic/Preferred equivalent of the same function and package: connectors J1-J4, polyfuses F1/F2, ferrites FB1-FB5, inductors L1/L2, the resistor arrays RN1-RN22 (layout is fixed), the ICs U1-U11 and U14-U16, and both oscillators. Rejected swaps: Y1 to the Basic YXC X322512MSB4SI (80 ohm ESR and 20 pF load against 50 ohm and 10 pF; with the 1k series resistor the RP2350 oscillator may not start); U1 to the Basic AMS1117 (needs a tantalum-class ESR, the board has ceramics only); FB3/FB5 to the Basic GZ2012D601TF (500 mA rating, FB3 carries the 5 V logic and LDO current).
