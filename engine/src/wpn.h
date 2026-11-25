// First-person weapon presentation and the extra weapon actions, on top of weapon.h (table + loadout slots):
//  - wvm_draw: the viewmodel with the sight blend modes (vm/make_vm_weapon.py): 0 opaque, 1 alpha (lens vignettes,
//    reticles), 2 additive (red dots, glows), 3 opaque in hip view only (lens caps / the inner tube of a scope: hidden
//    when aiming so the world shows through the glass)
//  - the L118A scope: the scoped view is one premultiplied screen texture baked from the game's RIFLESCOPE model
//    (vm/make_scope_overlay.py, inside vm_l118.gvm, attachment 'scope' = texture, half size, version); scoping is
//    instant as in the original, a shot drops the scope for the bolt, the scope comes back while L is held
// Needs model.h, weapon.h and hud.h (hud_poly / HG from hud_game.h) before it.
#pragma once
#include <math.h>
#include <string.h>

#include "wpn_vm.h"     // wvm_draw: the viewmodel draw (host-tested: tools/weapons/vmdraw_host.c)
#include "wpn_pose.h"   // wpn_vm_pose: the viewmodel pose kept in step with the aim amount (host-tested: tools/weapons/pose_host.c)

// ---- sniper scope
// Footage (Tutorial Stuff.mp4 254.8-268 s, 60 fps): the scope comes up in one frame (no raise, no zoom-in), the view
// is 8x the hip view (dummy 15 -> 123 px), the eyecup ring is fixed on screen while the view drifts slightly; a shot
// shows one frame of muzzle flash in the glass, then the gun is down at once (hip fire clip + bolt), and the scope
// comes back after the bolt while the button is held. HUD: no crosshair, the ammo counter under the ring.
#define SCOPE_FOVY 8.07f   // world vertical fov when scoped: 8x the 59 deg hip view
// Armory (Early Access tutorial 1080 s): L118A-S1B sight "2x SCOPE": ZOOM x8.0 (x12.0), AIM SPEED 0.25 s, VARIABLE ZOOM on
// ([Right-click] brings the scope up and cycles the zoom levels). Footage 261.2 s: the gun does not move for 0.25 s, then
// the scope is there in one frame. Vita: L held = scope after 0.25 s; D-pad down while scoped = x8 <-> x12.
#define SCOPE_AIM_T 0.25f
typedef struct { int on, zoom, live; float flash, t, hold; } WScope;   // live: > 0 while the scope weapon is the one being updated
static WScope wscope;
// the overlay texture of a loadout slot (vm_l118.gvm 'scope' attachment version 2), or -1
static int wpn_scope_tex(const WSlot *w, float *half) {
	if (!w->ok || !w->scope || w->scope[2] < 1.5f) return -1;
	int ti = (int)(w->scope[0] + 0.5f); if (ti < 0 || ti >= w->m.ntex) return -1;
	if (half) *half = w->scope[1] > 0.1f ? w->scope[1] : 0.556f;
	return ti;
}
// D-pad down belongs to the scope (x8 / x12) while it is up or coming up with L held, not to the sprint toggle
static int wpn_scope_dpad(int l_held) { int r = l_held && wscope.live > 0 && (wscope.on || wscope.hold > 0); if (wscope.live > 0) wscope.live--; return r; }
// world vertical fov when aiming with weapon D (the variable zoom of the sniper scope: x12 = 2 atan(tan(29.5 deg) / 12))
static float wpn_ads_fov(const WDef *D) { return D->sniper && wscope.zoom ? 5.38f : D->ads_fov; }
// scope weapons: the aim target only after the aim time (the view does not move meanwhile); returns the target 0/1
// (aim: L held; can: nothing stops aiming now - the bolt, a reload or a switch drop the scope, the zoom level stays while L is held)
static float wpn_scope_target(int aim, int can, float dt, int zoom_press) {
	wscope.live = 2;
	if (!aim) { wscope.hold = 0; wscope.zoom = 0; return 0; }
	if (!can) { wscope.hold = 0; return 0; }
	wscope.hold += dt; if (zoom_press && wscope.on) { wscope.zoom ^= 1; ev_play("weapon/sr/l118a/aim_on_02", NULL); }
	return wscope.hold >= SCOPE_AIM_T ? 1.f : 0.f;
}
// the slow drift of the scoped view (yaw, pitch offsets in radians), added to the aim while scoped
static void wpn_scope_sway(float t, float *dy, float *dp) {
	*dy = 0.0011f * sinf(t * 1.1f) + 0.0004f * sinf(t * 2.9f + 1.3f);
	*dp = 0.0009f * sinf(t * 1.7f + 0.6f) + 0.0003f * sinf(t * 3.7f);
}
// the scoped view: one textured quad around the screen centre (premultiplied, HUD blend), tinted by the local light;
// flash: muzzle flash in the glass for a frame after a scoped shot
// ---- b52 (user: "better quality scope effects ... specially sniper rifles"): on top of the baked scoped view
//  - scope shadow: the eye lags the scope when the view turns or the body moves, so a soft dark crescent creeps in from
//    the side of the lens (the clear circle shifts against the motion, up to 28 px, springs back when still);
//  - the glass: a faint chromatic fringe at the lens edge (cyan inside, warm outside) and a dim sheen arc at the top left
//    that drifts with the shadow. All drawn with the HUD's white texel, ~0.3 screens of extra fill, no shader.
// The baked overlay's glass reaches r ~ 207 px around the screen centre (its vignette 0.05 -> 0.63 alpha out to 186 px), the
// opaque tube begins at 214 px.
static float wsc_ox, wsc_oy;   // the clear circle's offset (px), smoothed
static void wpn_scope_motion(float dyaw, float dpitch, float vside, float vfwd, float speed, float t, float dt) {
	if (dt <= 0 || dt > 0.1f) return;
	float wy = dyaw / dt, wp = dpitch / dt, bob = speed > 30 ? sinf(t * 8.5f) * fminf(speed, 600) * 0.004f : 0;
	float tx = wy * 12.0f - vside * 0.012f, ty = wp * 12.0f + bob + vfwd * 0.002f, l = sqrtf(tx * tx + ty * ty);
	if (l > 28) { tx *= 28 / l; ty *= 28 / l; }
	float k = fminf(1, dt * 7); wsc_ox += (tx - wsc_ox) * k; wsc_oy += (ty - wsc_oy) * k;
}
static void wsc_band(const HImg *I, float cx0, float cy0, float r0, float cx1, float cy1, float r1, float a0, float a1, int seg, const float *col) {
	float wu = (I->x + I->w * 0.5f) / htw[I->tex], wv = (I->y + I->h * 0.5f) / hth[I->tex], uv[4][2] = { { wu, wv }, { wu, wv }, { wu, wv }, { wu, wv } };
	for (int i = 0; i < seg; i++) { float t0 = a0 + (a1 - a0) * i / seg, t1 = a0 + (a1 - a0) * (i + 1) / seg, c0 = cosf(t0), s0 = sinf(t0), c1 = cosf(t1), s1 = sinf(t1);
		float q[4][2] = { { cx0 + r0 * c0, cy0 + r0 * s0 }, { cx0 + r0 * c1, cy0 + r0 * s1 }, { cx1 + r1 * c1, cy1 + r1 * s1 }, { cx1 + r1 * c0, cy1 + r1 * s0 } };
		hud_poly(htex[I->tex], HG.screen, (const float (*)[2])q, (const float (*)[2])uv, 4, col); }
}
static void wpn_scope_glass(float l) {
	if (HG.white < 0) return; const HImg *I = &himg[HG.white]; const float TAU = 6.2831853f, RO = 214;   // RO: where the opaque tube begins
	float o = sqrtf(wsc_ox * wsc_ox + wsc_oy * wsc_oy);
	if (o > 0.4f) {   // the shadow: 6 layers of a clear circle (r 202) offset 0.5..1.5 x o, darkening the glass outside it up to the tube;
		// on the leading side the circle passes beyond the tube and the layer has no width there - the crescent is one-sided
		float wu = (I->x + I->w * 0.5f) / htw[I->tex], wv = (I->y + I->h * 0.5f) / hth[I->tex], uv[4][2] = { { wu, wv }, { wu, wv }, { wu, wv }, { wu, wv } };
		float sh[4] = { 0, 0, 0, 0.17f * fminf(1, o / 4) };
		for (int k = 0; k < 6; k++) { float f = 0.5f + 0.2f * k, px[49][2];
			for (int i = 0; i <= 48; i++) { float t = TAU * i / 48, x = wsc_ox * f + 202 * cosf(t), y = wsc_oy * f + 202 * sinf(t), r = sqrtf(x * x + y * y);
				if (r > RO) { x *= RO / r; y *= RO / r; } px[i][0] = 480 + x; px[i][1] = 272 + y; }
			for (int i = 0; i < 48; i++) { float t0 = TAU * i / 48, t1 = TAU * (i + 1) / 48;
				float q[4][2] = { { px[i][0], px[i][1] }, { px[i + 1][0], px[i + 1][1] }, { 480 + RO * cosf(t1), 272 + RO * sinf(t1) }, { 480 + RO * cosf(t0), 272 + RO * sinf(t0) } };
				hud_poly(htex[I->tex], HG.screen, (const float (*)[2])q, (const float (*)[2])uv, 4, sh); } } }
	{ float cy_[4] = { 0, 0.032f * l, 0.045f * l, 0 }, wm[4] = { 0.036f * l, 0.016f * l, 0, 0 };   // the fringe (additive: premultiplied, alpha 0)
	  wsc_band(I, 480, 272, 193, 480, 272, 199, 0, TAU, 48, cy_); wsc_band(I, 480, 272, 199, 480, 272, 205, 0, TAU, 48, wm); }
	{ float g = 0.047f * l, sc[4] = { g, g, g * 1.15f, 0 }, cx = 480 - wsc_ox * 0.3f, cy = 272 - wsc_oy * 0.3f;   // the sheen, top left
	  wsc_band(I, cx, cy, 140, cx, cy, 190, TAU * 0.56f, TAU * 0.70f, 12, sc); }
}
static void wpn_scope_draw(GLuint tex, float half, const float *lit, float flash) {
	if (HG.screen < 0 || !tex) return;
	float s = half * 544, x0 = 480 - s, x1 = 480 + s, y0 = 272 - s, y1 = 272 + s;
	if (flash > 0 && HG.white >= 0) {   // a hot white-yellow disc in the glass (drawn under the ring)
		const HImg *I = &himg[HG.white]; float wu = (I->x + I->w * 0.5f) / htw[I->tex], wv = (I->y + I->h * 0.5f) / hth[I->tex];
		float a = fminf(1, flash), col[4] = { 1, 0.92f, 0.72f, 0.85f * a }, xy[16][2], uv[16][2];
		for (int k = 0; k < 16; k++) { float t = k * 6.2831853f / 16; xy[k][0] = 480 + 200 * cosf(t); xy[k][1] = 300 + 160 * sinf(t); uv[k][0] = wu; uv[k][1] = wv; }
		hud_poly(htex[I->tex], HG.screen, (const float (*)[2])xy, (const float (*)[2])uv, 16, col);
	}
	float l = 1; if (lit) { l = (lit[0] + lit[1] + lit[2]) / 3 * 1.1f; l = fminf(1.25f, fmaxf(0.35f, l)); }
	const float m[4] = { l, l, l, 1 };
	float q[4][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } }, uq[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	hud_poly(tex, HG.screen, (const float (*)[2])q, (const float (*)[2])uq, 4, m);
	wpn_scope_glass(l);   // b52
}

