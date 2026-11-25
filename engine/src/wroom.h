// wroom.h - b84 (user: "try to make the "Waiting for players" screen and it's animations when the players connect look as
// much like the videos as possible"): the v2.0 match start, from the user's EP26 Reiko video 1:26-2:10 (the same in EP16,
// EP17, EP25):
//   the dive (1:30-1:37): black, the Section 9 emblem glowing up, "[ESC] - Skip Dive" (here {x} SKIP DIVE), light lines
//     rushing out of the centre faster and faster, a burst of light, a white flash;
//   the waiting room (1:38-2:06): the map in the network view (world.h W_NETSET), the player free to walk with the hologram
//     gun (vision.h's spawn look, held for the whole wait); a dark band across the top with "Waiting for players..." in
//     yellow over a tip (they change every few seconds); the countdown under it (yellow from 3); DAILY MISSIONS on the left
//     and the chat log under them ("<name> joined the battle."); the own team's cards in the middle at the bottom (yours
//     framed yellow), the two teams' lists under them (yours on the left in blue, READY in yellow, a red VS);
//   the world materialising (2:06): white pixel blocks over the switch back, then the mode's banner (main.c's start splash,
//     map_t starts at the end of the room) - the gun stays a hologram ~3 s (vision.h vis_round_start).
// Offline nobody has to load: the room runs WR_LEN s and the bots "connect" one after another in its first half (a card
// flips in with a flash, the list row writes the name and READY pops, the chat log says who joined, ui/ready_on).
// main.c: wr_begin + wr_add per player after the map is set up; while wr_on(): the sim is held (bots, modes, the match clock),
// the player walks but cannot fire, aim or use skills, the game HUD is off; wr_update each frame, wr_draw after the HUD,
// wr_netview() switches the world's network view.

