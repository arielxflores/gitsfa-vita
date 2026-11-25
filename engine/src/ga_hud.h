// Ghost Assault's HUD pieces (b63, ga.h): drawn from a plain description of this frame (GaHudView) with the hud pack's white
// image, so the HUD harnesses (tools/ui/host/combo.c: overlap check, captures) draw the same pieces as the game.
// Needs hud.h, hud_game.h (HG), skills.h (sk_hud_rect / sk_hud_frame / sk_prompt) before it.
// Footage: "9 Minutes of Ghost Assault Gameplay - IGN" (36 s the compass ring, 131 s the cyber dive, 186 s the corpse hack)
// and "NEW GAME MODE GHOST ASSAULT - Nick Bunyun" (the ghost's DANGER / DETECTED).
#pragma once
#include <math.h>
typedef struct {
	float now;
	int compass; float yaw;                         // the cyborg's compass ring: yaw = the view's bearing (world: +x east = 0, +y north)
	int nping; float ping_rel[8], ping_show[8]; unsigned char ping_close[8];   // pings: bearing relative to the view (+ = right), seconds left, filled
	int drone; float drone_x, drone_y, drone_m;     // the seeker drone's marker (screen px, metres away)
	float dive_k; int ndia; float dia[8][2];        // the cyber dive (0..1) and the enemies it shows (screen px)
	float detected_k, danger_k;                     // the ghost's warnings (0..1)
	int hack; int hack_ally; float hack_pr;         // a body being hacked / restored: progress 0..1 (< 0: in reach, not held yet)
} GaHudView;
#define GA_PING_SHOW_T 1.6f
static void ga_seg(float x0, float y0, float x1, float y1, float w, const float *c) {   // a line on the HUD (screen px)
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy); if (l < 0.01f || HG.white < 0) return; float nx = -dy / l, ny = dx / l;
	float A[6] = { dx, w * nx, x0 - 0.5f * w * nx, dy, w * ny, y0 - 0.5f * w * ny }; hud_img(HG.white, HG.screen, A, c, HUD_Z4, -1, 1);
}
static void ga_tri(float x0, float y0, float x1, float y1, float x2, float y2, const float *c) {   // a filled triangle
	if (HG.white < 0) return; const HImg *I = &himg[HG.white]; float u = (I->x + I->w * 0.5f) / htw[I->tex], v = (I->y + I->h * 0.5f) / hth[I->tex];
	float xy[3][2] = { { x0, y0 }, { x1, y1 }, { x2, y2 } }, uv[3][2] = { { u, v }, { u, v }, { u, v } };
	hud_poly(htex[I->tex], HG.screen, (const float (*)[2])xy, (const float (*)[2])uv, 3, c);
}
static void ga_diamond(float x, float y, float r, float w, const float *c) { ga_seg(x, y - r, x + r, y, w, c); ga_seg(x + r, y, x, y + r, w, c); ga_seg(x, y + r, x - r, y, w, c); ga_seg(x - r, y, x, y - r, w, c); }
// the cyborg's compass ring (footage 36 s / 186 s: a thin ring on the floor round the player, its far half across the bottom
// of the view - highest in the middle, lower toward the sides - the four letters on it in small dark boxes; a ghost's ping is
// a red triangle on it, open when far, filled when close). Screen-space ellipse: centre (480, 475), radii 330 x 70: bearing 0
// (ahead) at the top (y 405), +-46 degrees at its ends (241 / 719, y 427). The original runs it off the screen under its
// gauges; here it stops between the skill gauge and the HP / AP figures (nothing over them).
#define GA_ARC_CX 480.0f
#define GA_ARC_CY 475.0f
#define GA_ARC_RX 330.0f
#define GA_ARC_RY 70.0f
#define GA_ARC_SPAN 0.81f
static void ga_arc_pt(float rel, float *x, float *y) { *x = GA_ARC_CX + GA_ARC_RX * sinf(rel); *y = GA_ARC_CY - GA_ARC_RY * cosf(rel); }
static void ga_ring(float dy, float w, const float *c) {   // the visible arc (a second, fainter one under it: footage)
	float px, py; ga_arc_pt(-GA_ARC_SPAN, &px, &py);
	for (int k = 1; k <= 28; k++) { float x, y; ga_arc_pt(-GA_ARC_SPAN + 2 * GA_ARC_SPAN * k / 28, &x, &y); ga_seg(px, py + dy, x, y + dy, w, c); px = x; py = y; }
}
static float ga_wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static void ga_compass_paint(const GaHudView *V) {
	static const float ln[4] = { 0.88f, 0.94f, 0.97f, 0.55f }, ln2[4] = { 0.88f, 0.94f, 0.97f, 0.22f }, lt[4] = { 0.92f, 0.96f, 1, 0.9f }, bx[4] = { 0.05f, 0.08f, 0.1f, 0.6f };
	HUD_TAG("gacompass");
	ga_ring(0, 1.5f, ln); ga_ring(14, 1.2f, ln2);
	static const char *NM[4] = { "E", "N", "W", "S" };
	for (int k = 0; k < 4; k++) { float rel = ga_wrap(V->yaw - k * 1.5707963f); if (fabsf(rel) > GA_ARC_SPAN * 0.97f) continue; float x, y; ga_arc_pt(rel, &x, &y);
		sk_hud_rect(x - 12, y - 1, x + 12, y + 23, bx); sk_hud_frame(x - 12, y - 1, x + 12, y + 23, 1, ln2); hud_text(HG.screen, x, y + 19, 20, 2, lt, HUD_Z4, NM[k]); }
	for (int i = 0; i < V->nping && i < 8; i++) {
		float rel = V->ping_rel[i], x, y; if (fabsf(rel) > GA_ARC_SPAN * 0.97f) rel = rel > 0 ? GA_ARC_SPAN * 0.97f : -GA_ARC_SPAN * 0.97f; ga_arc_pt(rel, &x, &y);   // (behind or aside: at the arc's end)
		float age = GA_PING_SHOW_T - V->ping_show[i], al = fminf(1, V->ping_show[i] / 0.4f), s = 13 + 7 * fmaxf(0, 1 - age / 0.25f);   // (pops in a little bigger)
		float c[4] = { 1, 0.16f, 0.18f, al }; y -= 16;
		if (V->ping_close[i]) ga_tri(x, y + s * 0.7f, x - s * 0.85f, y - s * 0.7f, x + s * 0.85f, y - s * 0.7f, c);   // close: filled (pointing down at the ring)
		else { ga_seg(x, y + s * 0.7f, x - s * 0.85f, y - s * 0.7f, 2.5f, c); ga_seg(x - s * 0.85f, y - s * 0.7f, x + s * 0.85f, y - s * 0.7f, 2.5f, c); ga_seg(x + s * 0.85f, y - s * 0.7f, x, y + s * 0.7f, 2.5f, c); }
	}
}
static void ga_hud_paint(const GaHudView *V) {
	static const float W1[4] = { 1, 1, 1, 1 }, YEL[4] = { 0.98f, 0.92f, 0.25f, 1 }; float now = V->now;
	if (V->dive_k > 0) {   // the cyber dive after a hack (footage 131 s): the world dark blue with cyan scan lines, the enemies as red diamonds
		HUD_TAG("~ga.dive");   // (a full-screen layer and world markers: not an overlap)
		float k = V->dive_k, bl[4] = { 0.0f, 0.05f, 0.12f, 0.55f * k }, ln[4] = { 0.3f, 0.75f, 1, 0.12f * k };
		sk_hud_rect(0, 0, 960, 544, bl); for (int y = (int)fmodf(now * 60, 18); y < 544; y += 18) sk_hud_rect(0, (float)y, 960, (float)y + 2, ln);
		for (int i = 0; i < V->ndia && i < 8; i++) { float sx = V->dia[i][0], sy = V->dia[i][1], r[4] = { 1, 0.15f, 0.2f, k }, rf[4] = { 1, 0.15f, 0.2f, 0.3f * k };
			ga_diamond(sx, sy, 14, 3, r); ga_tri(sx - 8, sy, sx + 8, sy, sx, sy + 8, rf); ga_tri(sx - 8, sy, sx + 8, sy, sx, sy - 8, rf); }
		HUD_TAG("gadive"); float c[4] = { 0.6f, 0.88f, 1, k }; hud_text(HG.screen, 480, 476, 24, 2, c, HUD_Z4, "CYBER DIVE");   // (under the compass ring)
	}
	if (V->compass) ga_compass_paint(V);
	if (V->drone) {   // the seeker drone's marker (seen through walls by the cyborgs)
		HUD_TAG("~ga.drone");   // (a world marker, as the name tags)
		float sx = V->drone_x, sy = V->drone_y, c[4] = { 1, 0.85f, 0.2f, 0.9f }; char b[16];
		ga_diamond(sx, sy, 11, 2.5f, c); ga_tri(sx - 6, sy, sx + 6, sy, sx, sy + 6, c); ga_tri(sx - 6, sy, sx + 6, sy, sx, sy - 6, c);
		hud_text(HG.screen, sx, sy - 18, 20, 2, c, HUD_Z4, "SEEKER DRONE"); snprintf(b, sizeof(b), "%dm", (int)V->drone_m); hud_text(HG.screen, sx, sy + 34, 20, 2, c, HUD_Z4, b);
	}
	if (V->detected_k > 0) {   // DETECTED: red scan lines across the view, the red bar (footage 18 s, 50 s, 232 s)
		HUD_TAG("~ga.scan");   // (the red scan lines: a full-screen layer)
		float k = V->detected_k, fl = 0.75f + 0.25f * sinf(now * 30), r[4] = { 0.95f, 0.08f, 0.12f, 0.20f * k * fl }, rb[4] = { 0.35f, 0.02f, 0.04f, 0.8f * k };
		for (int y = (int)fmodf(now * 140, 38); y < 544; y += 38) sk_hud_rect(0, (float)y, 960, (float)y + 5, r);
		HUD_TAG("gadetected"); float dy = hud_place(380, 318, 580, 352, HUD_P_NOTICE, 88, 400);
		if (dy < HUD_HIDE) { sk_hud_rect(380, 318 + dy, 580, 352 + dy, rb); float t[4] = { 1, 0.35f, 0.38f, k }; hud_text(HG.screen, 480, 344 + dy, 24, 2, t, HUD_Z4, "! DETECTED"); hud_claim(380, 318 + dy, 580, 352 + dy, HUD_P_NOTICE); }
	}
	if (V->danger_k > 0) {   // DANGER over the radar: a red ring and the word (footage 18 - 20 s)
		HUD_TAG("~ga.danger");   // (over the radar on purpose: the original's DANGER sits on it, footage)
		float k = V->danger_k, pu = 0.6f + 0.4f * sinf(now * 9), r[4] = { 1, 0.12f, 0.15f, 0.85f * k * pu }, f[4] = { 0.45f, 0.02f, 0.05f, 0.45f * k };
		float cx = 96, cy = 86, R = 52, px = cx + R, py = cy;
		sk_hud_rect(cx - R * 0.8f, cy - 16, cx + R * 0.8f, cy + 14, f);
		for (int k2 = 1; k2 <= 32; k2++) { float t = 6.2831853f * k2 / 32, x = cx + R * cosf(t), y = cy + R * sinf(t); ga_seg(px, py, x, y, 3, r); px = x; py = y; }
		float tc[4] = { 1, 0.3f, 0.32f, k }; hud_text(HG.screen, cx, cy + 9, 22, 2, tc, HUD_Z4, "DANGER");
	}
	if (V->hack) {   // a body in reach / being hacked (footage 186 s): the ring's dashes turn cyan from the left as it fills, the panel
		// above the ring in the middle ("HACKING CORPSE", "HOLD [E]")
		HUD_TAG("gacompass.hack");   // (the dashes on the compass ring: part of it)
		float pr = V->hack_pr, c[4] = { 0.3f, 0.9f, 0.95f, 0.95f }, cd[4] = { 0.3f, 0.9f, 0.95f, 0.22f };
		if (pr > 0) for (int k = 0; k < 36; k++) { float r0 = -GA_ARC_SPAN + 2 * GA_ARC_SPAN * k / 36, r1 = r0 + 2 * GA_ARC_SPAN / 36 * 0.55f, x0, y0, x1, y1;
			ga_arc_pt(r0, &x0, &y0); ga_arc_pt(r1, &x1, &y1); ga_seg(x0, y0 + 7, x1, y1 + 7, 4, (k + 0.5f) / 36 < pr ? c : cd); }
		HUD_TAG("gahack"); float dy = hud_place(350, 296, 610, 370, HUD_P_NOTICE, 88, 400);   // (clear of the ring's pings: their tops reach y ~378)
		if (dy < HUD_HIDE) { static const float bg[4] = { 0.03f, 0.06f, 0.08f, 0.78f }, ed[4] = { 0.75f, 0.85f, 0.9f, 0.5f }; sk_hud_rect(350, 296 + dy, 610, 370 + dy, bg); sk_hud_frame(350, 296 + dy, 610, 370 + dy, 1, ed);
			hud_text(HG.screen, 480, 324 + dy, 24, 2, W1, HUD_Z4, V->hack_ally ? "RESTORING ALLY" : "HACKING CORPSE");
			sk_prompt(480, 360 + dy, 24, YEL, "HOLD {SQ}"); hud_claim(350, 296 + dy, 610, 370 + dy, HUD_P_NOTICE); }
	}
}
