// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PERSIST_STATUS_OK    0x00                 // last request done (or none)
#define PERSIST_STATUS_BUSY  0x01
#define PERSIST_STATUS_ERROR 0xE0

bool    persist_load(void);                       // boot: overlay a valid saved block on dbg.cfg; false if none or not usable
bool    persist_valid(void);                      // a usable saved block exists
void    persist_request(char cmd);                // config port: 'W' save the current cfg, 'X' forget the saved one
void    persist_poll(void);                       // main loop: runs a pending request (blocks 0.1 to 1 s)
uint8_t persist_status(void);
