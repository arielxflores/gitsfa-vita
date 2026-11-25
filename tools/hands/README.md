# The hands' poses against the footage (b105)

Dev-container scratch tools that fitted the skill gesture (engine/src/hands.h) to the Demolition video's frames; they keep
their working files in a scratch folder (`/tmp/claude-0/hv` in the scripts - change it at the top of each).

- `exp.py`   builds vm_hands.gvm into the scratch folder and pickles the 'skill' / 'idle' frames (bone positions, rotations).
- `ovl.py`   the port's arm outlined in green over a footage frame: `ovl.py <frame> <footage.jpg> <out.jpg> "roll=0,fov=56.7,off=(-2.3,1.4,0)" ...`
             (roll / sh / yaw / pit move the forearm bone alone - in the hands' model only the folded knife's housing rides it).
- `sbs.py`   the same cases side by side with a pixel grid (`BOX=x0,y0,x1,y1`), for reading feature positions.
- `proj.py`  where points of the housing's space land on screen (the pivot's ring, the hologram's rings).
- `rend.py`, `roll.py`  earlier tries (fore-twist fractions, roll sweeps). `kn.py`: the knife's Grip in the forearm's space.
