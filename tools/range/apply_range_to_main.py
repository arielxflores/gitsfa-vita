# usage: python3 apply_range_to_main.py /root/gits/engine/src/main.c out.c   (writes a patched copy; review, then move it over main.c)
# applies the range.h integration to a copy of main.c (anchored replacements; each anchor must occur exactly once)
import sys
src, dst = sys.argv[1], sys.argv[2]
s = open(src).read()
E = [
 ('#include "menu.h"\n', '#include "menu.h"\n#include "range.h"\nstatic int nbot_full;   // the bots made at start; maps with practice targets (range.h) run with none\n'),
 ('\ttex_check(0);\n\treturn 0;\n}', '\t{ int rr = range_load(VDIR, MAPS[mi].stem); if (rr) logp("range: %d practice targets (%s.grange)%s\\n", rr, MAPS[mi].stem, rr < 0 ? " FAILED" : ""); }\n\ttex_check(0);\n\treturn 0;\n}'),
 ('\tfloat pos[3], yaw = 0, pitch = 0, P[16], V[16], MVP[16], fwd[3];\n', '\tnbot_full = nbot; if (range_active()) nbot = 0;\n\tfloat pos[3], yaw = 0, pitch = 0, P[16], V[16], MVP[16], fwd[3];\n'),
 ('\t\t\tif (bots_ok && nspawn) { BotTarget T = { pl.pos, pl.vel, 1 }; bots_pick_chars();', '\t\t\tnbot = range_active() ? 0 : nbot_full;\n\t\t\tif (bots_ok && nspawn) { BotTarget T = { pl.pos, pl.vel, 1 }; bots_pick_chars();'),
 ('int spect = autoplay && map_t < TOUR_SPECT && bots_ok && !fly;', 'int spect = autoplay && map_t < TOUR_SPECT && bots_ok && nbot > 0 && !fly;'),
 ('int tb = bots_visible_nearest(eye, 2600);\n\t\t\t\tif (tb >= 0) {\n\t\t\t\t\tV3 c = bots[tb].chest;',
  'int tb = bots_visible_nearest(eye, 2600); V3 rc_ = eye; int rt_ = tb < 0 ? range_visible_nearest(eye, 7000, &rc_) : -1;\n\t\t\t\tif (tb >= 0 || rt_ >= 0) {\n\t\t\t\t\tV3 c = tb >= 0 ? bots[tb].chest : rc_;'),
 ('\t\t\t\t\t\t\tif (bi >= 0) { if (nph < 24) { PHit *q = &ph[nph++]; q->bi = bi;',
  '\t\t\t\t\t\t\tfloat rt_ = 0; int rh_ = 0, ri_ = range_hit_test(eye, dir, bi >= 0 ? bt : dist > 0 ? dist : 15000, &rt_, &rh_);\n'
  '\t\t\t\t\t\t\tif (ri_ >= 0) { int dead_ = range_damage(ri_, rh_ ? D->dmg_h : D->dmg, rh_, vadd(eye, vmul(dir, rt_)), dir); phits++; if (rh_) pheads++; hit_marker = 0.2f;\n'
  '\t\t\t\t\t\t\t\tif (dead_) kill_marker = 0.7f; else if (!snd) { play(rh_ ? "ui_hit_heatshot2" : "ui_hit_common_", rh_ ? 210 : 190); snd = 1; } }\n'
  '\t\t\t\t\t\t\telse if (bi >= 0) { if (nph < 24) { PHit *q = &ph[nph++]; q->bi = bi;'),
 ('\t\t\t\t\tlogp("  grenade exploded at %.0f %.0f %.0f\\n", c.x, c.y, c.z);', '\t\t\t\t\tif (range_blast(c, R, 160)) kill_marker = 0.7f;\n\t\t\t\t\tlogp("  grenade exploded at %.0f %.0f %.0f\\n", c.x, c.y, c.z);'),
 ('\t\tif (cool_t >= 0) { cool_t -= dt;', '\t\trange_update(dt, v3(pos[0], pos[1], pos[2]));\n\t\tif (cool_t >= 0) { cool_t -= dt;'),
 ('if (map_audio_pending) { map_audio_pending = 0; amb_for_map(VDIR, MAPS[cur_map].stem, 170); music_play(VDIR "bgm_in_game_dm_round.gstr", 0, 190, 300); voice_play("d_aramaki_tdm_gamestart", 230, 0); play("ui_game_start_mode", 200); }',
  'if (map_audio_pending) { map_audio_pending = 0; amb_for_map(VDIR, MAPS[cur_map].stem, 170); if (!range_active()) { music_play(VDIR "bgm_in_game_dm_round.gstr", 0, 190, 300); voice_play("d_aramaki_tdm_gamestart", 230, 0); play("ui_game_start_mode", 200); } }'),
 ('if (match_t > 0 && (match_len > 0 || autoplay)) match_t -= dt;', 'if (match_t > 0 && (match_len > 0 || autoplay) && (!range_active() || ap_finish)) match_t -= dt;'),
 ('\t\tif (autoplay && vm_ok && WSC->res < WD->mag) WSC->res = WD->res;\n', '\t\tif (autoplay && vm_ok && WSC->res < WD->mag) WSC->res = WD->res;\n\t\tif (vm_ok && range_refill(&WSC->ammo, &WSC->res, WD->mag)) logp("  range: ammo refilled (%d in reserve)\\n", WSC->res);\n'),
 ('\t\t  fx_draw(MVP, pos, R_, U_);', '\t\t  range_draw(MVP, pos, R_, U_); fx_draw(MVP, pos, R_, U_);'),
 ('H_.aim_red = bots_ok && bots_hit_test(eye, dir, 6000, &bt_, &bh_) >= 0; }',
  'H_.aim_red = bots_ok && bots_hit_test(eye, dir, 6000, &bt_, &bh_) >= 0;\n\t\t\t\t  if (range_active()) { float wd_ = raycast(eye, dir, 9000, NULL, NULL); if (range_aim(eye, dir, wd_ > 0 ? wd_ : 9000) >= 0) H_.aim_red = 1; } }'),
 ('\t\t\t\thud_begin();\n\t\t\t\tif (scoped)', '\t\t\t\tif (range_active()) { H_.nfeed = 0; H_.nchip = 0; H_.mm_tex = 0; H_.notify = NULL; H_.ntag = 0; H_.nblip = 0; }   // the range shows none of these\n\t\t\t\thud_begin();\n\t\t\t\tif (scoped)'),
 ('\t\t\t\thud_game_draw(&H_, now / 1e6f);\n', '\t\t\t\thud_game_draw(&H_, now / 1e6f);\n\t\t\t\trange_hud(MVP, now / 1e6f);\n'),
 ('\t\t\tif (audio_late) logp(', '\t\t\tif (range_active()) { char rs_[160]; range_status(rs_, sizeof(rs_)); logp("  %s\\n", rs_); }\n\t\t\tif (audio_late) logp('),
]
bad = 0
for old, new in E:
    n = s.count(old)
    if n != 1: print('ANCHOR %d TIMES: %r' % (n, old[:90])); bad += 1; continue
    s = s.replace(old, new)
open(dst, 'w').write(s)
print('applied %d of %d edits' % (len(E) - bad, len(E)))
sys.exit(1 if bad else 0)
