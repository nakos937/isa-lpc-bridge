"""isa-lpc-bridge netlist (SKiDL). Run: source env.sh && python sch/build.py
Writes sch/out/isa_bridge.net (KiCad netlist) and sch/out/isa_bridge.kicad_sch, runs nothing else.
Every rail/branch name here is a net; values follow memory notes + datasheets (see docs/)."""
import os, sys, json, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
(ROOT / "sch" / "out").mkdir(exist_ok=True)
os.chdir(ROOT / "sch" / "out")  # SKiDL writes .erc/.log/_sklib files to cwd; must happen before import
from skidl import *
import skidl
sys.path.insert(0, str(ROOT / "docs"))
import pinmap  # noqa: E402

for t in (KICAD9, KICAD10, KICAD8):
    skidl.lib_search_paths[t] += [os.environ["KICAD_SYMBOL_DIR"], str(ROOT / "lib")]
skidl.footprint_search_paths[KICAD9] += [os.environ["KICAD_FOOTPRINT_DIR"], str(ROOT / "lib")]
set_default_tool(KICAD9)
skidl.config.cfg if False else None

OUT = ROOT / "sch" / "out"
OUT.mkdir(exist_ok=True)

# ---- footprints (KiCad stock unless noted) -------------------------------------------------
FP = dict(
    c0402="Capacitor_SMD:C_0402_1005Metric", c0603="Capacitor_SMD:C_0402_1005Metric",
    c0805="Capacitor_SMD:C_0805_2012Metric", c1206="Capacitor_SMD:C_1206_3216Metric", c1210="Capacitor_SMD:C_1210_3225Metric",
    r0402="Resistor_SMD:R_0402_1005Metric", r0603="Resistor_SMD:R_0402_1005Metric",
    rn="Resistor_SMD:R_Array_Convex_4x0402", fb="Inductor_SMD:L_0805_2012Metric", fb1206="Inductor_SMD:L_1206_3216Metric",
    d="isa_bridge:D_SOD-123FL_1.1x1.5", tvs="Diode_SMD:D_SMA", tp="TestPoint:TestPoint_Pad_D1.0mm",
    qfn80="Package_DFN_QFN:QFN-80-1EP_10x10mm_P0.4mm_EP3.4x3.4mm", tssop16="Package_SO:TSSOP-16_4.4x5mm_P0.65mm", qfn16="Package_DFN_QFN:DHVQFN-16-1EP_2.5x3.5mm_P0.5mm_EP1x2mm", qfn20="Package_DFN_QFN:DHVQFN-20-1EP_2.5x4.5mm_P0.5mm_EP1x3mm",
    tssop20="Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm", tssop24="Package_SO:TSSOP-24_4.4x7.8mm_P0.65mm",
    msop12="Package_SO:MSOP-12-1EP_3x4.039mm_P0.65mm_EP1.651x2.845mm", msop10="Package_SO:MSOP-10-1EP_3x3mm_P0.5mm_EP1.68x1.88mm",
    sot235="Package_TO_SOT_SMD:SOT-23-5", vssop8="Package_SO:VSSOP-8_2.3x2mm_P0.5mm",
    xtal="Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm", osc="Oscillator:Oscillator_SMD_Abracon_ASE-4Pin_3.2x2.5mm",
    ind_mcu="isa_bridge:L_Abracon_AOTA-B201610S3R3-101-T", ind_boost="Inductor_SMD:L_Bourns_SRP5030T",
    hdr2x9="Connector_PinHeader_2.54mm:PinHeader_2x09_P2.54mm_Horizontal", hdr1x2="Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Horizontal",
    hdr1x4="Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Horizontal", hdr1x6="Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical",
    isa="isa_bridge:ISA", berg="isa_bridge:Berg_171826-4_1x04_P2.50mm",
)

# ---- helpers --------------------------------------------------------------------------------
_nets = {}
def N(name):
    if name not in _nets: _nets[name] = Net(name)
    return _nets[name]

def R(val, a, b, fp="r0402"):
    r = Part("Device", "R", value=val, footprint=FP[fp]); r[1] += a; r[2] += b; return r

def C(val, a, b, fp="c0402"):
    c = Part("Device", "C", value=val, footprint=FP[fp]); c[1] += a; c[2] += b; return c

def decouple(rail, n, val="100n", fp="c0402"):
    for _ in range(n): C(val, rail, N("GND"), fp)

def FBEAD(a, b, val="BLM21PG221SN1D"):
    f = Part("Device", "FerriteBead", value=val, footprint=FP["fb1206"] if val.startswith("BLM31") else FP["fb"]); f[1] += a; f[2] += b; return f

