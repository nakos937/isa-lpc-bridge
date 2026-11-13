# PASS_HG25 pre-order audit (2026-11-11)

Scope: the board file `pcb/krt/PASS_HG25.kicad_pcb`, `fab/HG25` (gerbers, BOM, CPL), every IC pin, connector, pull and power
part against datasheets, the Raspberry Pi reference design and the dISAppointment board. Method: pad/net/position dump with
pcbnew, kicad-cli DRC, datasheet text and drawings, LCSC and JLC stock queries. Nothing was measured on hardware.

## Findings
1. Blocker, fixed. FB1/FB2/FB4 (BLM31KN601SN1L, C668306): JLC had 2 in stock, 3 are needed per board. These beads feed
   +5V_SLOT, +12V_SLOT and the inverter: without them the slot has no power. BOM and `fab/lcsc_map.json` now use C237603
   BLM31KN601SH1L (same part in the SH grade, 215 in stock).
2. Must check before power, cannot be settled from documents. J1 pin 1 against the plug key: the TE drawing is not
   reachable (TE blocks the download, the LCSC sheet has no drawing). Meter procedure is in `order-HG25.md`; a wrong
   orientation is fixed by swapping two contacts in the plug, the board is not lost.
3. Must check in the JLC preview. L2 polarity dot at the silk dot (left pad, +1V1). Our footprint has pad 1 on the right
   and is placed at 180 degrees; the Raspberry Pi footprint has pad 1 (dot, +1V1) on the left at 0 degrees. Net and
   geometry match the reference (output capacitor directly below the inductor). The footprint description said the
   opposite and is corrected. Also every IC pin 1, D1-D4 cathodes, Y1/Y2, U1, U3 as listed in the order sheet.
4. Low. LDRQ#/DRQ/IOCHRDY and the LPC pins go straight to MCU pads. DRQ0-7 and IOCHRDY are fault tolerant pads (5.5 V with
   IOVDD up, 3.63 V with IOVDD off): a card that drives DRQ high while the 3.3 V rail is down exceeds that for
   milliseconds. The LPC pads (GPIO40-47) are not tolerant: never run the host with J4 connected and J1 unplugged.
   Both are already in the order sheet.
5. Low. The 3.3 V pull-ups on IOR#, IOW# and the 4245 OE# follow the 3.3 V rail, not VCCA as TI's power-up note asks.
   The LDO tracks the 5 V rail within a millisecond and the 68 ohm resistors limit any contention; not a functional risk.
6. Low. +1V1 reaches the DVDD pins through 0.1 mm traces (20 mm outer, 28 mm inner). Fine at the expected core current;
   it is thinner than the rest of the power routing.
