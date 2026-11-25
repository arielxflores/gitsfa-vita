# Operator skills - research, assets and design (GITS:SAC First Assault, Vita port)

Status: research + assets + spec. Nothing under `engine/src` was touched; everything below is for the `skills.h` you write.
Confidence tags: **[M]** measured in footage / skill videos, **[D]** read from the game's data (file names, NIF meshes /
timings, sounds), **[E]** my estimate or design choice where the data is encrypted (DB.dat) and the footage does not show it.

## 0. Sources used

* **Skill videos shipped with the game** (the best source - the user's own install, `ui.dat: ui\GFx_DA\Data\rsc\video\skill\*.usm`,
  CRI USM with plain MPEG-1, demuxed with a 20-line script, no decryption): `opticalCamo, ARMLAUNCHER, heatSpotted, SENTRYGUN,
  endocoverFusion` (= Borma's metaboost), `SPRINTACC` (= Paz's rush), `Shield, CYBERWALL, DMDDrone, CyberDive, EMP, SUMMON`
  (mini-Tachikoma "seekers", a cut / earlier skill). The `*_ani.usm` files are anime intro clips (no gameplay).
  Contact sheets / key frames: the project folder's `skills-study\` (v_*, z_*, key_*, bars, ringicons).
* **3v3 TDM + Demolition footage** (Motoko player): cooldown ring and duration bar measured frame by frame.
* **effect.dat** `effect\cyberbrain\*` (one NIF family per skill), `effect\screen\*`, `effect\kfm\*` (screen + radar), `explosion\`,
  `missile\`, `turret\`, `character\emp_ch_ele`; **model.dat** `model\Npc\CB_*` (sentry, drone, EMP device, wall projector,
  mini-Tachikoma), `model\Object\shield|Dome` (Azuma), `model\Objects\CB_CC` (wrist deploy controller); **weapon.dat**
  `weapon\Armluncher` (Batou's arm), `weapon\CN_CW` (Maven's wall launcher).
* **Sounds / voices** (`audio_manifest.txt`): `is_*` skill SFX (RAM pack), `d_<op>_use_<skill>`, `d_op_ready_<skill>`,
  `d_op_enemy_<skill>`, `d_op_warn_specialrecon`.
* **HUD**: InGameUI.swf `intelMC.skillMC.skillIcon.iconN` (13 icons, already in hud.ghud as `skill.N`).

### Kurutsu (kuro)
Her voice set exists - it is filed under the internal codename **`emp`**: her model textures are `model\ActorGhost\Kuro\Kuro_A\EMP_*.dds`,
the 12 `<op>_mvp_001` lines are azuma bato borma **emp** ishikawa maven motoko paz reiko saito sitara togusa (no kuro), and the
`emp` set is complete (`act_emp_*` 18 groups, `cqc_split_emp_*`, `k_emp_*`, `r_emp_*`, `d_emp_get/_transferintel/_use_shockwave`,
`v_character_select_emp`). **Her skill is EMP SHOCKWAVE** (skill video `EMP.usm`, HUD icon 16 = the 3-legged EMP device
`model\Npc\CB_EMP`, `is_emp*` sounds, `d_op_ready_emp`, `d_op_enemy_emp`). Engine fix: `op_voice()` in main.c maps kuro to
`"togusa"` - it should be `"emp"` (AUDIO_NOTES.md says the same wrong thing). Note her use line is `d_emp_use_shockwave_001/_002`.

## 1. Summary

Order = `CHARS[]` / `MENU_CHR[]`. Icon = hud.ghud `skill.N`; menu = GameLoading `skillIconMc` frame (`ld.skico.N` / `op.skico.N`),
"-" = no menu frame exists (the menu swf only has 7 skill icons; use the HUD image).

| # | operator | skill (voice id) | what it does | active | cooldown | tiers (T1/T2/T3) | icon / menu |
|---|---|---|---|---|---|---|---|
| 0 | Batou | ARM LAUNCHER (`armlauncher`) | arm turns into a missile pod, auto-locks enemies near the crosshair, fires 1 homing missile per lock | until fired, max 8 s [E] | 50 s [E] | 2/3/4 locks [E] (2-3 seen in the T2 video) | 12 / - |
| 1 | Motoko | THERM-OPTIC CAMO (`optical`) | near-invisible; bots can't see her beyond 4 m | 15 s [M] | 60 s from activation [M] | 15/18/22 s [E] | 8 / 0 |
| 2 | Saito | HEAT SCAN (`heatscan`) | scan pulses reveal enemy heat silhouettes through walls | 5 s = 4 pulses [M] | 45 s [E] | 5/7/10 s [E] | 3 / 1 |
| 3 | Togusa | SPECIAL RECON (`specialrecon`) | throws a mini-Tachikoma that noise-scans 11.6 m around it; noisy enemies get marked | 16 s [D] | 45 s [E] | radius 11.6/14/17 m [D/E] | 13 / - |
| 4 | Ishikawa | SENTRY GUN (`sentrygun`) | deploys an automated gatling turret | 30 s or until destroyed [E] | 60 s [E] | arc 90/180/360 deg [D], T3 + grenade pod [D] | 1 / 7 |
| 5 | Borma | METABOOST (`metaboost`) | heals himself + linked allies in 10 m over 7.5 s, overheal at higher tiers | 7.5 s heal [M] + 10 s bonus [M] | 50 s [E] | cap 1.0/1.4/1.8 x max HP [M T3] | 2 / 2 |
| 6 | Paz | RUSH (`rush`) | sprint mode: dash at 2x speed; touching an enemy while dashing = instant takedown | energy: 2.2 s of dash [M] (21 s idle drain) | 40 s [E] | dash 2.2/2.8/3.4 s [E] | 9 / 3 |
| 7 | Azuma | PLASMA SHIELD (`plasmashield`) | toggled frontal energy shield, blocks bullets, he can shoot out; T3 = dome | energy 30 s idle [M], drains on hits | 45 s [E] | small / big panel / dome r 2.9 m [D] | 17 / - |
| 8 | Kurutsu | EMP SHOCKWAVE (`emp`, voice set `emp`) | places an EMP device that blasts after 2 s: destroys enemy deployables, shocks enemies | fuse 2 s [M] | 50 s [E] | radius 5/6.5/8 m [E] | 16 / - |
| 9 | Maven | CYBER WALL (`cyberwall`) | deploys a 3 x 2 m holo wall: stops enemy bullets, enemies passing through are hacked + revealed | 20 s [M-ish] | 50 s [E] | 3x2 / 4x2.5 / 5x3 m [D T1, E] | 5 / 6 |
| 10 | Reiko | CYBER DIVE (`cyberdive`) | ghost leaves the body and flies through walls for 5 s; diving into an enemy hacks him (blind + no fire) | 5 s [M] | 55 s [E] | hack 3/4/5 s [E] | 18 / - |
| 11 | Sitara | DMD DRONE (`dmddrone`) | calls a drone to a marked point; it hovers and lasers enemies around it | 20 s engaged (~90 s idle) [M] | 60 s [E] | dps 20/26/32 [E] | 19 / - |

`SUMMON` (mini-Tachikoma seekers that run at enemies and explode, HUD icon 11, menu frame 5, "SEEKER KILL") is a skill
video of a cut/older skill; its model (CB_SPIDERMINE) is the same mini-Tachikoma Togusa's recon uses.

**Footage facts that hold for all skills** [M]: skills are **ready at match start** (icon bright at t=0); the **cooldown starts at
activation** (the red ring starts shrinking the moment the camo goes on, and the camo was ready again 58-62 s after each
activation in both TDM and Demolition); the HUD shows the active skill as a **cyan bar under the crosshair with a small
copy of the skill icon above it** (1280x720: bar x 608..673, y 593..600, icon 624..655 x 570..585 -> Vita 960x544:
bar x 456..505, y 445..450, icon x 468..491, y 427..439); "Skill Ready" notice + `d_op_ready_<skill>` voice when the
cooldown ends; the ring icon is grey while cooling down. In TDM the HUD says **TIER 1 from intel 0 to 399** (gauge
200 -> 400 at intel 300), so the skill tier should be `1 + (intel >= 400) + (intel >= 800)` (the HUD's own TIER number
can stay as it is). The older skill videos all run at TIER 2.

## 2. Common framework

### 2.1 State machine (every skill)
```
READY --press--> [AIM]* --confirm--> ACTIVE --(time/energy out | cancel | death)--> COOLDOWN --(cool <= 0)--> READY
           *AIM only for placed skills (sentry, wall, drone, EMP, recon): hologram preview, R = place, skill button/O = cancel
            (cancel returns to READY, no cooldown); Batou's AIM = launcher mode (locks build up), R = fire.