def flag(net):
    p = Part("power", "PWR_FLAG", footprint="TestPoint:TestPoint_Pad_D1.0mm"); p[1] += net

GND = N("GND")
V5, V12 = N("+5V_IN"), N("+12V_IN")

# ================================================================================================
# 1. POWER INPUT (Berg floppy connector) and branch filtering
# ================================================================================================
berg = Part("isa_bridge", "Berg_Floppy_Power_4P", footprint=FP["berg"])
f5 = Part("Device", "Polyfuse", value="1812L260/12MR", footprint="Fuse:Fuse_1812_4532Metric")
f12 = Part("Device", "Polyfuse", value="1812L150/24MR", footprint="Fuse:Fuse_1812_4532Metric")
berg["+5V"] += f5[1]; f5[2] += V5
berg["+12V"] += f12[1]; f12[2] += V12
berg["GND"] += GND
flag(V5); flag(V12); flag(GND)
# surge clamps after the fuses (unidirectional TVS, SMA): 5 V standoff, 13 V standoff (ADP5071 and 25 V caps sit behind it)
t5 = Part("Device", "D_Zener", value="SMAJ5.0A", footprint=FP["tvs"]); t5["K"] += V5; t5["A"] += GND
t12 = Part("Device", "D_Zener", value="SMAJ13A", footprint=FP["tvs"]); t12["K"] += V12; t12["A"] += GND
C("47u/10V", V5, GND, "c1206"); C("10u/25V", V5, GND, "c1206")        # 100u/16V in 1210 does not exist as MLCC; 47u/10V X7R (GRM32ER71A476KE15L)        # bulk at the connector
C("22u/25V", V12, GND, "c1206"); C("10u/25V", V12, GND, "c1206")        # 47u/25V 1210 not found; 22u/25V X7R (GRM32ER71E226KE15L)

# branches: slot rails never share a copper path with the switchers
V5_SLOT, V12_SLOT, V5_LDO, V5_LOG, V12_PUMP = (N(n) for n in ("+5V_SLOT", "+12V_SLOT", "+5V_LDO", "+5V_LOG", "+12V_PUMP"))
FBEAD(V5, V5_SLOT, "BLM31KN601SN1L"); C("22u/10V", V5_SLOT, GND, "c1206"); C("100n", V5_SLOT, GND)
FBEAD(V12, V12_SLOT, "BLM31KN601SN1L"); C("22u/25V", V12_SLOT, GND, "c1206"); C("100n", V12_SLOT, GND)
V5_LDO = V5_LOG                                                      # one logic 5 V branch: the 3V3 LDO is an ordinary part, no separate quiet branch
FBEAD(V5, V5_LOG); C("10u/10V", V5_LOG, GND, "c0805"); decouple(V5_LOG, 4)
FBEAD(V12, V12_PUMP, "BLM31KN601SN1L"); C("22u/25V", V12_PUMP, GND, "c1206"); C("100n", V12_PUMP, GND)
for n in (V5_SLOT, V12_SLOT, V5_LOG, V12_PUMP): flag(n)

# ================================================================================================
# 2. +3V3 (AP7361C-33E, 1 A) and +3V3_OSC (ferrite-filtered 3V3)
# ================================================================================================
V3 = N("+3V3"); V3_OSC = N("+3V3_OSC")
u3 = Part("Regulator_Linear", "AP7361C-33E", value="BL9110-330BPFB", footprint="Package_TO_SOT_SMD:SOT-223-3_TabPin2")   # Belling BL9110 SOT-223-3-B (1 GND, 2 VOUT + tab, 3 VIN), same pad order as the Diodes AP7361C-33ER (SOT-223R); AP7361C-33E-13 has no stock at JLC
for _p in u3.pins: _p.num = {"1": "3", "2": "1", "3": "2"}.get(str(_p.num), str(_p.num))                                   # AP7361C-33E numbering (1 IN, 2 GND, 3 OUT) -> 1 GND, 2 OUT, 3 IN
u3["VI"] += V5_LDO; u3["GND"] += GND; u3["VO"] += V3
C("22u/10V", V3, GND, "c1206"); C("22u/10V", V3, GND, "c1206")
FBEAD(V3, V3_OSC); C("10u/10V", V3_OSC, GND, "c0805")                        # oscillator rail: 3V3 through a ferrite bead and 10u instead of a second LDO

