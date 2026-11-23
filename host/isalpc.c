/* isalpc: forward ISA card ports from an Intel PCH (D31:F0) to LPC and set up the ISA-LPC bridge for them. DOS and Linux.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Written from scratch. The idea and the DMA register list come from sapphisa (rasteri, dISAppointment) and lpcexp.
 *   DOS:    wcl -bt=dos -ms -3 -fe=ISALPC.EXE isalpc.c
 *   Linux:  cc -O2 -o isalpc isalpc.c        (run as root)
 *   test:   make test                         (fake PCH + the firmware's config port)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;                      /* 32 bits or more */

/* ---- platform: PCI config bytes of bus 0 device 31 function 0, I/O port bytes -------------------------------------- */
#if defined(ISALPC_SIM)
u8 sim_pci[256], sim_io[65536];
void cfgport_write(u16 a, u8 d);
u8 cfgport_read(u16 a);
static int plat_open(void) { return 0; }
static u8 pci_r8(u8 r) { return sim_pci[r]; }
static void pci_w8(u8 r, u8 v) { sim_pci[r] = v; }
static u8 io_r8(u16 p) { return (p & 0xFFFE) == 0x4E ? cfgport_read(p) : sim_io[p]; }
static void io_w8(u16 p, u8 v) { if ((p & 0xFFFE) == 0x4E) cfgport_write(p, v); else sim_io[p] = v; }
#define DMA_DEFAULT 0
#elif defined(__WATCOMC__)
#include <conio.h>
static void out32(u16 port, u32 v) {
    _asm {
        .386
        mov dx, port
        mov eax, v
        out dx, eax
    }
}
static int plat_open(void) { return 0; }
static u8 pci_r8(u8 r) { out32(0xCF8, 0x8000F800UL | (r & 0xFC)); return (u8)inp(0xCFC + (r & 3)); }
static void pci_w8(u8 r, u8 v) { out32(0xCF8, 0x8000F800UL | (r & 0xFC)); outp(0xCFC + (r & 3), v); }
static u8 io_r8(u16 p) { return (u8)inp(p); }
static void io_w8(u16 p, u8 v) { outp(p, v); }
#define DMA_DEFAULT 1                           /* DOS: the BIOS leaves the 8237s unprogrammed (sapphisa) */
#else
#include <fcntl.h>
#include <unistd.h>
static int fd_pci = -1, fd_io = -1;
static int plat_open(void) {
    const char *p = getenv("ISALPC_PCI"), *q = getenv("ISALPC_PORT");
    fd_pci = open(p ? p : "/sys/bus/pci/devices/0000:00:1f.0/config", O_RDWR);
    fd_io = open(q ? q : "/dev/port", O_RDWR);
    if (fd_pci < 0 || fd_io < 0) { perror("isalpc: PCI config or /dev/port (root?)"); return 1; }
    return 0;
}
static u8 pci_r8(u8 r) { u8 v = 0xFF; if (pread(fd_pci, &v, 1, r) != 1) v = 0xFF; return v; }
static void pci_w8(u8 r, u8 v) { if (pwrite(fd_pci, &v, 1, r) != 1) perror("isalpc: pci write"); }
static u8 io_r8(u16 p) { u8 v = 0xFF; if (pread(fd_io, &v, 1, p) != 1) v = 0xFF; return v; }
static void io_w8(u16 p, u8 v) { if (pwrite(fd_io, &v, 1, p) != 1) perror("isalpc: port write"); }
#define DMA_DEFAULT 0
#endif

static u16 pci_r16(u8 r) { return (u16)(pci_r8(r) | ((u16)pci_r8((u8)(r + 1)) << 8)); }
static u32 pci_r32(u8 r) { return (u32)pci_r16(r) | ((u32)pci_r16((u8)(r + 2)) << 16); }
static void pci_w16(u8 r, u16 v) { pci_w8(r, (u8)v); pci_w8((u8)(r + 1), (u8)(v >> 8)); }

