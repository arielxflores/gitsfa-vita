// Ghost Assault (b63, the user: "add the ghost assault mode") - the 2015 alpha's round mode, rebuilt from the footage
// ("Ghost in the Shell First Assault - 9 Minutes of Ghost Assault Gameplay - IGN", "NEW GAME MODE GHOST ASSAULT - Nick Bunyun"),
// the game's own leftovers (voice lines d_aramaki_gm_* / d_mastermind_gm_* / d_op_gm_*, ui/ghost_* sounds, effect\ghost_mode)
// and Elimination's round framework (elim.h / elim_game.h: rounds, banners, spectating, sides switching).
// "A deadly game of hide and seek between Ghosts and Cyborgs" (the alpha's scoreboard):
//   Sides: CYBORG (side 0) and GHOST (side 1); the teams change sides after round to_win - 1 (first to 4 = the footage's
//     best of 7: "x 6 y" over the score, a tiebreak 7th round). Rounds 2:30; a round ends when a side is wiped out, or on time:
//     the GHOSTS win it ("Mission Failed: Time's Up" for the cyborgs, footage 158 s).
//   Cyborgs: the white cyborg body (chr_cyborg.gvm), 200 HP, a blade only (b70: the arm blade, vm_axe_cy / w3_axe), Hyper Sprint
//     (the rush skill). They hunt by the ghosts' PINGS: a ghost within ~12 m of a cyborg pings every few seconds - a red
//     triangle on the cyborg's compass arc at the bottom of the screen, open when it is farther, filled when it is close
//     (chat: "the ping is the red triangle", "if its open they are 10 - 15 feet away", "if its full, they are closer").
//   Ghosts: their operator, 100 HP, permanent therm-optic camo (the white haze; seen only up close or when they fire), a
//     pistol with no reserve ("20 / 0"; chat: "can't really fight back"); top left DANGER when a cyborg is near, DETECTED with
//     red scan lines when one has them in sight or they are revealed.
//   Bodies: anyone can hack a corpse (hold Square): an ENEMY body is a cyberbrain hack - "Enemy cyberbrain hack successful."
//     +300, the cyber dive (a blue view, the enemy team shown through walls for 7 s); an ALLY body is restored - "ALLY
//     RESPAWN SUCCESSFUL" +500, the ally is back where it lay (voice lines GHOST/CYBORG_HACKING_GHOST/CYBORG_BODY_SUCCESS).
//   Seeker drone: late in the round (0:45) a drone appears on the map; the first cyborg to reach it deploys it: "Seeker
//     Drones report enemy locations." - every ghost pings, map-wide, every 4 s until the round ends (chat: "it spawns when
//     the round is almost over", "it needs to be picked up by a cyborg").
//   Offline only (bots); a network room asking for it plays Elimination.
// Included at the end of elim_game.h; main.c sets ga_want before elim_match_setup and calls the few hooks below.
#pragma once
#define GA_ROUND_TIME 150.0f
#define GA_PING_R 1200.0f        // a ghost pings when a cyborg is this near (cm)
#define GA_PING_CLOSE 450.0f     // filled triangle nearer than this ("10 - 15 feet")
#define GA_PING_EVERY 3.0f
#define GA_PING_SHOW 1.6f       // (= ga_hud.h GA_PING_SHOW_T)
#define GA_DRONE_AT 45.0f        // round time left when the seeker drone appears
#define GA_DRONE_EVERY 4.0f
#define GA_HACK_T 2.5f           // hold time: an enemy body
#define GA_REVIVE_T 3.0f         // an ally body
#define GA_REVEAL_T 7.0f         // the cyber dive: the enemies shown through walls
#define GA_REACH 180.0f
#define GA_CY_HP 200.0f
#define GA_GH_HP 100.0f
#define GA_SLASH 70.0f           // a cyborg bot's blade (two hits on a ghost, as the player's 55; b70: the blow that would kill is an execution)
#define GA_DANGER_R 1000.0f
#define GA_GHOST_RANGE 1300.0f   // ghost bots hold their fire beyond this (each shot shows them for a second)
#define GA_FACE_MASTERMIND 14    // the cyborgs' commander portrait (cm.face.14, InGameUI chaIcon icon14: the red-lit bald man the footage shows as "Terrorist Leader")

static int ga_on, ga_cyb_ct = -1, ga_wpn_lock, ga_use_held; static float p_hp_max_ga = 100; static const float *ga_hud_mvp;
static struct {
	float ping_t[1 + NBOT_MAX], ping_age[1 + NBOT_MAX], ping_show[1 + NBOT_MAX], ping_dist[1 + NBOT_MAX]; V3 ping_pos[1 + NBOT_MAX];
	unsigned char used[1 + NBOT_MAX];          // this body was hacked / restored (cleared when the actor lives again)
	int hold_tgt; float hold_t, dive_t;   // the player's hack (actor, -1 none), the cyber dive (s left)
	int drone; V3 drone_pos; float drone_t;    // 0 none, 1 on the map, 2 deployed; seconds to the next drone pulse
	float danger_t, detected_t, scan_t;        // the ghost player's HUD (DANGER fades, DETECTED, the last scan flash)
	int bot_hold[NBOT_MAX]; float bot_hold_t[NBOT_MAX], hide_t[NBOT_MAX], hide[NBOT_MAX][3];
	float msg_t, note_t; const char *msg, *msg_name, *note; int msg_face;
	int orig_ct[NBOT_MAX], orig_wpn[NBOT_MAX]; unsigned char labeled[NBOT_MAX], saved;
	V3 pdeath; float voice_cd;
} GA;

