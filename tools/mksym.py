"""Generate lib/isa_bridge.kicad_sym from datasheet pin tables (single source of truth for custom parts)."""
import pathlib, json, re

# type codes: i input, o output, b bidirectional, t tristate, p power_in, w power_out, n no_connect, u passive
PARTS = {
    "SN74LVCC4245A": dict(ref="U", fp="Package_SO:TSSOP-24_4.4x7.8mm_P0.65mm", ds="SN74LVCC4245A datasheet SCAS584O",
        left=[(2, "DIR", "i"), (22, "OE#", "i")] + [(2 + i, f"A{i}", "b") for i in range(1, 9)][0:0]
              + [(2 + i, f"A{i}", "b") for i in range(1, 9)],
        right=[(24 - 3 + 0, "", "")][0:0] + [(22 - i, f"B{i}", "b") for i in range(1, 9)],
        top=[(1, "VCCA", "p"), (24, "VCCB", "p")], bottom=[(11, "GND", "p"), (12, "GND", "p"), (13, "GND", "p"), (23, "VCCB2", "p")]),
    "SN74AHCT595": dict(ref="U", fp="Package_DFN_QFN:DHVQFN-16-1EP_2.5x3.5mm_P0.5mm_EP1x2mm", ds="SN74AHCT595 datasheet (BQB WQFN-16, pins as PW)",
        left=[(14, "SER", "i"), (11, "SRCLK", "i"), (12, "RCLK", "i"), (10, "~{SRCLR}", "i"), (13, "~{OE}", "i")],
        right=[(15, "QA", "o"), (1, "QB", "o"), (2, "QC", "o"), (3, "QD", "o"), (4, "QE", "o"), (5, "QF", "o"), (6, "QG", "o"), (7, "QH", "o"), (9, "QH'", "o")],
        top=[(16, "VCC", "p")], bottom=[(8, "GND", "p"), (17, "EP", "p")]),
    "SN74AHCT244": dict(ref="U", fp="Package_DFN_QFN:DHVQFN-20-1EP_2.5x4.5mm_P0.5mm_EP1x3mm", ds="SN74AHCT244 datasheet (RGY VQFN-20, pins as PW)",
        left=[(1, "1OE", "i"), (19, "2OE", "i"), (2, "1A0", "i"), (4, "1A1", "i"), (6, "1A2", "i"), (8, "1A3", "i"), (17, "2A0", "i"), (15, "2A1", "i"), (13, "2A2", "i"), (11, "2A3", "i")],
        right=[(18, "1Y0", "o"), (16, "1Y1", "o"), (14, "1Y2", "o"), (12, "1Y3", "o"), (3, "2Y0", "o"), (5, "2Y1", "o"), (7, "2Y2", "o"), (9, "2Y3", "o")],
        top=[(20, "VCC", "p")], bottom=[(10, "GND", "p"), (21, "EP", "p")]),
    "SN74HCT138": dict(ref="U", fp="Package_DFN_QFN:DHVQFN-16-1EP_2.5x3.5mm_P0.5mm_EP1x2mm", ds="SN74HCT138 datasheet (RGY VQFN-16, pins as PW)",
        left=[(1, "A0", "i"), (2, "A1", "i"), (3, "A2", "i"), (4, "~{E0}", "i"), (5, "~{E1}", "i"), (6, "E2", "i")],
        right=[(15, "~{Y0}", "o"), (14, "~{Y1}", "o"), (13, "~{Y2}", "o"), (12, "~{Y3}", "o"), (11, "~{Y4}", "o"), (10, "~{Y5}", "o"), (9, "~{Y6}", "o"), (7, "~{Y7}", "o")],
        top=[(16, "VCC", "p")], bottom=[(8, "GND", "p"), (17, "EP", "p")]),
    "SN74LVC244A": dict(ref="U", fp="Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm", ds="SN74LVC244A datasheet",
        left=[(1, "1OE#", "i"), (19, "2OE#", "i"), (2, "1A1", "i"), (4, "1A2", "i"), (6, "1A3", "i"), (8, "1A4", "i"),
              (11, "2A1", "i"), (13, "2A2", "i"), (15, "2A3", "i"), (17, "2A4", "i")],
        right=[(18, "1Y1", "o"), (16, "1Y2", "o"), (14, "1Y3", "o"), (12, "1Y4", "o"),
               (9, "2Y1", "o"), (7, "2Y2", "o"), (5, "2Y3", "o"), (3, "2Y4", "o")],
        top=[(20, "VCC", "p")], bottom=[(10, "GND", "p"), (21, "EP", "p")]),
    "SN74LVC165A": dict(ref="U", fp="Package_SO:TSSOP-16_4.4x5mm_P0.65mm", ds="SN74LVC165A datasheet",
        left=[(1, "SH/LD#", "i"), (2, "CLK", "i"), (15, "CLKINH", "i"), (10, "SER", "i"),
              (11, "A", "i"), (12, "B", "i"), (13, "C", "i"), (14, "D", "i"), (3, "E", "i"), (4, "F", "i"), (5, "G", "i"), (6, "H", "i")],
        right=[(9, "QH", "o"), (7, "QH#", "o")], top=[(16, "VCC", "p")], bottom=[(8, "GND", "p"), (17, "EP", "p")]),
    "SN74LVC165A_T": dict(ref="U", fp="Package_SO:TSSOP-16_4.4x5mm_P0.65mm", ds="SN74LVC165A datasheet, TSSOP-16 (no exposed pad)",
        left=[(1, "SH/LD#", "i"), (2, "CLK", "i"), (15, "CLKINH", "i"), (10, "SER", "i"),
              (11, "A", "i"), (12, "B", "i"), (13, "C", "i"), (14, "D", "i"), (3, "E", "i"), (4, "F", "i"), (5, "G", "i"), (6, "H", "i")],
        right=[(9, "QH", "o"), (7, "QH#", "o")], top=[(16, "VCC", "p")], bottom=[(8, "GND", "p")]),
    "SN74AHCT138": dict(ref="U", fp="Package_SO:TSSOP-16_4.4x5mm_P0.65mm", ds="SN74AHCT138 datasheet",
        left=[(1, "A", "i"), (2, "B", "i"), (3, "C", "i"), (4, "G2A#", "i"), (5, "G2B#", "i"), (6, "G1", "i")],
        right=[(15, "Y0#", "o"), (14, "Y1#", "o"), (13, "Y2#", "o"), (12, "Y3#", "o"), (11, "Y4#", "o"), (10, "Y5#", "o"), (9, "Y6#", "o"), (7, "Y7#", "o")],
        top=[(16, "VCC", "p")], bottom=[(8, "GND", "p")]),
    "LTC3261": dict(ref="U", fp="Package_SO:MSOP-12-1EP_3x4.039mm_P0.65mm_EP1.651x2.845mm", ds="LTC3261 3261fb",
        left=[(10, "EN", "i"), (11, "MODE", "i"), (2, "RT", "u"), (9, "VIN", "p")],
        right=[(4, "VOUT", "w"), (8, "C+", "u"), (5, "C-", "u")],
        top=[], bottom=[(13, "GND", "p"), (1, "NC", "n"), (3, "NC", "n"), (6, "NC", "n"), (7, "NC", "n"), (12, "NC", "n")]),
    "TPS61170": dict(ref="U", fp="Package_SON:WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm", ds="TPS61170 SLVS789D (QFN-6 2x2, DRV)",
        left=[(5, "CTRL", "i"), (6, "VIN", "p"), (1, "FB", "i"), (2, "COMP", "u")],
        right=[(4, "SW", "u")], top=[], bottom=[(3, "GND", "p"), (7, "PAD", "p")]),
    "Berg_Floppy_Power_4P": dict(ref="J", fp="isa_bridge:Berg_171826-4_1x04_P2.50mm", ds="AMP 171826-4 / EI series, pin1 +5V red, 2-3 GND, 4 +12V yellow",
        left=[(1, "+5V", "u"), (2, "GND", "u"), (3, "GND", "u"), (4, "+12V", "u")], right=[], top=[], bottom=[]),
}