// ---- small matrix helper (main.c's mul4 comes after the includes)
static void wpn_mul4(float *o, const float *a, const float *b) { float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; } memcpy(o, r, 64); }

// ---- knife: second slash and the stealth kill from behind
// Tutorial (Tutorial Stuff.mp4 412-440 s, "Melee": "Get close to the enemy without being spotted. [Right-click] when
// prompted to perform a stealth kill."): knife in hand, right-click = melee; close behind an unaware enemy it becomes
// the stealth kill (one hit kill, the arm grabs the shoulder and stabs); "Stealth kill failed" when the target faces
// you. Vita: with the knife out (hold D-pad left) L = second slash / stealth kill; a quick knife (tap D-pad left)
// behind an unaware enemy is a stealth kill as well. Clips (vm_knife.gvm): fire2 = Knife fire_right, cqc_ready / cqc =
// the 'saito_knife' cqc set (1.8 s, the stab lands at 0.55 s); the camera stays in first person here (the original cuts
// to a third-person view of the kill: needs the 3rd-person cqc / death_cqc clips in the characters, see the report).
#define WST_RANGE 170.f   // cm, horizontal
#define WST_KILL_T 0.55f
// b70 (the user: "make sure that the blade kill animations are included in the game like how they function in the ghost
// assault gameplay footage"): the cyborg's arm blade (vm_axe_cy.gvm) kills by execution, as IGN "9 Minutes of Ghost
// Assault" 251.5-254.25 s and Nick Bunyun "NEW GAME MODE GHOST ASSAULT" 257.75-260.25 s show: a slash wounds the ghost,
// the next blow that would kill it becomes the execution - the left hand grabs the head (the victim facing the cyborg,
// pulled in close, the view held on its face), the blade goes up under the chin, "NICE KILL +100" as the stab begins,
// blood, the body released ~1.5 s after the grab and it drops. From behind the same with the set's back clips.
// Clips (vm/make_vm_weapon.py axe_cy): cqc_ready (0.33 s) + cqc (1.83 s: the 'saito' pair - the left hand holds the head in
// front, the blade comes up under it ~0.6 s in) / cqcb_ready (0.33 s) + cqcb (0.4 s: the set's own). The victim (bots.h
// bot_grab_ex) is snapped in front of the player facing it (front) or away (back), held in its grabbed pose
// (Death_CQC_Ready_Saito / _Back_Ready_Bato) and dies with the matching death (kind 4 / 3) when the strike begins.
#define WEX_FRONT_D 40.f   // the victim's feet this far in front of the player's (front execution: its head where the left hand holds it, ~48 cm out)
#define WEX_BACK_D 40.f
#define WEX_STAB_F 0.62f   // the stab, seconds into cqc (front: the saito pair, the blade under the chin) ...
#define WEX_STAB_B 0.17f   // ... and into cqcb (back)
typedef struct { int on, victim, killed, from; float t, len; int axe, front, stage, stabbed; float rdy, stab; } WStealth;
static WStealth wst = { 0, -1, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
static float wst_fail_t, wst_prompt;   // "STEALTH KILL FAILED" message timer; the prompt (1 = a stealth kill is possible now)
static int wst_stab_ev;                // b70: set for one frame when the blade goes in (main.c: blood, the view's jolt)
// the bot that can be stealth-killed from where the player stands (-1 none); *front = a bot in reach that sees / faces the player
static int wpn_stealth_target(V3 pp, float pyaw, int *front) {
	int best = -1; float bd = 1e9f; if (front) *front = -1;
	for (int i = 0; i < nbot; i++) {
		const Bot *b = &bots[i]; if (b->state == BS_DEAD || b->team == 0) continue;
		float dx = b->pl.pos.x - pp.x, dy = b->pl.pos.y - pp.y, dz = b->pl.pos.z - pp.z, hd = sqrtf(dx * dx + dy * dy);
		if (hd > WST_RANGE || fabsf(dz) > 80 || hd < 1) continue;
		float a = atan2f(dy, dx) - pyaw; while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f;
		if (fabsf(a) > 0.8f) continue;   // the player has to face it
		float back = (cosf(b->pl.yaw) * dx + sinf(b->pl.yaw) * dy) / hd;   // > 0: it looks away from the player
		int aware = b->state == BS_ENGAGE && b->sees && b->tgt == -1;
		if (back > 0.45f && !aware) { if (hd < bd) { bd = hd; best = i; } }
		else if (front) *front = i;
	}
	return best;
}
// b70: can bot v be executed by the player standing at pp (feet) facing pyaw? -> 1 front, 2 back, 0 no
static int wpn_exec_check(int v, V3 pp, float pyaw) {
	if (v < 0 || v >= nbot) return 0; const Bot *b = &bots[v]; if (b->state == BS_DEAD || b->grabbed || b->netc || b->netp) return 0;
	float dx = b->pl.pos.x - pp.x, dy = b->pl.pos.y - pp.y, dz = b->pl.pos.z - pp.z, hd = sqrtf(dx * dx + dy * dy);
	if (hd > 230 || hd < 1 || fabsf(dz) > 70 || !b->pl.grounded) return 0;
	float a = atan2f(dy, dx) - pyaw; while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f;
	if (fabsf(a) > 0.9f) return 0;
	float away = (cosf(b->pl.yaw) * dx + sinf(b->pl.yaw) * dy) / hd;   // > 0: it looks away from the player
	return away > 0.35f ? 2 : 1;
}
// the victim to its place: in front of the player, facing it (front) or turned away (back); not into a wall
static void wpn_exec_place(Bot *b, V3 pp, float yaw, int front) {
	float c = cosf(yaw), s = sinf(yaw), want = front ? WEX_FRONT_D : WEX_BACK_D;
	V3 o = v3(pp.x, pp.y, pp.z + 90); float h = raycast(o, v3(c, s, 0), want + 30, NULL, NULL);
	float d = h >= 0 ? fmaxf(18, fminf(want, h - 28)) : want;
	b->pl.pos.x = pp.x + c * d; b->pl.pos.y = pp.y + s * d; if (fabsf(b->pl.pos.z - pp.z) < 40) b->pl.pos.z = pp.z;
	b->pl.vel = v3(0, 0, 0); b->pl.yaw = front ? yaw + 3.14159265f : yaw; b->prev_yaw = b->pl.yaw; b->yaw_rate = 0;
	logp("  execution: the victim placed %.0f cm in front (wanted %.0f, wall at %.0f)\n", d, want, h);
}
// start the stealth kill on bot v with the knife slot's model (the caller switches to the knife). b70: with the arm blade
// (cqcb clips) it is the execution from behind
static void wpn_exec_start(WSlot *k, int v, int from, int front, V3 pp, float *pyaw);
static void wpn_stealth_start(WSlot *k, int v, int from, V3 pp, float *pyaw) {
	if ((k->c[CL_CQCB] >= 0 || k->cy) && k->c[CL_CQC] >= 0) { wpn_exec_start(k, v, from, 0, pp, pyaw); return; }   // (b107: the cyborg's blade, whichever model it is)
	wst.on = 1; wst.victim = v; wst.killed = 0; wst.t = 0; wst.from = from; wst.axe = 0; wst.front = 0; wst.stage = 1; wst.stabbed = 1; wst.rdy = 0;
	wst.len = k->c[CL_CQC] >= 0 ? k->len[CL_CQC] : 0.9f;
	anim_stop(&k->a); k->a.base = -1; anim_set_base(&k->a, k->c[CL_IDLE], 0, 0); anim_play(&k->a, k->c[CL_CQC] >= 0 ? k->c[CL_CQC] : k->c[CL_FIRE], 0.03f, 0.2f, 0);
	Bot *b = &bots[v]; b->hold = 1; b->hold_yaw = b->pl.yaw; b->hold_pitch = 0;
	*pyaw = atan2f(b->pl.pos.y - pp.y, b->pl.pos.x - pp.x);
	ev_play("weapon/knife/swing_r", NULL);
}
// b70: the arm blade's execution of bot v (front 1: cqc_ready + cqc, 0: cqcb_ready + cqcb)
static void wpn_exec_start(WSlot *k, int v, int from, int front, V3 pp, float *pyaw) {
	// b107: the knife's set (the cyborg's blade on PC, vm_knife_cy.gvm) has one execution pair - the grab and the blade up under
	// the chin; from behind it plays the same on the victim turned away (the katar's set has its own back pair)
	int fc = front || k->c[CL_CQCB] < 0;   // the clips: the front pair
	int cr = k->c[fc ? CL_CQC_READY : CL_CQCB_READY], cs = k->c[fc ? CL_CQC : CL_CQCB];
	wst.on = 1; wst.victim = v; wst.killed = 0; wst.t = 0; wst.from = from; wst.axe = 1; wst.front = fc; wst.stabbed = 0;
	wst.rdy = cr >= 0 ? k->len[fc ? CL_CQC_READY : CL_CQCB_READY] : 0; wst.stab = wst.rdy + (fc ? WEX_STAB_F : WEX_STAB_B);
	wst.len = wst.rdy + (cs >= 0 ? k->len[fc ? CL_CQC : CL_CQCB] : 0.9f);
	anim_stop(&k->a); k->a.base = -1; anim_set_base(&k->a, k->c[CL_IDLE], 0, 0);
	if (cr >= 0) { anim_play(&k->a, cr, 0.06f, 0, 0); wst.stage = 0; } else { anim_play(&k->a, cs, 0.06f, 0.2f, 0); wst.stage = 1; }
	Bot *b = &bots[v]; float yaw = atan2f(b->pl.pos.y - pp.y, b->pl.pos.x - pp.x);
	wpn_exec_place(b, pp, yaw, front); *pyaw = yaw;
	bot_grab_ex(v, front);
	ev_play("weapon/knife/swing_r", NULL);
}
// per frame while on: the victim index once, when the kill lands (the caller deals the damage); -1 otherwise.
// done -> 1 when the clip is over (the caller returns to wst.from or keeps the knife). b70 (the arm blade): the kill lands as
// the strike begins (the victim's death clip is the strike's partner), wst_stab_ev when the blade goes in
static int wpn_stealth_update(float dt, V3 pp, float *pyaw, float *pitch, int *done, WSlot *k) {
	*done = 0; wst_stab_ev = 0; if (!wst.on) return -1;
	wst.t += dt; Bot *b = &bots[wst.victim];
	if (wst.axe) {
		if (wst.stage == 0 && wst.t >= wst.rdy - 0.017f && k) { anim_play(&k->a, k->c[wst.front ? CL_CQC : CL_CQCB], 0.02f, 0.2f, 0); wst.stage = 1; }
		// the view: on the victim's face (front) / the back of its head, from the grab until the release
		V3 h = b->head; if (h.z < b->pl.pos.z + 40) h = v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 150);
		float want = atan2f(h.y - pp.y, h.x - pp.x), d = want - *pyaw; while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f;
		float hd = sqrtf((h.x - pp.x) * (h.x - pp.x) + (h.y - pp.y) * (h.y - pp.y)), wp = atan2f(h.z - (pp.z + P_EYE), fmaxf(hd, 20.f));
		if (wst.t < wst.len - 0.35f) { float k_ = fminf(1, 9 * dt); *pyaw += d * k_; *pitch += (fminf(wp, 0.55f) - *pitch) * fminf(1, 6 * dt); }
		else *pitch += (0 - *pitch) * fminf(1, 3 * dt);   // the release: the view comes back level
	} else if (!wst.killed) {   // keep facing the victim's back
		float want = atan2f(b->pl.pos.y - pp.y, b->pl.pos.x - pp.x), d = want - *pyaw; while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f;
		*pyaw += d * fminf(1, 10 * dt); *pitch += (-0.12f - *pitch) * fminf(1, 6 * dt);
	}
	int r = -1;
	float kt = wst.axe ? wst.rdy : WST_KILL_T;
	if (!wst.killed && (wst.t >= kt || b->state == BS_DEAD)) { wst.killed = 1; if (!wst.axe) b->hold = 0; if (b->state != BS_DEAD) r = wst.victim; if (!wst.axe) ev_play("impact/killknife", &b->chest.x); }
	if (wst.axe && !wst.stabbed && wst.t >= wst.stab) { wst.stabbed = 1; wst_stab_ev = 1; ev_play("impact/killknife", &b->head.x); }
	if (wst.t >= wst.len - 0.1f) { wst.on = 0; *done = 1; if (wst.axe) { b->hold = 0; bot_release(wst.victim); } }
	return r;
}

