import sys, struct, re, collections
b = open(sys.argv[1], 'rb').read()
def nameseg(x): return len(x) == 4 and all((65 <= c <= 90) or c == 95 or (48 <= c <= 57) for c in x) and not (48 <= x[0] <= 57)
def eisa(v):
    v = struct.unpack('>I', struct.pack('<I', v))[0]
    return ''.join(chr(64 + ((v >> s) & 31)) for s in (26, 21, 16)) + '%04X' % (v & 0xFFFF)
def const(i):
    p = b[i]
    if p == 0x0A: return b[i+1], 2
    if p == 0x0B: return struct.unpack_from('<H', b, i+1)[0], 3
    if p == 0x0C: return struct.unpack_from('<I', b, i+1)[0], 5
    if p in (0, 1): return p, 1
    if p == 0xFF: return 0xFFFFFFFF, 1
    return None, 0
dev = '?'; hid = {}; res = collections.defaultdict(list); names = {}; regions = []
i = 36
while i < len(b) - 8:
    if b[i] == 0x5B and b[i+1] == 0x82:
        n = (b[i+2] >> 6) + 1
        seg = b[i+2+n:i+6+n]
        if nameseg(seg): dev = seg.decode() + '@%X' % i
    if b[i] == 0x08 and nameseg(b[i+1:i+5]):
        nm = b[i+1:i+5].decode(); v, l = const(i+5)
        if nm == '_HID':
            if v is not None and b[i+5] == 0x0C: hid[dev] = eisa(v)
            elif b[i+5] == 0x0D: hid[dev] = b[i+6:b.index(0, i+6)].decode('latin1')
        elif v is not None: names.setdefault(nm, v)
    if b[i] == 0x5B and b[i+1] == 0x80 and nameseg(b[i+2:i+6]):
        sp = b[i+6]; off, l = const(i+7)
        ln, l2 = const(i+7+l) if l else (None, 0)
        if l and b[i+7] not in (0, 1): regions.append((b[i+2:i+6].decode(), sp, off, ln, dev))
        elif not l: regions.append((b[i+2:i+6].decode(), sp, b[i+7:i+11].decode('latin1') if nameseg(b[i+7:i+11]) else '?', None, dev))
    if b[i] == 0x47 and b[i+1] in (0, 1) and b[i+6] in (0, 1, 2, 4, 8, 16) and 0 < b[i+7]:
        mn, mx = struct.unpack_from('<HH', b, i+2)
        if mn <= mx and mn >= 0x10 and (b[i+8] in (0x47, 0x22, 0x23, 0x2A, 0x79, 0x4B, 0x86, 0x87) ): res[dev].append('IO %X-%X' % (mn, mn + b[i+7] - 1) + ('' if mn == mx else ' (..%X)' % mx))
    if b[i] in (0x22, 0x23) and b[i + (3 if b[i] == 0x22 else 4)] in (0x47, 0x22, 0x23, 0x2A, 0x79, 0x4B):
        m, = struct.unpack_from('<H', b, i+1)
        if m: res[dev].append('IRQ ' + ','.join(str(k) for k in range(16) if m >> k & 1))
    if b[i] == 0x2A and b[i+3] in (0x47, 0x22, 0x23, 0x2A, 0x79):
        if b[i+1]: res[dev].append('DMA ' + ','.join(str(k) for k in range(8) if b[i+1] >> k & 1))
    i += 1
print('== named constants of interest')
for k in sorted(names):
    if re.match(r'(PM|GP|SMB|TCO|RCB|SRCB|MCH|SMI|APM|IO[0-9A-Z]|SP[0-9]|GPB|PMB|ICH|LPC|TPM|SIO|IOE|IOS|HWM|ACP|PCIB|PEB|EGP|ASS)', k): print(' %s=%X' % (k, names[k]), end='')
print('\n== SystemIO regions')
for r in regions:
    if r[1] == 1: print(' ', r[0], '%s' % (('%X' % r[2]) if isinstance(r[2], int) else r[2]), 'len', ('%X' % r[3]) if r[3] is not None else '?', 'in', r[4].split('@')[0])
print('== devices with I/O resources')
for d, r in res.items():
    u = []
    [u.append(x) for x in r if x not in u]
    print(' %-5s %-9s %s' % (d.split('@')[0], hid.get(d, ''), '; '.join(u)))
