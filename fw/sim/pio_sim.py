# SPDX-License-Identifier: GPL-2.0-or-later
"""Tiny RP2350 PIO simulator + assembler for the subset used by fw/pio/*.pio.
Own code, no external dependency. Instruction encodings follow the RP2350 datasheet (PIO v1)."""
import re

# ------------------------------------------------------------------ assembler
def asm(src):
    """returns (opcodes, labels). Supports: set/jmp/wait/in/out/mov/push/pull/irq/nop, [delay], labels, comments."""
    lines = []
    for ln in src.splitlines():
        ln = ln.split(";")[0].strip()
        if ln.startswith("public "):
            ln = ln[7:].strip()
        if ln.startswith(".wrap_target"):
            lines.append("@WT"); continue
        if ln.startswith(".wrap"):
            lines.append("@WP"); continue
        if ln.startswith(".side_set"):
            t = ln.split(); lines.append("@SS %s %d %d" % (t[1], "opt" in t, "pindirs" in t)); continue
        if not ln or ln.startswith("."):
            continue
        lines.append(ln)
    labels, prog = {}, []
    for ln in lines:
        if ln == "@WT": labels["__wt"] = len(prog); continue
        if ln == "@WP": labels["__wp"] = len(prog) - 1; continue
        if ln.startswith("@SS"): labels["__ss"] = tuple(int(v) for v in ln.split()[1:]); continue
        m = re.match(r"^(\w+):\s*(.*)$", ln)
        if m:
            labels[m.group(1)] = len(prog)
            ln = m.group(2)
            if not ln:
                continue
        prog.append(ln)
    ops = []
    ss_n, ss_opt, _ = labels.get("__ss", (0, 0, 0))
    for ln in prog:
        d = 0
        m = re.search(r"\[(\d+)\]\s*$", ln)
        if m:
            d = int(m.group(1)); ln = ln[:m.start()].strip()
        m = re.search(r"\sside\s+(\w+)\s*$", ln)
        sv = None
        if m:
            sv = int(m.group(1), 0); ln = ln[:m.start()].strip()
        if ss_n:   # side-set occupies the top bits of the delay field (opt: enable bit first)
            nb = ss_n + ss_opt
            assert d < (1 << (5 - nb)), "delay too large with side-set"
            hi = 0
            if sv is not None:
                hi = ((1 << ss_n) if ss_opt else 0) | sv
            else:
                assert ss_opt, "side-set required on every instruction"
            d |= hi << (5 - nb)
        tok = [t for t in re.split(r"[,\s]+", ln) if t]
        op = tok[0]
        def num(t): return int(t, 0)
        if op == "jmp":
            conds = {"": 0, "!x": 1, "x--": 2, "!y": 3, "y--": 4, "x!=y": 5, "pin": 6, "!osre": 7}
            if "!=" in tok:
                c, t = "x!=y", tok[-1]
            elif len(tok) == 2: c, t = "", tok[1]
            else: c, t = tok[1], tok[2]
            a = labels[t] if t in labels else num(t)
            w = 0x0000 | (conds[c] << 5) | a
        elif op == "wait":
            pol = num(tok[1]); src_ = tok[2]
            if src_ == "irq":
                idx = tok[3]; mode = 0
                if idx in ("prev", "next"):
                    mode = 1 if idx == "prev" else 3; idx = tok[4]
                w = 0x2000 | (pol << 7) | (2 << 5) | (mode << 3) | (num(idx) & 7)
            else:
                s = {"gpio": 0, "pin": 1, "jmppin": 3}[src_]
                w = 0x2000 | (pol << 7) | (s << 5) | num(tok[3])
        elif op == "in":
            s = {"pins": 0, "x": 1, "y": 2, "null": 3, "isr": 6, "osr": 7}[tok[1]]
            w = 0x4000 | (s << 5) | (num(tok[2]) & 31)
        elif op == "out":
            dst = {"pins": 0, "x": 1, "y": 2, "null": 3, "pindirs": 4, "pc": 5, "isr": 6, "exec": 7}[tok[1]]
            w = 0x6000 | (dst << 5) | (num(tok[2]) & 31)
        elif op == "push":
            w = 0x8000 | (0 << 7) | (1 << 5)
            if "noblock" in tok: w &= ~(1 << 5)
        elif op == "pull":
            w = 0x8000 | (1 << 7) | (1 << 5)
            if "noblock" in tok: w &= ~(1 << 5)
        elif op == "mov":
            dst = {"pins": 0, "x": 1, "y": 2, "pindirs": 3, "exec": 4, "pc": 5, "isr": 6, "osr": 7}[tok[1]]
            s = tok[2]; mop = 0
            if s.startswith("!") or s.startswith("~"): mop = 1; s = s[1:]
            elif s.startswith("::"): mop = 2; s = s[2:]
            src_ = {"pins": 0, "x": 1, "y": 2, "null": 3, "status": 5, "isr": 6, "osr": 7}[s]
            w = 0xA000 | (dst << 5) | (mop << 3) | src_
        elif op == "irq":
            rest = tok[1:]; mode = 0; clr = 0; wt = 0
            while rest and rest[0] in ("prev", "next", "set", "nowait", "wait", "clear"):
                k = rest.pop(0)
                if k == "prev": mode = 1
                elif k == "next": mode = 3
                elif k == "clear": clr = 1
                elif k == "wait": wt = 1
            w = 0xC000 | (clr << 6) | (wt << 5) | (mode << 3) | (num(rest[0]) & 7)
        elif op == "set":
            dst = {"pins": 0, "x": 1, "y": 2, "pindirs": 4}[tok[1]]
            w = 0xE000 | (dst << 5) | (num(tok[2]) & 31)
        elif op == "nop":
            w = 0xA000 | (2 << 5) | 2  # mov y, y
        else:
            raise ValueError(ln)
        ops.append(w | (d << 8))
    return ops, labels


