// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

void lpc_init(void);
void lpc_poll(void);                 // serve every word the front end has queued
void lpc_service_irq(void);          // IRQ inputs -> SERIRQ frame mask (pulse stretching, quiet-mode kick)
void lpc_service_drq(void);          // DRQ -> LDRQ# messages
void lpc_reset(void);                // restart every PIO engine and forget DMA/IRQ state (host reset)
void lpc_set_sync(int long_wait);    // 1 = 0110, 0 = 0101