// ---- actors (0 = the player, 1 + i = bot i, as skills.h)
static int ga_side(int team) { return elim_side(&em, team); }              // 0 cyborg, 1 ghost
static int ga_team(int a) { return a == 0 ? 0 : bots[a - 1].team; }
static int ga_cyborg(int a) { return ga_side(ga_team(a)) == 0; }
static int ga_alive(int a) { return a == 0 ? elim_player_alive() : bots[a - 1].state != BS_DEAD; }
static V3 ga_pos(int a) { return a == 0 ? EMB.pl->pos : bots[a - 1].pl.pos; }
static int ga_n(void) { return 1 + nbot; }
static int ga_player_cyborg(void) { return ga_side(0) == 0; }
static float ga_hd(V3 a, V3 b) { float dx = a.x - b.x, dy = a.y - b.y; return sqrtf(dx * dx + dy * dy); }
static void ga_voice(const char *line) { ev_play("ui/doughnut_on", NULL); voice_play(line, 230, 0); }
static void ga_say(int face, const char *name, const char *msg, const char *line) { GA.msg_face = face; GA.msg_name = name; GA.msg = msg; GA.msg_t = 0; if (line) ga_voice(line); }
static void ga_notice(const char *t) { GA.note = t; GA.note_t = 0; }
// the player's side's commander: Aramaki for the ghosts, the Mastermind for the cyborgs
static void ga_say_side(const char *ghost_msg, const char *ghost_line, const char *cyb_msg, const char *cyb_line) {
	if (ga_player_cyborg()) ga_say(GA_FACE_MASTERMIND, "TERRORIST LEADER", cyb_msg, cyb_line); else ga_say(FACE_ARAMAKI, "CHIEF ARAMAKI", ghost_msg, ghost_line);   // (footage 131 s: the cyborgs' commander is the "Terrorist Leader")
}

// ---- hooks (bots.h, skills.h, main.c)
static int ga_melee_only(int i) { return ga_on && ga_cyborg(1 + i); }
static int ga_no_reserve(int i) { return ga_on && !ga_cyborg(1 + i); }
static int ga_force_cloak(int a) { return ga_on && a >= 0 && a < ga_n() && !ga_cyborg(a) && ga_alive(a) && !(a > 0 && bots[a - 1].grabbed); }   // (b70: a ghost in a cyborg's grip is seen: IGN 252.5-254 s)
static int ga_actor_skill(int a, int def) { return !ga_on || a < 0 || a >= ga_n() ? def : ga_cyborg(a) ? SK_RUSH : SK_OPTICAL; }   // Hyper Sprint / the camo gauge
static float ga_max_range(int i) { return ga_on && !ga_cyborg(1 + i) ? GA_GHOST_RANGE : -1; }
static int ga_grip_decloak(int a) { return ga_on && a > 0 && a < ga_n() && bots[a - 1].grabbed; }
// b70: executions (IGN "9 Minutes of Ghost Assault" 251.5-254.25 s, Nick Bunyun 257.75-260.25 s: a slash wounds the ghost,
// the cyborg's next blow that would kill it is the execution). Bot i's blow at tgt (-1 the player) dealing dmg: 1 from the
// front, 2 from behind (the victim faces away), 0 a plain slash
static int ga_exec_ok(int i, int tgt, float dmg) {
	if (!ga_on || em.state != ES_LIVE || i < 0 || i >= nbot || !ga_cyborg(1 + i) || bots[i].grabbed) return 0;
	V3 vp; float vyaw;
	if (tgt >= 0) { if (tgt >= nbot) return 0; const Bot *v = &bots[tgt];
		if (v->state == BS_DEAD || v->grabbed || v->ex_on || ga_cyborg(1 + tgt) || v->hp > dmg || !v->pl.grounded) return 0;
		for (int j = 0; j < nbot; j++) if (bots[j].ex_on && bots[j].ex_tgt == tgt) return 0;
		vp = v->pl.pos; vyaw = v->pl.yaw; }
	else if (tgt == -1) { if (!elim_player_alive() || ga_player_cyborg() || *EMB.php > dmg || !EMB.pl->grounded || *EMB.fly) return 0;
		for (int j = 0; j < nbot; j++) if (bots[j].ex_on && bots[j].ex_tgt == -1) return 0;
		vp = EMB.pl->pos; vyaw = EMB.pl->yaw; }
	else return 0;
	V3 d = vsub(bots[i].pl.pos, vp); float hd = sqrtf(d.x * d.x + d.y * d.y); if (hd > 200 || fabsf(d.z) > 60) return 0;
	float facing = hd > 1 ? (cosf(vyaw) * d.x + sinf(vyaw) * d.y) / hd : 1;   // > 0: the victim looks at the attacker
	return facing < -0.35f ? 2 : 1;
}