7. Info. Stock is thin for F1 (8), J3 (29), U2 (143), Y1 (207).
8. Info. J4 is wired by jumper leads: odd pins (LCLK, LFRAME#, PCIRST#, LAD3, LAD0, LDRQ#) are the pad row away from the
   board edge, even pins the row at the edge. Confirm each lead with a meter (TP8 = LCLK = pin 1).

## Checked, no finding
- Pin by pin against datasheets: U1 BL9110 SOT-223-3-B (1 GND, 2 OUT + tab, 3 IN; the FA/FC/FD variants differ), U2 ADP5071
  LFCSP (Table 5, all 20 pins + pad), U3 MC79M05 DPAK (1 GND, 2 IN + tab, 3 OUT), U4 RP2354B (all 80 pins + pad against
  the KiCad RP2350B symbol and the datasheet figure; QSPI pins open as an RP2354 needs), U5-U7 595 and U8/U9 165 in BQB,
  U10 244, U11 138, U12/U13 4245 (pin 1 VCCA 5 V, 23/24 VCCB), U14 2G74, U15 1G125, U16 1G00, Y1, Y2 (Taitien OX: pin 1
  enable high, 3.3 V, 14.31818 MHz per the LCSC listing), D1-D4 polarity, L1, F1, F2.
- J3: all 98 pads have the same position, drill, pad size and net as the dISAppointment footprint (a board that works),
  same side and handedness; the ED98KBGFBK drawing gives the same 2.54 pitch, 5.08 row spacing and 129.54 overall span.
  The bracket end (pin 1) is 7 mm from the board edge, so the card bracket hangs free.
- J4 against the manual drawing of TPMS1 (page 23): every connected pin matches; host +3V, +3VSB, SMBus and S_PWRDWN#
  pins are left open.
- Pulls: every resistor classified by rail. Strobes, OE#, SH/LD#, LDRQ#, PCIRST#, RUN up; AEN, TC, ISA_RUN, SD_DIR, DRQ, IRQ
  down; slot-side pulls on 5 V only behind buffers or on 5 V tolerant LVC inputs. No dangling net, no one-pad net.
- Power: ADP5071 feedback 1M/62k = -12.10 V, EN2 2.28 V, diode and capacitor ratings, 79M05 from -12.1 V, TVS standoffs
  against ATX tolerances, capacitor voltage codes of the actual MPNs (C24 and C25 are 25 V and 50 V parts).
- 5 V path to the slot: 27 mm of 1.0 mm, fuse, 20 mm of 0.6 mm, bead, then a B.Cu pour 3.7 to 12 mm wide along the socket:
  about 45 mOhm of copper plus fuse (<= 50 mOhm) and bead (38 mOhm).
- Fab data: DRC 0 violations, 0 unconnected, 0 parity errors; gerbers are newer than the board file; no NPTH; drills 0.2,
  0.3, 1.0, 1.2 mm; BOM and CPL list the same 153 parts as the board, every position and angle equals the board's.
  Design rules sit at the JLC multilayer minimums (0.09 to 0.1 mm), 3606 trace segments are 0.1 mm wide.

## Not checked
Mechanical fit in the case, JLC's rotation models (preview only), signal integrity on real cards, anything on silicon.

## Via price tier (2026-11-12)
JLC capability page (saved: the JLCPCB capabilities page (jlcpcb.com/capabilities/pcb-capabilities)): multilayer 1 oz track width and
spacing 0.09 / 0.09 mm; pad to track 0.1 mm; via hole to track 0.2 mm; via diameter at least 0.1 mm (0.15 preferred) above the
hole. Pricing note 3: "0.1mm or 0.15mm hole size with any size via diameter, and 0.2mm or 0.25mm hole size with via diameter
less than 0.45mm, will cost more."
- PASS_HG25 has 0.2 mm holes in 0.45 mm vias: by that sentence it is not in a surcharge tier, so the default order option
  "0.3mm/(0.4/0.45mm)" should apply to the unchanged board. The earlier advice to pick "0.2mm/(0.3/0.35mm)" (+17.61 USD, and it
  pulls in the 4-wire Kelvin test, +17.11 USD) was read off the option label, not this rule. JLC's file review has the last word.
- `pcb/krt/CAND_HG25_via03.kicad_pcb` (all 1242 small vias drilled 0.3 mm, made by `pcb/krt/scripts/via03_*.py`) is NOT
  orderable. It passes the project DRC, but that rule set only checks via holes against tracks. An independent audit
  (own distance maths, 2026-11-12) against JLC's "via hole to copper 0.2 mm" found the 0.3 mm holes 0.166 to 0.176 mm from
  other-net copper: 1078 places on the inner ground plane, 1181 on the B.Cu pours, 42 against other vias, 5 against pads.
  With the rule widened to all copper (`(condition "A.Type == 'Via'")`) the pours refill correctly and lose about 1 % of
  their area, and most via pairs can be moved apart, but four clusters stay boxed in (near 145.0/24.5, 69.4/16.9,
  66.0/15.2, 148.9/10.7 mm): those need hand rerouting. Abandoned unless JLC refuses the default via tier for PASS_HG25.
- PASS_HG25 in the same audit: smallest via hole to other-net track 0.225, to other via copper 0.227, to pour copper 0.216,
  to pad copper 0.225 (outer) and 0.270 (inner), hole to hole 0.352, track to track 0.100 mm. Nothing below a JLC limit.
