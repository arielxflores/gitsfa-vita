// Game effects built from the original particle NIFs (effect.dat): muzzle flashes (1st and 3rd person),
// tracers, impact effects per surface material, blood / cyborg hits, shell ejection, bullet-hole decals and the
// grenade explosion (explosion_grenade.nif: fx_explosion, optional blast mark fx_scorch).
// Data: fx.gfx (tools: /root/fx/build_fx.py) = one 1024x1024 DXT5 atlas + emitter / effect / decal tables.
// Cheap by design: one pool of camera-facing / stretched / axis-aligned quads (FX_MAXP), a ring of decals
// (FX_MAXD), drawn through sprite.h in three batches (multiply-x2 decals, alpha, additive).
// World space: z up, centimetres. View space (first-person muzzle flash, shells): the viewmodel camera space
// (x right, y up, -z forward), drawn with the viewmodel's matrix after the weapon so the gun hides the flash.
#pragma once
#include "sprite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

enum { FX_IMPACT_DEFAULT, FX_IMPACT_WOOD, FX_IMPACT_METAL, FX_IMPACT_SAND, FX_IMPACT_GLASS, FX_IMPACT_CAR, FX_IMPACT_BOX,
	FX_IMPACT_SOFT, FX_IMPACT_CONCRETE, FX_HIT_BLOOD, FX_HIT_CYBORG, FX_MUZZLE_3P, FX_MUZZLE_FP, FX_SHELL_FP, FX_SHELL_3P,
	FX_TRACER, FX_EXPLOSION, FX_EXPL_DEBRIS, FX_NUM };   // FX_EXPLOSION chains to FX_EXPL_DEBRIS (spawn FX_EXPLOSION only)
#define FX_DECAL_SCORCH 10   // decal table entry of the explosion blast mark (fx_scorch); 0..9 are the materials of fx_impact
#define FX_MAXP 400
#define FX_MAXD 64
#define FX_MAXQ 48
#define FX_MAXDD 16          // decal definitions accepted from the file (10 materials + extras)

typedef struct {   // 176 bytes, same layout as the file
	int cell, kind, blend, fx, fy, nfr, anim, nmin, nmax, dir, nck, anchor;   // kind: 0 billboard, 1 stretched along velocity, 2 axis-aligned
	// dir: 0 normal, 1 shot, 2 reflected, 3 sphere, 4 up, 5 ring (around the normal, elevation within +-spread of its plane)
	// anchor (kind 2): 0 centred, 1 base at the particle, 2 base at the particle and axis = the particle's own direction
	// jitter > 0: random box offset +-jitter; jitter < 0: start |jitter| out along the particle's own direction (radial)
	float fps, delay, life, life_var, size, size_var, aspect, grow, size_end, speed, speed_var, spread, offset, jitter,
		gravity, drag, rot0, rot1, spin, stretch, bright, cull, r0, r1;
	float ck_t[4]; unsigned char ck_c[4][4];
} FxEmit;
typedef struct { char name[16]; int n, pick, em[6]; } FxEffect;
typedef struct { int cell, grid; float size, life; } FxDecalDef;
typedef struct { float p[3], v[3], ax[3], g[3]; float age, life, size, rot, spin, len; short em; unsigned char frame, view; } FxP;
typedef struct { float p[3], t[3], b[3], n[3]; float size, age, life; short cell, frame, grid, pad; } FxDecal;
typedef struct { float p[3], n[3], d[3], t; short em, view; } FxQ;

static GLuint fx_tex; static int fx_ok, fx_ncell, fx_nem, fx_neff;
static float (*fx_cell)[4]; static FxEmit *fx_em; static FxEffect *fx_eff; static FxDecalDef fx_ddef[FX_MAXDD]; static int fx_nddef;
static FxP fx_p[FX_MAXP]; static int fx_np;
static FxDecal fx_d[FX_MAXD]; static int fx_nd, fx_dhead;
static FxQ fx_q[FX_MAXQ]; static int fx_nq;
static float fx_cam[3];                       // last camera position (spawn culling)
static float fx_vdown[3] = { 0, -1, 0 };       // world "down" in view space (set by fx_shell_fp)
static unsigned fx_seed = 0x9e3779b9u;
static int fx_stat_quads, fx_stat_flush;

