# SPDX-License-Identifier: GPL-2.0-or-later
"""Cycle-accurate-ish check of lpc_fe.pio + lpc_tx.pio (script player) against a reactive behavioural LPC host.
Covers I/O read/write and 8 and 16 bit DMA read/write. Randomises LCLK phase against the system clock, host launch delay
(2..11 ns) and CPU reply latency. The CPU model builds the reply scripts exactly like src/lpc.c.
Run: python3 fw/sim/test_lpc.py     (env: N trials, SEED, SYSCLK MHz)"""
import os, random, sys
sys.path.insert(0, os.path.dirname(__file__))
from pio_sim import asm, SM, System

HERE = os.path.dirname(os.path.abspath(__file__))
SYSCLK_MHZ = float(os.environ.get("SYSCLK", 400))
CYC = 1000.0 / SYSCLK_MHZ
LCLK_NS = 30.0
LAD0, LFRAME, LCLK = 24, 28, 29
PAD_DELAY = 2.0
WAIT_SYNC = 6                       # 0110
DUTY = float(os.environ.get("DUTY", 0.5))          # LCLK high fraction (PCI 33 MHz: 11 ns min high and low = 0.37..0.63)
CLAIM_MAX = float(os.environ.get("CLAIM_MAX", 40))   # core 1 claim latency, sys clocks after the front end push


def load(name):
    return asm(open(os.path.join(HERE, "..", "pio", name)).read())


# ---- script words, same encoding as lpc.c -------------------------------------------------
D, R = 0xF, 0x0
_, TL = load("lpc_tx.pio")
NX, END, RESUME, SAMP = TL["pullp"], TL["start"], TL["wloop"], TL["samp"]   # flags are player addresses (out pc)
def W(nib, dirs=D, flag=NX): return (nib & 15) | (dirs << 4) | (flag << 8)

def script_ior(d):              # sync ready, data, TAR
    return [W(0), W(d & 15), W(d >> 4), W(0xF), W(0, R, END)]
def script_ack(sync):           # sync, TAR   (I/O write, DMA read 8 bit)
    return [W(sync), W(0xF), W(0, R, END)]
def script_dmaw8(sync, d):
    return [W(sync), W(d & 15), W(d >> 4), W(0xF), W(0, R, END)]
def script_dmaw16(d):           # sync more, byte0, sync ready, byte1, TAR
    return [W(9), W(d & 15), W((d >> 4) & 15), W(0), W((d >> 8) & 15), W((d >> 12) & 15), W(0xF), W(0, R, END)]
def script_dmar16_a():          # sync more, TAR, host byte 1 (SAMPLE2 = two clocks), host TAR, back to wait states
    return [W(9), W(0xF), W(0, R), W(0, R, SAMP), W(0, R), W(0, R), W(WAIT_SYNC, D, RESUME)]


