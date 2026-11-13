"""Single source of truth: RP2354B GPIO assignment. Run: python pinmap.py (checks rules, prints table)."""
import json, sys, pathlib

PINS = json.load(open(pathlib.Path(__file__).parent.parent / "lib" / "rp2354b_qfn80_pins.json"))

# name -> (gpio, direction as seen by MCU, note)
MAP = {
    # shift chain on hardware SPI0 (RX 0/4/16/20/32/36, SCK 2/6/18/22/34/38, TX 3/7/19/23/35/39)
    "CHAIN_MISO":  (4,  "in",  "SPI0 RX  <- 165 QH"),
    "CHAIN_SCK":   (6,  "out", "SPI0 SCK -> 595 SRCLK + 165 CLK"),
    "CHAIN_MOSI":  (7,  "out", "SPI0 TX  -> 595 SER"),
    "CHAIN_RCLK":  (5,  "out", "595 RCLK"),
    "CHAIN_SHLD":  (8,  "out", "165 SH/LD#"),
    # 138 DACK decoder; G1 shares the AEN net
    "DACK_A":      (0,  "out", "138 A"),
    "DACK_B":      (1,  "out", "138 B"),
    "DACK_C":      (2,  "out", "138 C"),
    "AEN":         (3,  "out", "244 in -> slot AEN, also 138 G1"),
    # ISA strobes via output 244
    "IOR_N":       (9,  "out", "244 in -> slot IOR#"),
    "IOW_N":       (10, "out", "244 in -> slot IOW#"),
    "TC":          (13, "out", "244 in -> slot TC"),
    "SD_OE_N":     (11, "out", "2x LVCC4245A OE# (H = isolated)"),
    "IOCHRDY":     (12, "in",  "direct <- slot IOCHRDY (GPIO0-39 are 5 V tolerant, RP2350 DS 14.8.2.1)"),
    "ISA_RUN":     (14, "out", "1G00 -> RESETDRV (low = reset)"),
    "PCIRST_N":    (15, "in",  "TPMS1 PCIRST# (also 1G00)"),
    # data bus on the LVCC4245A B side (3.3 V), two chips
    **{f"SD{i}": (16 + i, "bidir", "4245 B side") for i in range(16)},
    # DRQ straight from the slot (GPIO0-39 are 5 V tolerant with IOVDD = 3.3 V)
    **{f"DRQ{c}": (g, "in", "direct from slot") for c, g in zip((0, 1, 2, 3, 5, 6, 7), range(32, 39))},
    "SD_DIR":      (39, "out", "2x LVCC4245A DIR: A=ISA(5V) B=MCU(3V3); H = A->B = card->MCU (read), L = MCU->card (write); set before strobe"),
    # LPC (3.3 V only), PIO0 with GPIOBASE=16
    "LAD0":        (40, "bidir", "PIO"), "LAD1": (41, "bidir", "PIO"),
    "LAD2":        (42, "bidir", "PIO"), "LAD3": (43, "bidir", "PIO"),
    "LFRAME_N":    (44, "in",    "PIO"),
    "LCLK":        (45, "in",    "PIO"),
    "SERIRQ":      (46, "bidir", "PIO, open-drain style + pull-up"),
    "LDRQ_N":      (47, "out",   "PIO, to wired chipset tap"),
}

# Reset state of every GPIO is input + internal pull-down (RPD 36-113 kohm at 3.3 V, datasheet Table).
# MCU outputs that are active-low need an external 4.7 kohm pull-up so the net idles inactive while the pin is Hi-Z:
POWERUP_PULLUP = ["IOR_N", "IOW_N", "SD_OE_N", "CHAIN_SHLD", "LDRQ_N"]
# MCU outputs that idle correctly on the internal pull-down (active-high or edge-triggered):
POWERUP_PULLDOWN_OK = ["AEN", "TC", "DACK_A", "DACK_B", "DACK_C", "SD_DIR", "ISA_RUN", "CHAIN_SCK", "CHAIN_MOSI", "CHAIN_RCLK"]
# Host-driven or shared lines: no MCU power-up part. LAD needs 15-100 kohm pull-ups (LPC spec 10.4) and SERIRQ an
# external pull-up (PCH datasheet 5.11): confirm whether the TPMS1 side already provides them before adding parts.
HOST_DRIVEN = ["LFRAME_N", "PCIRST_N", "LCLK", "LAD0", "LAD1", "LAD2", "LAD3", "SERIRQ"]

def check():
    errs = []
    used = {}
    for n, (g, d, _) in MAP.items():
        if not 0 <= g <= 47: errs.append(f"{n}: GPIO{g} out of range")
        if g in used: errs.append(f"GPIO{g} used by {used[g]} and {n}")
        used[g] = n
    lad = [MAP[f"LAD{i}"][0] for i in range(4)]
    if lad != list(range(lad[0], lad[0] + 4)): errs.append("LAD0-3 not contiguous ascending")
    lpc = [MAP[n][0] for n in ("LAD0","LAD1","LAD2","LAD3","LFRAME_N","LCLK","SERIRQ","LDRQ_N")]
    if not all(16 <= g <= 47 for g in lpc): errs.append("LPC outside PIO GPIOBASE=16 window (16-47)")
    sd = [MAP[f"SD{i}"][0] for i in range(16)]
    if sd != list(range(sd[0], sd[0] + 16)): errs.append("SD0-15 not contiguous")
    spi = {"RX": {0,4,16,20,32,36}, "SCK": {2,6,18,22,34,38}, "TX": {3,7,19,23,35,39}}
    if MAP["CHAIN_MISO"][0] not in spi["RX"]: errs.append("CHAIN_MISO not an SPI RX pin")
    if MAP["CHAIN_SCK"][0] not in spi["SCK"]: errs.append("CHAIN_SCK not an SPI SCK pin")
    if MAP["CHAIN_MOSI"][0] not in spi["TX"]: errs.append("CHAIN_MOSI not an SPI TX pin")
    if len({MAP[k][0] // 4 for k in ("CHAIN_MISO","CHAIN_SCK","CHAIN_MOSI")}) != 2 and False: pass
    for n in POWERUP_PULLUP + POWERUP_PULLDOWN_OK + HOST_DRIVEN:
        if n not in MAP: errs.append(f"power-up rule names unknown signal {n}")
    if set(POWERUP_PULLUP) & set(POWERUP_PULLDOWN_OK): errs.append("signal in both power-up lists")
    outs = {n for n, (g, d, _) in MAP.items() if d == "out"}
    unplaced = outs - set(POWERUP_PULLUP) - set(POWERUP_PULLDOWN_OK)
    if unplaced: errs.append(f"outputs with no power-up rule: {sorted(unplaced)}")
    return errs, used

if __name__ == "__main__":
    errs, used = check()
    for g in range(48):
        n = used.get(g, "-- spare --")
        print(f"GPIO{g:<2} pin{PINS[f'GPIO{g}']:<3} {n:<10}", MAP[n][2] if n in MAP else "")
    print(f"\n{len(used)} of 48 GPIO used")
    print("ERRORS:" if errs else "pin map OK", *errs, sep="\n  ")
    sys.exit(1 if errs else 0)
