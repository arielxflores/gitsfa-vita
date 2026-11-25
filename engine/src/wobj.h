// b87 (user: "the train in the train station map moves and is animated so please include the train moving and being animated
// in the map for the vita version as well alongside any and every visual and smoke effect"): the maps' animated world objects
// (the original's WorldObjFx.txt + world\WorldObjFx\<obj>.nif; tools/wobj/make_wobj.py writes wobj_<map>.txt and the models).
//
// Train Station: the freight train (Train_ani.nif, 365 m) waits in the tunnel north of the station, eases straight and runs
// through at ~125 m/s (5.7-12.6 s of its 21.7 s animation), then waits off the map; BEGIN 0 / END 21 / RESET 20 = one pass every
// 41 s, as the user's video (passes at 7:23 and 8:03). Its timeline: the bell (start01, 0.1 s), the station rumble (train01),
// the head / tail passing sounds (shock01 / shock20, 4.0 / 3.7 s: played where the train passes nearest the listener - a moving
// 3D voice is not available here), and the dust it throws up in the pit (EFFECT_train_smoke at smoke01-04, 6.3-11.5 s, and
// EFFECT_train_smoke_end at smoke05-06; here the game's dust impact scaled up, a few puffs over ~1.5 s, with the smoke sound).
// INTER_DUMMY shock01-20 along the train: anyone in the pit in its way is killed (the original's "shock" dummies).
// The timed train sounds of the map's ambience (events.gev AMB_TIMED: every 25 s, at a guessed distance) are muted while the
// train runs, so what is heard goes with what is seen (audio.h amb_timed_mute).
// Drawn with the models' program (rigid, one bone in map space), lit by the map's light where it passes (light.h light_at),
// frustum-tested by its 365 m box (moving) and skipped while it waits off the map.
// Test switch (autoplay): ux0:data/gitsfa/trainview - on Train Station the camera stands on the platform edge looking up the
// track, and the train's clock starts 3 s before it runs (main.c).
#pragma once
#define WO_MAXD 40
#define WO_MAXS 8
#define WO_MAXF 8
typedef struct { char name[16]; int bone; float p[3]; } WoDummy;
typedef struct { int amb; char ev[64]; int dummy; float at; int done; } WoSound;
typedef struct { int end; int dummy; float at; char ev[64]; int done, puffs; float next; } WoFx;
// b105 (user: "on the urban map the helicopter sitting outside the map seems frozen ... like it's supposed to be animated"):
// a map can have several objects (WO_MAXO). Urban's second one is the police helicopter parked behind the barrier east of
// the square - part of the map's effect scene (effect\map\urban.nif, node 'heli'), which tools/world_fx.py bakes into the
// map as it stands at t = 0. On the PC its two rotor discs turn (one turn in 3 s, looping: an idling machine; the disc's
// texture shows six blurred blades, so the turning is seen). tools/wobj/make_fxobj.py writes the two discs as
// wobj_urban_heli.gvm (2 bones, a 3 s loop: BEGIN 0, END 3, RESET 0) and a 'hide' line with the box of the still copies:
// the map's blended meshes inside that box are not drawn while the turning ones are there (world.h w_hide_box). Each
// object runs through the same code: WO is the one in hand (wo_p).
#define WO_MAXO 3
typedef struct {
	int ok; Model m; float begin, end, reset, len, t, period, stop; int nd; WoDummy d[WO_MAXD]; int ns; WoSound s[WO_MAXS]; int nf; WoFx f[WO_MAXF];
	MBone pose[4]; float lit[3]; int moving, shown, cyc; float mn[3], mx[3];   // pose: this frame's bones; mn / mx: the model's box in its own space
	int kills, drawn_frames; V3 ph; char gvm[64];   // ph: the head's place last frame (moving)
} WObj;
static WObj WOA[WO_MAXO]; static WObj *wo_p = &WOA[0]; static int wo_n;
#define WO (*wo_p)
static int wo_find(const char *n) { for (int i = 0; i < WO.nd; i++) if (!strcmp(WO.d[i].name, n)) return i; return -1; }
static void wo_free(void) { for (int i = 0; i < WO_MAXO; i++) if (WOA[i].ok) model_free(&WOA[i].m); memset(WOA, 0, sizeof(WOA)); wo_p = &WOA[0]; wo_n = 0; amb_timed_mute[0] = 0; w_hide_box(NULL); }
// a dummy's map position now
static V3 wo_dummy_in(int i, const MBone *pose) {
	if (i < 0 || i >= WO.nd) return v3(0, 0, 0); const WoDummy *D = &WO.d[i];
	if (D->bone < 0 || D->bone >= WO.m.nb) return v3(D->p[0], D->p[1], D->p[2]);
	const MBone *B = &pose[D->bone]; float x = B->q[0], y = B->q[1], z = B->q[2], w = B->q[3], px = D->p[0], py = D->p[1], pz = D->p[2];
	float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
	return v3(R[0] * px + R[1] * py + R[2] * pz + B->p[0], R[3] * px + R[4] * py + R[5] * pz + B->p[1], R[6] * px + R[7] * py + R[8] * pz + B->p[2]);
}
static V3 wo_dummy_pos(int i) { return wo_dummy_in(i, WO.pose); }
static void wo_pose_at(float ta, MBone *out);
// where a moving dummy comes nearest the listener between t0 and t1 (a passing sound is played there: a moving voice is not available)
static V3 wo_dummy_nearest(int i, float t0, float t1, V3 L) {
	MBone tmp[4]; V3 best = wo_dummy_pos(i); float bd = 1e30f;
	for (int k = 0; k <= 40; k++) { wo_pose_at(t0 + (t1 - t0) * k / 40.f, tmp); V3 p = wo_dummy_in(i, tmp), d = vsub(p, L); float dd = vdot(d, d); if (dd < bd) { bd = dd; best = p; } }
	return best;
}
static int wo_load(const char *dir, const char *stem) {
	wo_free(); char p[160]; snprintf(p, sizeof(p), "%swobj_%s.txt", dir, stem); FILE *f = fopen(p, "rb"); if (!f) return 0;
	char ln[200]; int cur = -1; float hb[6]; int hide = 0;
	while (fgets(ln, sizeof(ln), f)) {
		char a[64], b[64], c[64], gvm[64]; float x, y, z, w; int k;
		if (ln[0] == '#') continue;
		float u, v, q;
		if (sscanf(ln, "obj %63s %f %f %f %f", gvm, &x, &y, &z, &w) == 5) { if (cur + 1 >= WO_MAXO) break; cur++; wo_p = &WOA[cur]; snprintf(WO.gvm, sizeof(WO.gvm), "%s", gvm); WO.begin = x; WO.end = y; WO.reset = z; WO.len = w; }
		else if (cur < 0) continue;
		else if (sscanf(ln, "box %f %f %f %f %f %f", &x, &y, &z, &u, &v, &q) == 6) { WO.mn[0] = x; WO.mn[1] = y; WO.mn[2] = z; WO.mx[0] = u; WO.mx[1] = v; WO.mx[2] = q; }
		else if (sscanf(ln, "hide %f %f %f %f %f %f", &hb[0], &hb[1], &hb[2], &hb[3], &hb[4], &hb[5]) == 6) hide = 1 + cur;
		else if (sscanf(ln, "dummy %15s %d %f %f %f", a, &k, &x, &y, &z) == 5 && WO.nd < WO_MAXD) { WoDummy *D = &WO.d[WO.nd++]; snprintf(D->name, 16, "%.15s", a); D->bone = k; D->p[0] = x; D->p[1] = y; D->p[2] = z; }
		else if (sscanf(ln, "sound %63s %63s %63s %f", a, b, c, &x) == 4 && WO.ns < WO_MAXS) { WoSound *S = &WO.s[WO.ns++]; S->amb = !strcmp(a, "amb"); snprintf(S->ev, 64, "%s", b); S->dummy = wo_find(c); S->at = x; }
		else if (sscanf(ln, "effect %63s %63s %d %f %f %63s", a, b, &k, &x, &y, c) == 6 && WO.nf < WO_MAXF) { WoFx *F = &WO.f[WO.nf++]; F->end = strstr(a, "_end") != NULL; F->dummy = wo_find(b); F->at = x; snprintf(F->ev, 64, "%s", strcmp(c, "-") ? c : ""); }
	}
	fclose(f);
	for (int i = 0; i <= cur; i++) {
		wo_p = &WOA[i]; snprintf(p, sizeof(p), "%s%s", dir, WO.gvm);
		if (model_load(&WO.m, p) < 0 || !WO.m.ok || WO.m.nclip < 1 || WO.m.nb < 1 || WO.m.nb > 4) { logp("wobj: %s failed\n", p); if (WO.m.ok) model_free(&WO.m); memset(&WO, 0, sizeof(WO)); continue; }
		WO.ok = 1; WO.period = WO.end + WO.reset; if (WO.period < 1) WO.period = WO.len + 1;
		if (WO.mn[0] >= WO.mx[0]) { WO.mn[0] = WO.mn[2] = -800; WO.mx[0] = WO.mx[2] = 800; WO.mn[1] = -1000; WO.mx[1] = 40000; }   // (no box line: a generous one)
		for (int c = 0; c < 3; c++) WO.lit[c] = 1;
		// b105: when the object has come to rest for good (the last frame that still moves a bone): a fly-by is not drawn hanging
		// in the sky at the end of its path (Urban's helicopter stands still from 11.2 s to its END at 15 s, 180 m out and
		// 100 m up - out of sight behind the old 120 m far plane unless it was near the edge of the view; with the whole
		// skyline drawn it would hang there, motionless, for 4 s of every 45). A looping object (period = its clip) never rests.
		{ const MClip *C = &WO.m.clip[0]; int last = 0; const float *E = C->data + (size_t)(C->nf - 1) * WO.m.nb * 7;
		  for (int fr = 0; fr < C->nf - 1; fr++) { const float *A = C->data + (size_t)fr * WO.m.nb * 7; float d = 0; for (int b_ = 0; b_ < WO.m.nb; b_++) for (int k = 4; k < 7; k++) d += fabsf(A[b_ * 7 + k] - E[b_ * 7 + k]); if (d > 1.0f) last = fr; }
		  WO.stop = WO.reset > 0.5f && WO.m.nb <= 4 && wo_find("shock20") < 0 && last + 2 < C->nf - 1 ? (last + 2) / (C->fps > 0 ? C->fps : 30.f) + 0.25f : 1e9f; }
		// the map's own timed sounds of this object are muted: this module plays them in time with the train
		for (int k = 0; k < WO.ns; k++) {
			if (strstr(WO.s[k].ev, "/train_")) { snprintf(amb_timed_mute, sizeof(amb_timed_mute), "object/train_"); break; }
			if (strstr(WO.s[k].ev, "helicopter")) { snprintf(amb_timed_mute, sizeof(amb_timed_mute), "helicopter"); break; } }
		logp("wobj: %s, %d dummies, %d sounds, %d effects, %.1f s of %.1f s cycle, box %.0f..%.0f%s\n", WO.gvm, WO.nd, WO.ns, WO.nf, WO.len, WO.period, WO.mn[1], WO.mx[1], WO.stop < 1e8f ? ", hidden once it has stopped" : "");
		if (hide == 1 + i) { int n = w_hide_box(hb); logp("wobj: %d still mesh(es) of the map left out (its turning copy is drawn: %s)\n", n, WO.gvm); }
		if (i + 1 > wo_n) wo_n = i + 1;
	}
	wo_p = &WOA[0]; return wo_n > 0 ? 1 : -1;
}
static int wo_trainview, wo_view;   // test switches (main.c): trainview, wobjview
// the bone poses at animation time ta (the model's one clip, 30 fps, lerp + nlerp)
static void wo_pose_at(float ta, MBone *out) {
	const MClip *C = &WO.m.clip[0]; float fr = ta * C->fps; if (fr < 0) fr = 0; if (fr > C->nf - 1) fr = (float)(C->nf - 1);
	int f0 = (int)fr, f1 = f0 + 1 < C->nf ? f0 + 1 : f0; float u = fr - f0;
	for (int b = 0; b < WO.m.nb; b++) {
		const float *a = C->data + (f0 * WO.m.nb + b) * 7, *c = C->data + (f1 * WO.m.nb + b) * 7; float s = a[0] * c[0] + a[1] * c[1] + a[2] * c[2] + a[3] * c[3] < 0 ? -1.f : 1.f, l = 0;
		for (int k = 0; k < 4; k++) { out[b].q[k] = a[k] + (s * c[k] - a[k]) * u; l += out[b].q[k] * out[b].q[k]; }
		l = 1.f / sqrtf(l > 1e-12f ? l : 1); for (int k = 0; k < 4; k++) out[b].q[k] *= l;
		for (int k = 0; k < 3; k++) out[b].p[k] = a[4 + k] + (c[4 + k] - a[4 + k]) * u;
	}
}
static void wo_pose(float ta) { wo_pose_at(ta, WO.pose); }
// the point of the segment a-b nearest q
static V3 wo_nearest(V3 a, V3 b, V3 q) { V3 d = vsub(b, a); float L = vdot(d, d), t = L > 1 ? vdot(vsub(q, a), d) / L : 0; t = t < 0 ? 0 : t > 1 ? 1 : t; return vadd(a, vmul(d, t)); }

