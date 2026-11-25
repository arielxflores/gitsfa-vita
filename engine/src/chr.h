// Third-person characters: the original's animation clips (shared banks chr_anim_m.gab / chr_anim_f.gab built by
// tools/anim/make_bank.py from the morpheme network binaries), blended in local bone space the way the original
// network does it (tools/anim/make_bank.py: file layout; slots.py: which network node feeds each role), FK into character space (z up, cm, the character faces -y), and drawn
// with distance LODs and CPU skinning (skin.h: NEON, on the jobs.h worker cores).
//
//   CBank   one per skeleton (male / female): bones, clips (int16 local rotations, root + animated positions),
//           per-weapon "animation sets" (slot -> clip), clip events (foot plants, sounds)
//   CAnim   per character instance: locomotion (8 directions x walk/run x stand/crouch, phase-synced as the original's
//           footstep sync tracks), idle/aim/turn, sprint (lean + diagonal), jump/fall/land, climb/vault, slide,
//           upper-body actions (fire, reload, swap, hit reactions, grenade, skill, plant/defuse), deaths, aim pitch
//   CSkin   per instance software-skinning buffers (shared by the LODs): chr_skin_queue + skin_run, chr_draw
#pragma once
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef CHR_CLOCK_US
#define CHR_CLOCK_US() 0ULL   // microsecond clock for the skinning statistics (bots.h: sceKernelGetProcessTimeWide)
#endif
#define CHR_API static __attribute__((unused))   // API functions a build may not call

#define CB_MAXB 48
typedef struct { float q[4], p[3]; } CLoc;   // a local transform (rotation x,y,z,w; translation)
typedef struct {
	char name[32]; int nf, flags; float fps, len, speed, phase, dir[2];
	const short *q[CB_MAXB]; const float *p[CB_MAXB]; unsigned char qa[CB_MAXB], pa[CB_MAXB];   // track data, animated flags
	int ev0, nev;
} CClip;
enum { CE_LFOOT = 1, CE_RFOOT = 2, CE_SOUND = 3, CE_EFFECT = 4 };
typedef struct { float t; int clip, kind; const char *str; } CEvent;
typedef struct {
	int ok, nb, nclip, nset, nslot;
	char bone[CB_MAXB][32]; int parent[CB_MAXB];
	CClip *clip; short *set; char (*setname)[16]; char (*slotname)[16];
	CEvent *ev; int nev; unsigned char *blob; char *strs;
	int root, pelvis, spine[3], neck, head, grip, lhand, rhand, lfoot, rfoot;   // bone indices
	float upper[CB_MAXB], spine_mask[CB_MAXB];   // upper-body feather mask (weapon layer), aim-pitch share per bone
} CBank;

static int cb_find_bone(const CBank *B, const char *n) { for (int i = 0; i < B->nb; i++) if (!strcmp(B->bone[i], n)) return i; return -1; }
static int cb_find_slot(const CBank *B, const char *n) { for (int i = 0; i < B->nslot; i++) if (!strncmp(B->slotname[i], n, 16)) return i; return -1; }
static int cb_find_set(const CBank *B, const char *n) { for (int i = 0; i < B->nset; i++) if (!strncmp(B->setname[i], n, 16)) return i; return -1; }
static int cb_clip(const CBank *B, int set, int slot) {   // the clip of a slot in a set (the rifle set when the set has none)
	if (slot < 0 || slot >= B->nslot) return -1;
	int c = set >= 0 && set < B->nset ? B->set[set * B->nslot + slot] : -1;
	if (c < 0) c = B->set[slot];
	return c;
}

static unsigned cb_u32(const unsigned char *p) { unsigned v; memcpy(&v, p, 4); return v; }
static int cb_load(CBank *B, const char *path) {
	memset(B, 0, sizeof(*B));
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	unsigned char *d = malloc(n); if (!d) { fclose(f); return -4; }
	if (fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return -2; } fclose(f);
	if (memcmp(d, "GAB1", 4) || cb_u32(d + 4) != 1) { free(d); return -2; }
	B->nb = cb_u32(d + 8); B->nclip = cb_u32(d + 12); B->nset = cb_u32(d + 16); B->nslot = cb_u32(d + 20);
	if (B->nb > CB_MAXB) { free(d); return -3; }
	const unsigned char *p = d + 24;
	for (int i = 0; i < B->nb; i++, p += 36) { memcpy(B->bone[i], p, 32); B->bone[i][31] = 0; B->parent[i] = (int)cb_u32(p + 32); }
	B->slotname = (char (*)[16])p; p += 16 * B->nslot;
	int setrec = (16 + 2 * B->nslot + 3) & ~3;
	B->setname = malloc(16 * B->nset); B->set = malloc(2 * B->nset * B->nslot);
	for (int s = 0; s < B->nset; s++, p += setrec) { memcpy(B->setname[s], p, 16); memcpy(&B->set[s * B->nslot], p + 16, 2 * B->nslot); }
	B->clip = calloc(B->nclip, sizeof(CClip));
	int hsz = 68 + 8 * B->nb;   // name[32] u16 nf u16 flags f32 fps speed phase dir[2] u32 off len nquat, rot[nb] pos[nb]
	const unsigned char *data = p + hsz * B->nclip;
	for (int c = 0; c < B->nclip; c++, p += hsz) {
		CClip *C = &B->clip[c]; memcpy(C->name, p, 32); C->name[31] = 0;
		unsigned short nf, fl; memcpy(&nf, p + 32, 2); memcpy(&fl, p + 34, 2); C->nf = nf; C->flags = fl;
		memcpy(&C->fps, p + 36, 4); memcpy(&C->speed, p + 40, 4); memcpy(&C->phase, p + 44, 4); memcpy(C->dir, p + 48, 8);
		unsigned off = cb_u32(p + 56), nq = cb_u32(p + 64);
		const short *qb = (const short *)(data + off); const float *pb = (const float *)(data + off + 8 * nq);
		for (int b = 0; b < B->nb; b++) {
			unsigned r = cb_u32(p + 68 + 4 * b), t = cb_u32(p + 68 + 4 * B->nb + 4 * b);
			C->q[b] = qb + 4 * (r & 0x7fffffff); C->qa[b] = r >> 31;
			C->p[b] = pb + 3 * (t & 0x7fffffff); C->pa[b] = t >> 31;
		}
		C->len = C->nf > 1 ? (C->nf - 1) / C->fps : 0.001f;
	}
	// events and strings after the clip data
	const unsigned char *q = data; unsigned last = 0;
	{ const unsigned char *h = p - hsz * B->nclip; for (int c = 0; c < B->nclip; c++, h += hsz) { unsigned e = cb_u32(h + 56) + cb_u32(h + 60); e = (e + 3) & ~3u; if (e > last) last = e; } }
	q = data + last;
	if (q + 4 <= d + n) {
		B->nev = cb_u32(q); q += 4; const unsigned char *er = q; q += 12 * B->nev;
		unsigned ns = cb_u32(q); q += 4; B->strs = (char *)q;
		B->ev = calloc(B->nev + 1, sizeof(CEvent));
		for (int i = 0; i < B->nev; i++, er += 12) {
			CEvent *E = &B->ev[i]; memcpy(&E->t, er, 4); unsigned short ci, so; memcpy(&ci, er + 4, 2); E->kind = er[6]; memcpy(&so, er + 8, 2);
			E->clip = ci; E->str = so < ns ? B->strs + so : "";
			if (ci < B->nclip) { CClip *C = &B->clip[ci]; if (!C->nev) C->ev0 = i; C->nev++; }
		}
	}
	B->blob = d;
	B->root = cb_find_bone(B, "Bip01"); B->pelvis = cb_find_bone(B, "Bip01_Pelvis");
	B->spine[0] = cb_find_bone(B, "Bip01_Spine"); B->spine[1] = cb_find_bone(B, "Bip01_Spine1"); B->spine[2] = cb_find_bone(B, "Bip01_Spine2");
	B->neck = cb_find_bone(B, "Bip01_Neck"); B->head = cb_find_bone(B, "Bip01_Head"); B->grip = cb_find_bone(B, "Grip");
	B->lhand = cb_find_bone(B, "Bip01_L_Hand"); B->rhand = cb_find_bone(B, "Bip01_R_Hand");
	B->lfoot = cb_find_bone(B, "Bip01_L_Foot"); B->rfoot = cb_find_bone(B, "Bip01_R_Foot");
	if (B->root < 0 || B->pelvis < 0 || B->spine[0] < 0 || B->head < 0 || B->grip < 0) { free(d); return -5; }
	// upper body: the spine chain feathers in (the original blends the weapon layer with a feather blend), arms, head and gun fully
	for (int b = 0; b < B->nb; b++) {
		int up = 0; for (int k = b; k >= 0; k = B->parent[k]) if (k == B->spine[0]) { up = 1; break; }
		B->upper[b] = up ? 1.f : 0.f;
	}
	B->upper[B->spine[0]] = 0.35f; B->upper[B->spine[1]] = 0.7f;
	B->spine_mask[B->spine[0]] = 0.3f; B->spine_mask[B->spine[1]] = 0.3f; B->spine_mask[B->spine[2]] = 0.4f;
	B->ok = 1;
	return 0;
}
CHR_API void cb_free(CBank *B) { if (!B->ok) return; free(B->blob); free(B->clip); free(B->set); free(B->setname); free(B->ev); memset(B, 0, sizeof(*B)); }

