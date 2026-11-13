// cc -Isrc -o cfgport_test test/cfgport_test.c && ./cfgport_test
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#define dbg test_dbg
#include "../src/cfg.h"
static fw_dbg_t test_dbg;
#include "../src/cfgport.c"

static void open_at(unsigned o) { cfgport_write(0x4E, 'I'); cfgport_write(0x4E, 'S'); cfgport_write(0x4E, 'A'); cfgport_write(0x4E, o & 0xFF); cfgport_write(0x4E, o >> 8); }

int main(void) {
    dbg.cfg.magic = FW_MAGIC; dbg.cfg.build = 11; dbg.cfg.irq_frames = 0x18AA8; dbg.st.ior = 0x11223344;
    assert(cfgport_read(0x4F) == 0xFF);                                    // locked
    cfgport_write(0x4F, 0x55); assert(dbg.cfg.magic == FW_MAGIC);
    cfgport_write(0x4E, 0x26); cfgport_write(0x4E, 0x26); cfgport_write(0x4E, 0x5A); assert(cfgport_read(0x4F) == 0xFF);   // Fintek probe: stays locked
    cfgport_write(0x4E, 'I'); cfgport_write(0x4E, 'I'); cfgport_write(0x4E, 'S'); cfgport_write(0x4E, 'A');             // restart inside the key
    cfgport_write(0x4E, 0); cfgport_write(0x4E, 0);
    assert(cfgport_read(0x4F) == 'I' && cfgport_read(0x4F) == 'S' && cfgport_read(0x4F) == 'A' && cfgport_read(0x4F) == '1');
    assert(cfgport_read(0x4F) == 11);
    open_at(0); cfgport_write(0x4F, 0); assert(dbg.cfg.magic == FW_MAGIC);  // magic and build are read only
    open_at(offsetof(fw_cfg_t, irq_frames)); cfgport_write(0x4F, 0xF8); cfgport_write(0x4F, 0xDE); cfgport_write(0x4F, 0x01);
    assert(dbg.cfg.irq_frames == 0x1DEF8);
    open_at(offsetof(fw_cfg_t, deny) + (0x3F8 >> 3)); cfgport_write(0x4F, 0xA5); assert(dbg.cfg.deny[0x3F8 >> 3] == 0xA5);
    open_at(sizeof(fw_cfg_t)); cfgport_write(0x4F, 0); assert(dbg.st.ior == 0x11223344 && cfgport_read(0x4F) == 0x33);   // stats read only (offset stepped)
    open_at(sizeof(fw_cfg_t) + sizeof(fw_stats_t)); assert(cfgport_read(0x4F) == 0xFF);                                   // past the end
    cfgport_write(0x4E, 0x00); assert(cfgport_read(0x4F) == 0xFF);          // any index write locks again
    puts("cfgport ok");
    return 0;
}
