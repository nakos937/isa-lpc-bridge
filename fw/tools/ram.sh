#!/bin/sh
# Load the RAM build over SWD and run it: no flash writes, a power cycle drops it.
# usage: tools/ram.sh   (probe: CMSIS-DAP, e.g. Raspberry Pi Debug Probe / Pico running picoprobe)
# A reset would start the bootrom, so the core is pointed at the image by hand: MSP and PC from the vector table, VTOR to it.
cd "$(dirname "$0")/.."
: "${OPENOCD:=$(command -v openocd || echo $HOME/work/toolchain/openocd/bin/openocd)}"
$OPENOCD -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000" \
  -c "init; reset halt; load_image build-ram/isa_lpc.elf" \
  -c "set v [read_memory 0x20000000 32 2]; mww 0xe000ed08 0x20000000; reg msp [lindex \$v 0]; reg pc [expr {[lindex \$v 1] & 0xFFFFFFFE}]; resume; shutdown"
