// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

void     isa_init(void);
void     isa_hold_reset(bool hold);
uint8_t  isa_io_read(uint16_t addr);                      // byte cycle, AT byte lanes (odd = SBHE# low)
void     isa_io_write(uint16_t addr, uint8_t data);
bool     isa_iocs16(uint16_t addr);                       // the port answers IOCS16# (sensed when the address changes)
bool     isa_iocs16w(uint16_t addr);                      // same, sensed with the word cycle address (SBHE# low)
void     isa_addr_forget(void);                           // next cycle shifts the address and senses IOCS16# again
uint16_t isa_io_read16(uint16_t addr);                    // SBHE# low: even = word, odd = byte on SD8..15
void     isa_io_write16(uint16_t addr, uint16_t data);
uint8_t  isa_dma_read(unsigned ch, bool tc);               // card -> host (IOR# with DACK)
void     isa_dma_write(unsigned ch, bool tc, uint8_t d);   // host -> card (IOW# with DACK)
uint16_t isa_dma16_read(unsigned ch, bool tc);             // card -> host, SBHE# low, SD0..15
void     isa_dma16_write(unsigned ch, bool tc, uint16_t d);
void     isa_chain_refresh(void);              // re-shift the current address, samples the IRQ inputs
uint32_t isa_chain_raw(uint32_t word24);       // returns the 165 readout (low 16 bits)
uint32_t isa_irq_stream(void);                 // last readout
uint32_t isa_serirq_mask(uint32_t stream);     // frame mask for SERIRQ
uint32_t isa_drq(void);                        // bit n = DRQn (n = 0..7, bit 4 always 0)
bool     isa_pcirst_n(void);
void     isa_ndelay(uint32_t ns);
