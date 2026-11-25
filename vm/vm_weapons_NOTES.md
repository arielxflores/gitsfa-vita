# vm_<short>.gvm weapon set: integration notes (fork: more first-person weapons)
Build: `python3 vm/make_vm_weapon.py all` (writes build/data/vm_*.gvm and /tmp/claude-0/weapons/vminfo.json), then
`python3 vm/make_weapons_txt.py` (build/data/weapons.txt). Previews: `python3 vm/preview_weapon.py <short>`.
gvmlib.py is untouched; vm_c30.gvm / vm_m4a1.gvm rebuild byte-identical. Helpers are in vm/vmw.py:
- every weapon is decoded with its own rig from GIS_base_1st.nmb (scar 23, acr 29, mp5 1, kriss 11, l118 3, 870 13,
  m249 45, seburom5 4, mateba 8, knife 7, m67 5). fix_rigs re-reads the bind pose of rigs with >64 bones (M249: 71).
- decode_valid: Bip01_Head at an eye rest position (2.4,4.3,159) or (0,4.9,159.9), Grip within 30 cm of the right hand, and
  (strict) hand and gun in front of the body. Among the hash variants, the one decoded with the best-scoring bone map wins.
- skinrun_fixed: skin.run sometimes takes another mesh's NiSkinningMeshModifier (L118A); this picks the right one.
- borrowed clips (another family) hold the weapon parts at this weapon's bind; clips without a Grip rotation key get the
  idle's held angle (same fix as vm_m4a1).

Clip names: idle walk run sprint sprint_in sprint_out fire fire_ads ads_idle ads_in reload reload_empty draw(=swap_end)
holster(=swap_start) jump fall land slide slide_fire, plus
- l118: bolt (play after each shot); no ads_in. Scope: attachment `scope` = (texture index of the reticle R_SC_CROSS, 0, 0).
- 870: reload_start, reload_loop (one shell), reload_end; reload/reload_empty = start+loop+end (one shell); ads_idle = fire_ads frame 0.
- m67: cook, fire = throw: the grenade leaves the hand between frames 3 and 4 (0.10-0.13 s) -> stop drawing meshes >= 2, spawn the projectile.
- knife: no ADS / reload / walk / slide_fire; m67: no ADS / reload / walk / slide_fire; pistols: no sprint_out; l118/870/m249: no ads_in.
Meshes 0-1 are always the arms, 2+ the weapon.

Projection: hip exactly as vm_m4a1 (vm fovy 49.75, offset -2.37,0.08,0). ADS: vm fovy 11.5, offset (x,y,-15.6) per weapon
(weapons.txt) = minus the aim point (reticle / front post) in camera space in ads_idle frame 0, so the sight is on the view axis.
World ADS fovy: 51.4 (1.18x) iron/dot/holo, 31.7 (2x) ACOG (scar), 16.1 (4x, overlay, viewmodel hidden) l118.
Which sight each weapon carries (my choice, the default loadouts are in the encrypted TDBs): scar TRICO_GOA (ACOG),
acr GRAFT_N3 (red dot), kriss DIOTECH660 (holo), l118 RIFLESCOPE, mp5 IS_MP5, 870 IS_SF_COM_01, m249 IS_M249 (+ bipod).

## b30 round (weapons agent): hands on the gun, palette windows, sprint, climb / vault
- Left hand: the game's aimed clips (stand_zoom, fire_zoom), the aimed walk and the slides leave the left arm
  unanimated (it keeps the idle arm pose while the gun moves to the eye: hand 10 cm beside the handguard, 30 cm in
  slides). build() re-seats it with two-bone IK (vmw.lhand_ik) at the idle's frame-0 hold, relative to the gun bone it
  grips (Grip; 870: charging_handle = the pump), on every clip whose left arm is unanimated or holds a steady wrong
  spot (< 6 cm). info['clips'][name]['lhand_ik'] = {dev, still, gap}. Not on climb / vault (hand on the obstacle).
- Palette windows: the Vita shader does not reach the upper rows of pal[144] (M249 body, entries 44-47, never drew on
  the device). vmw.pal_windows: each mesh's bone indices are relative to its first palette entry, stored in bits 8-15
  of the mesh's blend word (<= 36 entries per window). Engine: model.h MMesh.pbase, model_draw_pal / wpn.h wvm_draw
  upload pal[pbase ...] per mesh. Host tools (preview.py, vmgl.py) add the base.
- Sprint: M4A1 sprint_running 812cdcfc (decoded with rig 22, retargeted for the others) = the original's sprint (gun
  canted at the right, carried in the right hand; Neon Ruins footage 86 s). sprint_base held the gun low in the middle.
- SCAR / 870 jump: decoded with the M4A1 rig and retargeted (their own rigs put the gun 15 cm off both hands).
- New clips 'climb' (climb_start+catch+up+land, 0.8 s) and 'vault' (window_up_A + window_land_A, 0.6 s; pistols: P99
  set; knife: Knife set) for player.h's mantle (PE_CLIMB / PE_VAULT). Concat entries: ('concat', [parts], seconds).
