// Player physics against the map's player-collision mesh (world_colp.nif: "coc_player" plus the vaultable
// "coc_jumper" cover objects). Bullet collision (world_colb.nif "cob_*") is only used by raycast() and for the
// footstep material. Units: centimetres, Z up (same as the original game data).
//
// Movement model (numbers measured from footage of the original, see the notes at the constants):
//  - a vertical capsule (radius P_RADIUS; 180 cm standing, 120 crouched, 100 sliding) made of stacked spheres;
//  - steps and kerbs up to P_STEP are climbed by walking into them (step up, move, put the feet back down),
//    slopes up to ~50 degrees are walkable, the feet stay on the floor when walking down steps and slopes;
//  - run by default, walk while aiming or with the walk modifier, sprint forward, crouch, slide (crouch pressed
//    while sprinting), jump, climb onto ledges and vault over cover (jump while moving into it).
// The caller sets the in_* fields before player_update() and reads eye / crouched / slide_t / events afterwards.
#pragma once
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { float x, y, z; } V3;
static inline V3 v3(float x, float y, float z) { V3 r = { x, y, z }; return r; }
static inline V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 vmul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 vcross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

// triangle flags (.gcol v3 keeps them in the high bits of the per-triangle material byte)
#define TF_PLAYER 1   // blocks the player (coc_player, coc_jumper)
#define TF_BULLET 2   // hit by raycast(): bullets, sight lines (coc_player and the cob_* meshes, as before)
#define TF_CLIMB  4   // vaultable / climbable cover (coc_jumper)
typedef struct { V3 a, b, c, n; float zmin, zmax; unsigned char mat, flags; } Tri;
typedef struct { int gi, type; float x, y, z, yaw; } Spawn;

static Tri *ctris; static int nctri; static int floor_mat = 0, floor_real = 0;
static Spawn *spawns; static int nspawn; static float col_zmin;   // lowest player-collision point: below it the player has fallen out
// 2D grids over XY: each cell lists the triangles overlapping it (bullet triangles for raycast(), player triangles for movement)
#define CELL 200.0f
static float gx0, gy0; static int gw, gh; static int *cstart, *clist, *pstart, *plist;

static unsigned *ray_stamp;
// perf (b35): the height range of each cell-list entry's triangle, quantized to 16 bits and rounded outwards, stored next to the
// lists (czr for clist, pzr for plist: lo, hi per entry). Crowded cells hold up to ~2500 triangles; the walks now reject most of
// them by height reading 4 bytes in order instead of fetching each 64-byte triangle (a cache miss each on the Vita). Exact: an
// entry is skipped only when its triangle lies clearly outside the span (a margin of several quantization steps), so the float
// tests that follow see the same triangles as before. col_build_zr() after any change of the lists (load_col, dem_add_site_col).
static unsigned short *czr, *pzr; static float zq_base, zq_inv = 1;
static int col_gen;   // counts collision changes (col_build_zr): caches of raycast results check it (bots.h bot_spawn_cands)
static inline unsigned zq_lo(float z) { float q = floorf((z - zq_base) * zq_inv) - 1; return q <= 0 ? 0u : q >= 65535 ? 65535u : (unsigned)q; }
static inline unsigned zq_hi(float z) { float q = ceilf((z - zq_base) * zq_inv) + 1; return q <= 0 ? 0u : q >= 65535 ? 65535u : (unsigned)q; }
static void col_build_zr(void) {
	col_gen++; free(czr); free(pzr); czr = pzr = NULL; if (!nctri || !cstart || !pstart) return;
	float lo = 1e30f, hi = -1e30f; for (int i = 0; i < nctri; i++) { if (ctris[i].zmin < lo) lo = ctris[i].zmin; if (ctris[i].zmax > hi) hi = ctris[i].zmax; }
	zq_base = lo - 8; float sc = (hi + 8 - zq_base) / 60000.f; if (sc < 0.05f) sc = 0.05f; zq_inv = 1 / sc;
	int nc = cstart[gw * gh], np = pstart[gw * gh];
	czr = malloc(4 * (size_t)(nc + 1)); pzr = malloc(4 * (size_t)(np + 1));
	if (!czr || !pzr) { free(czr); free(pzr); czr = pzr = NULL; return; }
	for (int k = 0; k < nc; k++) { const Tri *t = &ctris[clist[k]]; czr[2 * k] = (unsigned short)zq_lo(t->zmin); czr[2 * k + 1] = (unsigned short)zq_hi(t->zmax); }
	for (int k = 0; k < np; k++) { const Tri *t = &ctris[plist[k]]; pzr[2 * k] = (unsigned short)zq_lo(t->zmin); pzr[2 * k + 1] = (unsigned short)zq_hi(t->zmax); }
}
#define ZR_OUT(zr, k, qlo, qhi) ((zr) && ((zr)[2 * (k) + 1] < (qlo) || (zr)[2 * (k)] > (qhi)))   // entry k's triangle is clearly outside [qlo, qhi]
static void col_fix_spawns(void);
static void col_free(void) {
	free(czr); free(pzr); czr = pzr = NULL;
	free(ctris); free(spawns); free(cstart); free(clist); free(pstart); free(plist); free(ray_stamp);
	ctris = NULL; spawns = NULL; cstart = clist = pstart = plist = NULL; ray_stamp = NULL; nctri = nspawn = 0;
}
#include "glz.h"   // perf b56: packed collision
static int load_col(const char *path) {
	col_free();
	FILE *f = glz_fopen(path); if (!f) return -1;   // (perf b56: a packed .gcol unpacks as it is read; the loader thread)
	setvbuf(f, NULL, _IOFBF, 64 * 1024);
	char mg[4]; unsigned ver = 0, nv = 0, nt = 0;
	if (fread(mg, 4, 1, f) != 1 || memcmp(mg, "GCOL", 4) || fread(&ver, 4, 1, f) != 1 || fread(&nv, 4, 1, f) != 1 || fread(&nt, 4, 1, f) != 1) { fclose(f); return -2; }
	float *vp = malloc(nv * 12 + 12); unsigned *ix = malloc(nt * 12 + 12);
	if (fread(vp, 12, nv, f) != nv || fread(ix, 12, nt, f) != nt) { free(vp); free(ix); fclose(f); return -2; }
	if (fread(&nspawn, 4, 1, f) != 1 || nspawn < 0) nspawn = 0;
	spawns = malloc(sizeof(Spawn) * (nspawn + 1)); nspawn = (int)fread(spawns, sizeof(Spawn), nspawn, f);
	unsigned char *mats = calloc(nt + 1, 1); if (ver >= 2 && fread(mats, 1, nt, f) != nt) memset(mats, 0, nt);
	fclose(f);
	ctris = malloc(sizeof(Tri) * (nt + 1)); nctri = 0;
	float mnx = 1e9, mny = 1e9, mxx = -1e9, mxy = -1e9;
	for (unsigned i = 0; i < nt; i++) {
		if (ix[i * 3] >= nv || ix[i * 3 + 1] >= nv || ix[i * 3 + 2] >= nv) continue;
		Tri *t = &ctris[nctri];
		if (ver >= 3) { t->mat = mats[i] & 15; t->flags = mats[i] >> 4; }
		else { t->mat = mats[i]; t->flags = TF_PLAYER | TF_BULLET; }   // v2 files: every triangle blocked the player
		if (t->mat > 8) t->mat = 0;
		float *A = vp + ix[i * 3] * 3, *B = vp + ix[i * 3 + 1] * 3, *C = vp + ix[i * 3 + 2] * 3;
		t->a = v3(A[0], A[1], A[2]); t->b = v3(B[0], B[1], B[2]); t->c = v3(C[0], C[1], C[2]);
		V3 n = vcross(vsub(t->b, t->a), vsub(t->c, t->a)); float l = sqrtf(vdot(n, n)); if (l < 1e-4f) continue;   // degenerate
		t->n = vmul(n, 1.0f / l);
		t->zmin = fminf(A[2], fminf(B[2], C[2])); t->zmax = fmaxf(A[2], fmaxf(B[2], C[2]));
		mnx = fminf(mnx, fminf(A[0], fminf(B[0], C[0]))); mxx = fmaxf(mxx, fmaxf(A[0], fmaxf(B[0], C[0])));
		mny = fminf(mny, fminf(A[1], fminf(B[1], C[1]))); mxy = fmaxf(mxy, fmaxf(A[1], fmaxf(B[1], C[1])));
		nctri++;
	}
	col_zmin = 1e9f; for (int i = 0; i < nctri; i++) if ((ctris[i].flags & TF_PLAYER) && ctris[i].zmin < col_zmin) col_zmin = ctris[i].zmin;
	if (col_zmin > 1e8f) col_zmin = 0;
	free(vp); free(ix); free(mats);
	if (!nctri) { mnx = mny = 0; mxx = mxy = 1; }
	gx0 = mnx - CELL; gy0 = mny - CELL; gw = (int)((mxx - gx0) / CELL) + 2; gh = (int)((mxy - gy0) / CELL) + 2;
	int *cnt = calloc(gw * gh, sizeof(int)), *pcnt = calloc(gw * gh, sizeof(int));
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 1) {
			cstart = malloc(sizeof(int) * (gw * gh + 1)); pstart = malloc(sizeof(int) * (gw * gh + 1)); cstart[0] = pstart[0] = 0;
			for (int i = 0; i < gw * gh; i++) { cstart[i + 1] = cstart[i] + cnt[i]; pstart[i + 1] = pstart[i] + pcnt[i]; }
			clist = malloc(sizeof(int) * (cstart[gw * gh] + 1)); plist = malloc(sizeof(int) * (pstart[gw * gh] + 1));
			memset(cnt, 0, sizeof(int) * gw * gh); memset(pcnt, 0, sizeof(int) * gw * gh);
		}
		for (int i = 0; i < nctri; i++) {
			Tri *t = &ctris[i];
			int x0 = (int)((fminf(t->a.x, fminf(t->b.x, t->c.x)) - gx0) / CELL), x1 = (int)((fmaxf(t->a.x, fmaxf(t->b.x, t->c.x)) - gx0) / CELL);
			int y0 = (int)((fminf(t->a.y, fminf(t->b.y, t->c.y)) - gy0) / CELL), y1 = (int)((fmaxf(t->a.y, fmaxf(t->b.y, t->c.y)) - gy0) / CELL);
			for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
				int c = y * gw + x;
				if (t->flags & TF_BULLET) { if (pass) clist[cstart[c] + cnt[c]] = i; cnt[c]++; }
				if (t->flags & TF_PLAYER) { if (pass) plist[pstart[c] + pcnt[c]] = i; pcnt[c]++; }
			}
		}
	}
	free(cnt); free(pcnt);
	col_build_zr();
	col_fix_spawns();
	return 0;
}

