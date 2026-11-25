# b107: the rooms' reverb and the reload schedule against the user's showcase video

Dev-container scratch tools (they keep their working files in `/tmp/claude-0/ah` and read the video's sound from
`/tmp/claude-0/sr/showcase.wav`, 48 kHz: `ffmpeg -i "<... ALL WEAPONS Showcase - Tigerfield ...>.mp4" -ar 48000 showcase.wav`).
`scene` = `tools/audio/host/scene.c` built there (`gcc -O2 -w -Iinc -I../../../engine/src -o scene scene.c -lm -lpthread`);
`scene one <event> 1 <seconds> <indoor> [<map> <indoor blend>]` renders one event, dry or in a map's room.

Reverb (engine/src/reverb.h):
- `ana.py <t,t,...>`   which sniper fire sample each isolated shot of the video is (spectrum of the first 0.25 s): the range
                       plays the *_fp_outdoor samples (0.98-0.99; indoor 0.92) -> audio.h snd_force_outdoor.
- `cmp2.py`            the shots' envelopes: video, the port dry, the port with the reverb (the same within 1 dB: the fire
                       samples carry their own tails, their wet level is -16..-18 dB).
- `clicks.py <wav>`    every isolated mechanical click of a recording and how the room rings on after it (5 ms steps).
- `match.py <t,...>`   which event a click is (normalised cross-correlation with every weapon event rendered by `scene`).
- `fit.py <tag>`       eight identified reload clicks: the video's ring-out against the port's -> RV_LATE (1.3: within
                       1.5 dB from 20 to 400 ms after the click).
- `rvt.c`, `rvb.c`     the network alone: energy gain and decay per band (2.4 s low / mid, 2.0 s above 5 kHz for the
                       range's 2.5 s x 0.8), and its cost (15 us per grain on the host).

Reload schedule (vm/make_wpn_snd.py, engine/src/wsnd.h):
- `timeline.py <wav> <out.pkl>`  every click of the video matched to an event -> (time the event started, name, score).
- `rlfit.py`           per weapon section (the video's order, from a viewer's time stamps) the reloads fitted to the game's
                       schedule: start and duration T of each -> make_wpn_snd.py RELOAD_PC (the PC plays the long empty
                       reloads 2 to 4 times faster than their clips; the standard ones within 3 % of the clip).
Fire rates measured the same way (the mode of the shot intervals per section) agree with weapons.txt (v2.0 armory FIRE RATE
x 10) within 3 % for 21 of 25 automatic guns; the video is an older build (it has the MP7 and no P226), so the table stays.
