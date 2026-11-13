# Licenses

This repository mixes parts with different licenses:

| Part | License | Why |
|---|---|---|
| `pcb/`, `fab/`, `lib/`, `sch/`, `docs/`, `images/`, `tools/` except the two folders below | **CC BY-SA 4.0** ([LICENSES/CC-BY-SA-4.0.txt](LICENSES/CC-BY-SA-4.0.txt)) | The ISA socket footprint and the pull-up network come from dISAppointment (CC BY-SA 4.0). Share-alike carries over to what is derived from them. |
| `fw/` (firmware, PIO programs, simulator, tests), `host/` (ISALPC DOS and Linux tool and its self-test), `tools/bios/`, `tools/review/` | **GPL-2.0-or-later** ([LICENSES/GPL-2.0.txt](LICENSES/GPL-2.0.txt)) | Independent code. The firmware builds against the Raspberry Pi pico-sdk (BSD-3-Clause, compatible). `host/isalpc.c` was written from scratch; only the list of DMA controller registers it resets is shared with sapphisa and lpcexp. |

The MCU power circuit values follow Raspberry Pi's RP2350 minimal design (MIT, [LICENSES/RaspberryPi-RP2350-minimal-design-MIT.txt](LICENSES/RaspberryPi-RP2350-minimal-design-MIT.txt)).

The BIOS image of the ASRock X99M Killer is **not** part of this repository; only a patch script that checks the sha256 of the original.

CC BY-SA 4.0 and GPL-2.0 are not compatible with each other: keep the two groups apart when you reuse files. The share-alike part is not our choice, it comes from the dISAppointment files the hardware derives from.