# ================================================================================================
# 3. NEGATIVE RAILS: one ADP5071 inverter (ADP5070 pin-compatible, 1.2 A inverter switch: ~10 % margin became ~3x) (+12 V -> -12 V), MC79M05 for -5 V.
#    Values from the ADP5070/ADP5071 datasheets: Table 8 resistors, Table 9 common parts (CIN1 10u, CVREG 1u, CVREF 1u), inductor/COMP for the inverter.
#    Boost half unused: EN1 low; PVIN1 tied to PVIN2 (datasheet requires the pins supplied).
# ================================================================================================
VNEG_RAW = N("-12V_SLOT")                                             # inverter output IS the slot -12 V rail (op-amp split supply, noise not critical: no post-LDO)
ub = Part("Regulator_Switching", "ADP5071AREZ", value="ADP5071ACPZ", footprint="Package_CSP:LFCSP-20-1EP_4x4mm_P0.5mm_EP2.6x2.6mm")   # LFCSP-20 (CP-20-8) is 11 USD cheaper than the TSSOP and in stock
_LFCSP = {"1": "19", "2": "20", "20": "18"}; _LFCSP.update({str(n): str(n - 2) for n in range(3, 20)})   # datasheet Table 5: TSSOP pin number -> LFCSP pin number
for _p in ub.pins:
    _p.num = _LFCSP.get(str(_p.num), str(_p.num))
for nm in ("PVIN1", "PVIN2", "PVINSYS"): ub[nm] += V12_PUMP
ub["PGND"] += GND; ub["AGND"] += GND; ub[21] += GND                          # AGND and the exposed pad (21)
# EN1/EN2 abs max 6 V (datasheet Table 3): never tie to +12 V. Divide +12 V to about 2 V for EN2 (precision threshold, see datasheet), EN1 low = boost off.
en2 = N("EN2_DIV"); R("200k", V12_PUMP, en2, "r0603"); R("47k", en2, GND, "r0603"); ub["EN2"] += en2
ub["EN1"] += GND; ub["SEQ"] += NC                                      # manual enable mode (datasheet Table 7); SEQ low would start the boost after the inverter
ub["SYNC/FREQ"] += GND                                                 # 1.2 MHz
ub["SLEW"] += GND                                                      # slowest slew = best noise (datasheet pin description)
ub["SS"] += NC
# second clamp right at the ADP5071 input (abs max PVIN 18 V): SMF12A, 13.3-14.7 V breakdown, about 15.5 V at a few amps; the upstream SMAJ13A is the big surge part
t12b = Part("Device", "D_Zener", value="SMF12A", footprint=FP["d"]); t12b["K"] += V12_PUMP; t12b["A"] += GND
C("1u/16V", ub["VREG"], GND, "c0603"); C("1u/16V", ub["VREF"], GND, "c0603")
for _ in range(2): C("10u/25V", V12_PUMP, GND, "c1206")               # CIN1 per Table 9
C("100n", V12_PUMP, GND); C("1u/25V", ub["PVINSYS"], GND, "c0603")   # datasheet layout: separate cap on PVINSYS for best noise
ub["FB1"] += GND; ub["COMP1"] += NC; ub["INBK"] += NC; ub["SW1"] += NC
SW2N = N("SW2_INV"); ub["SW2"] += SW2N                              # named so the router can give the inverter switch node a power width (peak inductor current 1.2-1.4 A)
l2 = Part("Device", "L", value="15u NR4030S150MT", footprint="isa_bridge:L_NR4030"); l2[1] += SW2N; l2[2] += GND
d2 = Part("Device", "D_Schottky", value="DFLS240", footprint=FP["d"]); d2["A"] += VNEG_RAW; d2["K"] += SW2N
C("10u/50V", VNEG_RAW, GND, "c1206"); C("10u/25V", VNEG_RAW, GND, "c1206")   # COUT2: ADP5071 Table 11 uses one 10 uF/50 V 1210 (GRM32ER71H106KA12L); HG24: 50 V 1206 (C13585, JLC Basic)
R("1M", VNEG_RAW, ub["FB2"], "r0603"); R("62k", ub["FB2"], ub["VREF"], "r0603")   # HG23: 1M is a JLC Basic part; VNEG = 0.8 - 0.8*1M/62k = -12.10 V (was 1.15M/71.5k, both Extended)   # VNEG = 0.8 - 0.8*RFT2/RFB2 = -12.067 V (1.15M/71.5k, the pair in the ADP5071 datasheet row; low VREF load 11 uA; 330k/20k loaded VREF with 40 uA; datasheet VREF 1.60 V, VREF-VFB2 0.8 V)
cc2 = N("COMP2_N"); R("15k", ub["COMP2"], cc2, "r0603"); C("22n", cc2, GND)           # HG23: CC2 22 nF (JLC Basic; zero at 480 Hz, below fc/4); was RC2 15k / CC2 47 nF (table 11 for 12 V in, 1.2 MHz)