// ---- dropped weapons and pick-ups
// Tutorial 249-252 s ("Weapon Pick Up": "Swap your weapon with the weapon at the center of ground. Press [V] near the
// weapon to swap."): near a weapon lying on the ground a prompt at the bottom centre shows [V], its ammo and its name;
// the swap is instant (the new gun comes up with its draw clip, the old one stays on the ground). Dead players drop their
// gun. Vita: hold Square (a tap still reloads). Every dropped weapon type needs a loaded model: the bots' gun (M4A1) is
// kept loaded in wspare when the loadout has none (loaded with the map, ~1 s, ~3.5 MB); a swap exchanges the models,
// so nothing is loaded during play.
#define NDROP 6
#define DROP_LIFE 30.f
typedef struct { int on, def, ammo, res; float p[3], yaw, t, lit[3]; int fly; float v[3], roll, fly_t, fly_len, spin, fz0; } WDrop;   // b79: fly = thrown from the dying hands (v, the roll about the barrel)
static WDrop wdrop[NDROP];
// b79 (Paz video EP19 5:27.05-5:27.2): ~0.1 s after it leaves the hands the gun turns into a cyan hologram (the texture tinted
// cyan, hexagon cells, bright edges - the spawn hologram's look) and stays one: a pick-up. main.c points this at vision.h.
static void (*wpn_holo_hook)(const Model *M, const float *MVP, const float *Mw, const MBone *P, float sp);
static const float WPN_HOLO_TINT[3] = { 0.55f, 1.60f, 1.60f };   // = vision.h VIS_SPAWN_TINT
static float wpn_drop_holo(const WDrop *d) { return !wpn_holo_hook ? 0 : d->t < 0.08f ? 0 : fminf(1, (d->t - 0.08f) / 0.14f); }
static WSlot wspare = { -1 };
static int wpn_bot_def = -1;   // weapons.txt index of the bots' gun
static int wpn_pick = -1; static float wpn_sq_t;   // the drop in reach; how long Square has been held
static unsigned char wpn_bot_dead[NBOT_MAX];
static void wslot_fix(WSlot *w) {   // pointers into the slot's own model after the struct was moved
	if (!w->ok) return; w->a.m = &w->m;
	w->mz = model_attach(&w->m, "mz_grip"); w->hud1 = model_attach(&w->m, "hud1"); w->scope = model_attach(&w->m, "scope"); w->tip = model_attach(&w->m, "blade_tip"); w->tbase = model_attach(&w->m, "blade_base");
}
static WSlot *wpn_holder(int def) {   // a loaded model of weapon def
	for (int s = 0; s < WS_N; s++) if (wsl[s].ok && wsl[s].def == def) return &wsl[s];
	return wspare.ok && wspare.def == def ? &wspare : NULL;
}
// after loadout_load (map start): keep the bots' gun loaded when no slot has it; clear the ground
static void wpn_spare_prepare(const char *dir, int want) {
	memset(wdrop, 0, sizeof(wdrop)); wpn_pick = -1; memset(wpn_bot_dead, 0, sizeof(wpn_bot_dead)); wst.on = 0; wscope.zoom = 0;
	wpn_bot_def = wdef_find("m4a1");
	if (!want || wpn_bot_def < 0) { wslot_free_ptr(&wspare); return; }
	for (int s = 0; s < WS_N; s++) if (wsl[s].ok && wsl[s].def == wpn_bot_def) { wslot_free_ptr(&wspare); return; }
	if (wspare.ok && wspare.def == wpn_bot_def) return;
	if (wslot_load_ptr(&wspare, wpn_bot_def, dir) < 0) wspare.def = -1;
}
static void wpn_drop_add(int def, const float *p, float yaw, int ammo, int res) {
	if (def < 0 || !wpn_holder(def)) return;
	int k = 0; for (int i = 0; i < NDROP; i++) { if (!wdrop[i].on) { k = i; break; } if (wdrop[i].t > wdrop[k].t) k = i; }
	WDrop *d = &wdrop[k]; memset(d, 0, sizeof(*d)); d->on = 1; d->def = def; d->ammo = ammo; d->res = res; d->yaw = yaw;
	V3 o = v3(p[0], p[1], p[2] + 60); float h = raycast(o, v3(0, 0, -1), 200, NULL, NULL);
	d->p[0] = p[0]; d->p[1] = p[1]; d->p[2] = h >= 0 ? o.z - h : p[2];
	light_at(d->p[0], d->p[1], d->p[2] + 20, d->lit);
}
// b79 (Paz video EP19 5:26.9-5:27.1: the gun leaves the dying enemy's hands, tumbles away and lands): the drop starts in the
// hands (f: 35 cm in front of the feet; chest height = f[2] + 105) and flies off away from the player (pp), up and turning
static void wpn_drop_throw(int def, const float *f, float yaw, V3 pp, int ammo, int res) {
	wpn_drop_add(def, f, yaw, ammo, res);
	WDrop *d = NULL; for (int k = 0; k < NDROP; k++) if (wdrop[k].on && wdrop[k].t == 0 && !wdrop[k].fly && wdrop[k].def == def) d = &wdrop[k];
	if (!d) return;
	float fx = f[0] - pp.x, fy = f[1] - pp.y, fl = sqrtf(fx * fx + fy * fy) + 1e-3f, z0 = f[2] + 105, floor_ = d->p[2], vz = 260;
	d->fly = 1; d->fz0 = floor_; d->p[0] = f[0]; d->p[1] = f[1]; d->p[2] = z0; d->v[0] = fx / fl * 170; d->v[1] = fy / fl * 170; d->v[2] = vz;
	float h = z0 - floor_; d->fly_len = (vz + sqrtf(vz * vz + 2 * 980 * fmaxf(h, 0))) / 980; d->fly_t = 0; d->roll = -1.5708f; d->spin = (rand() & 1 ? 1 : -1) * (2.0f + 1.5f * (rand() % 100) / 100.f);
}
// per frame: new drops from dead bots, ageing, the drop in reach (pp = the player's feet, pyaw = view yaw)
static void wpn_drops_update(float dt, V3 pp, float pyaw) {
	for (int i = 0; i < nbot && i < NBOT_MAX; i++) {
		int dead = bots[i].state == BS_DEAD;
		if (dead && !wpn_bot_dead[i] && bots[i].team != 0 && wpn_bot_def >= 0) {   // enemies drop their gun (allies' guns would clutter the ground)
			float f[3] = { bots[i].pl.pos.x + 35 * cosf(bots[i].pl.yaw), bots[i].pl.pos.y + 35 * sinf(bots[i].pl.yaw), bots[i].pl.pos.z };
			const WDef *D = &wdef[wpn_bot_def]; wpn_drop_throw(wpn_bot_def, f, bots[i].pl.yaw + 1.2f, pp, D->mag, D->mag * 2);
		}
		wpn_bot_dead[i] = dead;
	}
	wpn_pick = -1; float bd = 1e9f;
	for (int i = 0; i < NDROP; i++) {   // b79: the thrown ones fly (gravity, walls stop them), rolling onto their side as they land
		WDrop *d = &wdrop[i]; if (!d->on || !d->fly) continue; d->fly_t += dt;
		V3 o = v3(d->p[0], d->p[1], d->p[2]), mv = v3(d->v[0] * dt, d->v[1] * dt, 0); float ml = sqrtf(mv.x * mv.x + mv.y * mv.y);
		if (ml > 0.01f) { float hw = raycast(o, v3(mv.x / ml, mv.y / ml, 0), ml + 8, NULL, NULL); if (hw >= 0) { d->v[0] = d->v[1] = 0; mv = v3(0, 0, 0); } }
		d->v[2] -= 980 * dt; d->p[0] += mv.x; d->p[1] += mv.y; d->p[2] += d->v[2] * dt; d->yaw += d->spin * dt;
		float k = d->fly_len > 0.05f ? fminf(1, d->fly_t / d->fly_len) : 1; d->roll = -1.5708f + 7.854f * k;   // (-90 deg upright -> 360 deg: lying on its side)
		V3 up = v3(d->p[0], d->p[1], d->p[2] + 80); float hf = raycast(up, v3(0, 0, -1), 400, NULL, NULL), fz = hf >= 0 ? up.z - hf : d->fz0;   // (no floor under it: the one under the hands)
		if (d->v[2] < 0 && d->p[2] <= fz) { d->p[2] = fz; d->fly = 0; d->roll = 0; light_at(d->p[0], d->p[1], d->p[2] + 20, d->lit);
			static int nl_; if (nl_ < 3) { nl_++; logp("  gun drop: thrown from the hands, landed after %.2f s (b79)\n", d->fly_t); } }
		if (d->fly_t > 3) { d->fly = 0; d->roll = 0; }
	}
	for (int i = 0; i < NDROP; i++) {
		WDrop *d = &wdrop[i]; if (!d->on) continue; d->t += dt; if (d->fly) continue;
		if (d->t > DROP_LIFE || !wpn_holder(d->def)) { d->on = 0; continue; }
		float dx = d->p[0] - pp.x, dy = d->p[1] - pp.y, dz = d->p[2] - pp.z, hd = sqrtf(dx * dx + dy * dy);
		if (hd > 130 || fabsf(dz) > 90) continue;
		float a = atan2f(dy, dx) - pyaw; while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f;
		if (hd > 45 && fabsf(a) > 1.1f) continue;   // in front, or right under the feet
		if (hd < bd) { bd = hd; wpn_pick = i; }
	}
}
// Square handling near a drop: a tap reloads (on release), holding 0.35 s picks up. Returns 1 when the swap should happen.
static int wpn_pick_input(int sq_held, int *want_reload, float dt) {
	if (wpn_pick < 0) { wpn_sq_t = 0; return 0; }
	*want_reload = 0;
	if (sq_held) { float t0 = wpn_sq_t; wpn_sq_t += dt; return t0 < 0.35f && wpn_sq_t >= 0.35f; }
	if (wpn_sq_t > 0 && wpn_sq_t < 0.35f) *want_reload = 1;
	wpn_sq_t = 0; return 0;
}
// swap: the drop's gun goes into its slot (pistols: secondary, the rest: primary), the old one is dropped at pp.
// Returns the slot that now holds the new gun (the caller switches to it), or -1 (only ammo was taken / nothing).
static int wpn_pick_up(V3 pp, float pyaw) {
	if (wpn_pick < 0) return -1;
	WDrop *d = &wdrop[wpn_pick]; int s = !strcmp(wdef[d->def].cls, "hg") ? WS_SECONDARY : WS_PRIMARY; WSlot *w = &wsl[s];
	if (w->ok && w->def == d->def) {   // the same gun: take its ammo
		w->res += d->ammo + d->res; if (w->res > wdef[d->def].res * 2) w->res = wdef[d->def].res * 2;
		d->on = 0; ev_play("ui/ghost_ammo_pickup", NULL); wpn_pick = -1; return -1;
	}
	WSlot *h = wpn_holder(d->def); if (!h || h == w || !w->ok) return -1;
	WSlot t = *w; *w = *h; *h = t; wslot_fix(w); wslot_fix(h);
	int od = h->def, oa = h->ammo, orr = h->res;   // the gun we had
	w->ammo = d->ammo; w->res = d->res; d->on = 0;
	float f[3] = { pp.x + 30 * cosf(pyaw), pp.y + 30 * sinf(pyaw), pp.z };
	wpn_drop_add(od, f, pyaw + 1.4f, oa, orr);
	ev_play("weapon/common/pickup", NULL); wpn_pick = -1;
	return s;
}
// the dropped guns, lying on their side (world pass; VP = world -> clip)
static MBone wpn_gpose[96];
static void wpn_drops_draw(const float *VP, const float *fogc) {
	for (int i = 0; i < NDROP; i++) {
		const WDrop *d = &wdrop[i]; if (!d->on) continue; WSlot *h = wpn_holder(d->def); if (!h || h->grip < 0 || h->m.nb > 96 || h->c[CL_IDLE] < 0) continue;
		model_sample(&h->m, h->c[CL_IDLE], 0, wpn_gpose); const MBone *G = &wpn_gpose[h->grip];
		// Grip space: x = barrel, z = top of the gun, y = its left. World: barrel along yaw, the left side up.
		float c = cosf(d->yaw), s = sinf(d->yaw), Wg[9] = { c, s, 0, 0, 0, 1, s, -c, 0 };   // columns: images of grip x, y, z
		if (d->fly || d->roll != 0) { float cr = cosf(d->roll), sr = sinf(d->roll), y_[3] = { 0, 0, 1 }, z_[3] = { s, -c, 0 };   // b79: in the air, rolled about the barrel
			for (int r = 0; r < 3; r++) { Wg[3 + r] = y_[r] * cr + z_[r] * sr; Wg[6 + r] = -y_[r] * sr + z_[r] * cr; } }
		float x = G->q[0], y = G->q[1], z = G->q[2], w = G->q[3];
		float Rg[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y) };   // column-major grip -> camera
		float A[9];   // world <- camera = Wg * Rg^T
		for (int r = 0; r < 3; r++) for (int cc = 0; cc < 3; cc++) { float sum = 0; for (int k = 0; k < 3; k++) sum += Wg[k * 3 + r] * Rg[k * 3 + cc]; A[cc * 3 + r] = sum; }
		float o[3] = { d->p[0], d->p[1], d->p[2] + (d->fly ? 0 : 5.0f) }, t[3];
		for (int r = 0; r < 3; r++) t[r] = o[r] - (A[r] * G->p[0] + A[3 + r] * G->p[1] + A[6 + r] * G->p[2]);
		float M[16] = { A[0], A[1], A[2], 0, A[3], A[4], A[5], 0, A[6], A[7], A[8], 0, t[0], t[1], t[2], 1 }, MVP[16]; wpn_mul4(MVP, VP, M);
		float tint[4] = { fminf(1.4f, d->lit[0] * 1.1f), fminf(1.4f, d->lit[1] * 1.1f), fminf(1.4f, d->lit[2] * 1.1f), 1 };
		float ld[4] = { 0.25f * A[0] + A[2], 0.25f * A[3] + A[5], 0.25f * A[6] + A[8], 0 };   // light from above (world up in the pose's camera space)
		float sp = wpn_drop_holo(d);   // b79: the pick-up hologram (self-lit: half its own light)
		if (sp > 0) for (int k = 0; k < 3; k++) { float b = tint[k] * (1 - 0.5f * sp) + 0.55f * sp; tint[k] = fminf(2.0f, b * (1 - sp + sp * WPN_HOLO_TINT[k])); }
		glDisable(GL_CULL_FACE); md_mesh0 = 2; wvm_draw(&h->m, MVP, wpn_gpose, ld, tint, 0, fogc);
		if (sp > 0) wpn_holo_hook(&h->m, MVP, M, wpn_gpose, sp);
		md_mesh0 = 0; glEnable(GL_CULL_FACE);
	}
}
// ---- HUD pieces (wpn_hud.h, host-tested in tools/weapons): pick-up prompt, stealth-kill prompt / failure
#include "wpn_hud.h"
static void wpn_hud(int knife_out) {
	WHud h; memset(&h, 0, sizeof(h));
	if (wpn_pick >= 0) { const WDrop *d = &wdrop[wpn_pick]; h.pick = 1; h.pick_name = wdef[d->def].name; h.pick_ammo = d->ammo + d->res; h.pick_icon = wdef[d->def].icon; h.pick_hold = wpn_sq_t / 0.35f; }
	h.stealth = wst_prompt > 0 && !wst.on; h.knife_out = knife_out; h.fail_t = wst_fail_t;
	wpn_hud_draw(&h);
}