// ---- match start / end
static int ga_pistol_wpn(void) { for (int w = 0; w < NBOT_WPN; w++) if (!strcmp(BOT_WPN[w].sh, "seburom5")) return w; return 0; }
static void ga_restore_bots(void) {   // back to the operators and their guns (the next match)
	if (!GA.saved) return;
	for (int i = 0; i < nbot && i < NBOT_MAX; i++) { bot_restyle(&bots[i], GA.orig_ct[i], GA.orig_wpn[i], NULL); if (GA.labeled[i]) bot_label[i][0] = 0; GA.labeled[i] = 0; }
	GA.saved = 0;
}
static void ga_knife_arms(int cy);
static void ga_off(void) {
	if (ga_on) logp("ghost assault: off\n");
	ga_restore_bots(); if (ga_on) ga_knife_arms(0); wvm_cyborg = 0; ga_on = 0; ga_wpn_lock = 0; p_hp_max_ga = 100; bot_slash_dmg = 55; rh_lbl_a = rh_lbl_e = NULL;
	bot_melee_only = NULL; bot_no_reserve = NULL; bot_max_range = NULL; sk_force_cloak = NULL; bot_exec_ok = NULL; sk_grip_decloak = NULL;
}
static void ga_setup(int test) {   // after elim_start (elim_game.h): the mode's rules on top of Elimination's
	memset(&GA, 0, sizeof(GA)); GA.hold_tgt = -1;
	ga_on = 1; em.no_zones = 1; em.time_win_side = 1; em.round_time = em.round_left = test ? 60 : GA_ROUND_TIME; em.timewarn_at = em.round_time * 0.2f;   // (0:30 of 2:30: "Timer Warning")
	em.to_win = MENU_LIMITS[MODE_GA][1]; for (int k = 0; k < 4; k++) if (MENU_LIMITS[MODE_GA][k] == cfg.score_limit) em.to_win = cfg.score_limit;
	em.switch_after = em.to_win - 1; if (em.switch_after < 1) em.switch_after = 1;
	if (test) { em.to_win = 2; em.switch_after = 1; }   // the test run: two short rounds, one on each side
	for (int i = 0; i < nbot && i < NBOT_MAX; i++) { GA.orig_ct[i] = bots[i].ct; GA.orig_wpn[i] = bots[i].wpn;
		if (!bot_label[i][0]) { snprintf(bot_label[i], sizeof(bot_label[i]), "%s", chr_display(chrt[bots[i].ct].name)); GA.labeled[i] = 1; } }   // (the cyborg body keeps its operator's name)
	GA.saved = 1;
	bot_melee_only = ga_melee_only; bot_no_reserve = ga_no_reserve; bot_max_range = ga_max_range; bot_slash_dmg = GA_SLASH; sk_force_cloak = ga_force_cloak;
	bot_exec_ok = ga_exec_ok; sk_grip_decloak = ga_grip_decloak;   // b70
	wpn_bot_def = -1;   // no guns on the ground (the ghosts' pistols are not dropped)
	rh_lbl_a = "CYBORG"; rh_lbl_e = "GHOST";
	logp("ghost assault: first to %d (switch after %d), %.0f s rounds, cyborg body %s\n", em.to_win, em.switch_after, em.round_time, ga_cyb_ct >= 0 ? "chr_cyborg.gvm" : "MISSING (operators stand in)");
}
// the round's looks and loadouts: before the bots are spawned (elim_place_all), then after everyone is placed
static void ga_round_bots(void) {
	if (!ga_on) return; int pw = ga_pistol_wpn();
	for (int i = 0; i < nbot; i++) { Bot *b = &bots[i];
		if (ga_cyborg(1 + i)) bot_restyle(b, ga_cyb_ct >= 0 ? ga_cyb_ct : GA.orig_ct[i], GA.orig_wpn[i], "knife");
		else bot_restyle(b, GA.orig_ct[i], pw, NULL); }
	rh_lbl_a = ga_player_cyborg() ? "CYBORG" : "GHOST"; rh_lbl_e = ga_player_cyborg() ? "GHOST" : "CYBORG";
}
// the knife in the cyborg's chrome arms (vm_knife_cy.gvm, vm/make_vm_weapon.py knife_cy) or the operator's: loaded when the
// side changes (the round's freeze, ~0.1 - 0.3 s on the Vita, under the round banner)
static void ga_knife_arms(int cy) {
	wvm_cyborg = cy; WSlot *k = &wsl[WS_KNIFE]; if (!k->ok || (k->cy != 0) == cy) return;
	int d = k->def, r = wslot_load(WS_KNIFE, d, VDIR);
	logp("  ghost assault: knife arms %s (%s)\n", cy ? "cyborg" : "operator", r < 0 ? "FAILED" : wsl[WS_KNIFE].cy == 2 ? "ok: the katar, vm_axe_cy.gvm" : !cy ? "ok" : wsl[WS_KNIFE].cy ? "ok: the knife's blade, vm_knife_cy.gvm" : "no cyborg arms in the data");
	if (r < 0 && cy) { wvm_cyborg = 0; wslot_load(WS_KNIFE, d, VDIR); }
}
static void ga_player_loadout(void) {   // health (no armour in this mode), the side's one weapon, no grenades
	int cy = ga_player_cyborg(); p_hp_max_ga = cy ? GA_CY_HP : GA_GH_HP; *EMB.php = p_hp_max_ga; *EMB.pap = 0; ga_wpn_lock = cy ? 1 : 2;
	if (wsl[WS_GRENADE].ok) wsl[WS_GRENADE].ammo = wsl[WS_GRENADE].res = 0;
	ga_knife_arms(cy);
	if (!cy && wsl[WS_SECONDARY].ok) { wslot_refill(WS_SECONDARY); wsl[WS_SECONDARY].res = 0; }   // the ghosts' pistol: a magazine, no reserve ("20 / 0")
	if (vm_ok) { int s = cy ? WS_KNIFE : WS_SECONDARY; if (wsl[s].ok) { WP.quick_from = -1; WP.sw_to = s; weapon_finish_switch(); } }
}
static void ga_round_after(void) {
	if (!ga_on) return;
	for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; b->hp = ga_cyborg(1 + i) ? GA_CY_HP : GA_GH_HP; if (!ga_cyborg(1 + i)) b->mag = BOT_WPN[b->wpn].mag; }
	ga_player_loadout();
	for (int a = 0; a < 1 + NBOT_MAX; a++) { GA.ping_t[a] = 1.0f + frand() * GA_PING_EVERY; GA.ping_age[a] = 99; GA.ping_show[a] = 0; GA.used[a] = 0; }
	for (int i = 0; i < NBOT_MAX; i++) { GA.bot_hold[i] = 0; GA.bot_hold_t[i] = 0; GA.hide_t[i] = 0; }
	GA.hold_tgt = -1; GA.hold_t = 0; GA.dive_t = 0; GA.drone = 0; GA.danger_t = GA.detected_t = 0; GA.note_t = 99;
	if (em.round == 1 || em.round == em.switch_after + 1)   // the side's briefing (round 1, and after the switch)
		ga_say_side("Stay hidden. Survive until the time runs out!", em.round == 1 ? "d_aramaki_gm_001" : "d_aramaki_gm_003",
			"Find the Ghosts and eliminate them!", em.round == 1 ? "d_mastermind_gm_001" : "d_mastermind_gm_003");
	GA.msg_t = em.round == 1 || em.round == em.switch_after + 1 ? -1.2f : 99;
}