M12 = VNEG_RAW
M5 = N("-5V_SLOT")                                                       # 7905 (negative 1 A linear, dropout about 2 V) fed from -12V_SLOT
u5 = Part("Regulator_Linear", "MC7905", value="MC79M05CDTG", footprint="Package_TO_SOT_SMD:TO-252-3_TabPin2")   # onsemi MC7900 (1 A) exists only as TO-220/D2PAK; the DPAK part is MC79M05CDT (500 mA, pin1 GND, tab = pin2 = input)
u5["GND"] += GND; u5["VI"] += M12; u5["VO"] += M5
C("10u/25V", M5, GND, "c1206")   # MC79M00 datasheet: no protection diode needed below 10 uF output capacitance (was 22 uF)

# ================================================================================================
# 4. MCU: RP2354B + 12 MHz crystal + reference power circuit (values from RPi RP2350A Minimal R4)
# ================================================================================================
DVDD = N("+1V1"); VREG_LX = N("VREG_LX"); VREG_AVDD = N("VREG_AVDD")
mcu = Part("MCU_RaspberryPi", "RP2354B", footprint=FP["qfn80"])
for pin in mcu.pins:
    nm = pin.name
    if nm == "IOVDD": pin += V3
    elif nm == "DVDD": pin += DVDD
    elif nm == "VREG_FB": pin += DVDD
    elif nm in ("ADC_AVDD", "QSPI_IOVDD", "USB_OTP_VDD", "VREG_VIN"): pin += V3
    elif nm == "VREG_AVDD": pin += VREG_AVDD
    elif nm == "VREG_PGND" or nm == "GND": pin += GND
    elif nm == "VREG_LX": pin += VREG_LX
# regulator: L1 3.3 uH between LX and DVDD (dot side = LX), C6 VREG_VIN, C7 4.7u on DVDD, R3 33 + C9 on AVDD
flag(DVDD); flag(VREG_AVDD)
l1 = Part("Device", "L", value="3.3u AOTA-B201610S3R3-101-T", footprint=FP["ind_mcu"]); l1[1] += DVDD; l1[2] += VREG_LX                                            # pad 1 (polarity dot) = +1V1 as on the Raspberry Pi reference board
C("4.7u/10V", V3, GND, "c0603"); C("4.7u/10V", DVDD, GND, "c0603"); C("4.7u/10V", DVDD, GND, "c0603")
R("33", V3, VREG_AVDD); C("4.7u/10V", VREG_AVDD, GND, "c0603")
decouple(DVDD, 3); decouple(V3, 10)                       # 100n per IOVDD/ADC/USB/QSPI/VIN pin pair
C("10u/10V", V3, GND, "c0805"); C("10u/10V", V3, GND, "c0805")
# crystal ABM8-272-T3: XIN pin1, XOUT -> 1k -> pin3, 15 pF each side to GND (reference design)
xt = Part("Device", "Crystal_GND24", value="12MHz ABM8-272-T3", footprint=FP["xtal"])
xin, xo = N("XIN"), N("XOUT"); xo2 = N("XOUT_X")
xt[1] += xin; xt[3] += xo2; xt[2] += GND; xt[4] += GND
mcu["XIN"] += xin; mcu["XOUT"] += xo
R("1k", xo, xo2); C("15p", xin, GND); C("15p", xo2, GND)
# reset / SWD header (Pico debug probe order): pin1 SWCLK, 2 GND, 3 SWDIO, 4 RUN. Flash with picotool/OpenOCD over SWD; no BOOTSEL/UART boot parts.
RUN = N("RUN")
mcu["RUN"] += RUN; R("10k", V3, RUN)
C("100n", RUN, GND)
for nm in ("QSPI_SS", "QSPI_SD1", "QSPI_SD2", "QSPI_SD3", "QSPI_SCLK", "QSPI_SD0"): mcu[nm if nm != "QSPI_SS" else "~{QSPI_SS}"] += NC   # internal flash die uses them, nothing external
for nm in ("USB_DM", "USB_DP"): mcu[nm] += NC                          # USB unused (rp2350.txt p.1077: pins may be left, pull resistors are internal)
mcu["SWCLK"] += N("SWCLK"); mcu["SWDIO"] += N("SWDIO")
prog = Part("Connector_Generic", "Conn_01x04", value="PROG_SWD", footprint=FP["hdr1x4"])
for i, n in enumerate([N("SWCLK"), GND, N("SWDIO"), RUN], 1): prog[i] += n

# GPIO wiring from pinmap.py: every MAP name becomes a net named after the signal
sig = {}
DIRECT = {f"DRQ{c}" for c in (0, 1, 2, 3, 5, 6, 7)} | {"IOCHRDY"}      # no buffer: MCU pin = slot net
def S(name):
    """MCU-side net for a pinmap signal. M_ prefix keeps it distinct from the slot-side net of the same name."""
    if name not in sig: sig[name] = N(name) if name in DIRECT else N("M_" + name)
    return sig[name]
