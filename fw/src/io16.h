#pragma once
#include <stdbool.h>
#include <stdint.h>

void    io16_write(uint16_t a, uint8_t d, uint32_t now_us);
uint8_t io16_read(uint16_t a, uint32_t now_us);
void    io16_flush(void);                            // before any other bus cycle
void    io16_tick(uint32_t now_us);                  // lone byte older than the pair window goes out
void    io16_drop(void);                             // host reset: forget held bytes and the sensed IOCS16#