// ---- reveals: an actor of the other side shown to team for s seconds (vision.h draws it through walls; bots go there)
static void ga_reveal(int a, int team, float s) {
	if (a < 0 || a >= SK_MAXACT || !sk_valid(a)) return;
	SkillActor *A = &SKA[a]; if (A->mark_t[team & 1] < s) { A->mark_t[team & 1] = s; A->mark_by[team & 1] = -1; }
	GA.ping_pos[a] = ga_pos(a); GA.ping_age[a] = 0;
}
static void ga_reveal_team(int victim_team, int to_team, float s) { for (int a = 0; a < ga_n(); a++) if (ga_alive(a) && ga_team(a) == victim_team) ga_reveal(a, to_team, s); }
// someone of side `hacker_side` hacked / restored body `a` (finished): its effect and the lines each side hears
static void ga_body_done(int hacker, int a) {
	int ht = ga_team(hacker), enemy = ga_team(a) != ht, pc = ga_player_cyborg(), hcy = ga_cyborg(hacker); GA.used[a] = 1;
	if (enemy) {   // the cyberbrain hack: the body's team shown to the hacker's for a while
		ga_reveal_team(ga_team(a), ht, GA_REVEAL_T);
		if (hacker == 0) { sk_notify(0, SKN_POINTS, "ENEMY CYBERBRAIN HACKED", 300); GA.dive_t = GA_REVEAL_T; ev_play("ui/ghost_brain_hacking_success", NULL); ev_play("ui/ghost_dive_in", NULL); }
		else if (ht == 0) ev_play("ui/ghost_team_hacked", NULL);
		if (ht != 0) { GA.detected_t = 2.5f; ev_play("ui/ghost_scan_detected", NULL); }   // the enemy now knows where we are
		// the lines: (cyborgs hear the Mastermind, ghosts the Operator) - who hacked whose body
		if (pc) voice_play(hcy ? "d_mastermind_gm_012" : "d_mastermind_gm_011", 230, 0); else voice_play(hcy ? "d_op_gm_002" : "d_op_gm_001", 230, 0);
		logp("  ghost assault: %s hacked %s's body (team %d shown to team %d)\n", hacker ? bot_name(hacker - 1) : "player", a ? bot_name(a - 1) : "player", ga_team(a), ht);
	} else {       // an ally restored where it lies
		if (a == 0) { *EMB.pdead = 0; *EMB.hurt = 0; *EMB.reload = 0; ga_player_loadout(); ga_notice("You are back in the fight."); ev_play("ui/ghost_respawn", NULL); }
		else { BotTarget T = { EMB.pl->pos, EMB.pl->vel, elim_player_alive() }; Bot *b = &bots[a - 1]; bot_revive_here(a - 1, &T);
			b->hp = ga_cyborg(a) ? GA_CY_HP : GA_GH_HP; if (!ga_cyborg(a)) b->mag = BOT_WPN[b->wpn].mag; if (ht == 0) ev_play("ui/ghost_respawn", NULL); }
		if (hacker == 0) sk_notify(0, SKN_POINTS, "ALLY RESPAWN SUCCESSFUL", 500);
		if (pc) voice_play(hcy ? "d_mastermind_gm_014" : "d_mastermind_gm_013", 230, 0); else voice_play(hcy ? "d_op_gm_004" : "d_op_gm_003", 230, 0);
		GA.used[a] = 0;
		logp("  ghost assault: %s restored %s\n", hacker ? bot_name(hacker - 1) : "player", a ? bot_name(a - 1) : "player");
	}
}
// the body actor a can work on (-1 none): dead, not used, its corpse within reach of feet p
static int ga_body_near(int self, V3 p, float reach) {
	int best = -1; float bd = reach;
	for (int b = 0; b < ga_n(); b++) { if (b == self || ga_alive(b) || GA.used[b]) continue;
		if (b == 0 && (*EMB.pdead <= 0 || *EMB.fly)) continue; if (b > 0 && bots[b - 1].dead_t < 1.0f) continue;
		V3 q = b == 0 ? GA.pdeath : bots[b - 1].pl.pos; float d = ga_hd(p, q); if (d < bd && fabsf(q.z - p.z) < 130) { bd = d; best = b; } }
	return best;
}

