#!/usr/bin/env python3
"""Integrates the operator skills (engine/src/skills.h) into the engine as anchored edits on copies of main.c / bots.h.

usage: apply_skills_to_main.py IN_main.c OUT_main.c [IN_bots.h OUT_bots.h] [--no-autoplay-skills]

Every edit names a short, distinctive anchor and the exact number of times it must occur; a missing, ambiguous or
already-patched anchor stops the script with an error and nothing is written. See INTEGRATION.md (same folder) for the
list of edits and why each one is there. Without the bots.h pair, main.c still builds: the bot hooks are compiled out
(#ifdef BOT_SK_HOOKS) and bots then only ignore a cloaked player, as before.
"""
import re, sys

class Patch:
    def __init__(self, path, text):
        self.path, self.text, self.log = path, text, []

    def _count(self, s):
        return self.text.count(s)

    def _need(self, anchor, n, eid):
        c = self._count(anchor)
        if c != n:
            raise SystemExit('%s: edit %s: anchor %r found %d times (expected %d)' % (self.path, eid, anchor, c, n))

    def replace(self, eid, anchor, new, n=1):
        """substring replace (every occurrence; there must be exactly n)"""
        self._need(anchor, n, eid)
        self.text = self.text.replace(anchor, new)
        self.log.append(eid)

    def _lines_with(self, anchor, eid, n=1, exact=False):
        L = self.text.split('\n')
        idx = [i for i, l in enumerate(L) if (l.strip() == anchor if exact else anchor in l)]
        if len(idx) != n:
            raise SystemExit('%s: edit %s: line anchor %r found on %d lines (expected %d)' % (self.path, eid, anchor, len(idx), n))
        return L, idx

    def insert_before(self, eid, anchor, new, exact=False):
        L, idx = self._lines_with(anchor, eid, 1, exact)
        ind = re.match(r'\s*', L[idx[0]]).group(0)
        L[idx[0]:idx[0]] = [ind + s if s else s for s in new.split('\n')]
        self.text = '\n'.join(L); self.log.append(eid)

    def insert_after(self, eid, anchor, new, exact=False, indent=None):
        L, idx = self._lines_with(anchor, eid, 1, exact)
        ind = re.match(r'\s*', L[idx[0]]).group(0) if indent is None else indent
        L[idx[0] + 1:idx[0] + 1] = [ind + s if s else s for s in new.split('\n')]
        self.text = '\n'.join(L); self.log.append(eid)

    def replace_line(self, eid, anchor, new, exact=False):
        L, idx = self._lines_with(anchor, eid, 1, exact)
        ind = re.match(r'\s*', L[idx[0]]).group(0)
        L[idx[0]] = ind + new
        self.text = '\n'.join(L); self.log.append(eid)

    def replace_lines(self, eid, first, last, new, span=4):
        """replace the line containing `first` through the next line (within span) containing `last`"""
        L, idx = self._lines_with(first, eid, 1)
        i = idx[0]; j = next((k for k in range(i, min(len(L), i + span)) if last in L[k]), -1)
        if j < 0:
            raise SystemExit('%s: edit %s: end anchor %r not found within %d lines of %r' % (self.path, eid, last, span, first))
        ind = re.match(r'\s*', L[i]).group(0)
        L[i:j + 1] = [ind + s for s in new.split('\n')]
        self.text = '\n'.join(L); self.log.append(eid)

    def replace_in_line(self, eid, line_anchor, old, new, n_in_line):
        L, idx = self._lines_with(line_anchor, eid, 1)
        c = L[idx[0]].count(old)
        if c != n_in_line:
            raise SystemExit('%s: edit %s: %r found %d times in the line (expected %d)' % (self.path, eid, old, c, n_in_line))
        L[idx[0]] = L[idx[0]].replace(old, new)
        self.text = '\n'.join(L); self.log.append(eid)

    def regex_in_line(self, eid, line_anchor, pattern, new, must_contain):
        L, idx = self._lines_with(line_anchor, eid, 1)
        m = re.search(pattern, L[idx[0]])
        if not m or must_contain not in m.group(0):
            raise SystemExit('%s: edit %s: pattern %r not found as expected in the line with %r' % (self.path, eid, pattern, line_anchor))
        L[idx[0]] = L[idx[0]][:m.start()] + new + L[idx[0]][m.end():]
        self.text = '\n'.join(L); self.log.append(eid)


