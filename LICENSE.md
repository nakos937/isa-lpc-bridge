# Licenses

**Everything written for this project is GPL-2.0-or-later** ([LICENSES/GPL-2.0.txt](LICENSES/GPL-2.0.txt)): the board design
(`pcb/`, `fab/`, `sch/`, our footprints and symbols in `lib/`), the firmware (`fw/`), the host tool (`host/`), the scripts in
`tools/`, `docs/` (our own text) and `images/`. The firmware builds against the Raspberry Pi pico-sdk (BSD-3-Clause, compatible).

**Exceptions, third-party material that keeps its own license:**

| File | License | Origin |
|---|---|---|
| `lib/isa_bridge.pretty/ISA.kicad_mod` (ISA socket footprint) | CC BY-SA 4.0 ([LICENSES/CC-BY-SA-4.0.txt](LICENSES/CC-BY-SA-4.0.txt)) | [dISAppointment](https://github.com/rasteri/dISAppointment) by rasteri |
| `lib/disappointment_pulls.json`, `lib/isa_socket_pinout.json` (pull-up values and socket pin roles, extracted from its netlist) | CC BY-SA 4.0 | dISAppointment |
| `docs/datasheets/` | their publishers' terms, not ours | see [docs/datasheets/README.md](docs/datasheets/README.md) |

The ISA socket footprint is embedded in `pcb/PASS_HG25.kicad_pcb`, so that one file carries CC BY-SA 4.0 material inside it.

The MCU power circuit values follow Raspberry Pi's RP2350 minimal design (MIT, [LICENSES/RaspberryPi-RP2350-minimal-design-MIT.txt](LICENSES/RaspberryPi-RP2350-minimal-design-MIT.txt)).
`host/isalpc.c` was written from scratch; the list of DMA controller registers it resets is shared with sapphisa and lpcexp,
which are CC BY-SA 4.0 in the copies we downloaded.

The BIOS image of the ASRock X99M Killer is **not** part of this repository; only a patch script that checks the sha256 of the original.