class Host:
    """Reactive LPC host: ops list consumed one clock at a time (wait ops consume clocks while the target says 5/6)."""
    def __init__(self, ops, phase, rnd):
        self.ops, self.i, self.phase, self.rnd = ops, 0, phase, rnd
        self.k = -1
        self.cur = None
        self.drive_val = None
        self.lframe = 1
        self.tval = 5.0
        self.samples = []
        self.errors = []
        self.syncs = []
        self.waits = 0
        self.floats = 0
        self.done = False

    def edge(self, k): return self.phase + k * LCLK_NS

    def begin(self, k):
        self.k = k
        self.tval = self.rnd.uniform(2.0, 11.0)
        self.prev = (self.drive_val, self.lframe)
        if self.i >= len(self.ops):
            self.cur = ("end",); self.drive_val = None; self.lframe = 1; self.done = True
            return
        op = self.ops[self.i]
        self.cur = op
        if op[0] == "start": self.drive_val, self.lframe = (op[1] if len(op) > 1 else 0), 0
        elif op[0] == "d": self.drive_val, self.lframe = op[1], 1
        else: self.drive_val, self.lframe = None, 1

    def end(self, k, sample):
        op = self.cur
        self.samples.append((k, op[0], sample))
        if op[0] in ("start", "d", "z"):
            self.i += 1
        elif op[0] == "wait":
            if sample == 0xF and not self.syncs and self.floats < 2:     # SYNC may start up to 2 clocks late (LPC 1.1 4.2.1.9)
                self.floats += 1
            elif sample in (5, 6):
                self.waits += 1
                if self.waits > 60: self.errors.append("sync wait too long"); self.i = len(self.ops)
            else:
                self.syncs.append(sample); self.waits = 0
                if sample not in op[1]: self.errors.append("sync %x not in %s" % (sample, op[1]))
                self.i += 1
        elif op[0] == "exp":
            if sample != op[1]: self.errors.append("clock %d expected %x got %x (%s)" % (k, op[1], sample, op[2] if len(op) > 2 else ""))
            self.i += 1

    def lad_at(self, t):
        """what the host drives at time t: before launch delay the previous clock's value is still there"""
        k = int((t - self.phase) // LCLK_NS) if t >= self.phase else -1
        if k < 0 or k != self.k: return (None, 1)
        tin = t - self.edge(k)
        if tin < self.tval: return self.prev
        return (self.drive_val, self.lframe)


def nib(v, n): return [(v >> (4 * (n - 1 - i))) & 15 for i in range(n)]


def build(kind, addr, data, ch):
    """returns host op list"""
    S = [("start",)]
    if kind == "ior":
        S += [("d", 0)] + [("d", n) for n in nib(addr, 4)] + [("d", 0xF), ("z",)]
        S += [("wait", (0,))]
        S += [("exp", data & 15, "dlo"), ("exp", data >> 4, "dhi"), ("exp", 0xF, "TAR1"), ("exp", 0xF, "TAR2")]
    elif kind == "iow":
        S += [("d", 2)] + [("d", n) for n in nib(addr, 4)] + [("d", data & 15), ("d", data >> 4), ("d", 0xF), ("z",)]
        S += [("wait", (0,)), ("exp", 0xF, "TAR1"), ("exp", 0xF, "TAR2")]
    elif kind == "dmar8":
        S += [("d", 8), ("d", ch | 8), ("d", 0), ("d", data & 15), ("d", data >> 4), ("d", 0xF), ("z",)]
        S += [("wait", (0, 9)), ("exp", 0xF, "TAR1"), ("exp", 0xF, "TAR2")]
    elif kind == "dmaw8":
        S += [("d", 0xA), ("d", ch), ("d", 0), ("d", 0xF), ("z",)]
        S += [("wait", (0, 9)), ("exp", data & 15, "dlo"), ("exp", data >> 4, "dhi"), ("exp", 0xF, "TAR1"), ("exp", 0xF, "TAR2")]
    elif kind == "dmar16":
        S += [("d", 8), ("d", ch | 8), ("d", 1), ("d", data & 15), ("d", (data >> 4) & 15), ("d", 0xF), ("z",)]
        S += [("wait", (9,)), ("exp", 0xF, "TAR1"), ("exp", 0xF, "TAR2")]
        S += [("d", (data >> 8) & 15), ("d", (data >> 12) & 15), ("d", 0xF), ("z",)]
        S += [("wait", (0,)), ("exp", 0xF, "TAR1"), ("exp", 0xF, "TAR2")]
    elif kind == "dmaw16":
        S += [("d", 0xA), ("d", ch | 8), ("d", 1), ("d", 0xF), ("z",)]
        S += [("wait", (9,)), ("exp", data & 15, "b0lo"), ("exp", (data >> 4) & 15, "b0hi"), ("exp", 0, "sync2"),
              ("exp", (data >> 8) & 15, "b1lo"), ("exp", (data >> 12) & 15, "b1hi"), ("exp", 0xF, "TAR1"), ("exp", 0xF, "TAR2")]
    return S


CLAIM = W(WAIT_SYNC, D, RESUME)


def run_cycle(kind, addr, data, ch, rnd, cpu_lat, claim_lat=None):
    fe_ops, fl = load("lpc_fe.pio")
    tx_ops, tl = load("lpc_tx.pio")
    sysm = System(3)
    fe = SM("fe", fe_ops, sysm.pios[0], sysm.bus, dict(in_base=24, isr_left=True, osr_right=True, rx_depth=8, start=fl["idle"],
                                                      wrap_target=fl["__wt"], wrap_top=fl["__wp"]))
    tx = SM("tx", tx_ops, sysm.pios[1], sysm.bus,
            dict(in_base=24, out_base=24, out_count=4, set_base=24, set_count=4, jmp_pin=LFRAME, tx_depth=4, rx_depth=4,
                 isr_left=False, autopush=8, sideset=tl.get('__ss'), side_base=LAD0, wrap_target=tl.get('__wt', 0), wrap_top=tl.get('__wp', len(tx_ops) - 1)))
    tx.y = WAIT_SYNC
    sysm.pios[0].sms.append(fe); sysm.pios[1].sms.append(tx)

    unclaimed = kind in ("skip", "tpm")
    if unclaimed:   # another device's I/O read: the target must leave LAD alone, then return to idle on the next START
        ops = [("start", 5 if kind == "tpm" else 0), ("d", 0)] + [("d", n) for n in nib(addr, 4)] + [("d", 0xF), ("z",)] + [("exp", 0xF, "float")] * 4 + [("z",)] * 2 + [("start",), ("z",)]
    else:
        ops = build(kind, addr, data, ch)
    if claim_lat is None: claim_lat = rnd.uniform(14, CLAIM_MAX)
    host = Host(ops, rnd.uniform(0, LCLK_NS) + 40 * LCLK_NS, rnd)
    t = 0.0
    tend = host.edge(len(ops) + 130)
    pad = []
    lad_dir = 0; lad_val = 0
    errors = []
    cpu_state = None            # (due_time, action)
    cpu_q = []; q_due = 0.0
    seen_words = []
    last_k = -1
    sample_done = set()
    dmar16_sent_a = False
    while t < tend and not host.done:
        while pad and pad[0][0] <= t:
            _, lad_val, lad_dir = pad.pop(0)
        k = int((t - host.phase) // LCLK_NS) if t >= host.phase else -1
        # clock bookkeeping
        if k != last_k and k >= 0:
            host.begin(k); last_k = k
        hl, hframe = host.lad_at(t) if k >= 0 else (None, 1)
        clk = 1 if (k >= 0 and ((t - host.phase) % LCLK_NS) < LCLK_NS * DUTY) else 0
        if unclaimed and lad_dir: errors.append("drove LAD on an unclaimed cycle, clock %d" % k)
        if hl is not None and lad_dir and lad_val != hl:
            errors.append("contention t=%.1f clock %d host=%x target=%x" % (t, k, hl, lad_val))
        lad = hl if hl is not None else (lad_val if lad_dir else 0xF)
        if hl is None and not lad_dir: lad = 0xF
        sysm.bus.level = (lad << LAD0) | (hframe << LFRAME) | (clk << LCLK)
        # end-of-clock sample (edge - 7 ns)
        if k >= 0 and k not in sample_done and (host.edge(k + 1) - t) <= 7.0 + 1e-9:
            sample_done.add(k)
            host.end(k, lad)
        # CPU model
        if cpu_state is None and fe.rx:
            w = fe.rx.pop(0); seen_words.append(w)
            if not unclaimed:
                cpu_q.append(CLAIM); q_due = t + claim_lat * CYC        # core 1
                cpu_state = (t + (claim_lat + cpu_lat) * CYC, ("fe", w))
        if cpu_state is None and kind == "dmar16" and dmar16_sent_a and tx.rx:
            byte = tx.rx.pop(0) >> 24
            if byte != (data >> 8) & 0xFF: errors.append("dmar16 byte1 %02x want %02x" % (byte, (data >> 8) & 0xFF))
            cpu_state = (t + cpu_lat * CYC, ("dmar16_b", byte))
        if cpu_state is not None and t >= cpu_state[0]:
            typ, w = cpu_state[1]
            cpu_state = None
            if typ == "fe":
                if ((w >> 24) & 15) != 0xF:                          # I/O write
                    a = (w >> 8) & 0xFFFF; d = ((w & 15) << 4) | ((w >> 4) & 15)
                    if (a, d) != (addr, data): errors.append("iow word %08x" % w)
                    cpu_q += script_ack(0)
                else:
                    ty = (w >> 16) & 15
                    if ty == 0:
                        if (w & 0xFFFF) != addr: errors.append("ior addr %04x" % (w & 0xFFFF))
                        cpu_q += script_ior(data)
                    elif ty == 8:
                        chn = (w >> 12) & 7; size = (w >> 8) & 15
                        b0 = ((w >> 4) & 15) | ((w & 15) << 4)
                        if chn != (ch | 8) & 7 and chn != ch: errors.append("dmar ch %d" % chn)
                        if size == 0:
                            if b0 != data: errors.append("dmar8 data %02x" % b0)
                            cpu_q += script_ack(0)
                        else:
                            if b0 != data & 0xFF: errors.append("dmar16 b0 %02x" % b0)
                            cpu_q += script_dmar16_a(); dmar16_sent_a = True
                    elif ty == 0xF and ((w >> 8) & 15) == 0xA:
                        chn = (w >> 4) & 7; size = w & 15
                        if size == 0: cpu_q += script_dmaw8(0, data & 0xFF)
                        else: cpu_q += script_dmaw16(data & 0xFFFF)
                    else:
                        errors.append("unknown word %08x" % w)
            elif typ == "dmar16_b":
                cpu_q += script_ack(0)
        if cpu_q and t >= q_due and len(tx.tx) < tx.tx_depth:      # put(): wait for room, ~4 cycles per word
            tx.tx.append(cpu_q.pop(0)); q_due = t + 4 * CYC
        fe.step(); tx.step()
        new_dir = 1 if (tx.out_dir >> LAD0) & 15 == 15 else 0
        new_val = (tx.out_val >> LAD0) & 15
        if (new_dir, new_val) != (lad_dir, lad_val) and (not pad or pad[-1][1:] != (new_val, new_dir)):
            pad.append((t + PAD_DELAY, new_val, new_dir))
        t += CYC
    if kind == "tpm" and seen_words: errors.append("front end took a TPM cycle (START 0101)")
    if unclaimed and tx.pc != tl["start"]: errors.append("tx not back at start (pc %d)" % tx.pc)
    errors += host.errors
    if not host.done: errors.append("host did not finish (script stalled at op %d: %s)" % (host.i, host.ops[host.i] if host.i < len(host.ops) else None))
    return errors, host


def main():
    rnd = random.Random(int(os.environ.get("SEED", "1")))
    n = int(os.environ.get("N", "300"))
    kinds = ["ior", "iow", "dmar8", "dmaw8", "dmar16", "dmaw16", "skip", "tpm"]
    bad = 0
    for i in range(n):
        kind = kinds[i % len(kinds)] if os.environ.get("ROUND") else rnd.choice(kinds)
        addr = rnd.randrange(0x10000); data = rnd.randrange(65536 if kind.endswith("16") else 256)
        ch = rnd.choice([0, 1, 2, 3]) if not kind.endswith("16") else rnd.choice([5, 6, 7])
        lat = rnd.choice([0, 20, 60, 120, 400])
        errs, host = run_cycle(kind, addr, data, ch & 7, rnd, lat)
        if errs:
            bad += 1
            if bad <= 6: print("FAIL", kind, hex(addr), hex(data), ch, "lat", lat, errs[:3])
    print("trials", n, "failures", bad, "sysclk", SYSCLK_MHZ)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
