#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# usage: tools/build.sh [ram]    needs PICO_SDK_PATH (SDK 2.1+), arm-none-eabi-gcc, cmake, ninja
set -e
cd "$(dirname "$0")/.."
: "${PICO_SDK_PATH:=$HOME/work/toolchain/pico-sdk}"
export PICO_SDK_PATH
TC=$HOME/work/toolchain/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi/bin
[ -d "$TC" ] && export PATH="$TC:$PATH"
if [ "$1" = ram ]; then B=build-ram; OPT=-DFW_RAM=ON; else B=build; OPT=; fi
cmake -S . -B $B -G Ninja $OPT -DFW_SYSCLK_KHZ=${FW_SYSCLK_KHZ:-400000}
ninja -C $B
ls -la $B/isa_lpc.elf $B/isa_lpc.uf2 2>/dev/null