- Checks: tools/weapons/pose_host.sh (engine pose code on the real files), vmblend.py (hands under blending),
  vmside.py (arms + gun from outside), vmstrip.py (engine projection), vmdepth.py (near plane).

## b32 round (weapons agent): arms in every state, running, ADS / scope, pistol + KRISS framing, Vita prompts
- Sprint is the game's sprint_base again (b31 device shots 36/39/50: sprint_running left a floating gun with no arms).
  PSS9 TDM 169-171 s and Neon 84-89 s: the gun lowered across the bottom, no hands in view, as sprint_base.
- Palette windows stay: the device reports uniform 'pal' size 144 (48 entries), but b31 shot-42 draws the M249 body
  with windows and b28 shot-33 did not without them. tools/weapons/vmdraw_host.sh runs the engine's wvm_draw (wpn_vm.h)
  + model.h + MD_VS skinning in a software GL (tools/weapons/sgl): arm / gun coverage per state, 0 non-finite vertices.
- KRISS: the Kriss clips hold the gun ~6.5 cm lower than the M4A1 set (its ADS needs a +6.52 cm correction; the hip
  showed only the sight's top, no hands). weapons.txt lifts its hip view 4 cm (-2.37, 4.00, 0); the sprint clip_off of
  b31 (3 cm) is gone, the 4 cm lift puts the borrowed M4A1 sprint where the SMG sprint footage has it (Sitara 269 s).
- Pistols (P99 clips, ADS correction +5.5 / +7.7 cm): fitted on the Early Access tutorial "Secondary Weapons Fire"
  (309-319 s, SEBURO M5): hip offset (2, 5.6, -6) cm (slide top at 53 % of the height, hand + forearm at the bottom
  right) and ADS viewmodel fovy 26 (11.5 made the slide 2.3x too wide). MATEBA uses the same values (no footage).
- L118 scope eyecup x2.2 brighter (Machina Media 364/370 s). M4A1 iron sight checked on Rob Cram 56/112 s.
- sprint_in / sprint_out are baked but the engine does not play them (weapon.h CL_NAME has no entry; sprint <-> run is
  the 0.25 s base fade). Don't wire up the M4A1 set's sprint_stop2 as it is: it starts at the sprint_running pose
  (gun 25 cm off sprint_base); the Kriss set's sprint_stop2 starts at sprint_base (1.3 cm) and could be cross-decoded.

## b37 round (weapons agent): lens glass (no black / empty scopes)
- The device (b32 shots 27/28/31/41/42/53/54/56) and Vita3K (b35-1) drew every tube sight's lens as near-black: the game's
  closed-lens textures LENS_Blur1/2/3_D are dark tints (blue / amber / teal, RGB ~5-20) meant for its reflective lens
  shader (LENS_Blur_S = baked lens reflections, LENS_Blur_N1 = dome normal map, Textures/CUBE.dds = a street env map).
- Now (make_vm_weapon.py LENS, engine wpn_vm.h lens program): LENS_01 (the eyepiece dome) gets a baked 256x256 DXT5:
  RGB = tint x dome shading + LENS_Blur_S highlights, A = 0.5 + 0.5 x LENS_01's aimed vignette alpha (never 0:
  Pillow premultiplies RGBA when making mips, which blacked out the lens centre on Vita3K). The engine fades it from
  closed (hip; 78-96 % opaque, so a reticle / dot behind it shows dimly, as the range footage's dark lens with a faint red
  reticle) to the vignette (aimed; it opens over ads 0.38-0.5, when the hip-only inner tube goes) and adds a reflection of lensenv (CUBE.dds + its mirror: a seamless
  256x128 panorama, blurred and its shadows lifted: its black doorway made a black hole in the lens in Vita3K) looked up with the reflected view ray, scrolled by the camera yaw / pitch (it moves as you turn),
  times the coating colour and a fresnel term, stronger toward the rim. Closed: ACOG amber, CompM2 blue, L118A copper
  (tutorial 251-266 s: a copper dome with the street reflected); EOTech window: clear glass with a faint reflection.
- The LENS_Blur caps and the L118A's eyepiece cap (RifeScope_D, hip-only) are not built any more.
- Pass order in wvm_draw: opaque, alpha, additive, lens glass (last: it dims what is behind it).
- Host: vm/vmgl.py LVS/LFS = the same maths (render(..., yaw=, pitch=)).
- Checked in Vita3K (results weapons-7/8/9): SC-AR amber, ACR blue, L118A copper at hip, open views aimed, holo clear.
  Cost: lens pixels at hip 2-6.5k (0.4-1.2 % of the screen), aimed 15-39k (KRISS holo window the most); one extra texture
  read + ~12 ALU per lens pixel over the model shader; -2 draws per ACOG / CompM2 frame (lens caps gone), -1 for L118A.