# ------------------------------------------------------------------ simulator
M32 = 0xFFFFFFFF


class Bus:
    """shared pins: 32 PIO-view pins. value = pad level seen by every SM (host drives + SM outputs resolved by caller)"""
    def __init__(self):
        self.level = 0


class SM:
    def __init__(self, name, ops, pio, bus, cfg):
        self.name, self.ops, self.pio, self.bus = name, ops, pio, bus
        self.pc = cfg.get("start", 0)
        self.ss = cfg.get("sideset")            # (count, opt, pindirs) from asm labels["__ss"]
        self.side_base = cfg.get("side_base", 0)
        self.x = self.y = 0
        self.isr = 0; self.isr_n = 0; self.osr = 0; self.osr_n = 32
        self.rx = []; self.tx = []
        self.rx_depth = cfg.get("rx_depth", 4); self.tx_depth = cfg.get("tx_depth", 4)
        self.in_base = cfg.get("in_base", 0); self.out_base = cfg.get("out_base", 0); self.out_count = cfg.get("out_count", 0)
        self.set_base = cfg.get("set_base", 0); self.set_count = cfg.get("set_count", 0)
        self.jmp_pin = cfg.get("jmp_pin", 0)
        self.in_left = cfg.get("isr_left", False)          # shift ISR left?
        self.autopush = cfg.get("autopush", 0)             # threshold bits, 0 = off
        self.out_right = cfg.get("osr_right", True)
        self.status_tx_lt = cfg.get("status_tx_lt", 1)
        self.out_val = 0        # level latch of the OUT/SET mapped pins (bit positions in system pins)
        self.out_dir = 0
        self.stall = False
        self.delay = 0
        self.wrap_top = cfg.get("wrap_top", len(ops) - 1); self.wrap_target = cfg.get("wrap_target", 0)
        self.instr_count = 0
        self.last_exec = {}

    # helpers
    def pin(self, i):
        return (self.bus.level >> ((self.in_base + i) % 32)) & 1

    def read_pins(self, n):
        v = 0
        for i in range(n):
            v |= self.pin(i) << i
        return v

    def set_pins(self, base, count, val):
        for i in range(count):
            b = (base + i) % 32
            self.out_val = (self.out_val & ~(1 << b)) | (((val >> i) & 1) << b)

    def set_dirs(self, base, count, val):
        for i in range(count):
            b = (base + i) % 32
            self.out_dir = (self.out_dir & ~(1 << b)) | (((val >> i) & 1) << b)

    def shift_in(self, val, n):
        mask = (1 << n) - 1 if n < 32 else M32
        val &= mask
        if self.in_left:
            self.isr = ((self.isr << n) | val) & M32
        else:
            self.isr = ((self.isr >> n) | (val << (32 - n))) & M32
        self.isr_n = min(32, self.isr_n + n)
        if self.autopush and self.isr_n >= self.autopush:
            if len(self.rx) < self.rx_depth: self.rx.append(self.isr)
            self.isr = 0; self.isr_n = 0

    def shift_out(self, n):
        if n == 0: n = 32
        if self.out_right:
            v = self.osr & ((1 << n) - 1) if n < 32 else self.osr
            self.osr = (self.osr >> n) & M32
        else:
            v = (self.osr >> (32 - n)) & ((1 << n) - 1) if n < 32 else self.osr
            self.osr = (self.osr << n) & M32
        self.osr_n = min(32, self.osr_n + n)
        return v

    def irq_flags(self, mode):
        # mode 0 this pio, 1 prev, 3 next
        idx = self.pio.index
        if mode == 1: idx = (idx - 1) % len(self.pio.sys.pios)
        elif mode == 3: idx = (idx + 1) % len(self.pio.sys.pios)
        return self.pio.sys.pios[idx].irq

    def status(self):
        return M32 if len(self.tx) < self.status_tx_lt else 0

    def step(self):
        """one system clock"""
        if self.delay > 0:
            self.delay -= 1
            return
        w = self.ops[self.pc]
        op = w >> 13
        dly = (w >> 8) & 31
        if self.ss:   # side-set takes effect when the instruction issues, stalled or not
            n, opt, dirs = self.ss; nb = n + opt
            hi = dly >> (5 - nb); dly &= (1 << (5 - nb)) - 1
            if not opt or (hi >> n) & 1:
                (self.set_dirs if dirs else self.set_pins)(self.side_base, n, hi & ((1 << n) - 1))
        arg = w & 0xFF
        nxt = self.wrap_target if self.pc == self.wrap_top else self.pc + 1   # hardware wraps only from the .wrap instruction
        stall = False
        if op == 0:  # jmp
            c = (arg >> 5) & 7; a = arg & 31
            take = False
            if c == 0: take = True
            elif c == 1: take = (self.x == 0)
            elif c == 2: take = (self.x != 0); self.x = (self.x - 1) & M32
            elif c == 3: take = (self.y == 0)
            elif c == 4: take = (self.y != 0); self.y = (self.y - 1) & M32
            elif c == 5: take = (self.x != self.y)
            elif c == 6: take = bool((self.bus.level >> self.jmp_pin) & 1)
            elif c == 7: take = self.osr_n < 32  # !osre approx
            if take: nxt = a
        elif op == 1:  # wait
            pol = (arg >> 7) & 1; s = (arg >> 5) & 3; idx = arg & 31
            if s == 0: cur = (self.bus.level >> idx) & 1
            elif s == 1: cur = self.pin(idx)
            elif s == 3: cur = (self.bus.level >> ((self.jmp_pin + idx) % 32)) & 1
            else:
                mode = (idx >> 3) & 3; n = idx & 7
                fl = self.irq_flags(mode); cur = (fl >> n) & 1
            if cur == pol:
                if s == 2 and pol == 1:
                    mode = (idx >> 3) & 3; n = idx & 7
                    idxp = self.pio.index
                    if mode == 1: idxp = (idxp - 1) % len(self.pio.sys.pios)
                    elif mode == 3: idxp = (idxp + 1) % len(self.pio.sys.pios)
                    self.pio.sys.pios[idxp].irq &= ~(1 << n)
            else:
                stall = True
        elif op == 2:  # in
            s = (arg >> 5) & 7; n = arg & 31 or 32
            if s == 0: v = self.read_pins(n)
            elif s == 1: v = self.x
            elif s == 2: v = self.y
            elif s == 3: v = 0
            elif s == 6: v = self.isr
            else: v = self.osr
            self.shift_in(v, n)
        elif op == 3:  # out
            d = (arg >> 5) & 7; n = arg & 31 or 32
            v = self.shift_out(n)
            if d == 0: self.set_pins(self.out_base, self.out_count, v)
            elif d == 1: self.x = v
            elif d == 2: self.y = v
            elif d == 4: self.set_dirs(self.out_base, self.out_count, v)
            elif d == 5: nxt = v & 31
            elif d == 6: self.isr = v; self.isr_n = n
        elif op == 4:  # push/pull
            if (arg >> 7) & 1:  # pull
                blk = (arg >> 5) & 1
                if self.tx:
                    self.osr = self.tx.pop(0); self.osr_n = 0
                elif blk:
                    stall = True
                else:
                    self.osr = self.x; self.osr_n = 0
            else:
                blk = (arg >> 5) & 1
                if len(self.rx) < self.rx_depth:
                    self.rx.append(self.isr); self.isr = 0; self.isr_n = 0
                elif blk:
                    stall = True
                else:
                    self.isr = 0; self.isr_n = 0
        elif op == 5:  # mov
            d = (arg >> 5) & 7; mo = (arg >> 3) & 3; s = arg & 7
            if s == 0: v = self.read_pins(32)
            elif s == 1: v = self.x
            elif s == 2: v = self.y
            elif s == 3: v = 0
            elif s == 5: v = self.status()
            elif s == 6: v = self.isr
            else: v = self.osr
            if mo == 1: v = (~v) & M32
            elif mo == 2: v = int(format(v, "032b")[::-1], 2)
            if d == 0: self.set_pins(self.out_base, self.out_count, v)
            elif d == 1: self.x = v
            elif d == 2: self.y = v
            elif d == 3: self.set_dirs(self.out_base, self.out_count, v)
            elif d == 5: nxt = v & 31
            elif d == 6: self.isr = v; self.isr_n = 0
            elif d == 7: self.osr = v; self.osr_n = 0
        elif op == 6:  # irq
            clr = (arg >> 6) & 1; wt = (arg >> 5) & 1; mode = (arg >> 3) & 3; n = arg & 7
            idxp = self.pio.index
            if mode == 1: idxp = (idxp - 1) % len(self.pio.sys.pios)
            elif mode == 3: idxp = (idxp + 1) % len(self.pio.sys.pios)
            if clr: self.pio.sys.pios[idxp].irq &= ~(1 << n)
            else: self.pio.sys.pios[idxp].irq |= (1 << n)
        elif op == 7:  # set
            d = (arg >> 5) & 7; v = arg & 31
            if d == 0: self.set_pins(self.set_base, self.set_count, v)
            elif d == 1: self.x = v
            elif d == 2: self.y = v
            elif d == 4: self.set_dirs(self.set_base, self.set_count, v)
        self.instr_count += 1
        if not stall:
            self.pc = nxt
            self.delay = dly
            self.last_exec[self.pc] = self.instr_count


class PIOBlock:
    def __init__(self, sys_, index):
        self.sys = sys_; self.index = index; self.irq = 0; self.sms = []


class System:
    def __init__(self, nblocks=3):
        self.pios = [PIOBlock(self, i) for i in range(nblocks)]
        self.bus = Bus()