// ---- bots: where to go (elim_goal) and their hacks
static int ga_goal(int i, float *g) {
	Bot *b = &bots[i]; V3 p = b->pl.pos; int me = 1 + i;
	if (GA.bot_hold[i]) { V3 q = GA.bot_hold[i] - 1 == 0 ? GA.pdeath : bots[GA.bot_hold[i] - 2].pl.pos; g[0] = q.x; g[1] = q.y; g[2] = q.z; return 1; }
	if (ga_cyborg(me)) {
		if (GA.drone == 1) { float dd = ga_hd(p, GA.drone_pos); int nearest = 1; for (int j = 0; j < nbot; j++) if (j != i && ga_cyborg(1 + j) && ga_alive(1 + j) && ga_hd(bots[j].pl.pos, GA.drone_pos) < dd) nearest = 0;
			if (nearest || dd < 2500) { g[0] = GA.drone_pos.x; g[1] = GA.drone_pos.y; g[2] = GA.drone_pos.z; return 1; } }
		// the freshest ping (or a reveal) of a living ghost, nearer ones first
		int best = -1; float bs = 1e9f;
		for (int a = 0; a < ga_n(); a++) { if (ga_cyborg(a) || !ga_alive(a) || GA.ping_age[a] > 8) continue; float s = GA.ping_age[a] * 400 + ga_hd(p, GA.ping_pos[a]); if (s < bs) { bs = s; best = a; } }
		if (best >= 0) { V3 q = GA.ping_pos[best]; g[0] = q.x + (frand() - 0.5f) * 200; g[1] = q.y + (frand() - 0.5f) * 200; g[2] = q.z; return 1; }
		int body = ga_body_near(me, p, 2500); if (body >= 0) { V3 q = body == 0 ? GA.pdeath : bots[body - 1].pl.pos; g[0] = q.x; g[1] = q.y; g[2] = q.z; return 1; }   // a body to hack / restore
		if (nspawn > 0) { int s = rand() % nspawn; g[0] = spawns[s].x; g[1] = spawns[s].y; g[2] = spawns[s].z; return 1; }   // sweep the map
		return 0;
	}
	// a ghost: restore an ally nobody hunts near, else keep to a hiding spot far from the cyborgs (a new one when one comes near)
	{ int body = ga_body_near(me, p, 2000); if (body >= 0 && ga_team(body) == b->team) { V3 q = body == 0 ? GA.pdeath : bots[body - 1].pl.pos; int safe = 1;
		for (int a = 0; a < ga_n(); a++) if (ga_cyborg(a) && ga_alive(a) && ga_hd(ga_pos(a), q) < 1200) safe = 0;
		if (safe) { g[0] = q.x; g[1] = q.y; g[2] = q.z; return 1; } } }
	float near_c = 1e9f; for (int a = 0; a < ga_n(); a++) if (ga_cyborg(a) && ga_alive(a)) near_c = fminf(near_c, ga_hd(ga_pos(a), p));
	if (GA.hide_t[i] <= 0 || near_c < 900) {
		int bestS = -1; float bv = -1;
		for (int k = 0; k < 24 && nspawn > 0; k++) { int s = rand() % nspawn; V3 q = v3(spawns[s].x, spawns[s].y, spawns[s].z); float m = 1e9f;
			for (int a = 0; a < ga_n(); a++) if (ga_cyborg(a) && ga_alive(a)) m = fminf(m, ga_hd(ga_pos(a), q));
			float v = fminf(m, 4000) - 0.15f * ga_hd(q, p) + frand() * 600; if (v > bv) { bv = v; bestS = s; } }
		if (bestS >= 0) { GA.hide[i][0] = spawns[bestS].x; GA.hide[i][1] = spawns[bestS].y; GA.hide[i][2] = spawns[bestS].z; GA.hide_t[i] = 18 + frand() * 14; }
	}
	g[0] = GA.hide[i][0]; g[1] = GA.hide[i][1]; g[2] = GA.hide[i][2]; return GA.hide_t[i] > 0;
}
// the seeker drone picked up by actor a (a cyborg): its pulses start; each side hears what it means
static void ga_drone_deploy(int a) {
	if (GA.drone != 1) return; GA.drone = 2; GA.drone_t = 0.5f; ev_play("ui/ghost_seeker_drone_pickup", a == 0 ? NULL : &GA.drone_pos.x);
	if (ga_player_cyborg()) { ga_notice("Seeker Drones report enemy locations."); voice_play("d_mastermind_gm_016", 230, 0); }
	else { ga_notice("Enemy Seeker Drones report your team's positions."); voice_play("d_op_gm_007", 230, 0); }
	logp("  ghost assault: seeker drone deployed by %s\n", a ? bot_name(a - 1) : "the player");
}
static void ga_bots_frame(float dt) {
	for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; int me = 1 + i;
		if (GA.hide_t[i] > 0) GA.hide_t[i] -= dt;
		if (b->state == BS_DEAD) { GA.bot_hold[i] = 0; continue; }
		if (GA.bot_hold[i]) {   // working on a body: kneel there (bots.h hold) until done; a fight breaks it off
			int a = GA.bot_hold[i] - 1; V3 q = a == 0 ? GA.pdeath : bots[a - 1].pl.pos;
			if (ga_alive(a) || GA.used[a] || b->state == BS_ENGAGE || ga_hd(b->pl.pos, q) > GA_REACH + 40) { GA.bot_hold[i] = 0; b->hold = 0; continue; }
			b->hold = 1; b->hold_yaw = atan2f(q.y - b->pl.pos.y, q.x - b->pl.pos.x); b->hold_pitch = -0.5f;
			GA.bot_hold_t[i] += dt; float need = ga_team(a) == b->team ? GA_REVIVE_T : GA_HACK_T;
			if (GA.bot_hold_t[i] >= need) { GA.bot_hold[i] = 0; b->hold = 0; ga_body_done(me, a); }
			continue;
		}
		if (b->state == BS_ROAM && em.state == ES_LIVE) { int a = ga_body_near(me, b->pl.pos, GA_REACH);
			if (a >= 0 && (ga_cyborg(me) || ga_team(a) == b->team)) { GA.bot_hold[i] = 1 + a; GA.bot_hold_t[i] = 0; }   // (ghosts restore allies; cyborgs hack and restore)
			if (ga_cyborg(me) && GA.drone == 1 && ga_hd(b->pl.pos, GA.drone_pos) < 160) ga_drone_deploy(me); }
	}
}