/* ---- presets: "+lo-hi" is relative to the base (name=base on the command line), "@name" pulls another preset -------- */
struct preset { const char *name; u16 base; const char *spec; const char *irq; const char *what; u16 wlo, whi; };   /* wlo/whi: word read ports, base + offset (0/0 = none) */
static const struct preset presets[] = {
    {"adlib",    0,     "388-38B", "", "AdLib, OPL2/OPL3", 0, 0},
    {"sb",       0x220, "+0-F 388-38B 200-207", "", "Sound Blaster 1.x/2.0/Pro", 0, 0},
    {"sb16",     0x220, "+0-F 330-331 388-38B 200-207", "", "Sound Blaster 16", 0, 0},
    {"awe32",    0x220, "+0-F +400-403 +800-803 +C00-C03 330-331 388-38B 200-207", "", "AWE32/AWE64 (EMU8000 at base+400/800/C00)", 0x400, 0xC03},
    {"gus",      0x240, "+0-F +100-10B +506-506", "", "Gravis UltraSound (ICS mixer at base+506)", 0x104, 0x104},
    {"gusmax",   0x240, "+0-F +100-10F +506-506", "", "UltraSound MAX (codec at base+10C)", 0x104, 0x104},
    {"guspnp",   0x240, "+0-F +100-10F 388-38B 330-331 200-207 @pnp", "", "UltraSound PnP, InterWave", 0x104, 0x104},
    {"orpheus2", 0x240, "220-22F 530-53F 388-38B 330-331 200-207 +0-F +100-10F @pnp", "", "Orpheus II (CS4237B + InterWave + PCMIDI at 330)", 0x104, 0x104},
    {"cs423x",   0x530, "+0-7 120-127 220-22F 388-38B 330-331 200-207 @pnp", "", "Crystal CS4232/4236/4237 (control port 120)", 0, 0},
    {"wss",      0x530, "+0-7 388-38B", "", "Windows Sound System, AD1848/CS4231", 0, 0},
    {"opl3sa",   0x220, "+0-F 530-537 388-38B 330-331 370-371 200-207 @pnp", "", "Yamaha OPL3-SA2/SA3 (YMF711/715/719)", 0, 0},
    {"ess",      0x220, "+0-F 388-38B 330-331 200-207 @pnp", "", "ESS ES688/1688/1868/1869", 0, 0},
    {"mpu401",   0x330, "+0-1", "", "MPU-401, Roland SCC-1", 0, 0},
    {"game",     0x200, "+0-7", "", "game port", 0, 0},
    {"pnp",      0,     "279-279 A79-A79 203-277", "", "ISA PnP ports (read port 203-277)", 0, 0},
    {"com1",     0x3F8, "+0-7", "4", "serial port card", 0, 0},
    {"com2",     0x2F8, "+0-7", "3", "serial port card", 0, 0},
    {"com3",     0x3E8, "+0-7", "4", "serial port card", 0, 0},
    {"com4",     0x2E8, "+0-7", "3", "serial port card", 0, 0},
    {"lpt1",     0x378, "+0-7", "7", "parallel port card", 0, 0},
    {"lpt2",     0x278, "+0-7", "5", "parallel port card", 0, 0},
    {"lpt3",     0x3BC, "+0-3", "7", "parallel port card (MDA address)", 0, 0},
    {"fdc",      0x3F0, "+0-5 +7-7", "6", "floppy controller (DMA 2)", 0, 0},
    {"fdc2",     0x370, "+0-5 +7-7", "6", "floppy controller, secondary address", 0, 0},
    {"ide1",     0x1F0, "+0-7 +206-206", "14", "IDE interface (onboard SATA must not be in legacy mode)", 0x0, 0x1},
    {"ide2",     0x170, "+0-7 +206-206", "15", "IDE interface, secondary", 0x0, 0x1},
    {"ne2000",   0x300, "+0-1F", "", "NE2000 network card", 0x10, 0x11},
    {"aha1542",  0x330, "+0-3", "11", "Adaptec AHA-154x SCSI", 0, 0},
    {"aha152x",  0x340, "+0-1F", "11", "Adaptec AHA-152x SCSI, SB16 SCSI-2", 0x16, 0x17},
};
#define NPRESET (sizeof presets / sizeof presets[0])

/* ---- state ------------------------------------------------------------------------------------------------------------ */
static u8 allow[8192];                          /* ports the cards use (bridge: everything else is denied) */
static u8 need[2048], need0[2048];              /* DWords (port >> 2) still to forward / as requested */
static u32 irq_add, irq_set;
static int have_irq_set, have_ports, opt_dry, opt_force, opt_dma = DMA_DEFAULT, opt_ldrq1;
static u16 opt_gpio, gpio_from;
static u16 iod, en, pmbase, gpiobase;
static u32 pciirq;
static u32 gen[4], gen0[4];
static u32 word_rd[3];                         /* firmware word_rd: first | last << 16 */
static int nword;                     /* planned / as the BIOS left them */
static u8 gen_kept[4];
static int fix_steal, opt_keepsirq, opt_nopirq, opt_save, opt_forget;
static const u16 comr[8] = {0x3F8, 0x2F8, 0x220, 0x228, 0x238, 0x2E8, 0x338, 0x3E8};

static int getbit(const u8 *m, u32 i) { return (m[i >> 3] >> (i & 7)) & 1; }
static void setbit(u8 *m, u32 i, int v) { if (v) m[i >> 3] |= (u8)(1 << (i & 7)); else m[i >> 3] &= (u8)~(1 << (i & 7)); }

static int add_range(u32 lo, u32 hi) {
    u32 p;
    if (lo > hi || hi > 0xFFFF) { printf("bad range %lX-%lX\n", lo, hi); return 1; }
    if (lo < 0x100) { printf("%lX-%lX: ports below 100 belong to the chipset\n", lo, hi); return 1; }
    for (p = lo; p <= hi; p++) { setbit(allow, p, 1); setbit(need, p >> 2, 1); }
    have_ports = 1;
    return 0;
}

