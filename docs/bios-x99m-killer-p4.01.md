# ASRock X99M Killer/3.1 BIOS P4.01: what it does to LPC (static analysis, 2026-11-11)

Image `X99MK3_4.01` (16 MB flash image, sha256 8b38bc5b...e0eca3, not in the repo). Unpacked with
`tools/bios/uefi.py` (651 files), DSDT scanned with `dsdt.py`, setup defaults with `ifr.py`.
Nothing here was checked on the running board: `isalpc` without arguments shows the live values.

## Breaks something
1. GPIOBASE = 0x500 (DSDT `GPBS`, and SBPEI writes ports 500-56B), PMBASE = 0x400, SMBus 0x580. Ports 500-57F are the PCH GPIO
   registers: a WSS codec at 530/534 cannot work and writes there would change GPIOs. Hits Orpheus II (CS4237B WSS 534/538),
   OPL3-SA, every WSS card at its default base. `isalpc` moves the GPIO block (default to 680) when a preset needs these
   ports: after boot only PchSmiDispatcher and PlatformResetSmm touch GPIO, and both read the base from the chipset. All
   hard-coded accesses to 500-57F are in SEC/PEI/DXE modules. The DSDT keeps 500 (region GPIV at 52C): DOS only.
2. The BIOS uses three of the four GEN decode ranges: GEN1 = 280-2BF (NCT6791D, hardware monitor at 290/295/296, table in
   NCT6791DPeiInit), GEN3 = CA0-CAF (IPMI KCS of the reference code, 0x000C0CA3), GEN4 = 600-61F (0x001C0601), both written by
   PlatformEarlyInit. One range is free, so nearly every `isalpc` preset needs `force`. It replaces GEN4, then GEN3, then
   GEN1, and keeps 280-2BF forwarded when a new range covers that block (SB16: only GEN4 goes; Orpheus II: all three, the
   hardware monitor stays reachable; AWE32: five blocks, costs the hardware monitor range and both COM decodes).
3. (Withdrawn 2026-11-11, review.) LDRQ1# is not a problem: the SBPEI GPIO table (E3C168, low byte = pin number, flags in
   the high byte, pin 8 has no entry) holds 0x0017 for GPIO23 = native. The first reading took entry 0x0318 for GPIO23; it
   is GPIO24 (MGPIO0, the ME recovery strap), a GPIO input. Both LDRQ0# and LDRQ1# work without `isalpc ldrq1`.
4. Serial IRQ Mode defaults to Quiet (hidden Intel RC option, varstore offset F7). The bridge then has to start every SERIRQ
   cycle itself (`serirq_kick`, never run on silicon). `isalpc` switches to continuous mode (`keepsirq` to leave it).
5. Cards jumpered into 280-2BF (SB at 280, NE2000 at 280) share the range with the Super I/O decode; 290-297 is taken.

## Harmless, worth knowing
- LPC_EN = 0x3803 early (COMA 3F8, COMB 2F8, 62/66, 2E/2F, 4E/4F on; LPT, floppy, game port off). LPC_I/O_DEC: COMA 3F8,
  COMB 2F8, LPT 378. Both COM decodes are on although the board has one port: none is free for 220-22F without `force`.
- 4E/4F: PlatformInfo (PEI) probes a BMC Super I/O there on every boot (out 4E 5A, 07; out 4F 02; in 4F; out 4E A5). The
  bridge's config port stays locked and returns FF, which reads as "no BMC". The board's own Super I/O is at 2E.
- Super I/O setup: COM1 3F8/IRQ4 on (setup can move it to 3E8 or switch it off), LPT and second UART off, KBC IRQ 1 and 12.
- PCI interrupt links may use IRQ 3,4,5,6,7,10,11,12,14,15 (DSDT). No "reserve IRQ for ISA" option exists; the legacy
  routing table is built at boot. A PCI device can land on the sound card's IRQ: `isalpc` lists the IRQs PCI uses.
- Port 61 bits 2 and 3 are set early: IOCHK# and SERR# NMIs are off.
- Port 80 goes to LPC (default). The bridge ignores it (ports below 100 are denied).
- Security Device Support defaults to on: TPM probing uses TPM-type LPC cycles at FED40000, which the front end skips.
- CSM on by default, option ROM policy "Legacy only". Fast Boot off. Legacy USB on (port 60/64 SMI emulation).
- ICC: clock enables are not overridden (ME profile decides); "ICC Unused Clocks" enabled. Whether the TPM header gets
  LCLK is not visible in the image: scope it.