// b102 (task #154): the dive is the game's own - effect\cyber_world\start_screen.nif (the Section 9 emblem on black, swelling
// past the camera at 2-3 s, light streaks, rings rushing by, white and red flickers; its black backdrop clears at 4.07-4.27 s),
// then the camera of effect\camera\cam_cyber_world.nif falling from 226 m onto the spawn over the network's map
// (start_bg_deco.nif, 700 m of circuit lines on the ground, the level under it in the network view), and
// start_screen_end.nif's flare and rings into the room. 8 s; fxscene.h plays the three (dive_a / dive_b / dive_c.gsc, baked by
// tools/fx/make_scene.py). Without the files: b84's drawn dive below.
#define WR_DIVE2_LEN 8.0f
#define WR_DIVE2_CAM 4.0f   // the camera path takes over (its descent starts at 4.27 s; the backdrop clears from 4.07 s)
#define WR_DIVE2_END 7.6f   // start_screen_end.nif begins
#define WR_DIVE_LEN 2.6f    // the dive: emblem 0-0.9 s, lines 0.7-2.2 s, the burst and the white flash 2.0-2.6 s
#define WR_LEN 15.0f        // the room's count (the footage's online wait: 39 s)
#define WR_OUT_LEN 0.8f     // the materialising blocks
enum { WR_OFF, WR_DIVE, WR_WAIT, WR_OUT };
typedef struct { char name[24]; int chr, team, me; float t_in; } WrPlayer;
static struct {
	int phase, np, mode, cd_last, tip, nchat, chat_seen;
	float t, pt, tip_t;
	WrPlayer p[12];
	char chat[5][48]; float chat_t[5];
	const char *mis[3]; int mis_need[3];
	unsigned rs;
} WR;
static struct { FxScene a, b, c; int ok; float end_t; } WRD;   // b102: the dive's scenes; end_t > 0: the end flare still playing over the room
static int wr_on(void) { return WR.phase != WR_OFF; }
static float wr_dbg_t0 = -1, wr_dbg_rate = 1, wr_dbg_w0; static int wr_dbg_cycle;   // test switch divedbg.txt "<t0> <rate> [<first mesh> <end mesh>]": from t0 the dive runs at that rate, its time on the screen (a look at single frames in the emulator); rate 0: the dive starts at t0 and stays there, and with first mesh -1 the meshes are shown one after the other, 1.5 s each
static float wr_dive_len(void) { return WRD.ok ? WR_DIVE2_LEN : WR_DIVE_LEN; }
static int wr_netview(void) { return WR.phase == WR_WAIT || (WR.phase == WR_OUT && WR.pt < WR_OUT_LEN * 0.4f) || (WR.phase == WR_DIVE && WRD.ok && WR.pt >= WR_DIVE2_CAM); }
static int wr_dive_cam(float *eye, float *dir, float *up) { return WR.phase == WR_DIVE && WRD.ok && WR.pt >= WR_DIVE2_CAM && fxs_camera(&WRD.b, WR.pt, eye, dir, up); }
static void wr_dive_world(const float *PVM) { if (WR.phase == WR_DIVE && WRD.ok) fxs_draw(&WRD.b, WR.pt, PVM, fminf(1, (WR.pt - WR_DIVE2_CAM) / 0.3f)); }
static int wr_dive_dark(void) { return WR.phase == WR_DIVE && WRD.ok && WR.pt < 4.05f; }   // the dive's black backdrop is whole: nothing of the level shows (main.c draws none)
static void wr_dive_free(void) { fxs_free(&WRD.a); fxs_free(&WRD.b); fxs_free(&WRD.c); WRD.ok = 0; WRD.end_t = 0; }
static float wr_rnd(void) { WR.rs = WR.rs * 1664525u + 1013904223u; return (WR.rs >> 8) / 16777216.0f; }
// the mode's tips (the footage's own lines where they were legible; the rest in the same voice, about this port's rules)
static const char *const WR_TIPS_ANY[] = {
	"Therm-optic Camo, Nano-Gel Armor, Heat Sensor and Hyper Sprint can be shared at Tier 2 via SkillSync.",
	"The Tier 2 Arm Launcher fires 4 missiles.",
	"Tier 2 skills charge as you score: watch the big ring.",
	"Press {s} near an ally using a Tier 2 skill to use SkillSync.",   // (the footage's line, its F key as the Vita button)
};
static const char *const WR_TIPS_DEM[] = {
	"Offense and defense will switch sides once before a match is over.",
	"Planting a bomb will stop the round timer and start a new explosion counter that will end the match unless it is disarmed.",
	"Planting a bomb will not immediately show its location to defenders.",
	"The demolition target will be displayed on the HUD and map.",
};
static const char *const WR_TIPS_TDM[] = {
	"Eliminations are worth 100 points. The first team to the score limit wins.",
	"After each death you return to the fight in a few seconds.",
};
static const char *const WR_TIPS_TTM[] = {
	"Capture terminals to call in your team's Think Tank.",
	"The team holding more terminals when time runs out wins.",
};
static const char *const WR_TIPS_EM[] = {
	"One life per round: the last team standing wins the round.",
	"Parts of the map are locked after a while. Anyone caught inside takes lethal damage.",
};
static const char *wr_tip(int k) {
	const char *const *m = WR_TIPS_TDM; int nm = 2;
	if (WR.mode == MODE_DEM) { m = WR_TIPS_DEM; nm = 4; } else if (WR.mode == MODE_TTM) { m = WR_TIPS_TTM; nm = 2; } else if (WR.mode == MODE_EM || WR.mode == MODE_GA) { m = WR_TIPS_EM; nm = 2; }
	int n = nm + 4; k = ((k % n) + n) % n;
	return k < nm ? m[k] : WR_TIPS_ANY[k - nm];
}
static void wr_begin(int mode) {
	memset(&WR, 0, sizeof(WR)); WR.phase = WR_DIVE; WR.mode = mode; WR.cd_last = -1; WR.rs = (unsigned)(sceKernelGetProcessTimeWide() & 0xffffffu) | 1u; WR.tip = (int)(wr_rnd() * 64);
	int rounds = mode == MODE_DEM || mode == MODE_EM || mode == MODE_GA;   // (as menu_career's three missions)
	WR.mis[0] = "Win the match"; WR.mis_need[0] = 1;
	WR.mis[1] = rounds ? "Defeat 5 enemies" : "Defeat 10 enemies"; WR.mis_need[1] = rounds ? 5 : 10;
	WR.mis[2] = mode == MODE_DEM ? "Plant or defuse the bomb" : mode == MODE_TTM ? "Capture a terminal" : "Use cyberbrain skills 3 times"; WR.mis_need[2] = mode == MODE_DEM || mode == MODE_TTM ? 1 : 3;
	ev_play("ui/logo_dive", NULL);
	wr_dive_free();
	WRD.ok = fxs_load(&WRD.a, VDIR "dive_a.gsc") == 0 && fxs_load(&WRD.b, VDIR "dive_b.gsc") == 0 && fxs_load(&WRD.c, VDIR "dive_c.gsc") == 0; if (!WRD.ok) wr_dive_free();
	{ FILE *df = fopen(VDIR "divedbg.txt", "rb"); wr_dbg_t0 = -1; wr_dbg_rate = 1; fxs_dbg_lo = 0; fxs_dbg_hi = FXS_MAXM;
	  if (df) { if (fscanf(df, "%f %f %d %d", &wr_dbg_t0, &wr_dbg_rate, &fxs_dbg_lo, &fxs_dbg_hi) < 2) { wr_dbg_t0 = -1; wr_dbg_rate = 1; } fclose(df); }
	  wr_dbg_cycle = wr_dbg_t0 >= 0 && fxs_dbg_lo < 0; wr_dbg_w0 = -1; }
	logp("waiting room (b84): the dive (%s), then %.0f s\n", WRD.ok ? "the game's start_screen / cam_cyber_world scenes, 8 s - b102" : "drawn: no dive_a / b / c.gsc", WR_LEN);
}
// a player in the room: team 0 = yours. The others connect at random times in the first half of the count.
static void wr_add(const char *name, int chr, int team, int me) {
	if (WR.np >= 12) return; WrPlayer *P = &WR.p[WR.np++];
	snprintf(P->name, sizeof(P->name), "%s", name ? name : "PLAYER"); P->chr = chr; P->team = team; P->me = me;
	P->t_in = me ? 0 : 0.8f + wr_rnd() * WR_LEN * 0.5f;
}
static void wr_chat(const char *fmt, const char *s) {
	if (WR.nchat == 5) { memmove(WR.chat[0], WR.chat[1], sizeof(WR.chat[0]) * 4); memmove(WR.chat_t, WR.chat_t + 1, sizeof(float) * 4); WR.nchat = 4; }
	snprintf(WR.chat[WR.nchat], sizeof(WR.chat[0]), fmt, s); WR.chat_t[WR.nchat] = WR.t; WR.nchat++;
}
// returns 1 on the frame the room ends (main.c: the match starts); pressed = menu_buttons() of this frame
static int wr_update(float dt, unsigned pressed) {
	if (WR.phase == WR_OFF) return 0;
	if (WR.phase == WR_DIVE && wr_dbg_t0 >= 0 && wr_dbg_rate == 0 && WR.pt < wr_dbg_t0) WR.pt = wr_dbg_t0;
	WR.pt += WR.phase == WR_DIVE && wr_dbg_t0 >= 0 && WR.pt >= wr_dbg_t0 ? dt * wr_dbg_rate : dt;
	if (WR.phase == WR_DIVE) {
		float L = wr_dive_len();   // (b102: ui/logo_dive is 9.6 s - the sound of the game's whole dive; b84's two extra cues belong to the drawn one)
		if (!WRD.ok && WR.pt - dt < 0.7f && WR.pt >= 0.7f) ev_play("ui/ghost_dive_in", NULL);
		if ((pressed & MB_OK) && WR.pt < L - 0.4f) WR.pt = L - 0.4f;   // {x} SKIP DIVE: straight to the flash
		if (WR.pt >= L) { WR.phase = WR_WAIT; WR.pt = 0; WR.t = 0; if (!WRD.ok) ev_play("ui/ghost_dive_end", NULL); wr_chat("%s is in the waiting room.", WR.np ? WR.p[0].name : "PLAYER");
			if (WRD.ok) { WRD.end_t = WR_DIVE2_LEN - WR_DIVE2_END; fxs_free(&WRD.a); fxs_free(&WRD.b); } }
		return 0;
	}
	if (WRD.end_t > 0) { WRD.end_t += dt; if (WRD.end_t > fxs_len(&WRD.c)) wr_dive_free(); }
	if (WR.phase == WR_WAIT) {
		float t0 = WR.t; WR.t += dt; WR.tip_t += dt;
		if (WR.tip_t > 6.0f) { WR.tip_t = 0; WR.tip++; }
		for (int i = 0; i < WR.np; i++) if (!WR.p[i].me && t0 < WR.p[i].t_in && WR.t >= WR.p[i].t_in) { ev_play("ui/ready_on", NULL); wr_chat("%s joined the battle.", WR.p[i].name); }
		float left = WR_LEN - WR.t; int n = left > 0 ? (int)ceilf(left) : 0;
		if (n != WR.cd_last && n <= 3) ev_play(n > 0 ? "ui/ready_count" : "ui/ready_game_start_end", NULL);
		WR.cd_last = n;
		if (left <= 0) { WR.phase = WR_OUT; WR.pt = 0; ev_play("ui/ready_game_start", NULL); logp("waiting room: everyone is in (%d players): the match starts\n", WR.np); }
		return 0;
	}
	if (WR.pt >= WR_OUT_LEN) { WR.phase = WR_OFF; return 1; }
	return 0;
}
// ---- drawing (the ovl pack; 960 x 544; text >= 20 px)
#define WR_CX 606   // the cards and the lists: centred right of the missions / chat column (x 14-336)
static const float WR_BLUE[4] = { 0.22f, 0.62f, 0.92f, 1 }, WR_REDT[4] = { 0.92f, 0.28f, 0.26f, 1 }, WR_YEL[4] = { 1.0f, 0.82f, 0.10f, 1 };
static void wr_col(float *o, const float *c, float a) { o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = c[3] * a; }
static void wr_draw_dive(void) {
	float t = WR.pt, k[4];
	static const float blk[4] = { 0, 0, 0, 1 }; mu_rect(0, 0, 960, 544, blk);
	// the emblem: three broken rings and a core, glowing up and out (0 - 0.9 s)
	float ea = t < 0.25f ? t / 0.25f : t < 0.75f ? 1 : fmaxf(0, 1 - (t - 0.75f) / 0.3f);
	if (ea > 0) {
		static const float cy[4] = { 0.30f, 0.82f, 0.86f, 1 };
		for (int r = 0; r < 3; r++) { float r0 = 22 + r * 13, a0 = 30 + r * 70 + t * 40 * (r & 1 ? -1 : 1); wr_col(k, cy, ea * (0.9f - r * 0.2f));
			mu_arc(480, 262, r0, r0 + 6, a0, a0 + 250, k, k); }
		wr_col(k, cy, ea); mu_disc(480, 262, 9, k); float g[4] = { 0.5f, 1, 1, 0.25f * ea }; mu_soft(480, 262, 70, g);
	}
	// the lines rushing out (0.7 - 2.3 s): 72 rays, each a dash moving out along it, faster as the dive goes on
	if (t > 0.7f) {
		float u = (t - 0.7f) / 1.6f, sp = 0.6f + 3.5f * u * u;
		for (int i = 0; i < 72; i++) {
			float ang = i * 0.0872665f + 0.37f * (i % 5), ph = (float)((i * 37) % 100) / 100.0f, d = fmodf(ph + (t - 0.7f) * sp, 1.0f);
			float r0 = 20 + d * d * 620, r1 = r0 + 30 + d * 260 * (0.4f + u), ca = cosf(ang), sa = sinf(ang), a = fminf(1, d * 3) * fminf(1, u * 3) * (i % 3 ? 0.55f : 0.9f);
			float c[4] = { 0.45f + 0.4f * (i % 2), 0.92f, 0.95f, a };
			mu_line(480 + ca * r0, 272 + sa * r0, 480 + ca * r1, 272 + sa * r1, i % 4 ? 1.5f : 2.5f, c);
		}
	}
	// the burst and the flash (2.0 - 2.6 s)
	if (t > 2.0f) { float b = (t - 2.0f) / 0.6f; float g[4] = { 0.7f, 1, 1, fminf(1, b * 1.6f) }; mu_soft(480, 272, 80 + b * 900, g);
		float w[4] = { 1, 1, 1, b > 0.6f ? (b - 0.6f) / 0.4f : 0 }; mu_rect(0, 0, 960, 544, w); }
	float ha = t < WR_DIVE_LEN - 0.4f ? fminf(1, t * 3) : 0; mu_a = ha; mu_hints("x:SKIP DIVE", MU_HINT_Y); mu_a = 1;
}
// the network view's screen layer: falling light streaks and a darker edge (EP26 1:38-2:06)
static void wr_draw_streaks(float now, float a) {
	for (int i = 0; i < 26; i++) {
		float x = fmodf(i * 389.0f + 57, 960), sp = 160 + (i * 53) % 220, len = 30 + (i * 71) % 110, y = fmodf(now * sp + i * 97.0f, 544 + len) - len;
		float c[4] = { 0.40f, 0.95f, 0.92f, a * (i % 3 ? 0.10f : 0.22f) };
		mu_rect(x, y, x + (i % 4 ? 1.5f : 2.5f), y + len, c);
	}
	float e0[4] = { 0, 0.02f, 0.03f, 0.55f * a }, e1[4] = { 0, 0.02f, 0.03f, 0 };
	mu_rectv(0, 0, 960, 70, e0, e1); mu_rectv(0, 474, 960, 544, e1, e0);
}
static void wr_draw_room(float now) {
	float t = WR.t, in = mu_ease(t / 0.35f), c[4], c2[4];
	// the band and its title, the tip under it (it slides down as the room appears)
	float dy = -40 * (1 - in), by0 = 58 + dy;
	const char *tip = wr_tip(WR.tip); int tl = mu_lines(tip, 860, MU_FINE); if (tl > 2) tl = 2;
	float by1 = by0 + 34 + tl * 22 + 8;
	{ float b0[4] = { 0.0f, 0.03f, 0.04f, 0.72f * in }, b1[4] = { 0.0f, 0.03f, 0.04f, 0.0f };
	  mu_recth(0, by0, 120, by1, b1, b0); mu_rect(120, by0, 840, by1, b0); mu_recth(840, by0, 960, by1, b0, b1);
	  float ln[4] = { 0.35f, 0.75f, 0.80f, 0.35f * in }; mu_rect(120, by0, 840, by0 + 1, ln); mu_rect(120, by1 - 1, 840, by1, ln); }
	wr_col(c, WR_YEL, in); mu_text(480, by0 + 27, MU_TEXT, 2, c, "Waiting for players...");
	{ float ta = in * fminf(1, WR.tip_t / 0.3f), tw = fminf(860, mu_twg(tip, MU_FINE)); wr_col(c, MU_WHITE, ta); mu_wrapg(480 - tw / 2, by0 + 34, 860, MU_FINE, 22, 2, c, tip); }   // (centred block; {glyph} tokens)
	// the countdown (a pulse each second, yellow from 3)
	{ float left = WR_LEN - t; int n = left > 0 ? (int)ceilf(left) : 0; float fr = n - left, s = 1 + 0.25f * fmaxf(0, 1 - fr / 0.25f);
	  char b[8]; snprintf(b, sizeof(b), "%d", n); wr_col(c, n <= 3 ? WR_YEL : MU_WHITE, in); mu_text(480, by1 + 42, MU_H2 * s, 2, c, b); }
	// DAILY MISSIONS (left)
	{ float x = 14 - 30 * (1 - in), y = 168; wr_col(c, MU_CYAN, in); mu_rect(x, y - 15, x + 12, y - 3, c); mu_text(x + 18, y, MU_FINE, 0, c, "DAILY MISSIONS");
	  for (int k = 0; k < 3; k++) { float yy = y + 26 + k * 46; wr_col(c, MU_WHITE, in); mu_text(x + 6, yy, MU_FINE, 0, c, WR.mis[k]);
		char b[16]; snprintf(b, sizeof(b), "0 / %d", WR.mis_need[k]); wr_col(c2, MU_DIM, in); mu_text(x + 22, yy + 21, MU_FINE, 0, c2, b);
		wr_col(c2, WR_YEL, in); mu_rect(x, yy - 9, x + 4, yy - 5, c2); } }
	// the chat log (under the missions; the newest at the bottom, each fading after 12 s)
	for (int k = 0; k < WR.nchat; k++) { float age = t - WR.chat_t[k], a = in * fminf(1, (age + 0.01f) / 0.25f) * (age > 12 ? fmaxf(0, 1 - (age - 12) / 1.5f) : 1);
		if (a <= 0) continue; wr_col(c, k == WR.nchat - 1 && age < 0.6f ? WR_YEL : MU_WHITE, a); mu_textbox(14, 320 + k * 22, 336, 342 + k * 22, MU_FINE, 0, 0, c, WR.chat[k]); }
	// the own team's cards (bottom centre): name, portrait, a bar (yours yellow); a card flips in when its player connects
	int idx[2][6], n[2] = { 0, 0 };
	for (int i = 0; i < WR.np; i++) { int tm = WR.p[i].team ? 1 : 0; if (n[tm] < 6) idx[tm][n[tm]++] = i; }
	{ int nc = n[0] > 4 ? 4 : n[0]; float cw = 92, gap = 10, x0 = WR_CX - (nc * cw + (nc - 1) * gap) / 2, y0 = 268, y1 = 372;
	  for (int k = 0; k < nc; k++) { const WrPlayer *P = &WR.p[idx[0][k]]; float x = x0 + k * (cw + gap), ct = t - P->t_in, on = ct >= 0, fl = on ? mu_ease(ct / 0.25f) : 0;
		float bg[4] = { 0.03f, 0.10f, 0.12f, 0.82f * in }; mu_rect(x, y0, x + cw, y1, bg);
		if (!on) { wr_col(c, MU_DIM, in * 0.8f); for (int d = 0; d < 3; d++) { float ph = fmodf(now * 2.5f - d * 0.25f, 1.0f); float dc[4] = { c[0], c[1], c[2], c[3] * (ph < 0.5f ? 1 : 0.35f) }; mu_disc(x + cw / 2 - 12 + d * 12, (y0 + y1) / 2, 3.5f, dc); }
			float fr[4] = { 0.30f, 0.55f, 0.60f, 0.6f * in }; mu_frame(x, y0, x + cw, y1, 1.5f, fr); continue; }
		float hw = cw / 2 * fl, cx = x + cw / 2;   // the flip: the card opens from its middle
		if (P->chr >= 0 && P->chr < 12) { char fb[20]; snprintf(fb, sizeof(fb), "cm.face.%d", MENU_CHR_FACE[P->chr]); float m[4] = { 1, 1, 1, in }; mu_imgcover(MI(fb), cx - hw, y0 + 24, cx + hw, y1 - 22, m); }
		float nb[4] = { 0.02f, 0.05f, 0.06f, 0.85f * in }; mu_rect(cx - hw, y0, cx + hw, y0 + 24, nb);
		if (fl > 0.9f) { wr_col(c, MU_WHITE, in); mu_textbox(x + 3, y0, x + cw - 3, y0 + 24, MU_FINE, 2, 0, c, P->name); }
		const float *bc = P->me ? WR_YEL : MU_CYAND; float bb[4]; wr_col(bb, bc, in); mu_rect(cx - hw, y1 - 22, cx + hw, y1, bb);
		if (fl > 0.9f) { wr_col(c, P->me ? MU_BLACK : MU_WHITE, in); mu_text(cx, y1 - 5, MU_FINE, 2, c, P->me ? "YOU" : "READY"); }
		wr_col(c, P->me ? WR_YEL : MU_CYAN, in * (P->me ? 1 : 0.7f)); mu_frame(cx - hw, y0, cx + hw, y1, P->me ? 2.5f : 1.5f, c);
		if (ct < 0.45f) { float w[4] = { 1, 1, 1, (1 - ct / 0.45f) * 0.8f * in }; mu_rect(cx - hw, y0, cx + hw, y1, w); } } }   // the connect flash
	// the lists (yours left in blue, the enemy right in red; a red VS): the row writes the name in, READY pops after it
	for (int tm = 0; tm < 2; tm++) {
		float x0 = tm ? WR_CX + 16 : WR_CX - 252, x1 = x0 + 236, y = 392, a = in;
		const float *hc = tm ? WR_REDT : WR_BLUE; float hb[4] = { hc[0] * 0.45f, hc[1] * 0.45f, hc[2] * 0.45f, 0.85f * a }, hb2[4] = { hb[0], hb[1], hb[2], 0 };
		mu_recth(x0, y, x1, y + 26, hb, hb2); wr_col(c, hc, a); mu_rect(x0, y, x0 + 3, y + 26, c);
		wr_col(c, MU_WHITE, a); mu_text(x0 + 10, y + 20, MU_FINE, 0, c, tm ? "GHOST B" : "GHOST A");
		int rows = n[0] > n[1] ? n[0] : n[1]; if (rows > 4) rows = 4; if (rows < 1) rows = 1;
		for (int r = 0; r < rows; r++) {
			float ry = y + 30 + r * 24; float sl[4] = { 0.5f, 0.75f, 0.78f, 0.18f * a }; mu_rect(x0, ry + 22, x1, ry + 23, sl);
			if (r >= n[tm]) continue; const WrPlayer *P = &WR.p[idx[tm][r]]; float ct = t - P->t_in;
			if (ct < 0) { wr_col(c, MU_OFF, a * 0.8f); mu_text(x0 + 10, ry + 18, MU_FINE, 0, c, "- - - - -"); continue; }
			if (P->me) { float mb[4] = { 0.20f, 0.46f, 0.66f, 0.70f * a }; mu_rect(x0, ry, x1, ry + 22, mb); }
			if (ct < 0.4f) { float w[4] = { 1, 1, 1, (1 - ct / 0.4f) * 0.55f * a }; mu_rect(x0, ry, x0 + (x1 - x0) * fminf(1, ct / 0.2f), ry + 22, w); }   // the write-in flash
			{ char nm[24]; int len = (int)strlen(P->name), sh = (int)(len * fminf(1, ct / 0.25f)); snprintf(nm, sizeof(nm), "%.*s", sh, P->name); wr_col(c, MU_WHITE, a); mu_text(x0 + 10, ry + 18, MU_FINE, 0, c, nm); }
			float rt = ct - 0.35f; if (rt > 0) { float s = 1 + 0.4f * fmaxf(0, 1 - rt / 0.2f); wr_col(c, WR_YEL, a * fminf(1, rt / 0.1f)); mu_text(x1 - 8, ry + 18, MU_FINE * s, 1, c, "READY"); }
		}
	}
	wr_col(c, WR_REDT, in); mu_text(WR_CX, 470, MU_TEXT, 2, c, "VS");
}
static void wr_draw_out(void) {   // white pixel blocks over the switch back to the world (2:06)
	float u = WR.pt / WR_OUT_LEN, a = u < 0.4f ? u / 0.4f : 1 - (u - 0.4f) / 0.6f; unsigned s = 0x9e3779b9u + (unsigned)(WR.pt * 30);
	for (int i = 0; i < 70; i++) { s = s * 1664525u + 1013904223u; float x = (s >> 8) % 960, y = (s >> 12) % 544, w = 16 + (s >> 3) % 90, h = 6 + (s >> 17) % 40;
		float c[4] = { i % 3 ? 1.0f : 0.6f, 1.0f, 1.0f, a * (i % 2 ? 0.85f : 0.5f) }; mu_rect(x, y, x + w, y + h, c); }
	float w[4] = { 0.85f, 1, 1, a * 0.35f }; mu_rect(0, 0, 960, 544, w);
}
static void wr_draw(float now) {
	if (WR.phase == WR_OFF || menu_ovl < 0) return;
	if (WR.phase == WR_DIVE && WRD.ok) {   // b102: the game's scenes, then the hint over them
		if (WR.pt < 4.27f) { float k[4] = { 0, 0, 0, WR.pt < 4.07f ? 1 : 1 - (WR.pt - 4.07f) / 0.2f }; menu_begin(menu_ovl); mu_rect(0, 0, 960, 544, k); menu_end(); }   // (the scene's own black backdrop, Plane01, once more as a plain rectangle: nothing of the level before it clears)
		if (wr_dbg_cycle) { if (wr_dbg_w0 < 0) wr_dbg_w0 = now; int q_ = (int)((now - wr_dbg_w0) / 1.5f) % WRD.a.nm; fxs_dbg_log = q_ != fxs_dbg_lo; fxs_dbg_lo = q_; fxs_dbg_hi = q_ + 1; }
		fxs_draw(&WRD.a, WR.pt, NULL, 1); if (WR.pt >= WR_DIVE2_END) fxs_draw(&WRD.c, WR.pt - WR_DIVE2_END, NULL, 1);
		menu_begin(menu_ovl); mu_a = WR.pt < WR_DIVE2_LEN - 0.4f ? fminf(1, WR.pt * 3) : 0; mu_hints("x:SKIP DIVE", MU_HINT_Y); mu_a = 1;
		if (wr_dbg_t0 >= 0) { char tb[40]; const float w[4] = { 1, 1, 0.4f, 1 }; snprintf(tb, sizeof(tb), "T %.3f M %d", WR.pt, fxs_dbg_lo); mu_text(480, 40, 24, 2, w, tb); }
		menu_end(); return; }
	menu_begin(menu_ovl);
	if (WR.phase == WR_DIVE) wr_draw_dive();
	else if (WR.phase == WR_WAIT && WRD.end_t > 0) { wr_draw_streaks(now, mu_ease(WR.t / 0.35f)); wr_draw_room(now); menu_end(); fxs_draw(&WRD.c, WRD.end_t, NULL, 1); return; }   // (b102: the end flare and its rings run on over the room)
	else if (WR.phase == WR_WAIT) { wr_draw_streaks(now, mu_ease(WR.t / 0.35f)); wr_draw_room(now); if (WR.t < 0.35f) { float w[4] = { 1, 1, 1, 1 - WR.t / 0.35f }; mu_rect(0, 0, 960, 544, w); } }   // (the dive's flash fading out over the room)
	else wr_draw_out();
	menu_end();
}
static V3 wr_sp; static float wr_yaw;   // main.c: the spawn the player returns to when the room ends
