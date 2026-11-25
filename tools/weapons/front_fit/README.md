# Where each gun's front ends on the PC (b105, vm/barrels.py)

The PC's item pictures (ui.dat `<GUN>_BASIC_01.png`, 384 x 150; extract them to a folder and set the path in the scripts -
`/tmp/claude-0/pcui` there) against the port's side silhouettes.
- `side.py`      a gun's side silhouette from its vm_<gun>.gvm (the gun's own space, cm).
- `sheet_map.py` the port's short names -> the pictures' names.
- `fit.py`       scale + offset of the silhouette onto the picture (an IoU score over the rear three quarters), then the picture's
                 front tip read back in the gun's space: the numbers of vm/barrels.py FRONT.
- `sheet.py`, `overlay.py`  contact sheets of the two, for the eye.
