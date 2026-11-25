// Kill cam (the respawn modes: Team Deathmatch, Terminal Conquest). Footage (PSS9 TDM 90-96 s, 365-373 s; Port 339-346 s):
// the player is hit, the view drops to the body (red tint) for ~2 s, then cuts to the killer seen in third person from a few
// metres in front, the killer left of the centre, in normal colours, while the respawn bar and its timer count down; the HP /
// skill gauges are gone meanwhile (minimap, score and kill feed stay). The fall before the cut stays main.c's first-person drop
// (the engine has no third-person body of the player). Round modes (Elimination, Demolition) spectate the allies instead.
// Included at the end of elim_game.h. main.c hooks: killcam_camera (after the death fall, before elim_camera), killcam_panel_t
// (the respawn panel's clock), killcam_active (the red tint is off during the kill cam).
// b72 (the user's new PC footage, v49 433-449 s): the fall is the player's own body in third person (pbody.h): ~2.5 s on the
// body (red-tinted, the HUD up with HP 000), then the cut to the killer, who flashes up as a cyan hologram for a moment (red
// fragments around him, v49 440 s), with the respawn timer from 3.5 s (the footage's "Respawn Timer 03:..") - the respawn
// modes' death is 6 s (was 3.5; the bots' 5 s). Net games, the round modes and Ghost Assault keep 3.5 s.
// b82 (the user's Paz video EP19 5:57.8-6:06.2, v2.0): the body ragdolls (pbody.h) for ~1.8 s under a camera high above it;
// then the camera rises and turns towards the killer (his red marker in view, the body small below) until the cut at 3.0 s;
// the kill cam follows the killer, seen from the front, centred, in his normal colours (no hologram, no red fragments: EP19
// 6:01.0-6:01.4 frame by frame), the "Respawn Timer" counting 5.00 -> 0.00 (seconds . hundredths); respawn at 8.0 s with the
// cyan edge glow. Respawn modes' death: 8 s (was 6: 2.5 body + 3.5 kill cam).
#pragma once
#include "pbody.h"
#define KC_DEATH  8.0f   // main.c's death time in the respawn modes (pdead_t starts here)
#define KC_KILLER 5.0f   // the kill cam and its respawn timer (the rest is on the body)
#define KC_TURN   1.8f   // b82: when the camera leaves the body for the killer (seconds into the death)
static struct { int on, body, bot; float dir, len, kt; float cam[3], cyaw, cpitch; int cam_ok; } KC = { 0, 0, -1, 0, 3.5f, 0 };
static int killcam_active(void) { return KC.on; }
static int killcam_body(void) { return KC.body; }   // the camera shows the player's body (pbody.h): no gun, the HUD stays
static float killcam_death_len(void) { return em_round_mode() || ga_on || net_on() ? 3.5f : KC_DEATH; }
// main.c: a death began (pdead_t 0 -> len)
static void killcam_death(float len) { KC.len = len > 0 ? len : 3.5f; KC.kt = 0; }
static float killcam_elapsed(float pdead) { return KC.len - pdead; }
static float killcam_body_len(void) { return em_round_mode() ? 1e9f : KC.len >= 5 ? KC.len - KC_KILLER : 1.5f; }
// pdead: main.c's pdead_t (counts down from KC.len); killer: the bot that killed the player (-1 none: no kill cam)
static void killcam_camera(float *pos, float *yaw, float *vpitch, float pdead, int killer, float dt) {
	int was = KC.on; KC.on = 0; KC.body = 0;
	if (pdead <= 0) { KC.kt = 0; vis_kc_actor = -1; return; }
	float el = KC.len - pdead;
	int kc_ok = !em_round_mode() && killer >= 0 && killer < nbot;
	if (el < killcam_body_len() || !kc_ok) {
		if (PB.on && !em_watching()) { pbody_camera(pos, yaw, vpitch, el); KC.body = 1;
			// b82: towards the killer before the cut (EP19 5:59.6-6:01.0): up ~1.5 m over the body, turned to face him
			float bl = killcam_body_len(); if (kc_ok && bl >= 2.5f && bl < 100 && el > KC_TURN) {
				float k = (el - KC_TURN) / 1.0f; if (k > 1) k = 1; k = k * k * (3 - 2 * k);
				const Bot *b = &bots[killer]; V3 o = v3(pos[0], pos[1], pos[2]), c = v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 110);
				float up = 150, h = raycast(o, v3(0, 0, 1), up + 30, NULL, NULL); if (h > 0) up = fmaxf(0, h - 30);
				V3 hi = v3(o.x, o.y, o.z + up), e = vsub(c, hi); float hl = sqrtf(e.x * e.x + e.y * e.y) + 1e-3f, ty = atan2f(e.y, e.x), tp = atan2f(e.z, hl);
				if (tp > 0.1f) tp = 0.1f; if (tp < -0.75f) tp = -0.75f;
				pos[2] += up * k; *yaw += wrap_pi(ty - *yaw) * k; *vpitch += (tp - *vpitch) * k; } }
		vis_kc_actor = -1; KC.cam_ok = 0; return; }
	Bot *b = &bots[killer]; V3 c = v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 115);   // the killer's chest
	if (!was || KC.bot != killer) {   // the side the camera stands on: towards where the player fell (the killer faced that way)
		float dx = pos[0] - c.x, dy = pos[1] - c.y; KC.dir = dx * dx + dy * dy > 1 ? atan2f(dy, dx) : b->pl.yaw; KC.bot = killer; KC.kt = 0; KC.cam_ok = 0; }
	KC.kt += dt;
	vis_kc_actor = -1;   // b82: EP19 shows the killer as he is (b72's hologram flash and red fragments came from older footage)
	// b82: the camera turns slowly with the killer as he runs (EP19 6:01-6:06: always his front, centred), ~2.9 m off, above his chest
	{ float want_dir = b->state != BS_DEAD ? b->pl.yaw : KC.dir, dd = wrap_pi(want_dir - KC.dir); if (KC.kt > 0.8f) KC.dir += dd * fminf(1, 0.6f * dt); }
	V3 f = v3(cosf(KC.dir), sinf(KC.dir), 0), sd = v3(-sinf(KC.dir), cosf(KC.dir), 0);
	V3 want = vadd(c, vadd(vmul(f, 290), vadd(vmul(sd, 30), v3(0, 0, 40))));
	V3 d = vsub(want, c); float l = sqrtf(vdot(d, d)); V3 dn = vmul(d, 1 / l);
	float h = raycast(c, dn, l, NULL, NULL); if (h > 0) want = vadd(c, vmul(dn, fmaxf(70, h - 25)));   // not through a wall
	if (KC.cam_ok) { float k = fminf(1, 8 * dt); want = v3(KC.cam[0] + (want.x - KC.cam[0]) * k, KC.cam[1] + (want.y - KC.cam[1]) * k, KC.cam[2] + (want.z - KC.cam[2]) * k); }
	KC.cam[0] = want.x; KC.cam[1] = want.y; KC.cam[2] = want.z; KC.cam_ok = 1;
	V3 e = vsub(c, want); float hl = sqrtf(e.x * e.x + e.y * e.y) + 1e-3f;
	pos[0] = want.x; pos[1] = want.y; pos[2] = want.z;
	*yaw = atan2f(e.y, e.x) - 0.06f; *vpitch = atan2f(e.z, hl);   // the killer a touch left of the centre, the panel to his right (EP19)
	KC.on = 1;
}
// the respawn panel's clock (menu_respawn_draw's t): respawn modes from the cut to the killer (footage: the bar comes with the kill
// cam); round modes from the death until the spectating begins (Elimination shows none). < 0: not shown
static float killcam_panel_t(float pdead) {
	if (pdead <= 0 || elim_on || em_watching()) return -1;
	if (em_round_mode()) return KC.len - pdead;
	float t = KC.len - pdead - killcam_body_len(); return t >= 0 ? t : -1;
}