GPIO_PIN = pinmap.PINS  # {"GPIO7": 4, ...} datasheet QFN-80 pin numbers (checked against the symbol)
SERIES_MCU = ("CHAIN_SCK", "IOR_N", "IOW_N", "LCLK")                   # HG23: 33R at the MCU pin (net M_X_o = MCU side), nets M_X = far side
for name, (g, d, _) in pinmap.MAP.items():
    mcu[GPIO_PIN[f"GPIO{g}"]] += (N("M_" + name + "_o") if name in SERIES_MCU else S(name))
for pin in mcu.pins:
    if pin.name.startswith("GPIO") and not pin.is_connected(): pin += NC
for n in pinmap.POWERUP_PULLUP: R("4.7k", N("M_" + n + "_o") if n in SERIES_MCU else S(n), V3)   # HG23: strobe pull-ups sit on the MCU side of the series R
for n in ("ISA_RUN", "AEN", "TC", "SD_DIR"): R("10k", S(n), GND)


# ================================================================================================
# 5. ISA-side logic (5 V domain +5V_LOG) and MCU-side buffers (3.3 V). Chip-side net "X_o" -> 33R -> slot net "X".
# ================================================================================================
def vcc_gnd(part, vcc, vcc_pin="VCC"):
    part[vcc_pin] += vcc; part["GND"] += GND
    C("100n", vcc, GND)

def rarr(value, pairs, pack="rn"):
    """R_Pack04 arrays: pairs of (netA, netB), 4 resistors per package, unused ones tied to NC."""
    for k in range(0, len(pairs), 4):
        pk = Part("Device", "R_Pack04", value=value, footprint=FP[pack]); chunk = pairs[k:k + 4]
        for i, (x, y) in enumerate(chunk): pk[i + 1] += x; pk[8 - i] += y
        for i in range(len(chunk), 4): pk[i + 1] += NC; pk[8 - i] += NC

# 33R series damping only where edges are fast or the line is a strobe/clock: SD0-15, IOR#, IOW#, OSC, BCLK, RESETDRV.
# Address, SBHE#, DACK#, AEN, TC change rarely, and dISAppointment (proven with an SB16) has no series resistors on any ISA line.
NO_SERIES = {f"SA{i}" for i in range(20)} | {"SBHE#", "AEN", "TC"} | {f"DACK{c}#" for c in (0, 1, 2, 3, 5, 6, 7)}
def slot_out(name):
    """returns (chip_side_net, slot_side_net); joined by a 33R series resistor unless the line is in NO_SERIES."""
    if name in NO_SERIES:
        n = N(name); return n, n
    chip, slot = N(name + "_o"), N(name); (_series68 if name.startswith("SD") else _series).append((chip, slot)); return chip, slot
_series, _series68 = [], []   # SD0-15: 68R (4245A 24 mA edges, simulated: 33R leaves 5.7 V / 22 mA clamp current on long lines); others 33R

RESETDRV = N("RESETDRV")   # internal, active high; 595 OE# and RESETDRV slot pin

# ---- address chain: 3x 74AHCT595 (SA0-15, SBHE#, SA16-19) -------------------------------------------------------
sa_o, sa = zip(*[slot_out(f"SA{i}") for i in range(20)]); sb_o, sb = slot_out("SBHE#")
bits = list(sa_o[0:16]) + [sb_o] + list(sa_o[16:20]) + [None] * 3
sck, rclk, mosi = S("CHAIN_SCK"), S("CHAIN_RCLK"), S("CHAIN_MOSI")
chain_in = [mosi, N("CHAIN_C1"), N("CHAIN_C2")]
for k in range(3):
    u = Part("isa_bridge", "SN74AHCT595", footprint=FP["qfn16"]); vcc_gnd(u, V5_LOG); u[17] += GND
    u["SER"] += chain_in[k]; u["SRCLK"] += sck; u["RCLK"] += rclk; u["~{SRCLR}"] += V5_LOG; u["~{OE}"] += RESETDRV
    for b_, q in enumerate("ABCDEFGH"):
        net = bits[k * 8 + b_]
        (u[f"Q{q}"].__iadd__(NC) if net is None else u[f"Q{q}"].__iadd__(net))
    # pin 9 is QH' (serial out). Look it up by number: u["QH'"] fuzzy-matches pin 7 (QH) and shorts SA7/SA15 into the chain.
    if k < 2: u[9] += chain_in[k + 1]
    else: u[9] += NC