def isa_symbol():
    pin = json.load(open(pathlib.Path(__file__).parent.parent / "lib" / "isa_socket_pinout.json"))
    names = {int(k): re.sub(r"_\d+$", "", v["fn"]) for k, v in pin.items()}
    rows = [(n, names[n], "u") for n in range(1, 99)]
    return dict(ref="J", fp="isa_bridge:ISA", ds="ISA 16-bit socket (Sullins EBC49DCWN-S420), pin numbering from dISAppointment footprint",
                left=rows[:49], right=rows[49:], top=[], bottom=[])

PT = {"i": "input", "o": "output", "b": "bidirectional", "t": "tri_state", "p": "power_in", "w": "power_out", "n": "no_connect", "u": "passive"}
STEP = 2.54


def sym(name, d):
    L, R, T, B = d["left"], d["right"], d["top"], d["bottom"]
    h = max(len(L), len(R)) * STEP + STEP
    w = max(max(len(T), len(B), 1) * STEP + STEP, 17.78)
    x1, x2, y1, y2 = -w / 2, w / 2, h / 2, -h / 2
    o = [f'  (symbol "{name}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)']
    for k, v, y in (("Reference", d["ref"], y1 + 1.27), ("Value", name, y2 - 1.27), ("Footprint", d["fp"], 0), ("Datasheet", d["ds"], 0)):
        hide = "" if k in ("Reference", "Value") else " (hide yes)"
        o.append(f'    (property "{k}" "{v}" (at 0 {y:.2f} 0) (effects (font (size 1.27 1.27)){hide}))')
    o.append(f'    (symbol "{name}_0_1" (rectangle (start {x1:.2f} {y1:.2f}) (end {x2:.2f} {y2:.2f}) (stroke (width 0.254) (type default)) (fill (type background))))')
    o.append(f'    (symbol "{name}_1_1"')

    def pin(side, i, n, num, nm, t, count):
        if side == "L": x, y, a = x1 - 2.54, y1 - STEP - i * STEP, 0
        elif side == "R": x, y, a = x2 + 2.54, y1 - STEP - i * STEP, 180
        elif side == "T": x, y, a = x1 + STEP + i * STEP, y1 + 2.54, 270
        else: x, y, a = x1 + STEP + i * STEP, y2 - 2.54, 90
        o.append(f'      (pin {PT[t]} line (at {x:.2f} {y:.2f} {a}) (length 2.54) (name "{nm}" (effects (font (size 1.27 1.27)))) (number "{num}" (effects (font (size 1.27 1.27)))))')
    for side, lst in (("L", L), ("R", R), ("T", T), ("B", B)):
        for i, (num, nm, t) in enumerate(lst):
            pin(side, i, len(lst), num, nm, t, len(lst))
    o.append("    )")
    o.append("  )")
    return "\n".join(o)