static int add_irqs(const char *s, u32 *mask) {
    while (*s) {
        char *e;
        u32 n = strtoul(s, &e, 10);
        if (e == s || n < 2 || n > 15 || n == 8 || n == 13) { printf("bad IRQ list '%s'\n", s); return 1; }
        *mask |= 1UL << (n == 2 ? 9 : n);
        s = *e == ',' ? e + 1 : e;
    }
    return 0;
}

static int add_token(const char *t, u16 base, int depth);

static int add_preset(const char *name, int len, u16 base, int depth) {
    unsigned i;
    char tok[40];
    const char *s;
    for (i = 0; i < NPRESET; i++) if ((int)strlen(presets[i].name) == len && !strncmp(presets[i].name, name, len)) break;
    if (i == NPRESET || depth > 3) return -1;
    if (!base) base = presets[i].base;
    if (presets[i].whi) {
        u32 r = (u32)(base + presets[i].wlo) | ((u32)(base + presets[i].whi) << 16);
        int k;
        for (k = 0; k < nword && word_rd[k] != r; k++) ;
        if (k == nword) {
            if (nword == 3) { printf("more than 3 word port ranges (%s)\n", presets[i].name); return 1; }
            word_rd[nword++] = r;
        }
    }
    for (s = presets[i].spec; *s; ) {
        int n = 0;
        while (*s == ' ') s++;
        while (*s && *s != ' ' && n < 39) tok[n++] = *s++;
        tok[n] = 0;
        if (n && add_token(tok, base, depth + 1)) return 1;
    }
    return add_irqs(presets[i].irq, &irq_add);
}

static int add_token(const char *t, u16 base, int depth) {
    char *e;
    u32 lo, hi;
    const char *eq = strchr(t, '=');
    int r;
    if (*t == '@') { r = add_preset(t + 1, (int)strlen(t + 1), 0, depth); return r < 0 ? (printf("unknown preset %s\n", t), 1) : r; }
    if (!strncmp(t, "irq=", 4)) { have_irq_set = 1; return add_irqs(t + 4, &irq_set); }
    r = add_preset(t, eq ? (int)(eq - t) : (int)strlen(t), eq ? (u16)strtoul(eq + 1, NULL, 16) : 0, depth);
    if (r >= 0) return r;
    lo = strtoul(*t == '+' ? t + 1 : t, &e, 16);
    hi = lo;
    if (e == t || (*t == '+' && e == t + 1)) { printf("unknown preset or range '%s' (try: isalpc list)\n", t); return 1; }
    if (*e == '-') hi = strtoul(e + 1, &e, 16);
    if (*e) { printf("bad range '%s'\n", t); return 1; }
    if (*t == '+') { lo += base; hi += base; }
    return add_range(lo, hi);
}

/* ---- decode logic ----------------------------------------------------------------------------------------------------- */
static int fixed_covers(u16 d, u16 e, u16 p) {              /* LPC_I/O_DEC, LPC_EN: is port p forwarded by a fixed decode */
    u16 lpt = (d >> 8) & 3;
    if ((e & 1) && (p & 0xFFF8) == comr[d & 7]) return 1;
    if ((e & 2) && (p & 0xFFF8) == comr[(d >> 4) & 7]) return 1;
    if ((e & 4) && lpt == 0 && (p & 0xFBF8) == 0x378) return 1;
    if ((e & 4) && lpt == 2 && (p & 0xFBFC) == 0x3BC && (p & 3) != 3) return 1;    /* 278 is left out: the PCH makes 279 read only, PnP writes it */
    if ((e & 8) && (p & 0xFFF8) == ((d & 0x1000) ? 0x370 : 0x3F0) && (p & 7) != 6) return 1;   /* FDD: 3F0-3F5, 3F7 (PCH LPC_I/O_DEC) */
    if ((e & 0x100) && (p & 0xFFF8) == 0x200) return 1;
    if ((e & 0x200) && (p & 0xFFF8) == 0x208) return 1;
    return 0;
}
static int dw_fixed(u16 d, u16 e, u16 p) {                  /* every requested port of the DWord at p */
    u16 i;
    for (i = 0; i < 4; i++) if (getbit(allow, p + i) && !fixed_covers(d, e, (u16)(p + i))) return 0;
    return 1;
}
static int gen_covers(u32 g, u16 p) {
    u16 mask = (u16)((g >> 16) & 0xFC) | 3;
    return (g & 1) && ((p ^ (u16)g) & 0xFFFC & ~mask) == 0;
}
static int forbidden(u16 p) {                               /* never forward by accident */
    if ((p >= 0x3B0 && p <= 0x3BB) || (p >= 0x3C0 && p <= 0x3DF)) return 1;   /* VGA */
    if (p >= 0xCF8 && p <= 0xCFF) return 1;
    if (pmbase && p >= pmbase && p < pmbase + 0x80) return 1;
    if (gpiobase && p >= gpiobase && p < gpiobase + 0x80) return 1;
    return p < 0x100;
}