# ---------------------------------------------------------------- main.c: the world callbacks (inserted before main())
SKW_BLOCK = r'''// ---- operator skills (skills.h, tools/skills/INTEGRATION.md): the world as the skill code sees it. Actor 0 = the player, 1 + i = bot i.
// main()'s locals the callbacks need are bound once (skw_bind); bots get their character's skill and their kills as intel (x100).
static struct { Player *pl; float *php, *pap, *pdead, *pitch, *pflash, *kill_marker, *hit_marker, *notify_t, *streak_t; int *fly, *intel, *kills, *m_pkills, *notify_pts, *streak, *autoplay; const char **notify; } SKB;
static signed char skw_ct_skill[NCHR_MAX]; static int skw_ct_n = -1;
static int skw_n(void) { return 1 + nbot; }
static V3 skw_pos(int a) { return a == 0 ? SKB.pl->pos : bots[a - 1].pl.pos; }
static V3 skw_vel(int a) { return a == 0 ? SKB.pl->vel : bots[a - 1].pl.vel; }
static float skw_yaw(int a) { return a == 0 ? SKB.pl->yaw : bots[a - 1].pl.yaw; }
static int skw_alive(int a) { if (a == 0) return !*SKB.fly && *SKB.pdead <= 0 && *SKB.php > 0; return a - 1 < nbot && bots[a - 1].state != BS_DEAD; }
static V3 skw_eye(int a) { if (a == 0) return v3(SKB.pl->pos.x, SKB.pl->pos.y, SKB.pl->pos.z + (SKB.pl->eye > 0 ? SKB.pl->eye : P_EYE)); return bot_eye(&bots[a - 1]); }
static V3 skw_dir(int a) { float y = skw_yaw(a), p = a == 0 ? *SKB.pitch : bots[a - 1].aim_pitch; return v3(cosf(y) * cosf(p), sinf(y) * cosf(p), sinf(p)); }
static float skw_hp(int a) { return a == 0 ? *SKB.php : bots[a - 1].hp; }
static int skw_los(V3 a, V3 b) { return bot_los(a, b); }
static int skw_damage(int v, float dmg, int att, int kind) {
	if (v == 0) {   // the player: the same path as bot bullets (armour first, death, killer = the bot behind it)
		V3 ap = att > 0 ? skw_pos(att) : SKB.pl->pos; self_dmg += dmg; self_dmg_dir = atan2f(ap.y - SKB.pl->pos.y, ap.x - SKB.pl->pos.x); if (att >= 1) bot_last_hitter = att - 1; return 0; }
	int i = v - 1; if (i >= nbot || bots[i].state == BS_DEAD) return 0;
	int killed = bot_hurt(i, dmg, 0, att == 0 ? -1 : att >= 1 ? att - 1 : -2);
	if (killed && att == 0) {   // the player's skill kill: the same bookkeeping as a gun kill
		static const char *KN[5] = { "SENTRY KILL", "ROCKET KILL", "NICE KILL", "DRONE KILL", "NICE KILL" };
		(*SKB.kills)++; *SKB.kill_marker = 0.7f; ev_play("impact/ui_killshot", NULL); ev_play("impact/killshot_fp", NULL);
		score_l += 100; *SKB.streak = *SKB.streak_t > 0 ? *SKB.streak + 1 : 1; *SKB.streak_t = 4; *SKB.intel += 100;
		*SKB.notify = kind >= 0 && kind < 5 ? KN[kind] : "NICE KILL"; *SKB.notify_t = 0; *SKB.notify_pts = 100;
		feed_add(feed, &nfeed, cfg.name, 1, chr_display(chrt[bots[i].ct].name), -1); (*SKB.m_pkills)++; my_kills_on[i]++;
		logp("  killed bot %d (skill, kind %d)\n", i, kind); }
	return killed;
}
static void skw_heal(int a, float add, float cap) { float *hp = a == 0 ? SKB.php : &bots[a - 1].hp, mx = 100 * cap;
	if (add > 0) { float nh = *hp + add, lim = fmaxf(*hp, mx); *hp = nh > lim ? lim : nh; } else if (*hp > mx) *hp = mx; }
static int skw_firing(int a) { return a == 0 ? *SKB.pflash > 0.04f : bots[a - 1].flash_t > 0.04f; }
static int skw_bot_info(int a, int *t, int *s, float *l, V3 *ls) {
	if (a < 1 || a - 1 >= nbot) return 0; const Bot *b = &bots[a - 1];
	*t = b->tgt == -1 ? 0 : b->tgt >= 0 ? 1 + b->tgt : -1; *s = b->sees && b->state == BS_ENGAGE; *l = b->state == BS_ENGAGE ? b->lost_t : 99; *ls = b->last_seen; return 1; }
static void skw_notify(int a, int what, const char *text, int pts) {
	if (a != 0) return; if (what == SKN_HIT) { *SKB.hit_marker = 0.2f; return; } if (what == SKN_TEXT) { *SKB.notify = text; *SKB.notify_t = 0; *SKB.notify_pts = 0; return; }
	*SKB.kill_marker = 0.5f; *SKB.notify = text; *SKB.notify_t = 0; *SKB.notify_pts = pts; *SKB.intel += pts; ev_play("impact/ui_killshot", NULL); }
static const SkWorld SKW = { .n_actors = skw_n, .pos = skw_pos, .vel = skw_vel, .yaw = skw_yaw, .alive = skw_alive, .eye = skw_eye, .aim_dir = skw_dir, .hp = skw_hp,
	.los = skw_los, .damage = skw_damage, .heal = skw_heal, .firing = skw_firing, .bot_info = skw_bot_info, .shot_fx = on_bot_shot, .notify = skw_notify };
#ifdef BOT_SK_HOOKS   // bots.h hooks (apply_skills_to_main.py with bots.h): camo / hack visibility, shields, EMP aim, revealed enemies, rush ...
static int skb_can_see(int i, int t) { return sk_can_see(1 + i, t < 0 ? 0 : 1 + t) != 0; }
static int skb_block(int i, V3 from, V3 to, V3 *end) { return sk_bullet_block(1 + i, from, to, end); }
static float skb_aim(int i) { return sk_aim_factor(1 + i); }
static int skb_goal(int i, float *g) { return sk_bot_goal(1 + i, g); }
static void skb_move(int i, float *mx, float *my, int *sprint, int *jump) { Bot *b = &bots[i]; float y = b->pl.yaw; sk_bot_move(1 + i, mx, my, sprint, jump, &y); b->pl.yaw = y; b->pl.speed_mul = sk_speed_mul(1 + i, *sprint); }
static float skb_alpha(int i) { return sk_draw_alpha(1 + i, lis_pos); }
static int skw_hidden_from_bots(void) { return 0; }   // per bot, through bot_sk_can_see
#else
static int skw_hidden_from_bots(void) { return sk_camo_on(0); }   // no bot hooks: bots simply ignore a cloaked player (the old behaviour)
#endif
static void skw_sync(void) {   // each actor's operator / team, tiers (player: intel; bots: kills x 100)
	if (skw_ct_n != nchrt) { for (int c = 0; c < NCHR_MAX; c++) skw_ct_skill[c] = (signed char)(c < nchrt ? sk_skill_of_stem(chrt[c].name) : -1); skw_ct_n = nchrt; }
	sk_actor_set(0, cfg.chr >= 0 && cfg.chr < SK_COUNT ? cfg.chr : SK_OPTICAL, 0); sk_set_tier(0, *SKB.intel);
	for (int i = 0; i < nbot && i + 1 < SK_MAXACT; i++) { int s = skw_ct_skill[bots[i].ct]; sk_actor_set(1 + i, s >= 0 ? s : SK_OPTICAL, bots[i].team); sk_set_tier(1 + i, bots[i].kills * 100); }
}
static void skw_frame(float dt) { if (!SKB.pl) return; skw_sync(); skills_update(dt); }
static void skw_match_start(void) { if (!SKB.pl) return; skw_sync(); skills_reset_match(); }
static void skw_bind(Player *pl, float *php, float *pap, float *pdead, float *pitch, float *pflash, float *kill_marker, float *hit_marker, float *notify_t, float *streak_t,
	int *fly, int *intel, int *kills, int *m_pkills, int *notify_pts, int *streak, int *autoplay, const char **notify) {
	SKB.pl = pl; SKB.php = php; SKB.pap = pap; SKB.pdead = pdead; SKB.pitch = pitch; SKB.pflash = pflash; SKB.kill_marker = kill_marker; SKB.hit_marker = hit_marker;
	SKB.notify_t = notify_t; SKB.streak_t = streak_t; SKB.fly = fly; SKB.intel = intel; SKB.kills = kills; SKB.m_pkills = m_pkills; SKB.notify_pts = notify_pts;
	SKB.streak = streak; SKB.autoplay = autoplay; SKB.notify = notify;
#ifdef BOT_SK_HOOKS
	bot_sk_can_see = skb_can_see; bot_sk_block = skb_block; bot_sk_aim = skb_aim; bot_sk_goal = skb_goal; bot_sk_move = skb_move; bot_sk_alpha = skb_alpha;
#endif
}
'''