// ---- sampling and blending (local space)
static inline void cq_norm(float *q) { float l = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]; l = l > 1e-12f ? 1.0f / sqrtf(l) : 0; q[0] *= l; q[1] *= l; q[2] *= l; q[3] *= l; }
// clip time (s) -> frame pair; loop: wrap, else clamp
static inline void cb_frame(const CClip *C, float t, int loop, int *i0, int *i1, float *a) {
	float fr = t * C->fps; int n = C->nf;
	if (n < 2) { *i0 = *i1 = 0; *a = 0; return; }
	if (loop) { float L = (float)(n - 1); fr = fmodf(fr, L); if (fr < 0) fr += L; }
	else { if (fr < 0) fr = 0; if (fr > n - 1) fr = (float)(n - 1); }
	int k = (int)fr; if (k > n - 2) k = n - 2; *i0 = k; *i1 = k + 1; *a = fr - k; if (*a > 1) *a = 1;
}
static void cb_sample(const CBank *B, int ci, float t, int loop, CLoc *out) {
	const CClip *C = &B->clip[ci]; int i0, i1; float a; cb_frame(C, t, loop, &i0, &i1, &a);
	const float s = 1.0f / 32767;
	for (int b = 0; b < B->nb; b++) {
		CLoc *o = &out[b];
		if (C->qa[b]) {
			const short *A = C->q[b] + 4 * i0, *Q = C->q[b] + 4 * i1;
			for (int k = 0; k < 4; k++) o->q[k] = (A[k] + (Q[k] - A[k]) * a) * s;   // neighbouring frames share a hemisphere (make_bank.py)
			cq_norm(o->q);
		} else { const short *A = C->q[b]; for (int k = 0; k < 4; k++) o->q[k] = A[k] * s; }
		if (C->pa[b]) { const float *A = C->p[b] + 3 * i0, *P = C->p[b] + 3 * i1; for (int k = 0; k < 3; k++) o->p[k] = A[k] + (P[k] - A[k]) * a; }
		else memcpy(o->p, C->p[b], 12);
	}
}
// dst = nlerp(dst, src, w * mask[b])
static void cb_blend(int nb, CLoc *dst, const CLoc *src, float w, const float *mask) {
	if (w <= 0.001f) return;
	if (w >= 0.999f && !mask) { memcpy(dst, src, nb * sizeof(CLoc)); return; }
	for (int b = 0; b < nb; b++) {
		float wb = mask ? w * mask[b] : w; if (wb <= 0.001f) continue;
		float *q = dst[b].q; const float *r = src[b].q, d = q[0] * r[0] + q[1] * r[1] + q[2] * r[2] + q[3] * r[3], sw = d < 0 ? -wb : wb;
		for (int k = 0; k < 4; k++) q[k] = q[k] * (1 - wb) + r[k] * sw;
		cq_norm(q);
		for (int k = 0; k < 3; k++) dst[b].p[k] += (src[b].p[k] - dst[b].p[k]) * wb;
	}
}
// running weighted blend: pass the accumulated weight so far; the first call copies
static void cb_accum(const CBank *B, CLoc *acc, float *wacc, CLoc *tmp, int ci, float t, int loop, float w) {
	if (ci < 0 || w <= 0.002f) return;
	if (*wacc <= 0) { cb_sample(B, ci, t, loop, acc); *wacc = w; return; }
	cb_sample(B, ci, t, loop, tmp); *wacc += w; cb_blend(B->nb, acc, tmp, w / *wacc, NULL);
}

// ---- FK: local -> character space (MBone, model.h). pitch: aim pitch (rad, + up) shared over the spine about the
// character's lateral axis (x), as the original's GunAimIK leans the upper body into the aim
static inline void cq_mul(const float *a, const float *b, float *o) {
	float ax = a[0], ay = a[1], az = a[2], aw = a[3], bx = b[0], by = b[1], bz = b[2], bw = b[3];
	o[0] = aw * bx + ax * bw + ay * bz - az * by; o[1] = aw * by - ax * bz + ay * bw + az * bx;
	o[2] = aw * bz + ax * by - ay * bx + az * bw; o[3] = aw * bw - ax * bx - ay * by - az * bz;
}
static inline void cq_rot(const float *q, const float *v, float *o) {
	float x = q[0], y = q[1], z = q[2], w = q[3];
	float tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
	o[0] = v[0] + w * tx + (y * tz - z * ty); o[1] = v[1] + w * ty + (z * tx - x * tz); o[2] = v[2] + w * tz + (x * ty - y * tx);
}
static void cb_fk(const CBank *B, const CLoc *L, float pitch, MBone *out) {
	float e[CB_MAXB][4]; int ne = 0; (void)ne;
	for (int b = 0; b < B->nb; b++) {
		int p = B->parent[b]; MBone *o = &out[b];
		if (p < 0) { memcpy(o->q, L[b].q, 16); memcpy(o->p, L[b].p, 12); }
		else { cq_mul(out[p].q, L[b].q, o->q); float v[3]; cq_rot(out[p].q, L[b].p, v); for (int k = 0; k < 3; k++) o->p[k] = out[p].p[k] + v[k]; }
		float sh = B->spine_mask[b];
		if (sh > 0 && fabsf(pitch) > 1e-3f) {   // extra rotation about x through this joint: aim up = rotate -y towards +z
			float h = -pitch * sh * 0.5f, r[4] = { sinf(h), 0, 0, cosf(h) }, q[4];
			cq_mul(r, o->q, q); memcpy(o->q, q, 16);
		}
		(void)e;
	}
}
// b63: the menus' out-of-game poses (chr_og_m.gab / chr_og_f.gab, tools/anim/make_og_bank.py: the original's
// GIS_*_3rd_OutGame networks): one-shot clip c0 (an operator's "choice" pose when picked, a result's win / lose), then the
// looping c1 (its idle), crossfaded over xf s; t = seconds since c0 started (c0 < 0: c1 alone). Writes character-space
// bones (bank order) to out; L / T: scratch (CB_MAXB each). Returns 0 when there is nothing to play.
CHR_API int cb_pose_seq(const CBank *B, int c0, int c1, float t, float xf, CLoc *L, CLoc *T, MBone *out) {
	if (!B || !B->ok || (c0 < 0 && c1 < 0)) return 0;
	if (c0 >= B->nclip) c0 = -1; if (c1 >= B->nclip) c1 = -1; if (c0 < 0 && c1 < 0) return 0;
	float l0 = c0 >= 0 ? B->clip[c0].len : 0; if (xf > l0 * 0.5f) xf = l0 * 0.5f; if (t < 0) t = 0;
	float t1 = t - (l0 - xf);   // the loop starts as the crossfade begins
	if (c0 >= 0 && (t < l0 || c1 < 0)) {
		cb_sample(B, c0, t, 0, L);
		if (c1 >= 0 && t1 > 0 && xf > 1e-3f) { cb_sample(B, c1, t1, 1, T); float k = t1 / xf; k = k * k * (3 - 2 * k); cb_blend(B->nb, L, T, k, NULL); }
	} else cb_sample(B, c1, t1 > 0 ? t1 : t, 1, L);
	cb_fk(B, L, 0, out); return 1;
}