struct win { u16 base; u8 mask; };
static struct win w[64];
static int nw;

static long win_cost(u16 base, u8 mask) {                   /* ports covered, plus a penalty for forbidden ones nobody asked for */
    long cost = 0;
    u16 d;
    for (d = 0; d < 0x100; d += 4) {
        u16 p = (base & 0xFF00) | d;
        if ((p ^ base) & 0xFC & ~mask) continue;
        cost += 4;
        if (forbidden(p) && !getbit(need0, p >> 2)) cost += 1024;
    }
    return cost;
}

static int pages_needed(void) {
    u16 pg;
    int n = 0;
    for (pg = 1; pg < 0x100; pg++) if (memcmp(need + pg * 8, "\0\0\0\0\0\0\0\0", 8)) n++;
    return n;
}

/* make port p reach LPC through a fixed decode (COMA/COMB, LPT, FDD). Decodes that are off are free to use; one the BIOS
 * enabled is taken only with steal + 'force', and never when its current range is one we asked for. *used: taken in this pass. */
static int fixed_try(u16 p, u16 *pd, u16 *pe, u16 *used, int steal) {
    static const u16 lptr[3] = {0x378, 0x278, 0x3BC};
    u16 d = *pd, e = *pe, k, slot, cur, bit;
    steal = steal && fix_steal;
    for (k = 0; k < 8; k++) if (comr[k] == (p & 0xFFF8)) break;
    if (k < 8) {
        for (slot = 0; slot < 2; slot++) {
            cur = comr[(d >> (slot * 4)) & 7];
            if (*used & (1 << slot)) continue;
            if ((e & (1 << slot)) && (!steal || getbit(need0, cur >> 2) || getbit(need0, (cur >> 2) + 1))) continue;
            *pd = (u16)((d & ~(7 << (slot * 4))) | (k << (slot * 4))); *pe = (u16)(e | (1 << slot)); *used |= (u16)(1 << slot);
            return 1;
        }
        return 0;
    }
    if ((p & 0xFFF8) == 0x378 || (p & 0xFFFC) == 0x3BC) { bit = 4; cur = lptr[(d >> 8) & 3]; d = (u16)((d & ~0x300) | ((p & 0xFFF8) == 0x378 ? 0 : 0x200)); }
    else if ((p & 0xFFF8) == 0x3F0 || (p & 0xFFF8) == 0x370) { bit = 8; cur = (d & 0x1000) ? 0x370 : 0x3F0; d = (u16)((d & ~0x1000) | ((p & 0xFFF8) == 0x370 ? 0x1000 : 0)); }
    else return 0;
    if ((*used & bit) || ((e & bit) && (!steal || getbit(need0, cur >> 2))) || !dw_fixed(d, (u16)(e | bit), p)) return 0;
    *pd = d; *pe = (u16)(e | bit); *used |= bit;
    return 1;
}

/* move every DWord of a page that is still needed to fixed decodes; 1 = the page needs no GEN window any more */
static int page_to_fixed(u16 pg, u16 *pd, u16 *pe) {
    u16 d = *pd, e = *pe, i, p, used = 0;
    for (i = 0; i < 64; i++) {
        p = (u16)((pg << 8) | (i << 2));
        if (getbit(need, p >> 2) && !dw_fixed(d, e, p) && !fixed_try(p, &d, &e, &used, 1)) return 0;
    }
    for (i = 0; i < 64; i++) setbit(need, ((u32)pg << 6) | i, 0);
    *pd = d; *pe = e;
    return 1;
}

static int print_ports(u16 base, u8 mask) {                 /* returns 1 when the window also covers VGA or chipset ports */
    u16 d, start = 0;
    int open = 0, bad = 0;
    for (d = 0; d <= 0x100; d += 4) {
        int in = d < 0x100 && !((d ^ base) & 0xFC & ~mask);
        if (in && forbidden((u16)((base & 0xFF00) | d))) bad = 1;
        if (in && !open) { start = d; open = 1; }
        if (!in && open) { printf(" %X-%X", (base & 0xFF00) | start, (base & 0xFF00) | (d - 1)); open = 0; }
    }
    return bad;
}

/* returns 0 and fills gen[], iod, en with the new values, or 1 when the request does not fit.
 * nsteal: how many ranges the BIOS set may be replaced (highest first); fix_steal: also its COM/LPT/FDD decodes. */