BOT_HOOKS = r'''// operator skills (skills.h; set by the game, all NULL = no skills, bots behave exactly as before)
#define BOT_SK_HOOKS 1
static int (*bot_sk_can_see)(int i, int target);              // target -1 = the player, >= 0 a bot; 0 = it can't see it (camo, hacked)
static int (*bot_sk_block)(int i, V3 from, V3 to, V3 *end);   // 1 = the shot is stopped (shield / cyber wall / deployable) at *end
static float (*bot_sk_aim)(int i);                            // hit-chance factor (EMP 0.5); <= 0: no firing (hacked, placing, diving, rushing)
static int (*bot_sk_goal)(int i, float *g);                   // a roam goal from skills (enemies revealed by heat scan / recon / wall)
static void (*bot_sk_move)(int i, float *mx, float *my, int *sprint, int *jump);   // movement override (rush, flee EMP / drone, hacked ...)
static float (*bot_sk_alpha)(int i);                          // draw: 1 normal, (0, 1) cloaked ghost (additive), 0 hidden'''


def patch_main(p, autoplay_skills=True):
    if 'skills.h' in p.text:
        raise SystemExit('%s: already contains skills.h: refusing to patch twice' % p.path)
    p.insert_after('M1 include', '#include "dem.h"', '#include "skills.h"   // operator skills (tools/skills/SKILLS.md, INTEGRATION.md)', indent='')
    p.insert_before('M2 world callbacks', 'int main() {', SKW_BLOCK, exact=True)
    p.replace('M3 fx_sk.gfx', 'int fr = fx_init(VDIR "fx.gfx");',
              'int fr = fx_init(VDIR "fx_sk.gfx"); if (fr < 0) fr = fx_init(VDIR "fx.gfx"); /* fx_sk.gfx = fx.gfx + the skill effects (build_skills_fx.py) */')
    p.insert_before('M4 skills_init', 'if (sr < 0) logp("skinning shader failed (%d)',
                    '{ int skr = skills_init(VDIR, &SKW, md_ok); logp("skills: %s (%d parts: sk_*.gvm + sk_ovl.gtx)\\n", skr < 0 ? "FAILED" : "ok", skr); }')
    p.insert_after('M5 bind', 'cfg_apply(); pvoice = op_voice(cfg.chr);',
                   'skw_bind(&pl, &php, &pap, &pdead_t, &pitch, &pflash, &kill_marker, &hit_marker, &notify_t, &streak_t, &fly, &intel, &kills, &m_pkills, &notify_pts, &streak, &autoplay, &notify); skw_match_start();')
    ms = 'skw_match_start();   // skills: everyone ready, nothing deployed'
    if autoplay_skills:
        ms = 'if (autoplay) { cfg.chr = tour_i % 12; pvoice = op_voice(cfg.chr); }   // test run: a different operator (skill) on each map\n' + ms
    p.insert_before('M6 match start', 'tex_rechecked = 0; light_at(pl.pos.x, pl.pos.y, pl.pos.z + 20, vm_lit);', ms)
    p.replace_lines('M7 skill button', 'if (in_skill && cool_t < 0 && pdead_t <= 0)', 'logp("  skill: therm-optic camo\\n"); }',
                    'if (in_skill && pdead_t <= 0 && sk_press(0)) logp("  skill: %s\\n", sk_state_name(0));   // operator skill (skills.h): D-pad right; again = cancel / toggle')
    fire = ('if (!fly && pdead_t <= 0) {   // operator skill: R places / launches / dives while the skill aims; no gun while placing, diving, rushing or hacked\n'
            + ('\tif (autoplay && map_t > TOUR_SPECT) { int ap_p = 0, ap_f = 0; sk_autoplay(0, trig || aim, dt, &ap_p, &ap_f); if (ap_p && sk_press(0)) logp("  skill: %s\\n", sk_state_name(0)); if (ap_f) trig_pr = 1; }\n' if autoplay_skills else '')
            + '\tif (trig_pr && sk_fire(0)) trig = trig_pr = 0;\n'
            + '\tif (!sk_can_fire(0)) { trig = trig_pr = 0; in_knife = in_gren = gren_held = 0; want_reload = 0; aim = 0; }\n}')
    p.insert_before('M8 fire / weapon gate', 'if (dem_on && dem_prog >= 0) want_reload = 0;', fire)
    p.replace('M9 movement', 'player_update(&pl, mx, my, sprint, jump, dt);',
              'sk_move_input(0, &mx, &my, &sprint, &jump, dt); if (sk_speed_mul(0, sprint && my > 0.3f) > 1) pl.speed_mul = 2; else if (pl.speed_mul == 2) pl.speed_mul = 0; /* dive: the stick flies the ghost; rush: x2 sprint */\n'
              '\t\t\tplayer_update(&pl, mx, my, sprint, jump, dt);')
    p.insert_after('M10 player bullets', 'float bt = 0; int bh = 0, bi = bots_ok ? bots_hit_test(eye, dir, dist > 0 ? dist : 15000, &bt, &bh) : -1;',
                   '{ float lim_ = bi >= 0 ? bt : dist > 0 ? dist : 15000; if (sk_bullet(eye, dir, lim_, 0, bh ? D->dmg_h : D->dmg) < lim_) { bi = -1; dist = -1; } }   // stopped by an enemy shield / cyber wall / deployable (skills.h)')
    p.replace('M11 grenade vs deployables', 'snd_gren_exp(&c.x);', 'snd_gren_exp(&c.x); sk_blast(c, 650, 160, 0);')
    p.insert_before('M12 dive camera', 'if (pitch > 1.5f) pitch = 1.5f; if (pitch < -1.5f) pitch = -1.5f;',
                    'if (!fly) { V3 sce_; if (sk_camera(0, &sce_)) { pos[0] = sce_.x; pos[1] = sce_.y; pos[2] = sce_.z; } }   // cyber dive: the ghost camera')
    p.insert_before('M13 skills update', '// ---- bots', 'skw_frame(dt);   // operator skills: actors, tiers, skills_update (skills.h)', exact=True)
    p.replace('M14 bot target', 'BotTarget T = { pl.pos, pl.vel, !fly && pdead_t <= 0 && camo_t <= 0 };',
              'BotTarget T = { pl.pos, pl.vel, !fly && pdead_t <= 0 && !skw_hidden_from_bots() }; /* camo, hacks: per bot through bot_sk_can_see (skills.h) */')
    p.insert_after('M15 round reset', 'if (ev & DEV_RESET) {', '\tsk_round_reset();   // skills: deployables gone, active skills end (cooldowns keep running)')
    p.replace_lines('M16 old camo tick', 'if (cool_t >= 0) { cool_t -= dt;', 'if (camo_t > 0 && camo_t - dt <= 0) ev_play("intel_skill/cloaking_end", NULL); camo_t -= dt;',
                    '// (the operator skill timers live in skills.h now; cool_t / camo_t are unused)', span=2)
    p.replace('M17 rush FOV', 'persp(P, spect ? 70 * 3.14159f / 180 : 2 * atanf(hf), 960.0f / 544, 10, 12000);',
              'persp(P, spect ? 70 * 3.14159f / 180 : 2 * atanf(hf) + sk_fov_add(0) * 3.14159f / 180, 960.0f / 544, 10, 12000);')
    p.replace('M18 world pass', 'range_draw(MVP, pos, R_, U_); fx_draw(MVP, pos, R_, U_);', 'sk_draw_world(MVP, pos, R_, U_, fog); range_draw(MVP, pos, R_, U_); fx_draw(MVP, pos, R_, U_);')
    p.replace_line('M19 hide viewmodel', 'if (spect) {', 'if (spect || sk_hide_viewmodel(0)) {   // (arm-launcher mode, cyber dive: no gun)', exact=True)
    p.insert_before('M20 overlays', "// ---- HUD: the game's own widgets (hud_game.h)", 'if (!spect) sk_draw_overlays(0);   // skill screen layers (camo haze, dive, glitch ...)')
    p.regex_in_line('M21 HUD skill fields', 'H_.cool =', r'H_\.cool = .*?(?=H_\.key = NULL;)', 'sk_hud(0, &H_.skill_icon, &H_.cool, NULL); ', 'H_.skill_icon')
    p.replace_in_line('M22 HUD chips', 'int ca[5] = {', 'camo_t > 0', 'sk_camo_on(0)', 2)
    p.replace('M23 skills HUD', 'hud_game_draw(&H_, now / 1e6f);', 'hud_game_draw(&H_, now / 1e6f); skills_hud(0, MVP, now / 1e6f);')
    p.replace('M24 minimap', 'int seen = bot_los(v3(pos[0], pos[1], pos[2]), b->chest);',
              'int seen = (sk_draw_alpha(1 + i, v3(pos[0], pos[1], pos[2])) >= 1 && bot_los(v3(pos[0], pos[1], pos[2]), b->chest)) || sk_known(1 + i);')
    for k in ('sk_press(0)', 'skills_hud(', 'sk_draw_world(', 'skw_frame(dt)', 'sk_bullet(', 'skw_bind('):
        if k not in p.text:
            raise SystemExit('main.c: %s missing after patching' % k)


