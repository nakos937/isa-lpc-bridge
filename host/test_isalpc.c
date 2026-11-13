// make test: isalpc against a fake PCH (BIOS-like state) and the firmware's config port
#include <assert.h>
#define dbg test_dbg
#include "../fw/src/cfg.h"
static fw_dbg_t test_dbg;
#include "../fw/src/cfgport.c"
#define ISALPC_SIM
#define main isalpc_main
#include "isalpc.c"
#undef main

static void put16(int r, unsigned v) { sim_pci[r] = v; sim_pci[r + 1] = v >> 8; }
static void put32(int r, unsigned long v) { put16(r, v & 0xFFFF); put16(r + 2, v >> 16); }
static unsigned long g32(int r) { return pci_r32(r); }
static int fwd(unsigned p) {                                 // port forwarded to LPC by the fake PCH state
    for (int i = 0; i < 4; i++) if (gen_covers(g32(0x84 + 4 * i), p)) return 1;
    return fixed_covers(pci_r16(0x80), pci_r16(0x82), p);
}
static int denied(unsigned p) { return (test_dbg.cfg.deny[p >> 3] >> (p & 7)) & 1; }

static void bios(unsigned gpiobase_) {                       // state after POST: COM1, LPT, KBC, 2E/2F; GEN1 = hardware monitor 290-29F
    memset(sim_pci, 0, sizeof sim_pci); memset(sim_io, 0, sizeof sim_io); memset(&test_dbg, 0, sizeof test_dbg);
    put16(0, 0x8086); put16(2, 0x8D47); sim_pci[0x0A] = 0x01; sim_pci[0x0B] = 0x06;
    put16(0x80, 0x0010); put16(0x82, 0x1405);                // COMA 3F8 on, COMB 2F8 off, LPT 378 on, KBC, CNF1
    put32(0x84, 0x000C0291); put32(0x40, 0x1801); put32(0x48, gpiobase_ | 1); sim_pci[0x4C] = 0x10; sim_pci[0x64] = 0xD0;
    sim_pci[0x60] = 0x0B; sim_pci[0x61] = 0x0A; sim_pci[0x62] = 0x80; sim_pci[0x63] = 0x80; memset(sim_pci + 0x68, 0x80, 4);
    sim_io[gpiobase_ + 2] = 0x80; sim_io[0x682] = 0x80;      // LDRQ1# pin is GPIO23 (the fake keeps a copy where the block may move)
    test_dbg.cfg.magic = FW_MAGIC; test_dbg.cfg.build = 11; test_dbg.cfg.sysclk_khz = 400000;
    test_dbg.cfg.irq_frames = 0x18EA8; memset(test_dbg.cfg.deny, 0xFF, sizeof test_dbg.cfg.deny); test_dbg.cfg.deny[9] &= 0x3F;
}
static int run(const char *args) {
    static char buf[200]; char *argv[20]; int argc = 1;
    memset(allow, 0, sizeof allow); memset(need, 0, sizeof need); irq_add = irq_set = 0;
    have_irq_set = have_ports = opt_dry = opt_force = opt_ldrq1 = 0; opt_dma = 0; opt_gpio = gpio_from = 0; fix_steal = opt_keepsirq = opt_nopirq = 0;
    strcpy(buf, args); argv[0] = "isalpc";
    for (char *t = strtok(buf, " "); t; t = strtok(NULL, " ")) argv[argc++] = t;
    printf("--- isalpc %s\n", args);
    return isalpc_main(argc, argv);
}