static float fx_rnd(void) { fx_seed ^= fx_seed << 13; fx_seed ^= fx_seed >> 17; fx_seed ^= fx_seed << 5; return (fx_seed >> 8) * (1.0f / 16777216.0f); }
static float fx_sym(void) { return fx_rnd() - 0.5f; }
static float fx_dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void fx_cross(float *o, const float *a, const float *b) { float r[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; memcpy(o, r, 12); }
static float fx_norm(float *a) { float l = sqrtf(fx_dot(a, a)); if (l > 1e-6f) { a[0] /= l; a[1] /= l; a[2] /= l; } return l; }
static void fx_basis(const float *n, float *t, float *b) {   // any two unit vectors perpendicular to n
	float h[3] = { 0, 0, 1 }; if (fabsf(n[2]) > 0.9f) { h[0] = 1; h[2] = 0; }
	fx_cross(t, h, n); fx_norm(t); fx_cross(b, n, t);
}

static int fx_init(const char *path) {
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	char mg[4]; unsigned short wh[2]; unsigned nmip, n;
	if (fread(mg, 4, 1, f) != 1 || memcmp(mg, "FXG1", 4) || fread(wh, 2, 2, f) != 2 || fread(&nmip, 4, 1, f) != 1) { fclose(f); return -2; }
	glGenTextures(1, &fx_tex); glBindTexture(GL_TEXTURE_2D, fx_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nmip > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	void *buf = malloc((size_t)wh[0] * wh[1]); if (!buf) { fclose(f); return -3; }
	for (unsigned m = 0; m < nmip; m++) { unsigned sz; if (fread(&sz, 4, 1, f) != 1 || sz > (unsigned)wh[0] * wh[1] || fread(buf, sz, 1, f) != 1) { free(buf); fclose(f); return -4; }
		glCompressedTexImage2D(GL_TEXTURE_2D, m, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, wh[0] >> m, wh[1] >> m, 0, sz, buf); sceGxmTransferFinish(); }
	free(buf);
	if (fread(&n, 4, 1, f) != 1 || n > 256) { fclose(f); return -5; } fx_ncell = n; fx_cell = malloc(16 * n); if (fread(fx_cell, 16, n, f) != n) { fclose(f); return -5; }
	if (fread(&n, 4, 1, f) != 1 || n > 256) { fclose(f); return -6; } fx_nem = n; fx_em = malloc(sizeof(FxEmit) * n); if (fread(fx_em, sizeof(FxEmit), n, f) != n) { fclose(f); return -6; }
	if (fread(&n, 4, 1, f) != 1 || n > 64) { fclose(f); return -7; } fx_neff = n; fx_eff = malloc(sizeof(FxEffect) * n); if (fread(fx_eff, sizeof(FxEffect), n, f) != n || fx_neff < FX_NUM) { fclose(f); return -7; }
	if (fread(&n, 4, 1, f) != 1 || n < 10 || n > FX_MAXDD) { fclose(f); return -8; } if (fread(fx_ddef, sizeof(FxDecalDef), n, f) != n) { fclose(f); return -9; }
	fx_nddef = (int)n; fclose(f);
	for (int i = 0; i < fx_nem; i++) { FxEmit *e = &fx_em[i]; if (e->cell < 0 || e->cell >= fx_ncell || e->fx < 1 || e->fy < 1 || e->nfr < 1) return -10; }
	for (int i = 0; i < fx_nddef; i++) if (fx_ddef[i].cell < 0 || fx_ddef[i].cell >= fx_ncell || fx_ddef[i].grid < 1) return -11;
	fx_np = fx_nd = fx_dhead = fx_nq = 0; fx_ok = 1;
	return fx_neff;
}
static void fx_clear(void) { fx_np = fx_nd = fx_dhead = fx_nq = 0; }
static int fx_count(void) { return fx_np; }

static FxP *fx_alloc(void) {
	if (fx_np < FX_MAXP) return &fx_p[fx_np++];
	int best = 0; float bl = 1e9f;   // full: replace the particle closest to dying
	for (int i = 0; i < FX_MAXP; i++) { float l = fx_p[i].life - fx_p[i].age; if (l < bl) { bl = l; best = i; } }
	return &fx_p[best];
}
// spawn one emitter's particles. n: surface normal (or main direction), d: shot direction (may be NULL)
static void fx_emit(int ei, const float *pos, const float *n, const float *d, int view) {
	const FxEmit *e = &fx_em[ei];
	if (e->delay > 0 && fx_nq < FX_MAXQ) { FxQ *q = &fx_q[fx_nq++]; memcpy(q->p, pos, 12); memcpy(q->n, n, 12); if (d) memcpy(q->d, d, 12); else memcpy(q->d, n, 12);
		q->t = e->delay; q->em = (short)ei; q->view = (short)view; return; }
	if (!view && e->cull > 0) { float dx = pos[0] - fx_cam[0], dy = pos[1] - fx_cam[1], dz = pos[2] - fx_cam[2]; if (dx * dx + dy * dy + dz * dz > e->cull * e->cull) return; }
	float base[3], dd[3];
	if (d) memcpy(dd, d, 12); else { dd[0] = -n[0]; dd[1] = -n[1]; dd[2] = -n[2]; }
	switch (e->dir) {
	case 1: memcpy(base, dd, 12); break;
	case 2: { float k = 2 * fx_dot(dd, n); base[0] = dd[0] - k * n[0]; base[1] = dd[1] - k * n[1]; base[2] = dd[2] - k * n[2]; fx_norm(base); } break;
	case 4: base[0] = base[1] = 0; base[2] = 1; break;
	default: memcpy(base, n, 12);
	}
	float t1[3], t2[3]; fx_basis(base, t1, t2);
	int cnt = e->nmin + (e->nmax > e->nmin ? (int)(fx_rnd() * (e->nmax - e->nmin + 1)) : 0);
	for (int k = 0; k < cnt; k++) {
		FxP *p = fx_alloc(); memset(p, 0, sizeof(*p)); p->em = (short)ei; p->view = (unsigned char)view;
		float v[3];
		if (e->dir == 3) { do { v[0] = fx_sym() * 2; v[1] = fx_sym() * 2; v[2] = fx_sym() * 2; } while (fx_dot(v, v) > 1 || fx_dot(v, v) < 0.01f); fx_norm(v); }
		else if (e->dir == 5) { float el = (fx_rnd() * 2 - 1) * e->spread, ph = fx_rnd() * 6.2831853f, ce = cosf(el), se = sinf(el), c = cosf(ph) * ce, s = sinf(ph) * ce;
			for (int i = 0; i < 3; i++) v[i] = base[i] * se + t1[i] * c + t2[i] * s; }
		else { float ct = 1 - fx_rnd() * (1 - cosf(e->spread)), st = sqrtf(fmaxf(0, 1 - ct * ct)), ph = fx_rnd() * 6.2831853f, c = cosf(ph) * st, s = sinf(ph) * st;
			for (int i = 0; i < 3; i++) v[i] = base[i] * ct + t1[i] * c + t2[i] * s; }
		float sp = e->speed + e->speed_var * fx_sym(), rad = e->jitter < 0 ? -e->jitter : 0;
		for (int i = 0; i < 3; i++) { p->p[i] = pos[i] + n[i] * e->offset + v[i] * rad + (e->jitter > 0 ? fx_sym() * 2 * e->jitter : 0); p->v[i] = v[i] * sp; p->ax[i] = e->anchor == 2 ? v[i] : base[i]; }
		if (view) for (int i = 0; i < 3; i++) p->g[i] = fx_vdown[i] * e->gravity; else p->g[2] = -e->gravity;
		p->life = fmaxf(0.02f, e->life + e->life_var * fx_sym()); p->size = fmaxf(0.1f, e->size + e->size_var * fx_sym());
		p->rot = e->rot0 + (e->rot1 - e->rot0) * fx_rnd(); p->spin = e->spin * (fx_rnd() * 2 - 1);
		p->frame = (unsigned char)(e->anim == 1 ? (int)(fx_rnd() * e->nfr) % e->nfr : 0); p->len = 0;
	}
}
static void fx_spawn_ex(int id, const float *pos, const float *n, const float *d, int view) {
	for (int depth = 0; depth < 4 && fx_ok && id >= 0 && id < fx_neff; depth++) {   // pick < 0: chained to effect -pick (> 6 emitters)
		const FxEffect *E = &fx_eff[id]; int first = 0;
		if (E->pick > 0) { first = E->pick; fx_emit(E->em[(int)(fx_rnd() * E->pick) % E->pick], pos, n, d, view); }
		for (int i = first; i < E->n; i++) fx_emit(E->em[i], pos, n, d, view);
		if (E->pick >= 0 || -E->pick == id) break;
		id = -E->pick;
	}
}
// world-space effect at pos; normal = surface normal (or main direction); dir = incoming shot direction (may be NULL)
static void fx_spawn(int id, const float *pos, const float *normal, const float *dir) { fx_spawn_ex(id, pos, normal, dir, 0); }

// a bullet hole (mat 0..8 from mat_hit[]) or a blood splat (mat 9) on the surface at pos
static void fx_decal(int mat, const float *pos, const float *normal) {
	if (!fx_ok || mat < 0 || mat >= fx_nddef) return;
	const FxDecalDef *dd = &fx_ddef[mat];
	FxDecal *D = &fx_d[fx_dhead]; fx_dhead = (fx_dhead + 1) % FX_MAXD; if (fx_nd < FX_MAXD) fx_nd++;
	float n[3] = { normal[0], normal[1], normal[2] }; fx_norm(n); float t[3], b[3]; fx_basis(n, t, b);
	float a = fx_rnd() * 6.2831853f, c = cosf(a), s = sinf(a);
	for (int i = 0; i < 3; i++) { D->t[i] = t[i] * c + b[i] * s; D->b[i] = b[i] * c - t[i] * s; D->n[i] = n[i]; D->p[i] = pos[i] + n[i] * 0.7f; }
	D->size = dd->size * (0.85f + 0.3f * fx_rnd()); D->age = 0; D->life = dd->life; D->cell = (short)dd->cell; D->grid = (short)dd->grid;
	D->frame = (short)((int)(fx_rnd() * dd->grid * dd->grid) % (dd->grid * dd->grid));
}
// impact of a shot: material effect + bullet hole (mat 0..8), or a hit on a character (mat 9: blood, no decal)
static void fx_impact(int mat, const float *pos, const float *normal, const float *dir) {
	if (mat < 0 || mat > 9) mat = 0;
	fx_spawn(FX_IMPACT_DEFAULT + mat, pos, normal, dir);
	if (mat < 9) fx_decal(mat, pos, normal);
}
// grenade / bomb explosion (explosion_grenade.nif) at pos = the ground point or just above it; normal = surface normal or
// main direction ((0,0,1) if NULL). Flash, fireball, smoke ball + ring, smoke streaks, sparks, burning debris, dirt: 47
// particles over ~2 s (<= 36 alive at once, 15 of them large), delayed parts queued (5 FxQ). No ground mark: call
// fx_scorch where the ground really is.
static void fx_explosion(const float *pos, const float *normal) {
	if (!fx_ok || !pos || FX_EXPLOSION >= fx_neff) return;
	float n[3] = { 0, 0, 1 };
	if (normal) { n[0] = normal[0]; n[1] = normal[1]; n[2] = normal[2]; if (fx_norm(n) < 1e-6f) { n[0] = n[1] = 0; n[2] = 1; } }
	fx_spawn_ex(FX_EXPLOSION, pos, n, NULL, 0);
}
// blast mark (B_Down, ~1.4 m, 6 s) on the surface at pos, e.g. under a grenade that went off on the ground
static void fx_scorch(const float *pos, const float *normal) {
	float n[3] = { 0, 0, 1 }; if (normal) { n[0] = normal[0]; n[1] = normal[1]; n[2] = normal[2]; if (fx_norm(n) < 1e-6f) { n[0] = n[1] = 0; n[2] = 1; } }
	if (pos) fx_decal(FX_DECAL_SCORCH, pos, n);
}
// a tracer streak flying from -> to (world)
static void fx_tracer(const float *from, const float *to) {
	if (!fx_ok) return;
	const FxEffect *E = &fx_eff[FX_TRACER]; int ei = E->em[0]; const FxEmit *e = &fx_em[ei];
	float d[3] = { to[0] - from[0], to[1] - from[1], to[2] - from[2] }; float L = fx_norm(d); if (L < 200) return;
	const float speed = 16000;   // cm/s: slow enough to be seen for a few frames
	FxP *p = fx_alloc(); memset(p, 0, sizeof(*p)); p->em = (short)ei;
	for (int i = 0; i < 3; i++) { p->p[i] = from[i] + d[i] * 60; p->v[i] = d[i] * speed; p->ax[i] = from[i]; }
	p->life = (L - 60) / speed; p->size = e->size; p->len = 1;   // len 1: tail clipped at the muzzle (ax)
}
// third-person muzzle flash (bots): pos = muzzle, dir = barrel direction
static void fx_muzzle_3p(const float *pos, const float *dir) { fx_spawn_ex(FX_MUZZLE_3P, pos, dir, dir, 0); }
// first-person muzzle flash, in viewmodel camera space (pos = muzzle point, dir = barrel, about (0,0,-1))
static void fx_muzzle_fp(const float *pos_cam, const float *dir_cam) { fx_spawn_ex(FX_MUZZLE_FP, pos_cam, dir_cam, dir_cam, 1); }
// first-person shell: viewmodel camera space; eject = direction out of the ejection port; pitch = view pitch (gravity)
static void fx_shell_fp(const float *pos_cam, const float *eject_cam, float pitch) {
	fx_vdown[0] = 0; fx_vdown[1] = -cosf(pitch); fx_vdown[2] = sinf(pitch);
	float e[3] = { eject_cam[0], eject_cam[1], eject_cam[2] }; fx_norm(e); fx_spawn_ex(FX_SHELL_FP, pos_cam, e, NULL, 1);
}

static void fx_update(float dt) {
	if (!fx_ok || dt <= 0) return;
	if (dt > 0.1f) dt = 0.1f;
	for (int i = 0; i < fx_nq; ) { FxQ *q = &fx_q[i]; q->t -= dt;
		if (q->t <= 0) { FxQ c = *q; fx_q[i] = fx_q[--fx_nq]; FxEmit *e = &fx_em[c.em]; float dl = e->delay; e->delay = 0; fx_emit(c.em, c.p, c.n, c.d, c.view); e->delay = dl; }
		else i++; }
	for (int i = 0; i < fx_np; ) {
		FxP *p = &fx_p[i]; p->age += dt;
		if (p->age >= p->life) { fx_p[i] = fx_p[--fx_np]; continue; }
		const FxEmit *e = &fx_em[p->em];
		for (int k = 0; k < 3; k++) p->v[k] += p->g[k] * dt;
		if (e->drag > 0) { float f = 1.0f / (1.0f + e->drag * dt); p->v[0] *= f; p->v[1] *= f; p->v[2] *= f; }
		for (int k = 0; k < 3; k++) p->p[k] += p->v[k] * dt;
		p->rot += p->spin * dt; i++;
	}
	for (int i = 0; i < fx_nd; i++) fx_d[i].age += dt;
}

static void fx_color(const FxEmit *e, float t, float *c) {
	int k = 0; while (k < e->nck - 1 && t > e->ck_t[k + 1]) k++;
	const unsigned char *a = e->ck_c[k], *b = e->ck_c[k + 1 < e->nck ? k + 1 : k];
	float span = (k + 1 < e->nck) ? e->ck_t[k + 1] - e->ck_t[k] : 1, f = span > 1e-5f ? (t - e->ck_t[k]) / span : 0; if (f < 0) f = 0; if (f > 1) f = 1;
	for (int i = 0; i < 4; i++) c[i] = (a[i] + (b[i] - a[i]) * f) * (1.0f / 255);
	c[0] *= e->bright; c[1] *= e->bright; c[2] *= e->bright;
}
static void fx_uv(int cell, int fx_, int fy_, int frame, float *uv) {
	const float *r = fx_cell[cell]; float w = (r[2] - r[0]) / fx_, h = (r[3] - r[1]) / fy_; int cx = frame % fx_, cy = (frame / fx_) % fy_;
	uv[0] = r[0] + cx * w; uv[1] = r[1] + cy * h; uv[2] = uv[0] + w; uv[3] = uv[1] + h;
}
static void fx_room(const float *VP, int mode) { if (sp_n + 6 > SP_MAXQ * 6) { sprite_flush_mode(VP, fx_tex, mode); fx_stat_flush++; } }

// one pass over the particles of a blend mode (0 alpha, 1 additive) in world (view 0) or view space (view 1)
static void fx_draw_pass(const float *VP, const float *cam, const float *R, const float *U, int mode, int view) {
	for (int i = 0; i < fx_np; i++) {
		const FxP *p = &fx_p[i]; if (p->view != view) continue;
		const FxEmit *e = &fx_em[p->em]; if (e->blend != mode) continue;
		float t = p->age / p->life, s = p->size * (1 + (e->size_end - 1) * t);
		if (e->grow > 0 && p->age < e->grow) s *= p->age / e->grow;
		if (s <= 0.01f) continue;
		int fr = p->frame;
		if (e->anim == 0) fr = (int)(t * e->nfr); else if (e->anim == 2) fr = (int)(p->age * e->fps); else if (e->anim == 3) fr = (int)(p->age * e->fps) % e->nfr;
		if (fr >= e->nfr) fr = e->nfr - 1;
		float uv[4], col[4]; fx_uv(e->cell, e->fx, e->fy, fr, uv); fx_color(e, t, col); if (col[3] <= 0.004f) continue;
		float q0[3], q1[3], q2[3], q3[3];
		if (e->kind == 0) {   // camera-facing, rolled
			float c = cosf(p->rot), sn = sinf(p->rot), w = s, h = s * e->aspect, r_[3], u_[3];
			for (int k = 0; k < 3; k++) { r_[k] = (R[k] * c + U[k] * sn) * w; u_[k] = (U[k] * c - R[k] * sn) * h; }
			for (int k = 0; k < 3; k++) { q0[k] = p->p[k] - r_[k] + u_[k]; q1[k] = p->p[k] + r_[k] + u_[k]; q2[k] = p->p[k] + r_[k] - u_[k]; q3[k] = p->p[k] - r_[k] - u_[k]; }
		} else {   // 1: streak along the velocity (head at p), 2: plane along ax (base at p if anchor) turned to the camera
			float a[3], len, half;
			if (e->kind == 1) { memcpy(a, p->v, 12); float spd = fx_norm(a); if (spd < 1e-3f) memcpy(a, p->ax, 12); len = s + spd * e->stretch;
				if (p->len > 0) { float dx = p->p[0] - p->ax[0], dy = p->p[1] - p->ax[1], dz = p->p[2] - p->ax[2], dl = sqrtf(dx * dx + dy * dy + dz * dz); if (len > dl) len = dl; }
				half = len * 0.5f; }
			else { memcpy(a, p->ax, 12); half = s * e->aspect; }
			float mid[3], toc[3], sd[3];
			float off = (e->kind == 1) ? -half : (e->anchor ? half : 0);
			for (int k = 0; k < 3; k++) { mid[k] = p->p[k] + a[k] * off; toc[k] = cam[k] - mid[k]; }
			fx_cross(sd, a, toc); if (fx_norm(sd) < 1e-4f) continue;
			for (int k = 0; k < 3; k++) { float ah = a[k] * half, sw = sd[k] * s;
				q0[k] = mid[k] + ah - sw; q1[k] = mid[k] + ah + sw; q2[k] = mid[k] - ah + sw; q3[k] = mid[k] - ah - sw; }
		}
		fx_room(VP, mode); sprite_quad(q0, q1, q2, q3, uv[0], uv[1], uv[2], uv[3], col); fx_stat_quads++;
	}
	sprite_flush_mode(VP, fx_tex, mode); fx_stat_flush++;
}
// world effects + decals: VP = world view-projection, cam = eye position, R / U = camera right / up (unit)
static void fx_draw(const float *VP, const float *cam, const float *R, const float *U) {
	if (!fx_ok) return;
	memcpy(fx_cam, cam, 12); fx_stat_quads = fx_stat_flush = 0;
	if (sp_n) sprite_flush(VP, sp_blob, 0);   // never mix in someone else's queued quads
	// bullet holes (DST_COLOR x SRC_COLOR: grey 128 leaves the surface unchanged); shrink away over their last 0.5 s
	for (int i = 0; i < fx_nd; i++) {
		const FxDecal *D = &fx_d[i]; if (D->age >= D->life) continue;
		float tc[3] = { cam[0] - D->p[0], cam[1] - D->p[1], cam[2] - D->p[2] }, dn = fx_dot(tc, D->n);
		if (dn <= 0 || fx_dot(tc, tc) > 3500.f * 3500.f) continue;   // behind the surface or too far to see
		float s = D->size; if (D->life - D->age < 0.5f) s *= (D->life - D->age) * 2;
		float uv[4], q0[3], q1[3], q2[3], q3[3]; fx_uv(D->cell, D->grid, D->grid, D->frame, uv);
		for (int k = 0; k < 3; k++) { float t = D->t[k] * s, b = D->b[k] * s; q0[k] = D->p[k] - t + b; q1[k] = D->p[k] + t + b; q2[k] = D->p[k] + t - b; q3[k] = D->p[k] - t - b; }
		static const float wc[4] = { 1, 1, 1, 1 };
		fx_room(VP, 2); sprite_quad(q0, q1, q2, q3, uv[0], uv[1], uv[2], uv[3], wc); fx_stat_quads++;
	}
	sprite_flush_mode(VP, fx_tex, 2);
	fx_draw_pass(VP, cam, R, U, 0, 0);
	fx_draw_pass(VP, cam, R, U, 1, 0);
}
// first-person (view-space) effects: call right after drawing the weapon, with the weapon's matrix
static void fx_draw_fp(const float *VMVP) {
	if (!fx_ok) return;
	static const float o[3] = { 0, 0, 0 }, R[3] = { 1, 0, 0 }, U[3] = { 0, 1, 0 };
	int any = 0; for (int i = 0; i < fx_np; i++) if (fx_p[i].view) { any = 1; break; }
	if (!any) return;
	fx_draw_pass(VMVP, o, R, U, 0, 1);
	fx_draw_pass(VMVP, o, R, U, 1, 1);
}