def patch_bots(p):
    if 'bot_sk_' in p.text:
        raise SystemExit('%s: already has the skill hooks: refusing to patch twice' % p.path)
    p.insert_after('B1 hook pointers', 'static int (*bot_goal)(int i, float *g);', BOT_HOOKS, indent='')
    p.replace('B2 visibility', 'if (k < 0) break; int t = cand[k]; cd[k] = 1e9f;',
              'if (k < 0) break; int t = cand[k]; cd[k] = 1e9f; if (bot_sk_can_see && !bot_sk_can_see(i, t)) continue;')
    p.replace('B3 no firing', 'else if (b->sees && b->see_t > b->react && tdist < 4000 && !bot_freeze) {',
              'else if (b->sees && b->see_t > b->react && tdist < 4000 && !bot_freeze && !(bot_sk_aim && bot_sk_aim(i) <= 0)) {')
    p.replace('B4 aim factor', 'p *= fminf(1, 0.3f + b->see_t * 0.3f);', 'p *= fminf(1, 0.3f + b->see_t * 0.3f); if (bot_sk_aim) p *= bot_sk_aim((int)(b - bots));', n=2)
    p.replace('B5 shots stopped', 'V3 end = vadd(b->muzzle, vmul(d, 1.6f));',
              'V3 end = vadd(b->muzzle, vmul(d, 1.6f)); if (bot_sk_block && bot_sk_block((int)(b - bots), b->muzzle, pc, &end)) { if (bot_on_shot) bot_on_shot(b->muzzle, end); return; }', n=2)
    p.replace('B6 roam goal', 'if (!(bot_goal && bot_goal(i, g))) {', 'if (!(bot_goal && bot_goal(i, g)) && !(bot_sk_goal && bot_sk_goal(i, g))) {')
    p.insert_before('B7 movement', "// ---- physics (the player's own movement code)", 'if (bot_sk_move && b->state != BS_DEAD) bot_sk_move(i, &mx, &my, &sprint, &jump);   // skills: rush, flee, hacked ...')
    a = 'model_draw_cpu(&C->m, &b->sc, MVP, b->pose, ld, tint, fogc, reskin); else model_draw(&C->m, MVP, b->pose, ld, tint, fogc);'
    p.insert_before('B8a camo ghost', a, 'float ga_ = bot_sk_alpha ? bot_sk_alpha(i) : 1.f; if (ga_ <= 0) continue;   // cloaked: a faint additive ghost (skills.h)\n'
                    'if (ga_ < 1) { tint[0] = 0.55f; tint[1] = 0.65f; tint[2] = 0.75f; tint[3] = ga_; glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE); }')
    p.insert_after('B8b camo ghost', a, 'if (ga_ < 1) { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }')
    p.replace('B9 no blob shadow', '{ Bot *b = &bots[i]; if (!b->pose) continue;', '{ Bot *b = &bots[i]; if (!b->pose) continue; if (bot_sk_alpha && bot_sk_alpha(i) < 1) continue;')
    p.replace('B10 no planar shadow', 'V3 d = vsub(b->pl.pos, lis_pos); if (vdot(d, d) > 2200.f * 2200.f) continue;',
              'V3 d = vsub(b->pl.pos, lis_pos); if (vdot(d, d) > 2200.f * 2200.f) continue; if (bot_sk_alpha && bot_sk_alpha(i) < 1) continue;')


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) not in (2, 4):
        raise SystemExit(__doc__)
    autoplay_skills = '--no-autoplay-skills' not in sys.argv
    jobs = []
    m = Patch(args[0], open(args[0]).read()); patch_main(m, autoplay_skills); jobs.append((m, args[1]))
    if len(args) == 4:
        b = Patch(args[2], open(args[2]).read()); patch_bots(b); jobs.append((b, args[3]))
    for p, out in jobs:   # only written when every edit applied
        open(out, 'w').write(p.text)
        print('%s -> %s: %d edits (%s)' % (p.path, out, len(p.log), ', '.join(e.split()[0] for e in p.log)))


if __name__ == '__main__':
    main()
