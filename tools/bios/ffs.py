# top-level FFS files of a flash image: offsets, checksum check. usage: ffs.py image [offset ...]
import sys, struct, uuid
img = open(sys.argv[1], 'rb').read(); want = [int(x, 16) for x in sys.argv[2:]]
def sum8(b): return (-sum(b)) & 0xFF
files = []; o = 0; bad = 0
while True:
    i = img.find(b'_FVH', o)
    if i < 0: break
    base = i - 40
    if base < 0: o = i + 4; continue
    fvlen, = struct.unpack_from('<Q', img, base + 32); hlen, = struct.unpack_from('<H', img, base + 48); ext, = struct.unpack_from('<H', img, base + 52)
    if fvlen < 64 or base + fvlen > len(img) or hlen < 56: o = i + 4; continue
    if sum(struct.unpack_from('<%dH' % (hlen // 2), img, base)) & 0xFFFF: print('FV %X header checksum BAD' % base)
    p = base + hlen
    if ext: p = base + ((ext + struct.unpack_from('<I', img, base + ext + 16)[0] + 7) & ~7)
    while p + 24 <= base + fvlen:
        h = img[p:p+24]
        if h == b'\xff' * 24: break
        size = int.from_bytes(h[20:23], 'little'); attr = h[19]; hs = 24
        if attr & 1: size, = struct.unpack_from('<Q', img, p + 24); hs = 32
        if size < hs or p + size > base + fvlen: break
        hdr = bytearray(img[p:p+hs]); hdr[17] = 0; hdr[23] = 0
        hok = sum(hdr) & 0xFF == 0
        fck = h[17]; fok = (fck == sum8(img[p+hs:p+size])) if attr & 0x40 else fck == 0xAA
        if not (hok and fok): bad += 1
        files.append((p, size, hs, attr, str(uuid.UUID(bytes_le=h[:16])), h[18], hok, fok))
        p = (p + size + 7) & ~7
    o = base + fvlen
print(len(files), 'top-level files,', bad, 'with bad checksums')
for w in want:
    for p, size, hs, attr, g, t, hok, fok in files:
        if p <= w < p + size: print('offset %X in file @%X size %X type %02X attr %02X (%s) guid %s' % (w, p, size, t, attr, 'data checksum' if attr & 0x40 else 'fixed AA', g))
