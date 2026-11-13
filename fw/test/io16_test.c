// SPDX-License-Identifier: GPL-2.0-or-later
// cc -I../src -o io16_test io16_test.c && ./io16_test
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define dbg test_dbg
#include "../src/cfg.h"
static fw_dbg_t test_dbg;
#include "../src/io16.c"

static char out[256]; static int n;
static uint16_t bus16 = 0xBEEF; static uint8_t bus8 = 0x5A;
bool isa_iocs16(uint16_t a) { return (a & ~3u) == 0x620 || (a & ~1u) == 0x300; }
bool isa_iocs16w(uint16_t a) { return isa_iocs16(a); }
void isa_addr_forget(void) {}
void isa_io_write(uint16_t a, uint8_t d) { n += sprintf(out + n, "w8 %x %02x;", a, d); }
uint8_t isa_io_read(uint16_t a) { n += sprintf(out + n, "r8 %x;", a); return bus8; }
void isa_io_write16(uint16_t a, uint16_t d) { n += sprintf(out + n, "w16 %x %04x;", a, d); }
uint16_t isa_io_read16(uint16_t a) { n += sprintf(out + n, "r16 %x;", a); return bus16; }
#define EXPECT(s) do { assert(!strcmp(out, s) || (printf("got '%s' want '%s'\n", out, s), 0)); n = 0; out[0] = 0; } while (0)

int main(void) {
    dbg.cfg.word_rd[1] = 0x620u | 0xE83u << 16;
    io16_write(0x620, 0x34, 100); io16_write(0x621, 0x12, 101); EXPECT("w16 620 1234;");          // outw
    io16_write(0x620, 0x77, 300); io16_tick(303); EXPECT(""); io16_tick(305); EXPECT("w8 620 77;");   // lone even after 5 us
    io16_write(0x621, 0x88, 400); EXPECT("w8 621 88;");                                            // lone odd: at once
    io16_write(0x620, 0x01, 500); io16_write(0x622, 0x02, 501); EXPECT("w8 620 01;");             // not a partner
    io16_flush(); EXPECT("w8 622 02;");
    io16_write(0x220, 0x05, 600); io16_write(0x221, 0x06, 601); EXPECT("w8 220 05;w8 221 06;");   // no IOCS16#: never held
    assert(io16_read(0x620, 700) == 0xEF && io16_read(0x621, 701) == 0xBE); EXPECT("r16 620;");   // inw: one cycle
    assert(io16_read(0x620, 800) == 0xEF); io16_tick(803); assert(io16_read(0x621, 810) == 0x5A); EXPECT("r16 620;r8 621;");   // too late
    assert(io16_read(0x620, 820) == 0xEF); io16_tick(840); assert(io16_read(0x621, 843) == 0xBE); EXPECT("r16 620;");   // slow cycle (IOCHRDY): window starts at its end
    assert(io16_read(0x620, 900) == 0xEF && io16_read(0x220, 901) == 0x5A && io16_read(0x621, 902) == 0x5A);
    EXPECT("r16 620;r8 220;r8 621;");                                                               // other read drops the cached byte
    io16_write(0x620, 0x11, 1000); assert(io16_read(0x620, 1001) == 0xEF); EXPECT("w8 620 11;r16 620;");   // write flushed first
    assert(io16_read(0x300, 1100) == 0x5A && io16_read(0x301, 1101) == 0x5A); EXPECT("r8 300;r8 301;");   // not listed: byte cycles
    io16_write(0x300, 0x34, 1200); io16_write(0x301, 0x12, 1201); EXPECT("w16 300 1234;");          // writes pair on any IOCS16# port
    puts("io16 ok");
    return 0;
}