static void wo_update1(float dt, const float *lis) {
	if (!WO.ok) return;
	float prev = WO.t; WO.t += dt; float ta = fmodf(WO.t, WO.period), tp = fmodf(prev, WO.period);
	if (wo_view) { float lo = WO.begin + 1, hi = fminf(WO.end, WO.len) - 1; if (hi > lo && (ta < lo || ta > hi)) { WO.t = floorf(WO.t / WO.period) * WO.period + lo; ta = lo; } }   // (test switch wobjview: always out)
	int cyc = (int)(WO.t / WO.period); if (cyc != WO.cyc) { WO.cyc = cyc; for (int i = 0; i < WO.ns; i++) WO.s[i].done = 0; for (int i = 0; i < WO.nf; i++) { WO.f[i].done = 0; WO.f[i].puffs = 0; } }
	(void)tp;
	float tc = ta < WO.len ? ta : WO.len; wo_pose(tc);
	if (wo_view) { static float lt; if ((lt += dt) > 3) { lt = 0; logp("wobjview: t %.1f, bone 0 at %.0f %.0f %.0f, listener %.0f %.0f %.0f, drawn %d frames\n", ta, WO.pose[0].p[0], WO.pose[0].p[1], WO.pose[0].p[2], lis[0], lis[1], lis[2], WO.drawn_frames); } }
	int sh01 = wo_find("shock01"), sh20 = wo_find("shock20");
	V3 head = wo_dummy_pos(sh01), tail = wo_dummy_pos(sh20), L = v3(lis[0], lis[1], lis[2]);
	WO.shown = ta >= WO.begin && ta < WO.end && ta < WO.stop;
	{ V3 dv = vsub(head, WO.ph); WO.moving = vdot(dv, dv) > (200.f * dt) * (200.f * dt) && WO.shown; WO.ph = head; }
	// light where the train passes nearest the listener (the map's light); a flying object (the helicopter): daylight
	if (sh20 >= 0) { V3 q = wo_nearest(head, tail, L); float Lc[3]; light_at(q.x, q.y, q.z + 150, Lc); float k = fminf(1, 3 * dt); for (int c = 0; c < 3; c++) WO.lit[c] += (Lc[c] - WO.lit[c]) * k; }
	else for (int c = 0; c < 3; c++) WO.lit[c] = 1.0f;
	// sounds
	for (int i = 0; i < WO.ns; i++) { WoSound *S = &WO.s[i]; if (S->done || ta < S->at) continue; S->done = 1;
		V3 p = S->dummy >= 0 ? wo_dummy_pos(S->dummy) : wo_nearest(head, tail, L);
		if (S->dummy >= 0 && WO.d[S->dummy].bone >= 0) p = wo_dummy_nearest(S->dummy, ta, fminf(WO.end, WO.len), L);   // a moving one: where it passes the listener
		ev_play(S->ev, &p.x); }
	// dust in the pit: the game's dust impact, scaled up, a few puffs along the track at each dummy
	for (int i = 0; i < WO.nf; i++) { WoFx *F = &WO.f[i]; if (F->dummy < 0 || ta < F->at || F->puffs >= (F->end ? 3 : 5)) continue;
		if (!F->done) { F->done = 1; F->next = ta; if (F->ev[0]) { V3 p = wo_dummy_pos(F->dummy); ev_play(F->ev, &p.x); } }
		if (ta < F->next) continue; F->next = ta + 0.32f; F->puffs++;
		V3 p = wo_dummy_pos(F->dummy); float r1 = (float)(rand() % 1000) / 1000.f - 0.5f, r2 = (float)(rand() % 1000) / 1000.f - 0.5f;
		p.x += r1 * 260; p.y += r2 * 900; p.z += 30; float up[3] = { 0, 0, 1 };
		fx_scale = F->end ? 2.6f : 3.4f; fx_impact_nodecal(3, &p.x, up, NULL); fx_scale = 1; }
	// the shock dummies: anyone in the pit in the train's way
	if (WO.moving && sh01 >= 0 && sh20 >= 0) {
		int na = SKW_.n_actors ? SKW_.n_actors() : 0;
		for (int a = 0; a < na; a++) { if (!sk_alive(a)) continue; V3 p = sk_pos(a);
			V3 q = wo_nearest(head, tail, p); float dx = p.x - q.x, dy = p.y - q.y; if (dx * dx + dy * dy > 230.f * 230.f || p.z > q.z + 120 || p.z < q.z - 450) continue;
			if (sk_damage(a, 9999, -1, 0)) WO.kills++; if (a == 0) WO.kills++; }
	}
}
static void wo_update(float dt, const float *lis) { for (int i = 0; i < wo_n; i++) { wo_p = &WOA[i]; wo_update1(dt, lis); } wo_p = &WOA[0]; }
static void wo_draw1(const float *VP, const float *fog) {
	if (!WO.ok || !WO.shown) return;
	// frustum: the 8 corners of the box, moved by bone 0
	{ const MBone *B = &WO.pose[0]; float x = B->q[0], y = B->q[1], z = B->q[2], w = B->q[3];
	  float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
	  int out[6] = { 1, 1, 1, 1, 1, 1 };
	  for (int c = 0; c < 8; c++) { float l[3] = { c & 1 ? WO.mx[0] : WO.mn[0], c & 2 ? WO.mx[1] : WO.mn[1], c & 4 ? WO.mx[2] : WO.mn[2] }, wp[3], q[4];
		for (int r = 0; r < 3; r++) wp[r] = R[r * 3] * l[0] + R[r * 3 + 1] * l[1] + R[r * 3 + 2] * l[2] + B->p[r];
		for (int r = 0; r < 4; r++) q[r] = VP[r] * wp[0] + VP[4 + r] * wp[1] + VP[8 + r] * wp[2] + VP[12 + r];
		if (q[0] > -q[3]) out[0] = 0; if (q[0] < q[3]) out[1] = 0; if (q[1] > -q[3]) out[2] = 0; if (q[1] < q[3]) out[3] = 0; if (q[3] > 0) out[4] = 0; if (q[2] < q[3]) out[5] = 0; }
	  for (int k = 0; k < 6; k++) if (out[k]) return; }
	const float ld[4] = { 0.33f, 0.42f, 0.84f, 0 }; float tint[4] = { 1, 1, 1, 1 };
	for (int c = 0; c < 3; c++) tint[c] = fminf(1.4f, fmaxf(0.25f, WO.lit[c] * 1.1f));
	glDisable(GL_CULL_FACE); md_rim = 0;
	model_draw(&WO.m, VP, WO.pose, ld, tint, fog);
	glEnable(GL_CULL_FACE); WO.drawn_frames++;
}
static void wo_draw(const float *VP, const float *fog) { for (int i = 0; i < wo_n; i++) { wo_p = &WOA[i]; wo_draw1(VP, fog); } wo_p = &WOA[0]; }