static int plan(int nsteal, int final) {
    u16 nd = iod, ne = en, pg, p, used = 0;
    int nfree = 0, i, j, k = nsteal;
    u32 d;
    memcpy(need, need0, sizeof need);
    for (i = 3; i >= 0; i--) {
        gen_kept[i] = (u8)(gen0[i] & 1);
        if (gen_kept[i] && k) { gen_kept[i] = 0; k--; }
        if (!gen_kept[i]) nfree++;
    }
    if (getbit(need, 0x200 >> 2) || getbit(need, 0x204 >> 2)) ne |= 0x100;
    if (getbit(need, 0x208 >> 2) || getbit(need, 0x20C >> 2)) ne |= 0x200;
    for (d = 0x40; d < 0x4000; d++) {
        if (!getbit(need, d)) continue;
        p = (u16)(d << 2);
        if (dw_fixed(nd, ne, p) || fixed_try(p, &nd, &ne, &used, 0)) setbit(need, d, 0);
        for (i = 0; i < 4; i++) if (gen_kept[i] && gen_covers(gen0[i], p)) setbit(need, d, 0);
    }
    for (pg = 1; pg < 0x100 && pages_needed() > nfree; pg++)
        if (memcmp(need + pg * 8, "\0\0\0\0\0\0\0\0", 8)) page_to_fixed(pg, &nd, &ne);
    if (pages_needed() > nfree) {
        if (!final) return 1;
        printf("Does not fit: %d port blocks (xx00-xxFF) left, %d free GEN decode ranges%s.\nBlocks:", pages_needed(), nfree,
               opt_force ? "" : " (add 'force' to take the ones the BIOS set)");
        for (pg = 1; pg < 0x100; pg++) if (memcmp(need + pg * 8, "\0\0\0\0\0\0\0\0", 8)) printf(" %X00", pg);
        printf("\n");
        return 1;
    }
    nw = 0;                                                 /* exact aligned blocks first, then merge inside a page until they fit */
    for (pg = 1; pg < 0x100; pg++) {
        u8 done[8];
        u16 size, s, k;
        if (!memcmp(need + pg * 8, "\0\0\0\0\0\0\0\0", 8)) continue;
        memset(done, 0, sizeof done);
        for (size = 64; size; size >>= 1) for (s = 0; s < 64; s += size) {
            for (k = 0; k < size; k++) if (!getbit(need, ((u32)pg << 6) + s + k) || getbit(done, s + k)) break;
            if (k < size) continue;
            if (nw == 64) { printf("too many separate ranges\n"); return 1; }
            w[nw].base = (u16)((pg << 8) | (s << 2)); w[nw].mask = (u8)((size - 1) << 2); nw++;
            for (k = 0; k < size; k++) setbit(done, s + k, 1);
        }
    }
    while (nw > nfree) {
        int bi = -1, bj = -1;
        long best = 0x7FFFFFFFL;
        for (i = 0; i < nw; i++) for (j = i + 1; j < nw; j++) {
            u8 m;
            long c;
            if ((w[i].base ^ w[j].base) & 0xFF00) continue;
            m = (u8)(w[i].mask | w[j].mask | ((w[i].base ^ w[j].base) & 0xFC));
            c = win_cost(w[i].base, m) - win_cost(w[i].base, w[i].mask) - win_cost(w[j].base, w[j].mask);
            if (c < best) { best = c; bi = i; bj = j; }
        }
        w[bi].mask = (u8)(w[bi].mask | w[bj].mask | ((w[bi].base ^ w[bj].base) & 0xFC));
        w[bj] = w[--nw];
    }
    for (i = 0; i < 4; i++) {                               /* a replaced BIOS range stays forwarded where it costs nothing */
        u16 ob = (u16)gen0[i] & 0xFFFC;
        u8 om = (u8)((gen0[i] >> 16) & 0xFC);
        if (gen_kept[i] || !(gen0[i] & 1)) continue;
        for (j = 0; j < nw; j++) if (!((w[j].base ^ ob) & 0xFF00)) break;
        if (j < nw) w[j].mask = (u8)(w[j].mask | om | ((w[j].base ^ ob) & 0xFC));
        else if (nw < nfree) { w[nw].base = ob; w[nw].mask = om; nw++; }
    }
    for (i = 0, j = 0; i < 4; i++) {
        if (gen_kept[i]) { gen[i] = gen0[i]; continue; }
        gen[i] = j < nw ? ((u32)w[j].mask << 16) | w[j].base | 1 : 0;
        j++;
    }
    iod = nd; en = ne;
    return 0;
}

/* ---- the ISA-LPC bridge behind 4E/4F (fw/src/cfgport.c) ---------------------------------------------------------------- */
static void br_seek(u16 off) { io_w8(0x4E, 'I'); io_w8(0x4E, 'S'); io_w8(0x4E, 'A'); io_w8(0x4E, (u8)off); io_w8(0x4E, (u8)(off >> 8)); }
static u32 br_r32(u16 off) { u32 v; br_seek(off); v = io_r8(0x4F); v |= (u32)io_r8(0x4F) << 8; v |= (u32)io_r8(0x4F) << 16; return v | ((u32)io_r8(0x4F) << 24); }

/* config port command at offset F000: 'W' save the bridge config to its flash, 'X' forget it. Status: 0 ok, 1 busy, E0 error. */
static int br_cmd(u8 c) {
    long n;
    u8 st = 1;
    br_seek(0xF000); io_w8(0x4F, c);
    for (n = 0; n < 20000L && st == 1; n++) { br_seek(0xF000); st = io_r8(0x4F); }
    return st;
}