- DSDT also reserves 600-61F, 800-81F, 880-883 (I/O traps of the reference code).

## What can be switched off (list for the user, nothing changed yet)
Setup already offers (no image change): Serial Port (COM1 3F8/IRQ4), Onboard HD Audio, Intel I218-V LAN, Killer E2201 LAN,
SATA and S_SATA controllers, USB 3 controller, Thunderbolt, Security Device Support (TPM probing), boot from LAN. Each PCI
device switched off is one less taker for IRQ 5/7/10/11. Keep CSM and Legacy USB (keyboard under DOS).

Only by patching the image (all sites are uncompressed PEI code or tables at fixed offsets of the 16 MB file):
| What | Where | Change | Gain | Risk |
|---|---|---|---|---|
| GEN3 decode CA0-CAF (IPMI leftover) | PlatformEarlyInit, E1D287 | imm32 000C0CA3 -> 0 | one free GEN range from power-on | none seen: no BMC on the board |
| GEN4 decode 600-61F | PlatformEarlyInit, E1D296 | imm32 001C0601 -> 0 | one free GEN range | DSDT reserves it; no user found in the image |
| COMB decode 2F8 (no COM2 on the board) | PlatformEarlyInit, E1D271 | LPC_EN 3803 -> 3801 | COMB free for 220-227 | none seen |
| Super I/O decode 280-2BF | NCT6791DPeiInit table, E0DCCC | base 280 len 40 -> base 290 len 10 | 280-28F and 2A0-2BF free for cards | low: only 290-297 is used |
| COM1 enable at PEI | NCT6791DPeiInit script, E0DD44.. | LDN2 CR30 01 -> 00 | none: Setup's Serial Port switch decides later | - |
| Hardware monitor (LDN B, 290) | NCT6791DPeiInit script | CR30 01 -> 00 and drop the decode | 290-29F free | fan control and the H/W monitor page stop working: not advised |
| Serial IRQ Mode default Quiet | setup defaults (NVRAM default store, varstore offset F7) | 0 -> 1 | continuous mode from power-on | store not located yet |
| IRQs PCI may take (IsaIrqMask in GenericSio, PciBus, CsmDxe) | default mask constant | add 5 and 7 | PCI never lands on the sound card IRQ | constant not located yet |

Not possible by patching: moving the GPIO block off 500 (address built into about 15 modules and the DSDT).
Every image change above is also done by `isalpc` after boot (with `force`); the patch only makes it true from power-on.
Flashing: the descriptor lets the host write the BIOS region, no AMI Secure Flash key file is in the image; whether ASRock
Instant Flash takes a changed file is unknown. A wrong PEI patch means no boot: SPI programmer and a backup first.

## Patched image (2026-11-11, user: keep fan control, carry on)
`tools/bios/patch_x99m.py` checks the sha256 of P4.01 and the expected bytes at each site, then
changes 7 bytes (all in one uncompressed PEIM file with a fixed 0xAA data checksum, so no checksum edit):
- E1D271 LPC_EN 3803 -> 3801 (COMB 2F8 decode off), E1D287 GEN3 -> 0 (CA0-CAF), E1D296 GEN4 -> 0 (600-61F).
- The first version (commit d9c8ebd) also changed E3C197 and so set GPIO24, the ME recovery strap, to native by mistake.
  That byte is back to the original; do not use the d9c8ebd image.
- Checked: the patched file still unpacks to the same 651 files, all file checksums valid, `cmp` shows exactly 7 bytes.
- Not patched: Super I/O window 280-2BF (table layout not decoded, fans depend on it; `isalpc force` shrinks it at runtime),
  Serial IRQ default and IsaIrqMask (the code is in LZMA-compressed sections: patching means rebuilding the volume).
- Not flashed, not booted. Output is `~/Downloads/x99m-bios-isalpc/X99MK3_4.01.isalpc` (kept out of the repo, vendor image).
  Needs an SPI programmer or whatever flash path ASRock allows for a modified file; keep the original zip next to it.
- User view on PCI IRQs: none of Voodoo5 5500, Radeon 9070, NVMe, NICs needs one, maybe the Aureal Vortex 2. `isalpc nopirq`
  switches off PIRQ A-H routing for the IRQs the bridge serves (opt-in, not done by default).

