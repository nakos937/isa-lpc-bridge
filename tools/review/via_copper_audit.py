# SPDX-License-Identifier: GPL-2.0-or-later
# Independent clearance audit (own distance maths for tracks and vias; KiCad shapes only for pads and zone fills).
# JLC multilayer limits: copper to copper 0.09, pad to track 0.1, via hole to other-net copper 0.2, hole to hole 0.2, PTH hole to track 0.28, inner PTH hole to copper 0.3.
import pcbnew, sys, math, collections
mm = pcbnew.ToMM; F = pcbnew.FromMM
b = pcbnew.LoadBoard(sys.argv[1])
CU = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]
V = [(mm(t.GetPosition().x), mm(t.GetPosition().y), mm(t.GetWidth(pcbnew.F_Cu)) / 2, mm(t.GetDrill()) / 2, t.GetNetCode(), t) for t in b.GetTracks() if t.Type() == pcbnew.PCB_VIA_T]
T = [(mm(t.GetStart().x), mm(t.GetStart().y), mm(t.GetEnd().x), mm(t.GetEnd().y), mm(t.GetWidth()) / 2, t.GetNetCode(), t.GetLayer()) for t in b.GetTracks() if t.Type() != pcbnew.PCB_VIA_T]
grid = collections.defaultdict(list); G = 2.0
for i, s in enumerate(T):
    for gx in range(int(min(s[0], s[2]) // G) - 1, int(max(s[0], s[2]) // G) + 2):
        for gy in range(int(min(s[1], s[3]) // G) - 1, int(max(s[1], s[3]) // G) + 2): grid[(gx, gy)].append(i)
vg = collections.defaultdict(list)
for i, v in enumerate(V): vg[(int(v[0] // G), int(v[1] // G))].append(i)
def dseg(x, y, s):
    ax, ay, bx, by = s[:4]; dx, dy = bx - ax, by - ay; L = dx * dx + dy * dy
    k = 0 if L == 0 else max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / L)); return math.hypot(x - ax - k * dx, y - ay - k * dy)
def segseg(a, c):
    def cross(o, p, q): return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0])
    p1, p2, p3, p4 = (a[0], a[1]), (a[2], a[3]), (c[0], c[1]), (c[2], c[3])
    if cross(p1, p2, p3) * cross(p1, p2, p4) < 0 and cross(p3, p4, p1) * cross(p3, p4, p2) < 0: return 0.0
    return min(dseg(a[0], a[1], c), dseg(a[2], a[3], c), dseg(c[0], c[1], a), dseg(c[2], c[3], a))
m = collections.defaultdict(lambda: (9.0, None)); cnt = collections.Counter()
def note(k, d, where, lim):
    if d < m[k][0]: m[k] = (d, where)
    if d < lim - 1e-4: cnt[k] += 1
zones = [(z, l) for z in b.Zones() if not z.GetIsRuleArea() for l in z.GetLayerSet().Seq()]
pads = [p for p in b.GetPads()]
pg = collections.defaultdict(list)
for p in pads: pg[(int(mm(p.GetPosition().x) // G), int(mm(p.GetPosition().y) // G))].append(p)
for i, v in enumerate(V):
    x, y, r, h, net, obj = v; g = (int(x // G), int(y // G))
    for j in set(grid[g]):
        s = T[j]
        if s[5] == net: continue
        d = dseg(x, y, s) - s[4]
        note('hole to other-net track', d - h, (round(x, 3), round(y, 3)), 0.2); note('via copper to other-net track', d - r, (round(x, 3), round(y, 3)), 0.09)
    for gx in (g[0] - 1, g[0], g[0] + 1):
        for gy in (g[1] - 1, g[1], g[1] + 1):
            for j in vg[(gx, gy)]:
                if j == i: continue
                o = V[j]; d = math.hypot(o[0] - x, o[1] - y)
                note('hole to hole', d - h - o[3], (round(x, 3), round(y, 3)), 0.2)
                if o[4] != net: note('hole to other-net via copper', d - h - o[2], (round(x, 3), round(y, 3)), 0.2); note('via copper to other-net via copper', d - r - o[2], (round(x, 3), round(y, 3)), 0.09)
            for p in pg[(gx, gy)]:
                if p.GetNetCode() == net: continue
                hs = obj.GetEffectiveHoleShape()
                for l in CU:
                    if not p.IsOnLayer(l): continue
                    lo, hi = 0.0, 1.0
                    if not hs.Collide(p.GetEffectiveShape(l), F(1.0)): continue
                    for _ in range(14):
                        mid = (lo + hi) / 2
                        if hs.Collide(p.GetEffectiveShape(l), F(mid)): hi = mid
                        else: lo = mid
                    note('hole to other-net pad copper (%s)' % ('inner' if l in (pcbnew.In1_Cu, pcbnew.In2_Cu) else 'outer'), lo, (round(x, 3), round(y, 3)), 0.2)
    hs = obj.GetEffectiveHoleShape()
    for z, l in zones:
        if z.GetNetCode() == net: continue
        fp = z.GetFilledPolysList(l)
        if not fp.Collide(hs, F(0.6)): continue
        lo, hi = 0.0, 0.6
        for _ in range(13):
            mid = (lo + hi) / 2
            if fp.Collide(hs, F(mid)): hi = mid
            else: lo = mid
        note('hole to other-net zone copper %s' % b.GetLayerName(l), lo, (round(x, 3), round(y, 3)), 0.2)
# track to track, different nets, same layer
for i, s in enumerate(T):
    seen = set()
    for gx in range(int(min(s[0], s[2]) // G), int(max(s[0], s[2]) // G) + 1):
        for gy in range(int(min(s[1], s[3]) // G), int(max(s[1], s[3]) // G) + 1):
            for j in grid[(gx, gy)]:
                if j <= i or j in seen: continue
                seen.add(j); o = T[j]
                if o[5] == s[5] or o[6] != s[6]: continue
                note('track to other-net track', segseg(s, o) - s[4] - o[4], (round(s[0], 2), round(s[1], 2)), 0.09)
# component PTH holes: JLC PTH to track 0.28, inner-layer PTH hole to copper 0.3
for p in pads:
    if p.GetAttribute() != pcbnew.PAD_ATTRIB_PTH or p.GetDrillSizeX() == 0: continue
    hs = p.GetEffectiveHoleShape(); net = p.GetNetCode(); where = (round(mm(p.GetPosition().x), 3), round(mm(p.GetPosition().y), 3))
    for t in b.GetTracks():
        if t.GetNetCode() == net or not hs.Collide(t.GetEffectiveShape(t.GetLayer() if t.Type() != pcbnew.PCB_VIA_T else pcbnew.F_Cu), F(0.5)): continue
        sh = t.GetEffectiveShape(t.GetLayer() if t.Type() != pcbnew.PCB_VIA_T else pcbnew.F_Cu); lo, hi = 0.0, 0.5
        for _ in range(14):
            mid = (lo + hi) / 2
            if hs.Collide(sh, F(mid)): hi = mid
            else: lo = mid
        note('PTH hole to other-net track/via', lo, where, 0.28)
    for z, l in zones:
        if z.GetNetCode() == net or l not in (pcbnew.In1_Cu, pcbnew.In2_Cu): continue
        fp = z.GetFilledPolysList(l)
        if not fp.Collide(hs, F(0.5)): continue
        lo, hi = 0.0, 0.5
        for _ in range(14):
            mid = (lo + hi) / 2
            if fp.Collide(hs, F(mid)): hi = mid
            else: lo = mid
        note('PTH hole to other-net inner copper', lo, where, 0.3)
print(sys.argv[1].split('/')[-1])
for k in sorted(m): print('  %-44s min %.4f mm at %s   below limit: %d' % (k, m[k][0], m[k][1], cnt[k]))