def check():
    for name, d in PARTS.items():
        nums = [p[0] for side in ("left", "right", "top", "bottom") for p in d[side]]
        assert len(nums) == len(set(nums)), f"{name}: duplicate pin numbers {sorted(nums)}"
    p = PARTS["SN74LVCC4245A"]
    allp = {x[0]: x[1] for side in ("left", "right", "top", "bottom") for x in p[side]}
    assert sorted(allp) == list(range(1, 25)), "LVCC4245A must have pins 1-24"
    assert allp[3] == "A1" and allp[10] == "A8" and allp[21] == "B1" and allp[14] == "B8", allp
    c = PARTS["LTC3261"]; cp = {x[0]: x[1] for side in ("left", "right", "top", "bottom") for x in c[side]}
    assert cp[2] == "RT" and cp[4] == "VOUT" and cp[5] == "C-" and cp[8] == "C+" and cp[9] == "VIN" and cp[10] == "EN" and cp[11] == "MODE" and cp[13] == "GND"
    t = PARTS["TPS61170"]; tp = {x[0]: x[1] for side in ("left", "right", "top", "bottom") for x in t[side]}
    assert tp[1] == "FB" and tp[2] == "COMP" and tp[3] == "GND" and tp[4] == "SW" and tp[5] == "CTRL" and tp[6] == "VIN"


if __name__ == "__main__":
    PARTS["ISA_16bit_Socket"] = isa_symbol()
    check()
    out = ['(kicad_symbol_lib (version 20231120) (generator "mksym")']
    out += [sym(n, d) for n, d in PARTS.items()]
    out.append(")")
    dest = pathlib.Path(__file__).parent.parent / "lib" / "isa_bridge.kicad_sym"
    dest.write_text("\n".join(out) + "\n")
    print("wrote", dest, len(PARTS), "symbols")
