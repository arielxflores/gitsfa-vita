# Operator skills: integration into the engine

`engine/src/skills.h` holds the whole skill system (12 operators, bots included). It talks to the game only through
`SkWorld` (callbacks) and a handful of hook calls. `apply_skills_to_main.py` adds those calls to `main.c` and the bot hooks
to `bots.h` as anchored edits:

    python3 tools/skills/apply_skills_to_main.py IN_main.c OUT_main.c [IN_bots.h OUT_bots.h] [--no-autoplay-skills]

Every edit has a short anchor that must occur an exact number of times. If an anchor is missing, ambiguous or already
patched, the script stops and writes nothing. Without the `bots.h` pair, `main.c` still builds: the bot hooks are compiled
out (`#ifdef BOT_SK_HOOKS`), and bots then only ignore a cloaked player, as they do today.

Data on the Vita (`ux0:data/gitsfa/`): `fx_sk.gfx` (replaces `fx.gfx`, which stays as a fallback), `sk_turret.gvm`,
`sk_drone.gvm`, `sk_emp.gvm`, `sk_minitachi.gvm`, `sk_wallpod.gvm`, `sk_wall.gvm`, `sk_shield.gvm`, `sk_dome.gvm`,
`sk_missile.gvm`, `sk_heatghost.gvm`, `vm_armlauncher.gvm` and `sk_ovl.gtx`. If any of these are missing, that part simply
isn't drawn. `sk_turret.gvm`, `sk_minitachi.gvm` and `sk_emp.gvm` are rigged with the game's own clips
(`tools/skills/make_skill_rigs.py`, from the Data/anim sets GIS_turret / GIS_smalltachi / GIS_emp); `vm_armlauncher.gvm` is
Batou's first-person launcher arm (`tools/skills/make_vm_armlauncher.py`, GIS_base_1st rig 38).

## main.c edits

