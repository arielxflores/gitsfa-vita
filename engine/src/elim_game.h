// Elimination in the match loop (elim.h rules + the engine): included by main.c after the Demolition section, so it
// sees the engine's globals (bots, weapons, audio, HUD, menus). main()'s locals it needs are bound once (elim_bind).
// main.c hooks: elim_bind (init), elim_match_setup (match start), elim_frame (each frame, after Demolition),
// elim_camera (spectating), elim_hud (HUD state), elim_hud_draw / elim_overlays_draw (drawing), elim_tour_extra (test run).
// Spectating after death (follow a living ally, L / R switch) serves Demolition too (footage: Downtown, Train Station).
#pragma once
#include "elim.h"
#include "hud_round.h"

static ElimMap emap; static ElimMatch em; static int elim_on, elim_test;   // elim_test: the test run's shorter rounds
static float em_t_banner = 99, em_t_start = 99, em_t_warn = 99, em_t_evac = 99, em_t_lock = 99, em_t_timewarn = 99, em_t_band = 99, em_t_switch = 99, em_t_lone = 99, em_t_cmd = 99;
static int __attribute__((unused)) em_band_res, em_band_round, em_band_reason, em_zone_p = -1, em_spec = -1, em_spec_prev_b, em_dead_frames;
static float em_zone_dmg_t;   // the player's time inside a locked zone (damage ticks)
static const char *em_cmd_msg = "Eliminate all enemy agents!";   // Aramaki's line (top left; the newest replaces the one shown)
static void em_commander(const char *msg, float delay) { em_cmd_msg = msg; em_t_cmd = -delay; }
static int scoreboard_shown(void); static float scoreboard_hold_dt(float dt);   // hud_match.h (perf b38: the round screens wait while the scoreboard is up)
static struct { Player *pl; float *pitch, *php, *pap, *pdead, *hurt, *reload, *match_over, *pflash; int *fly, *autoplay, *end_result; } EMB;
static void elim_bind(Player *pl, float *pitch, float *php, float *pap, float *pdead, float *hurt, float *reload, int *fly, int *autoplay, float *match_over, int *end_result, float *pflash) {
	EMB.pflash = pflash; EMB.pl = pl; EMB.pitch = pitch; EMB.php = php; EMB.pap = pap; EMB.pdead = pdead; EMB.hurt = hurt; EMB.reload = reload; EMB.fly = fly; EMB.autoplay = autoplay; EMB.match_over = match_over; EMB.end_result = end_result;
}
static int elim_player_alive(void) { return EMB.pl && *EMB.pdead <= 0 && !*EMB.fly; }
#include "ga.h"   // b63: Ghost Assault (cyborgs vs ghosts) on this round framework: its rules, bots, HUD (hooks below call ga_*)