// ---- the animator
enum { CA_NONE, CA_UPPER, CA_FULL };   // action masks
typedef struct {
	float vf, vr;          // velocity in the character's frame (cm/s): forward, right
	float yaw_rate;        // rad/s, + = turning left (counter-clockwise from above)
	int grounded, crouch, sprint, slide;
	int jump;              // 1 on the frame the character took off with a jump
	float air_t;           // seconds in the air
	int mantle; float mantle_k;   // 0 / 1 climb / 2 vault, progress 0..1
	float zoom;            // 0..1 aiming down sights
	float aim_pitch;       // rad, + up
} CInput;
typedef struct { int clip; float t, rate, w, fin, fout; int mask, loop, hold; short seq[10]; int nseq; } CAct;   // seq: clips that follow, gap-free
typedef void (*CEventFn)(void *user, int kind, const char *str, int foot);
typedef struct {
	const CBank *B; int set;
	// locomotion
	float phase, move_w, crouch_w, zoom_w, sprint_w, idle_t, turn_w, turn_t, turn_dir, lean, diag, dir_ang, run_w;
	int dom_clip; float dom_t_prev;   // the heaviest locomotion clip and its normalised time (foot events)
	// air / mantle / slide (full body, on top of locomotion)
	int air_state; float air_t, air_w; int air_sprint; float land_t, land_w; int land_clip;
	int mantle_kind; float mantle_w; int slide_on; float slide_t, slide_w, slide_end_t;
	// actions
	CAct up;      // upper-body action (reload, swap, hit, grenade, skill ...)
	CAct fire;    // the recoil of each shot (upper body)
	CAct full;    // full-body one-shots (plant, CQC ...)
	int dead; CAct death; float death_scale, death_p0[2];   // death: the root's travel scaled about its start (walls)
	// output
	CLoc loc[CB_MAXB], tmp[CB_MAXB], tmp2[CB_MAXB];
	MBone pose[CB_MAXB];
	CEventFn on_event; void *user;
	int was_ground; float last_pitch;
	float spr_end_t, spr_peak;   // seconds since the sprint ended, top speed of that sprint (the sprint-stop skid)
	float spd_last;              // ground speed of the previous update (deceleration)
} CAnim;

static void ca_init(CAnim *A, const CBank *B, int set) {
	memset(A, 0, sizeof(*A)); A->B = B; A->set = set; A->was_ground = 1; A->dom_clip = -1; A->death_scale = 1; A->spr_end_t = 99;
	A->up.clip = A->fire.clip = A->full.clip = A->death.clip = -1;
}
CHR_API int ca_slot(const CAnim *A, const char *slot) { return cb_clip(A->B, A->set, cb_find_slot(A->B, slot)); }
// slot ids looked up once per bank (names in tools/anim/slots.py)
enum { S_IDLE, S_ZOOM, S_CIDLE, S_CZOOM, S_TURNL, S_TURNR, S_CTURNL, S_CTURNR,
	S_WALKF, S_WALKB, S_WALKL, S_WALKR, S_RUNF, S_RUNB, S_RUNL, S_RUNR, S_CWALKF, S_CWALKB, S_CWALKL, S_CWALKR, S_CRUNF, S_CRUNB, S_CRUNL, S_CRUNR,
	S_SPRINT, S_SPRLL, S_SPRLR, S_SPRFL, S_SPRFR, S_SPRACC, S_SPRSTOP2, S_SPRSTOP4, S_SLIDE, S_SLIDEEND, S_SLIDEZOOM, S_SLIDEFIRE,
	S_JUMP, S_FALL, S_LAND, S_SJUMP1, S_SFALL1, S_LAND1, S_SJUMP2, S_SFALL2, S_LAND2,
	S_CLIMBRDY, S_CLIMBST, S_CLIMBCAT, S_CLIMBUP, S_CLIMBLND, S_WINRDY, S_WINUP, S_WINBRK, S_WINLND,
	S_FIRE, S_FIREZOOM, S_RELOAD, S_RLSTART, S_RLLOOP, S_RLEND, S_SWAPST, S_SWAPEND, S_HIT1, S_HIT2, S_HIT3, S_HITFLASH, S_HITEXP,
	S_SKILL, S_PLANTST, S_PLANTEND, S_DEFUSEST, S_DEFUSEEND, S_COOK, S_THROW, S_CQCBRDY, S_CQCB, S_CQCFRDY, S_CQCF,
	S_D_HF1, S_D_HF2, S_D_HB1, S_D_HB2, S_D_HS1, S_D_HS2, S_D_BF1, S_D_BF2, S_D_BB1, S_D_BB2, S_D_BS1, S_D_BS2, S_D_SPR, S_D_EXP,
	S_D_XHEAD, S_D_XLARM, S_D_XRARM, S_D_XLLEG, S_D_XRLEG, S_D_CQCBRDY, S_D_CQCB, S_D_CQCFRDY, S_D_CQCF, S_D_HS1M, S_D_HS2M, S_D_BS1M, S_D_BS2M, S_N };
static const char *CA_SLOT_NAME[S_N] = { "idle", "zoom", "c_idle", "c_zoom", "turn_l", "turn_r", "c_turn_l", "c_turn_r",
	"walk_f", "walk_b", "walk_l", "walk_r", "run_f", "run_b", "run_l", "run_r", "cwalk_f", "cwalk_b", "cwalk_l", "cwalk_r", "crun_f", "crun_b", "crun_l", "crun_r",
	"sprint", "sprint_ll", "sprint_lr", "sprint_fl", "sprint_fr", "spr_acc", "spr_stop2", "spr_stop4", "slide", "slide_end", "slide_zoom", "slide_fire",
	"jump", "fall", "land", "sjump1", "sfall1", "land1", "sjump2", "sfall2", "land2",
	"climb_rdy", "climb_st", "climb_cat", "climb_up", "climb_lnd", "win_rdy", "win_up", "win_brk", "win_lnd",
	"fire", "fire_zoom", "reload", "rl_start", "rl_loop", "rl_end", "swap_st", "swap_end", "hit1", "hit2", "hit3", "hit_flash", "hit_exp",
	"skill", "plant_st", "plant_end", "defuse_st", "defuse_end", "cook", "throw", "cqcb_rdy", "cqcb", "cqcf_rdy", "cqcf",
	"d_hf1", "d_hf2", "d_hb1", "d_hb2", "d_hs1", "d_hs2", "d_bf1", "d_bf2", "d_bb1", "d_bb2", "d_bs1", "d_bs2", "d_spr", "d_exp",
	"d_xhead", "d_xlarm", "d_xrarm", "d_xlleg", "d_xrleg", "d_cqcb_rdy", "d_cqcb", "d_cqcf_rdy", "d_cqcf", "d_hs1m", "d_hs2m", "d_bs1m", "d_bs2m" };
typedef struct { const CBank *B; int slot[S_N]; } CSlots;
static CSlots ca_slots[2];
static const int *ca_slot_tab(const CBank *B) {
	for (int i = 0; i < 2; i++) if (ca_slots[i].B == B) return ca_slots[i].slot;
	for (int i = 0; i < 2; i++) if (!ca_slots[i].B) { ca_slots[i].B = B; for (int s = 0; s < S_N; s++) ca_slots[i].slot[s] = cb_find_slot(B, CA_SLOT_NAME[s]); return ca_slots[i].slot; }
	return ca_slots[0].slot;
}
static int ca_c(const CAnim *A, int s) { return cb_clip(A->B, A->set, ca_slot_tab(A->B)[s]); }
static float ca_len(const CAnim *A, int s) { int c = ca_c(A, s); return c < 0 ? 0 : A->B->clip[c].len; }

static void ca_play(CAct *a, int clip, float fin, float fout, int mask, float rate) {
	if (clip < 0) return;
	if (a->clip == clip && a->w > 0.5f && a->t < 0.25f) { a->t = 0; return; }   // restarted quickly (automatic fire): keep the weight
	a->clip = clip; a->t = 0; a->fin = fin; a->fout = fout; a->mask = mask; a->rate = rate > 0 ? rate : 1; a->loop = 0; a->hold = 0; a->nseq = 0;
	if (a->w > 1) a->w = 1;
}
static void ca_then(CAct *a, int clip) { if (clip >= 0 && a->clip >= 0 && a->nseq < 10) a->seq[a->nseq++] = (short)clip; }   // queue a clip after the current one
static void ca_stop(CAct *a) { a->clip = -1; a->w = 0; a->nseq = 0; }
// advance a one-shot; fires its events; returns 1 while it plays
static int ca_act_update(CAnim *A, CAct *a, float dt) {
	if (a->clip < 0) return 0;
	const CClip *C = &A->B->clip[a->clip];
	float t0 = a->t; a->t += dt * a->rate;
	if (A->on_event && C->nev) {
		float n0 = t0 / C->len, n1 = a->t / C->len;
		for (int i = 0; i < C->nev; i++) { const CEvent *E = &A->B->ev[C->ev0 + i]; if (E->t >= n0 && E->t < n1 && (E->kind == CE_SOUND || E->kind == CE_EFFECT)) A->on_event(A->user, E->kind, E->str, 0); }
	}
	if (a->nseq && a->t >= C->len) {   // the next queued clip continues at full weight (shotgun shells, stealth-kill stages)
		a->t -= C->len; a->clip = a->seq[0]; for (int k = 1; k < a->nseq; k++) a->seq[k - 1] = a->seq[k]; a->nseq--;
		C = &A->B->clip[a->clip]; if (a->t > C->len) a->t = C->len;
	}
	float target = 1;
	if (!a->loop && !a->hold && !a->nseq) { float rem = C->len - a->t; if (rem < a->fout * a->rate) target = rem / (a->fout * a->rate > 1e-3f ? a->fout * a->rate : 1e-3f); }
	if (target < 0) target = 0;
	if (a->w < target) { a->w += dt / (a->fin > 0.01f ? a->fin : 0.01f); if (a->w > target) a->w = target; } else a->w = target;
	if (!a->loop && !a->hold && !a->nseq && a->t >= C->len) { a->clip = -1; a->w = 0; return 0; }
	if (a->hold && a->t > C->len) a->t = C->len;
	return 1;
}