static void bridge(void) {
    u32 i, frames, nf;
    if (br_r32(0) != 0x31415349UL) {
        printf("Bridge: no answer on 4E/4F (not powered, firmware before build 11, or its config port is off).%s\n",
               opt_nopirq ? " 'nopirq' not applied." : "");
        return;
    }
    printf("Bridge: firmware build %lu, %lu MHz.", br_r32(4), br_r32(0x20) / 1000);
    br_seek(0xF001);
    i = io_r8(0x4F);
    if (i != 0xFF) printf(" Saved config in flash: %s.", i ? "yes" : "no");   /* 0xFF: firmware without the save command */
    frames = br_r32(0x2C);
    if (have_ports && !opt_dry) {
        setbit(allow, 0x4E, 1); setbit(allow, 0x4F, 1);
        br_seek(0x40);                                      /* deny[]: claim only the ports of the selected cards */
        for (i = 0; i < 8192; i++) io_w8(0x4F, (u8)~allow[i]);
        nf = have_irq_set ? (irq_set | 0x10000UL) : (frames | irq_add);
        br_seek(0x2C);
        for (i = 0; i < 4; i++) io_w8(0x4F, (u8)(nf >> (8 * i)));
        frames = nf;
        br_seek(0x34);                                      /* word_rd[]: 16 bit data ports of the selected cards */
        for (i = 0; i < 12; i++) io_w8(0x4F, (u8)(word_rd[i >> 2] >> (8 * (i & 3))));
        printf(" Port filter, IRQ list and word ports written.");
        if (opt_nopirq) {                                   /* PCI interrupt routing off for the IRQs the bridge serves */
            static const u8 pirq[8] = {0x60, 0x61, 0x62, 0x63, 0x68, 0x69, 0x6A, 0x6B};
            for (i = 0; i < 8; i++) {
                u8 v = pci_r8(pirq[i]);
                u16 n = v & 15, port = n < 8 ? 0x4D0 : 0x4D1;
                if ((v & 0x80) || !((nf >> n) & 1)) continue;
                pci_w8(pirq[i], (u8)(v | 0x80));
                io_w8(port, (u8)(io_r8(port) & ~(1 << (n & 7))));        /* ELCR: back to edge triggered, as an ISA card expects */
                pciirq &= ~(1UL << n);
                printf("\nPIRQ%c routing to IRQ %u switched off.", (int)(i < 4 ? 'A' + i : 'E' + i - 4), n);
            }
        }
    }
    if ((opt_save || opt_forget) && !opt_dry) {
        i = br_cmd((u8)(opt_forget ? 'X' : 'W'));
        printf("\n%s: %s.", opt_forget ? "Saved config forgotten" : "Config saved to flash", i == 0 ? "done" : i == 1 ? "no answer, still busy" : "FAILED (flash error or firmware without the command)");
    }
    printf("\nBridge IRQs:");
    for (i = 3; i < 16; i++) if ((frames >> i) & 1) printf(" %lu%s", i, ((pciirq >> i) & 1) ? "(PCI too!)" : "");
    printf("\n");
    io_w8(0x4E, 0);                                         /* lock */
}

/* ---- chipset odds and ends -------------------------------------------------------------------------------------------- */
static void dma_reset(void) {                               /* register list as in sapphisa / lpcexp: the BIOS leaves the 8237s unprogrammed */
    static const u8 zero[] = {0x00, 0x01, 0x04, 0x05, 0x06, 0x07, 0x08, 0x82, 0x87, 0x89, 0x8A, 0x8B, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5,
                              0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1};
    unsigned i;
    for (i = 0; i < sizeof zero; i++) io_w8(zero[i], 0);
    io_w8(0xDE, 0x0E); io_w8(0xDF, 0x0E);
    printf("DMA controllers reset.\n");
}

