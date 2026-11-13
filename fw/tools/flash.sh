#!/bin/sh
# Program the flash build over SWD (only needed when the PIO programs or the C code change; tuning needs no reflash).
cd "$(dirname "$0")/.."
: "${OPENOCD:=$(command -v openocd || echo $HOME/work/toolchain/openocd/bin/openocd)}"
$OPENOCD -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000" \
  -c "program build/isa_lpc.elf verify reset exit"
