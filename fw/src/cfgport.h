// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

#define CFGPORT 0x4Eu                                  // index; data = CFGPORT + 1
void    cfgport_write(uint16_t a, uint8_t d);
uint8_t cfgport_read(uint16_t a);