# ---- input chain: 2x 74LVC165A (3.3 V) reading IOCS16#, IRQ3-7,9-12,14,15 + IOCHCK# --------------------------------------------
IRQ = {n: N(f"IRQ{n}") for n in (3, 4, 5, 6, 7, 9, 10, 11, 12, 14, 15)}; IOCHCK = N("IOCHCK#")
miso, shld = S("CHAIN_MISO"), S("CHAIN_SHLD")
u1 = Part("isa_bridge", "SN74LVC165A", footprint=FP["qfn16"]); u2 = Part("isa_bridge", "SN74LVC165A", footprint=FP["qfn16"])   # JLC: SN74LVC165AWBQBRQ1 (C33901164) is the same 2.5 x 3.5 mm WQFN-16 (BQB) with wettable flanks
for u in (u1, u2):
    u[17] += GND
    u["VCC"] += V3; u["GND"] += GND; C("100n", V3, GND)
    u["SH/LD#"] += shld; u["CLK"] += sck; u["CLKINH"] += GND
for u, ins in ((u1, [N("IOCS16#"), IRQ[4], IRQ[5], IRQ[6], IRQ[7], IRQ[9], IRQ[10], IRQ[11]]),   # H = first bit out: IOCS16# readable without a clock (HG25)
               (u2, [IRQ[12], IRQ[14], IRQ[15], IOCHCK, IRQ[3], GND, GND, GND])):
    for pin, net in zip("HGFEDCBA", ins): u[pin] += net
u1["QH"] += miso; u1["SER"] += u2["QH"]; u2["SER"] += GND; u1["QH#"] += NC; u2["QH#"] += NC

# ---- strobes: AHCT244 -> IOR#, IOW#, AEN, TC (always enabled) -----------------------------------------------------------
st = Part("isa_bridge", "SN74AHCT244", footprint=FP["qfn20"]); vcc_gnd(st, V5_LOG); st[21] += GND
st["1OE"] += GND; st["2OE"] += GND
aen_o, aen = slot_out("AEN")
for pin_in, pin_out, src, name in (("1A0", "1Y0", S("IOR_N"), "IOR#"), ("1A1", "1Y1", S("IOW_N"), "IOW#"), ("1A2", "1Y2", S("AEN"), "AEN"), ("1A3", "1Y3", S("TC"), "TC")):
    if name == "AEN": o = aen_o
    else: o, _ = slot_out(name)
    st[pin_in] += src; st[pin_out] += o
for pin in ("2A0", "2A1", "2A2", "2A3"): st[pin] += GND
for pin in ("2Y0", "2Y1", "2Y2", "2Y3"): st[pin] += NC

# ---- DACK decode: 74HCT138, G1(E2) = MCU AEN, G2A/G2B low ---------------------------------------------------------------
dk = Part("isa_bridge", "SN74AHCT138", value="74AHCT138", footprint=FP["tssop16"]); vcc_gnd(dk, V5_LOG)   # TSSOP-16, Nexperia 74AHCT138PW in stock (the DHVQFN-16 has no stock)
dk["A"] += S("DACK_A"); dk["B"] += S("DACK_B"); dk["C"] += S("DACK_C")
dk["G2A#"] += GND; dk["G2B#"] += GND; dk["G1"] += S("AEN")
for c in (0, 1, 2, 3, 5, 6, 7):
    o, _ = slot_out(f"DACK{c}#"); dk[f"Y{c}#"] += o
dk["Y4#"] += NC

# ---- data bus: 2x SN74LVCC4245A, A port = ISA 5 V (via 33R), B port = MCU 3.3 V ----------------------------------------------------------
for k in range(2):
    t = Part("isa_bridge", "SN74LVCC4245A", value="SN74LVC4245A", footprint=FP["tssop24"])   # SN74LVC4245A is a Preferred JLC part; pin 23 is VCCB on it (not connected on the LVCC)
    t["VCCA"] += V5_LOG; t["VCCB"] += V3; t["GND"] += GND; C("100n", V5_LOG, GND); C("100n", V3, GND)
    t["DIR"] += S("SD_DIR"); t["OE#"] += S("SD_OE_N"); t["VCCB2"] += V3
    for i in range(8):
        o, _ = slot_out(f"SD{k * 8 + i}")
        t[f"A{i + 1}"] += o; t[f"B{i + 1}"] += S(f"SD{k * 8 + i}")

