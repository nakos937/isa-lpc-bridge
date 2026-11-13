# Free LPC resources in ASRock X99M Killer/3.1 BIOS P4.01 (X99MK3_4.01). Does not flash anything.
#   python3 patch_x99m.py X99MK3_4.01 X99MK3_4.01.isalpc
# Every patch site is checked for the expected bytes first; the sites are inside uncompressed PEIM files with fixed 0xAA
# data checksums (attr 00), so no checksum changes. Not patched on purpose: Super I/O hardware monitor window (fans).
# The SBPEI GPIO table (E3C168, low byte = pin number, pin 8 absent) already leaves GPIO23/LDRQ1# native: nothing to do there.
import hashlib, sys
SRC_SHA256 = '8b38bc5be26f934e77b3329b67f657641f2cf3f39af30c894dff2e5733e0eca3'
PATCHES = [  # (offset, expected, new, what)
    (0xE1D271, '03', '01', 'LPC_EN 3803 -> 3801: COMB decode (2F8) off, the board has no COM2'),
    (0xE1D287, 'a30c0c00', '00000000', 'GEN3 000C0CA3 -> 0: CA0-CAF (IPMI KCS leftover) not forwarded'),
    (0xE1D296, '01061c00', '00000000', 'GEN4 001C0601 -> 0: 600-61F not forwarded'),
]
src, dst = sys.argv[1:3]
img = bytearray(open(src, 'rb').read())
if hashlib.sha256(img).hexdigest() != SRC_SHA256: sys.exit('not P4.01 (sha256 differs)')
for off, old, new, what in PATCHES:
    o, n = bytes.fromhex(old), bytes.fromhex(new)
    if img[off:off + len(o)] != o: sys.exit('%06X: expected %s, found %s' % (off, old, img[off:off + len(o)].hex()))
    img[off:off + len(n)] = n
    print('%06X %s -> %s  %s' % (off, old, new, what))
open(dst, 'wb').write(img)
print('wrote', dst, hashlib.sha256(img).hexdigest())
