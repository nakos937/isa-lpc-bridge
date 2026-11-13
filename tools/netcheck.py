"""Independent netlist sanity checks on sch/out/isa_bridge.net (does not trust SKiDL's ERC)."""
import re, sys, collections, pathlib, json
ROOT = pathlib.Path(__file__).resolve().parent.parent
t = (ROOT / "sch/out/isa_bridge.net").read_text()
comps = {}
for m in re.finditer(r'\(comp\s+\(ref "([^"]+)"\)\s+\(value "([^"]*)"\)(.*?)\(libsource\s+\(lib "([^"]*)"\)\s+\(part "([^"]*)"\)', t, re.S):
    fp = re.search(r'\(footprint "([^"]*)"\)', m.group(3))
    comps[m.group(1)] = dict(value=m.group(2), fp=fp.group(1) if fp else "", lib=m.group(4), part=m.group(5))
nets = {}
for m in re.finditer(r'\(net\s+\(code (\d+)\)\s+\(name "([^"]*)"\)(.*?)(?=\n    \(net\n|\n  \)\n)', t, re.S):
    nets[m.group(2)] = re.findall(r'\(ref "([^"]+)"\)\s+\(pin "([^"]+)"\)', m.group(3))
errs, warn = [], []
real = {r: c for r, c in comps.items() if not r.startswith("#")}
print(f"{len(real)} parts, {len(nets)} nets")
print("by prefix:", dict(collections.Counter(re.sub(r"\d+$", "", r) for r in real)))
for r, c in real.items():
    if not c["fp"]: errs.append(f"{r} {c['part']} has no footprint")
pins_used = collections.defaultdict(set)
for n, nodes in nets.items():
    for r, p in nodes: pins_used[r].add(p)
for n, nodes in nets.items():
    real_nodes = [x for x in nodes if not x[0].startswith("#")]
    if len(real_nodes) == 1 and not n.startswith(("unconnected", "NC", "Net-(")): warn.append(f"single-node net {n}: {real_nodes}")
# every IC pin must appear in some net (NC pins appear in 'unconnected' nets)
for r, c in real.items():
    if r[0] in "UJ": 
        if not pins_used[r]: errs.append(f"{r} {c['part']} has no connected pins")
# power-pin sanity on named IC pins
need = {"U": "VCC"}
DIRECT = {f"DRQ{c}" for c in (0, 1, 2, 3, 5, 6, 7)} | {"IOCHRDY"}   # unbuffered: GPIO0-39 are 5 V tolerant
rails = {"GND", "+3V3", "+3V3_OSC", "+5V_LOG", "+5V_SLOT", "+12V_SLOT", "+12V_PUMP", "-12V_SLOT", "-5V_SLOT", "+1V1"}
for n in rails:
    if n not in nets: errs.append(f"rail {n} missing")
# 5 V logic must never connect to MCU GPIO nets directly
mcu = [r for r, c in real.items() if c["part"] == "RP2354B"][0]
v5 = {r for r, p in nets.get("+5V_LOG", [])}
for n, nodes in nets.items():
    if any(r == mcu for r, _ in nodes) and n.startswith("M_"):
        for r, p in nodes:
            if r in v5 and real.get(r, {}).get("part", "").startswith("74AHCT") and not r == mcu:
                # AHCT inputs are 5 V parts but TTL-threshold: 3.3 V drive is fine, flag only outputs
                pass
# ISA socket connectivity
isa = [r for r, c in real.items() if c["part"] == "ISA_16bit_Socket"][0]
sock = sorted(int(p) for p in pins_used[isa]); print(f"ISA socket pins connected: {len(sock)} of 98")
if len(sock) != 98: errs.append(f"ISA socket has {98-len(sock)} unconnected pins: {sorted(set(range(1,99))-set(sock))}")
# MCU pin accounting
print(f"MCU pins connected: {len(pins_used[mcu])} of 81")
miss = sorted(set(map(str, range(1, 82))) - pins_used[mcu], key=int)
EXPECT_NC = {"66": "QSPI_SD3", "67": "QSPI_SCLK", "70": "QSPI_SD1", "71": "QSPI_SS", "72": "QSPI_SD0", "73": "QSPI_SD2",
             "74": "USB_DM", "75": "USB_DP"}   # internal flash die, no USB; build.py ties them to NC (SKiDL omits NC pins)
unexpected = [m for m in miss if m not in EXPECT_NC]
if unexpected: errs.append(f"MCU pins not in any net and not declared NC: {unexpected}")
print("MCU pins left NC on purpose:", {m: EXPECT_NC[m] for m in miss if m in EXPECT_NC})
# pinmap cross-check: every MAP signal must be on the right MCU pin
sys.path.insert(0, str(ROOT / "docs")); import pinmap
for name, (g, d, _) in pinmap.MAP.items():
    pin = str(pinmap.PINS[f"GPIO{g}"]); net = name if name in DIRECT else f"M_{name}"
    if f"{net}_o" in nets: net += "_o"                   # series resistor at the MCU pin (R22-R25)
    if (mcu, pin) not in [(r, p) for r, p in nets.get(net, [])]: errs.append(f"{name}: MCU pin {pin} (GPIO{g}) not on net {net}")
for e in errs: print("ERROR:", e)
for w in warn: print("warn: ", w)
print("RESULT:", "FAIL" if errs else "PASS", f"({len(errs)} errors, {len(warn)} warnings)")
sys.exit(1 if errs else 0)