// ---- each frame (elim_frame, after the round logic)
static void ga_frame(float dt) {
	if (!ga_on) return;
	GA.msg_t += dt; GA.note_t += dt; if (GA.voice_cd > 0) GA.voice_cd -= dt; if (GA.dive_t > 0) { GA.dive_t -= dt; if (GA.dive_t <= 0) ev_play("ui/ghost_dive_end", NULL); }
	if (GA.detected_t > 0) GA.detected_t -= dt; GA.scan_t += dt;
	if (elim_player_alive()) GA.pdeath = EMB.pl->pos;
	for (int a = 0; a < 1 + NBOT_MAX; a++) { GA.ping_age[a] += dt; if (GA.ping_show[a] > 0) GA.ping_show[a] -= dt; }
	for (int a = 0; a < ga_n(); a++) if (ga_alive(a)) GA.used[a] = 0;
	if (em.state != ES_LIVE) { GA.hold_tgt = -1; GA.hold_t = 0; return; }
	int pc = ga_player_cyborg(), palive = elim_player_alive();
	// pings: each living ghost, every few seconds, when a cyborg is near (or map-wide once the seeker drone is up)
	for (int a = 0; a < ga_n(); a++) {
		if (ga_cyborg(a) || !ga_alive(a)) continue;
		GA.ping_t[a] -= dt; if (GA.ping_t[a] > 0) continue;
		GA.ping_t[a] = (GA.drone == 2 ? GA_DRONE_EVERY : GA_PING_EVERY) + frand() * 0.6f;
		V3 p = ga_pos(a); float nd = 1e9f; for (int c = 0; c < ga_n(); c++) if (ga_cyborg(c) && ga_alive(c)) nd = fminf(nd, ga_hd(ga_pos(c), p));
		if (nd > GA_PING_R && GA.drone != 2) continue;
		GA.ping_pos[a] = p; GA.ping_age[a] = 0;
		if (pc && palive && a != 0) { float d = ga_hd(EMB.pl->pos, p); if (d <= GA_PING_R || GA.drone == 2) { GA.ping_show[a] = GA_PING_SHOW; GA.ping_dist[a] = d; ev_play("ui/ghost_scan_triangle", NULL); } }
	}
	// the seeker drone: appears late in the round, the first cyborg to reach it deploys it
	if (GA.drone == 0 && em.round_left <= em.round_time * (GA_DRONE_AT / GA_ROUND_TIME) && nspawn > 0) {   // (0:45 of 2:30)
		int best = 0; float bv = -1;
		for (int k = 0; k < 16; k++) { int s = rand() % nspawn; V3 q = v3(spawns[s].x, spawns[s].y, spawns[s].z); float m = 1e9f;
			for (int c = 0; c < ga_n(); c++) if (ga_cyborg(c) && ga_alive(c)) m = fminf(m, ga_hd(ga_pos(c), q));
			float v = -fabsf(m - 1800) + frand() * 300; if (v > bv) { bv = v; best = s; } }   // (not on top of a cyborg, not across the map)
		GA.drone = 1; GA.drone_pos = v3(spawns[best].x, spawns[best].y, spawns[best].z);
		if (pc) ga_notice("SEEKER DRONE AVAILABLE"); logp("  ghost assault: seeker drone at %.0f %.0f %.0f\n", GA.drone_pos.x, GA.drone_pos.y, GA.drone_pos.z);
	}
	if (GA.drone == 1 && pc && palive && ga_hd(EMB.pl->pos, GA.drone_pos) < 160 && fabsf(EMB.pl->pos.z - GA.drone_pos.z) < 150) ga_drone_deploy(0);
	if (GA.drone == 2) {
		GA.drone_t -= dt;
		if (GA.drone_t <= 0) { GA.drone_t = GA_DRONE_EVERY; int cteam = ga_side(0) == 0 ? 0 : 1;   // (the cyborgs' team)
			for (int a = 0; a < ga_n(); a++) if (!ga_cyborg(a) && ga_alive(a)) ga_reveal(a, cteam, 1.8f);
			if (!pc && palive) { GA.detected_t = 1.8f; ev_play("ui/ghost_scan_detected", NULL); } else if (pc) ev_play("ui/ghost_scan", NULL); }
	}
	ga_bots_frame(dt);
	// the player's hack / restore: hold Square over a body
	if (palive) {   // (the test run: its player holds Square over any body a little farther away, so the panel and the hack show)
		int ap = *EMB.autoplay, a = ga_body_near(0, EMB.pl->pos, ap ? GA_REACH * 3 : GA_REACH);
		if (a != GA.hold_tgt) { GA.hold_tgt = a; GA.hold_t = 0; }
		if (a >= 0 && (ga_use_held || ap)) { if (GA.hold_t == 0) ev_play(ga_team(a) == 0 ? "ui/ghost_respawn" : "ui/ghost_brain", NULL);
			GA.hold_t += dt; if (GA.hold_t >= (ga_team(a) == 0 ? GA_REVIVE_T : GA_HACK_T)) { ga_body_done(0, a); GA.hold_tgt = -1; GA.hold_t = 0; } }
		else GA.hold_t = 0;
	} else { GA.hold_tgt = -1; GA.hold_t = 0; }
	// the ghost player's danger / detection
	if (!pc && palive) {
		float nd = 1e9f; int seen = 0;
		for (int i = 0; i < nbot; i++) { if (!ga_cyborg(1 + i) || bots[i].state == BS_DEAD) continue; nd = fminf(nd, ga_hd(bots[i].pl.pos, EMB.pl->pos)); if (bots[i].tgt == -1 && bots[i].sees && bots[i].state == BS_ENGAGE) seen = 1; }
		if (nd < GA_DANGER_R) GA.danger_t = 1.0f; else if (GA.danger_t > 0) GA.danger_t -= dt;
		if (seen) { if (GA.detected_t <= 0) ev_play("ui/ghost_scan_detected", NULL); GA.detected_t = fmaxf(GA.detected_t, 0.6f); }
		if (sk_valid(0) && SKA[0].mark_t[1 - ga_team(0)] > 0) GA.detected_t = fmaxf(GA.detected_t, 0.4f);
	} else GA.danger_t = 0;
}
// round events (elim_frame): the lines at the round's end / the time alarm
static void ga_round_event(unsigned ev) {
	if (!ga_on) return;
	if (ev & EEV_TIMEWARN) { ev_play("ui/ghost_time_warning", NULL); voice_play(ga_player_cyborg() ? "d_mastermind_gm_005" : "d_aramaki_gm_005", 230, 0); }
	if (ev & EEV_ROUND_END) { int won = em.last_winner == 0; if (em.last_winner >= 0) voice_play(ga_player_cyborg() ? (won ? "d_mastermind_gm_006" : "d_mastermind_gm_009") : (won ? "d_aramaki_gm_006" : "d_aramaki_gm_009"), 230, 0);
		for (int i = 0; i < nbot; i++) bots[i].hold = 0; GA.dive_t = 0; }
}
// ---- HUD state (elim_hud): health scale, pings on the minimap are set by main.c from GA.ping_show
static void ga_hud(HudState *H) { if (!ga_on) return; H->hp_max = p_hp_max_ga; H->zone_tag = NULL; }