# ---- DRQ + IOCHRDY: slot net and MCU net are the same net, straight to GPIO. GPIO0-39 are 5 V fault tolerant with IOVDD = 3.3 V (RP2350 datasheet 14.8.2.1, 14.9.1).
# ================================================================================================
# 6. Clocks for the slot: 14.31818 MHz oscillator (+3V3_OSC) -> OSC, divided by 2 -> BCLK. RESETDRV.
# ================================================================================================
osc = Part("Oscillator", "ASE-xxxMHz", value="ASE-14.31818MHZ-L-C-T", footprint=FP["osc"])
osc["EN"] += V3_OSC; osc["GND"] += GND; osc["Vdd"] += V3_OSC; C("100n", V3_OSC, GND); C("100n", V3_OSC, GND)
osc_raw = N("OSC_RAW"); osc["OUT"] += osc_raw
ff = Part("74xGxx", "74LVC2G74", footprint="Package_SO:VSSOP-8_2.3x2mm_P0.5mm")
ff["VCC"] += V3_OSC; ff["GND"] += GND; C("100n", V3_OSC, GND)
ff["~{CLR}"] += V3_OSC; ff["~{PRE}"] += V3_OSC
bq = N("BCLK_Q"); bq_raw = N("BCLK_Q_RAW"); ff["C"] += osc_raw; ff["D"] += ff["~{Q}"]; ff["Q"] += bq_raw
def buf(src, name):
    b = Part("74xGxx", "74AHCT1G125", footprint=FP["sot235"]); b[3] += GND; b[5] += V5_LOG; C("100n", V5_LOG, GND)
    b[1] += GND; b[2] += src                              # OE# low = enabled
    o, slotn = slot_out(name); b[4] += o; return slotn
bclk_slot = buf(bq, "BCLK")                                   # no OSC (14.318 MHz) output: dISAppointment leaves ISA pin B30 open, CS4237B has no OSC pin
# RESETDRV = NAND(ISA_RUN, PCIRST#): high (reset) while the host resets or firmware has not released the slot
nd = Part("74xGxx", "74AHCT1G00", footprint=FP["sot235"]); nd[3] += GND; nd[5] += V5_LOG; C("100n", V5_LOG, GND)
nd[1] += S("ISA_RUN"); nd[2] += S("PCIRST_N"); nd[4] += RESETDRV
rs_o = N("RESETDRV"); rs_slot = N("RESETDRV_S"); _series.append((rs_o, rs_slot))

# ================================================================================================
# 7. ISA socket: pin roles from the dISAppointment netlist, pull-ups from lib/disappointment_pulls.json
# ================================================================================================
isa = Part("isa_bridge", "ISA_16bit_Socket", footprint=FP["isa"])
pinout = json.load(open(ROOT / "lib" / "isa_socket_pinout.json"))
POWER = {"GND": GND, "+5V": V5_SLOT, "-5V": M5, "-12V": M12, "+12V": V12_SLOT}
SPECIAL = {"RSTDRV": rs_slot, "SYSCLK": bclk_slot, "BALE": N("BALE"), "DACK1": N("DACK1#")}
unused_slot = {}
for pnum, info in pinout.items():
    nm = info["net"].lstrip("/")
    if nm in POWER: net = POWER[nm]
    elif nm in SPECIAL: net = SPECIAL[nm]
    elif nm.startswith("unconnected"): net = N("OSC_SLOT") if pnum == "30" else NC   # B30 = 14.318 MHz oscillator through R26 (HG24), no room for a 5 V buffer
    elif nm.startswith("DACK"): net = N(nm.split("/")[0])
    elif nm.startswith("LA"): net = N(nm.split("/")[0]); unused_slot[net.name] = net
    elif nm in ("REFRESH#", "MEMCS16#", "IOCS16#", "OWS#", "MASTER#", "SMEMR#", "SMEMW#", "MEMR#", "MEMW#"):
        net = N(nm); unused_slot[nm] = net
    else: net = N(nm)                                                      # SD*, SA*, IRQ*, DRQ*, IOR#, IOW#, AEN, TC, IOCHCK#, IOCHRDY, SBHE#
    isa[int(pnum)] += net
R("1k", N("BALE"), V5_SLOT)
R("4.7k", N("REFRESH#"), V5_SLOT)                                         # undriven here, keep inactive                                                # BALE held high: static address, transparent latches

# pull-ups/downs exactly as dISAppointment (proven with SB16), mapped to my nets
pulls = json.load(open(ROOT / "lib" / "disappointment_pulls.json"))
RAILS = {"+5V": V5_SLOT, "GND": GND}
groups = {}
for sig_name, p in pulls.items():
    if p["to"] not in RAILS: continue
    base = sig_name.split("/")[0]
    if base in ("ROMCS#", "PWRDN#") or base == "SBHE#" or (base.startswith("SA") and base[2:].isdigit()): continue   # SA/SBHE# are driven by the 595 chain, hi-Z only while RESETDRV holds the cards in reset; SD pulls stay (floating reads must return 0xFF)
    net = _nets.get(base) or _nets.get(base.replace("DACK1", "DACK1#"))
    if net is None: continue
    rail = "+3V3" if (base in DIRECT and p["to"] == "+5V") else p["to"]     # unbuffered MCU pins: +5V is up before IOVDD, so pull to 3.3 V (abs max 3.63 V with IOVDD = 0)
    groups.setdefault((p["ohm"].lower(), rail), []).append(net)