// b55: the per-cell bullet / player lists again, after triangles' flags changed (range.h: the Shooting Range's invisible
// boundary no longer stops bullets); same grid
static void col_rebuild_lists(void) {
	if (!nctri || !cstart || !pstart) return;
	int *cnt = calloc(gw * gh, sizeof(int)), *pcnt = calloc(gw * gh, sizeof(int)); if (!cnt || !pcnt) { free(cnt); free(pcnt); return; }
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 1) {
			for (int i = 0; i < gw * gh; i++) { cstart[i + 1] = cstart[i] + cnt[i]; pstart[i + 1] = pstart[i] + pcnt[i]; }   // (never more entries than before: flags were only cleared)
			memset(cnt, 0, sizeof(int) * gw * gh); memset(pcnt, 0, sizeof(int) * gw * gh);
		}
		for (int i = 0; i < nctri; i++) {
			Tri *t = &ctris[i];
			int x0 = (int)((fminf(t->a.x, fminf(t->b.x, t->c.x)) - gx0) / CELL), x1 = (int)((fmaxf(t->a.x, fmaxf(t->b.x, t->c.x)) - gx0) / CELL);
			int y0 = (int)((fminf(t->a.y, fminf(t->b.y, t->c.y)) - gy0) / CELL), y1 = (int)((fmaxf(t->a.y, fmaxf(t->b.y, t->c.y)) - gy0) / CELL);
			for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
				int c = y * gw + x;
				if (t->flags & TF_BULLET) { if (pass) clist[cstart[c] + cnt[c]] = i; cnt[c]++; }
				if (t->flags & TF_PLAYER) { if (pass) plist[pstart[c] + pcnt[c]] = i; pcnt[c]++; }
			}
		}
	}
	free(cnt); free(pcnt); free(czr); free(pzr); czr = pzr = NULL; col_build_zr(); col_gen++;
}
static V3 closest_pt_tri(V3 p, V3 a, V3 b, V3 c) {
	V3 ab = vsub(b, a), ac = vsub(c, a), ap = vsub(p, a);
	float d1 = vdot(ab, ap), d2 = vdot(ac, ap); if (d1 <= 0 && d2 <= 0) return a;
	V3 bp = vsub(p, b); float d3 = vdot(ab, bp), d4 = vdot(ac, bp); if (d3 >= 0 && d4 <= d3) return b;
	float vc = d1 * d4 - d3 * d2; if (vc <= 0 && d1 >= 0 && d3 <= 0) return vadd(a, vmul(ab, d1 / (d1 - d3)));
	V3 cp = vsub(p, c); float d5 = vdot(ab, cp), d6 = vdot(ac, cp); if (d6 >= 0 && d5 <= d6) return c;
	float vb = d5 * d2 - d1 * d6; if (vb <= 0 && d2 >= 0 && d6 <= 0) return vadd(a, vmul(ac, d2 / (d2 - d6)));
	float va = d3 * d6 - d5 * d4;
	if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return vadd(b, vmul(vsub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
	float den = 1.0f / (va + vb + vc); return vadd(a, vadd(vmul(ab, vb * den), vmul(ac, vc * den)));
}

// ---- tuning. Speeds were measured on 60 fps footage of the original (ground-plane visual odometry with the
// focal length fitted from camera turns, eye height P_EYE) and agree with the foot speed of the game's own
// third-person locomotion clips (walk ~200, run ~330-380, sprint ~470-530 cm/s).
#define P_RADIUS 35.0f
#define P_HEIGHT 180.0f          // standing collision height
#define P_HEIGHT_CROUCH 120.0f   // crouched (third-person crouch clips: head bone ~105 cm)
#define P_HEIGHT_SLIDE 100.0f    // sliding (third-person sliding clips: head bone ~75 cm)
#define P_EYE 152.6f             // standing eye height (first-person rig Camera_dummy)
#define P_EYE_CROUCH 106.0f      // = P_EYE scaled like the third-person head bone (149 standing -> 104 crouched)
#define P_EYE_SLIDE 78.0f        // (149 -> 75 sliding)
#define P_RUN 335.0f             // default movement with a rifle (footage: cluster centre 333)
#define P_WALK 220.0f            // walk modifier / moving while aiming down sights (footage: 222)
#define P_SPRINT 535.0f          // sprint with a rifle (footage: 532; with a pistol ~670, see speed_mul)
#define P_CROUCH 175.0f          // crouch walk
#define P_SLIDE 690.0f           // slide entry speed (a burst over sprint speed)
#define P_SLIDE_TIME 1.1f        // slide length (s); the speed decays towards crouch speed (time constant 0.45 s)
#define P_JUMP 470.0f            // jump take-off speed: apex ~79 cm with P_GRAVITY
#define P_GRAVITY 1400.0f
#define P_ACCEL 12.0f            // ground acceleration (1/s, exponential approach to the target speed)
#define P_DECEL 14.0f            // ground deceleration when the stick is released
#define P_AIRACCEL 2.0f
#define P_STEP 45.0f             // highest step / kerb climbed by walking into it
#define P_SNAP 32.0f             // feet stay on floors that drop by up to this much (stairs, slopes)
#define P_WALKABLE 0.64f         // floor normal z: slopes up to ~50 degrees can be walked
// b122 (user: "in the shooting range there seemed to be obstacles that you can't climb over but look like you should be able to
// climb over"): the range's two big blocks stand 2.35-2.55 m above its floor (the one beside the platform 249 / 258, the free
// one 259 / 268, the floor 14) - just over the 2.3 m reach, so a jump at them did nothing (ledge_sweep below: 127 of the range's
// 319 dead jumps). There the reach is 2.6 m (main.c sets it per map); everywhere else it stays 2.3 m: on the other maps the same
// 30 cm opens new ground (Ghost Storage +1705 places to stand, Train Station +325, Electronics Center +171) nobody has checked,
// and their high climbs are the PC's own authored maneuvers (smf.txt).
static float p_climb_max = 230.0f;
#define P_CLIMB_MAX p_climb_max   // highest ledge (above the feet) that can be climbed onto
#define P_VAULT_MAX 135.0f       // cover up to this height is vaulted over when there is floor behind it

// one-shot events of the last player_update (bit mask in Player.events) for sounds and animations
#define PE_JUMP 1          // took off with a jump
#define PE_LAND 2          // touched down after being in the air (land_vz = vertical speed at impact)
#define PE_CROUCH 4        // crouched down
#define PE_STAND 8         // stood up
#define PE_SLIDE 16        // started a slide
#define PE_SLIDE_END 32    // the slide finished (the player is crouched afterwards if in_crouch is still set)
#define PE_CLIMB 64        // started climbing onto a ledge
#define PE_VAULT 128       // started vaulting over cover

typedef struct {
	V3 pos; V3 vel; int grounded; float yaw, pitch; float mantle_t, mantle_len, m_peak; V3 m_from, m_to;
	// ---- inputs, set by the caller before player_update (0 = off)
	int in_crouch;      // the crouch toggle state: 1 = wants to be crouched. Switching it on while sprinting slides.
	int in_ads;         // aiming down sights: moves at walk speed, no sprint
	int in_walk;        // walk modifier
	float speed_mul;    // weapon speed factor (0 means 1.0; ~1.24 with a pistol or knife)
	float jump_mul;     // b79: jump take-off factor (0 means 1.0; HYPER SPRINT 1.32 / 1.42)
	// ---- state, read by the caller
	int crouched;       // 1 while crouched or sliding (collision height lowered)
	float slide_t;      // > 0 while sliding: seconds since the slide started
	float eye;          // camera height above pos.z: eases between stances, dips on steps and landings (0 before the first update)
	float height;       // current collision height
	int events;         // PE_* events of the last update
	float land_vz;      // vertical speed at the last landing (cm/s, negative)
	int mantle_kind;    // while mantle_t > 0: 1 climbing onto a ledge, 2 vaulting over cover, 3 a map's special maneuver (b87)
	int sprinting;      // 1 while actually sprinting
	// ---- internal
	int was_crouch_in, keep_run; float eye_off, air_t, slide_cd; V3 slide_dir;
	V3 sm_p[6]; float sm_cum[6]; int sm_n;   // b87: the special maneuver's path (from where the body stands); b104: sm_p[0..3] = the authored curve's four control points, sm_p[4] (sm_n 5) = a climb's floor, sm_cum[1] = the curve's time
	float sm_duck;      // b104: how far the eye ducks at the middle of a maneuver through a low opening (cm)
	int floor_m;        // perf (b35): footstep material under the feet at the last grounded update (player_floor_mat)
	int local_floor;    // perf (b35): 1 = player_move leaves the floor_mat / floor_real globals alone (bots: physics on the jobs.h
	                    // workers) and only notes where it last stood: player_floor_mat casts the material ray when a sound needs it
	int floor_dirty; V3 floor_at;
} Player;
static float raycast(V3 o, V3 d, float maxd, int *mat, V3 *nrm);
static float raycast_mask(V3 o, V3 d, float maxd, int flags, int *tri);

// push a sphere out of nearby player-collision triangles; returns 1 if a walkable floor was hit (allow_floor).
// Floor-like contacts always push straight up (never sideways); ceilings push down; everything else pushes horizontally.
static int sphere_resolve(V3 *c, float r, int allow_floor, V3 *floor_n) {
	int floor = 0; unsigned qlo = zq_lo(c->z - r), qhi = zq_hi(c->z + r);
	int cx0 = (int)((c->x - r - gx0) / CELL), cx1 = (int)((c->x + r - gx0) / CELL);
	int cy0 = (int)((c->y - r - gy0) / CELL), cy1 = (int)((c->y + r - gy0) / CELL);
	if (cx0 < 0) cx0 = 0; if (cy0 < 0) cy0 = 0; if (cx1 >= gw) cx1 = gw - 1; if (cy1 >= gh) cy1 = gh - 1;
	for (int y = cy0; y <= cy1; y++) for (int x = cx0; x <= cx1; x++) {
		int cell = y * gw + x;
		for (int k = pstart[cell]; k < pstart[cell + 1]; k++) {
			if (ZR_OUT(pzr, k, qlo, qhi)) continue;
			Tri *t = &ctris[plist[k]];
			if (c->z + r < t->zmin || c->z - r > t->zmax) continue;
			{ float rm = r + 0.01f, xl = c->x - rm, xh = c->x + rm, yl = c->y - rm, yh = c->y + rm;   // perf (b35): all three corners beyond the sphere
			  if ((t->a.x > xh && t->b.x > xh && t->c.x > xh) || (t->a.x < xl && t->b.x < xl && t->c.x < xl) ||  // on one side in x or y: the
			      (t->a.y > yh && t->b.y > yh && t->c.y > yh) || (t->a.y < yl && t->b.y < yl && t->c.y < yl)) continue; }   // triangle can't touch it
			V3 q = closest_pt_tri(*c, t->a, t->b, t->c), d = vsub(*c, q);
			float dd = vdot(d, d); if (dd >= r * r) continue;
			float dist = sqrtf(dd); V3 n = dist > 1e-3f ? vmul(d, 1.0f / dist) : t->n;
			float pen = r - dist;
			if (n.z > P_WALKABLE) { c->z += pen / n.z; if (allow_floor) { floor = 1; if (floor_n) *floor_n = n; } }
			else if (n.z < -0.7f) { *c = vadd(*c, vmul(n, pen)); }
			else { V3 h = v3(n.x, n.y, 0); float hl = sqrtf(vdot(h, h)); if (hl > 0.05f) { float push = pen / hl; if (push > pen * 4) push = pen * 4; *c = vadd(*c, vmul(h, push / hl)); } }
			qlo = zq_lo(c->z - r); qhi = zq_hi(c->z + r);   // (the sphere moved)
		}
	}
	return floor;
}
// does a column of height h standing at feet p overlap player collision above the step height (deeper than tol)?
static int body_blocked(V3 p, float h, float tol) {
	float r = P_RADIUS - tol;
	for (float z = p.z + P_STEP + r; ; z += r) {
		if (z > p.z + h - r) z = p.z + h - r;
		V3 c = v3(p.x, p.y, z), o = c; sphere_resolve(&c, r, 0, NULL);
		if (fabsf(c.x - o.x) + fabsf(c.y - o.y) + fabsf(c.z - o.z) > 0.01f) return 1;
		if (z >= p.z + h - r) break;
	}
	return 0;
}

// how deep a column of height h standing at feet p is inside player collision above the step height (largest push, cm)
static float body_depth(V3 p, float h) {
	float worst = 0;
	for (float z = p.z + P_STEP + P_RADIUS; ; z += P_RADIUS) {
		if (z > p.z + h - P_RADIUS) z = p.z + h - P_RADIUS;
		V3 c = v3(p.x, p.y, z), o = c; sphere_resolve(&c, P_RADIUS, 0, NULL);
		float m = fabsf(c.x - o.x) + fabsf(c.y - o.y) + fabsf(c.z - o.z); if (m > worst) worst = m;
		if (z >= p.z + h - P_RADIUS) break;
	}
	return worst;
}

// one collide-and-slide pass from feet *p by d: spheres from base (above the feet) up to the head are pushed out of
// walls horizontally. With base == P_RADIUS (the full capsule, in the air) the feet sphere also lands on walkable
// floors. Ceilings stop upward motion. Returns PM_FLOOR | PM_WALL | PM_CEIL.
#define PM_FLOOR 1
#define PM_WALL 2
#define PM_CEIL 4
static int pm_pass(V3 *p, V3 d, float h, float base, float *vz, int allow_floor) {
	V3 feet = vadd(*p, d); int res = 0;
	if (base > h - P_RADIUS) base = h - P_RADIUS;
	for (int it = 0; it < 4; it++) {
		float px = 0, py = 0, pz = 0, best = 0; int fl = 0;
		for (float z = base; ; z += P_RADIUS) {
			if (z > h - P_RADIUS) z = h - P_RADIUS;
			V3 c = v3(feet.x, feet.y, feet.z + z), o = c;
			int isfeet = z == P_RADIUS, f = sphere_resolve(&c, P_RADIUS, isfeet && allow_floor, NULL);
			float hx = c.x - o.x, hy = c.y - o.y, m = fabsf(hx) + fabsf(hy);
			if (m > best) { best = m; px = hx; py = hy; }
			if (isfeet) { if (f) fl = 1; if (c.z - o.z > pz) pz = c.z - o.z; }
			else if (c.z - o.z < -0.01f) { res |= PM_CEIL; if (*vz > 0) { if (c.z - o.z < pz) pz = c.z - o.z; *vz = 0; } }   // head against a ceiling
			if (z >= h - P_RADIUS) break;
		}
		if (best > 0.01f) res |= PM_WALL;
		feet.x += px; feet.y += py; feet.z += pz;
		if (fl) res |= PM_FLOOR;
		if (fabsf(px) + fabsf(py) + fabsf(pz) < 0.01f) break;
	}
	*p = feet; return res;
}

// Floor probe under the feet: a disc of 9 points (centre + ring of radius P_PROBE). For walkable surfaces whose
// height at a point lies in [zlo, zhi] it returns the highest one, each extrapolated along its own slope to the
// centre (so slopes give the height under the centre and steps give the step top): the support height, or -1e9.
// *block gets the ring points that see a surface just above zhi (a step too high to walk onto), as a bit mask.
#define P_PROBE (P_RADIUS * 0.6f)
static const float probe_off[9][2] = { {0, 0}, {1, 0}, {0.7071f, 0.7071f}, {0, 1}, {-0.7071f, 0.7071f}, {-1, 0}, {-0.7071f, -0.7071f}, {0, -1}, {0.7071f, -0.7071f} };
static float floor_probe(float x, float y, float zlo, float zhi, float zblock, int *block, int *tri) {
	float best = -1e9f, maxraw = -1e9f; int bm = 0, bt = -1; const float r = P_PROBE; unsigned qlo = zq_lo(zlo), qhi = zq_hi(zblock);
	int cx0 = (int)((x - r - gx0) / CELL), cx1 = (int)((x + r - gx0) / CELL), cy0 = (int)((y - r - gy0) / CELL), cy1 = (int)((y + r - gy0) / CELL);
	if (cx0 < 0) cx0 = 0; if (cy0 < 0) cy0 = 0; if (cx1 >= gw) cx1 = gw - 1; if (cy1 >= gh) cy1 = gh - 1;
	for (int gy = cy0; gy <= cy1; gy++) for (int gx = cx0; gx <= cx1; gx++) {
		int cell = gy * gw + gx;
		for (int k = pstart[cell]; k < pstart[cell + 1]; k++) {
			if (ZR_OUT(pzr, k, qlo, qhi)) continue;
			const Tri *t = &ctris[plist[k]];
			if (t->zmin > zblock || t->zmax < zlo) continue;
			float nz = t->n.z, nx = t->n.x, ny = t->n.y; if (nz < 0) { nz = -nz; nx = -nx; ny = -ny; }
			if (nz < 0.05f) continue;   // vertical: seen by the body, not by the floor probe
			float tx0 = fminf(t->a.x, fminf(t->b.x, t->c.x)), tx1 = fmaxf(t->a.x, fmaxf(t->b.x, t->c.x));
			float ty0 = fminf(t->a.y, fminf(t->b.y, t->c.y)), ty1 = fmaxf(t->a.y, fmaxf(t->b.y, t->c.y));
			if (tx0 > x + r || tx1 < x - r || ty0 > y + r || ty1 < y - r) continue;
			float e1x = t->b.x - t->a.x, e1y = t->b.y - t->a.y, e2x = t->c.x - t->a.x, e2y = t->c.y - t->a.y, den = e1x * e2y - e1y * e2x;
			if (fabsf(den) < 1e-6f) continue; den = 1.0f / den;
			float pd = nx * t->a.x + ny * t->a.y + nz * t->a.z;   // plane: nx x + ny y + nz z = pd
			for (int q = 0; q < 9; q++) {
				float ox = probe_off[q][0] * r, oy = probe_off[q][1] * r, px = x + ox, py = y + oy;
				if (px < tx0 || px > tx1 || py < ty0 || py > ty1) continue;
				float wx = px - t->a.x, wy = py - t->a.y, u = (wx * e2y - wy * e2x) * den, v = (e1x * wy - e1y * wx) * den;
				if (u < -1e-4f || v < -1e-4f || u + v > 1.0001f) continue;
				float z = (pd - nx * px - ny * py) / nz;
				if (z > zhi && z <= zblock && q) { if (nz >= P_WALKABLE || z > zhi + 2) bm |= 1 << q; continue; }
				if (z < zlo || z > zhi || nz < P_WALKABLE) continue;
				if (z > maxraw) maxraw = z;
				float zc = z + (nx * ox + ny * oy) / nz;   // the plane extrapolated to the centre
				if (zc > zhi) zc = zhi;
				if (zc > best) { best = zc; bt = plist[k]; }
			}
		}
	}
	if (best > maxraw) best = maxraw;   // never above the highest real floor point (a slope ending at a landing)
	if (block) *block = bm; if (tri) *tri = bt;
	return best;
}

// Move the feet with velocity * dt. On the ground the body (above P_STEP) slides along walls and the floor probe
// carries the feet up kerbs and stairs (up to P_STEP) and down steps and slopes (up to P_SNAP); in the air the
// whole capsule collides and lands on floors (the floor probe also catches ledges the feet only just reach).
static void player_move(Player *p, float dt) {
	float h = p->height > 0 ? p->height : P_HEIGHT;
	V3 delta = vmul(p->vel, dt);
	float len = sqrtf(vdot(delta, delta)); int steps = (int)(len / 15.0f) + 1;
	V3 sd = vmul(delta, 1.0f / steps);
	int grounded = p->grounded && p->vel.z <= 0; V3 pos0 = p->pos;
	for (int s = 0; s < steps; s++) {
		V3 start = p->pos;
		if (grounded) {
			V3 d = v3(sd.x, sd.y, 0), a = start; float vz0 = 0; int blk = 0, moved = 0;
			for (int tries = 0; tries < 2 && !moved; tries++) {
				a = start; int r = pm_pass(&a, d, h, P_STEP + P_RADIUS, &vz0, 0);
				float sz = floor_probe(a.x, a.y, start.z - P_SNAP, start.z + P_STEP, start.z + P_STEP + P_RADIUS, &blk, NULL);
				// ring points that see a step too high, in the direction of the move: slide along it / stop
				float bx = 0, by = 0; for (int q = 1; q < 9; q++) if (blk & (1 << q)) { float ox = probe_off[q][0], oy = probe_off[q][1]; if (ox * d.x + oy * d.y > 0) { bx += ox; by += oy; } }
				float bl = sqrtf(bx * bx + by * by);
				if (bl > 1e-3f) { bx /= bl; by /= bl; float k = d.x * bx + d.y * by; d.x -= bx * k; d.y -= by * k; continue; }
				if (sz < -1e8f) { p->pos = a; grounded = 0; moved = 1; break; }   // walked off an edge: falling
				V3 b = v3(a.x, a.y, sz);
				if ((r & PM_CEIL) || sz > start.z + 1) {   // head room: never move deeper under a ceiling (moving out is fine)
					float d1 = body_depth(b, h); if (d1 > 0.3f && d1 > body_depth(start, h) + 0.05f) { d = v3(0, 0, 0); continue; } }
				if (sz > start.z + 0.5f) p->eye_off -= sz - start.z;      // step up: the camera follows smoothly
				else if (sz < start.z - 2.0f) p->eye_off += start.z - sz;  // step down
				p->pos = b; moved = 1;
			}
			if (grounded) p->vel.z = 0;
		} else {
			V3 a = start; int r = pm_pass(&a, sd, h, P_RADIUS, &p->vel.z, p->vel.z <= 0);
			if (r & PM_CEIL) { float d1 = body_depth(a, h); if (d1 > 0.3f && d1 > body_depth(start, h) + 0.05f) { a.x = start.x; a.y = start.y; } }   // no flying in under a low ceiling
			if (p->vel.z <= 0) {
				int blk; float sz = floor_probe(a.x, a.y, a.z - 1.0f, a.z + P_STEP * 0.5f, a.z + P_STEP * 0.5f, &blk, NULL);
				if (sz > -1e8f && !body_blocked(v3(a.x, a.y, sz), h, 1.0f)) { a.z = sz; r |= PM_FLOOR; }
			}
			p->pos = a;
			if (r & PM_FLOOR) { grounded = 1; if (p->vel.z < 0) p->vel.z = 0; }
		}
	}
	p->grounded = grounded;
	// blocked: the horizontal velocity becomes what was actually moved (slides along walls, stops against them)
	{ float ix = delta.x, iy = delta.y, ax = p->pos.x - pos0.x, ay = p->pos.y - pos0.y, il = ix * ix + iy * iy, al = ax * ax + ay * ay;
	  if (dt > 0 && il > 1e-4f && al < il * 0.98f) { float k = (ax * ix + ay * iy) / il; if (k < 0) k = 0; if (k > 1) k = 1;
		p->vel.x = ax / dt; p->vel.y = ay / dt; if (al > il) { p->vel.x *= sqrtf(il / al); p->vel.y *= sqrtf(il / al); } (void)k; } }
	// footstep material: the bullet-collision surface under the feet (0 = generic, sounds like stone)
	if (grounded && p->local_floor) { p->floor_dirty = 1; p->floor_at = p->pos; }   // (the ray waits until a sound asks: player_floor_mat)
	else if (grounded) { int m = 0; V3 n; float d = raycast(v3(p->pos.x, p->pos.y, p->pos.z + 30), v3(0, 0, -1), 80, &m, &n); p->floor_m = d >= 0 ? m : 0; floor_mat = floor_real = p->floor_m; }
}

// the footstep material where a local_floor player last stood on the ground (the ray player_move used to cast every update)
static int player_floor_mat(Player *p) {
	if (p->floor_dirty) { int m = 0; V3 n; float d = raycast(v3(p->floor_at.x, p->floor_at.y, p->floor_at.z + 30), v3(0, 0, -1), 80, &m, &n); p->floor_m = d >= 0 ? m : 0; p->floor_dirty = 0; }
	return p->floor_m;
}
// climbing: a ledge 40..P_CLIMB_MAX above the feet right in front with room to stand on it, or low cover (a wall,
// a barrier, a window sill) with floor behind it to vault over. Returns 1 climb, 2 vault, 0 nothing.
static int ledge_cover;   // set by player_ledge: the obstacle is marked vaultable cover in the map (coc_jumper)
static int ledge_why;   // (tests, ledge_sweep: which check of player_ledge said no - the n-th 'return 0' of it)
#define LW(n) (ledge_why = (n), 0)
static int player_ledge(const Player *p, V3 dir, V3 *dest, float *peak) {
	ledge_why = 0;
	float h = p->height > 0 ? p->height : P_HEIGHT;
	V3 o = v3(p->pos.x, p->pos.y, p->pos.z + 50); int ti = -1;
	float hw = raycast_mask(o, dir, P_RADIUS + 55, TF_PLAYER, &ti);
	if (hw < 0) { o.z = p->pos.z + 110; hw = raycast_mask(o, dir, P_RADIUS + 55, TF_PLAYER, &ti); if (hw < 0) return LW(1); }
	if (ti >= 0 && fabsf(ctris[ti].n.z) > 0.7f) return LW(2);     // a slope, not a wall
	ledge_cover = ti >= 0 && (ctris[ti].flags & TF_CLIMB);
	// the top of the obstacle: the lowest height at which the way ahead is clear (10 cm steps)
	float top = -1e9f;
	for (float z = 40; z <= P_CLIMB_MAX + 10; z += 10)
		if (raycast_mask(v3(p->pos.x, p->pos.y, p->pos.z + z), dir, hw + 30, TF_PLAYER, NULL) < 0) { top = p->pos.z + z - 5; break; }
	if (top < -1e8f) return LW(3);
	// a top surface just behind the face (box, ledge, sill) gives the exact height
	int has_top = 0;
	{ V3 a = v3(p->pos.x + dir.x * (hw + 12), p->pos.y + dir.y * (hw + 12), top + 30);
	  float hd = raycast_mask(a, v3(0, 0, -1), 60, TF_PLAYER, &ti);
	  if (hd >= 0 && ti >= 0 && fabsf(ctris[ti].n.z) >= P_WALKABLE) { top = a.z - hd; has_top = 1; } }
	float rise = top - p->pos.z; if (rise < 40 || rise > P_CLIMB_MAX) return LW(4);
	// room to rise (the body goes over the edge crouched: feet just above the top, head ~100 cm higher)
	float need = top + 25 + P_HEIGHT_SLIDE - (p->pos.z + h - 20);
	if (need > 0 && raycast_mask(v3(p->pos.x, p->pos.y, p->pos.z + h - 20), v3(0, 0, 1), need, TF_PLAYER, NULL) >= 0) return LW(5);
	if (raycast_mask(v3(p->pos.x, p->pos.y, top + 35), dir, hw + 40, TF_PLAYER, NULL) >= 0) return LW(6);   // nothing in the way over the edge
	if (raycast_mask(v3(p->pos.x, p->pos.y, top + 25 + P_HEIGHT_SLIDE - 10), dir, hw + 40, TF_PLAYER, NULL) >= 0) return LW(7);
	*peak = top + 25;
	if (rise <= P_VAULT_MAX) for (float ah = hw + 60; ah <= hw + 270; ah += 20) {   // low cover with lower floor behind it: vault over
		V3 b = v3(p->pos.x + dir.x * ah, p->pos.y + dir.y * ah, top + 40);
		float fd = raycast_mask(b, v3(0, 0, -1), 450, TF_PLAYER, &ti); if (fd < 0) break;
		float fb = b.z - fd; if (top - fb < 30) continue;          // still on top of the cover
		if (ti < 0 || fabsf(ctris[ti].n.z) < P_WALKABLE) continue;   // over the far (sloped) face of the cover
		if (raycast_mask(v3(p->pos.x, p->pos.y, top + 40), dir, ah, TF_PLAYER, NULL) >= 0) break;
		if (raycast_mask(v3(b.x, b.y, fb + 5), v3(0, 0, 1), h - 5, TF_PLAYER, NULL) >= 0) continue;
		V3 d = v3(b.x, b.y, fb + 1); if (body_blocked(d, h, 2.0f)) continue;
		*dest = d; return 2;
	}
	if (!has_top) return LW(8);
	V3 a = v3(p->pos.x + dir.x * (hw + 40), p->pos.y + dir.y * (hw + 40), top + 1);   // climb up and stand on the top
	{ float hd = raycast_mask(v3(a.x, a.y, top + 30), v3(0, 0, -1), 60, TF_PLAYER, &ti); if (hd < 0 || ti < 0 || fabsf(ctris[ti].n.z) < P_WALKABLE) return LW(9); a.z = top + 30 - hd + 1; }
	if (raycast_mask(v3(a.x, a.y, a.z + 5), v3(0, 0, 1), h - 5, TF_PLAYER, NULL) >= 0) return LW(10);  // room to stand on top
	if (body_blocked(a, h, 2.0f)) return LW(11);
	*dest = a; *peak = a.z + 1; return 1;
}
#undef LW

// b87 (user: "make sure you can move around and traverse the game like you can in the pc version ... ladders that looked like
// they could be climbed that didn't function"): the maps' special maneuvers (map.dat Specialmaneuver.smf, tools/smf/make_smf.py
// -> smf.txt): authored paths from a floor zone over / onto / through an obstacle - vaults over cover and through windows,
// climbs onto ledges, containers and ladders' tops, leaps from upper walkways over the railing to the floor below (202 on the
// 16 maps). Jump inside a zone while moving (or facing) towards the path's end, within 75 degrees: the body follows the
// authored path (kept at its own sideways offset in the zone), the time from its length (rise ~2.9 m/s, across ~5 m/s, drops
// as a fall). Checked before the ledge / cover raycasts (player_ledge), which still cover everything the maps do not mark.
typedef struct { float zx[4], zy[4], zz; V3 p[4], f[4]; } SmfMan;   // zone corners / height, path, the obstacle's face
static SmfMan *smf_m; static int smf_n; static char smf_dbg[400];
static void smf_free(void) { free(smf_m); smf_m = NULL; smf_n = 0; }
static int smf_load(const char *path, const char *stem) {
	smf_free(); FILE *f = fopen(path, "rb"); if (!f) return 0; char ln[400]; int cap = 0;
	while (fgets(ln, sizeof(ln), f)) {
		char st[32]; SmfMan m; if (ln[0] == '#') continue;
		int nr = 0; char *q = ln; float v[33];
		if (sscanf(q, "%31s%n", st, &nr) != 1 || strcmp(st, stem)) continue; q += nr;
		int nv = 0; while (nv < 33 && sscanf(q, "%f%n", &v[nv], &nr) == 1) { q += nr; nv++; }
		if (nv < 21) continue;
		for (int k = 0; k < 4; k++) { m.zx[k] = v[2 * k]; m.zy[k] = v[2 * k + 1]; m.p[k] = v3(v[9 + 3 * k], v[10 + 3 * k], v[11 + 3 * k]); m.f[k] = nv >= 33 ? v3(v[21 + 3 * k], v[22 + 3 * k], v[23 + 3 * k]) : m.p[3]; }
		m.zz = v[8];
		if (smf_n == cap) { cap = cap ? cap * 2 : 16; SmfMan *n = realloc(smf_m, sizeof(SmfMan) * cap); if (!n) break; smf_m = n; }
		smf_m[smf_n++] = m;
	}
	fclose(f); return smf_n;
}
static int smf_inside(const SmfMan *m, float x, float y) {   // the zone's quad (either winding), grown 20 cm
	int pos = 0, neg = 0;
	for (int k = 0; k < 4; k++) { float ax = m->zx[k], ay = m->zy[k], bx = m->zx[(k + 1) & 3], by = m->zy[(k + 1) & 3], ex = bx - ax, ey = by - ay, l = sqrtf(ex * ex + ey * ey) + 1e-3f;
		float c = (ex * (y - ay) - ey * (x - ax)) / l; if (c > -20) pos++; if (c < 20) neg++; }
	return pos == 4 || neg == 4;
}
// the maneuver a jump would start now (moving / facing dir), -1 none
static int smf_pick(const Player *p, V3 dir) {
	int best = -1; float bc = cosf(75 * 3.14159f / 180);
	for (int i = 0; i < smf_n; i++) { const SmfMan *m = &smf_m[i];
		if (fabsf(p->pos.z - m->zz) > 70 || !smf_inside(m, p->pos.x, p->pos.y)) continue;
		float dx = m->p[3].x - m->p[0].x, dy = m->p[3].y - m->p[0].y, l = sqrtf(dx * dx + dy * dy);
		if (l < 30) { dx = m->p[2].x - m->p[0].x; dy = m->p[2].y - m->p[0].y; l = sqrtf(dx * dx + dy * dy); }   // a climb straight up: its small step in
		if (l < 1) continue; float c = (dx * dir.x + dy * dir.y) / l; if (c > bc) { bc = c; best = i; } }
	return best;
}
// b104 (user, on the Vita: "running over the buses in urban felt really slow and like the player went too high ... the part
// where you climb some ladder to go into a small corridor ... really slow and not seamless and ... you clipped through the top
// of the corridor"). What the records are, checked against every map's collision (16 maps, 202 records):
//  - the four path points are a cubic Bezier's control points, not points the body passes: the standard vault (0 / +103 /
//    +132 / -12 cm over 4.2 m) is an arc 87 cm high; a leap off a walkway (+103 / +307 / -373) rises 1.1 m, not 3 m. b87-b103
//    ran the body THROUGH the points: 45 cm too high over cover and through windows (a bus's window ends 2.87 m up; the eye
//    passed at 2.9 m), 2 m too high off the walkways.
//  - a climb's curve ends where the body HANGS: on all 34 climbs the floor is 1.75-2.1 m above that end (the hands' reach),
//    just beyond the face. The face quad's top is 1.5-3.5 m above it: b87 climbed to that top - right by chance where a low
//    wall ends at the floor, 0.6-1.5 m too high on twelve climbs (Urban's two: through the corridor's ceiling, then a fall).
//  - times from the game's own clips: Climb_start 0.43 s (the jump to the hang), Climb_catch 0.33 + Climb_up 0.70 s (the pull
//    up, ~1.95 m); Window_Up 0.30 s + Window_Break 0.50 (f) / 0.77 s (m) for the standard 4.2 m - ~0.93 s. b87: 1.7 s and ~3 s.
// (b109) the path of maneuver i from where the body stands, into p->sm_*: keep = how much of the body's sideways offset in the
// zone the path keeps (1 all, 0 = the authored line). Returns the time; *okp: a climb 1 its floor found, 2 the face's top, 0 none; a
// vault 1 its landing found. smf_start picks keep.
static float smf_build(Player *p, int i, float keep, int *okp) {
	const SmfMan *m = &smf_m[i]; V3 d = vsub(m->p[3], m->p[0]); d.z = 0; float l = sqrtf(vdot(d, d)); if (l < 1) d = v3(cosf(p->yaw), sinf(p->yaw), 0); else d = vmul(d, 1 / l);
	V3 off = vsub(p->pos, m->p[0]); off.z = 0; off = vsub(off, vmul(d, vdot(off, d))); off = vmul(off, keep);   // the body's sideways place in the zone
	p->sm_p[0] = p->pos; for (int k = 1; k < 4; k++) p->sm_p[k] = vadd(m->p[k], off); p->sm_n = 4; p->sm_duck = 0;
	float h = p->height > 0 ? p->height : P_HEIGHT; smf_dbg[0] = 0;
	float ftop = fmaxf(fmaxf(m->f[0].z, m->f[1].z), fmaxf(m->f[2].z, m->f[3].z)), hl = sqrtf((m->p[3].x - m->p[0].x) * (m->p[3].x - m->p[0].x) + (m->p[3].y - m->p[0].y) * (m->p[3].y - m->p[0].y));
	float T;
	if (ftop > p->sm_p[3].z + 60 && hl < 120) {   // a climb (a wall, a ladder, a container): the curve to the hang, then the pull up onto the floor
		V3 a = m->f[0], b = m->f[1]; if (fabsf(m->f[1].z - m->f[0].z) > 50) b = m->f[3];   // two corners at the same height: the face's line
		V3 t = vsub(b, a); t.z = 0; float tl = sqrtf(vdot(t, t)); V3 n = tl > 1 ? v3(-t.y / tl, t.x / tl, 0) : d;
		if (vdot(n, d) < 0) n = vmul(n, -1);   // pointing away from the start, through the face
		V3 e = p->sm_p[3]; float dp = vdot(vsub(e, a), n); V3 onp = vsub(e, vmul(n, dp));   // the end, on the face's plane
		V3 c = v3(onp.x + n.x * 45, onp.y + n.y * 45, ftop + 4); int ok = 0;
		// the floor: the lowest one the body can stand on, 0.8-2.8 m above the hang, nearest beyond the face
		for (float sd = 45; sd <= 150 && !ok; sd += 15) for (float zz = e.z + 80; zz <= e.z + 280 && !ok; zz += 20) {
			int ti = -1; float hd = raycast_mask(v3(onp.x + n.x * sd, onp.y + n.y * sd, zz + 25), v3(0, 0, -1), 50, TF_PLAYER, &ti);
			if (hd < 0 || ti < 0 || fabsf(ctris[ti].n.z) < P_WALKABLE) continue;
			V3 cc = v3(onp.x + n.x * sd, onp.y + n.y * sd, zz + 25 - hd + 1); if (!body_blocked(cc, h, 2.0f)) { c = cc; ok = 1; } }
		for (int k = 0; k < 4 && !ok; k++) { V3 cc = v3(onp.x + n.x * (45 + 15.f * k), onp.y + n.y * (45 + 15.f * k), ftop + 4); int ti = -1; float hd = raycast_mask(v3(cc.x, cc.y, cc.z + 60), v3(0, 0, -1), 160, TF_PLAYER, &ti);   // (none found: the face's top, as b87)
			if (hd < 0 || ti < 0 || fabsf(ctris[ti].n.z) < P_WALKABLE) continue; cc.z = cc.z + 60 - hd + 1; if (!body_blocked(cc, h, 2.0f)) { c = cc; ok = 2; } }
		p->sm_p[4] = c; p->sm_n = 5; float up = c.z - e.z, ku = up / 195; ku = ku < 0.6f ? 0.6f : ku > 1.5f ? 1.5f : ku;
		p->sm_cum[1] = 0.43f; T = 0.43f + 1.03f * ku;
		snprintf(smf_dbg, sizeof(smf_dbg), " climb: hang %.0f, floor %.0f (%+.0f, %s), face top %.0f, %.2f s", e.z, c.z, up, ok == 1 ? "found" : ok ? "at the face's top" : "none: drops", ftop, T);
		*okp = ok;
	} else {   // a vault / leap: its end on the floor under it
		V3 e = p->sm_p[3]; int ti = -1; float hd = raycast_mask(v3(e.x, e.y, e.z + 60), v3(0, 0, -1), 260, TF_PLAYER, &ti); *okp = 0;
		if (hd >= 0 && ti >= 0 && fabsf(ctris[ti].n.z) >= P_WALKABLE) { V3 c = v3(e.x, e.y, e.z + 60 - hd + 1); if (!body_blocked(c, h, 2.0f)) { p->sm_p[3] = c; *okp = 1; } }
		V3 tr = vsub(p->sm_p[3], p->sm_p[0]); float across = sqrtf(tr.x * tr.x + tr.y * tr.y), drop = p->sm_p[0].z - p->sm_p[3].z - 60;
		T = 0.23f + across / 600 + (drop > 0 ? sqrtf(2 * drop / 980) * 0.5f : 0); if (T < 0.45f) T = 0.45f; p->sm_cum[1] = T;
		// the opening's top (the face quad: a window's frame) lower than the standing body at the arc's top: the eye ducks under it
		float apex = 0; for (int q = 1; q < 8; q++) { float u = q / 8.f, w = 1 - u, z = w * w * w * p->sm_p[0].z + 3 * w * w * u * p->sm_p[1].z + 3 * w * u * u * p->sm_p[2].z + u * u * u * p->sm_p[3].z; if (z > apex || q == 1) apex = z; }
		float over = apex + P_EYE + 22 - ftop; if (ftop > p->sm_p[0].z + 150 && over > 0) p->sm_duck = over > 50 ? 50 : over;
		snprintf(smf_dbg, sizeof(smf_dbg), " vault: arc top %+.0f, across %.0f, opening top %+.0f (duck %.0f), %.2f s", apex - p->sm_p[0].z, across, ftop - p->sm_p[0].z, p->sm_duck, T);
	}
	p->sm_p[p->sm_n - 1].z += 1; p->sm_cum[0] = 0;
	return T;
}
// b109 (user: "when you climb into the small corridor in urban from the far left side of its entrance ... you either clip out of
// the map to the left or overextend and clip out a bit at the top before entering"). b87-b108 kept the body's whole sideways
// offset in the zone: right over a long piece of cover, wrong where the zone is wider than what is climbed - Urban's ladders
// (zone 2.5 m wide, the passage 1.3 m): from the zone's edge the path ran up the wall beside the opening, found no floor there
// (-> the face's top, through the ceiling) or one beyond the wall. Now the offset is kept only as far as the whole offset path
// stays in the open with the authored one: the eye of both paths in sight of each other at nine moments, the same floor at
// the end (25 cm) - else less of it, down to the authored line (the body slides to the ladder as it jumps to it).
static V3 smf_at(const Player *p, float t);
static int smf_why;   // (tests: why the last check failed - 100 the floors' heights, q / 10 + q the q-th moment's sight line from the authored path / to it)
static int smf_same_room(const Player *a, const Player *b) {   // path b (authored) and path a: in sight of each other all the way, ending on one floor
	if (fabsf(a->sm_p[a->sm_n - 1].z - b->sm_p[b->sm_n - 1].z) > 25) { smf_why = 100; return 0; }
	for (int q = 1; q <= 9; q++) { V3 pa = smf_at(a, a->mantle_len * q / 9), pb = smf_at(b, b->mantle_len * q / 9); pa.z += P_EYE - 12; pb.z += P_EYE - 12;
		V3 e = vsub(pa, pb); float dl = sqrtf(vdot(e, e)); if (dl < 8) continue; int ti = -1; float hd = raycast_mask(pb, vmul(e, 1 / dl), dl, TF_PLAYER, &ti); if (hd >= 0 && hd < dl - 2) { smf_why = q; return 0; }
		hd = raycast_mask(pa, vmul(e, -1 / dl), dl, TF_PLAYER, &ti); if (hd >= 0 && hd < dl - 2) { smf_why = 10 + q; return 0; } }   // (both ways: a wall seen from its back is not hit)
	return 1;
}
static float smf_keep_dbg;
static void smf_start(Player *p, int i) {
	static Player ref, cand; int ok0 = 0, ok = 0; float T, kept = 0;   // (static: a Player is large for the stack of a Vita thread)
	ref = *p; ref.mantle_len = smf_build(&ref, i, 0, &ok0); char dbg0[sizeof(smf_dbg)]; memcpy(dbg0, smf_dbg, sizeof(dbg0));
	int found = 0;
	if (ok0 == 1) for (int q = 0; q < 8 && !found; q++) { float keep = 1 - q * 0.14f;
		cand = *p; cand.mantle_len = smf_build(&cand, i, keep, &ok); if (ok == 1 && smf_same_room(&cand, &ref)) { found = 1; kept = keep; } }
	else { cand = *p; cand.mantle_len = smf_build(&cand, i, 1, &ok); found = 1; kept = 1; }   // (the authored line itself finds no floor: as before b109)
	if (!found) { cand = ref; memcpy(smf_dbg, dbg0, sizeof(dbg0)); }
	smf_keep_dbg = kept; T = cand.mantle_len; memcpy(p->sm_p, cand.sm_p, sizeof(p->sm_p)); memcpy(p->sm_cum, cand.sm_cum, sizeof(p->sm_cum)); p->sm_n = cand.sm_n; p->sm_duck = cand.sm_duck;
	{ int l = (int)strlen(smf_dbg); snprintf(smf_dbg + l, sizeof(smf_dbg) - l, ", sideways offset kept %.0f%%", kept * 100); }
	p->mantle_kind = 3; p->mantle_len = p->mantle_t = T; p->m_from = p->pos; p->m_to = p->sm_p[p->sm_n - 1]; p->m_peak = fmaxf(p->sm_p[1].z, p->sm_p[2].z);
	p->vel = v3(0, 0, 0); p->events |= p->m_to.z > p->pos.z + 60 ? PE_CLIMB : PE_VAULT;
}
static V3 smf_at(const Player *p, float t) {   // the body at time t: the authored Bezier, then (a climb) the pull up onto the floor
	float tj = p->sm_cum[1];
	if (t <= tj || p->sm_n < 5) { float u = tj > 1e-4f ? t / tj : 1; u = u < 0 ? 0 : u > 1 ? 1 : u; float w = 1 - u, b0 = w * w * w, b1 = 3 * w * w * u, b2 = 3 * w * u * u, b3 = u * u * u;
		const V3 *P = p->sm_p; return v3(b0 * P[0].x + b1 * P[1].x + b2 * P[2].x + b3 * P[3].x, b0 * P[0].y + b1 * P[1].y + b2 * P[2].y + b3 * P[3].y, b0 * P[0].z + b1 * P[1].z + b2 * P[2].z + b3 * P[3].z); }
	// the pull up: the height first (it keeps the curve's last speed: no stop at the hang), the step in over its second half
	float u = (t - tj) / fmaxf(p->mantle_len - tj, 1e-4f); u = u > 1 ? 1 : u; V3 e = p->sm_p[3], c = p->sm_p[4];
	float tu = fmaxf(p->mantle_len - tj, 1e-4f) * 0.82f, up = c.z - e.z, m0 = fabsf(up) > 1 ? 3 * (p->sm_p[3].z - p->sm_p[2].z) / fmaxf(tj, 1e-4f) * tu / up : 0; m0 = m0 < 0 ? 0 : m0 > 2.5f ? 2.5f : m0;
	float uz = u / 0.82f; uz = uz > 1 ? 1 : uz; float sz = (uz * uz * uz - 2 * uz * uz + uz) * m0 + (3 - 2 * uz) * uz * uz, ux = (u - 0.45f) / 0.55f; ux = ux < 0 ? 0 : ux; float sx = ux * ux * (3 - 2 * ux);
	return v3(e.x + (c.x - e.x) * sx, e.y + (c.y - e.y) * sx, e.z + (c.z - e.z) * sz);
}
// b109 test (smftest, at the map's load): every maneuver started from every 10 cm across its zone (and the 20 cm the zone is grown by),
// planned only - where does the path of the b108 way (the whole sideways offset) and of the b109 way leave the authored path's space
static void (*smf_logf)(const char *fmt, ...);
static void smf_sweep(const Player *pl, const char *stem) {
	if (!smf_logf) return; static Player q, ref, old; int starts = 0, bad_old = 0, bad_new = 0, noref = 0, nman_old = 0;
	for (int i = 0; i < smf_n; i++) { const SmfMan *m = &smf_m[i]; float cx = (m->zx[0] + m->zx[1] + m->zx[2] + m->zx[3]) / 4, cy = (m->zy[0] + m->zy[1] + m->zy[2] + m->zy[3]) / 4;
		float dx = m->p[3].x - m->p[0].x, dy = m->p[3].y - m->p[0].y, l = sqrtf(dx * dx + dy * dy); if (l < 30) { dx = m->p[2].x - m->p[0].x; dy = m->p[2].y - m->p[0].y; l = sqrtf(dx * dx + dy * dy); } if (l < 1) continue;
		dx /= l; dy /= l; float px = -dy, py = dx, lo = 0, hi = 0; for (int k = 0; k < 4; k++) { float t = (m->zx[k] - cx) * px + (m->zy[k] - cy) * py; if (t < lo) lo = t; if (t > hi) hi = t; }
		int bo = 0, bn = 0, n = 0, nr = 0, why1 = 0, kind = 0; float kmin = 1, wo = 0;
		for (float sd = lo - 18; sd <= hi + 18; sd += 10) { float x = cx + px * sd, y = cy + py * sd; if (!smf_inside(m, x, y)) continue; n++;
			q = *pl; q.pos = v3(x, y, m->zz + 2); q.yaw = atan2f(dy, dx); q.height = P_HEIGHT; q.mantle_t = 0; int ok0 = 0, ok1 = 0;
			ref = q; ref.mantle_len = smf_build(&ref, i, 0, &ok0); old = q; old.mantle_len = smf_build(&old, i, 1, &ok1);
			if (ok0 != 1) nr++;
			if (ok0 == 1 && (ok1 != 1 || !smf_same_room(&old, &ref))) { if (!bo) { why1 = ok1 != 1 ? 200 + ok1 : smf_why; kind = ref.sm_n; } bo++; if (fabsf(sd) > wo) wo = fabsf(sd); }
			smf_start(&q, i); if (smf_keep_dbg < kmin) kmin = smf_keep_dbg;
			if (ok0 == 1) { old = q; old.mantle_len = q.mantle_len; if (!smf_same_room(&old, &ref)) bn++; } }
		starts += n; bad_old += bo; bad_new += bn; noref += nr; if (bo) nman_old++;
		if (bo || bn || nr) smf_logf("smfsweep %s %2d: zone %.0f cm wide, %d starts: the whole offset leaves the authored space from %d of them (%.0f cm off the middle and beyond), now %d (least offset kept %.0f%%; %s, first reason %d)%s\n", stem, i, hi - lo, n, bo, wo, bn, kmin * 100, kind == 5 ? "climb" : "vault", why1, nr ? "; THE AUTHORED LINE FINDS NO FLOOR" : ""); }
	smf_logf("smfsweep %s: %d maneuvers, %d starts across their zones; the whole offset (b108) left the authored space from %d starts on %d maneuvers; now %d; authored lines without a floor: %d starts\n", stem, smf_n, starts, bad_old, nman_old, bad_new, noref);
}
// b122 (user: "make sure that you're able to climb everything that you're supposed to be able to climb, in the shooting range
// there seemed to be obstacles that you can't climb over but look like you should be able to climb over"): test tool, switch
// ledgesweep - every place the body can stand, found by walking out from the spawn point on a 40 cm grid (steps up to P_STEP,
// any drop) and going on from wherever a climb, a vault or a special maneuver ends; from each place in 8 directions: is there
// an obstacle whose top is 45..300 cm above the feet, and does a jump at it do anything? Those where nothing happens are
// listed in groups (150 cm cells of the obstacle's face, its top's height) with the check of player_ledge that said no.
typedef struct { short ix, iy; float z; } LsNode;
static void ledge_sweep(const Player *pl, const char *stem) {
	enum { CAP = 60000, HN = 1 << 17, GC = 400 }; const float G = 40;
	LsNode *q = calloc(CAP, sizeof(LsNode)); int *ht = malloc(sizeof(int) * HN); if (!q || !ht) { free(q); free(ht); return; }
	typedef struct { int kx, ky, kz, n, why[12], smf; float px, py, pz, yaw, rise, rmin, rmax; } Grp; Grp *g = calloc(GC, sizeof(Grp)); int gn = 0, nq = 0, head = 0, climbs = 0, vaults = 0, sms = 0, fails = 0;
	for (int i = 0; i < HN; i++) ht[i] = -1;
	#define LS_ADD(X, Y, Z) do { int ix_ = (int)floorf((X) / G + 0.5f), iy_ = (int)floorf((Y) / G + 0.5f), iz_ = (int)floorf((Z) / 70 + 0.5f); unsigned h_ = ((unsigned)ix_ * 73856093u ^ (unsigned)iy_ * 19349663u ^ (unsigned)iz_ * 83492791u) & (HN - 1); int f_ = 0; \
		while (ht[h_] >= 0) { LsNode *o_ = &q[ht[h_]]; if (o_->ix == ix_ && o_->iy == iy_ && (int)floorf(o_->z / 70 + 0.5f) == iz_) { f_ = 1; break; } h_ = (h_ + 1) & (HN - 1); } \
		if (!f_ && nq < CAP) { q[nq].ix = (short)ix_; q[nq].iy = (short)iy_; q[nq].z = (Z); ht[h_] = nq++; } } while (0)
	LS_ADD(pl->pos.x, pl->pos.y, pl->pos.z);
	static const float DX[8] = { 1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f }, DY[8] = { 0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f };
	while (head < nq) { LsNode nd = q[head++]; V3 p = v3(nd.ix * G, nd.iy * G, nd.z);
		for (int d = 0; d < 8; d++) { V3 dir = v3(DX[d], DY[d], 0); float step = d & 1 ? G * 1.4142f : G; int ti = -1;
			// walking on: nothing in the way above a step's height, a floor to stand on, room for the body
			if (raycast_mask(v3(p.x, p.y, p.z + P_STEP + 12), dir, step + 4, TF_PLAYER, NULL) < 0) {
				V3 t = v3(p.x + dir.x * step, p.y + dir.y * step, p.z + P_STEP + 12); float fd = raycast_mask(t, v3(0, 0, -1), 900, TF_PLAYER, &ti);
				if (fd >= 0 && ti >= 0 && fabsf(ctris[ti].n.z) >= P_WALKABLE) { V3 e = v3(t.x, t.y, t.z - fd + 1); if (!body_blocked(e, P_HEIGHT, 2.0f)) LS_ADD(e.x, e.y, e.z); } }
			Player b = *pl; b.pos = v3(p.x, p.y, p.z); b.grounded = 1; b.height = P_HEIGHT; b.mantle_t = 0; b.slide_t = 0; b.yaw = atan2f(dir.y, dir.x);
			int sm = smf_n ? smf_pick(&b, dir) : -1; V3 dest; float peak;
			if (sm >= 0) { Player c = b; smf_start(&c, sm); sms++; LS_ADD(c.m_to.x, c.m_to.y, c.m_to.z); continue; }
			int k = player_ledge(&b, dir, &dest, &peak); if (k) { if (k == 1) climbs++; else vaults++; LS_ADD(dest.x, dest.y, dest.z); continue; }
			// nothing happens: is there something of a climbable height?
			float hw = raycast_mask(v3(p.x, p.y, p.z + 50), dir, P_RADIUS + 55, TF_PLAYER, &ti); if (hw < 0) hw = raycast_mask(v3(p.x, p.y, p.z + 110), dir, P_RADIUS + 55, TF_PLAYER, &ti);
			if (hw < 0 || (ti >= 0 && fabsf(ctris[ti].n.z) > 0.7f)) continue;
			float top = -1; for (float z = 40; z <= 300; z += 10) if (raycast_mask(v3(p.x, p.y, p.z + z), dir, hw + 30, TF_PLAYER, NULL) < 0) { top = z - 5; break; }
			if (top < 45) continue;
			fails++; float hx = p.x + dir.x * hw, hy = p.y + dir.y * hw; int kx = (int)floorf(hx / 150), ky = (int)floorf(hy / 150), kz = (int)floorf((p.z + top) / 40), gi = -1;
			for (int i = 0; i < gn; i++) if (g[i].kx == kx && g[i].ky == ky && g[i].kz == kz) { gi = i; break; }
			if (gi < 0 && gn < GC) { gi = gn++; g[gi].kx = kx; g[gi].ky = ky; g[gi].kz = kz; g[gi].px = p.x; g[gi].py = p.y; g[gi].pz = p.z; g[gi].yaw = b.yaw; g[gi].rmin = 1e9f; g[gi].rmax = -1e9f; }
			if (gi >= 0) { g[gi].n++; g[gi].why[ledge_why < 12 ? ledge_why : 0]++; if (top < g[gi].rmin) g[gi].rmin = top; if (top > g[gi].rmax) g[gi].rmax = top; } } }
	smf_logf("ledgesweep %s: %d places to stand (%s), %d climbs, %d vaults, %d special maneuvers from them; %d jumps at an obstacle 45-300 cm high do nothing, in %d groups%s\n", stem, nq, nq >= CAP ? "the table full" : "all found", climbs, vaults, sms, fails, gn, gn >= GC ? " (table full)" : "");
	for (int i = 0; i < gn; i++) { Grp *r = &g[i]; char w[160]; int o = 0; w[0] = 0; for (int k = 0; k < 12; k++) if (r->why[k]) o += snprintf(w + o, sizeof(w) - o, " %d:%d", k, r->why[k]);
		smf_logf("ledgesweep %s group %3d: face near %6d %6d, top %4.0f..%4.0f cm above the feet, %3d tries, checks%s; e.g. from %.0f %.0f %.0f yaw %.0f\n", stem, i, r->kx * 150 + 75, r->ky * 150 + 75, r->rmin, r->rmax, r->n, w, r->px, r->py, r->pz, r->yaw * 57.2958f); }
	#undef LS_ADD
	free(q); free(ht); free(g);
}
// b59: the view's tilt (radians) while climbing / vaulting - a vault dips it as the body goes over (about 7 degrees), a
// climb looks up at the ledge, then down over it - eased in and out, 0 at both ends
static float mantle_view_pitch(const Player *p) {
	if (p->mantle_t <= 0 || p->mantle_len <= 0) return 0;
	float k = 1 - p->mantle_t / p->mantle_len; if (k < 0) k = 0; if (k > 1) k = 1; float s = sinf(k * 3.14159f);
	if (p->mantle_kind == 2) return -0.12f * s * s;
	if (p->mantle_kind == 3) return p->m_to.z < p->m_from.z - 60 ? -0.16f * s * s : 0.05f * sinf(k * 6.2832f) * s;   // b87: a leap down looks down over the edge
	return 0.07f * sinf(k * 6.2832f) * s;   // up first (looking at the edge), then down as the body rolls over it
}
// HUD prompt helper (the original shows "W + SPACE" in front of climbable cover and windows): what a jump would do now,
// facing yaw. 0 nothing / a plain jump, 1 climb onto a ledge, 2 vault over; +4 when the obstacle is marked cover in
// the map (the original's prompt spots). Costs ~25 short raycasts: call it every few frames, not every frame.
static int player_ledge_ahead(const Player *p) {
	if (!p->grounded || p->mantle_t > 0 || p->slide_t > 0) return 0;
	{ int sm = smf_n ? smf_pick(p, v3(cosf(p->yaw), sinf(p->yaw), 0)) : -1; if (sm >= 0) return smf_m[sm].p[3].z > p->pos.z + 60 ? 1 : 2; }   // b87: a special maneuver
	V3 dest; float peak; int k = player_ledge(p, v3(cosf(p->yaw), sinf(p->yaw), 0), &dest, &peak);
	return k ? k | (ledge_cover ? 4 : 0) : 0;
}

static void player_update(Player *p, float mx, float my, int sprint, int jump, float dt) {
	if (dt > 0.1f) dt = 0.1f;
	if (p->height <= 0) p->height = P_HEIGHT;
	if (p->eye <= 0) p->eye = P_EYE;
	p->events = 0;
	float mul = p->speed_mul > 0 ? p->speed_mul : 1.0f;
	if (p->mantle_t > 0) { // climbing / vaulting: up first, then over the edge
		// b59 (user: climbing / vaulting looked jittery, with pops; smooth like the PC version): eased curves without corners - the
		// rise eases out to the top (it starts as a jump does), the move over the edge eases in and out, the drop to the far
		// floor eases in and out (before: straight lines meeting at corners, the forward move starting and stopping at once)
		p->mantle_t -= dt; float k = 1 - p->mantle_t / p->mantle_len; if (k > 1) k = 1;
		if (p->mantle_kind == 3) {   // b87: a special maneuver - the authored path
			p->pos = smf_at(p, p->mantle_len - (p->mantle_t > 0 ? p->mantle_t : 0)); p->vel = v3(0, 0, 0); p->grounded = 0;
			if (p->mantle_t <= 0) { p->mantle_t = 0; p->pos = p->sm_p[p->sm_n - 1]; p->grounded = 1; p->mantle_kind = 0; p->air_t = 0; p->keep_run = 0;
				// b104: a vault lands running when the stick is held forward (b87-b103 stopped dead at every landing)
				if (p->sm_n < 5 && my > 0.3f) { float sp_ = (sprint ? P_SPRINT : P_RUN) * mul; p->vel = v3(cosf(p->yaw) * sp_, sinf(p->yaw) * sp_, 0); } }
			{ float target = p->crouched ? P_EYE_CROUCH : P_EYE; if (p->sm_duck > 0 && p->mantle_t > 0) target -= p->sm_duck * sinf(k * 3.14159f); p->eye += (target - p->eye) * fminf(1, 10 * dt); }
			p->was_crouch_in = p->in_crouch ? 1 : 0;
			return;
		}
		float kx = (k - 0.3f) / 0.65f, z; kx = kx < 0 ? 0 : kx > 1 ? 1 : kx; kx = kx * kx * (3 - 2 * kx);
		if (k < 0.55f) { float u = k / 0.55f; z = p->m_from.z + (p->m_peak - p->m_from.z) * (1 - (1 - u) * (1 - u) * (1 - u)); }
		else { float u = (k - 0.55f) / 0.45f; z = p->m_peak + (p->m_to.z - p->m_peak) * (u * u * (3 - 2 * u)); }
		p->pos = v3(p->m_from.x + (p->m_to.x - p->m_from.x) * kx, p->m_from.y + (p->m_to.y - p->m_from.y) * kx, z);
		p->vel = v3(0, 0, 0); p->grounded = 0;
		if (p->mantle_t <= 0) { p->mantle_t = 0; p->pos = p->m_to; p->grounded = 1; p->mantle_kind = 0; p->air_t = 0;
			if (p->keep_run) { V3 f = v3(cosf(p->yaw), sinf(p->yaw), 0); p->vel = vmul(f, P_RUN * mul); } p->keep_run = 0; }
		{ float target = p->crouched ? P_EYE_CROUCH : P_EYE; p->eye += (target - p->eye) * fminf(1, 10 * dt); }
		p->was_crouch_in = p->in_crouch ? 1 : 0;
		return;
	}
	float cy = cosf(p->yaw), sy = sinf(p->yaw);
	V3 fwd = v3(cy, sy, 0), rgt = v3(sy, -cy, 0);
	V3 wish = vadd(vmul(fwd, my), vmul(rgt, mx)); float wl = sqrtf(vdot(wish, wish)); if (wl > 1) { wish = vmul(wish, 1 / wl); wl = 1; }
	float hs = sqrtf(p->vel.x * p->vel.x + p->vel.y * p->vel.y);
	// ---- crouch / slide / stand
	int crouch_press = p->in_crouch && !p->was_crouch_in;
	int sprinting = sprint && my > 0.3f && fabsf(mx) < 0.75f && !p->in_ads && p->slide_t <= 0;
	p->slide_cd -= dt;
	if (crouch_press && p->grounded && p->slide_t <= 0 && p->slide_cd <= 0 && (p->sprinting || sprinting) && hs > P_RUN * 0.9f * mul) {
		p->slide_t = 1e-4f; p->crouched = 1; p->height = P_HEIGHT_SLIDE; p->events |= PE_SLIDE;
		V3 d = hs > 1 ? v3(p->vel.x / hs, p->vel.y / hs, 0) : fwd; p->slide_dir = d;
		float v0 = fmaxf(hs, P_SLIDE * mul); p->vel.x = d.x * v0; p->vel.y = d.y * v0; hs = v0;
	}
	if (p->slide_t > 0) {
		p->slide_t += dt;
		if (p->slide_t >= P_SLIDE_TIME || (jump && p->slide_t > 0.15f) || (!p->in_crouch && p->slide_t > 0.25f) || (hs < P_CROUCH * 0.8f && p->slide_t > 0.1f)) {
			p->slide_t = 0; p->slide_cd = 0.4f; p->events |= PE_SLIDE_END;
			if (!body_blocked(p->pos, P_HEIGHT_CROUCH, 1.0f)) p->height = P_HEIGHT_CROUCH;
		}
	}
	if (p->slide_t <= 0) {
		int want = p->in_crouch;   // (the caller keeps the toggle; it should clear it when the player starts to sprint)
		if (want && !p->crouched) { p->crouched = 1; p->height = P_HEIGHT_CROUCH; p->events |= PE_CROUCH; }
		else if (!want && p->crouched) {   // stand up only where there is room
			if (!body_blocked(p->pos, P_HEIGHT, 1.0f)) { p->crouched = 0; p->height = P_HEIGHT; p->events |= PE_STAND; }
		}
		if (p->crouched && p->height < P_HEIGHT_CROUCH && !body_blocked(p->pos, P_HEIGHT_CROUCH, 1.0f)) p->height = P_HEIGHT_CROUCH;
	}
	p->was_crouch_in = p->in_crouch ? 1 : 0;
	if (p->crouched) sprinting = 0;
	p->sprinting = p->grounded ? sprinting : p->sprinting && sprinting;
	// ---- jump: climb or vault when there is a ledge or cover in front, else a normal jump (stands up from a crouch)
	if (p->grounded && jump && p->slide_t <= 0) {
		V3 dir = wl > 0.3f ? vmul(wish, 1 / wl) : fwd, dest; float peak;
		{ int sm = smf_n ? smf_pick(p, dir) : -1; if (sm >= 0) { smf_start(p, sm); return; } }   // b87: the map's special maneuvers first
		int kind = (wl > 0.3f || hs > 100) ? player_ledge(p, dir, &dest, &peak) : 0;
		if (kind) {
			p->m_from = p->pos; p->m_to = dest; p->m_peak = peak; p->mantle_kind = kind;
			p->mantle_len = p->mantle_t = kind == 2 ? 0.45f + (peak - p->pos.z) * 0.0015f : 0.35f + (peak - p->pos.z) * 0.0025f;
			p->keep_run = kind == 2 && hs > P_RUN * 0.8f;   // keep running after a vault
			p->vel = v3(0, 0, 0); p->events |= kind == 2 ? PE_VAULT : PE_CLIMB;
			return;
		}
		if (p->crouched) { if (!body_blocked(p->pos, P_HEIGHT, 1.0f)) { p->crouched = 0; p->height = P_HEIGHT; p->events |= PE_STAND; } else jump = 0; }
	}
	// ---- horizontal speed
	float spd = p->crouched ? P_CROUCH : sprinting ? P_SPRINT : (p->in_ads || p->in_walk) ? P_WALK : P_RUN;
	spd *= mul;
	if (p->slide_t > 0) {   // slide: keeps the burst, friction brings it down to crouch speed; the stick steers a little
		float vmin = P_CROUCH * mul, v = vmin + (hs - vmin) * expf(-dt / 0.45f); if (v < vmin) v = vmin;
		V3 d = p->slide_dir;
		if (wl > 0.3f) { V3 w = vmul(wish, 1 / wl); d = vadd(d, vmul(w, 1.5f * dt)); float l = sqrtf(d.x * d.x + d.y * d.y); if (l > 1e-3f) { d = vmul(d, 1 / l); p->slide_dir = d; } }
		if (p->grounded) { p->vel.x = d.x * v; p->vel.y = d.y * v; }
	} else {
		V3 target = vmul(wish, spd);
		float a = p->grounded ? (wl > 0.1f ? P_ACCEL : P_DECEL) : P_AIRACCEL, k = 1 - expf(-a * dt);
		p->vel.x += (target.x - p->vel.x) * k; p->vel.y += (target.y - p->vel.y) * k;
	}
	if (p->grounded && jump && p->slide_t <= 0) { p->vel.z = P_JUMP * (p->jump_mul > 0 ? p->jump_mul : 1.0f); p->grounded = 0; p->events |= PE_JUMP; }
	p->vel.z -= P_GRAVITY * dt; if (p->vel.z < -3000) p->vel.z = -3000;
	int was = p->grounded; float vz = p->vel.z;
	player_move(p, dt);
	if (!p->grounded) p->air_t += dt;
	if (p->grounded && !was && p->air_t > 0.05f) { p->events |= PE_LAND; p->land_vz = vz; if (vz < -300) p->eye_off -= fminf(16, -vz * 0.01f); }
	if (p->grounded) p->air_t = 0;
	// ---- eye height: eases to the stance height; step-ups and landings dip the camera, which recovers quickly
	float target = p->slide_t > 0 ? P_EYE_SLIDE : p->crouched ? P_EYE_CROUCH : P_EYE;
	float base = p->eye - p->eye_off; base += (target - base) * fminf(1, (p->slide_t > 0 ? 14.0f : 10.0f) * dt);
	p->eye_off *= expf(-12 * dt); if (fabsf(p->eye_off) < 0.05f) p->eye_off = 0;
	if (p->eye_off > 40) p->eye_off = 40; if (p->eye_off < -40) p->eye_off = -40;
	p->eye = base + p->eye_off;
}

// is the way ahead blocked for walking at knee height? a ray from knee (feet fz + P_STEP + 12) along d; hits on ramps
// (walkable faces) and on stairs (flat treads rising in steps <= P_STEP from the feet up to the riser hit) don't count
static int steer_knee_blocked(V3 p, V3 d, float look, float fz) {
	int tri; float t = raycast_mask(p, d, look, TF_PLAYER, &tri); if (t < 0) return 0;
	V3 n = ctris[tri].n; if (vdot(n, d) > 0) n = vmul(n, -1);
	if (n.z >= P_WALKABLE) return 0;
	float top = p.z + P_STEP + 10, prev = 1e9f;
	for (float s = t + 10; s > -10; s -= 20) {   // walk the floor profile back from just past the riser to the feet
		int ft; float x = p.x + d.x * fmaxf(s, 0), y = p.y + d.y * fmaxf(s, 0), h = raycast_mask(v3(x, y, top), v3(0, 0, -1), top - fz + 60, TF_PLAYER, &ft);
		if (h < 0 || fabsf(ctris[ft].n.z) < P_WALKABLE) return 1;   // stairs have flat treads (a sloped barrier face doesn't)
		float z = top - h; if (prev < 1e8f && prev - z > P_STEP) return 1;
		if (s == t + 10 && z < p.z - 1) return 1;   // nothing to stand on behind what the ray hit
		prev = z; top = z + 5;
	}
	return prev - fz > P_STEP;
}

// local steering for path followers (autoplay, bots): the direction closest to want (unit, xy) in which a body of
// radius r can walk at least look cm (clear at knee and chest height, floor ahead not dropping more than 1.5 m;
// ramps and stairs count as clear). Tries 0, +-20, +-40, +-60, +-85 degrees; returns want when everything is blocked.
static V3 player_steer(V3 feet, V3 want, float r, float look) {
	static const float ang[9] = { 0, 0.35f, -0.35f, 0.70f, -0.70f, 1.05f, -1.05f, 1.48f, -1.48f };
	for (int k = 0; k < 9; k++) {
		float c = cosf(ang[k]), s = sinf(ang[k]); V3 d = v3(want.x * c - want.y * s, want.x * s + want.y * c, 0), side = v3(-d.y, d.x, 0);
		int ok = 1;
		for (int o = -1; o <= 1 && ok; o++) if (raycast_mask(v3(feet.x + side.x * r * 0.85f * o, feet.y + side.y * r * 0.85f * o, feet.z + 125), d, look, TF_PLAYER, NULL) >= 0) ok = 0;
		for (int o = -1; o <= 1 && ok; o++) if (steer_knee_blocked(v3(feet.x + side.x * r * 0.85f * o, feet.y + side.y * r * 0.85f * o, feet.z + P_STEP + 12), d, look, feet.z)) ok = 0;
		if (ok) { float up = look * 0.9f; V3 q = v3(feet.x + d.x * look, feet.y + d.y * look, feet.z + 60 + up); if (raycast_mask(q, v3(0, 0, -1), 220 + up, TF_PLAYER, NULL) < 0) ok = 0; }
		if (ok) return d;
	}
	return want;
}

// can a body of radius r walk straight from feet a to feet b? (clear at knee and chest height, floor all the way;
// ramps and stairs count as clear)
static int player_clear_path(V3 a, V3 b, float r) {
	V3 d = v3(b.x - a.x, b.y - a.y, 0); float l = sqrtf(d.x * d.x + d.y * d.y); if (l < 1) return 1;
	d = vmul(d, 1 / l); V3 side = v3(-d.y, d.x, 0);
	for (int o = -1; o <= 1; o++) if (raycast_mask(v3(a.x + side.x * r * 0.85f * o, a.y + side.y * r * 0.85f * o, 125 + fmaxf(a.z, b.z)), d, l, TF_PLAYER, NULL) >= 0) return 0;
	float zl = fminf(a.z, b.z);
	for (int o = -1; o <= 1; o++) if (steer_knee_blocked(v3(a.x + side.x * r * 0.85f * o, a.y + side.y * r * 0.85f * o, zl + P_STEP + 12), d, l, zl)) return 0;
	for (float s = 40; s < l; s += 40) { V3 q = v3(a.x + d.x * s, a.y + d.y * s, a.z + (b.z - a.z) * s / l + 60); if (raycast_mask(q, v3(0, 0, -1), 160, TF_PLAYER, NULL) < 0) return 0; }
	return 1;
}

// spawn points: drop stray table entries (an exact 0 coordinate), stand each point on its floor and move it out of
// walls (the nearest free spot within 80 cm); points with no free spot nearby are dropped.
static void col_fix_spawns(void) {
	int n = 0;
	for (int i = 0; i < nspawn; i++) {
		Spawn s = spawns[i]; int ok = 0;
		if (s.x == 0.0f || s.y == 0.0f) continue;
		for (float r = 0; r <= 80 && !ok; r += 10) for (int a = 0; a < (r > 0 ? 12 : 1) && !ok; a++) {
			float x = s.x + r * cosf(a * 0.5235988f), y = s.y + r * sinf(a * 0.5235988f); int blk;
			float fz = floor_probe(x, y, s.z - 80, s.z + 60, s.z + 60, &blk, NULL);
			if (fz < -1e8f || body_blocked(v3(x, y, fz), P_HEIGHT, 0.5f)) continue;
			s.x = x; s.y = y; s.z = fz + 1; ok = 1;
		}
		if (!ok) continue;
		// never start with the face against a wall: turn round when the view ahead is blocked within 1.5 m and open behind
		V3 o = v3(s.x, s.y, s.z + P_EYE); float c = cosf(s.yaw), sn = sinf(s.yaw);
		float ahead = raycast_mask(o, v3(c, sn, 0), 400, TF_PLAYER, NULL), behind = raycast_mask(o, v3(-c, -sn, 0), 400, TF_PLAYER, NULL);
		if (ahead >= 0 && ahead < 150 && (behind < 0 || behind > 300)) s.yaw += s.yaw > 0 ? -3.14159265f : 3.14159265f;
		spawns[n++] = s;
	}
	if (n) nspawn = n; col_gen++;   // (the spawn points changed: bots.h's cached spawn sight tests)
}

// ---- grid ray walk over XY: nearest triangle with any of the flags. Returns the distance or -1 (*tri = its index).
// perf (b35): a triangle that spans several cells is met more than once along the ray; the walk used to skip repeats with a
// per-triangle stamp array (shared: not thread-safe, and a cache miss per triangle on the Vita). Now a small direct-mapped
// cache on the stack skips most repeats; a repeat it misses is tested again and gives the same answer (the nearest hit is
// kept with a strict '<', and the height-span test only gets stricter as the hit gets nearer), so the results are identical
// and raycasts can run on the jobs.h workers. (ray_stamp stays declared for code that frees it.)
#define RAY_SEEN 64
#define RAY_SEEN_INIT int seen_[RAY_SEEN]; memset(seen_, 0xff, sizeof(seen_))
#define RAY_SEEN_SKIP(ti) { int *s_ = &seen_[(ti) & (RAY_SEEN - 1)]; if (*s_ == (ti)) continue; *s_ = (ti); }
static float raycast_mask(V3 o, V3 d, float maxd, int flags, int *tri) {
	if (tri) *tri = -1;
	if (!nctri) return -1;
	RAY_SEEN_INIT; float best = maxd; int hit = -1;
	int use_p = !(flags & TF_BULLET); const int *st = use_p ? pstart : cstart, *li = use_p ? plist : clist;
	float x = (o.x - gx0) / CELL, y = (o.y - gy0) / CELL; int cx = (int)floorf(x), cy = (int)floorf(y);
	int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1;
	float tdx = fabsf(d.x) > 1e-6f ? CELL / fabsf(d.x) : 1e30f, tdy = fabsf(d.y) > 1e-6f ? CELL / fabsf(d.y) : 1e30f;
	float tx = fabsf(d.x) > 1e-6f ? ((d.x > 0 ? (cx + 1 - x) : (x - cx)) * CELL) / fabsf(d.x) : 1e30f;
	float ty = fabsf(d.y) > 1e-6f ? ((d.y > 0 ? (cy + 1 - y) : (y - cy)) * CELL) / fabsf(d.y) : 1e30f;
	float t_cell = 0;
	for (int step = 0; step < 512; step++) {
		if (cx >= 0 && cy >= 0 && cx < gw && cy < gh) {
			int cell = cy * gw + cx; const unsigned short *zr = use_p ? pzr : czr;
			float zb = o.z + d.z * best; unsigned qlo = zq_lo(fminf(o.z, zb)), qhi = zq_hi(fmaxf(o.z, zb));
			for (int k = st[cell]; k < st[cell + 1]; k++) {
				if (ZR_OUT(zr, k, qlo, qhi)) continue;
				int ti = li[k]; RAY_SEEN_SKIP(ti);
				Tri *t = &ctris[ti]; if (!(t->flags & flags)) continue;
				{ float z1 = o.z + d.z * best; if (t->zmax < fminf(o.z, z1) || t->zmin > fmaxf(o.z, z1)) continue; }   // outside the ray's height span
				V3 e1 = vsub(t->b, t->a), e2 = vsub(t->c, t->a), pv = vcross(d, e2); float det = vdot(e1, pv);
				if (fabsf(det) < 1e-6f) continue; float inv = 1 / det; V3 tv = vsub(o, t->a);
				float u = vdot(tv, pv) * inv; if (u < 0 || u > 1) continue;
				V3 qv = vcross(tv, e1); float v = vdot(d, qv) * inv; if (v < 0 || u + v > 1) continue;
				float tt = vdot(e2, qv) * inv;
				if (tt > 0.01f && tt < best) { best = tt; hit = ti; }
			}
		}
		if (hit >= 0 && best <= t_cell) break;
		if (tx < ty) { t_cell = tx; tx += tdx; cx += sx; } else { t_cell = ty; ty += tdy; cy += sy; }
		if (t_cell > best) break;
		if ((cx < 0 && sx < 0) || (cy < 0 && sy < 0) || (cx >= gw && sx > 0) || (cy >= gh && sy > 0)) break;
	}
	if (tri) *tri = hit;
	return hit < 0 ? -1 : best;
}
// ---- hitscan: first bullet-collision triangle along a ray (a real surface wins over the player mesh at the same spot)
// stop (perf, b35): a sight test only needs to know whether something lies at or before `stop`; the first such hit returns
// at once (raycast() itself passes -1: the walk to the nearest hit as before)
static inline float raycast_core(V3 o, V3 d, float maxd, int *mat, V3 *nrm, float stop) {
	if (!nctri) return -1;
	RAY_SEEN_INIT; float best = maxd; int hit = -1; float best_real = maxd; int hit_real = -1;
	float x = (o.x - gx0) / CELL, y = (o.y - gy0) / CELL; int cx = (int)floorf(x), cy = (int)floorf(y);
	int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1;
	float tdx = fabsf(d.x) > 1e-6f ? CELL / fabsf(d.x) : 1e30f, tdy = fabsf(d.y) > 1e-6f ? CELL / fabsf(d.y) : 1e30f;
	float tx = fabsf(d.x) > 1e-6f ? ((d.x > 0 ? (cx + 1 - x) : (x - cx)) * CELL) / fabsf(d.x) : 1e30f;
	float ty = fabsf(d.y) > 1e-6f ? ((d.y > 0 ? (cy + 1 - y) : (y - cy)) * CELL) / fabsf(d.y) : 1e30f;
	float t_cell = 0;
	for (int step = 0; step < 512; step++) {
		if (cx >= 0 && cy >= 0 && cx < gw && cy < gh) {
			int cell = cy * gw + cx;
			float zb = o.z + d.z * best_real; unsigned qlo = zq_lo(fminf(o.z, zb)), qhi = zq_hi(fmaxf(o.z, zb));
			for (int k = cstart[cell]; k < cstart[cell + 1]; k++) {
				if (ZR_OUT(czr, k, qlo, qhi)) continue;
				int ti = clist[k]; RAY_SEEN_SKIP(ti);
				Tri *t = &ctris[ti];
				{ float z1 = o.z + d.z * best_real; if (t->zmax < fminf(o.z, z1) || t->zmin > fmaxf(o.z, z1)) continue; }   // outside the ray's height span
				V3 e1 = vsub(t->b, t->a), e2 = vsub(t->c, t->a), pv = vcross(d, e2); float det = vdot(e1, pv);
				if (fabsf(det) < 1e-6f) continue; float inv = 1 / det; V3 tv = vsub(o, t->a);
				float u = vdot(tv, pv) * inv; if (u < 0 || u > 1) continue;
				V3 qv = vcross(tv, e1); float v = vdot(d, qv) * inv; if (v < 0 || u + v > 1) continue;
				float tt = vdot(e2, qv) * inv;
				if (tt > 0.5f && tt < best) { best = tt; hit = ti; if (tt <= stop) return tt; }
				if (tt > 0.5f && t->mat && tt < best_real) { best_real = tt; hit_real = ti; }
			}
		}
		if (hit >= 0 && best + 4 <= t_cell) break;  // nearest hit lies within cells already walked
		if (tx < ty) { t_cell = tx; tx += tdx; cx += sx; } else { t_cell = ty; ty += tdy; cy += sy; }
		if (t_cell > best) break;
		if ((cx < 0 && sx < 0) || (cy < 0 && sy < 0) || (cx >= gw && sx > 0) || (cy >= gh && sy > 0)) break;
	}
	if (hit < 0) return -1;
	if (hit_real >= 0 && best_real - best < 4) { hit = hit_real; best = best_real; }
	if (mat) *mat = ctris[hit].mat; if (nrm) *nrm = ctris[hit].n;
	return best;
}
static float raycast(V3 o, V3 d, float maxd, int *mat, V3 *nrm) { return raycast_core(o, d, maxd, mat, nrm, -1); }
// clear line of sight from o along the unit d for l: raycast()'s answer "no hit, or the hit lies beyond l - margin" (exact: a hit at
// t <= l - margin - 4 settles it, since raycast() returns the nearest hit or a real-material one at most 4 further)
static int ray_clear(V3 o, V3 d, float l, float margin) { float h = raycast_core(o, d, l, NULL, NULL, l - margin - 4); return h < 0 || h > l - margin; }
// anything within maxd along the unit d (raycast() > 0): the first hit found answers it
static int ray_any(V3 o, V3 d, float maxd) { return raycast_core(o, d, maxd, NULL, NULL, maxd) > 0; }
// material id -> sound name part (0 player mesh, 1 wood, 2 metal, 3 sand, 4 glass, 5 car, 6 paperbox, 7 soft, 8 concrete)
static const char *mat_hit[9] __attribute__((unused)) = { "hit_ar_stone_", "hit_ar_wood_", "hit_ar_metal_", "hit_ar_dirt_", "hit_ar_glass_", "hit_ar_metal_car_", "hit_ar_wood_", "hit_ar_dirt_", "hit_ar_stone_" };
static const char *mat_foot[9] = { "stone", "wood", "metal", "dirt", "glass", "metal", "wood", "dirt", "stone" };
static const char *mat_shell[9] __attribute__((unused)) = { "shelldrop_ar_sr_stone_", "shelldrop_ar_sr_wood_", "shelldrop_common_metal_", "shelldrop_ar_sr_dirt_", "shelldrop_ar_sr_stone_", "shelldrop_common_metal_", "shelldrop_ar_sr_wood_", "shelldrop_ar_sr_dirt_", "shelldrop_ar_sr_stone_" };