static void status(void) {
    u8 sirq = pci_r8(0x64), gc = pci_r8(0x4C);
    u32 f = 0;
    int i;
    static const u8 pirq[8] = {0x60, 0x61, 0x62, 0x63, 0x68, 0x69, 0x6A, 0x6B};
    printf("LPC_I/O_DEC %04X  LPC_EN %04X  COMA %X%s COMB %X%s  PMBASE %X  GPIOBASE %X\n", iod, en,
           comr[iod & 7], (en & 1) ? "" : "(off)", comr[(iod >> 4) & 7], (en & 2) ? "" : "(off)", pmbase, gpiobase);
    for (i = 0; i < 4; i++) {
        printf("GEN%d %08lX", i + 1, gen[i]);
        if ((gen[i] & 1) && print_ports((u16)gen[i] & 0xFFFC, (u8)((gen[i] >> 16) & 0xFC)))
            printf("  (covers VGA or chipset ports: fine with the graphics card in a CPU slot)");
        printf("%s\n", (gen[i] & 1) ? "" : "  free");
    }
    for (i = 0; i < 8; i++) { u8 v = pci_r8(pirq[i]); if (!(v & 0x80)) pciirq |= 1UL << (v & 15); }
    printf("PCI devices use IRQ:");
    for (i = 3; i < 16; i++) if ((pciirq >> i) & 1) printf(" %d", i);
    printf("%s\n", pciirq ? " (not usable by ISA cards)" : " none");
    if ((sirq & 0xC0) != 0xC0 && have_ports && !opt_dry && !opt_keepsirq) {   /* quiet mode needs the bridge to start each cycle */
        pci_w8(0x64, (u8)(sirq | 0xC0));
        printf("SERIRQ was %s: set to continuous mode.\n", (sirq & 0x80) ? "in quiet mode" : "off");
    } else printf("SERIRQ %s.\n", !(sirq & 0x80) ? "off" : (sirq & 0x40) ? "on, continuous mode" : "on, quiet mode");
    if ((gc & 0x10) && gpiobase) {
        u16 gpiobase = (u16)(pci_r32(0x48) & 0xFF80);       /* where the block is now (a dry run plans with the new place) */
        f = io_r8(gpiobase + 2) & 0x80;                     /* GPIO_USE_SEL bit 23 */
        if (f && opt_ldrq1 && !opt_dry) {
            if (gc & 1) printf("LDRQ1# pin is GPIO23 and the GPIO registers are locked.\n");
            else { io_w8(gpiobase + 2, (u8)(io_r8(gpiobase + 2) & ~0x80)); printf("LDRQ1# pin switched from GPIO23 to LDRQ1#.\n"); }
        } else printf("LDRQ1# pin is %s.\n", f ? "GPIO23: DMA through LDRQ1# needs 'ldrq1' (LDRQ0# is not affected)" : "LDRQ1#");
    }
}

static void usage(void) {
    unsigned i;
    printf("isalpc [preset[=base] | lo-hi | irq=n,n ...] [force] [dry] [dma|nodma] [ldrq1]\n"
           "  no arguments: show the chipset and bridge state\n"
           "  force: replace decode ranges the BIOS set (as few as needed)    dry: change nothing\n"
           "  keepsirq: leave the Serial IRQ mode alone (default: continuous)\n"
           "  nopirq: stop routing PCI interrupts (PIRQ A-H) to IRQs the bridge serves\n"
           "  save: keep the bridge settings in its flash, so it answers right after power-up (the PCH windows still need ISALPC at every boot)\n");
    printf("  forget: erase the saved bridge settings\n");
    printf("  irq=:  IRQs the bridge serves (default: its list plus those of the presets)\n"
           "  dma:   reset the DMA controllers (DOS default)    ldrq1: switch the GPIO23 pin to LDRQ1#\n"
           "  gpio=: where the PCH GPIO registers go when a card needs their ports (default 680)\nPresets (hex ports):\n");
    for (i = 0; i < NPRESET; i++) printf("  %-9s %-3X %s\n", presets[i].name, presets[i].base, presets[i].what);
}

