# SPDX-License-Identifier: GPL-2.0-or-later
# minimal UEFI flash image walker: descriptor regions, firmware volumes, FFS files, sections (LZMA GUIDed, FV-in-FV)
import sys, struct, lzma, uuid, os, json
img = open(sys.argv[1], 'rb').read(); out = sys.argv[2]; os.makedirs(out, exist_ok=True)
LZMA_GUID = uuid.UUID('ee4e5898-3914-4259-9d6e-dc7bd79403cf')
mods = []
def guid(b): return str(uuid.UUID(bytes_le=bytes(b)))
def sections(data, ctx, depth):
    o = 0
    while o + 4 <= len(data):
        size = int.from_bytes(data[o:o+3], 'little'); typ = data[o+3]; hdr = 4
        if size == 0xFFFFFF: size = struct.unpack_from('<I', data, o+4)[0]; hdr = 8
        if size < hdr or o + size > len(data): break
        body = data[o+hdr:o+size]
        if typ == 0x02:
            g = uuid.UUID(bytes_le=bytes(body[:16])); doff = struct.unpack_from('<H', body, 16)[0]
            payload = data[o+doff:o+size]
            if g == LZMA_GUID:
                try: sections(lzma.decompress(payload, format=lzma.FORMAT_ALONE), ctx, depth+1)
                except Exception as e: ctx['err'] = 'lzma ' + str(e)
            else:
                ctx.setdefault('guided', []).append(str(g)); sections(payload, ctx, depth+1)
        elif typ == 0x01:
            ulen, ctype = struct.unpack_from('<IB', body, 0)
            if ctype == 0: sections(body[5:], ctx, depth+1)
            else: ctx['err'] = 'tiano-compressed %d' % ulen
        elif typ == 0x17: volumes(body, depth+1)
        elif typ == 0x15: ctx['name'] = body.decode('utf-16le', 'ignore').rstrip('\0')
        elif typ in (0x10, 0x12): ctx.setdefault('code', []).append((typ, bytes(body)))
        elif typ in (0x19, 0x18): ctx.setdefault('raw', []).append(bytes(body[16:] if typ == 0x18 else body))
        o = (o + size + 3) & ~3
def volumes(data, depth):
    o = 0
    while True:
        i = data.find(b'_FVH', o)
        if i < 40: 
            if i < 0: break
            o = i + 4; continue
        base = i - 40
        fvlen, = struct.unpack_from('<Q', data, base + 32); hlen, = struct.unpack_from('<H', data, base + 48)
        ext, = struct.unpack_from('<H', data, base + 52)
        if fvlen < 64 or base + fvlen > len(data) or hlen < 56: o = i + 4; continue
        fv = data[base:base+fvlen]; p = hlen
        if ext:
            esz, = struct.unpack_from('<I', fv, ext + 16); p = (ext + esz + 7) & ~7
        while p + 24 <= len(fv):
            h = fv[p:p+24]
            if h == b'\xff' * 24: break
            size = int.from_bytes(h[20:23], 'little'); typ = h[18]; attr = h[19]; hsz = 24
            if attr & 1: size, = struct.unpack_from('<Q', fv, p + 24); hsz = 32
            if size < hsz or p + size > len(fv): break
            ctx = {'guid': guid(h[:16]), 'type': typ, 'size': size, 'depth': depth}
            body = fv[p+hsz:p+size]
            if typ == 0x01: ctx['raw'] = [bytes(body)]
            elif typ != 0xF0: sections(body, ctx, depth)
            mods.append(ctx)
            p = (p + size + 7) & ~7
        o = base + fvlen
volumes(img, 0)
idx = []
for n, m in enumerate(mods):
    name = m.get('name') or m['guid']
    safe = ''.join(c if c.isalnum() or c in '-_.' else '_' for c in name)
    for k, (t, b) in enumerate(m.get('code', [])): open(f'{out}/{n:04d}_{safe}.{"te" if t == 0x12 else "pe"}', 'wb').write(b)
    for k, b in enumerate(m.get('raw', [])): open(f'{out}/{n:04d}_{safe}.raw{k}', 'wb').write(b)
    idx.append({k: v for k, v in m.items() if k not in ('code', 'raw')} | {'ncode': len(m.get('code', [])), 'nraw': len(m.get('raw', []))})
json.dump(idx, open(out + '/index.json', 'w'), indent=0)
print(len(mods), 'files;', sum(1 for m in mods if m.get('err')), 'with errors')
