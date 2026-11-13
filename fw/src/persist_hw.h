// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PERSIST_BYTES 8448u                       // 33 flash pages: the block, padded to a page boundary

const uint8_t *persist_hw_data(void);             // PERSIST_BYTES at the saved location (memory mapped flash)
bool persist_hw_write(const uint8_t *buf);        // erase, program PERSIST_BYTES, verify
bool persist_hw_erase(void);
