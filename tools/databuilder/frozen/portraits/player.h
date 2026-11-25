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
static void col_fix_spawns(void);
static void col_free(void) {
	free(ctris); free(spawns); free(cstart); free(clist); free(pstart); free(plist); free(ray_stamp);
	ctris = NULL; spawns = NULL; cstart = clist = pstart = plist = NULL; ray_stamp = NULL; nctri = nspawn = 0;
}
static int load_col(const char *path) {
	col_free();
	FILE *f = fopen(path, "rb"); if (!f) return -1;
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
	col_fix_spawns();
	return 0;
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
#define P_CLIMB_MAX 230.0f       // highest ledge (above the feet) that can be climbed onto
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
	// ---- state, read by the caller
	int crouched;       // 1 while crouched or sliding (collision height lowered)
	float slide_t;      // > 0 while sliding: seconds since the slide started
	float eye;          // camera height above pos.z: eases between stances, dips on steps and landings (0 before the first update)
	float height;       // current collision height
	int events;         // PE_* events of the last update
	float land_vz;      // vertical speed at the last landing (cm/s, negative)
	int mantle_kind;    // while mantle_t > 0: 1 climbing onto a ledge, 2 vaulting over cover
	int sprinting;      // 1 while actually sprinting
	// ---- internal
	int was_crouch_in, keep_run; float eye_off, air_t, slide_cd; V3 slide_dir;
} Player;
static float raycast(V3 o, V3 d, float maxd, int *mat, V3 *nrm);
static float raycast_mask(V3 o, V3 d, float maxd, int flags, int *tri);

