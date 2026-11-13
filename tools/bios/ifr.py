import sys, struct, re
b = open(sys.argv[1], 'rb').read(); want = re.compile(sys.argv[2], re.I)
# string packages (en-US)
strs = {}
for m in re.finditer(rb'en-US\x00', b):
    p = m.start() - 0x2E
    if p < 0 or b[p+3] != 0x04: continue
    ln = int.from_bytes(b[p:p+3], 'little'); hs, so = struct.unpack_from('<II', b, p + 4)
    q = p + so; sid = 1; tbl = {}
    while q < p + ln:
        t = b[q]
        if t == 0x00: break
        if t == 0x14:
            e = q + 1
            while b[e:e+2] != b'\0\0': e += 2
            tbl[sid] = b[q+1:e].decode('utf-16le', 'replace'); sid += 1; q = e + 2
        elif t == 0x10:
            e = b.index(0, q + 1); tbl[sid] = b[q+1:e].decode('latin1'); sid += 1; q = e + 1
        elif t == 0x20: sid += b[q+1]; q += 2
        elif t == 0x21: sid += struct.unpack_from('<H', b, q+1)[0]; q += 3
        elif t == 0x22: sid += 1; q += 3
        elif t == 0x30: q += 4 + struct.unpack_from('<I', b, q+1)[0] if False else struct.unpack_from('<H', b, q+2)[0] + 0
        else: break
    if len(tbl) > len(strs): strs = tbl
print('strings', len(strs), file=sys.stderr)
# form packages
for m in re.finditer(rb'\x02\x0e[\x80-\xff]', b):
    p = m.start() - 3; ln = int.from_bytes(b[p:p+3], 'little')
    if ln < 100 or p + ln > len(b): continue
    q = p + 4; cur = None; suppress = 0; depth = []
    while q < p + ln:
        op = b[q]; l = b[q+1] & 0x7F; scope = b[q+1] >> 7
        if l < 2: break
        if op in (0x05, 0x06, 0x07):   # one-of, checkbox, numeric
            prompt, hlp, qid, vs, off = struct.unpack_from('<HHHHH', b, q + 2)
            name = strs.get(prompt, '?')
            cur = name if want.search(name) else None
            if cur:
                extra = ''
                if op == 0x06: extra = ' [checkbox default=%d]' % (b[q+13] & 1)
                print('%s  (varstore %X off %X)%s%s' % (name.strip(), vs, off, extra, '  {inside suppress/grayout}' if suppress else ''))
        elif op == 0x09 and cur:
            sid, fl, ty = struct.unpack_from('<HBB', b, q + 2); val = int.from_bytes(b[q+6:q+l], 'little')
            print('      %s%-28s = %X' % ('*' if fl & 0x10 else ' ', strs.get(sid, '?').strip()[:28], val))
        elif op == 0x5B and cur:      # default
            did, ty = struct.unpack_from('<HB', b, q + 2); print('      default[%d] = %X' % (did, int.from_bytes(b[q+5:q+l], 'little')))
        if op in (0x0A, 0x19): 
            if scope: suppress += 1; depth.append(1)
        elif scope: depth.append(0)
        if op == 0x29 and depth:
            if depth.pop(): suppress -= 1
        q += l
