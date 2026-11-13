# SPDX-License-Identifier: GPL-2.0-or-later
"""Checks ldrq and serirq programs (fw/pio/lpc_aux.pio) against a behavioural host. python3 fw/sim/test_aux.py"""
import os, random, sys
sys.path.insert(0, os.path.dirname(__file__))
from pio_sim import asm, SM, System

HERE = os.path.dirname(os.path.abspath(__file__))
CYC = 1000.0 / float(os.environ.get("SYSCLK", 400))
LCLK_NS = 30.0
LCLK, SERIRQ, LDRQ = 29, 30, 31


def split_programs(src):
    parts, cur, name = {}, [], None
    for ln in src.splitlines():
        if ln.startswith(".program"):
            if name: parts[name] = "\n".join(cur)
            name, cur = ln.split()[1], []
        elif name:
            cur.append(ln)
    parts[name] = "\n".join(cur)
    return parts


def lclk(t, phase):
    return 1 if ((t - phase) % LCLK_NS) < LCLK_NS / 2 and t >= phase else 0


def test_ldrq(ch, act, rnd):
    ops, lab = asm(split_programs(open(os.path.join(HERE, "..", "pio", "lpc_aux.pio")).read())["ldrq"])
    sysm = System(3)
    sm = SM("ldrq", ops, sysm.pios[2], sysm.bus, dict(in_base=24, out_base=LDRQ, out_count=1, wrap_target=0, wrap_top=len(ops) - 1))
    sm.out_val = 1 << LDRQ; sm.out_dir = 1 << LDRQ                  # idle high, driven
    phase = rnd.uniform(0, LCLK_NS) + 300
    w = ((ch >> 2 & 1) << 1) | ((ch >> 1 & 1) << 2) | ((ch & 1) << 3) | (act << 4) | (1 << 5)
    sm.tx.append(w)
    t = 0.0; seen = {}
    while t < phase + 20 * LCLK_NS:
        pad = (sm.out_val >> LDRQ) & 1
        sysm.bus.level = (lclk(t, phase) << LCLK)
        k = int((t - phase) // LCLK_NS) if t >= phase else -1
        tin = t - phase - k * LCLK_NS
        if k >= 0 and tin >= 15.0 and k not in seen:       # host samples at the falling edge
            seen[k] = pad
        sm.step(); t += CYC
    # message must start at some clock s: 0, ch2, ch1, ch0, act, 1
    bits = [seen[k] for k in sorted(seen)]
    s = ''.join(map(str, bits))
    want = "0" + format(ch, "03b") + str(act) + "1"
    return want in s, s, want


def test_serirq(mask, rnd, start_clocks=4, other=0):
    """Two SERIRQ cycles. other: frames 17-20 another device drives low in cycle 1 (must not be taken as the stop pulse)."""
    ops, lab = asm(split_programs(open(os.path.join(HERE, "..", "pio", "lpc_aux.pio")).read())["serirq"])
    sysm = System(3)
    sm = SM("serirq", ops, sysm.pios[2], sysm.bus, dict(in_base=24, out_base=SERIRQ, out_count=1, set_base=SERIRQ, set_count=1, tx_depth=4,
                                                       jmp_pin=SERIRQ, sideset=lab.get("__ss"), side_base=SERIRQ,
                                                       wrap_target=lab.get("__wt", 0), wrap_top=lab.get("__wp", len(ops) - 1)))
    sm.out_val = 0; sm.out_dir = 0
    sm.tx.append(mask)
    phase = rnd.uniform(0, LCLK_NS) + 300
    # per cycle: host low start_clocks, host high 1 clock (R), 21 frames of 3 clocks, stop 3 clocks low, 4 idle clocks
    R = start_clocks
    clen = R + 2 + 3 * 21 + 3 + 4
    host = {}                                   # clock -> level the host (or the other device) drives
    for cy in range(2):
        b0 = cy * clen
        for k in range(start_clocks): host[b0 + k] = 0
        host[b0 + R] = 1
        if cy == 0:
            for f in range(17, 21):
                if (other >> f) & 1: host[b0 + R + 2 + 3 * f] = 0; host[b0 + R + 3 + 3 * f] = 1
        s = b0 + R + 2 + 3 * 21 - 1             # stop pulse right after the last turn-around phase
        for k in range(3): host[s + k] = 0
        host[s + 3] = 1
    total = 2 * clen
    t = 0.0; got = {}
    while t < phase + (total + 6) * LCLK_NS:
        k = int((t - phase) // LCLK_NS) if t >= phase else -1
        tin = (t - phase - k * LCLK_NS) if k >= 0 else 0
        pad_dev = ((sm.out_val >> SERIRQ) & 1) if (sm.out_dir >> SERIRQ) & 1 else None
        drive = host.get(k)
        prev = host.get(k - 1, 1)
        if drive is not None and tin >= 6.0:
            lvl = drive
        elif drive is not None and prev is not None and host.get(k - 1) is not None:
            lvl = prev
        elif pad_dev is not None:
            lvl = pad_dev
        else:
            lvl = 1                                # pulled up
        if k < 0: lvl = 1
        sysm.bus.level = (lclk(t, phase) << LCLK) | (lvl << SERIRQ)
        if k >= 0 and tin >= 25.0 and k not in got:
            got[k] = lvl
        sm.step(); t += CYC
    ok = True; why = []
    for cy in range(2):
        for n in range(17):
            c = cy * clen + R + 2 + 3 * n
            want = 0 if (mask >> n) & 1 else 1
            if got.get(c) != want:
                ok = False; why.append("cycle %d frame %d clock %d got %r want %r" % (cy, n, c, got.get(c), want))
            rec = got.get(c + 1)
            if (mask >> n) & 1 and rec != 1:
                ok = False; why.append("cycle %d frame %d recovery %r" % (cy, n, rec))
            ta = got.get(c + 2)
            if ta != 1:
                ok = False; why.append("cycle %d frame %d turn-around %r" % (cy, n, ta))
    return ok, why[:3]


def main():
    rnd = random.Random(5); bad = 0
    for i in range(40):
        ch = rnd.randrange(8); act = rnd.randrange(2)
        ok, s, want = test_ldrq(ch, act, rnd)
        if not ok:
            bad += 1; print("LDRQ FAIL", ch, act, s, want)
    for i in range(40):
        mask = rnd.randrange(1 << 17)
        ok, why = test_serirq(mask, rnd, start_clocks=rnd.choice([4, 6, 8]), other=rnd.randrange(16) << 17 if i % 2 else 0)
        if not ok:
            bad += 1
            if bad < 6: print("SERIRQ FAIL", hex(mask), why)
    print("aux failures", bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