// ---- bots: where to go (bots.h bot_goal hook). Footage (Urban): both teams leave their spawn ends and meet in the open
// middle (the safe zone); stragglers in the outer zones head for the middle once the lock is announced.
static float (*em_prev_alpha)(int i);
static int em_round_mode(void) { return elim_on || dem_on; }   // the modes where the dead watch their allies until the next round
static float em_alpha(int i) { if (em_round_mode() && i == em_spec && *EMB.pdead > 0) return 0; return em_prev_alpha ? em_prev_alpha(i) : 1.f; }   // the spectated ally is not drawn (camera at its shoulder)
static int elim_goal(int i, float *g) {
	if (!elim_on || em.state != ES_LIVE) return 0;
	if (ga_on) return ga_goal(i, g);   // Ghost Assault: hunt the pings / hide (ga.h)
	Bot *b = &bots[i]; V3 p = b->pl.pos;
	int inz = elim_zone_at(&emap, p.x, p.y, p.z) >= 0;
	if (inz && em.round_left <= em.warn_at + 5) { const float *s = elim_safe_near(&emap, p.x, p.y, p.z, 0); if (s) { memcpy(g, s, 12); return 1; } }
	// the enemy: a living opponent's last known area most of the time, a safe point on the way otherwise (spreads the team out)
	if (frand() < 0.65f) {
		int cand[NBOT_MAX + 1], nc = 0;
		for (int j = 0; j < nbot; j++) if (bots[j].team != b->team && bots[j].state != BS_DEAD) cand[nc++] = j;
		if (b->team == 1 && elim_player_alive()) cand[nc++] = -1;
		if (nc) { int t = cand[rand() % nc]; V3 q = t >= 0 ? bots[t].pl.pos : EMB.pl->pos;
			if (!(em.locked && elim_zone_at(&emap, q.x, q.y, q.z) >= 0)) { g[0] = q.x + (frand() - 0.5f) * 600; g[1] = q.y + (frand() - 0.5f) * 600; g[2] = q.z; return 1; } }
	}
	const float *s = elim_safe_near(&emap, p.x, p.y, p.z, 1); if (s) { memcpy(g, s, 12); return 1; }
	return 0;
}
#include "tactics.h"   // bot tactics (goal hook): team knowledge, patrols, rotations; tactics_setup / tactics_frame
// ---- match setup (after the map and the bots are ready): Elimination when asked for and the map has the data
static void elim_match_setup_(const char *stem, int want, int test, int bots_ready);
static void elim_match_setup(const char *stem, int want, int test, int bots_ready) { elim_match_setup_(stem, want, test, bots_ready); tactics_setup(); if (ga_on) bot_goal = elim_goal; }   // (Ghost Assault: no team-contact tactics: the ghosts must not walk to the fight)
// a map without Elimination data (Ghost Assault plays on every map): the two sides' spawns from the TDM team bases (or, without
// them, the spawn points split between the two farthest apart)
static int elim_map_from_teams(ElimMap *M) {
	memset(M, 0, sizeof(*M)); if (nspawn < 2) return -1;
	int fa = 0, fb = 1; float fd = -1;
	if (!teams_ok) for (int i = 0; i < nspawn; i++) for (int j = i + 1; j < nspawn; j++) { float dx = spawns[i].x - spawns[j].x, dy = spawns[i].y - spawns[j].y, d = dx * dx + dy * dy; if (d > fd) { fd = d; fa = i; fb = j; } }
	for (int k = 0; k < 2; k++) M->sp[k] = malloc(sizeof(ElimSpawn) * (nspawn + 1));
	for (int i = 0; i < nspawn; i++) { int g = spawns[i].gi, t;
		if (teams_ok && g >= 0 && g < 64) t = team_of_group[g];
		else { float ax = spawns[i].x - spawns[fa].x, ay = spawns[i].y - spawns[fa].y, bx = spawns[i].x - spawns[fb].x, by = spawns[i].y - spawns[fb].y; t = ax * ax + ay * ay > bx * bx + by * by; }
		if (t < 0 || t > 1 || M->nsp[t] >= 64) continue; ElimSpawn *e = &M->sp[t][M->nsp[t]++]; e->x = spawns[i].x; e->y = spawns[i].y; e->z = spawns[i].z; e->yaw = spawns[i].yaw; }
	M->zone = calloc(1, sizeof(ElimZone)); M->bar = malloc(48); M->ok = 1;
	return M->nsp[0] && M->nsp[1] ? 0 : -7;
}
static void elim_match_setup_(const char *stem, int want, int test, int bots_ready) {
	elim_on = 0; elim_test = 0; em_spec = -1; em_t_cmd = 99; ga_off(); int ga = want == 2;   // want: 1 Elimination, 2 Ghost Assault
	if (emap.ok) elim_free(&emap);
	if (bots_ready && bot_sk_alpha != em_alpha) { em_prev_alpha = bot_sk_alpha; bot_sk_alpha = em_alpha; }   // (spectating hides the watched ally: Demolition too)
	if (!want || !bots_ready || nbot <= 0) return;
	char p[96]; snprintf(p, sizeof(p), VDIR "%s.gelim", stem); int r = elim_load(&emap, p);
	if (ga && (r < 0 || !emap.nsp[0] || !emap.nsp[1])) { if (emap.ok) elim_free(&emap); r = elim_map_from_teams(&emap); if (r == 0) logp("ghost assault: %s has no .gelim: the TDM bases' spawns\n", stem); }
	if (r < 0 || !emap.nsp[0] || !emap.nsp[1]) { logp("%s: %s not usable (%d)\n", ga ? "ghost assault" : "elimination", p, r); if (emap.ok) elim_free(&emap); return; }
	elim_on = 1; elim_start(&em, &emap, 0);
	if (test) { elim_test = 1; elim_set_round_time(&em, 45); }   // the test run: shorter rounds (the zone timeline keeps its proportions)
	if (ga) ga_setup(test);   // Ghost Assault's rules over Elimination's (ga.h)
	dem_on = 0; bot_no_respawn = 1; bot_goal = elim_goal; bot_freeze = 1; match_len = 0; score_limit = 0;
	int mr = -1; if (!ga) { snprintf(p, sizeof(p), VDIR "%s_em.gmm", stem); mr = minimap_load(p); } if (mr < 0) { snprintf(p, sizeof(p), VDIR "%s.gmm", stem); minimap_load(p); }   // (Ghost Assault: no zones hatched)
	em_t_banner = em_t_start = em_t_warn = em_t_evac = em_t_lock = em_t_timewarn = em_t_band = em_t_switch = em_t_lone = em_t_cmd = 99;
	logp("elimination: %d / %d spawns, %d lockable zones (%d barriers, %d safe points), first to %d (switch after %d), %.0f s rounds, radar %s\n",
		emap.nsp[0], emap.nsp[1], emap.nzone, emap.nbar, emap.nsafe, em.to_win, em.switch_after, em.round_time, mr < 0 ? "TDM" : "EM");
}
static void elim_place_all(void) {
	sk_round_reset(); vis_round_start();   // vision.h: the round-start hologram, as in Demolition
	Player *pl = EMB.pl; const ElimSpawn *sp = elim_spawn(&emap, elim_side(&em, 0), 0);
	if (sp) { pl->pos = v3(sp->x, sp->y, sp->z + 5); pl->yaw = sp->yaw; } pl->vel = v3(0, 0, 0); *EMB.pitch = 0;
	*EMB.php = 100; *EMB.pap = 100; *EMB.pdead = 0; *EMB.hurt = 0; for (int k = 0; k < WS_N; k++) wslot_refill(k); *EMB.reload = 0;
	WP.rl_phase = 0; WP.bolt_pending = 0; WP.gren_state = 0; memset(gren, 0, sizeof(gren));
	if (vm_ok) { if (WP.quick_from >= 0) { WP.sw_to = WP.quick_from; WP.quick_from = -1; } else WP.sw_to = wcur; weapon_finish_switch(); }
	BotTarget T = { pl->pos, pl->vel, 1 }; int k0 = 1, k1 = 0;
	ga_round_bots();   // Ghost Assault: cyborg bodies and blades, ghosts' pistols (before the spawns: the equip sounds, the animation sets)
	for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; bot_spawn(b, &T); const ElimSpawn *s2 = elim_spawn(&emap, elim_side(&em, b->team), b->team == 0 ? k0++ : k1++);
		if (s2) { b->pl.pos = v3(s2->x, s2->y, s2->z + 5); b->pl.yaw = s2->yaw; b->floor_z = s2->z; } b->npath = 0; b->repath_t = 0; b->hold = 0; b->tgt = -2; }
	em_spec = -1; em_zone_dmg_t = 0;
	ga_round_after();   // Ghost Assault: health, loadouts, the side's briefing
}
static void elim_round_client(void) {   // net client: the host placed everyone; the round's fresh start here
	sk_round_reset(); vis_round_start(); *EMB.php = 100; *EMB.pap = 100; *EMB.pdead = 0; *EMB.hurt = 0; for (int k = 0; k < WS_N; k++) wslot_refill(k); *EMB.reload = 0;
	WP.rl_phase = 0; WP.bolt_pending = 0; WP.gren_state = 0; memset(gren, 0, sizeof(gren));
	if (vm_ok) { if (WP.quick_from >= 0) { WP.sw_to = WP.quick_from; WP.quick_from = -1; } else WP.sw_to = wcur; weapon_finish_switch(); }
	em_spec = -1; em_zone_dmg_t = 0;
}
static void elim_next_spec(int dir) {   // the next living ally to watch
	for (int k = 1; k <= nbot; k++) { int c = ((em_spec < 0 ? 0 : em_spec) + dir * k + 2 * nbot) % nbot; if (bots[c].team == 0 && bots[c].state != BS_DEAD) { em_spec = c; return; } }
	em_spec = -1;
}
// ---- each frame (after the bots and Demolition). pressed: buttons pressed this frame (Vita mask)
static void em_spec_frame(unsigned pressed) {   // spectating (Elimination, Demolition): once the death fall is over, follow a living ally (L / R switch)
	if (em_round_mode() && *EMB.pdead > 0 && *EMB.pdead < 0.01f) {
		if (em_spec < 0 || bots[em_spec].state == BS_DEAD || bots[em_spec].team != 0) elim_next_spec(1);
		if (pressed & SCE_CTRL_RTRIGGER) elim_next_spec(1); if (pressed & SCE_CTRL_LTRIGGER) elim_next_spec(-1);
	} else em_spec = -1;
}
static int net_client_on(void); static unsigned net_mode_take(int m); static void net_host_mode(int m, unsigned ev);   // net_game.h (a client shows the host's rounds)
static void elim_frame(float dt, unsigned pressed) {
	tactics_frame(dt);
	if (!elim_on) { em_spec_frame(pressed); em_t_cmd += scoreboard_hold_dt(dt); return; }
	int ncl = net_client_on(); unsigned ev = 0;
	if (ncl) { em_zone_p = elim_player_alive() ? elim_zone_at(&emap, EMB.pl->pos.x, EMB.pl->pos.y, EMB.pl->pos.z) : -1; bot_freeze = em.state == ES_FREEZE; ev = net_mode_take(2); goto shown; }   // net: the host's round
	{ int a0 = elim_player_alive() ? 1 : 0, a1 = 0;
	for (int i = 0; i < nbot; i++) if (bots[i].state != BS_DEAD) { if (bots[i].team == 0) a0++; else a1++; }
	elim_set_alive(&em, a0, a1); bot_freeze = em.state == ES_FREEZE;
	// locked zones: damage over time inside (the player through the bot-damage path: armour first, the hit direction from the zone)
	em_zone_p = elim_player_alive() ? elim_zone_at(&emap, EMB.pl->pos.x, EMB.pl->pos.y, EMB.pl->pos.z) : -1;
	if (elim_locked(&em)) {
		if (em_zone_p >= 0 && !*EMB.autoplay) { em_zone_dmg_t += dt; if (em_zone_dmg_t >= 0.5f) { em_zone_dmg_t -= 0.5f; self_dmg += EM_ZONE_DPS * 0.5f; self_dmg_dir = EMB.pl->yaw + 3.14159f; bot_last_hitter = -1; } }
		for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; if (b->state == BS_DEAD) continue;
			if (elim_zone_at(&emap, b->pl.pos.x, b->pl.pos.y, b->pl.pos.z) >= 0) { bot_hurt(i, EM_ZONE_DPS * dt, 0, -2); b->repath_t = fminf(b->repath_t, 0.2f); } }
	}
	ev = elim_update(&em, dt); net_host_mode(2, ev); }