int main(void) {
    bios(0x1C00);
    assert(run("") == 0 && g32(0x88) == 0 && pci_r16(0x82) == 0x1405);                    // status only: nothing written
    assert(run("sb16 dry") == 0 && g32(0x88) == 0 && pci_r16(0x82) == 0x1405 && denied(0x220));
    assert(run("sb16") == 0);
    assert(g32(0x84) == 0x000C0291 && g32(0x88) == 0x00040229 && g32(0x8C) == 0x00000331 && g32(0x90) == 0x00000389);
    assert(pci_r16(0x80) == 0x0020 && (pci_r16(0x82) & 3) == 3);                           // 220-227 on the unused COMB decode
    assert((pci_r16(0x82) & 0x2100) == 0x2100 && fwd(0x201) && fwd(0x22F) && !fwd(0x230));
    assert(!denied(0x220) && !denied(0x22F) && denied(0x230) && denied(0x290) && !denied(0x388) && !denied(0x4E) && denied(0x3F8));
    assert(test_dbg.cfg.irq_frames == 0x18EA8);
    assert(run("sb16") == 0 && g32(0x88) == 0x00040229 && g32(0x8C) == 0x00000331);                                    // again: same result

    bios(0x1C00);
    assert(run("orpheus2") == 1 && g32(0x88) == 0);                                         // 4 blocks, 3 free ranges
    assert(run("orpheus2 force ldrq1") == 0);
    for (unsigned p = 0x100; p < 0x10000; p++) if (!denied(p)) assert(fwd(p));            // every port the bridge claims reaches LPC
    assert(fwd(0x279) && fwd(0xA79) && fwd(0x534) && fwd(0x344) && fwd(0x24F) && fwd(0x388) && fwd(0x290) && !fwd(0x1C00) && !fwd(0x620));   // the replaced BIOS range stays forwarded
    assert(!denied(0x279) && !denied(0x534) && !denied(0x53F) && denied(0x540) && denied(0x280) && !(sim_io[0x1C02] & 0x80));

    bios(0x1C00);
    assert(run("awe32") == 1);
    assert(run("awe32 force") == 0);                                                        // 220-22F moves to the COM decodes
    assert((pci_r16(0x80) & 0x77) == 0x23 && (pci_r16(0x82) & 3) == 3);
    assert(fwd(0x220) && fwd(0x22F) && fwd(0x620) && fwd(0xA20) && fwd(0xE23) && fwd(0x330) && fwd(0x388) && !fwd(0x3F8) && !fwd(0x3C0));

    bios(0x1C00);
    assert(run("com1 lpt1 fdc irq=5") == 0 && test_dbg.cfg.irq_frames == ((1u << 16) | (1u << 5)));
    bios(0x1C00);
    assert(run("com1 lpt1 fdc gus=220") == 0);
    assert(test_dbg.cfg.irq_frames == (0x18EA8 | (1u << 4) | (1u << 7) | (1u << 6)));
    assert(fwd(0x3F8) && fwd(0x378) && fwd(0x3F5) && fwd(0x3F7) && fwd(0x726) && fwd(0x32B) && !denied(0x3F8) && !denied(0x3F7) && denied(0x3F6));
    assert((pci_r16(0x82) & 8) && !fwd(0x3B0) && !fwd(0x3A0));                              // floppy on the FDD decode, no stray window

    bios(0x500);                                                                            // 530 sits in the PCH GPIO block here
    assert(run("wss dry") == 0 && pci_r16(0x48) == 0x501 && g32(0x88) == 0);
    assert(run("wss ldrq1") == 0 && pci_r16(0x48) == 0x681 && fwd(0x530) && fwd(0x537) && !denied(0x534) && !fwd(0x680));
    assert(!(sim_io[0x682] & 0x80) && sim_io[0x502] == 0x80);                               // LDRQ1# switch goes to the new place
    assert(run("wss") == 0 && pci_r16(0x48) == 0x681);                                      // again: stays
    bios(0x500);
    assert(run("awe32 wss") == 1 && pci_r16(0x48) == 0x501);                                // does not fit: nothing moved
    assert(run("wss 680-683 gpio=A80") == 0 && pci_r16(0x48) == 0xA81 && fwd(0x680) && fwd(0x530));
    bios(0x500); put32(0x40, 0x401);
    assert(run("400-403") == 1 && pci_r16(0x48) == 0x501);                                  // PMBASE is not moved
    assert(run("nosuch") == 1 && run("irq=8") == 1 && run("80-8F") == 1);

    // the X99M Killer/3.1 after POST (BIOS P4.01): three GEN ranges taken, both COM decodes on, GPIO at 500, SERIRQ quiet
#define X99() do { bios(0x500); put16(0x82, 0x3C03); put32(0x40, 0x401); put32(0x84, 0x003C0281); put32(0x8C, 0x000C0CA3); put32(0x90, 0x001C0601); sim_pci[0x64] = 0x90; sim_io[0x502] = sim_io[0x682] = 0; } while (0)   /* LDRQ1# native */
    X99();
    assert(run("sb16") == 1 && run("sb16 dry force") == 0 && g32(0x88) == 0 && sim_pci[0x64] == 0x90);
    assert(run("sb16 force keepsirq") == 0 && sim_pci[0x64] == 0x90);
    assert(g32(0x84) == 0x003C0281 && g32(0x8C) == 0x000C0CA3 && g32(0x90) != 0x001C0601);  // only GEN4 goes
    assert(fwd(0x22F) && fwd(0x330) && fwd(0x388) && fwd(0x295) && fwd(0x3F8) && fwd(0xCA2) && !fwd(0x600));
    X99();
    assert(run("orpheus2 force ldrq1") == 0 && sim_pci[0x64] == 0xD0 && pci_r16(0x48) == 0x681);
    assert(fwd(0x534) && fwd(0x53F) && fwd(0x344) && fwd(0x279) && fwd(0xA79) && fwd(0x388) && fwd(0x295) && fwd(0x3F8));  // hardware monitor stays reachable
    assert(denied(0x295) && !denied(0x534) && !fwd(0x680) && !fwd(0x400));
    X99();
    assert(run("sb16 force") == 0 && sim_pci[0x60] == 0x0B && sim_pci[0x61] == 0x0A);       // PCI routing untouched by default
    X99();
    sim_io[0x4D0] = 0x20; sim_io[0x4D1] = 0x0C;                                             // ELCR: IRQ 5, 10, 11 level triggered
    assert(run("sb16 force nopirq") == 0 && sim_pci[0x60] == 0x8B && sim_pci[0x61] == 0x8A && sim_pci[0x62] == 0x80);
    assert(sim_io[0x4D0] == 0x20 && sim_io[0x4D1] == 0x00);                                 // 10 and 11 back to edge, 5 was not PCI routed
    X99();
    assert(run("awe32 force") == 0 && fwd(0x620) && fwd(0xA20) && fwd(0xE20) && fwd(0x220) && fwd(0x388));
    assert(!fwd(0x295) && !fwd(0x3F8));                                                     // five blocks: costs the hardware monitor range and both COM decodes
    puts("isalpc ok");
    return 0;
}
