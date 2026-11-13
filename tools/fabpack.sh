#!/bin/zsh
# usage: fabpack.sh board.kicad_pcb outdir -> gerbers, drills, JLC bom.csv (+ lcsc_map.json lookups), cpl.csv
B=$1; O=$2; KC=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli; KPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
mkdir -p $O/gerber; rm -f $O/gerber/*
$KC pcb export gerbers --layers "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts" --subtract-soldermask -o $O/gerber/ $B >/dev/null 2>&1
$KC pcb export drill --format excellon --drill-origin absolute --excellon-separate-th --generate-map --map-format gerberx2 -o $O/gerber/ $B >/dev/null 2>&1
$KPY - $B $O "$(dirname "$0")/../fab/lcsc_map.json" "$(dirname "$0")/../fab/cpl_overrides.json" <<'PY' 2>&1 | grep -v '^\./src\|assert'
import pcbnew,sys,json,csv,collections,re
b=pcbnew.LoadBoard(sys.argv[1]); O=sys.argv[2]; M=json.load(open(sys.argv[3])); OV=json.load(open(sys.argv[4])); mm=pcbnew.ToMM
G=collections.defaultdict(list); cpl=[]
for f in b.GetFootprints():
    r=f.GetReference()
    if r.startswith("FID") or (f.GetAttributes()&pcbnew.FP_BOARD_ONLY): continue
    fp=f.GetFPIDAsString().split(":")[-1]; G[(f.GetValue(),fp)].append(r)
    if not r.startswith("TP"):
        p=f.GetPosition(); x,y,rot=mm(p.x),mm(p.y),f.GetOrientationDegrees()
        o=OV.get(r)
        if o:   # JLC part model frame differs from the footprint: see fab/cpl_overrides.json
            if o.get("center")=="pads":
                xs=[mm(q.GetPosition().x) for q in f.Pads()]; ys=[mm(q.GetPosition().y) for q in f.Pads()]
                x,y=(min(xs)+max(xs))/2,(min(ys)+max(ys))/2
            rot+=o.get("rot_offset",0)
        cpl.append((r,round(x,4),round(-y,4),"Top" if f.GetLayer()==pcbnew.F_Cu else "Bottom",round(rot%360,1)))
key=lambda r:(re.sub(r"\d","",r),int(re.sub(r"\D","",r) or 0))
rows=[]; miss=[]
for (v,fp),rs in sorted(G.items(),key=lambda x:key(sorted(x[1],key=key)[0])):
    if rs[0].startswith("TP"): continue
    m=M.get(f"{v}|{fp}",{}); rows.append((m.get("bom_comment",v),",".join(sorted(rs,key=key)),m.get("bom_footprint",fp),m.get("lcsc",""),re.sub(r"\s*\(.*$","",m.get("mpn","")),m.get("src","")))   # bom_* = text the JLC BOM tool matches on
    if not m.get("lcsc"): miss.append((v,fp,len(rs)))
# JLC BOM tool warns "matched to multiple part numbers" when rows share a Comment with different LCSC parts: one row per LCSC part, unique Comment
merged={}
for v,ds,fp,lc,mpn,src in rows:
    k=lc or (v,fp)
    if k in merged: merged[k][1]=merged[k][1]+","+ds
    else: merged[k]=[v,ds,fp,lc,mpn,src]
def size(fp):
    m=re.search(r"(\d{4})Metric",fp)
    if m: return m.group(1)
    return "4x0402" if "Array" in fp else fp.split("_")[0]
rows=[]; seen=set()
for v,ds,fp,lc,mpn,src in merged.values():
    ds=",".join(sorted(ds.split(","),key=key)); c=v
    if c in seen: c=f"{v} {size(fp)}"
    if c in seen: c=f"{v} {lc}"
    seen.add(c); rows.append((c,ds,fp,lc,mpn,src))
rows.sort(key=lambda r:key(r[1].split(",")[0]))
with open(O+"/bom.csv","w",newline="") as fh:
    w=csv.writer(fh); w.writerow(["Comment","Designator","Footprint","LCSC Part #","MPN","Source note"]); w.writerows(rows)
with open(O+"/cpl.csv","w",newline="") as fh:
    w=csv.writer(fh); w.writerow(["Designator","Mid X","Mid Y","Layer","Rotation"]); w.writerows(sorted(cpl,key=lambda c:key(c[0])))
print("bom lines",len(rows),"with LCSC",sum(1 for r in rows if r[3]),"cpl parts",len(cpl),"test points excluded (pads only)")
print("no LCSC number:",", ".join(f"{v} [{fp.split('_')[0]}] x{n}" for v,fp,n in miss))
PY
ls $O/gerber | wc -l
