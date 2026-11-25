// ragdoll.h - b82 (user, the Paz video EP19 5:56-6:06: "when the player gets shot ... they ragdoll"): a body that goes limp.
// The original (GIS_base_3rd.nmb, morpheme + PhysX 2) runs every death state as "death clip -> SoftKey_Ragdoll -> Ragdoll":
// the clip starts the fall, the physics body follows it softly, then the body is left to physics. Its physics rig (read as
// data from the network file; no morpheme or PhysX code) has 16 parts: pelvis, spine, spine1, spine2, neck, head, the two
// clavicles / upper arms / forearms / thighs / calves, each one capsule (e.g. upper arm r 4.5 cm x 22 cm, forearm 4.2 x 28,
// thigh 7.7 x 31, calf 5.0 x 52, head 7.3, pelvis 12), and joint limits (spine 5-15 deg swing, upper arm 20 / 37 deg swing
// +-25 twist, elbow / knee one axis).
// This is a clean-room position-based ragdoll of the same parts: 16 particles at the joints (sized like the rig's capsules),
// distance constraints along the bones and across the torso (it stays rigid), limits as minimum distances (elbows, knees,
// neck, hips) plus a hinge rule for the knees, self-collision between the limbs and the torso, and the world's player
// collision (a callback: the engine's sphere_resolve) with friction. Soft keyframing: for the first RD_SOFT seconds the
// particles are pulled towards the death clip's pose (fading), then nothing but gravity, the hit and the floor. The bones
// are rebuilt from the particles (torso frames from two directions, limbs swung from their parent, everything else rides
// along) into a bank pose that the usual skinning draws. Pure C on chr.h (host tests: tools/chars/host/ragdoll_test.c).
#pragma once
#include <math.h>
#include <string.h>
#define RD_NP 16
#define RD_NC 64
#define RD_SOFT 0.35f    // seconds of soft keyframing (the clip's own start of the fall)
#define RD_HZ 120.0f     // fixed physics steps
enum { RD_PELVIS, RD_SPINE, RD_NECK, RD_HEAD, RD_LSH, RD_LEL, RD_LWR, RD_RSH, RD_REL, RD_RWR, RD_LHIP, RD_LKN, RD_LAN, RD_RHIP, RD_RKN, RD_RAN };
typedef struct { unsigned char a, b, kind; float d, lo, hi; } RdCon;   // keep |a b| within [lo d, hi d]; kind 1: self-collision pair
typedef struct {
	int on, asleep, n, nc; float t, acc, still_t, soft;
	float x[RD_NP][3], xp[RD_NP][3], r[RD_NP], w[RD_NP];   // world positions, previous step's, radius, inverse mass
	float x0[RD_NP][3];                                     // model-space particles of the reference pose
	RdCon c[RD_NC];
	const CBank *B; int bone[RD_NP], hd_tip;               // bank bone of each particle; head: offset along the head bone
	MBone ref[CB_MAXB];                                     // the reference bank pose (model space): the pose at the start
	float T[5];                                             // model -> world: cos, sin of the turn, translation
	float fwd_sign;                                         // knee hinge: which way "forward" points from the particles
	int (*collide)(float *c, float r, float *n);            // push a sphere out of the world; 1 = touched (n: push direction)
	int contacts;                                           // (stats) particle contacts this frame
} Ragdoll;
static void rd_m2w(const Ragdoll *R, const float *m, float *w) { w[0] = R->T[0] * m[0] - R->T[1] * m[1] + R->T[2]; w[1] = R->T[1] * m[0] + R->T[0] * m[1] + R->T[3]; w[2] = m[2] + R->T[4]; }
static void rd_w2m(const Ragdoll *R, const float *w, float *m) { float x = w[0] - R->T[2], y = w[1] - R->T[3]; m[0] = R->T[0] * x + R->T[1] * y; m[1] = -R->T[1] * x + R->T[0] * y; m[2] = w[2] - R->T[4]; }
static float rd_len(const float *a) { return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
static void rd_sub(const float *a, const float *b, float *o) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static void rd_cross(const float *a, const float *b, float *o) { float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0]; o[0] = x; o[1] = y; o[2] = z; }
static float rd_dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void rd_norm(float *a) { float l = rd_len(a); if (l > 1e-6f) { a[0] /= l; a[1] /= l; a[2] /= l; } }
// the particle positions (model space) of a bank pose
static void rd_particles(const Ragdoll *R, const MBone *P, float (*out)[3]) {
	for (int i = 0; i < R->n; i++) { const MBone *b = &P[R->bone[i]]; memcpy(out[i], b->p, 12);
		if (i == RD_HEAD) { float ax[3] = { 1, 0, 0 }, v[3]; cq_rot(b->q, ax, v); for (int k = 0; k < 3; k++) out[i][k] += v[k] * R->hd_tip; } }
}
static void rd_con(Ragdoll *R, int a, int b, float lo, float hi, int kind) {
	if (R->nc >= RD_NC) return; float d[3]; rd_sub(R->x0[a], R->x0[b], d); RdCon *c = &R->c[R->nc++];
	c->a = (unsigned char)a; c->b = (unsigned char)b; c->kind = (unsigned char)kind; c->d = rd_len(d); c->lo = lo; c->hi = hi;
	if (kind == 1) { c->d = R->r[a] + R->r[b]; c->lo = 1; c->hi = 1e9f; }   // self-collision: never closer than the two radii
}
// B: the body's bank; P: its bank pose now (model space), P1: one step earlier (dt1 s: the starting velocity), T: model ->
// world (c, s, x, y, z), hit: the killing hit's push (world cm/s, added to the torso, less to the limbs). 0: bones missing.
static int rd_start(Ragdoll *R, const CBank *B, const MBone *P, const MBone *P1, float dt1, const float *T, const float *hit) {
	memset(R, 0, sizeof(*R)); R->B = B; R->n = RD_NP; memcpy(R->T, T, sizeof(R->T));
	static const char *const NM[RD_NP] = { "Bip01_Pelvis", "Bip01_Spine1", "Bip01_Neck", "Bip01_Head", "Bip01_L_UpperArm", "Bip01_L_Forearm", "Bip01_L_Hand",
		"Bip01_R_UpperArm", "Bip01_R_Forearm", "Bip01_R_Hand", "Bip01_L_Thigh", "Bip01_L_Calf", "Bip01_L_Foot", "Bip01_R_Thigh", "Bip01_R_Calf", "Bip01_R_Foot" };
	// radii from the rig's capsules (a little under them: the particles sit on the joints, the capsules between)
	static const float RAD[RD_NP] = { 12, 12, 7, 8, 5, 4.5f, 4.5f, 5, 4.5f, 4.5f, 8, 6.5f, 5, 8, 6.5f, 5 };
	static const float MASS[RD_NP] = { 4, 4, 1.5f, 1.5f, 1.2f, 0.9f, 0.5f, 1.2f, 0.9f, 0.5f, 2, 1.4f, 0.7f, 2, 1.4f, 0.7f };
	for (int i = 0; i < RD_NP; i++) { R->bone[i] = cb_find_bone(B, NM[i]); if (R->bone[i] < 0) return 0; R->r[i] = RAD[i]; R->w[i] = 1 / MASS[i]; }
	R->hd_tip = 9;   // the head particle: the middle of the head (the bone sits at its base; the rig's head capsule is 7 cm round)
	memcpy(R->ref, P, sizeof(MBone) * B->nb);
	rd_particles(R, P, R->x0);
	float xm1[RD_NP][3]; rd_particles(R, P1, xm1);
	const float h = 1 / RD_HZ;
	for (int i = 0; i < RD_NP; i++) {
		float wx[3], wp[3]; rd_m2w(R, R->x0[i], wx); rd_m2w(R, xm1[i], wp);
		float tor = (i <= RD_HEAD) ? 1 : (i == RD_LHIP || i == RD_RHIP) ? 0.85f : 0.6f;   // the hit pushes the torso hardest
		for (int k = 0; k < 3; k++) { float v = (wx[k] - wp[k]) / (dt1 > 1e-4f ? dt1 : 1) + (hit ? hit[k] * tor : 0); R->x[i][k] = wx[k]; R->xp[i][k] = wx[k] - v * h; }
	}
	// bones (rigid), the torso's braces (rigid), limits as minimum distances, self-collision
	static const unsigned char BONE[][2] = { { RD_PELVIS, RD_SPINE }, { RD_SPINE, RD_NECK }, { RD_NECK, RD_HEAD }, { RD_NECK, RD_LSH }, { RD_LSH, RD_LEL }, { RD_LEL, RD_LWR },
		{ RD_NECK, RD_RSH }, { RD_RSH, RD_REL }, { RD_REL, RD_RWR }, { RD_PELVIS, RD_LHIP }, { RD_LHIP, RD_LKN }, { RD_LKN, RD_LAN }, { RD_PELVIS, RD_RHIP }, { RD_RHIP, RD_RKN }, { RD_RKN, RD_RAN } };
	for (unsigned k = 0; k < sizeof(BONE) / 2; k++) rd_con(R, BONE[k][0], BONE[k][1], 1, 1, 0);
	static const unsigned char BRACE[][2] = { { RD_LSH, RD_RSH }, { RD_LHIP, RD_RHIP }, { RD_SPINE, RD_LSH }, { RD_SPINE, RD_RSH }, { RD_SPINE, RD_LHIP }, { RD_SPINE, RD_RHIP },
		{ RD_PELVIS, RD_LSH }, { RD_PELVIS, RD_RSH }, { RD_NECK, RD_LHIP }, { RD_NECK, RD_RHIP } };
	for (unsigned k = 0; k < sizeof(BRACE) / 2; k++) rd_con(R, BRACE[k][0], BRACE[k][1], 1, 1, 0);
	rd_con(R, RD_PELVIS, RD_NECK, 0.86f, 1.02f, 0);    // the spine bends a little (the rig: 5-15 deg a joint)
	rd_con(R, RD_SPINE, RD_HEAD, 0.80f, 1.03f, 0);     // the head nods, never folds into the chest
	rd_con(R, RD_LSH, RD_LWR, 0.42f, 1, 0); rd_con(R, RD_RSH, RD_RWR, 0.42f, 1, 0);   // elbows: up to ~130 deg bent
	rd_con(R, RD_LHIP, RD_LAN, 0.50f, 1, 0); rd_con(R, RD_RHIP, RD_RAN, 0.50f, 1, 0);   // knees
	rd_con(R, RD_LKN, RD_NECK, 0.55f, 1.2f, 0); rd_con(R, RD_RKN, RD_NECK, 0.55f, 1.2f, 0);   // the thighs don't fold up into the chest
	static const unsigned char SELF[][2] = { { RD_LEL, RD_SPINE }, { RD_LWR, RD_SPINE }, { RD_REL, RD_SPINE }, { RD_RWR, RD_SPINE }, { RD_LWR, RD_PELVIS }, { RD_RWR, RD_PELVIS },
		{ RD_LKN, RD_RKN }, { RD_LAN, RD_RAN }, { RD_LWR, RD_RWR }, { RD_LWR, RD_HEAD }, { RD_RWR, RD_HEAD }, { RD_LKN, RD_RAN }, { RD_RKN, RD_LAN } };
	for (unsigned k = 0; k < sizeof(SELF) / 2; k++) rd_con(R, SELF[k][0], SELF[k][1], 1, 1e9f, 1);
	// the knee hinge's forward: the character faces -y in model space; which sign of cross(side, up) that is
	{ float side[3], up[3], f[3]; rd_sub(R->x0[RD_LHIP], R->x0[RD_RHIP], side); rd_sub(R->x0[RD_SPINE], R->x0[RD_PELVIS], up); rd_cross(side, up, f); R->fwd_sign = f[1] < 0 ? 1 : -1; }
	R->on = 1; R->soft = 1; return 1;
}
static void rd_solve(Ragdoll *R) {
	for (int k = 0; k < R->nc; k++) {
		const RdCon *c = &R->c[k]; float *a = R->x[c->a], *b = R->x[c->b], d[3]; rd_sub(b, a, d); float l = rd_len(d); if (l < 1e-5f) continue;
		float lo = c->d * c->lo, hi = c->d * c->hi, tgt = l < lo ? lo : l > hi ? hi : l; if (tgt == l) continue;
		float wa = R->w[c->a], wb = R->w[c->b], s = (l - tgt) / (l * (wa + wb));
		for (int j = 0; j < 3; j++) { a[j] += d[j] * s * wa; b[j] -= d[j] * s * wb; }
	}
}
// knee hinge: the knee stays on the front side of the hip-ankle line (forward from the pelvis's own frame); once a step and
// partly (k), after the constraints (run inside every constraint pass it fought them and the body swung round on the floor)
static void rd_hinge(Ragdoll *R, float k) {
	float side[3], up[3], f[3]; rd_sub(R->x[RD_LHIP], R->x[RD_RHIP], side); rd_sub(R->x[RD_SPINE], R->x[RD_PELVIS], up); rd_cross(side, up, f); rd_norm(f);
	for (int j = 0; j < 3; j++) f[j] *= R->fwd_sign;
	for (int s = 0; s < 2; s++) { int h = s ? RD_RHIP : RD_LHIP, kn = h + 1, an = h + 2; float m[3], e[3];
		for (int j = 0; j < 3; j++) m[j] = (R->x[h][j] + R->x[an][j]) * 0.5f;
		rd_sub(R->x[kn], m, e); float bk = rd_dot(e, f);
		// (moved with their previous positions: a correction that also became speed fed the body energy - it jumped up 90 cm
		// after landing on its back in the host test)
		// (and balanced by the masses: an uneven push moved the whole body a centimetre a tenth of a second - it crawled; a
		// nearly straight leg, within 1.5 cm of the line, is left alone)
		if (bk < -1.5f) { bk += 1.5f; if (bk < -3) bk = -3;
			float mk = 1 / R->w[kn], mh = 1 / R->w[h], ma = 1 / R->w[an], tot = mk + mh + ma, sk = -bk * k * (mh + ma) / tot, so = bk * k * mk / tot;
			for (int j = 0; j < 3; j++) { float dk = f[j] * sk, dh = f[j] * so;
				R->x[kn][j] += dk; R->xp[kn][j] += dk; R->x[h][j] += dh; R->xp[h][j] += dh; R->x[an][j] += dh; R->xp[an][j] += dh; } } }
}
// one frame: fixed steps at RD_HZ; target: the death clip's pose (model space bank pose, NULL after the soft phase)
static void rd_update(Ragdoll *R, float dt, const MBone *target) {
	if (!R->on || R->asleep) return;
	R->t += dt; R->acc += dt; if (R->acc > 0.1f) R->acc = 0.1f;
	float tx[RD_NP][3]; int soft = target && R->t < RD_SOFT;
	if (soft) { float m[RD_NP][3]; rd_particles(R, target, m); for (int i = 0; i < R->n; i++) rd_m2w(R, m[i], tx[i]); }
	const float h = 1 / RD_HZ, g = 1250;   // (a little over 9.8 m/s2: the fall reads as weight, as games' ragdolls do)
	R->contacts = 0; float vmax = 0;
	while (R->acc >= h) {
		R->acc -= h;
		for (int i = 0; i < R->n; i++) { float *x = R->x[i], *p = R->xp[i], v[3];
			for (int j = 0; j < 3; j++) v[j] = (x[j] - p[j]) * 0.996f;
			v[2] -= g * h * h;
			for (int j = 0; j < 3; j++) { p[j] = x[j]; x[j] += v[j]; } }
		if (soft) {   // soft keyframing: towards the clip's pose, fading out
			float k = 1 - R->t / RD_SOFT; k = 0.35f * k * k;
			for (int i = 0; i < R->n; i++) for (int j = 0; j < 3; j++) { float d = (tx[i][j] - R->x[i][j]) * k; R->x[i][j] += d; R->xp[i][j] += d * 0.5f; } }
		for (int pass = 0; pass < 3; pass++) {   // constraints and the world in turns (a single collision pass at the end let the
			rd_solve(R); rd_solve(R);              // constraints push parts back into the floor: up to 11 cm, then bounces)
			if (R->collide) for (int i = 0; i < R->n; i++) {
				float c[3] = { R->x[i][0], R->x[i][1], R->x[i][2] }, n[3] = { 0, 0, 0 };
				if (!R->collide(c, R->r[i], n)) continue;
				if (pass == 2) R->contacts++;
				float push[3]; rd_sub(c, R->x[i], push); float pl = rd_len(push); if (pl > 1e-5f) for (int j = 0; j < 3; j++) n[j] = push[j] / pl;
				// friction, no bounce: the step's own motion (before the push: the push itself must not turn into speed) loses
				// its part into the surface and most of its slide
				float v[3]; rd_sub(R->x[i], R->xp[i], v); memcpy(R->x[i], c, 12); float vn = rd_dot(v, n), vt[3];
				for (int j = 0; j < 3; j++) vt[j] = v[j] - n[j] * vn;
				float fr = n[2] > 0.5f ? 0.5f : 0.15f;   // floors grip, walls let it slide down
				for (int j = 0; j < 3; j++) R->xp[i][j] = R->x[i][j] - (vt[j] * (1 - fr) + (vn > 0 ? n[j] * vn : 0));
			}
		}
		rd_hinge(R, 0.3f);
		for (int i = 0; i < R->n; i++) { float v[3]; rd_sub(R->x[i], R->xp[i], v); float s = rd_len(v) * RD_HZ; if (s > vmax) vmax = s; }
	}
	// at rest: it stops (the pose and skinning stay)
	if (!soft && vmax < 12) { R->still_t += dt; if (R->still_t > 0.4f) R->asleep = 1; } else R->still_t = 0;
	if (R->t > 4.5f) R->asleep = 1;   // (a hip still settling against the knee hinge after 4.5 s: frozen as it lies)
}
// quaternion helpers (x, y, z, w; chr.h's cq_mul / cq_rot)
static void rd_q_arc(const float *a, const float *b, float *q) {   // shortest rotation taking unit a onto unit b
	float c[3]; rd_cross(a, b, c); float d = rd_dot(a, b);
	if (d < -0.9999f) { float ax[3] = { 1, 0, 0 }; if (fabsf(a[0]) > 0.9f) { ax[0] = 0; ax[1] = 1; } rd_cross(a, ax, c); rd_norm(c); q[0] = c[0]; q[1] = c[1]; q[2] = c[2]; q[3] = 0; return; }
	float s = sqrtf((1 + d) * 2), is = 1 / s; q[0] = c[0] * is; q[1] = c[1] * is; q[2] = c[2] * is; q[3] = s * 0.5f;
}
static void rd_q_from_m(const float m[3][3], float *q) {   // rows / columns: m[r][c], a rotation matrix
	float tr = m[0][0] + m[1][1] + m[2][2];
	if (tr > 0) { float s = sqrtf(tr + 1) * 2; q[3] = 0.25f * s; q[0] = (m[2][1] - m[1][2]) / s; q[1] = (m[0][2] - m[2][0]) / s; q[2] = (m[1][0] - m[0][1]) / s; }
	else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) { float s = sqrtf(1 + m[0][0] - m[1][1] - m[2][2]) * 2; q[3] = (m[2][1] - m[1][2]) / s; q[0] = 0.25f * s; q[1] = (m[0][1] + m[1][0]) / s; q[2] = (m[0][2] + m[2][0]) / s; }
	else if (m[1][1] > m[2][2]) { float s = sqrtf(1 + m[1][1] - m[0][0] - m[2][2]) * 2; q[3] = (m[0][2] - m[2][0]) / s; q[0] = (m[0][1] + m[1][0]) / s; q[1] = 0.25f * s; q[2] = (m[1][2] + m[2][1]) / s; }
	else { float s = sqrtf(1 + m[2][2] - m[0][0] - m[1][1]) * 2; q[3] = (m[1][0] - m[0][1]) / s; q[0] = (m[0][2] + m[2][0]) / s; q[1] = (m[1][2] + m[2][1]) / s; q[2] = 0.25f * s; }
	float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]); for (int k = 0; k < 4; k++) q[k] /= l;
}
static void rd_frame(const float *aim, const float *side, float m[3][3]) {   // columns: aim, side (made orthogonal), their cross
	float x[3] = { aim[0], aim[1], aim[2] }, y[3], z[3]; rd_norm(x); rd_cross(x, side, z); rd_norm(z); rd_cross(z, x, y);
	for (int r = 0; r < 3; r++) { m[r][0] = x[r]; m[r][1] = y[r]; m[r][2] = z[r]; }
}
static void rd_q_delta(const float now[3][3], const float ref[3][3], float *q) {   // now * ref^T
	float m[3][3]; for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) m[r][c] = now[r][0] * ref[c][0] + now[r][1] * ref[c][1] + now[r][2] * ref[c][2];
	rd_q_from_m(m, q);
}
static void rd_q_slerp(const float *a, const float *b, float t, float *o) {
	float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], bb[4] = { b[0], b[1], b[2], b[3] };
	if (d < 0) { d = -d; for (int k = 0; k < 4; k++) bb[k] = -bb[k]; }
	if (d > 0.9995f) { for (int k = 0; k < 4; k++) o[k] = a[k] + (bb[k] - a[k]) * t; }
	else { float th = acosf(d), s = sinf(th), wa = sinf((1 - t) * th) / s, wb = sinf(t * th) / s; for (int k = 0; k < 4; k++) o[k] = a[k] * wa + bb[k] * wb; }
	float l = sqrtf(o[0] * o[0] + o[1] * o[1] + o[2] * o[2] + o[3] * o[3]); for (int k = 0; k < 4; k++) o[k] /= l;
}
// the bank pose (model space) of the body now: P (B->nb bones)
static void rd_pose(const Ragdoll *R, MBone *P) {
	const CBank *B = R->B; int nb = B->nb; float xm[RD_NP][3]; for (int i = 0; i < R->n; i++) rd_w2m(R, R->x[i], xm[i]);
	float D[CB_MAXB][4]; unsigned char set[CB_MAXB]; memset(set, 0, sizeof(set));
	// torso frames: pelvis (up the spine, across the hips), chest (up the neck, across the shoulders)
	float a0[3], s0[3], a1[3], s1[3], F0[3][3], F1[3][3], qp[4], qc[4];
	rd_sub(R->x0[RD_SPINE], R->x0[RD_PELVIS], a0); rd_sub(R->x0[RD_LHIP], R->x0[RD_RHIP], s0); rd_frame(a0, s0, F0);
	rd_sub(xm[RD_SPINE], xm[RD_PELVIS], a1); rd_sub(xm[RD_LHIP], xm[RD_RHIP], s1); rd_frame(a1, s1, F1); rd_q_delta(F1, F0, qp);
	rd_sub(R->x0[RD_NECK], R->x0[RD_SPINE], a0); rd_sub(R->x0[RD_LSH], R->x0[RD_RSH], s0); rd_frame(a0, s0, F0);
	rd_sub(xm[RD_NECK], xm[RD_SPINE], a1); rd_sub(xm[RD_LSH], xm[RD_RSH], s1); rd_frame(a1, s1, F1); rd_q_delta(F1, F0, qc);
	int pel = B->pelvis, sp0 = B->spine[0], sp1 = B->spine[1], sp2 = B->spine[2];
	memcpy(D[pel], qp, 16); set[pel] = 1;
	rd_q_slerp(qp, qc, 0.35f, D[sp0]); set[sp0] = 1; rd_q_slerp(qp, qc, 0.7f, D[sp1]); set[sp1] = 1; memcpy(D[sp2], qc, 16); set[sp2] = 1;
	// swung bones: (bone, its particle, the particle it points at, the bone whose delta it starts from)
	struct { int bone, p0, p1, from; float share; } SW[] = {
		{ R->bone[RD_NECK], RD_NECK, RD_HEAD, sp2, 0.5f }, { B->head, RD_NECK, RD_HEAD, sp2, 1 },
		{ R->bone[RD_LSH], RD_LSH, RD_LEL, sp2, 1 }, { R->bone[RD_LEL], RD_LEL, RD_LWR, R->bone[RD_LSH], 1 },
		{ R->bone[RD_RSH], RD_RSH, RD_REL, sp2, 1 }, { R->bone[RD_REL], RD_REL, RD_RWR, R->bone[RD_RSH], 1 },
		{ R->bone[RD_LHIP], RD_LHIP, RD_LKN, pel, 1 }, { R->bone[RD_LKN], RD_LKN, RD_LAN, R->bone[RD_LHIP], 1 },
		{ R->bone[RD_RHIP], RD_RHIP, RD_RKN, pel, 1 }, { R->bone[RD_RKN], RD_RKN, RD_RAN, R->bone[RD_RHIP], 1 } };
	for (int b = 0; b < nb; b++) {
		int k = -1; for (unsigned j = 0; j < sizeof(SW) / sizeof(SW[0]); j++) if (SW[j].bone == b) { k = (int)j; break; }
		if (set[b]) continue;
		if (k >= 0) {
			const float *df = set[SW[k].from] ? D[SW[k].from] : qp;
			float d0[3], d1[3], dp[3], r[4], ri[4] = { 0, 0, 0, 1 }; rd_sub(R->x0[SW[k].p1], R->x0[SW[k].p0], d0); rd_norm(d0); cq_rot(df, d0, dp);
			rd_sub(xm[SW[k].p1], xm[SW[k].p0], d1); rd_norm(d1); rd_q_arc(dp, d1, r);
			if (SW[k].share < 1) { float t[4]; rd_q_slerp(ri, r, SW[k].share, t); memcpy(r, t, 16); }
			cq_mul(r, df, D[b]); set[b] = 1; continue;
		}
		if (!strcmp(B->bone[b], "Bip01_L_Clavicle") || !strcmp(B->bone[b], "Bip01_R_Clavicle")) { memcpy(D[b], D[sp2], 16); set[b] = 1; continue; }   // with the chest
		int p = B->parent[b]; if (p >= 0 && set[p]) { memcpy(D[b], D[p], 16); set[b] = 1; }   // rides along with its parent
	}
	for (int b = 0; b < nb; b++) if (!set[b]) { memcpy(D[b], qp, 16); set[b] = 1; }   // (the root and anything above the pelvis: with the pelvis)
	// rotations, then positions: the pelvis at its particle, the rest hanging from their parents (bone lengths kept)
	for (int b = 0; b < nb; b++) cq_mul(D[b], R->ref[b].q, P[b].q);
	memcpy(P[pel].p, xm[RD_PELVIS], 12);
	{ int r = B->root; if (r >= 0 && r != pel) { float o[3], v[3]; rd_sub(R->ref[r].p, R->ref[pel].p, o); cq_rot(D[pel], o, v); for (int j = 0; j < 3; j++) P[r].p[j] = P[pel].p[j] + v[j]; } }
	for (int b = 0; b < nb; b++) {
		int p = B->parent[b]; if (b == pel || p < 0) continue;
		float o[3], v[3]; rd_sub(R->ref[b].p, R->ref[p].p, o); cq_rot(D[p], o, v); for (int j = 0; j < 3; j++) P[b].p[j] = P[p].p[j] + v[j];
	}
}