int main(int argc, char **argv) {
    int i, bad = 0;
    for (i = 1; i < argc; i++) {
        char *a = argv[i], *c;
        for (c = a; *c; c++) *c = (char)tolower((unsigned char)*c);   /* DOS users type ISALPC SB16 */
        if (!strcmp(a, "list") || !strcmp(a, "-h") || !strcmp(a, "/?")) { usage(); return 0; }
        else if (!strcmp(a, "force")) opt_force = 1;
        else if (!strcmp(a, "dry")) opt_dry = 1;
        else if (!strcmp(a, "dma")) opt_dma = 1;
        else if (!strcmp(a, "nodma")) opt_dma = 0;
        else if (!strcmp(a, "ldrq1")) opt_ldrq1 = 1;
        else if (!strcmp(a, "keepsirq")) opt_keepsirq = 1;
        else if (!strcmp(a, "nopirq")) opt_nopirq = 1;
        else if (!strcmp(a, "save")) opt_save = 1;
        else if (!strcmp(a, "forget")) opt_forget = 1;
        else if (!strncmp(a, "gpio=", 5)) opt_gpio = (u16)(strtoul(a + 5, NULL, 16) & 0xFF80);
        else bad |= add_token(a, 0, 0);
    }
    if (bad || plat_open()) return 1;
    if (pci_r16(0) != 0x8086 || pci_r8(0x0B) != 0x06 || pci_r8(0x0A) != 0x01) {
        printf("No Intel LPC bridge at 00:1F.0 (vendor %04X).\n", pci_r16(0));
        return 1;
    }
    printf("Intel LPC bridge %04X.\n", pci_r16(2));
    iod = pci_r16(0x80); en = pci_r16(0x82);
    pmbase = (u16)(pci_r32(0x40) & 0xFF80); gpiobase = (u16)(pci_r32(0x48) & 0xFF80);
    for (i = 0; i < 4; i++) gen[i] = gen0[i] = pci_r32((u8)(0x84 + 4 * i));
    if (have_ports && (pci_r8(0x4C) & 0x10) && gpiobase) {
        /* A card wants ports inside the PCH GPIO block (WSS at 530 with the block at 500): move the block. The pins keep their
         * setup. Only boot code has the address built in; an ACPI system expects the old place, so this is for DOS. */
        static const u16 cand[3] = {0x680, 0x900, 0x980};
        u32 p, q;
        u16 to = 0;
        for (p = gpiobase; p < (u32)gpiobase + 0x80 && !getbit(allow, p); p++) ;
        for (i = -1; p < (u32)gpiobase + 0x80 && !to && i < 3; i++) {
            u16 c = i < 0 ? opt_gpio : cand[i];
            for (q = c; c && q < (u32)c + 0x80 && !getbit(allow, q); q++) ;
            if (c && q == (u32)c + 0x80 && c != pmbase && c != gpiobase) to = c;
        }
        if (to) { gpio_from = gpiobase; gpiobase = to; }   /* plan with the new place, write it once the plan fits */
    }
    if (have_ports) {
        u32 p;
        u16 iod0 = iod, en0 = en;
        for (p = 0x100; p <= 0xFFFF; p++)
            if (getbit(allow, p) && (forbidden((u16)p))) { printf("Port %lX is used by the chipset or VGA: a card there cannot work.\n", p); bad = 1; p |= 0x7F; }
        if (bad) return 1;
        for (p = 0x100; p <= 0xFFFF; p++)                   /* the Super I/O behind a BIOS decode answers the same cycle */
            if (getbit(allow, p) && fixed_covers(iod0, en0, (u16)p)) {
                printf("Warning: %lX-%lX is forwarded by the BIOS to the onboard COM/LPT/floppy port: disable that port in the BIOS setup, or both answer.\n", p & ~7UL, p | 7);
                p |= 7;
            }
        memcpy(need0, need, sizeof need);
        {   /* BIOS ranges are replaced only with 'force', as few as needed, GEN4 first; its COM/LPT/FDD decodes last */
            int nb = 0, n, r;
            for (i = 0; i < 4; i++) nb += (int)(gen0[i] & 1);
            r = plan(0, !opt_force);
            for (n = 1; r && opt_force && n <= nb; n++) r = plan(n, 0);
            if (r && opt_force) { fix_steal = 1; r = plan(nb, 1); }
            if (r) return 1;
        }
        for (i = 0; i < 4; i++) {
            int j, still = 0;
            if (!(gen0[i] & 1) || gen[i] == gen0[i]) continue;
            for (j = 0; j < 4; j++) if (gen_covers(gen[j], (u16)gen0[i] & 0xFFFC)) still = 1;
            printf("BIOS range GEN%d", i + 1); print_ports((u16)gen0[i] & 0xFFFC, (u8)((gen0[i] >> 16) & 0xFC));
            printf(still ? " replaced, still forwarded.\n" : " replaced: no longer forwarded.\n");
        }
        for (i = 0; i < 2; i++)
            if ((en0 & (1 << i)) && ((iod0 ^ iod) >> (4 * i) & 7))
                printf("BIOS decode COM%c %X replaced: an onboard port there stops working.\n", 'A' + i, comr[(iod0 >> (4 * i)) & 7]);
        if (((en0 & 4) && ((iod0 ^ iod) & 0x300)) || ((en0 & 8) && ((iod0 ^ iod) & 0x1000))) printf("BIOS LPT or floppy decode replaced.\n");
        en |= 0x2000;                                       /* 4E/4F: the bridge's config port */
        if (gpio_from) printf("PCH GPIO registers %s from %X to %X (for DOS; ACPI systems expect %X).\n", opt_dry ? "would move" : "moved", gpio_from, gpiobase, gpio_from);
        if (!opt_dry) {
            if (gpio_from) pci_w16(0x48, (u16)(gpiobase | 1));   /* bit 0 reads 1: I/O space */
            pci_w16(0x80, iod); pci_w16(0x82, en);
            for (i = 0; i < 4; i++) {
                u8 r = (u8)(0x84 + 4 * i);
                if (gen_kept[i]) continue;
                pci_w8(r, 0); pci_w8((u8)(r + 3), 0); pci_w8((u8)(r + 2), (u8)(gen[i] >> 16)); pci_w8((u8)(r + 1), (u8)(gen[i] >> 8)); pci_w8(r, (u8)gen[i]);
            }
        }
        printf(opt_dry ? "Dry run, nothing written. Planned:\n" : "Decode ranges written:\n");
    }
    status();
    if (!have_ports && (opt_save || opt_forget) && !opt_dry && !(pci_r16(0x82) & 0x2000)) pci_w16(0x82, (u16)(pci_r16(0x82) | 0x2000));   /* 4E/4F to LPC */
    if (pci_r16(0x82) & 0x2000) bridge(); else printf("Bridge: 4E/4F is not forwarded yet (run with a preset).\n");
    if (have_ports && opt_dma && !opt_dry) dma_reset();
    return 0;
}