// ---- public actions
static void ca_fire(CAnim *A) { int c = ca_c(A, A->zoom_w > 0.5f ? S_FIREZOOM : S_FIRE); if (A->slide_on) c = ca_c(A, S_SLIDEFIRE); ca_play(&A->fire, c, 0.03f, 0.12f, CA_UPPER, 1); }
static float ca_reload(CAnim *A, float want_len) {   // returns the clip length (s) at the rate used
	int c = ca_c(A, S_RELOAD); if (c < 0) return 0; float L = A->B->clip[c].len, r = want_len > 0.2f ? L / want_len : 1;
	ca_play(&A->up, c, 0.15f, 0.25f, CA_UPPER, r); return L / r;
}
// pump shotgun: start, one loop per shell, end (the original's SG_ReloadStart / SG_Reload / SG_ReloadEnd); returns the total time
static float ca_reload_shells(CAnim *A, int shells) {
	int c0 = ca_c(A, S_RLSTART), c1 = ca_c(A, S_RLLOOP), c2 = ca_c(A, S_RLEND); if (c0 < 0 || c1 < 0 || c2 < 0) return ca_reload(A, 0);
	if (shells < 1) shells = 1; if (shells > 8) shells = 8;
	ca_play(&A->up, c0, 0.12f, 0.25f, CA_UPPER, 1); for (int k = 0; k < shells; k++) ca_then(&A->up, c1); ca_then(&A->up, c2);
	const CClip *C = A->B->clip; return C[c0].len + shells * C[c1].len + C[c2].len;
}
static void ca_upper(CAnim *A, int slot, float fin, float fout, float rate) { ca_play(&A->up, ca_c(A, slot), fin, fout, CA_UPPER, rate); }
// a slot's clip from another animation set (the knife slash, the grenade throw) on a layer; returns its length (s) or 0
static float ca_play_from(CAnim *A, CAct *a, const char *set, int slot, float fin, float fout, int mask) {
	int g = cb_find_set(A->B, set); int c = g < 0 ? -1 : cb_clip(A->B, g, ca_slot_tab(A->B)[slot]); if (c < 0) return 0;
	ca_play(a, c, fin, fout, mask, 1); return A->B->clip[c].len;
}
// stealth kill from behind: the victim is grabbed (held in the ready pose until ca_die(.., kind 3) plays its death),
// the attacker plays the grab and the stab (cqc back: the stab lands ~0.55 s after the grab starts)
// b70: front = the execution from the front (the Ghost Assault cyborg's blade: Death_CQC_Ready_Saito held, Saito_CQC_Ready + Saito_CQC)
static void ca_grabbed_ex(CAnim *A, int front) { int c = ca_c(A, front ? S_D_CQCFRDY : S_D_CQCBRDY); if (c < 0) c = ca_c(A, S_D_CQCBRDY); if (c < 0) return; ca_play(&A->full, c, 0.12f, 0.2f, CA_FULL, 1); A->full.hold = 1; ca_stop(&A->fire); ca_stop(&A->up); }
static __attribute__((unused)) void ca_grabbed(CAnim *A) { ca_grabbed_ex(A, 0); }
static void ca_released(CAnim *A) { if (A->full.clip >= 0 && (A->full.clip == ca_c(A, S_D_CQCBRDY) || A->full.clip == ca_c(A, S_D_CQCFRDY))) { A->full.hold = 0; A->full.fout = 0.2f; } }
// the attacker's grab and strike; *ready (optional) = the grab's length (the strike begins then)
static float ca_cqc_attack_ex(CAnim *A, int front, float *ready) {
	int c0 = ca_c(A, front ? S_CQCFRDY : S_CQCBRDY), c1 = ca_c(A, front ? S_CQCF : S_CQCB); if (ready) *ready = 0; if (c1 < 0) return 0;
	if (c0 >= 0) { ca_play(&A->full, c0, 0.1f, 0.25f, CA_FULL, 1); ca_then(&A->full, c1); } else ca_play(&A->full, c1, 0.1f, 0.25f, CA_FULL, 1);
	ca_stop(&A->fire); ca_stop(&A->up); if (ready) *ready = c0 >= 0 ? A->B->clip[c0].len : 0;
	return (c0 >= 0 ? A->B->clip[c0].len : 0) + A->B->clip[c1].len;
}
CHR_API float ca_cqc_attack(CAnim *A) { return ca_cqc_attack_ex(A, 0, NULL); }
static void ca_hit(CAnim *A, int kind) {   // 0 bullet (random of three), 1 flash, 2 explosion
	if (A->up.clip >= 0 && A->up.w > 0.3f && A->up.clip != ca_c(A, S_HIT1) && A->up.clip != ca_c(A, S_HIT2) && A->up.clip != ca_c(A, S_HIT3)) return;   // busy (reload ...)
	int s = kind == 1 ? S_HITFLASH : kind == 2 ? S_HITEXP : S_HIT1 + rand() % 3;
	ca_play(&A->up, ca_c(A, s), 0.05f, 0.2f, CA_UPPER, 1);
}
// death. head: headshot; side: where the shot came from relative to the character (0 front, 1 back, 2 left, 3 right);
// kind: 0 bullet, 1 explosion, 2 heavy hit (destruction deaths), 3 knife from behind (stealth kill), 4 knife from the front
static int ca_xlast;   // b80: the part of the last destruction death (1 head, 2 left arm, 3 right arm, 4 left leg, 5 right leg; 0 other deaths)
static void ca_die(CAnim *A, int head, int side, int kind) {
	ca_xlast = 0;
	static const int HEAD[4][2] = { { S_D_HF1, S_D_HF2 }, { S_D_HB1, S_D_HB2 }, { S_D_HS1M, S_D_HS2M }, { S_D_HS1, S_D_HS2 } };
	static const int BODY[4][2] = { { S_D_BF1, S_D_BF2 }, { S_D_BB1, S_D_BB2 }, { S_D_BS1M, S_D_BS2M }, { S_D_BS1, S_D_BS2 } };
	int s;
	if (kind == 1) s = S_D_EXP;
	else if (kind == 3) s = S_D_CQCB;
	else if (kind == 4) s = S_D_CQCF;
	else if (kind == 2) { static const int X[5] = { S_D_XHEAD, S_D_XLARM, S_D_XRARM, S_D_XLLEG, S_D_XRLEG }; int x = head ? 0 : 1 + rand() % 4; s = X[x]; ca_xlast = 1 + x; }
	else if (A->sprint_w > 0.6f && A->move_w > 0.6f && side == 0) s = S_D_SPR;
	else s = (head ? HEAD : BODY)[side & 3][rand() & 1];
	int c = ca_c(A, s); if (c < 0) c = ca_c(A, S_D_BF1);
	A->dead = 1; ca_play(&A->death, c, 0.1f, 0, CA_FULL, 1); A->death.hold = 1;
	ca_stop(&A->fire); if (A->up.w > 0) A->up.fout = 0.1f;
}
static void ca_revive(CAnim *A) { A->dead = 0; A->death_scale = 1; ca_stop(&A->death); ca_stop(&A->up); ca_stop(&A->fire); ca_stop(&A->full); A->air_state = 0; A->air_w = 0; A->land_w = 0; A->mantle_w = 0; A->slide_w = 0; A->slide_on = 0; }


