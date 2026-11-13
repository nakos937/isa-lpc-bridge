# Licenses

This repository mixes parts with different licenses:

| Part | License | Why |
|---|---|---|
| `pcb/`, `fab/`, `lib/`, `sch/`, `host/` (the host tool), `docs/`, `images/`, `tools/` except the two folders below | **CC BY-SA 4.0** ([LICENSES/CC-BY-SA-4.0.txt](LICENSES/CC-BY-SA-4.0.txt)) | The ISA socket footprint and pull-up network come from dISAppointment (CC BY-SA 4.0), `host/isalpc.c` is derived from its `sapphisa` and from lpcexp (CC BY-SA 4.0). Share-alike carries over to what is derived from them. |
| `fw/` (firmware, PIO programs, simulator, tests) | **MIT** (below) | Independent code. It builds against the Raspberry Pi pico-sdk (BSD-3-Clause). |
| `tools/bios/`, `tools/review/` | **MIT** (below) | Independent scripts. |

The MCU power circuit values follow Raspberry Pi's RP2350 minimal design (MIT, [LICENSES/RaspberryPi-RP2350-minimal-design-MIT.txt](LICENSES/RaspberryPi-RP2350-minimal-design-MIT.txt)).

The BIOS image of the ASRock X99M Killer is **not** part of this repository; only a patch script that checks the sha256 of the original.

## MIT License (firmware and scripts)
Copyright (c) 2026 nakos937

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
