// pbody.h - b72 (user: "please make sure to include or improve upon any death animations or elements you can as well"):
// the player's own body after death. PC footage (gameplay footage v49 375-401 s, 433-449 s, 512-528 s; v48 55:30-56:16,
// 1:07:18): the moment the player dies the view leaves the eyes for a camera behind and above the body, which falls in a
// death clip (a red burst at the chest) in a dark red-tinted frame while the HUD stays up (HP 000 in red); ~3 s later the
// respawn modes cut to the killer (killcam.h), the round modes keep the body until the spectating of an ally begins.
// Before b72 the engine had no third-person body of the player: the view dropped to the floor in first person.
// The body is the player's operator (chrt, the skin of the match), animated with the bots' death clips (chr.h ca_die: by the
// side the killing hit came from; heavy hits / explosions their own sets) and drawn as a dead bot (bots_draw).
// Included by killcam.h. main.c: pbody_start at the death (pdead_t 0 -> > 0), pbody_update every frame (stops when alive),
// pbody_draw after bots_draw; killcam.h pbody_camera (the camera).
// b82 (EP19 5:57.8-6:00): the body goes limp - ragdoll.h after the first RD_SOFT s of its clip (the original's "death clip ->
// SoftKey_Ragdoll -> Ragdoll"), thrown by the killing hit; it rolls and slides on the map's own collision until it lies still.
#pragma once
#include "ragdoll.h"
static struct {
	int on, ct, skin, wpn, lod; CAnim ca; CSkin sk; MBone mpose[64]; MBone *pose; int pose_new, still;
	float x, y, z, yaw, t, drop, vz, lit[3]; V3 pelvis, chest; float eye[3], eyaw, epitch, cam_yaw;
	Ragdoll rd; MBone rpose[CB_MAXB]; int rd_ok;   // b82: the ragdoll and its bank pose
} PB = { .ct = -1, .wpn = -1 };
static int pbody_ragdoll = 1;   // b82 test switch noragdoll: the death clip alone (b81)
static int pbody_collide(float *c, float r, float *n) {   // ragdoll.h's world: the player collision (player.h sphere_resolve)
	V3 p = v3(c[0], c[1], c[2]), o = p; sphere_resolve(&p, r, 1, NULL);
	float dx = p.x - o.x, dy = p.y - o.y, dz = p.z - o.z, l = sqrtf(dx * dx + dy * dy + dz * dz); if (l < 1e-4f) return 0;
	c[0] = p.x; c[1] = p.y; c[2] = p.z; n[0] = dx / l; n[1] = dy / l; n[2] = dz / l; return 1;
}
static V3 pbody_world(const float *p) {   // model space (the character faces -y) -> world (bot_world)
	float th = PB.yaw + 1.5707963f, c = cosf(th), s = sinf(th);
	return v3(PB.x + c * p[0] - s * p[1], PB.y + s * p[0] + c * p[1], PB.z - PB.drop + p[2]);
}
static void pbody_pose(void) {
	CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1;
	const CBank *B = PB.ca.B; MBone *P = ca_pose(&PB.ca, &in);
	if (PB.rd_ok) { rd_pose(&PB.rd, PB.rpose); P = PB.rpose; }   // b82
	cb_to_model(&chrt[PB.ct].bind, P, PB.mpose); PB.pose = PB.mpose; PB.pose_new = 1;
	PB.pelvis = pbody_world(P[B->pelvis].p); PB.chest = pbody_world(P[B->spine[2]].p);
}
// ct: the body's character type; feet / yaw: where the player stood; hit: the killing shot's direction of travel (0: from
// the front); dmg: its damage (>= 300 an explosion, >= 100 a heavy hit: the bots' destruction set); wsh: the gun (its
// third-person model in the hand until the body is down, as the bots')
static int pbody_start(int ct, V3 feet, float yaw, const float *eye, float vpitch, V3 hit, float dmg, const char *wsh) {
	PB.on = 0; if (ct < 0 || ct >= NCHR_MAX || !chrt[ct].m.ok || !chr_bank[chrt[ct].female].ok) return 0;
	ChrType *C = &chrt[ct];
	if (PB.sk.ok && (PB.ct != ct || PB.skin != C->skin)) cskin_free(&PB.sk);   // another operator / skin (chrt_reskin): new buffers
	if (!PB.sk.ok) cskin_init(&PB.sk, &C->m); if (!PB.sk.ok) return 0;
	PB.ct = ct; PB.skin = C->skin; PB.wpn = -1;
	for (int k = 0; wsh && k < NBOT_WPN; k++) if (!strcmp(BOT_WPN[k].sh, wsh) && bot_w3_ok[k] > 0) PB.wpn = k;
	const CBank *B = &chr_bank[C->female]; int set = cb_find_set(B, PB.wpn >= 0 ? BOT_WPN[PB.wpn].sh : "m4a1");
	ca_init(&PB.ca, B, set < 0 ? 0 : set);
	CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1; in.zoom = 1;
	for (int f = 0; f < 6; f++) ca_update(&PB.ca, &in, 1 / 30.f);   // the standing pose the death clip blends from
	static MBone pre[CB_MAXB]; memcpy(pre, ca_pose(&PB.ca, &in), sizeof(MBone) * B->nb);   // b82: the ragdoll's previous step (its starting speed)
	PB.x = feet.x; PB.y = feet.y; PB.z = feet.z; PB.yaw = yaw; PB.drop = PB.vz = 0; PB.t = 0; PB.still = 0; PB.sk.cur = -1; PB.lod = 0;
	// the side of the body the hit came from (bots.h bot_hit_side) and the clip set (bots.h bot_hurt)
	int side = 0; { float l = sqrtf(hit.x * hit.x + hit.y * hit.y);
		if (l > 1e-3f) { float fx = cosf(yaw), fy = sinf(yaw), sx = -hit.x / l, sy = -hit.y / l, fw = sx * fx + sy * fy, rt = sx * fy - sy * fx;
			side = fw > 0.707f ? 0 : fw < -0.707f ? 1 : rt > 0 ? 3 : 2; } }
	int kind = dmg >= 300 ? 1 : dmg >= 100 ? 2 : 0;
	ca_die(&PB.ca, 0, side, kind);
	{ CAnim *A = &PB.ca; A->death_scale = 1;   // the corpse's travel is shortened by a wall (bots.h bot_death_clear)
	  if (A->death.clip >= 0) { const CClip *Cl = &B->clip[A->death.clip]; CLoc L0[CB_MAXB], L1[CB_MAXB];
		cb_sample(B, A->death.clip, 0, 0, L0); cb_sample(B, A->death.clip, Cl->len, 0, L1);
		int r = B->root; float dv[3] = { L1[r].p[0] - L0[r].p[0], L1[r].p[1] - L0[r].p[1], 0 };
		V3 w = vsub(pbody_world(dv), v3(PB.x, PB.y, PB.z)); float l = sqrtf(w.x * w.x + w.y * w.y);
		A->death_p0[0] = L0[r].p[0]; A->death_p0[1] = L0[r].p[1];
		if (l >= 20) { V3 d = vmul(w, 1 / l); float h = raycast(v3(PB.x, PB.y, PB.z + 40), d, l + 45, NULL, NULL); if (h >= 0) A->death_scale = fmaxf(0, (h - 45) / l); } } }
	PB.rd_ok = 0; pbody_pose();
	// b82: the ragdoll from the clip's first pose, thrown along the killing hit (an explosion hardest), a little upwards
	if (pbody_ragdoll) { float th = PB.yaw + 1.5707963f, T[5] = { cosf(th), sinf(th), PB.x, PB.y, PB.z }, l = sqrtf(hit.x * hit.x + hit.y * hit.y + hit.z * hit.z);
		float sp = kind == 1 ? 650 : dmg >= 100 ? 420 : 300, hv[3] = { 0, 0, 0 };
		if (l > 1e-3f) { hv[0] = hit.x / l * sp; hv[1] = hit.y / l * sp; hv[2] = fmaxf(0, hit.z / l * sp) + sp * 0.4f; } else hv[2] = sp * 0.3f;
		CInput in2; memset(&in2, 0, sizeof(in2)); in2.grounded = 1; MBone *P0 = ca_pose(&PB.ca, &in2);
		if (rd_start(&PB.rd, B, P0, pre, 1 / 30.f, T, hv)) { PB.rd.collide = pbody_collide; PB.rd_ok = 1; pbody_pose(); } }
	{ float L[3]; light_at(PB.x, PB.y, PB.z + 20, L); memcpy(PB.lit, L, sizeof(L)); }
	// the red burst at the chest (footage v49 437.5 s: a red cloud around the falling body), a splat behind
	{ float l = sqrtf(vdot(hit, hit)); V3 d = l > 1e-3f ? vmul(hit, 1 / l) : v3(-cosf(yaw), -sinf(yaw), 0);
	  blood_hit(PB.chest, d); V3 n = vmul(d, -1);
	  for (int k = 0; k < 3; k++) { V3 p = vadd(PB.chest, v3(10 * (k - 1), 8 * (k & 1), 6 * k - 6)), q = v3(d.x + 0.35f * (k - 1), d.y - 0.35f * (k - 1), d.z + 0.25f); fx_impact(9, &p.x, &n.x, &q.x); } }
	PB.eye[0] = eye[0] - cosf(yaw) * 45; PB.eye[1] = eye[1] - sinf(yaw) * 45; PB.eye[2] = eye[2] + 8;   // (from the eyes the first frame was the back of the head)
	PB.eyaw = yaw; PB.epitch = vpitch; PB.cam_yaw = yaw;
	PB.on = 1; return 1;
}
static void pbody_stop(void) { PB.on = 0; }
static void pbody_update(float dt) {
	if (!PB.on) return; PB.t += dt;
	CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1;
	ca_update(&PB.ca, &in, dt);
	if (PB.rd_ok) {   // b82: the ragdoll (the clip's pose pulls it for its first RD_SOFT s)
		if (!PB.rd.asleep) { rd_update(&PB.rd, dt, PB.rd.t < RD_SOFT ? ca_pose(&PB.ca, &in) : NULL); pbody_pose(); PB.still = 0; }
		else if (!PB.still) { pbody_pose(); PB.still = 1; }   // at rest: the last pose, skinned once more
		float L[3]; light_at(PB.pelvis.x, PB.pelvis.y, PB.pelvis.z + 10, L); float k = fminf(1, 4 * dt); for (int c = 0; c < 3; c++) PB.lit[c] += (L[c] - PB.lit[c]) * k;
		return; }
	const CAnim *A = &PB.ca; int still = A->death.clip >= 0 && A->death.w >= 0.999f && A->death.t >= A->B->clip[A->death.clip].len + 0.1f;
	if (!still || !PB.still) pbody_pose(); PB.still = still;   // a body at rest keeps its pose and skinning
	// slid off an edge: drops to the floor under the pelvis (bots.h corpses)
	float h = raycast(v3(PB.pelvis.x, PB.pelvis.y, PB.z - PB.drop + 60), v3(0, 0, -1), 800, NULL, NULL), below = h > 0 ? h - 60 : 0;
	if (below > 3) { PB.vz += 1400 * dt; float s = fminf(below, PB.vz * dt); PB.drop += s; PB.still = 0; } else PB.vz = 0;
	float L[3]; light_at(PB.x, PB.y, PB.z - PB.drop + 20, L); float k = fminf(1, 4 * dt); for (int c = 0; c < 3; c++) PB.lit[c] += (L[c] - PB.lit[c]) * k;
}
// as a dead bot (bots_draw): lit like the floor under it, the rim light, its glow maps; the gun until the body is down
static void pbody_draw(const float *VP, const float *fogc, void (*mul)(float *, const float *, const float *)) {
	if (!PB.on || !PB.pose || !PB.sk.ok) return; ChrType *C = &chrt[PB.ct];
	float th = PB.yaw + 1.5707963f, c = cosf(th), s = sinf(th);
	float M[16] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, PB.x, PB.y, PB.z - PB.drop, 1 }, MVP[16]; mul(MVP, VP, M);
	float q[4]; for (int r = 0; r < 4; r++) q[r] = VP[r] * PB.pelvis.x + VP[4 + r] * PB.pelvis.y + VP[8 + r] * PB.pelvis.z + VP[12 + r];
	if (q[3] < -150 || q[0] > q[3] + 260 || q[0] < -q[3] - 260 || q[1] > q[3] + 400 || q[1] < -q[3] - 400) return;
	float vpy = sqrtf(VP[1] * VP[1] + VP[5] * VP[5] + VP[9] * VP[9]), px = q[3] > 1 ? 180 * vpy / q[3] * 272 : 1e9f;
	int lod = px > 150 ? 0 : px > 70 ? 1 : 2; if (lod != PB.lod) PB.sk.cur = -1; PB.lod = lod;
	if (PB.pose_new || PB.sk.cur < 0) { chr_skin_queue(&C->m, &PB.sk, lod, PB.pose); skin_run(); } PB.pose_new = 0;
	const float Lw[3] = { 0.33f, 0.42f, 0.84f }; float ld[4] = { c * Lw[0] + s * Lw[1], -s * Lw[0] + c * Lw[1], Lw[2], 0 };
	float tint[4] = { 1, 1, 1, 1 }; for (int k = 0; k < 3; k++) tint[k] = fminf(1.5f, fmaxf(0.18f, PB.lit[k] * 1.15f)) * 0.8f;
	md_rim = BOT_RIM; md_emis_k = 1; md_masked = 0;   // (main.c draws it between bots_draw and the shadows: no face culling)
	chr_draw(&C->m, &PB.sk, lod, MVP, PB.pose, ld, tint, fogc, 0);
	if (PB.wpn >= 0 && PB.ca.death.t <= 0.4f) chr_draw_rigid(&bot_w3[PB.wpn], MVP, PB.rd_ok ? &PB.rpose[PB.ca.B->grip] : &PB.ca.pose[PB.ca.B->grip], ld, tint, fogc);
	md_rim = 0;
}
// the camera (footage v49 437-441 s): from just behind the eyes it pulls back in ~0.5 s to ~2.1 m behind and ~1.4 m above the body,
// looking down at it, and drifts slowly round it; walls shorten the arm. el: seconds since the death
static void pbody_camera(float *pos, float *yaw, float *vpitch, float el) {
	V3 tg = v3(PB.pelvis.x, PB.pelvis.y, fmaxf(PB.pelvis.z, PB.z - PB.drop + 25) + 15);
	float k = el < 0.5f ? el / 0.5f : 1; k = k * k * (3 - 2 * k);
	float a = PB.cam_yaw + 0.11f * el, dist = 200, up = 185;   // (emulator b72-1: 2.7 m / 1.65 m left the body small; b82: higher, EP19 5:58-5:59.4 looks down ~45 deg)
	V3 dir = v3(-cosf(a) * dist, -sinf(a) * dist, up); float l = sqrtf(vdot(dir, dir)); V3 dn = vmul(dir, 1 / l);
	float h = raycast(tg, dn, l, NULL, NULL); if (h > 0) l = fmaxf(60, h - 25);
	V3 cam = vadd(tg, vmul(dn, l));
	V3 e = vsub(tg, cam); float hl = sqrtf(e.x * e.x + e.y * e.y) + 1e-3f, cy = atan2f(e.y, e.x), cp = atan2f(e.z, hl);
	pos[0] = PB.eye[0] + (cam.x - PB.eye[0]) * k; pos[1] = PB.eye[1] + (cam.y - PB.eye[1]) * k; pos[2] = PB.eye[2] + (cam.z - PB.eye[2]) * k;
	*yaw = PB.eyaw + wrap_pi(cy - PB.eyaw) * k; *vpitch = PB.epitch + (cp - PB.epitch) * k;
}