// the moving block's clips and weights: 8 directions (two neighbouring cardinal clips) x walk/run x stand/crouch,
// sprint (forward, leaning into turns, diagonal). Shared by the phase clock and the pose.
typedef struct { int n; int clip[24]; float w[24]; } CLocoSet;
static void ca_loco_set(const CAnim *A, float mw, CLocoSet *S) {
	static const int WALK[4] = { S_WALKF, S_WALKR, S_WALKB, S_WALKL }, RUN[4] = { S_RUNF, S_RUNR, S_RUNB, S_RUNL };
	static const int CWALK[4] = { S_CWALKF, S_CWALKR, S_CWALKB, S_CWALKL }, CRUN[4] = { S_CRUNF, S_CRUNR, S_CRUNB, S_CRUNL };
	S->n = 0;
	float a = A->dir_ang, x = a / 1.5707963f; int i0 = (int)floorf(x); float f = x - i0; i0 = ((i0 % 4) + 4) % 4; int i1 = (i0 + 1) % 4;
	float w4[4] = { 0, 0, 0, 0 }; w4[i0] += 1 - f; w4[i1] += f;
	float sw = A->sprint_w, nw = mw * (1 - sw), c = A->crouch_w, r = A->run_w;
	for (int d = 0; d < 4; d++) if (w4[d] > 0.002f) {
		int cs[4] = { ca_c(A, WALK[d]), ca_c(A, RUN[d]), ca_c(A, CWALK[d]), ca_c(A, CRUN[d]) };
		float ws[4] = { (1 - r) * (1 - c), r * (1 - c), (1 - r) * c, r * c };
		for (int k = 0; k < 4; k++) { float w = nw * w4[d] * ws[k]; if (cs[k] >= 0 && w > 0.002f && S->n < 24) { S->clip[S->n] = cs[k]; S->w[S->n++] = w; } }
	}
	if (sw > 0.002f) {
		float side = A->dir_ang / 0.785398f; if (side > 1) side = 1; if (side < -1) side = -1;   // + = to the right
		float ln = A->lean, wl = ln > 0 ? ln : 0, wr = ln < 0 ? -ln : 0, wdl = side < 0 ? -side : 0, wdr = side > 0 ? side : 0;
		float wf = 1 - wl - wr - wdl - wdr; if (wf < 0) wf = 0;
		int cs[5] = { ca_c(A, S_SPRINT), ca_c(A, S_SPRLL), ca_c(A, S_SPRLR), ca_c(A, S_SPRFL), ca_c(A, S_SPRFR) };
		float ws[5] = { wf, wl, wr, wdl, wdr }, tot = wf + wl + wr + wdl + wdr;
		for (int k = 0; k < 5; k++) { float w = mw * sw * ws[k] / (tot > 0 ? tot : 1); if (cs[k] >= 0 && w > 0.002f && S->n < 24) { S->clip[S->n] = cs[k]; S->w[S->n++] = w; } }
	}
}

// ---- per-frame update: advances every layer from the character's movement state
static void ca_update(CAnim *A, const CInput *in, float dt) {
	if (dt > 0.1f) dt = 0.1f;
	const CBank *B = A->B;
	float spd = sqrtf(in->vf * in->vf + in->vr * in->vr);
	float k8 = 1 - expf(-8 * dt), k5 = 1 - expf(-5 * dt);
	// stance / aim / sprint weights (eased like the original's state transitions, ~0.15-0.25 s)
	A->crouch_w += ((in->crouch ? 1.f : 0.f) - A->crouch_w) * k8;
	A->zoom_w += (in->zoom - A->zoom_w) * k8;
	A->sprint_w += ((in->sprint && in->grounded ? 1.f : 0.f) - A->sprint_w) * k8;
	// the moving block's weight follows the speed up to a slow walk (as the original's speed-driven idle <-> walk blend)
	float mv = spd > 120 ? 1.f : spd < 15 ? 0.f : (spd - 15) / 105;
	// slowing to a halt (decelerating hard): the legs keep their own pace and finish the step while the idle fades in
	int stopping = (A->spd_last - spd) / (dt > 1e-4f ? dt : 1e-4f) > 300 || mv < A->move_w - 0.05f; A->spd_last = spd;
	A->move_w += (mv - A->move_w) * (mv > A->move_w ? 1 - expf(-10 * dt) : 1 - expf(-12 * dt));
	A->lean += ((in->sprint ? fmaxf(-1, fminf(1, in->yaw_rate / 2.0f)) : 0) - A->lean) * k5;
	if (spd > 20) {   // travel direction: eased along the shortest arc so 8-direction blends do not jump
		float ang = atan2f(in->vr, in->vf), d = ang - A->dir_ang;
		while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f;
		A->dir_ang += d * (1 - expf(-10 * dt));
		while (A->dir_ang > 3.14159265f) A->dir_ang -= 6.2831853f; while (A->dir_ang < -3.14159265f) A->dir_ang += 6.2831853f;
	}
	// ---- idle clock and turn-in-place (the original steps with turn_left/right while the root turns)
	A->idle_t += dt;
	{ int turning = A->move_w < 0.3f && fabsf(in->yaw_rate) > 0.9f && in->grounded;
	  if (turning) { float dir = in->yaw_rate > 0 ? 1.f : -1.f; if (A->turn_w < 0.05f || dir != A->turn_dir) { if (A->turn_w < 0.05f) A->turn_t = 0; A->turn_dir = dir; } }
	  float len = ca_len(A, A->turn_dir > 0 ? S_TURNL : S_TURNR); if (len <= 0) len = 0.67f;
	  if (turning || (A->turn_w > 0.05f && A->turn_t < len)) { A->turn_t += dt * fminf(1.6f, fmaxf(0.8f, fabsf(in->yaw_rate) / 2.5f)); if (A->turn_t >= len && turning) A->turn_t -= len; }
	  float tw = turning || A->turn_t < len * 0.9f ? (turning ? 1.f : A->turn_w) : 0.f;
	  A->turn_w += (tw - A->turn_w) * (1 - expf(-10 * dt)); if (!turning && A->turn_t >= len) A->turn_w *= 1 - fminf(1, 8 * dt); }
	// ---- gait phase: rate = blended clip frequency x (speed / blended clip speed), within the original's play-speed range
	{
		static const int WALK[4] = { S_WALKF, S_WALKR, S_WALKB, S_WALKL }, RUN[4] = { S_RUNF, S_RUNR, S_RUNB, S_RUNL };
		float a = A->dir_ang, x = a / 1.5707963f; int i0 = (int)floorf(x); float f = x - i0; i0 = ((i0 % 4) + 4) % 4; int i1 = (i0 + 1) % 4;
		float w4[4] = { 0, 0, 0, 0 }; w4[i0] += 1 - f; w4[i1] += f;
		float vw = 0, vr = 0;   // blended walk / run clip speeds of the active directions -> walk/run weight
		for (int d = 0; d < 4; d++) if (w4[d] > 0) { int cw = ca_c(A, WALK[d]), cr = ca_c(A, RUN[d]); vw += w4[d] * (cw >= 0 ? B->clip[cw].speed : 200); vr += w4[d] * (cr >= 0 ? B->clip[cr].speed : 340); }
		(void)vw; (void)vr;
		// walk or run: the original picks the clip by state (walk modifier / aiming = walk, else run) and scales the play
		// speed; here by the speed the physics moves at (walk 220, run 335 cm/s: the switch between them)
		float rw = (spd - 240) / 70; A->run_w = rw < 0 ? 0 : rw > 1 ? 1 : rw;
		CLocoSet S; ca_loco_set(A, 1, &S);
		// the blended clips' ground velocity is the weighted sum of their velocity VECTORS: two neighbouring directions
		// (forward + right at 45 deg) travel ~0.7x as far per cycle as either clip, so the cycle has to run faster to keep
		// the planted foot still (averaging the speeds left it sliding ~1/3 of the speed on diagonals)
		float fsum = 0, vx = 0, vy = 0, wsum = 0;
		for (int k = 0; k < S.n; k++) { const CClip *C = &B->clip[S.clip[k]]; fsum += S.w[k] / C->len; vx += S.w[k] * C->speed * C->dir[0]; vy += S.w[k] * C->speed * C->dir[1]; wsum += S.w[k]; }
		float vsum = sqrtf(vx * vx + vy * vy);
		if (wsum > 0 && vsum > 1 && A->move_w > 0.01f) {
			float f0 = fsum / wsum, v0 = vsum / wsum, r = spd / v0; if (r < 0.4f) r = 0.4f; if (r > 1.6f) r = 1.6f;
			if (stopping && r < 0.85f) r = 0.85f;   // (before b32 the stride froze mid-step and skated to a stop)
			A->phase += dt * f0 * r; A->phase -= floorf(A->phase);
		}
	}
	// ---- air: jump -> fall -> land (sprint jumps have their own set)
	if (!in->grounded && in->mantle == 0) {
		A->air_t += dt;
		if (A->air_state == 0) {
			if (in->jump) { A->air_state = 1; A->air_sprint = A->sprint_w > 0.5f; A->air_t = 0; }   // took off
			else if (in->air_t > 0.25f) { A->air_state = 2; A->air_sprint = A->sprint_w > 0.5f; A->air_t = 0; }   // walked off an edge
		} else if (A->air_state == 1) { float L = ca_len(A, A->air_sprint ? S_SJUMP1 : S_JUMP); if (A->air_t >= L) { A->air_state = 2; A->air_t = 0; } }
		A->air_w += (1 - A->air_w) * (1 - expf(-14 * dt));
	} else {
		if (A->air_state && in->grounded) {   // landed
			if (A->air_state == 2 || A->air_t > 0.2f) { A->land_clip = ca_c(A, A->air_sprint ? S_LAND1 : S_LAND); A->land_t = 0; A->land_w = A->air_w; }
			A->air_state = 0; A->air_t = 0;
		}
		A->air_w *= expf(-14 * dt);
	}
	if (A->land_clip >= 0 && A->land_w > 0) {
		A->land_t += dt; float L = B->clip[A->land_clip].len;
		float tw = A->land_t < L - 0.2f ? 1.f : fmaxf(0, (L - A->land_t) / 0.2f);
		if (A->move_w > 0.5f) tw *= 0.5f;   // running on: the legs take over sooner
		A->land_w = fminf(A->land_w + dt * 10, tw); if (A->land_t >= L) { A->land_w = 0; A->land_clip = -1; }
	}
	A->was_ground = in->grounded;
	// ---- mantle (climb onto a ledge / vault over cover): driven by the physics' progress
	if (in->mantle) { A->mantle_kind = in->mantle; A->mantle_w += (1 - A->mantle_w) * (1 - expf(-20 * dt)); A->air_state = 0; A->air_w = 0; }
	else A->mantle_w *= expf(-10 * dt);
	A->turn_w *= 1 - A->mantle_w;
	// ---- slide
	if (in->slide) { if (!A->slide_on) { A->slide_on = 1; A->slide_t = 0; } A->slide_t += dt; A->slide_w += (1 - A->slide_w) * (1 - expf(-14 * dt)); A->slide_end_t = 0; }
	else if (A->slide_on) { A->slide_on = 0; A->slide_end_t = 1e-4f; }
	if (!in->slide) {
		if (A->slide_end_t > 0) { A->slide_end_t += dt; float L = ca_len(A, S_SLIDEEND); if (A->slide_end_t > L) { A->slide_end_t = 0; } }
		if (A->slide_end_t <= 0) A->slide_w *= expf(-10 * dt);
	}
	// ---- sprint stop: a halt straight out of a full sprint plays the original's skid (Sprint_Stop2) over the whole body
	if (in->sprint && in->grounded) { A->spr_end_t = 0; if (spd > A->spr_peak) A->spr_peak = spd; }
	else { A->spr_end_t += dt;
		if (A->spr_end_t < 0.4f && A->spr_peak > 450 && spd < 130 && in->grounded && !in->slide && !in->mantle && !A->dead && A->full.clip < 0 && A->up.clip < 0) {
			ca_play(&A->full, ca_c(A, S_SPRSTOP2), 0.06f, 0.3f, CA_FULL, 1); A->spr_peak = 0; }
		if (A->spr_end_t >= 0.4f) A->spr_peak = 0; }
	// ---- one-shots
	ca_act_update(A, &A->up, dt); ca_act_update(A, &A->fire, dt); ca_act_update(A, &A->full, dt);
	if (A->dead) ca_act_update(A, &A->death, dt);
	A->last_pitch = in->aim_pitch;
	// foot events of the heaviest locomotion clip (left / right plants)
	if (A->on_event && A->dom_clip >= 0 && A->move_w > 0.5f && in->grounded && !A->dead) {
		const CClip *C = &B->clip[A->dom_clip]; float tn = A->phase + C->phase; tn -= floorf(tn);
		float t0 = A->dom_t_prev;
		for (int i = 0; i < C->nev; i++) { const CEvent *E = &B->ev[C->ev0 + i]; if (E->kind != CE_LFOOT && E->kind != CE_RFOOT) continue;
			int cross = t0 <= tn ? (E->t > t0 && E->t <= tn) : (E->t > t0 || E->t <= tn);
			if (cross) A->on_event(A->user, E->kind, "", E->kind == CE_LFOOT ? 0 : 1); }
		A->dom_t_prev = tn;
	}
}

