#!/usr/bin/env python3
from pathlib import Path
root=Path('/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261013-sll-quantity-01/repo')
file=root/'src/lib/HiFPlacer/placement/placementInfo/PlacementInfo.cc'
text=file.read_text()
old='    if (x == pu->X() && y == pu->Y()) return false;\n    pu->setAnchorLocationAndForgetTheOriginalOne(x, y);'
assert text.count(old)==1
text='#include "SllClipTrace.h"\n'+text.replace(old,'    if (x == pu->X() && y == pu->Y()) return false;\n    sll_clip_trace::note(pu->X(), pu->Y(), x, y);\n    pu->setAnchorLocationAndForgetTheOriginalOne(x, y);')
file.write_text(text)
print('Added passive counters for projections inside spreading and the explicit clip step.')
