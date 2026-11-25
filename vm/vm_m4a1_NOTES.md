# vm_m4a1.gvm — integration note (fork: first-person weapon view)
Build: `python3 /root/gits/vm/make_vm_m4a1.py /root/gits/build/data 0 0 156.93` (CAM eye = rig (0,0,156.93): puts the holo dot exactly on the view axis in ads_idle).
Assets: M4A1_Body + M4A1_BA barrel (wea_ba) + M4A1_MA magazine (rig slot AR_SEBRO_Magazine) + IS_M4_E2_D folded front sight (E2)
+ QuickPoint_S1 holo sight (SC_DUMMY; emissive cyan lights baked from UV set 1; reticle/glow/lens additive, blend 2). Sleeve darkened x0.5.
gvmlib.static_meshes: now converts triangle strips (prim==1) and returns UV1 (no effect on vm_c30).
Clips: idle walk(=ADS walk) run sprint sprint_in sprint_out fire fire_ads ads_idle ads_in reload reload_empty draw holster jump fall land slide slide_fire.
Fixes baked in: weapon slots the clip doesn't key are re-seated at the M4A1 bind; clips with no Grip rotation key
(reload_fast f407a3c7, jump_up 5fc58ebb) get the idle Grip-in-Weapon_R rotation (else the gun points backwards).
NOTE vm_c30 uses jump 5fc58ebb too -> same backwards-gun bug there.

Projection (calibrated to footage at 1280x720, apply via K translation + persp fovy, lerp with ads):
  hip : fovy 49.75 deg, viewmodel translate (-2.37, +0.08, 0) cm (camera space)
  ADS : fovy 11.5 deg,  translate (0, 0, -15.6) cm  (camera further back; matches sight + rail plate + no hands)
  suggestion: lerp tan(fovy/2) and the translation with ads; keep sway/bob x(1-ads); ADS recoil = move toward camera ~3 cm, down ~0.3 cm per shot.
World FOV: Fov=90 is horizontal at 16:9 -> Vita 960x544 vertical 59.1 deg (est. from vertical-line convergence, f~640-760px@720p). ADS world zoom ~1.18x -> ~51 deg.
hud1 (Grip-space attachment): hip ammo counter is centred on projected hud1 -> (574,377) at 960x544 (idle; run sways x 554..575).
ADS and sprint: counter is screen-fixed at (0.495W, 0.90H) = (475,490); hud1 is NOT used there.
