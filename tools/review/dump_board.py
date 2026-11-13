import pcbnew, sys, json
b = pcbnew.LoadBoard(sys.argv[1]); out = []
for f in sorted(b.GetFootprints(), key=lambda f: f.GetReference()):
    pos = f.GetPosition()
    d = {'ref': f.GetReference(), 'value': f.GetValue(), 'fp': str(f.GetFPID().GetUniStringLibItemName()), 'lib': str(f.GetFPID().GetUniStringLibNickname()),
         'x': pos.x / 1e6, 'y': pos.y / 1e6, 'rot': f.GetOrientationDegrees(), 'layer': f.GetLayerName(), 'dnp': f.IsDNP() if hasattr(f, 'IsDNP') else False, 'pads': []}
    for p in f.Pads():
        pp = p.GetPosition(); sz = p.GetSize()
        d['pads'].append({'n': p.GetNumber(), 'net': p.GetNetname(), 'x': pp.x / 1e6, 'y': pp.y / 1e6, 'w': sz.x / 1e6, 'h': sz.y / 1e6,
                          'drill': p.GetDrillSize().x / 1e6, 'fn': p.GetPinFunction(), 'type': p.GetPinType(), 'smd': p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD})
    out.append(d)
bb = b.GetBoardEdgesBoundingBox()
json.dump({'parts': out, 'w': bb.GetWidth() / 1e6, 'h': bb.GetHeight() / 1e6, 'x0': bb.GetX() / 1e6, 'y0': bb.GetY() / 1e6,
           'layers': b.GetCopperLayerCount(), 'tracks': len(b.GetTracks()), 'nets': b.GetNetCount()}, open(sys.argv[2], 'w'))
print(len(out), 'footprints', bb.GetWidth() / 1e6, 'x', bb.GetHeight() / 1e6, 'mm', b.GetCopperLayerCount(), 'layers')