// ---- pose evaluation (character space MBones in bank order)
static MBone *ca_pose(CAnim *A, const CInput *in) {
	const CBank *B = A->B; int nb = B->nb; CLoc *L = A->loc, *T = A->tmp, *T2 = A->tmp2;
	float wacc = 0; A->dom_clip = -1; float domw = 0;
	if (A->dead && A->death.clip >= 0 && A->death.w >= 0.999f) {   // fully dead: only the death clip
		cb_sample(B, A->death.clip, A->death.t, 0, L);
		if (A->death_scale < 1) { int r = B->root; for (int k = 0; k < 2; k++) L[r].p[k] = A->death_p0[k] + (L[r].p[k] - A->death_p0[k]) * A->death_scale; }
		cb_fk(B, L, 0, A->pose); return A->pose;
	}
	// ---- idle block: (stand, crouch) x (idle, zoom)
	float mw = A->move_w, iw = 1 - mw;
	if (iw > 0.002f) {
		float c = A->crouch_w, z = A->zoom_w;
		cb_accum(B, L, &wacc, T, ca_c(A, S_IDLE), A->idle_t, 1, iw * (1 - c) * (1 - z));
		cb_accum(B, L, &wacc, T, ca_c(A, S_ZOOM), A->idle_t, 1, iw * (1 - c) * z);
		cb_accum(B, L, &wacc, T, ca_c(A, S_CIDLE), A->idle_t, 1, iw * c * (1 - z));
		cb_accum(B, L, &wacc, T, ca_c(A, S_CZOOM), A->idle_t, 1, iw * c * z);
		if (A->turn_w > 0.01f) {   // stepping on the spot while the root turns
			int ct = ca_c(A, A->turn_dir > 0 ? (c > 0.5f ? S_CTURNL : S_TURNL) : (c > 0.5f ? S_CTURNR : S_TURNR));
			if (ct >= 0 && wacc > 0) { cb_sample(B, ct, A->turn_t, 0, T); cb_blend(nb, L, T, A->turn_w, NULL); }
		}
	}
	// ---- moving block (ca_loco_set), every clip at the shared gait phase plus its own left-foot-plant offset
	if (mw > 0.002f) {
		CLocoSet S; ca_loco_set(A, mw, &S);
		for (int k = 0; k < S.n; k++) {
			int ci = S.clip[k]; const CClip *C = &B->clip[ci]; float tn = A->phase + C->phase; tn -= floorf(tn);
			cb_accum(B, L, &wacc, T, ci, tn * C->len, 1, S.w[k]);
			if (S.w[k] > domw) { domw = S.w[k]; A->dom_clip = ci; }
		}
	}
	if (wacc <= 0) cb_sample(B, ca_c(A, S_IDLE), A->idle_t, 1, L);
	// aiming while moving: the upper body raises the gun to the shoulder (zoom pose over the locomotion, feathered)
	if (A->zoom_w > 0.01f && mw > 0.01f && A->sprint_w < 0.99f) {
		int cz = ca_c(A, A->crouch_w > 0.5f ? S_CZOOM : S_ZOOM);
		if (cz >= 0) { cb_sample(B, cz, A->idle_t, 1, T); cb_blend(nb, L, T, A->zoom_w * mw * (1 - A->sprint_w) * 0.85f, B->upper); }
	}
	// ---- slide (full body)
	if (A->slide_w > 0.01f) {
		int cs = A->slide_end_t > 0 ? ca_c(A, S_SLIDEEND) : ca_c(A, S_SLIDE); float t = A->slide_end_t > 0 ? A->slide_end_t : A->slide_t;
		if (cs >= 0) { cb_sample(B, cs, t, 0, T); cb_blend(nb, L, T, A->slide_w, NULL); }
	}
	// ---- air (full body; the fall loops)
	if (A->air_w > 0.01f && A->air_state) {
		int cs = A->air_state == 1 ? ca_c(A, A->air_sprint ? S_SJUMP1 : S_JUMP) : ca_c(A, A->air_sprint ? S_SFALL1 : S_FALL);
		if (cs >= 0) { cb_sample(B, cs, A->air_t, A->air_state == 2, T); cb_blend(nb, L, T, A->air_w, NULL); }
	}
	if (A->land_w > 0.01f && A->land_clip >= 0) { cb_sample(B, A->land_clip, A->land_t, 0, T); cb_blend(nb, L, T, A->land_w, NULL); }
	// ---- mantle: vault = window_up then window_land, climb = climb_up then climb_land, over the physics' progress
	if (A->mantle_w > 0.01f && A->mantle_kind) {
		float k = in->mantle ? in->mantle_k : 1;
		int c1 = ca_c(A, A->mantle_kind == 2 ? S_WINUP : S_CLIMBUP), c2 = ca_c(A, A->mantle_kind == 2 ? S_WINLND : S_CLIMBLND);
		if (c1 >= 0 && c2 >= 0) {
			float L1 = B->clip[c1].len, L2 = B->clip[c2].len, t = k * (L1 + L2);
			if (t < L1) cb_sample(B, c1, t, 0, T); else cb_sample(B, c2, t - L1, 0, T);
			cb_blend(nb, L, T, A->mantle_w, NULL);
		}
	}
	// ---- full-body one-shots (plant, CQC ...)
	if (A->full.clip >= 0 && A->full.w > 0.01f) { cb_sample(B, A->full.clip, A->full.t, A->full.loop, T); cb_blend(nb, L, T, A->full.w, A->full.mask == CA_UPPER ? B->upper : NULL); }
	// ---- upper-body actions: the clip's upper body over the current pose (feathered into the spine)
	if (A->up.clip >= 0 && A->up.w > 0.01f) { cb_sample(B, A->up.clip, A->up.t, A->up.loop, T); cb_blend(nb, L, T, A->up.w, A->up.mask == CA_UPPER ? B->upper : NULL); }
	if (A->fire.clip >= 0 && A->fire.w > 0.01f) {
		// recoil: the fire clip relative to its own first frame, added to the upper body (keeps the arms of the pose under it)
		const CClip *C = &B->clip[A->fire.clip]; (void)C;
		cb_sample(B, A->fire.clip, A->fire.t, 0, T); cb_sample(B, A->fire.clip, 0, 0, T2);
		for (int b = 0; b < nb; b++) {
			float w = A->fire.w * B->upper[b]; if (w <= 0.01f) continue;
			float inv[4] = { -T2[b].q[0], -T2[b].q[1], -T2[b].q[2], T2[b].q[3] }, dq[4], q[4];
			cq_mul(inv, T[b].q, dq);   // delta in the bone's own frame: q' = q * (ref^-1 * cur)
			if (dq[3] < 0) for (int k = 0; k < 4; k++) dq[k] = -dq[k];
			for (int k = 0; k < 3; k++) dq[k] *= w; dq[3] = 1 - w + w * dq[3]; cq_norm(dq);
			cq_mul(L[b].q, dq, q); memcpy(L[b].q, q, 16);
		}
	}
	// ---- death (full body, held on the last frame)
	if (A->dead && A->death.clip >= 0) {
		cb_sample(B, A->death.clip, A->death.t, 0, T);
		if (A->death_scale < 1) { int r = B->root; for (int k = 0; k < 2; k++) T[r].p[k] = A->death_p0[k] + (T[r].p[k] - A->death_p0[k]) * A->death_scale; }
		cb_blend(nb, L, T, A->death.w, NULL); }
	float pitch = A->dead ? 0 : in->aim_pitch * (1 - A->sprint_w * 0.7f) * (1 - A->mantle_w) * (1 - A->slide_w * 0.5f);
	cb_fk(B, L, pitch, A->pose);
	return A->pose;
}