// ---- HUD drawing (elim_hud_draw, hud pack current; ga_hud_mvp = the world -> clip matrix of this frame): this frame's
// pieces described for ga_hud.h's painter
#include "ga_hud.h"
static int ga_project(V3 p, float *sx, float *sy) {
	const float *M = ga_hud_mvp; if (!M) return 0; float c[4]; for (int i = 0; i < 4; i++) c[i] = M[i] * p.x + M[4 + i] * p.y + M[8 + i] * p.z + M[12 + i];
	if (c[3] < 1) return 0; *sx = (c[0] / c[3] * 0.5f + 0.5f) * 960; *sy = (0.5f - c[1] / c[3] * 0.5f) * 544; return 1;
}
static void ga_hud_draw(float now) {
	if (!ga_on || !EMB.pl) return;
	int pc = ga_player_cyborg(), palive = elim_player_alive(); GaHudView V; memset(&V, 0, sizeof(V)); V.now = now;
	HUD_TAG("ga");
	if (GA.dive_t > 0 && palive) {
		V.dive_k = fminf(1, fminf(GA.dive_t / 0.4f, (GA_REVEAL_T - GA.dive_t) / 0.3f)); int mt = ga_team(0) & 1;
		for (int a = 1; a < ga_n() && ga_hud_mvp && V.ndia < 8; a++) { if (!ga_alive(a) || ga_team(a) == ga_team(0) || !sk_valid(a) || SKA[a].mark_t[mt] <= 0) continue;
			float sx, sy; V3 q = ga_pos(a); q.z += 110; if (!ga_project(q, &sx, &sy) || sx < 0 || sx > 960 || sy < 0 || sy > 544) continue;
			V.dia[V.ndia][0] = sx; V.dia[V.ndia][1] = sy; V.ndia++; }
	}
	if (pc && palive && (em.state == ES_LIVE || em.state == ES_FREEZE)) {   // (not under the round's result / switching bands)
		V.compass = 1; V.yaw = EMB.pl->yaw;
		for (int a = 1; a < ga_n() && V.nping < 8; a++) { if (GA.ping_show[a] <= 0) continue; V3 p = GA.ping_pos[a];
			V.ping_rel[V.nping] = wrap_pi(EMB.pl->yaw - atan2f(p.y - EMB.pl->pos.y, p.x - EMB.pl->pos.x)); V.ping_show[V.nping] = GA.ping_show[a]; V.ping_close[V.nping] = GA.ping_dist[a] < GA_PING_CLOSE; V.nping++; }
		float sx, sy; V3 q = GA.drone_pos; q.z += 120;
		if (GA.drone == 1 && ga_hud_mvp && ga_project(q, &sx, &sy)) { V.drone = 1; V.drone_x = fminf(fmaxf(sx, 120), 780); V.drone_y = fminf(fmaxf(sy, 200), 360); V.drone_m = ga_hd(EMB.pl->pos, GA.drone_pos) / 100; }   // (clear of the radar, the pause button, the skill chips)
	}
	if (!pc && palive && em.state == ES_LIVE) {
		if (GA.detected_t > 0) V.detected_k = fminf(1, GA.detected_t / 0.3f);
		if (GA.danger_t > 0) V.danger_k = fminf(1, GA.danger_t / 0.3f);
	}
	if (palive && GA.hold_tgt >= 0 && em.state == ES_LIVE) { int ally = ga_team(GA.hold_tgt) == ga_team(0); V.hack = 1; V.hack_ally = ally; V.hack_pr = GA.hold_t / (ally ? GA_REVIVE_T : GA_HACK_T); }
	ga_hud_paint(&V);
}
// ---- overlays (elim_overlays_draw, ovl pack): the commander's lines and the mode's notices
static void ga_overlays_draw(void) {
	if (!ga_on) return;
	if (GA.msg && GA.msg_t < 6) menu_commander_draw(GA.msg_face, GA.msg_name, GA.msg, GA.msg_t);
	if (GA.note && GA.note_t < 3) menu_notice_draw(GA.note, GA.note_t);
}
// the round banner's objective for the player's side this round
static const char *ga_goal_text(void) { return ga_player_cyborg() ? "Hunt down and eliminate the Ghosts" : "Stay hidden and survive until time runs out"; }