// push a sphere out of nearby player-collision triangles; returns 1 if a walkable floor was hit (allow_floor).
// Floor-like contacts always push straight up (never sideways); ceilings push down; everything else pushes horizontally.
static int sphere_resolve(V3 *c, float r, int allow_floor, V3 *floor_n) {
	int floor = 0;
	int cx0 = (int)((c->x - r - gx0) / CELL), cx1 = (int)((c->x + r - gx0) / CELL);
	int cy0 = (int)((c->y - r - gy0) / CELL), cy1 = (int)((c->y + r - gy0) / CELL);
	if (cx0 < 0) cx0 = 0; if (cy0 < 0) cy0 = 0; if (cx1 >= gw) cx1 = gw - 1; if (cy1 >= gh) cy1 = gh - 1;
	for (int y = cy0; y <= cy1; y++) for (int x = cx0; x <= cx1; x++) {
		int cell = y * gw + x;
		for (int k = pstart[cell]; k < pstart[cell + 1]; k++) {
			Tri *t = &ctris[plist[k]];
			if (c->z + r < t->zmin || c->z - r > t->zmax) continue;
			V3 q = closest_pt_tri(*c, t->a, t->b, t->c), d = vsub(*c, q);
			float dd = vdot(d, d); if (dd >= r * r) continue;
			float dist = sqrtf(dd); V3 n = dist > 1e-3f ? vmul(d, 1.0f / dist) : t->n;
			float pen = r - dist;
			if (n.z > P_WALKABLE) { c->z += pen / n.z; if (allow_floor) { floor = 1; if (floor_n) *floor_n = n; } }
			else if (n.z < -0.7f) { *c = vadd(*c, vmul(n, pen)); }
			else { V3 h = v3(n.x, n.y, 0); float hl = sqrtf(vdot(h, h)); if (hl > 0.05f) { float push = pen / hl; if (push > pen * 4) push = pen * 4; *c = vadd(*c, vmul(h, push / hl)); } }
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
	float best = -1e9f, maxraw = -1e9f; int bm = 0, bt = -1; const float r = P_PROBE;
	int cx0 = (int)((x - r - gx0) / CELL), cx1 = (int)((x + r - gx0) / CELL), cy0 = (int)((y - r - gy0) / CELL), cy1 = (int)((y + r - gy0) / CELL);
	if (cx0 < 0) cx0 = 0; if (cy0 < 0) cy0 = 0; if (cx1 >= gw) cx1 = gw - 1; if (cy1 >= gh) cy1 = gh - 1;
	for (int gy = cy0; gy <= cy1; gy++) for (int gx = cx0; gx <= cx1; gx++) {
		int cell = gy * gw + gx;
		for (int k = pstart[cell]; k < pstart[cell + 1]; k++) {
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
	if (grounded) { int m = 0; V3 n; float d = raycast(v3(p->pos.x, p->pos.y, p->pos.z + 30), v3(0, 0, -1), 80, &m, &n); floor_mat = floor_real = d >= 0 ? m : 0; }
}

// climbing: a ledge 40..P_CLIMB_MAX above the feet right in front with room to stand on it, or low cover (a wall,
// a barrier, a window sill) with floor behind it to vault over. Returns 1 climb, 2 vault, 0 nothing.
static int ledge_cover;   // set by player_ledge: the obstacle is marked vaultable cover in the map (coc_jumper)
static int player_ledge(const Player *p, V3 dir, V3 *dest, float *peak) {
	float h = p->height > 0 ? p->height : P_HEIGHT;
	V3 o = v3(p->pos.x, p->pos.y, p->pos.z + 50); int ti = -1;
	float hw = raycast_mask(o, dir, P_RADIUS + 55, TF_PLAYER, &ti);
	if (hw < 0) { o.z = p->pos.z + 110; hw = raycast_mask(o, dir, P_RADIUS + 55, TF_PLAYER, &ti); if (hw < 0) return 0; }
	if (ti >= 0 && fabsf(ctris[ti].n.z) > 0.7f) return 0;     // a slope, not a wall
	ledge_cover = ti >= 0 && (ctris[ti].flags & TF_CLIMB);
	// the top of the obstacle: the lowest height at which the way ahead is clear (10 cm steps)
	float top = -1e9f;
	for (float z = 40; z <= P_CLIMB_MAX + 10; z += 10)
		if (raycast_mask(v3(p->pos.x, p->pos.y, p->pos.z + z), dir, hw + 30, TF_PLAYER, NULL) < 0) { top = p->pos.z + z - 5; break; }
	if (top < -1e8f) return 0;
	// a top surface just behind the face (box, ledge, sill) gives the exact height
	int has_top = 0;
	{ V3 a = v3(p->pos.x + dir.x * (hw + 12), p->pos.y + dir.y * (hw + 12), top + 30);
	  float hd = raycast_mask(a, v3(0, 0, -1), 60, TF_PLAYER, &ti);
	  if (hd >= 0 && ti >= 0 && fabsf(ctris[ti].n.z) >= P_WALKABLE) { top = a.z - hd; has_top = 1; } }
	float rise = top - p->pos.z; if (rise < 40 || rise > P_CLIMB_MAX) return 0;
	// room to rise (the body goes over the edge crouched: feet just above the top, head ~100 cm higher)
	float need = top + 25 + P_HEIGHT_SLIDE - (p->pos.z + h - 20);
	if (need > 0 && raycast_mask(v3(p->pos.x, p->pos.y, p->pos.z + h - 20), v3(0, 0, 1), need, TF_PLAYER, NULL) >= 0) return 0;
	if (raycast_mask(v3(p->pos.x, p->pos.y, top + 35), dir, hw + 40, TF_PLAYER, NULL) >= 0) return 0;   // nothing in the way over the edge
	if (raycast_mask(v3(p->pos.x, p->pos.y, top + 25 + P_HEIGHT_SLIDE - 10), dir, hw + 40, TF_PLAYER, NULL) >= 0) return 0;
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
	if (!has_top) return 0;
	V3 a = v3(p->pos.x + dir.x * (hw + 40), p->pos.y + dir.y * (hw + 40), top + 1);   // climb up and stand on the top
	{ float hd = raycast_mask(v3(a.x, a.y, top + 30), v3(0, 0, -1), 60, TF_PLAYER, &ti); if (hd < 0 || ti < 0 || fabsf(ctris[ti].n.z) < P_WALKABLE) return 0; a.z = top + 30 - hd + 1; }
	if (raycast_mask(v3(a.x, a.y, a.z + 5), v3(0, 0, 1), h - 5, TF_PLAYER, NULL) >= 0) return 0;  // room to stand on top
	if (body_blocked(a, h, 2.0f)) return 0;
	*dest = a; *peak = a.z + 1; return 1;
}

// HUD prompt helper (the original shows "W + SPACE" in front of climbable cover and windows): what a jump would do now,
// facing yaw. 0 nothing / a plain jump, 1 climb onto a ledge, 2 vault over; +4 when the obstacle is marked cover in
// the map (the original's prompt spots). Costs ~25 short raycasts: call it every few frames, not every frame.
static int player_ledge_ahead(const Player *p) {
	if (!p->grounded || p->mantle_t > 0 || p->slide_t > 0) return 0;
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
		p->mantle_t -= dt; float k = 1 - p->mantle_t / p->mantle_len; if (k > 1) k = 1;
		float kx = k < 0.4f ? 0 : (k - 0.4f) / 0.5f, z; if (kx > 1) kx = 1;
		if (k < 0.55f) z = p->m_from.z + (p->m_peak - p->m_from.z) * (k / 0.55f); else z = p->m_peak + (p->m_to.z - p->m_peak) * ((k - 0.55f) / 0.45f);
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
	if (p->grounded && jump && p->slide_t <= 0) { p->vel.z = P_JUMP; p->grounded = 0; p->events |= PE_JUMP; }
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
	if (n) nspawn = n;
}

// ---- grid ray walk over XY: nearest triangle with any of the flags. Returns the distance or -1 (*tri = its index).
static unsigned ray_seq;
static float raycast_mask(V3 o, V3 d, float maxd, int flags, int *tri) {
	if (tri) *tri = -1;
	if (!nctri) return -1;
	if (!ray_stamp) ray_stamp = calloc(nctri, sizeof(unsigned));
	ray_seq++; float best = maxd; int hit = -1;
	int use_p = !(flags & TF_BULLET); const int *st = use_p ? pstart : cstart, *li = use_p ? plist : clist;
	float x = (o.x - gx0) / CELL, y = (o.y - gy0) / CELL; int cx = (int)floorf(x), cy = (int)floorf(y);
	int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1;
	float tdx = fabsf(d.x) > 1e-6f ? CELL / fabsf(d.x) : 1e30f, tdy = fabsf(d.y) > 1e-6f ? CELL / fabsf(d.y) : 1e30f;
	float tx = fabsf(d.x) > 1e-6f ? ((d.x > 0 ? (cx + 1 - x) : (x - cx)) * CELL) / fabsf(d.x) : 1e30f;
	float ty = fabsf(d.y) > 1e-6f ? ((d.y > 0 ? (cy + 1 - y) : (y - cy)) * CELL) / fabsf(d.y) : 1e30f;
	float t_cell = 0;
	for (int step = 0; step < 512; step++) {
		if (cx >= 0 && cy >= 0 && cx < gw && cy < gh) {
			int cell = cy * gw + cx;
			for (int k = st[cell]; k < st[cell + 1]; k++) {
				int ti = li[k]; if (ray_stamp[ti] == ray_seq) continue; ray_stamp[ti] = ray_seq;
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
static float raycast(V3 o, V3 d, float maxd, int *mat, V3 *nrm) {
	if (!nctri) return -1;
	if (!ray_stamp) ray_stamp = calloc(nctri, sizeof(unsigned));
	ray_seq++; float best = maxd; int hit = -1; float best_real = maxd; int hit_real = -1;
	float x = (o.x - gx0) / CELL, y = (o.y - gy0) / CELL; int cx = (int)floorf(x), cy = (int)floorf(y);
	int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1;
	float tdx = fabsf(d.x) > 1e-6f ? CELL / fabsf(d.x) : 1e30f, tdy = fabsf(d.y) > 1e-6f ? CELL / fabsf(d.y) : 1e30f;
	float tx = fabsf(d.x) > 1e-6f ? ((d.x > 0 ? (cx + 1 - x) : (x - cx)) * CELL) / fabsf(d.x) : 1e30f;
	float ty = fabsf(d.y) > 1e-6f ? ((d.y > 0 ? (cy + 1 - y) : (y - cy)) * CELL) / fabsf(d.y) : 1e30f;
	float t_cell = 0;
	for (int step = 0; step < 512; step++) {
		if (cx >= 0 && cy >= 0 && cx < gw && cy < gh) {
			int cell = cy * gw + cx;
			for (int k = cstart[cell]; k < cstart[cell + 1]; k++) {
				int ti = clist[k]; if (ray_stamp[ti] == ray_seq) continue; ray_stamp[ti] = ray_seq;
				Tri *t = &ctris[ti];
				{ float z1 = o.z + d.z * best_real; if (t->zmax < fminf(o.z, z1) || t->zmin > fmaxf(o.z, z1)) continue; }   // outside the ray's height span
				V3 e1 = vsub(t->b, t->a), e2 = vsub(t->c, t->a), pv = vcross(d, e2); float det = vdot(e1, pv);
				if (fabsf(det) < 1e-6f) continue; float inv = 1 / det; V3 tv = vsub(o, t->a);
				float u = vdot(tv, pv) * inv; if (u < 0 || u > 1) continue;
				V3 qv = vcross(tv, e1); float v = vdot(d, qv) * inv; if (v < 0 || u + v > 1) continue;
				float tt = vdot(e2, qv) * inv;
				if (tt > 0.5f && tt < best) { best = tt; hit = ti; }
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
// material id -> sound name part (0 player mesh, 1 wood, 2 metal, 3 sand, 4 glass, 5 car, 6 paperbox, 7 soft, 8 concrete)
static const char *mat_hit[9]  = { "hit_ar_stone_", "hit_ar_wood_", "hit_ar_metal_", "hit_ar_dirt_", "hit_ar_glass_", "hit_ar_metal_car_", "hit_ar_wood_", "hit_ar_dirt_", "hit_ar_stone_" };
static const char *mat_foot[9] = { "stone", "wood", "metal", "dirt", "glass", "metal", "wood", "dirt", "stone" };
static const char *mat_shell[9] = { "shelldrop_ar_sr_stone_", "shelldrop_ar_sr_wood_", "shelldrop_common_metal_", "shelldrop_ar_sr_dirt_", "shelldrop_ar_sr_stone_", "shelldrop_common_metal_", "shelldrop_ar_sr_wood_", "shelldrop_ar_sr_dirt_", "shelldrop_ar_sr_stone_" };