for (ohm, rail), nets in sorted(groups.items()):
    # IRQ and DRQ idle LOW: ISA IRQ/DRQ are active high (PC/104 App. C, PIIX3 and CS4237B datasheets), PnP cards leave unselected lines undriven, so a pull-up would read as an asserted request
    rarr(ohm, [(n, GND if n.name.startswith(("IRQ", "DRQ")) else (V3 if rail == "+3V3" else RAILS[rail])) for n in nets])
done = {n.name for ns in groups.values() for n in ns}
missing = [n for n in unused_slot if n not in done and n != "REFRESH#"]
print("slot nets with no pull in the JSON (check):", missing)

# 33R series resistors, one array pack per four lines
rarr("33", _series, pack="rn")
rarr("68", _series68, pack="rn")

# ================================================================================================
# 8. Host and programming headers
# ================================================================================================
tp = Part("Connector_Generic", "Conn_02x09_Odd_Even", value="TPMS1_LPC", footprint=FP["hdr2x9"])
ROLE = {1: "LCLK", 3: "LFRAME_N", 5: "PCIRST_N", 7: "LAD3", 11: "LAD0", 8: "LAD2", 10: "LAD1", 16: "SERIRQ"}
for pnum in range(1, 19):
    if pnum in ROLE: tp[pnum] += S(ROLE[pnum])
    elif pnum in (2, 12, 17, 18): tp[pnum] += GND
    elif pnum == 13: pass                                                  # key position: the host header has no pin 13, so LDRQ# here cannot touch a host line; wire from this pin to the chipset point
    else: tp[pnum] += NC                                                   # host +3V, +3VSB, SMB, S_PWRDWN#: not connected
R("100", S("LDRQ_N"), tp[13], "r0402")                                  # LDRQ# out through 100R to J5 pin 13 (replaces the separate J6 tap header)

# bring-up test points (1 mm pads): rails, clocks, reset, host-side LPC, slot control. Nets only; no extra parts besides the pads.
for _n in (V5_SLOT, V12_SLOT, V3, M12, M5, GND, S("ISA_RUN"), S("LCLK"), N("BCLK_Q")):
    _t = Part("Connector", "TestPoint", footprint=FP["tp"]); _t[1] += _n

C("10u/25V", M12, GND, "c1206")                                         # MC79M00 datasheet: CIN at the regulator, the inverter output caps sit at D4

R("33", bq_raw, bq)                                                      # source series resistor: 65 mm line, simulated undershoot -0.7 V / 17 mA clamp current at the AHCT input without it, -0.27 V / 0 mA with it

# ---- HG23 additions (appended last so no existing reference moves) ----------------------------------------------------------
for _n in SERIES_MCU: R("33", N("M_" + _n + "_o"), S(_n))                # source termination at the MCU pins: SCK 147 mm, IOR#/IOW# 135 mm, LCLK 160 mm
# DRQ idle-low pull-downs: RP2350-E9 needs <= 8.2 kohm incl. tolerance, 4.7k arrays (same part as RN3/RN4)
for _p in list(default_circuit.parts):
    if _p.name == "R_Pack04" and any(getattr(_pin.net, "name", "").startswith("DRQ") for _pin in _p.pins):
        _p.value = "4.7k"
# ---- HG24 -----------------------------------------------------------------------------------------------------------------
R("33", osc_raw, N("OSC_SLOT"))                                         # R26: slot reflections stay off the U14 clock input
# ---- HG25 -----------------------------------------------------------------------------------------------------------------
C("100n", DVDD, GND); C("100n", V3_OSC, GND)                            # C68 at U4.32, C69 at Y2.4: the nearest caps were 8 mm away
R("10k", S("PCIRST_N"), V3)                                             # R27: NAND input defined with J4 unplugged (RESETDRV stays asserted via R14 anyway)

if __name__ == "__main__":
    ERC()
    generate_netlist(file_=str(OUT / "isa_bridge.net"))
    generate_xml(file_=str(OUT / "isa_bridge.xml"))
    print("netlist written:", OUT / "isa_bridge.net")
    if os.environ.get("SCH") == "1":
        generate_schematic(filepath=str(OUT / "schematic"), top_name="isa_bridge")
        print("schematic written")