shown:
	if (ev & EEV_RESET) {
		if (ncl) elim_round_client(); else elim_place_all(); em_t_banner = 0; em_t_band = em_t_switch = 99; ev_play("ui/game_start_dm_text", NULL);
		em_t_lone = em_t_cmd = 99;   // qa (b42 fix, marked): a new round drops the last round's Aramaki line ("One Enemy Remaining" held under the scoreboard showed into round 2)
		if ((em.round == 1 || em.round == em.switch_after + 1) && !ga_on) { radio_play_at("voice/donut/tdm/gamestart", 0.7f); em_commander("Eliminate all enemy agents!", 1.2f); }   // (Ghost Assault: its commanders, ga_round_after)
		music_ev(em.round == 1 ? "bgm/in_game_dm_01_ready" : "bgm/in_game_dm_01_round", 300);
		logp("  elimination: round %d, side %d, score %d - %d\n", em.round, elim_side(&em, 0), em.score[0], em.score[1]);
	}
	if (ev & EEV_RESPAWN_SND) ev_play("ui/respawn", NULL);
	if (ev & EEV_START) { em_t_start = 0; ev_play("ui/game_start_dm_start", NULL); }
	if (ev & EEV_ZONE_WARN) { em_t_warn = 0; ev_play("ui/elim_zone_start", NULL); logp("  elimination: zone lock warning\n"); }
	if (ev & EEV_EVAC) { em_t_evac = 0; if (em_zone_p >= 0) ev_play("ui/elim_zone_start", NULL); for (int i = 0; i < nbot; i++) bots[i].repath_t = 0; }   // bots re-plan: out of the zones
	if ((ev & EEV_COUNT) && em_zone_p >= 0) ev_play("ui/elim_countdown", NULL);
	if (ev & EEV_LOCK) { em_t_lock = 0; ev_play("ui/elim_zone_start", NULL); logp("  elimination: zones locked\n"); }
	if (ev & EEV_TIMEWARN) { em_t_timewarn = 0; if (!ga_on) { radio_play("voice/donut/tdm/20sec"); ev_play("ui/crisis", NULL); } }   // (Ghost Assault: ga_round_event)
	if (ga_on) { if (ev & (EEV_ONE_LEFT_0 | EEV_ONE_LEFT_1)) ev_play("ui/ghost_player_remain", NULL); }
	else {
	if (ev & EEV_ONE_LEFT_0) { radio_play("voice/donut/dm/oneleft_self"); if (elim_player_alive()) em_commander("Lone Survivor", 0); }
	if (ev & EEV_ONE_LEFT_1) { radio_play("voice/donut/dm/oneleft_enemy"); em_commander("One Enemy Remaining", 0); }   // footage (Train Station): Aramaki's line with the voice
	}
	if (ev & EEV_ROUND_END) {
		em_t_band = 0; em_band_round = em.round; em_band_reason = em.last_reason; em_band_res = em.last_winner == 0 ? 0 : em.last_winner == 1 ? 1 : 2;
		ev_play("ui/result", NULL); music_ev(em_band_res == 0 ? "bgm/win" : em_band_res == 1 ? "bgm/lose" : "bgm/even", 300);
		logp("  elimination: round %d %s (%s), %d - %d\n", em.round, em_band_res == 0 ? "won" : em_band_res == 1 ? "lost" : "drawn", em.last_reason == ER_TIME ? "time up" : "team eliminated", em.score[0], em.score[1]);
	}
	if (ev & EEV_SWITCH) { em_t_switch = 0; ev_play("ui/result", NULL); ev_play("ui/round_change", NULL); }
	if (ev & EEV_MATCH_END) { int r = em.score[0] > em.score[1] ? 0 : em.score[0] < em.score[1] ? 1 : 2; *EMB.end_result = r;
		radio_play(r == 0 ? "voice/donut/dm/common_game_win" : r == 1 ? "voice/donut/dm/common_game_lose" : "voice/donut/dm/common_game_draw");
		music_ev(r == 0 ? "bgm/win" : r == 1 ? "bgm/lose" : "bgm/even", 200); *EMB.match_over = 8; logp("  elimination: match over %d - %d\n", em.score[0], em.score[1]); }
	ga_round_event(ev); ga_frame(dt);   // Ghost Assault: its lines, pings, bodies, the seeker drone (ga.h)
	em_spec_frame(pressed);
	{ float od = scoreboard_hold_dt(dt);   // (display clocks only: they stand still under the scoreboard)
	  em_t_banner += od; em_t_start += od; em_t_warn += od; em_t_evac += od; em_t_lock += od; em_t_timewarn += od; em_t_band += od; em_t_switch += od; em_t_lone += od; em_t_cmd += od; }
}
// spectating camera: over the shoulder of the watched ally (the original shows the ally's own first-person view; without
// that ally's weapon model a shoulder view reads better on the Vita). Returns 1 when it set the camera.
static int elim_camera(float *pos, float *yaw, float *vpitch, float dt) {
	if (!em_round_mode() || em_spec < 0 || *EMB.pdead <= 0) return 0;
	static float cy, cp; static int last = -1; Bot *b = &bots[em_spec];
	float ty = b->pl.yaw, tp = b->aim_pitch * 0.6f - 0.12f; if (last != em_spec) { cy = ty; cp = tp; last = em_spec; }
	cy += wrap_pi(ty - cy) * fminf(1, 5 * dt); cp += (tp - cp) * fminf(1, 5 * dt);
	V3 head = v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 165), f = v3(cosf(cy) * cosf(cp), sinf(cy) * cosf(cp), sinf(cp)), r = v3(sinf(cy), -cosf(cy), 0);
	V3 want = vadd(head, vadd(vmul(f, -170), vadd(vmul(r, 45), v3(0, 0, 25)))), d = vsub(want, head); float l = sqrtf(vdot(d, d)); V3 dn = vmul(d, 1 / l);
	float h = raycast(head, dn, l, NULL, NULL); if (h > 0) want = vadd(head, vmul(dn, fmaxf(20, h - 20)));
	pos[0] = want.x; pos[1] = want.y; pos[2] = want.z; *yaw = cy; *vpitch = cp; return 1;
}
// HUD state: round score, round timer (red in the last 20 s), the radar shows the mode's image (loaded at setup)
static void elim_hud(HudState *H) {
	if (em_round_mode() && *EMB.pdead > 0 && em_spec >= 0) H->hp = bots[em_spec].hp > 0 ? bots[em_spec].hp : 0;   // watching an ally: its health
	if (!elim_on) return;
	H->scoreL = em.score[0]; H->scoreR = em.score[1]; H->goal = em.to_win;
	float tl = elim_timer(&em); H->time_s = (int)(tl + 0.999f); H->time_warn = em.state == ES_LIVE && em.round_left <= em.timewarn_at;
	H->zone_tag = elim_locked(&em) ? "ZONE LOCKED" : NULL;
	ga_hud(H);
}
// the living ally before / after the watched one (the names beside L / R), NULL when there is no other
static const char *em_spec_neighbour(int dir) {
	for (int k = 1; k < nbot; k++) { int c = (em_spec + dir * k + 2 * nbot) % nbot; if (c == em_spec) break; if (bots[c].team == 0 && bots[c].state != BS_DEAD) return bot_name(c); }
	return NULL;
}
// in-HUD pieces (hud pack current): ZONE LOCKED tag, the evacuation countdown, spectator line (Demolition too)
static void elim_hud_draw(float now) {
	if (!em_round_mode()) return;
	if (*EMB.pdead > 0 && em_spec >= 0) { Bot *b = &bots[em_spec]; char st[48]; snprintf(st, sizeof(st), "%d Kill / %d Assist", b->kills, b->assists); /* (b83: v2.0, EP20 5:05) */ rh_spectate4(bot_name(em_spec), st, em_spec_neighbour(-1), em_spec_neighbour(1)); }
	if (!elim_on) { if (*EMB.pdead > 0 && *EMB.pdead < 0.01f && em_spec < 0 && dm.state == DS_LIVE) rh_spectate(NULL, "No allies left"); return; }
	if (elim_locked(&em) && HG.dm_emctxt < 0) rh_zone_tag("ZONE LOCKED", now);   // (hud_game.h draws the movie's tag: HudState.zone_tag)
	if (elim_evacuating(&em) && em_zone_p >= 0) rh_countdown(em.count_left > 0 ? em.count_left : 1, em.count_left <= 3);
	if (*EMB.pdead > 0 && em_spec < 0 && em.state == ES_LIVE) rh_spectate(NULL, "No allies left");
	ga_hud_draw(now);
}
// in-match screens (ovl pack): round banner + START, zone notices, timer warning, lone survivor, result / switching bands
static void elim_overlays_draw(void) {
	if (!elim_on || *EMB.match_over > 0 || scoreboard_shown()) return;   // (not over the held scoreboard: qa, b37)
	rh_legible_ovl();
	const char *GOAL = ga_on ? ga_goal_text() : "Eliminate all enemy reinforcements";
	if (em_t_banner < 6) rh_round_banner(ga_on ? MODE_GA : MODE_EM, em.round, GOAL, em_t_banner, em.round == 1, ga_on ? NULL : "rr.emb.emA");
	if (em_t_cmd < 6 && !ga_on) menu_commander_draw(FACE_ARAMAKI, "CHIEF ARAMAKI", em_cmd_msg, em_t_cmd);
	ga_overlays_draw();
	menu_notice_draw("START", em_t_start);
	menu_notice_draw("30 SECONDS TO ZONE LOCK", em_t_warn);
	if (em_zone_p >= 0 && elim_evacuating(&em)) menu_notice_draw("MOVE TO A SAFE ZONE!", fminf(em_t_evac, 1.0f));
	else menu_notice_draw("MOVE TO A SAFE ZONE!", em_zone_p >= 0 && elim_locked(&em) ? 1.0f : 99);
	if (!(em_zone_p >= 0 && elim_locked(&em))) menu_notice_draw("ACCESS TO CERTAIN ZONES HAS BEEN RESTRICTED.", em_t_lock);
	rh_timewarn("Timer Warning", "Time is running out!", em_t_timewarn, elim_locked(&em) ? 30 : 0);   // (under the ZONE LOCKED tag)
	static const char *RS[3] = { "Mission Successful: Team Eliminated", "Mission Failed: Team Eliminated", "Draw: Time Up" }, *RT[2] = { "Mission Successful: Time's Up", "Mission Failed: Time's Up" };   // (Ghost Assault: the ghosts win on time, footage 158 s)
	if (em_t_band < EM_POST_ROUND + 0.2f) rh_result_band(em_band_round, em_band_res, em_band_reason == ER_TIME ? (em_band_res < 2 ? RT[em_band_res] : RS[2]) : RS[em_band_res == 1 ? 1 : 0], em.score[0], em.score[1], em_t_band, -1);
	if (em_t_switch < EM_SWITCH_BAND) rh_switch_band(em.round + 1, GOAL, em_t_switch, -1);
}
// the test run: extra seconds on the Elimination map (two short rounds)
static float elim_tour_extra(void) { return elim_on && elim_test ? (ga_on ? 125.f : 80.f) : 0.f; }   // (Ghost Assault: two 60 s rounds and the switch)
static int em_watching(void) { return em_round_mode() && em_spec >= 0 && EMB.pdead && *EMB.pdead > 0; }   // watching an ally after death
static int killcam_active(void); static int killcam_body(void);
static int elim_spectating(void) { return em_watching() || killcam_active() || killcam_body(); }   // (b72: or the player's own body, pbody.h)   // main.c: no gun while the camera watches an ally or the killer
#include "killcam.h"   // the kill cam of the respawn modes (hooks: killcam_camera, killcam_panel_t, killcam_active)