| id | anchor | edit | why |
|---|---|---|---|
| M1 | `#include "dem.h"` | `#include "skills.h"` after it | the skill system (after every header it uses) |
| M2 | `int main() {` | the `skw_*` / `skb_*` block before it | `SkWorld` callbacks: positions, HP, damage (the player through `self_dmg`, the same path as bot bullets; bots through `bot_hurt` with the kill bookkeeping of a gun kill: score, intel, feed, "ROCKET KILL" ...), heals, LOS, firing, bot state; the bots.h hook functions; actor sync (player = `cfg.chr`, bot = its `chrt` stem, tier from intel / bot kills x100) |
| M3 | `int fr = fx_init(VDIR "fx.gfx");` | load `fx_sk.gfx`, fall back to `fx.gfx` | the SK_* effects and atlas cells (base ids 0..15 unchanged; skill ids looked up by name) |
| M4 | `if (sr < 0) logp("skinning shader failed` | `skills_init(VDIR, &SKW, md_ok)` before it | loads models and overlays, resolves effect names (after fx / HUD / model shader) |
| M5 | `cfg_apply(); pvoice = op_voice(cfg.chr);` | `skw_bind(...)` + `skw_match_start()` | binds the main() locals the callbacks use (pl, php, pitch, intel, kills, notify ...) |
| M6 | `tex_rechecked = 0; light_at(...)` (map load) | `skw_match_start()`; test run: `cfg.chr = tour_i % 12` | every match starts with all skills ready and nothing deployed; the autoplay tour uses a different operator on each map, which covers all 12 skills on the Vita (`--no-autoplay-skills` drops this) |
| M7 | the 2-line camo block `if (in_skill && cool_t < 0 ...` | `sk_button(0, pressed, held, dt)` | D-pad right = the operator's skill: tap = Tier 1 (or Tier 2 when Tier 1 cools), hold 0.4 s = Tier 2; pressing again cancels AIM, toggles the shield, ends rush / dive. Square near a sharing teammate = `sk_sync_try(0)` (SkillSync download) |
| M8 | `if (dem_on && dem_prog >= 0) want_reload = 0;` | block before it | R confirms placement / fires the launcher / dives in (`sk_fire`). No gun, knife or grenade while placing, diving, rushing or hacked. Autoplay uses the skill when fighting |
| M9 | `player_update(&pl, mx, my, sprint, jump, dt);` | `sk_move_input` + `pl.speed_mul` before it | dive: the stick flies the ghost camera and the body stands still; rush: x2 sprint (only touches `speed_mul` when it is 2) |
| M10 | the player's `bots_hit_test(eye, dir, ...)` line | `sk_bullet(...)` after it | enemy shields / cyber walls stop the shot; turrets, drones and recon units take the damage (with a hit marker) |
| M11 | `snd_gren_exp(&c.x);` | `+ sk_blast(c, 650, 160, 0)` | grenades damage enemy deployables |
| M12 | `if (pitch > 1.5f) pitch = 1.5f; ...` | `sk_camera` before it | cyber dive camera |
| M13 | `// ---- bots` | `skw_frame(dt)` before it | sync actors and tiers, then `skills_update` (before the bots, so skill damage to the player is applied the same frame) |
| M14 | `BotTarget T = { ... camo_t <= 0 };` | `... && !skw_hidden_from_bots()` | visibility is now per bot (`bot_sk_can_see`). Without bot hooks this falls back to "cloaked = ignored" |
| M15 | `if (ev & DEV_RESET) {` | `sk_round_reset()` | Demolition rounds: deployables and active skills end, cooldowns keep running |
| M16 | the 2-line `cool_t` / `camo_t` tick | a comment | skills.h owns the timers (`cool_t` / `camo_t` are left declared and unused) |
| M17 | the main `persp(P, spect ? ...)` | `+ sk_fov_add(0)` | +8 deg FOV while Paz dashes |
| M18 | `range_draw(MVP, pos, R_, U_); fx_draw(...)` | `sk_draw_world(MVP, pos, R_, U_, fog)` first | deployables, holograms, walls, shields, missiles, heat ghosts, rings / beams / markers (after characters, before particles) |
| M19 | `if (spect) {` (viewmodel) | `if (spect \|\| sk_hide_viewmodel(0)) { if (!spect) sk_draw_vm(0, vm_lit);` | no gun in arm-launcher mode (Batou's launcher arm is drawn instead: swap in 1.37 s, aim, shot, fold back 0.95 s) or during the dive |
| M20 | `// ---- HUD: the game's own widgets` | `sk_draw_overlays(0)` | full-screen skill layers (camo haze, metaboost, shield, rush lines, dive, glitch / lines, Laughing Man, edge glows) |
| M21 | `H_.cool = cool_t ... H_.skill_icon = SKI[...]` | `sk_hud(0, &H_.skill_icon, &H_.cool, NULL)` | ring and icon from the real skill state (icon per operator from `SK_DEF`) |
| M22 | `int ca[5] = { ... camo_t > 0 ... }` | `sk_camo_on(0)` | the camo HUD chips |
| M23 | `hud_game_draw(&H_, now / 1e6f);` | `+ skills_hud(0, MVP, now / 1e6f)` | active-skill bar under the crosshair with the small icon (SKILLS.md 2.3), AIM / dive / shield prompts, "EMP" / "HACKED" |
| M24 | minimap blips `int seen = bot_los(...pos..., b->chest);` | `(not cloaked && in sight) \|\| sk_known(1 + i)` | enemies revealed by heat scan / recon / wall pass show on the minimap; a cloaked enemy in sight does not |
| M25 | the start splash + Aramaki line in the in-match screens block (`menu_commander_draw(FACE_ARAMAKI, "ARAMAKI", ...)`) | one line after it: `if (map_t >= 7.5f && match_over_t <= 0 && !tut_on() && !(elim_on && (em_t_cmd < 6 \|\| em_t_lone < 6)) && sk_msg(...)) menu_commander_draw(...)` | the skill messages of the late-2016 footage in the commander box: ally use, enemy skill detected, own Tier 2 ready, SkillSync (SKILLS.md 8.3) |
| M26 | `... fx_draw(MVP, pos, R_, U_);` (the world pass, before the gun's depth clear) | `+ sk_draw_world_late(MVP, pos, R_, U_, fog);` | b50: Azuma's shields and Maven's walls after the particles (far to near), the player's own first-person shield last with no depth test |

## bots.h edits (all hooks NULL = the bots behave exactly as before)

| id | anchor | edit | why |
|---|---|---|---|
| B1 | `static int (*bot_goal)(int i, float *g);` | 6 hook pointers + `#define BOT_SK_HOOKS 1` | `bot_sk_can_see`, `bot_sk_block`, `bot_sk_aim`, `bot_sk_goal`, `bot_sk_move`, `bot_sk_alpha` |
| B2 | `if (k < 0) break; int t = cand[k]; cd[k] = 1e9f;` | skip targets `bot_sk_can_see` hides | camo (hidden beyond 4 m, seen after 0.8 s within 4 m or for 1 s after she fires), hacked bots see nothing |
| B3 | `else if (b->sees && ... && !bot_freeze) {` | `&& !(bot_sk_aim && bot_sk_aim(i) <= 0)` | no firing while hacked, placing, diving or rushing |
| B4 | `p *= fminf(1, 0.3f + b->see_t * 0.3f);` (x2) | `p *= bot_sk_aim(...)` | EMP-shocked bots aim 2x worse |
| B5 | `V3 end = vadd(b->muzzle, vmul(d, 1.6f));` (x2) | `bot_sk_block(...)`: the shot ends on the shield / wall / deployable | plasma shields, cyber walls and deployables stop bot bullets (and are drained / damaged by them) |
| B6 | `if (!(bot_goal && bot_goal(i, g))) {` | `&& !(bot_sk_goal && bot_sk_goal(i, g))` | roaming bots head for enemies their team has revealed (heat scan, recon marks, wall pass) |
| B7 | `// ---- physics (the player's own movement code)` | `bot_sk_move(i, &mx, &my, &sprint, &jump)` before it | rush charge, flee a charging EMP / a drone's laser, warned bots stop sprinting, strafe around a shield, stand still to place or dive, hacked drift, face a deployable they shoot at |
| B8 | bots_draw's `model_draw_cpu(... reskin) ...` line | alpha / additive state around it | a cloaked bot is a faint additive ghost (invisible beyond 15 m) |
| B9 / B10 | the blob / planar shadow loops | skip cloaked bots | no shadow gives a cloaked bot away |

## Checks

* Host: `tools/skills/host/sktest/run_test.sh` (GL and audio stubbed, real data files, every event / sound / voice name
  checked against `audio.h` and `audio_manifest.txt`, `-fsanitize=address,undefined`).
* Host, by eye: `tools/skills/host/skview/run.sh` renders the skills' sprites (rings, firing arcs, beams, drone pillar,
  heat-scan front, through-wall markers) and every screen overlay with the software rasterizer of `/root/fx/host`, into
  `host/skview/out/world.png` and `overlays.png`. Models are not drawn there; see `out/models_preview.png`.
* Vita: apply the script to a copy of `engine/`, then run `cmake -S . -B b && make gitsfa`. Until the grenade agent's
  `fx_explosion` is in `fx.h`, add `static void fx_explosion(const float *p, const float *n) { (void)p; (void)n; }` to
  that copy's `fx.h`.
* After `build_fx.py` changes (for example the grenade explosion), re-run `python3 tools/skills/build_skills_fx.py`. The
  explosion is merged automatically, and skills.h picks it up by name (`EXPLOSION`; until then it falls back to
  `SK_MS_EXP`).

## State for the vision workstream (camo / outlines / heat vision look)

skills.h decides WHAT state an actor is in; vision decides how it looks. All accessors take an actor index (0 = local player,
1 + i = bot i), are cheap and side-effect free, and return "nothing" before `skills_init` or for unknown actors.

| accessor | returns |
|---|---|
| `sk_cloak(a)` | cloak amount 0..1: 0 -> 1 over `SK_CLOAK_IN` (0.3 s) at activation, 1 -> 0 over `SK_CLOAK_OUT` (0.6 s) at the end, over `SK_CLOAK_DEATH` (0.25 s) on death |
| `sk_cloak_flags(a)` | `SKCF_ON` logically cloaked, `SKCF_FADE_IN`, `SKCF_FADE_OUT`, `SKCF_HIT` (hit < 1.4 s ago: electric flicker), `SKCF_FIRED` (fired < 0.3 s ago), `SKCF_NEAR` (an enemy within 4 m), `SKCF_SYNCED` (SkillSync download), `SKCF_TIER2`, `SKCF_EXPOSED` (enemies can see her now: fired < 1 s ago or an enemy near for > 0.8 s) |
| `sk_cloak_shimmer(a)` | 0..1 shimmer strength: 1 at the start of the forming (0.67 s) / dissolving (0.5 s) ramps, flickering after a hit, 0.15 while it holds, 0 without camo |
| `sk_cloak_break(a)` | 0..1 momentary break: a shot 1 -> 0 over 0.3 s, a hit 0.5 -> 0 over 1.4 s |
| `sk_cloak_seen_by(a, o)` | 1 if observer o makes a out (the bots' rule without walls), else 0 |
| `sk_cloaked(a)`, `sk_cloak_left(a)` | logical camo on / seconds left |
| `sk_revealed(b, team)` | bits: 1 heat scan, 2 recon / cyber-wall mark (b shows through walls to `team`) |
| `sk_reveal_left(b, team)`, `sk_reveal_until(b, team)`, `sk_time()` | seconds left / the skills clock time it ends (0 = not revealed) |
| `sk_revealed_by(b, team)` | the actor whose skill revealed b (-1) |
| `sk_heat_pulse(a)` | a's heat scan just pulsed: 1 -> 0 over 1 s, -1 when no scan runs |
| `sk_known(b)` | b is an enemy the local player's team knows through walls (minimap) |

Today skills.h itself still draws: the first-person white haze (`sk_draw_overlays`, cell `camo`, ramps 0.3 / 0.6 s), the heat
ghosts and kal markers (`sk_draw_world`), and bots.h's faint additive ghost for cloaked bots (`sk_draw_alpha` via B8). When
vision draws these, clear the matching bit of `sk_draw_mask` (`SK_DRAW_HAZE`, `SK_DRAW_HEATGHOST`, `SK_DRAW_MARKS`) so nothing is drawn twice;
the cloaked-bot ghost is bots.h B8 (`bot_sk_alpha`). Footage for the look:
SKILLS.md 8.1 (first-person haze + chrome gun `act_neon12.jpg` / `full_neon12_4.jpg`, third-person chrome body `kc_318.70.jpg`).