```
* `cool` is set to `cooldown[tier]` **at confirm** and counts down during ACTIVE as well (measured for camo). Keep counting while dead.
* Death ends personal effects (camo, shield, rush, dive, metaboost on self); deployables stay until their own end.
* Placement: preview 300 cm in front of the eye (max 700 cm for wall / EMP / recon, 3000 cm for the drone's target point),
  snapped to the ground with `raycast` down; valid if the ground normal z > 0.7 and the footprint (turret r 70 cm, wall
  half-width) is clear -> hologram cyan, else red. The original draws the deployable's own mesh as a white/cyan
  hologram (`*_ngon_*` NIFs, `CAST_*.nif` = the hologram copies): draw the GVM with blend 2 (additive) tint (0.3,0.8,1,0.6),
  red (1,0.25,0.2) when invalid, plus `C_DIR` cell `sk_disk` + `sk_dring` flat on the ground (r = footprint).
* Build: deployables spend **2.0 s** [D: `*_ngon_complete.nif` stop 2.0] growing: draw the model additive cyan with a
  rising clip plane (cheap: scale z from 0.1 -> 1 over 2 s) + `SK_BUILD` every 0.4 s; then solid. Removal: `SK_UNBUILD` (1.2 s,
  `*_ngon_delete` 1.2) when it expires, `SK_DESTROY` when shot down / EMP'd.

### 2.2 Audio rules
* own use: `voice_play("d_<voice>_use_<id>")` (voice = op_voice: kuro -> `emp`) + the skill's start SFX (`play`).
* `d_op_ready_<id>` when the cooldown ends (player only).
* `d_op_enemy_<id>` (player only) the first time per activation that an **enemy** skill becomes relevant to the player:
  an enemy camo/rush/shield user enters view, a sentry/wall/drone/EMP/recon is built within 30 m and in LOS, a missile locks
  the player, the player is heat-scanned / dived. Rate-limit 1 per 6 s.
* `d_op_warn_specialrecon` to a player detected by an enemy recon unit.
* All `is_*`, `cloaking_*` SFX are in the RAM pack -> `play()` / `play_pan()` (3D: pan + distance like bot sounds).

### 2.3 HUD (hud_game.h already has the ring, the red cooldown ramp `in.cool`, `skill.N`, TIER)
* `H_.skill_icon = SK_DEF[skill].hud_icon` (see table; today it is hard-coded 8).
* `H_.cool = cool / cooldown` (as now) .
* Active bar under the crosshair: two `white` quads (background rgba 0,0,0,0.35; fill 0.33,0.78,0.95) at the Vita rect
  above, fill width = `bar` 0..1 from `sk_hud()`, plus `skill.N` scaled to 23 px above it. Placed skills show the bar for
  their deployable's remaining life/HP.
* World markers (lock rings, recon icons, heat ghosts) are drawn by skills.h in the world pass (fx atlas cells).

### 2.4 Bots
Bots use the same `SkillActor` code. Each bot gets its operator's skill from its `ct` (op_of_chrt). Generic rule: try at most
every 2 s when READY with the per-skill condition in section 3 ("bot use"); placed skills place at the bot's position + 250 cm
forward (no preview). Reactions are listed per skill ("bots vs it"). All targeting goes through one function
`sk_can_see(observer, target)` that the bot code calls instead of its LOS check (camo, dive-hack, heat/recon reveal).

### 2.5 Performance budget (Vita, target <= 1 ms CPU per frame for all skills)
* Logic: a fixed pool, no allocation: `SK_MAXACT 9` actors, `SK_MAXOBJ 16` deployables, `SK_MAXMS 16` missiles. Per frame
  O(actors x objects) distance checks = < 0.05 ms.
* LOS (map raycast) is the expensive part: deployable target search at **5 Hz staggered** (one deployable per frame slot),
  max 1 raycast per enemy per search; recon/heat reveal = no raycast at all (it is through-walls by design).
* Bullets vs skills: one ray-vs-quad (wall, 2 tests) + ray-vs-OBB (shield panel) / ray-vs-sphere (dome) per shot: trivial.
* Draw: deployables use the **rigid GPU path** (every skill GVM is one bone per mesh: `model_prepare_rigid` makes it one
  draw call per mesh, no CPU skinning): turret 4 calls / 8 k tris, drone 1 / 5.3 k, EMP 3 / 3.9 k, mini-Tachikoma 2 / 4 k,
  wall pod 1 / 1.3 k, wall 1 / 128, dome 1 / 368. Heat ghosts: 1 call / 1 k tris per revealed enemy (<= 8).
* Screen overlays: one 1024x1024 DXT1 texture, at most 2 full-screen quads at a time (~0.1 ms GPU each at 960x544).
* Particles: inside fx.h's 300-particle pool (skill effects spawn 2-20 particles each).

## 3. Skills

Units: cm, s, HP. "max HP" = the operator's HP (100 in the TDM build; the skill videos ran 150-250 HP builds).
Damage numbers are [E] scaled to the engine's 100 HP + 100 AP.

### 3.0 Batou - ARM LAUNCHER (`armlauncher`, icon 12)
* Seen [M]: the right arm transforms into the launcher (`weapon\Armluncher\1st\Armluncher_1rd.nif` + 1st-person clips
  `base_Armluncher_1st_swap_star / swap_end / stand_idl / fire / walk / run / jump_*`), red lock rings appear on enemies near
  the crosshair (`armlauncher_target.nif` 0.17 s, `armlauncher_load_1/2`), fire -> camera shake (`cam_shake_hip_fire_launcher`)
  and missiles (`armlauncher_missile.nif`: 18 cm missile + `rocket_simg` flame, smoke trail `armlauncher_missile_fire/_smoke`),
  "ROCKET KILL +200", destroys deployables ("SENTRY DESTROYED"). 2-3 locks seen at T2.
* Spec: press -> AIM (launcher mode, `is_armlauncher_on` + `_on_ring_01`, 0.5 s swap, weapon can't fire). Each 0.35 s the
  enemy (operator or deployable) with the smallest angle to the view axis inside a 12 deg cone, <= 4000 cm, in LOS and not
  yet locked, gets a lock (`SK_TU_LOCK`-style ring: cell `sk_dring` red on him, `is_armlauncher_on_ring_02`), up to
  **locks[T] = 2/3/4** [E]. R fires (`is_armlauncher_fire` + `is_armlauncher_rocket_fire_0N`, `SK_AL_FIRE` at the arm) one missile
  per lock (none locked: 1 missile straight ahead); missiles leave 0.08 s apart, fly 2500 cm/s, turn rate 180 deg/s toward their
  target, live 3 s, `is_armlauncher_rocket_loop` on each, `SK_MS_TRAIL` every 1/30 s; on contact (target radius 40 cm, map
  raycast per step) -> `SK_MS_EXP` + `is_armlauncher_rocket_exp_0N`, damage 120 at the centre falling to 30 at 300 cm (not
  through walls: one LOS ray per victim). Deployables in 300 cm take 200. Then `is_armlauncher_off`, back to the gun.
  AIM times out after 8 s (fires nothing, still goes on cooldown).
* Cheapest faithful: keep the gun viewmodel but hide it during AIM and draw nothing for the arm (the arm launcher
  viewmodel is a separate job: its NIF is skinned to 30 bones and its clips are in GIS_base_1st.nmb - same pipeline as
  make_vm_weapon.py); missiles = `sk_missile.gvm` (1 call, 142 tris) + trail particles. Third person: bots show nothing extra.
* Bot use: >= 1 enemy visible at 800..4000 cm for > 1 s. Bots vs it: none (warning voice for the player only).

### 3.1 Motoko - THERM-OPTIC CAMO (`optical`, icon 8 / menu 0)
* Seen [M]: white haze vignette (`screen_op_camouflage`: `screen_camoflage.dds`) for the whole time; shimmering blue
  dots/lines on the body when it starts/ends (`camouflage_start` 0.67 s, `_end` 0.5 s, `camouflage_line`, lens flare
  `camouflage_lens_effect_*`); shooting does NOT end it (she kills while cloaked); hit while cloaked -> electric flicker
  (`camouflage_attack` 1.4 s). Duration bar 100 % -> 30 % over **15 s** then it ends (every one of 7 activations in TDM);
  ready again **58-62 s after activation** (TDM + Demolition, 9 cycles). Older video: the bar drained 5x faster while
  running+shooting than standing (~5 s vs ~30 s) - that build differed; use the TDM numbers.
* Spec: duration 15 s (T1) / 18 / 22 [E]; cooldown 60 s. Sounds `cloaking_start`, `cloaking_loop` (loop while active),
  `cloaking_end`. Effects: `SK_CAMO_ON` at start, `SK_CAMO_OFF` at end, `SK_CAMO_HIT` when damaged. Overlay `camo`
  (add, fade in/out 0.3 s). Third person (others looking at her): draw her model with **tint (0.55,0.65,0.75, 0.12)** using
  blend SRC_ALPHA/ONE (a faint additive ghost) - visible up close, invisible far (alpha *= clamp(1 - d/1500, 0, 1)); while
  she fires: alpha 0.35 for 0.3 s. The HUD chip "ACTIVE CAMO" / "SILENT CAMO" light up (already wired to camo_t).
* Bots vs it: `sk_can_see()` = 0 beyond 400 cm; within 400 cm visible after 0.8 s of LOS; any shot she fires reveals her to
  bots within 2500 cm for 1.0 s. A bot that loses her keeps shooting at the last seen spot for 1 s (existing lost_t logic).
* Bot use: when it sees an enemy at > 1500 cm and has not been seen itself, or when HP < 50 % and taking fire.

### 3.2 Saito - HEAT SCAN (`heatscan`, icon 3 / menu 1)
* Seen [M]: activation glitch (0.3 s), then for 5 s the bar drops in 4 steps of 1.25 s (pulses); enemies behind walls show
  as red heat silhouettes/streaks (`heat_scan_target_moving` / `_stop`: running / standing figures, 0.33 s fades,
  `heat_scan_target.nif` target reticle), off-screen enemies get red edge arrows; the scan front is a huge plane sweeping out
  (`heat_scan_line_first.nif`, 5 s, 100 m planes). Another clip shows a much longer drain (~70 s full) at the same tier - unknown.
* Spec: 4 pulses (T1, every 1.25 s: `is_heat_scan_woosh` + overlay `heat_mul` 0.4 s + `vig` tinted (0.9,0.35,0.15)
  and a ground ring expanding 0 -> 4000 cm in 1.0 s drawn with cell `sk_heat` / `sk_ring` as a vertical band around the
  scanner). Each pulse reveals every enemy within 4000 cm (no LOS needed) for 1.6 s (so the reveals overlap = continuous
  while active): draw `sk_heatghost.gvm` mesh 0 (moving) or 1 (standing, speed < 50) at their position/yaw, blend ONE/ONE,
  **depth test off**, colour (1.0,0.32,0.12) fading over the 1.6 s; `is_heat_scan_detect` once per pulse if anything was found.
  T2 6 pulses, T3 8 pulses [E]. Start: `is_heat_scan_start`, `SK_HS_FLASH` at Saito's head.
  The revealed set is shared with Saito's team (ally bots get the positions).
* Bots vs it: enemy bots are not told. Ally bots: revealed enemies count as "seen" for target selection (pre-aim), but they
  still need LOS to shoot.
* Bot use: no enemy visible for > 4 s and > 2 enemies alive.

### 3.3 Togusa - SPECIAL RECON (`specialrecon`, icon 13)
* Data [D]: `SpecialRecon_ngon_complete.nif` builds a **mini-Tachikoma** (its meshes are named TACHIKOMA_S: the
  `CB_SPIDERMINE\CB_TACHIKOMA_MINI` model); `SpecialRecon_warning*.nif` rings **r = 1157 cm**; `SpecialRecon_radar_ally/
  _enermy` radar sweep (2.4 s loop); `SpecialRecon_upper_warning_*` marker above heads (`kal.dds`); `detect_noise_*`
  UI (equalizer / sound-wave HUD); `is_noise_scan_start/loop (15.86 s)/end` + `_square/_triangle` blips; `is_mini_tachikoma_*`
  (spawn, stand_up, motor, jump, alarm_loop, destroyed, exp); `d_op_warn_specialrecon` (the scanned side is warned).
  No skill video: the behaviour below is reconstructed from those assets. Icon 13 (sound waves).
* Spec: AIM (place up to 700 cm: hologram of `sk_minitachi.gvm`), confirm -> the unit lands (`is_mini_tachikoma_spawn2`,
  `_stand_up3`, `SK_BUILD`), stands still, `is_noise_scan_start` then `_loop` for **16 s**. Every 0.5 s: each enemy within
  **1157 cm** (T1; T2 1400, T3 1700 [E]) whose horizontal speed > 250 cm/s (running, not walking/crouching) or who fired in the
  last 0.5 s is **marked** for 3 s: icon (cell `sk_kal`, red, 12x40 cm billboard 30 cm above the head) visible to Togusa's
  team through walls + minimap dot; first mark per enemy: `is_noise_scan_triangle` (owner) and `d_op_warn_specialrecon` +
  red `vig` pulse for the marked player. A radar ring (cell `sk_ring`, cyan, flat, r 0 -> 1157 every 2.4 s) on the ground.
  Unit HP 80, can be shot (hit sphere r 30 at +20 cm) -> `is_mini_tachikoma_destroyed2` + `SK_DESTROY`; end: `is_noise_scan_end`
  + `SK_UNBUILD`.
* Bots vs it: enemy bots that get the warning stop sprinting for 3 s (they walk), and shoot the unit when it is within 1500 cm
  and in LOS and no operator is visible. Ally bots treat marked enemies as seen (like heat scan).
* Bot use: when it reaches a junction (path node with > 2 neighbours) and no enemy is visible.

### 3.4 Ishikawa - SENTRY GUN (`sentrygun`, icon 1 / menu 7)
* Seen [M]: hologram placement (white turret + footprint ring), 2 s build, a 4-legged gatling turret with a laser sight
  (`turret_lazer_start/_loop`, cyan for allies / red for enemies, `turret_target.nif` on the target), "SENTRY KILL";
  ground arc indicator `turret\turret_degree_90.nif` / `_180.nif` + `turret_360.dds` and "360" under the turret in the
  T2-T3 clip -> **firing arc 90 / 180 / 360 deg by tier** [D]; TURRET.nif carries a 6-tube **grenade pod** (`Grenade_Fire01..06`,
  `CB_Turret_Grenade_*`, `is_turret_rocket_01..04`) -> T3 [D/E]. The bar (HP) drops only when shot.
* Spec: place <= 300 cm, facing = the player's yaw. Build 2 s (`is_turret_hacking_deploy` + `_deploy_loop`, `SK_BUILD`), then
  `is_turret_hacking_start3`, idle hum `is_turret_hacking_loop2`. Lifetime 30 s [E], HP 300 [E]. Targeting at 5 Hz: nearest
  enemy (operator, drone, mini-Tachikoma) within 2500 cm, inside the arc (+-45 / +-90 deg / any around its facing), in LOS
  from the `laser` attachment -> `is_turret_hacking_alarm` + `SK_TU_LOCK` at the laser node, 0.4 s spin-up
  (`_fire_start3`), then fires 10 shots/s (`_fire_loop3`), 9 dmg each [E], spread 1.5 deg, using the base `MUZZLE_3P` +
  `TRACER` effects at the `muzzle` attachment, laser beam (cell `sk_beam`, 1 cm wide, cyan/red) from `laser` to the target.
  Yaw/pitch turn at 240 deg/s. T3: every 6 s fires 2 grenades from `gren` (arc, 1000 cm/s, explode like a frag: reuse the
  grenade explosion, 60 dmg r 300) [E]. Damage taken: `is_turret_hacking_dmg2_0N`; destroyed: `_destroyed2` + `SK_DESTROY`;
  expiry: `_disassemble_start` -> `_disassembled` + `SK_UNBUILD`. One turret per Ishikawa (a new one removes the old one).
  Draw the arc on the ground while placing and for 2 s after the build: cell `sk_sector` (90 deg wedge, r 250 cm) x1/x2/x4.
* Model `sk_turret.gvm`: bones `base, yaw, pitch` (rigid parts), attachments `yaw` (pivot), `pitch` (pivot), `muzzle`, `laser`,
  `gren`, `tag` (name plate height), `fx`. Mesh 3 = grenade pod (skip it below T3 with `md_mesh1 = 3`). Pose: `P[yaw] =
  rotZ(yaw) about att.yaw`; `P[pitch] = P[yaw] * rotX(pitch) about att.pitch`. Clip `scan` = a +-45 deg idle sweep (or drive it).
* Bots vs it: an enemy bot that is shot by a turret targets the turret if no operator is visible (turret hit sphere r 45 cm at
  +60 cm); bots avoid pathing through a visible turret's arc when they have > 1 alternative (cheap: skip).
* Bot use: when engaged and the target is beyond 1000 cm, or when holding an objective (Demolition site) - place at feet.

### 3.5 Borma - METABOOST (`metaboost`, icon 2 / menu 2; video `endocoverFusion`)
* Seen [M]: blue caustic screen (`screen_metaboost`: `caustic_wave`), a big cyan ring expands from him and **links allies**
  (white lines to each linked ally, `is_metabolism_link` per ally, `our_meta_boost_start` on them), HP climbs: clip 1
  49 -> 170 in 7.5 s (under fire), clip 2 250 -> **450** in 7.5 s (accelerating: ~+20/s at first, ~+45/s at the end), the
  extra HP stayed for ~10 s more then HP snapped back to the 250 max. Bar: 7.5 s heal phase, then 10 s bonus phase.
* Spec: linked = Borma + allies within **1000 cm** [E] at activation (re-check every 1 s, link radius 1200 to keep).
  Heal phase 7.5 s: each linked ally gets `heal(t) = total * (t/7.5)^2` (accelerating), `total = 0.8 x max HP`, capped at
  `cap[T] = 1.0 / 1.4 / 1.8 x max HP` [M for 1.8 at T2-video, E for the others]; bonus phase 10 s: HP above max kept,
  then clamped to max. Sounds `is_metabolism_start`, `_loop` (heal phase), `_link` per linked ally, `_end`; `SK_META_ON`
  on each linked ally at start, `SK_META_TICK` on each every 0.5 s while healing; overlay `meta` + `vig` tinted
  (0.1,0.5,0.6) pulsing 2 Hz for the linked player. Lines: one `sk_beam` quad from Borma's chest to each linked ally for 1 s.
  AP is untouched (the original has no AP); with the engine's HP/AP keep it HP-only.
* Bot use: own HP < 50 % or >= 2 allies within 1000 cm below 70 %. Bots vs it: none.

### 3.6 Paz - RUSH (`rush`, icon 9 / menu 3; video `SPRINTACC`)
* Seen [M]: activation, bar full; Paz sprints extremely fast at an enemy and grabs him (CQC takedown: "NICE KILL",
  third-person clip shows the grab-and-stab), bar drains fast while dashing (~2.2 s full) and slowly otherwise (~21 s full);
  camera effects `cam_wd_inrush_ready/start/air/hit/land` (dash camera), `Screen_sprint_circle` speed lines,
  `foley_co_dash_01`, `foley_co_rush_stop_001`.
* Spec: energy 1.0; ACTIVE = rush mode: drains 0.048/s; while sprinting it drains **0.45/s** (T1; T2 0.36, T3 0.30) and
  sprint speed x2.0 (accel to full in 0.15 s), no weapon. Touching an enemy operator (capsule distance < 80 cm, within +-45
  deg of the run direction) while dashing = takedown: victim dies (counts as a melee kill), `cqc_split_paz_attack_f` /
  `cqc_split_<victim>_death_f|b` voices, `SK_RUSH_HIT` + base `HIT_BLOOD`, rush ends. Energy 0 or skill button = end
  (`foley_co_rush_stop_001`). Overlay `rush` while dashing (scale 1 -> 1.15), `SK_RUSH` every 2 frames at his feet
  (d = run direction). FOV +8 deg while dashing (cheap and reads well).
* Bots vs it: none (a rushing Paz is just fast). Bot use: an enemy visible within 600..2000 cm in LOS -> activate and run at it
  (path straight if LOS).

### 3.7 Azuma - PLASMA SHIELD (`plasmashield`, icon 17) - matched to the game's Shield video (2026-10-29)
Source: the game's own skill video `ui.dat ui\GFx_DA\Data\rsc\video\skill\Shield.usm` (712 x 400, 30 fps, 1178 frames; demuxed
on the PC to `work/skills3/Shield.m1v`, frame strips `work/skills3/f_*.jpg`, `fr_*.png`), the HUD bar per frame (`bar.py`
-> `bar.csv`), and the NIFs `model\Object\shield\azuma_{ally,enemy}_shield_tier{1,2}.nif` (dumped with /root/fx/nifps.py).
* **What it is**: a curved panel the holder carries in front of him - a 3D object, not a screen layer. NIF groups: `start`
  (0.17 s: the rim shrinks 3x -> 1x and fades in, a round glow 0.10-0.17 s, the hex surface fades in 0.13-0.17 s), `loop` (the
  solid rim `Shape02/05`; `CollMesh` as_out_tier2 x 0.5 with the outline shader `g_OutlinePow 2 / g_OutlineExp 0.75`; `T08`
  as_solid_tier2 x 0.25 additive; Tier 2 adds `T03` upup: a line rising over the panel in 1 s, every 2 s; `damage`
  cm_noise__big_line whose emissive cycles R -> G -> B every 0.067 s), `end` (= lowered / timed out, 0.17 s: rim and hex surface
  grow 1x -> 3x and fade, glow 0.5 -> 0 in 0.1 s), `crack` (= broken: the rim grows away; 6 / 11 pieces fly forward and out
  in 0.17 s and fade by 0.33 s), Tier 2 also `open` / `fold`. Ally emissive (0,1,1) cyan, **enemy NIFs (1,0,0) red**.
* **Where** (first-person rim fitted to the video, two-way chamfer on the teal pixels, `tools/skills/host/skshield`): the
  NIF origin 50.8 cm ahead of the eye, 21.4 cm below it, centred on the view line (the panel node's -6.5 cm offset cancelled),
  facing the body yaw; the view pitch does not tilt it (the panel moves on screen when looking up / down). Tier 1 panel 81 x 101
  cm, Tier 2 86 x 153 cm, both curved round the eye (x = a + b y^2), so face-on from the eye: in first person only the rim and
  a faint haze at the sides show; from the side (third person) the hex surface lights up as cyan glass.
* **Looks** (first person): steady = a thick teal rim (~7-8 px), clear inside; raise = a cyan wash (glow) for ~0.1 s; hit =
  fine horizontal noise lines + a faint tint over the panel, and the ring flash at the impact (`shield_hit.nif`, our SK_SH_HIT);
  lowered = a cyan hex wash (the hex surface growing 3x) for ~0.15 s; break = translucent cyan shards flying out, ~0.2 s.
  Tier 2: the same rim nearer the screen edges (taller panel), a band of hex cells rising every 2 s.
* **Rules** (the bar under the crosshair, measured per frame): **Tier 1 = 9 s** in 1 s steps (42 px bar: 37 / 32 / 28 / 25 / 19
  at 1 / 2 / 3 / 4 / 5 s), **Tier 2 ~22 s** (smooth); the bar is the time left and **keeps running while the shield is lowered**
  (frames 54-75 off: next step still at 84; Tier 2 frames 440-560 off: still draining); hits do not move the bar - the panel has
  its own HP and breaks (Tier 1 broke at 5.9 s under fire with 45 % of the bar left; the skill then ends). The player fires
  through his own panel. HP 400 / 700 (download 300) [E]. Prompt "[Q] - DEACTIVE SHIELD" / "REACTIVE SHIELD" (ours: D-pad
  right glyph). Cooldown / cost: 30 s / 500 [as before].
* **Port** (skills.h `sk_draw_shields`, `tools/skills/make_shield.py` -> `engine/src/sk_shield_geo.h` + `build/data/
  sk_shield.gtx` 256 x 256 DXT5, 65,552 bytes): the panel from the NIF outline as rows x 6 columns (UVs = the NIF's planar map),
  the rim as an eye-facing ribbon (1.5 cm), every phase above, ally cyan / enemy red, batched in two sprite draws (additive, then
  the rims) for all shields; quads that add nothing are skipped. No full-screen layer any more (the old `shield` /
  `shield_hit` sk_ovl cells are unused). Collision OBB = the same panel (origin above, half 40.6 x 50.6 / 43 x 76.4 cm).
  Comparison: `tools/skills/out/cmp_shield_fp.jpg` (game frame | host render over a shield-off frame of the same video),
  `out/shield_tp_renders.jpg` (third person ally T1 / T2, enemy).
* Not Azuma's (as far as the video shows): the **dome** (`Dome\azuma_sp_shield.nif`) appears in the video around another
  player (frames 472-520, while the player's own shield was lowered; frames ~900-1150 a dome over allies on stairs) - probably
  the late-2016 "Therm-optic Barrier" (Kurutsu, SKILLS.md 8.2). Not implemented.
* Bots vs it / bot use: unchanged (strafe when facing a raised panel; raise it under fire from the front with HP < 70 %).
* **b50 pass (2026-11-05: "render properly, look as good as they can")** - checked in Vita3K (E-Center bright, Ghost Storage
  dark; test switch `skshield`: everyone Azuma, bots shield at once, the player cycles up 2 s / lowered 1 s / Tier 1 and 2):
  - Order: shields and walls are drawn after the particles (`sk_draw_world_late`, main.c M26), far to near (the rims are alpha
    blended); the player's own panel last and without the depth test (a wall within 50 cm no longer cuts it; the gun is drawn
    after the depth clear, so it stays in front). Coplanar layers are additive with depth writes off: no z-fighting.
  - Fog: additive layers fade by (1 - fog), rims and wall edges turn to the fog colour (the world's formula, `md_fogp`); the
    EMP sphere fades too.
  - First person: no hex layer (the game's video shows none face-on: only the rim and a side haze); the fill at the NIF's 0.25
    (T08 as_solid) gives the haze - measured inside the rim against the video: cyan excess game +12..+20, ours +14..+18.
  - Tier 2's rising band is glowing hex LINES (as_dark_ally_tier2) masked by upup.dds swept up in 1 s every 2 s (NIF T03) -
    the video's frames 342-350 - not hex cells.
  - Hit: one layer of fine lines (cm_noise at its full 256 rows, region B2), colour / strength measured on frame 186
    (+18 / +32 / +20 over the panel, rows +-20; ours +18 / +32 / +23) - before: two layers, coarse bands, R/G/B cycling.
  - Shimmer: the hex surface (third person) multiplied by its dark map (turret_laser_noise, scrolling 1 / 8 s per axis, NIF),
    sampled per vertex.
  - Cost (host fragment count, `tools/skills/host/skshield/render.py`; screens of 960 x 544, every covered pixel shaded once):
    own Tier 1 up 1.47 -> 0.66, Tier 2 up 2.31 -> 0.85 (+0.44 while the band sweeps, 1 s in 2), hit 2.87 -> 1.36; raise 2.4 ->
    1.6 and lowered 2.0 (0.17 s each); third person 0.02 - 0.06. The sprite program is half precision (1 instruction + 1 read):
    ~1.0 ms per screen by tools/gpumodel's fit (k 0.3345 ns, read 4.81), blending not included.
  - Data: sk_shield.gtx 256 x 512 DXT5 (131,088 B; + regions H hex lines, B2 hit lines), sk_shield.gsg 7,040 B (+ chunk SHX1:
    dark map 32 x 32, fill profile, upup profile), both from make_shield.py (databuilder step 'shield').

### 3.8 Kurutsu - EMP SHOCKWAVE (`emp`, icon 16)
* Seen [M]: placement hologram of the 3-legged device with its range ring labelled "EMP" (`emp_ally_fx.nif`: `emp_line.DDS`
  ring + `emp_breakline`, `scan_grid`), placed, a blue sphere grows over ~1.5 s, then a flash + lightning: "BARRIER
  DESTROYED" (an enemy cyber wall), "SENTRY DESTROYED"; enemy characters get electric sparks (`character\emp_ch_ele.nif`, 3 s)
  and a glitchy screen (`screen_emp`, 0.8 s). The device top spins while charging (`emp_rotation_fx`, `GIS_emp` operate_turn clips).
  `is_emp_tier1` = a separate T1 blast sound [D].
* Spec: place <= 700 cm; fuse **2.0 s** (bar 3 steps) with `is_emp_beep`, `SK_EMP_CHG` every 0.1 s at the device, model clip
  `spin`, a translucent sphere `sk_dome.gvm` growing r 0 -> R tinted (0.3,0.5,1,0.25); then blast (`is_emp_exp`, T1
  `is_emp_tier1`; `SK_EMP_EXP` at +60 cm): every enemy deployable (sentry, wall, drone, recon unit, missiles in flight) within
  R = **500 / 650 / 800 cm** [E] is destroyed (`SK_DESTROY`); every enemy operator within R in LOS of the device gets
  "EMP'd" for 3 s [E]: active skill cancelled (camo off, shield off, rush/dive end), cooldown +5 s, HUD ring blinks; screen
  overlay `glitch` tinted (0.4,0.6,1) 0.8 s + `SK_EMP_SHOCK` on him every 0.3 s for 3 s; 10 damage [E]. The device then stays
  1 s and vanishes (`SK_UNBUILD`). Ground ring while placing/charging: cell `sk_ring` (cyan) at R.
* Bots vs it: enemy bots within R + 200 that hear `is_emp_beep` move away (repath to a point R+300 away from it) - skip if it
  costs too much; EMP'd bots can't use skills for 3 s and aim 2x worse.
* Bot use: an enemy deployable visible within 1500 cm, or >= 2 enemies within 800 cm of a point it can see.

### 3.9 Maven - CYBER WALL (`cyberwall`, icon 5 / menu 6)
* Seen [M]: she holds the wall launcher (`weapon\CN_CW`), a white frame hologram on the ground, throws the projector
  (`CBWALL.nif`), a cyan hex-grid wall stands (`CyberWallDepth.nif` plane **300 x 200 cm** [D]); enemies see it red; an enemy
  walking **through** it gets a hacked screen (green noise then red horizontal lines, ~1.5 s) and a red marker over his head
  ("is_screen_through"); "BARRIER DESTROYED" when Kurutsu's EMP hits it. Bar (= wall life): ~20 s when attacked, slow drain idle.
* Spec: place <= 700 cm, wall plane centred on the point, facing the player (normal = toward Maven). Build 2 s (`is_screen_start`,
  `is_screen_set1/2`, `SK_BUILD` at 5 points along it), life **20 s** [M-ish], HP 600 [E] (`is_screen_loop` while up).
  Size T1 300 x 200, T2 400 x 250, T3 500 x 300 [E]. **Enemy bullets** crossing the quad are stopped (impact = `SK_SH_HIT`
  + hp -= dmg); ally bullets pass. Characters pass through freely; an **enemy** crossing it (sign change of the plane distance
  inside the quad) gets: `is_screen_through`, `SK_WALL_PASS` on him, overlay `glitch` tinted (0.2,0.8,0.3) 0.6 s then
  `hlines` 0.9 s (scroll v), and is **revealed** to Maven's team for 5 s (red `sk_kal` marker, through walls). HP 0 ->
  `is_screen_exp_0N` + `SK_UNBUILD`; expiry -> `is_screen_off` + `SK_UNBUILD`. Draw: `sk_wallpod.gvm` (projector) at the base
  centre + `sk_wall.gvm` (unit panel, additive) with a model matrix scaling x by the width and z by the height, tint cyan
  (0.4,0.9,1,0.8) for allies, red (1,0.3,0.25,0.8) for enemies; flicker alpha 0.6..1 at 8 Hz for the last 2 s.
* Bots vs it: their bullets stop at it (they keep shooting = they burn its HP); path straight through (they get the debuff).
* Bot use: engaged with an enemy at > 1200 cm and HP < 60 % -> place between (at 300 cm toward the enemy).

* **Look, checked against the game's CYBERWALL video (2026-10-29, `work/skills3/w_all.jpg`, `w_key.jpg`)**: the wall is a nearly
  clear pane with a thin bright edge (ally cyan, enemy red with a faint red fill and grid); in third person faint cyan with
  square cells lighting up. Our additive hex panel at 0.8 was far too strong: walls are now drawn in the shield passes
  (`sk_draw_shields`) as a faint fill (ally 0.07, enemy 0.11) + a thin edge ribbon; build-up height and end flicker as before.
  Comparison `tools/skills/out/cmp_cyberwall.jpg`. (`sk_wall.gvm` is only the fallback without sk_shield.gtx and the placement
  preview.)
### 3.10 Reiko - CYBER DIVE (`cyberdive`, icon 18)
* Seen [M]: white flash, then her view leaves the body: a dark teal "cyber world" (`1p_cyber_world`, `cyber_sky`), her own
  hands as cyan holograms, flying fast **through walls**, enemies shown as red rings; after ~4-5 s she dives into an enemy
  (white flash, `screen_dive*`) and is back in her body; the enemies she hacked are then easy kills ("FIRST BLOOD",
  "DOUBLE KILL"). Third person: her body stands still on a cyan ring (`cyber_dive_ally_loop`: intergate ring, dive_sprite)
  - `is_dive_hologram_start/end/dmg_0N`: the body is the "hologram" that can be hit. Victim effects in the data:
  `cyber_dive_enemy_hit` (cube/mosaic glitch on the victim), `screen_debuff` = the **Laughing Man** logo over the screen
  (`smileman_*`), `screen_debuff_noise`. Strings `Ingame_DiveReady` / `Ingame_DiveCancel`.
* Spec: ACTIVE (dive) **5 s** [M]: her body stays (idle anim, `SK_DIVE_ON` at start, ring cell `sk_disk`+`sk_dring` cyan under
  it); her camera becomes a free ghost: moves 1200 cm/s in the view direction (stick), **no collision** (noclip), clamped to
  1500 cm from the body and to the map bounds; overlays `dive_mul` (mul2x) + `dive` (add); enemies within 2000 cm drawn as
  red `sk_ring` billboards (60 cm) at chest height through walls. R while an enemy is within 400 cm and inside 15 deg of the
  view = **dive into him**: `SK_DIVE_HIT` on him, `is_dive_start_fp_02`, white flash 0.2 s, camera back to the body; the
  victim is **hacked for 3 / 4 / 5 s** [E]: overlay `laugh` (centre, 40 % of the screen height) + `glitch` noise for players,
  can't fire or use skills, HUD hidden; bots: no firing, aim drifts. Timeout or skill button = `Ingame_DiveCancel`: back with no
  hack. Damage to the body during the dive: ends it at once (`is_dive_hologram_dmg_0N`) and the damage applies.
  Sounds: `is_dive_start_fp_01` (enter), `ui_ghost_dive_in`, `is_dive_hologram_start/_end`, `ui_ghost_dive_end2` (return).
* Cheapest faithful: no separate cyber-world geometry - the normal scene + the two overlays + fog distance 3000 + walls
  are passed through by the camera only; the red rings are 1 quad per enemy.
* Bots vs it: hacked bots stop shooting for the hack time and turn randomly; an enemy bot that sees the standing body shoots it.
* Bot use (bots don't fly a camera): when an enemy is visible within 2500 cm -> instantly "dive" it after 1.5 s of standing
  still (body vulnerable), applying the hack.

### 3.11 Sitara - DMD DRONE (`dmddrone`, icon 19)
* Seen [M]: aim marker with a vertical light pillar ("Click to deploy"), the drone flies in, hovers ~3-4 m above the point and
  lasers enemies around it (orange beam hits, `dmd_*_laser`, `dmd_enemy_damage` mosaic on the victim, red screen
  `screen_dmd_enemy` for the lasered player), "DRONE KILL", it also kills deployables ("SENTRY DESTROYED"); bar = drone life:
  ~20 s while engaging, ~90 s idle. Area `dmd_*_area.nif` (cylinder r 100-130 cm, scaled at run time).
* Spec: AIM: target point by raycast along the view, <= 3000 cm (pillar: cell `sk_pillar` 40 x 600 cm axis quad + `sk_disk`).
  Confirm: the drone spawns 1500 cm above Sitara and flies to point + 350 cm z in 1.5 s (`is_dmd_start_03`, `_motor_start`,
  `_motor_loop`), hovers with a slow bob (`is_dmd_loop_0N`), yaw toward its target. Energy 1.0: drains 0.011/s idle and
  0.05/s while lasering (= ~90 s / 20 s). Targeting at 5 Hz: nearest enemy (operator or deployable) within 900 cm horizontal of
  the hover point, in LOS from the drone -> laser: beam quad cell `sk_beam` (3 cm wide, orange (1,0.45,0.3) ally view / red
  enemy view) from `laser` att to the target chest, `is_dmd_heat_loop_01`, `SK_DL_HIT` at the hit every 0.1 s, damage
  **20 dps** (T1; 26, 32 [E]); the victim player gets `vig` tinted (0.9,0.1,0.05). HP 150 (hit sphere r 45): destroyed ->
  `SK_DESTROY` + `is_dmd_end_01`; energy out -> flies up and away 1 s (`_motor_end`).
* Model `sk_drone.gvm`: one rigid part; attachments `laser`, `eye1`, `eye2`.
* Bots vs it: a bot being lasered targets the drone if it can see it (60 %), else runs out of the 900 cm area.
* Bot use: engaged target behind cover (lost sight < 3 s ago) -> drone at the last seen point.

## 4. Assets produced

All tools in `/root/gits/tools/skills/`; raw inputs (extracted NIF/DDS + PNG conversions) in `/root/gits/raw/skills/`.
`sklib.py` (NIF rest-pose skinning, static meshes, rigid GVM1 writer), `make_skill_models.py`, `build_skills_fx.py`,
`make_skill_overlays.py`, `preview_gvm.py` / `preview_overlays.py` / `host/skgallery.c` (checks), `usm_demux.py` (skill videos),
`extract_skill_assets.py` (how raw/skills was pulled from the .dat files). Outputs of the checks: `out/*.png`.

### 4.1 Models (`build/data/sk_*.gvm`, GVM1, z up, cm, facing -y; every mesh rigid on one bone; clip 0 = `rest`)
**Update 2026-10-27:** sk_turret / sk_minitachi / sk_emp are now SKINNED to the game's own rigs with the game's own clips
(`make_skill_rigs.py`: turret deploy / idle / fire_start / fire / fire_end / damage / destroy / undeploy / folded / turn; mini-
Tachikoma deploy (4.7 s pop-up) / idle / walk / damage / destroy / jump / land / folded; EMP deploy / spin_start / spin / undeploy
/ folded), drawn on the GPU palette path; previews `out/rigged_deployables.png`. The DMD drone's and brain-shield machine's
clips are .nsa files our decoder does not read (the drone rig is a single bone anyway). New: `vm_armlauncher.gvm` (Batou's
first-person launcher arm, `make_vm_armlauncher.py`; overlay on the footage `out/armlauncher_vm_vs_footage.png`).
| file | content | bones | meshes / tris | attachments | extra clips |
|---|---|---|---|---|---|
| sk_turret.gvm | Ishikawa's sentry (TURRET.nif) | base, yaw, pitch | 4 / 8015 (mesh 3 = T3 grenade pod) | yaw pitch muzzle laser gren tag fx | scan |
| sk_drone.gvm | Sitara's DMD drone (DMD_DRONE.nif) | body | 1 / 5346 | laser eye1 eye2 | - |
| sk_emp.gvm | Kurutsu's EMP device (CB_EMP.nif) | legs, body, top | 3 / 3886 | body top head | spin |
| sk_minitachi.gvm | Togusa's recon unit (CB_TACHIKOMA_MINI.nif) | body, legs | 2 / 3972 | eye | walk (body bob) |
| sk_wallpod.gvm | Maven's wall projector (CBWALL.nif) | pod | 1 / 1284 | lens shield tag | - |
| sk_wall.gvm | wall panel, unit size x -0.5..0.5, z 0..1, additive hex grid, two-sided | wall | 1 / 128 | - | - |
| sk_shield.gvm | Azuma's T2 panel (azuma_ally_shield_tier2.nif): fill + hex lines + rising lines, additive, two-sided, centred | panel | 3 / 228 | - | - |
| sk_dome.gvm | upper hemisphere r 100 (azuma_sp_shield.nif), hex lines, additive (T3 shield, EMP sphere) | dome | 1 / 368 | - | - |
| sk_missile.gvm | arm-launcher missile (armlauncher_missile.nif), 18 cm along -y | body | 1 / 142 | tail nose | - |
| sk_heatghost.gvm | heat silhouettes (heat_scan_target_moving / _stop), 170 cm, white, additive | body | 2 / 2160 (0 = running, 1 = standing) | - | - |
Load with `model_load` (+ `model_keep_cpu = 1` before it and `model_prepare_rigid` after it, so each mesh takes the
one-entry-palette GPU path), pose = `model_sample(&M, clip, t, P)` then edit `P[bone]` for yaw/pitch etc., draw with
`model_draw`. Previews: `out/models_preview.png`. Rebuild: `python3 make_skill_models.py [outdir] [only...]`.

### 4.2 Effects (`build/data/fx_sk.gfx` = the base fx.gfx tables + skills; atlas 1024 x 2048)
`build_skills_fx.py` imports `/root/fx/build_fx.py` (does not run or modify it), appends 23 cells, 64 emitters (41 + 23 zero-count cell markers),
25 effects and 4 cell-directory effects, and writes `fx_sk.gfx` (+ `--separate fxs.gfx`: skill effects only, for a second fx instance).
Base effect ids 0..15 are unchanged, so `fx_sk.gfx` is a drop-in replacement for `fx.gfx`. **When the grenade explosion is in
build_fx.py, just re-run `python3 build_skills_fx.py`** - it picks the new base tables up (asserts on name clashes and on
fx.h's 64-effect limit; now 45 used). Resolve ids by name at init:
```c
static int fx_find(const char *n) { for (int i = 0; i < fx_neff; i++) if (!strncmp(fx_eff[i].name, n, 16)) return i; return -1; }
// a skill cell (ground rings, beams...): k = index in SK_CELLS (order below)
static int fx_skcell(int k) { char n[16]; snprintf(n, 16, "C_DIR%d", k / 6); int e = fx_find(n); return e < 0 ? -1 : fx_em[fx_eff[e].em[k % 6]].cell; }
```
SK_CELLS order (k): 0 sk_expl, 1 sk_smoke, 2 sk_flare, 3 sk_lenz, 4 sk_ring, 5 sk_dring, 6 sk_disk, 7 sk_upline, 8 sk_meta,
9 sk_ele (4x2), 10 sk_shard (4x4), 11 sk_beam, 12 sk_box, 13 sk_mosaic, 14 sk_scan, 15 sk_kal, 16 sk_glow, 17 sk_sprint,
18 sk_sector (90 deg wedge, apex at u0,v1), 19 sk_pillar (bright at v1), 20 sk_heat, 21 sk_hex, 22 sk_spark.
Draw them with `sprite_quad(..., fx_cell[c] uv ...)` + `sprite_flush_mode(VP, fx_tex, 1)` (additive) in the world pass.

| effect | spawn with | at |
|---|---|---|
| SK_CAMO_ON / _OFF | n = (0,0,1) | her feet (particles start at +95 cm) |
| SK_CAMO_HIT | n up | her feet, on each hit while cloaked |
| SK_AL_FIRE | n = d = launch direction | launcher muzzle |
| SK_MS_TRAIL | n = -velocity | missile tail, every 1/30 s |
| SK_MS_EXP | n = surface normal | impact |
| SK_BUILD / SK_UNBUILD | n up | deployable base, every 0.4 s while building / once |
| SK_DESTROY | n up | deployable centre |
| SK_TU_LOCK | n = toward target | turret `laser` att |
| SK_META_ON / SK_META_TICK | n up | each linked character's feet |
| SK_RUSH | n up, d = run dir | Paz's feet, every 2 frames while dashing |
| SK_RUSH_HIT | n = -run dir | victim chest |
| SK_SH_HIT | n = panel normal | bullet hit point on shield / wall |
| SK_SH_BREAK | n up | shield centre |
| SK_WALL_PASS | n up | intruder's feet |
| SK_DL_HIT | n = -beam dir, d = beam dir | laser hit point, every 0.1 s |
| SK_DIVE_ON / SK_DIVE_HIT | n up | Reiko's body / victim's feet |
| SK_EMP_EXP / SK_EMP_CHG | n up | device |
| SK_EMP_SHOCK | n up | EMP'd character's feet, every 0.3 s |
| SK_RE_PING | n up | recon unit, each scan tick |
| SK_HS_FLASH | n up | Saito's feet (flash at +160) |
Gallery (engine fx.h on the host, software GL): `out/fx_gallery.png`; harness `host/skgallery.c`.

### 4.3 Screen overlays (`build/data/sk_ovl.gtx`, GTX1 DXT1 1024x1024, load with menu.h's `menu_tex_load`)
UV table: `out/sk_ovl.txt` (also below). One quad over the screen (or the given part) with the vertex colour as tint;
`add` = `glBlendFunc(GL_ONE, GL_ONE)`, `mul2x` = `glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR)` (grey 128 = unchanged).
| cell | blend | used by |
|---|---|---|
| vig | add | generic edge glow: tint cyan (heal), red (DMD laser, recon warning), orange (heat pulse) |
| camo | add | Motoko cloaked |
| meta | add | metaboost (scroll v slowly, pulse 2 Hz) |
| shield | add | Azuma shield up, first person |
| rush | add | Paz dashing |
| dive / dive_mul | add / mul2x | Reiko diving (mul first) |
| glitch | add | EMP hit, wall pass, heat-scan start (jitter UVs per frame) |
| hlines | add | wall pass (after glitch) |
| heat_mul | mul2x | heat pulse |
| laugh | add | dive victim: Laughing Man in the centre (40 % of the screen height) |
| shield_hit | add | shield absorbed a hit (0.2 s) |
Preview (composited on a TDM frame): `out/overlays_preview.png`; rebuild `python3 make_skill_overlays.py`.

### 4.4 Already in the build (no new data needed)
* HUD icons: hud.ghud `skill.1,2,3,5,8,9,11,12,13,16,17,18,19` (`out/hud_skill_icons.png`).
* Sounds and voices: all in `sfx_*.gsnd` / `voice_*.gvp` (names in section 3).

## 5. Fixes needed elsewhere (for you; not done)
* `menu.h MENU_CHR`: skill names/descriptions/icons. Menu icon frames that exist (GameLoading skillIconMc, `out/menu_skill_icons.png`):
  0 camo, 1 heat scan, 2 metaboost, 3 rush, 5 mini-Tachikoma (summon), 6 cyber wall, 7/8 sentry, 4 empty. Batou, Togusa, Azuma,
  Kurutsu, Reiko, Sitara have none -> add InGameUI's `skillMC.skillIcon.iconN` to menu.ghud (as make_hud.py does) or draw
  hud.ghud's `skill.N`. Current table: bato 2 (wrong), saito 5 -> 1, togusa 6 (wrong), ishikawa 1 -> 7, borma 7 -> 2,
  azuma 8 (wrong), kuro "HACK" -> "EMP SHOCKWAVE", maven 7 -> 6, reiko 6 (wrong), sitara 3 (wrong).
* Suggested texts: ARM LAUNCHER "Turn your arm into a missile pod: lock on to enemies and fire homing missiles." / THERM-OPTIC
  CAMO "Bend light around your body and become nearly invisible." / HEAT SCAN "Pulse a heat scan: see enemies through walls." /
  SPECIAL RECON "Deploy a recon Tachikoma that tracks every noisy enemy around it." / SENTRY GUN "Deploy an automated sentry
  gun." / METABOOST "Boost your team's metabolism: fast healing for you and nearby allies." / RUSH "Charge at full speed: take
  down any enemy you reach." / PLASMA SHIELD "Raise a plasma shield that stops bullets from the front." / EMP SHOCKWAVE "Plant
  an EMP that fries enemy devices and shocks enemy operators." / CYBER WALL "Raise a holo wall: it stops enemy fire and hacks
  enemies who cross it." / CYBER DIVE "Dive into the net and hack an enemy's cyberbrain." / DMD DRONE "Call in a drone that
  burns enemies with its laser."
* `main.c op_voice()`: kuro -> `"emp"`; `H_.skill_icon` from the table; the skill button.
* AUDIO_NOTES.md: "Kuro has no voice lines: use togusa's" is wrong (voice set `emp`).

## 6. Proposed skills.h API

```c
// skills.h - operator skills (12 + bots). One pool, no allocation; the engine supplies world queries via callbacks.
#pragma once
#include "player.h"   // V3, raycast
#include "fx.h"
#include "model.h"

enum { SK_ARMLAUNCHER, SK_OPTICAL, SK_HEATSCAN, SK_SPECIALRECON, SK_SENTRYGUN, SK_METABOOST, SK_RUSH, SK_PLASMASHIELD,
       SK_EMP, SK_CYBERWALL, SK_CYBERDIVE, SK_DMDDRONE, SK_COUNT };            // == CHARS[] / MENU_CHR order
enum { SKS_READY, SKS_AIM, SKS_ACTIVE, SKS_COOLDOWN };
enum { SKO_NONE, SKO_TURRET, SKO_WALL, SKO_DRONE, SKO_EMP, SKO_RECON, SKO_DOME };

typedef struct {
	const char *id, *name, *desc;     // "armlauncher" (voice token), menu strings
	int hud_icon, menu_icon;          // hud.ghud skill.N, GameLoading frame or -1
	int placed;                       // 1: AIM shows a placement hologram
	float cooldown[3], duration[3];   // per tier (T1..T3)
	float p1[3], p2[3];               // per-skill tier values (locks, radius, cap, arc, ...)
} SkillDef;
extern const SkillDef SK_DEF[SK_COUNT];

typedef struct {                       // one per operator in the match: index 0 = the player, 1 + i = bot i
	int skill, team, tier, state;
	float t, cool, energy, bar;        // bar: 0..1 for the HUD (energy / life of the deployable)
	float fx_t, flag_t;                // effect timers (pulses, reveal, shock...)
	int obj;                           // deployable slot or -1
	int locks[4], nlocks;              // arm launcher
	float hack_t, emp_t, reveal_t;     // received effects: hacked (dive), EMP'd, revealed to the other team
	V3 dive_cam; float dive_yaw, dive_pitch;
} SkillActor;

typedef struct { int type, team, owner, hp, alive; V3 pos; float yaw, pitch, t, life, energy, build; int target; float scan_t, fire_t; } SkObj;

typedef struct {                       // world access the engine provides (keeps skills.h independent of bots.h/main.c)
	int (*n_actors)(void);
	V3  (*pos)(int a);  V3 (*vel)(int a);  float (*yaw)(int a);  int (*alive)(int a);  int (*team)(int a);
	V3  (*eye)(int a);  V3 (*aim_dir)(int a);
	int (*los)(V3 a, V3 b);                                   // map ray, 1 = clear
	void (*damage)(int victim, float dmg, int attacker, int kind);   // kind: 0 bullet, 1 blast, 2 melee kill, 3 laser
	void (*heal)(int a, float hp, float cap_mul);            // cap_mul x max HP
} SkWorld;

int   skills_init(const char *vdir, const SkWorld *w);   // loads sk_*.gvm, sk_ovl.gtx, resolves fx names; < 0 = failed
void  skills_reset_match(void);
void  sk_actor_set(int a, int skill, int team);           // at spawn / operator change
void  sk_set_tier(int a, int intel);                     // tier = 1 + (intel >= 400) + (intel >= 800)
int   sk_press(int a);                                   // skill button pressed: READY->AIM/ACTIVE, toggles, cancels
int   sk_fire(int a);                                    // fire pressed: 1 = consumed by the skill (place / launch / dive)
void  sk_on_death(int a);
void  skills_update(float dt);                           // all actors, deployables, missiles

// hooks the engine calls from its own code
int   sk_can_see(int observer, int target);              // 0 hidden (camo/far), 1 normal (still needs LOS), 2 known through walls
float sk_bullet(V3 from, V3 dir, float len, int shooter, float dmg);   // shields / walls / deployables: returns the shortened
                                                                        // length if absorbed (spawns the hit fx), else len
int   sk_hit_object(V3 from, V3 dir, float len, float dmg, int shooter); // bullets vs turret/drone/recon (-1 none)
float sk_speed_mul(int a);                               // rush 2.0, else 1
int   sk_can_fire(int a);                                // 0 while hacked / in launcher AIM / diving
int   sk_camera(int a, V3 *eye, float *yaw, float *pitch); // 1 = override the camera (cyber dive)
float sk_fov_add(int a);                                 // rush +8 deg
int   sk_bot_want(int a, int target, float dist, int sees, float hp_frac);  // 1 = use the skill now (section 3 "bot use")

// drawing
void  sk_draw_world(const float *VP, const float *cam, const float *R, const float *U); // deployables, holograms, ghosts, beams, rings
void  sk_draw_actor_tint(int a, float *tint, int *additive);   // camo ghost tint for an actor model (call before drawing it)
void  sk_draw_overlays(int a);                           // full-screen layers of the local player (after the 3D, before the HUD)
void  sk_hud(int a, int *icon, float *cool, float *bar); // fills HudState fields; bar < 0 = no active bar
```
Integration order per frame: `sk_set_tier` -> input (`sk_press`, `sk_fire` before the weapon) -> `skills_update` ->
movement (`sk_speed_mul`) -> bots (`sk_can_see`, `sk_bot_want`) -> bullets (`sk_bullet` / `sk_hit_object` before the
character/map hit tests) -> camera (`sk_camera`) -> draw world, `sk_draw_world` after opaque + before fx -> overlays -> HUD.
Replace the current `camo_t/cool_t` code in main.c and `BotTarget.alive = ... camo_t <= 0` with `sk_can_see`.

## 7. Not determined (DB.dat / StringTable.dat are encrypted; no footage)
* Cooldowns of everyone but Motoko, and all damage / HP numbers: estimates.
* Per-tier changes except Azuma (panel sizes / dome), Ishikawa (arc 90/180/360, grenade pod) and Kurutsu (separate T1 blast sound).
* Togusa's skill has no video: the recon behaviour is reconstructed from its effects/sounds (mini-Tachikoma + 11.6 m noise
  scan + warning); the threshold for "noisy" is my choice.
* Saito: a second clip drains ~14x slower at the same tier (maybe an aim/scope state or a chip).
* What exactly the cyber-dive hack does to the victim (the data has the Laughing Man screen + noise; I chose blind + no fire).
* Whether Maven's wall blocks ally bullets / enemy movement (the footage shows people walking through it; I chose: blocks
  enemy bullets only).
* Batou's arm launcher first-person viewmodel (not built: needs the 30-bone skin + GIS_base_1st clips through make_vm_weapon.py).
* Skill chips (per-operator passive upgrades, `rsc\item\rewards\<Op>_Skill_Chip_N.png`: cooldown -, duration +, HP +...) - not in scope.
* `is_fake_*` sounds (fake start/loop/set/line/hit/wave/shake/alert) belong to no listed skill - probably a cut decoy skill.

## 8. Footage pass (2026-10): what the gameplay videos show (supersedes the economy in sections 1-3 where they differ)

Three builds of the game appear in the footage; they differ in the skill economy:
* **Tutorial** (old build): one ring, intel *cost* (TIER 1 at 200, TIER 2 at 400, activation spends the intel).
* **3v3 TDM + PSS9 Demolition** (old build): one ring, intel fixed at 300, red cooldown arc, camo 15 s / 60 s.
* **The 7 operator videos** (final build, Neon Ruins, Electronics Center, Geofront, Underground Base, Urban City, HQ):
  big intel ring "TIER 2" (max = the operator's Tier-2 cost) + a small "TIER 1" circle with a numeric countdown.
  **The user's InGameUI.swf is this final build** (intelMC has t1basic / t1disMC / t2disMC / cooltimeCount / skillbtn1+2,
  transferMC / transSkill1-4 / shaReady / skillTier2Notify), so the port follows the final build:

| [M] measured | Tier 1 cooldown (from activation) | Tier 2 cost (ring max) | Tier 1 | Tier 2 |
|---|---|---|---|---|
| Motoko (Neon, HQ, Elim) | 25 s (24..00) | 400 | camo ~5 s | camo ~45 s + SkillSync |
| Batou (Electronics Center) | 25 s | 500 | arm swap 0.7 s, aim <= ~5 s, ONE rocket, arm back 0.8 s | not used in the video |
| Reiko (Geofront) | 30 s | 400 | white flash, ~3 s dive in a black / teal-wireframe cyber world, enemies as white holograms, dive-in = white sphere + radial lines | - |
| Sitara (Underground Base) | 30 s | 600 | wrist gesture, "DMD" ground frame, drone flies in from above, bar = drone life | bigger "DMD" volume, red floor grid, drone lives ~100 s (or until killed, survives the owner's death) |
* Tier 1 and Tier 2 share the key (both badges read "N"); Tier 1 can be used while Tier 2 is ready; Tier 2 can be used while
  Tier 1 cools down. While a Tier-2 skill (or its deployable) runs, the Tier-1 circle shows the "no" icon. Dead / round end:
  both "no". Intel is kept through death; kills fill the ring ("DOUBLE KILL +180", headshot +150 ...).
* "TIER 2 SKILL READY": yellow person icon + yellow text at the screen centre with a yellow line, Operator portrait
  "Therm-optic Camo ready." (op_<id>_ready). No notice when Tier 1 comes back.
* Active bar under the crosshair (person icon + cyan bar 607..672 x 593..601 at 1280x720) for both tiers.
* Cloaked first-person weapon = bright white/silver shimmering gun; white haze at the screen edges.
* **SkillSync** (tutorial text + Neon 100-104 s + Electronics Center 295-326 s): "Many Tier 2 skills can be shared with
  allies. A white line will link to nearby allies able to share your Tier 2 skill. Up to 3 allies can SkillSync your skill,
  granting them similar effects." / "A link line will appear when your teammate activates their Tier 2 skill. Approach your
  teammate and press [E] to download their skill. You will receive the Tier 1 effect of the shared skill."
  Sharer side: white floor lines to linkable allies (drawn white), 3 slots next to the gauge ("CCC") that fill with the
  names of allies who synced, "SKILLSYNC COMPLETED +20". Receiver side: cyan floor line to the sharer, cyan ring under him,
  "[E] <icon> SkillSync" box above his head; after the download a small icon of the synced skill (with a timer arc) above
  the Tier-1 circle; Batou's synced camo lasted ~30 s. Seen shared: camo, heat sensor (tutorial), plasma shield and
  metaboost icons on Reiko's gauge.
* Skill activation gesture (1st person): the left forearm with the cyberbrain wrist device rises (GIS_base_1st
  base_M4A1_1st_skill_start_*, model\Objects\CB_CC).

### 8.1 Therm-optic camo, final build: measured (2026-10-27; supersedes 3.1 where they differ)
Method: the cyan bar under the crosshair (1280x720: x 607..672, y 593..601) sampled at 10 fps over the whole of the three
Motoko videos (`work/skills2/scan2.py`, `segs.py`, `ends.py`; CSVs `work/skills2/{neon,elim,hq}.csv`), plus frame strips at
10 fps around activations / ends (`work/skills2/act_neon12.jpg`, `act_neon49.jpg`, `act_elim22.jpg`, `end_neon17.jpg`,
`neon318.jpg`, kill cam `kc_318.70.jpg`) and the gauge crops `work/skills/crop/*-bl-*.jpg` (1 fps).
* **Tier 1 = 5.0 s** in all 21 activations (Neon 12.1, 49.5, 94.1, 419.1; Elim 22.6, 64.1, 124.0, 202.1, 355.4, 391.9, 419.6, 593.7;
  HQ 33.5, 127.7, 233.6, 281.4, 415.7, 454.8, 524.8, 663.6 ...). Its bar is **5 steps of 1 s** (0.98 / 0.79 / 0.59 / 0.39 / 0.20).
* **Tier 2 = 44.0 s** (Neon 100.3 -> 144.3, 262.0 -> 306.0, 367.0 -> 410.9; Elim 142.8 -> 186.8; HQ 592.4 -> 636.5); a smooth bar.
  Costs the full 400 ring (ring empties at activation); the "CCC" SkillSync slots show for its whole length.
* **Tier-1 cooldown 25 s from activation** (the TIER 1 circle shows 24 at the activation frame, 00 24 s later, "TIER 1" next second).
  It **keeps counting during a Tier 2** (Neon 100-118: 18..00 next to the CCC slots), then shows the "no" icon until the Tier 2 ends;
  it also keeps counting while dead (Neon: 24 at 315, died 318.6, 12 at 327 after the respawn).
* **Activation is instant** in the final build: bar + countdown appear in the same frame, no wrist gesture, gun stays up; the white
  edge haze and the chrome-white first-person gun are there 0.1-0.3 s later. (The Early Access tutorial build still plays the
  ~1.3 s left-wrist gesture first: EA tutorial 435.6-437.0.) End: the haze fades over ~0.6 s.
* **Nothing but death ends it early**: firing (up to 33 ammo-counter changes inside one 5 s Tier 1), taking damage (Neon 317.9-318.5:
  HP 150 -> 064 while the bar kept stepping), sprinting and vaulting ("W + SPACE" prompt during camo, Neon 17.0 / 130) all leave
  the 5 s / 44 s untouched. Death ends it (Neon 318.6: 3.9 s); the kill cam shows the body still chrome for ~0.2 s.
* Tier 2 is used within ~1-3 s of the ring filling (Neon ring full 97 -> used 100.3; 366 -> 367.0); Tier 1 almost every time it
  is back, going into a fight. No Tier 2 during a running Tier 1 was seen (Neon 94.1 T1 ends 99.0, T2 at 100.3).
* Tutorial text (EA build): "Therm-optic Camo cloaks your body for a short time. Therm-optic Camo is excellent for sneaking around
  enemies, but you are not completely invisible." / "Thermoptic Camo will not make you completely invisible, but you will be much
  harder to spot." -> the bots' rule (sk_can_see): invisible beyond 4 m, seen within 4 m after 0.8 s, any shot shows her for 1 s
  within 25 m, allies always see her.
* Third person (kill cam, `kc_318.70.jpg`): a cloaked Motoko is a chrome / silver reflective body, clearly readable at 2-3 m.
* SkillSync download of the camo (Batou, Electronics Center 296 -> 326.5): **30 s**, shown as a small person icon with a shrinking
  cyan arc above the TIER 1 circle (no bar under the crosshair); the white haze is on for the whole download.
* Move speed while cloaked: no visible change (not measurable precisely from footage; kept normal). Sounds: `cloaking_start`
  (event intel_skill/cloaking_start), `cloaking_loop` (3.95 s RAM loop, local player), `cloaking_end`; `cloaking_hud_on` when
  Tier 1 is back. (The `ghost_cloaking_*_tp` events have dmax 1.0 with a curve that ends at 0: silent in our runtime.)
* Not determined: what the blank key badge under the TIER 1 circle / the ring means (Neon 145-178, 340-357; the badge shows no
  letter while the skill is otherwise ready).

### 8.2 Late-2016 matches (round-2 footage: n1..n8, 2026-10-27)
Videos (BRIEF "new footage"): n1 Adman Berutalube TC, n2 Machina Media Ruined Tokyo TC (Resistance HQ map label), n3 Rob Cram
Port (BETA BUILD watermark), n4 Adman Objective Rush Mid (Downtown), n6 nopproblem District Ruins TC, n7 Andu Train station
Demolition (30 Dec 2016), n8 StefMeister PSS9 TDM. Scripts and outputs in `work/skills2/` on the PC: `scan2.py` / `segs*.py`
(bar under the crosshair), `icons.py` + `icons_n*.jpg` (gauge crops every 8 s), `donut.py` + `donut_crops.py` (commander
messages, 2 fps teal-name detector, crops `dc_n*.png` + times `dc_n*.txt`, OCR with tesseract), `pc1.jpg` (portraits),
strips `st_n6_*.jpg`, `tb_n*.jpg`.
* **Economy**: these builds use the *intel-cost* gauge (TIER 1 at 200, TIER 2 at 400 for Motoko; Borma 300 / 400, Maven T2 500,
  Saito ~300; activation spends the intel; the gauge shows the intel number, a TIER badge and the key). Camo Tier 1 there =
  **15 s** in 15 bar steps (n7). Our HUD (b31 shot-18) is the Neon-era gauge (TIER 2 ring + TIER 1 cooldown circle + SkillSync
  slots) that section 8 / 8.1 measured, so the port keeps that economy; the late-2016 numbers are recorded here only.
* **No own use of the 8 unmeasured skills**: the players are Motoko (n2, n6, n7 most of the time, n8), Batou, Borma / Saito / Paz
  in n7 (no activation in those periods: 206-218 s round end, 788-803 s Saito killed first), Maven in n3 (no activation).
  n1's player has a skill whose HUD icon is frame 11 ("SUMMON", kill notice "SEEKER KILL +200"; enemy message "Enemy Seeker
  Drone user detected!" in n7) - an operator / skill that is not in our data.
* **Final-build skill names** (from the commander messages; ours in brackets where they differ): Arm Launcher, Therm-optic Camo,
  **Heat Sensor** [heat scan], **Cyber Sentry** [sentry gun], **Hyper Sprint** (enemy message: "Hyper-Sprint") [rush],
  **Nano-gel Armor** = Borma's skill in this build [metaboost], **Therm-optic Barrier** = Kurutsu's skill in this build
  [EMP shockwave] (an enemy one is "detected", like a deployable), Seeker Drone (unknown operator). What Nano-gel Armor and
  Therm-optic Barrier do was not visible (they may be different skills from the ones the voice set / effects describe).

### 8.3 Commander-box skill messages (late-2016 footage; implemented: skills.h `sk_msg*`, main.c draws them)
Every message box (portrait in a ring, name in teal, white text on a dark box, top left, ~5 s) of n2 / n6 / n7 / n8 was found
with `donut.py` and read (tesseract + `pc1.jpg` check). The skill ones:
| when | portrait / name | text |
|---|---|---|
| an ally uses his skill | the ally's operator / player name | "An ally activated Arm Launcher!", "An ally activated Heat Sensor!", "An ally activated Hyper Sprint!", "An ally activated Nano-gel Armor!", "An ally activated Therm-optic Barrier!", "An ally deployed a Cyber Sentry!", "An ally engaged Therm-optic Camo." |
| an enemy skill (operator line `op_<id>_enemy`) | the Operator (cm.face.12, name "Operator") | "Enemy Therm-optic Camo user detected!", "Enemy Heat Sensor user detected!", "Enemy Hyper-Sprint user detected!", "Enemy Arm Launcher user detected!", "Enemy Nano-gel Armor user detected!", "Enemy Seeker Drone user detected!", "Enemy Cyber Sentry detected!", "Enemy Therm-optic Barrier detected!" |
| own Tier 2 ready (`op_<id>_ready`) | the Operator | "Therm-optic Camo ready." |
| an ally downloads your shared Tier 2 | that ally | "Share code transmission complete." (one per ally, n6 104 / 109 / 114 s) |
| you download an ally's Tier 2 | the sharer | "Transfer complete." (n2 340 s, 4 s after "An ally activated Hyper Sprint!") |
Other messages in the same box (not skills; for the modes / tank workstreams): Chief Aramaki "You are clear to proceed with the
mission.", "Capture the terminal!", "Terminal captured.", "Terminal capture failed", "Mission Success", "Bomb that target right
away!", "The bomb has been planted!", "One Enemy Remaining", "Time is running out!", "Begin defensive Operations.", "Eliminate
all enemy agents!", "Enemy suppression failed!"; Operator "Your Tachikoma is ready to call in.", "Warning: Enemy Think Tank
deployed.", "WARNING: Enemy Think Tank redeployed!"; Tachikoma "Terminal support inbound!". The name label is mixed case
("Chief Aramaki", "Operator", "Tachikoma", player names).
* Portraits (InGameUI donut chaIcon frames = ovl pack `cm.face.N`, checked against the footage): 1 Motoko, 2 Batou, 3 Saito,
  **4 Paz** (n2 "Reyfer", Hyper Sprint), 5 Ishikawa, **6 Togusa** (the mullet), 7 Borma, 8 Kurutsu, 11 Aramaki, 12 the Operator,
  13 Tachikoma. (menu.h's enum has FACE_TOGUSA = 4 / FACE_PAZ = 6: swapped.) 9 / 15 / 16 / 17 not seen.
* Our rules (skills.h): ally use posts at activation (instant skills), when the arm launcher comes up, at placement (deployables:
  "deployed a/an"); the camo uses "engaged ... ."; enemy messages come with `sk_warn` (an enemy skill seen within 30 m, or it
  targets / hacks / EMPs / reveals the player; one per 6 s); "ready." only for Tier 2 (Neon footage: no notice for Tier 1).
  Names: the footage names where seen, ours for the rest (Special Recon, Metaboost, Plasma Shield, EMP Shockwave, Cyber Wall,
  Cyber Dive, DMD Drone). Queue: a message shows 5.45 s (the box's fade); a newer one replaces one shown >= 1.5 s (n6 439 -> 442 s:
  two ally messages 3 s apart), else waits (3 at most). Not during the first 7.5 s (Aramaki's start message), a tutorial, or
  Elimination's own commander lines. Render: `out/skill_messages_vita.png`, footage vs port `out/cmp_skill_messages.png`.
* How often skills come up in these matches (14 min TC n6, 13 min TC n2; human players, intel economy): each Batou fires the arm
  launcher about once per 3 min (n6: 13 uses by 3 players), each Ishikawa deploys a sentry once per ~3.5 min (n2: 7 by 2),
  Paz sprints once per ~3-4 min, Saito scans once per ~4 min, the Kurutsu (barrier) once per ~2-3 min; the player (Motoko) got
  "camo ready" 12 times in n6; SkillSync of a shared camo: 2-3 allies within ~10 s, 6 times in n6. Our bots (Neon economy:
  25-30 s Tier-1 cooldowns) use skills more often than that by design (the Neon Motoko footage: Tier 1 "almost every time it
  is back"); no change made.
