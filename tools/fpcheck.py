import os
HERE = os.path.dirname(os.path.abspath(__file__))
import sys,glob
sys.path.insert(0,"/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/lib/python3.9/site-packages")
import pcbnew
for f in sorted(glob.glob(os.path.join(HERE, "..", "lib", "isa_bridge.pretty", "*.kicad_mod"))):
    try:
        fp=pcbnew.FootprintLoad(os.path.join(HERE, "..", "lib", "isa_bridge.pretty"),f.split("/")[-1][:-10])
        print("OK  ",fp.GetFPIDAsString() or f.split("/")[-1],len(fp.Pads()),"pads")
    except Exception as e: print("FAIL",f.split("/")[-1],str(e)[:120])