// ---- model binding: a character model's bones -> bank bones (by name)
typedef struct { int n; signed char map[64]; } CBind;
static int cb_bind(CBind *M, const CBank *B, const Model *m) {
	M->n = m->nb < 64 ? m->nb : 64; int miss = 0;   // (cb_to_model writes M->n bones into 64-entry poses)
	for (int i = 0; i < m->nb && i < 64; i++) { int b = cb_find_bone(B, m->bone_name[i]); M->map[i] = (signed char)b; if (b < 0) miss++; }
	return miss;
}
// bank pose -> model bone order (bones the bank does not have keep the root transform)
static void cb_to_model(const CBind *M, const MBone *P, MBone *out) {
	for (int i = 0; i < M->n; i++) { int b = M->map[i]; out[i] = P[b >= 0 ? b : 0]; }
}

// ---- drawing: distance LODs + CPU skinning (skin.h: load-time plans, NEON, the jobs.h worker cores) into per-instance
// buffers shared by the LODs. Character .gvm meshes carry their LOD in the blend field: bits 4..6 = the LODs that draw
// the mesh (0 = every LOD). Rigid meshes (hair, goggles: one bone) are drawn with a one-entry palette (model.h).
#define CHR_NLOD 3
#define CHR_SLOTS 4
typedef struct { GLuint vbo[3][CHR_SLOTS]; int cap[CHR_SLOTS], cur, ok, lod; unsigned long long cmask; int croot, cinv; } CSkin;   // vbo[cur]: the last skinning, of LOD lod
// b80 (destruction deaths, bparts.h): cmask = palette entries of a body part (head / an arm / a leg) and croot its root bone (model
// order); a body with the part blown off draws those entries collapsed onto the root joint (cinv 0), the flying part draws
// every other entry collapsed (cinv 1)
static int chr_mesh_lods(const MMesh *m) { int l = (m->blend >> 4) & 7; return l ? l : 7; }
static int chr_nlods(const Model *M) { int o = 0; for (int i = 0; i < M->nmesh; i++) o |= chr_mesh_lods(&M->mesh[i]); return o == 7 ? 3 : o == 3 ? 2 : 1; }
// skinned meshes of LOD l in order -> slot k
static int chr_lod_meshes(const Model *M, int lod, int *out) {
	int n = 0; for (int i = 0; i < M->nmesh && i < 8; i++) { const MMesh *m = &M->mesh[i]; if (!m->sp) continue; if (chr_mesh_lods(m) & (1 << lod)) { if (n < CHR_SLOTS) out[n] = i; n++; } }
	return n < CHR_SLOTS ? n : CHR_SLOTS;
}
static int chr_clamp_lod(const Model *M, int lod) { int nl = chr_nlods(M); return lod >= nl ? nl - 1 : lod < 0 ? 0 : lod; }
// skinning mode: 0 = CPU (skin.h), 1 = the GPU palette path (model.h's shader on the mesh's own vertices). b32 made the
// GPU the default and the device drew every character as spikes; b33 went back to the CPU. b35: the GPU is used only
// when the start-up self-test proved it right on this device (skin_gpu.h), or when chrskin.txt forces it.
static int chr_gpu_skin = 0;
static void cskin_init(CSkin *c, const Model *M) {
	memset(c, 0, sizeof(*c)); c->cur = -1; c->ok = 1;
	if (chr_gpu_skin) return;   // GPU skinning: no per-instance buffers
	for (int l = 0; l < CHR_NLOD; l++) { int ms[CHR_SLOTS], n = chr_lod_meshes(M, l, ms); for (int k = 0; k < n; k++) { int b = SKIN_OUT_BYTES(M->mesh[ms[k]].sp); if (b > c->cap[k]) c->cap[k] = b; } }
	for (int s = 0; s < 3; s++) for (int k = 0; k < CHR_SLOTS; k++) if (c->cap[k]) {
		glGenBuffers(1, &c->vbo[s][k]); glBindBuffer(GL_ARRAY_BUFFER, c->vbo[s][k]); glBufferData(GL_ARRAY_BUFFER, c->cap[k], NULL, GL_DYNAMIC_DRAW); }
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
// model_palette + the part collapse of c (b80; c may be NULL)
static void chr_palette(const Model *M, const CSkin *c, const MBone *P, float *pal) {
	model_palette(M, P, pal);
	if (!c || !c->cmask || c->croot < 0) return;
	const float *r = P[c->croot].p;
	for (int e = 0; e < M->npal && e < 64; e++) { int in = (int)((c->cmask >> e) & 1); if (in == c->cinv) continue;
		float *o = &pal[e * 12]; for (int k = 0; k < 3; k++) { o[k * 4] = o[k * 4 + 1] = o[k * 4 + 2] = 0; o[k * 4 + 3] = r[k]; } }
}
// b80: the palette entries of model M whose bones (through bind) are bank bone broot or below it; *root = broot's model bone
static unsigned long long chr_part_mask(const Model *M, const CBind *bd, const CBank *B, int broot, int *root) {
	unsigned long long m = 0; *root = -1; if (broot < 0) return 0;
	for (int i = 0; i < bd->n; i++) if (bd->map[i] == broot) { *root = i; break; }
	for (int e = 0; e < M->npal && e < 64; e++) { int mb = M->pal_slot[e]; if (mb < 0 || mb >= bd->n) continue;
		for (int b = bd->map[mb], g = 0; b >= 0 && g < CB_MAXB; b = B->parent[b], g++) if (b == broot) { m |= 1ull << e; break; } }
	return *root >= 0 ? m : 0;
}
static void cskin_free(CSkin *c) { for (int s = 0; s < 3; s++) for (int k = 0; k < CHR_SLOTS; k++) if (c->vbo[s][k]) glDeleteBuffers(1, &c->vbo[s][k]); memset(c, 0, sizeof(*c)); }
// queue the skinning of LOD lod of one instance into its next buffer set (main thread: maps the buffers); skin_run()
// then skins everything queued on all cores. Several instances queued before one skin_run() share one fork-join.
static void chr_skin_queue(const Model *M, CSkin *c, int lod, const MBone *P) {
	if (chr_gpu_skin || !c->ok) return;
	lod = chr_clamp_lod(M, lod);
	float pal[48 * 12]; chr_palette(M, c, P, pal); const float *qp = skin_pal(pal, M->npal);
	int ms[CHR_SLOTS], n = chr_lod_meshes(M, lod, ms);
	c->cur = (c->cur + 1) % 3; c->lod = lod;
	for (int k = 0; k < n; k++) { const MMesh *m = &M->mesh[ms[k]]; if (SKIN_OUT_BYTES(m->sp) <= c->cap[k]) skin_add(m->sp, qp + m->pbase * 12, c->vbo[c->cur][k]); }
}
// scalar reference of the old per-vertex skinning (host tests: tools/chars/host/skin_test.c), on a model_keep_cpu copy
CHR_API void chr_skin16_c(const MMesh *m, const float *pal, int npal, unsigned char *dst) {
	(void)npal;
	const unsigned char *v = m->cpu; int n = m->nv;
	for (int i = 0; i < n; i++, v += 32, dst += 16) {
		float M[12]; const float *R = &pal[v[24] * 12]; float w0 = v[28] * (1.0f / 255);
		for (int k = 0; k < 12; k++) M[k] = R[k] * w0;
		for (int j = 1; j < 4; j++) { int wj = v[28 + j]; if (!wj) continue; const float *Q = &pal[v[24 + j] * 12]; float w = wj * (1.0f / 255); for (int k = 0; k < 12; k++) M[k] += Q[k] * w; }
		float p[3]; memcpy(p, v, 12); float o[3];
		for (int r = 0; r < 3; r++) o[r] = M[r * 4] * p[0] + M[r * 4 + 1] * p[1] + M[r * 4 + 2] * p[2] + M[r * 4 + 3];
		memcpy(dst, o, 12);
		float nx = (signed char)v[12], ny = (signed char)v[13], nz = (signed char)v[14];
		signed char *on = (signed char *)dst + 12;
		for (int r = 0; r < 3; r++) { float t = M[r * 4] * nx + M[r * 4 + 1] * ny + M[r * 4 + 2] * nz; t = t > 127 ? 127 : t < -128 ? -128 : t; on[r] = (signed char)(int)(t + (t < 0 ? -0.5f : 0.5f)); }
		on[3] = (signed char)v[15];   // (b62: the specular mask, as skin.h)
	}
}
static void chr_bind32(GLuint vbo) {
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0); glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, 32, (void *)12);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)16); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, 32, (void *)24);
	glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void *)28);
}
// the GPU palette path (only when verified on this device / forced): rigid meshes with a one-entry palette, the others
// with their palette window
static void chr_draw_gpu(const Model *M, int lod, const float *mvp, const float *pal, const float *ldir, const float *tint, const float *fogc) {
	glUseProgram(md_prog); md_cur_r = 0;
	for (int r = 0; r < 4; r++) { float row[4] = { mvp[r], mvp[4 + r], mvp[8 + r], mvp[12 + r] }; glUniform4fv(md_u_m[r], 1, row); }
	glUniform4fv(md_u_ldir, 1, ldir); glUniform4fv(md_u_tint, 1, tint); glUniform4fv(md_u_fog, 1, fogc); if (md_u_fogp >= 0) glUniform4fv(md_u_fogp, 1, md_fogp);
	md_eye_set(md_u_eye, md_u_sld, mvp, ldir);   // (b62: highlight / rim)
	int pb = -1;
	glEnableVertexAttribArray(3); glEnableVertexAttribArray(4); glActiveTexture(GL_TEXTURE0);
	for (int pass = 0; pass < 2; pass++)
	for (int i = 0; i < M->nmesh; i++) {
		const MMesh *m = &M->mesh[i]; int bm = m->blend & 15, add = bm == 2 || bm == MD_BM_CUT;
		if (add != pass || (md_shadow && add) || !(chr_mesh_lods(m) & (1 << lod)) || !md_mesh_vis(m)) continue;
		md_mesh_emis(m);   // (b71: glow, mask)
		if (m->rigid >= 0) { glUniform4fv(md_u_pal, 3, pal + m->rigid * 12); pb = -1; }
		else if (m->pbase != pb && m->pbase < M->npal) { pb = m->pbase; md_upload_window(md_u_pal, pal, pb, M->npal); }   // a palette window (model.h: 4 rows per entry)
		if (add) MD_BM_ON(bm);
		glBindTexture(GL_TEXTURE_2D, M->tex[m->tex]); chr_bind32(m->vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, 0);
		if (add) MD_BM_OFF();
	}
	glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
// draw a character: lod 0..2 (falls back to the nearest LOD the model has). reskin 1 skins it now (this instance alone);
// reskin 0 draws the last skinning (chr_skin_queue + skin_run earlier this frame, e.g. bots_draw for all bots at once)
static void chr_draw_(const Model *M, CSkin *c, int lod, const float *mvp, const MBone *P, const float *ldir, const float *tint, const float *fogc, int reskin);
static void chr_draw(const Model *M, CSkin *c, int lod, const float *mvp, const MBone *P, const float *ldir, const float *tint, const float *fogc, int reskin) {
	md_env_use(M); chr_draw_(M, c, lod, mvp, P, ldir, tint, fogc, reskin); md_env_use(NULL);   // (b112: the cyborg's own reflection picture, model.h)
}
static void chr_draw_(const Model *M, CSkin *c, int lod, const float *mvp, const MBone *P, const float *ldir, const float *tint, const float *fogc, int reskin) {
	lod = chr_clamp_lod(M, lod);
	float pal[48 * 12]; chr_palette(M, c, P, pal);
	if (chr_gpu_skin) { if (c && c->cur < 0) c->cur = 0; chr_draw_gpu(M, lod, mvp, pal, ldir, tint, fogc); return; }   // (cur >= 0: "drawn", for callers that check it)
	if (c->cur < 0) reskin = 1;
	if (reskin) { chr_skin_queue(M, c, lod, P); skin_run(); }
	else lod = c->lod;   // the LOD that was skinned
	int ms[CHR_SLOTS], n = chr_lod_meshes(M, lod, ms);
	GLint ub = md_begin_bone(mvp, ldir, tint, fogc);   // (model.h: the one-bone program)
	for (int pass = 0; pass < 2; pass++)
	for (int i = 0; i < M->nmesh && i < 8; i++) {
		const MMesh *m = &M->mesh[i]; int bm = m->blend & 15, add = bm == 2 || bm == MD_BM_CUT;   // (b116: + the cut-out parts)
		if (add != pass || (md_shadow && add) || !(chr_mesh_lods(m) & (1 << lod)) || !md_mesh_vis(m)) continue;
		md_mesh_emis(m);   // (b71: glow, mask)
		if (m->rigid >= 0) { glUniform4fv(ub, 3, &pal[m->rigid * 12]); chr_bind32(m->rvbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); }
		else {
			int k = -1; for (int j = 0; j < n; j++) if (ms[j] == i) k = j;
			if (k < 0 || !m->sp || SKIN_OUT_BYTES(m->sp) > c->cap[k]) continue;
			glUniform4fv(ub, 3, md_identity); skin_bind_out(m->sp, c->vbo[c->cur][k]); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->sp->ibo);
		}
		if (add) MD_BM_ON(bm);
		glBindTexture(GL_TEXTURE_2D, M->tex[m->tex]);
		glDrawElements(GL_TRIANGLES, m->rigid >= 0 ? m->ni : m->sp->ni, GL_UNSIGNED_SHORT, 0);
		if (add) MD_BM_OFF();
	}
	glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
// a rigid model (third-person weapon: every vertex on palette entry 0) at one bone transform
static void chr_draw_rigid(const Model *M, const float *mvp, const MBone *B, const float *ldir, const float *tint, const float *fogc) {
	float pal[12]; const float *S = M->pal_s2b;
	float x = B->q[0], y = B->q[1], z = B->q[2], w = B->q[3];
	float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
	for (int r = 0; r < 3; r++) for (int k = 0; k < 4; k++) pal[r * 4 + k] = R[r * 3] * S[k] + R[r * 3 + 1] * S[4 + k] + R[r * 3 + 2] * S[8 + k] + (k == 3 ? B->p[r] : 0);
	model_draw_pal(M, mvp, pal, 1, ldir, tint, fogc, NULL);
}
