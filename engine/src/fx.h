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
#include "fxtrim.h"   // b81: particle trimming (polygons around the visible part of each atlas frame)
#include "glz.h"      // b95: the effects pack may arrive packed (GLZ1): the Vita's copy was, and every effect failed to load (-2)
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
typedef struct { float p[3], v[3], ax[3], g[3]; float age, life, size, rot, spin, len; short em; unsigned char frame, view; unsigned short seq; } FxP;   // seq: spawn number within its emitter (b36: the thinning rank)
typedef struct { float p[3], t[3], b[3], n[3]; float size, age, life; short cell, frame, grid, pad; } FxDecal;
typedef struct { float p[3], n[3], d[3], t, sc; short em, view; } FxQ;   // (sc: b86 fx_scale at the spawn)

static GLuint fx_tex; static int fx_ok, fx_ncell, fx_nem, fx_neff;
static float (*fx_cell)[4]; static FxEmit *fx_em; static FxEffect *fx_eff; static FxDecalDef fx_ddef[FX_MAXDD]; static int fx_nddef;
static FxP fx_p[FX_MAXP]; static int fx_np;
static void fx_kc_drop(void);   // (perf, b35: fx_alloc)
static FxDecal fx_d[FX_MAXD]; static int fx_nd, fx_dhead;
static FxQ fx_q[FX_MAXQ]; static int fx_nq;
static float fx_scale = 1;   // b86: size, speed and spread of what is spawned (the Think Tank's explosion: x1.7); 1 = as authored
static float fx_cam[3];                       // last camera position (spawn culling)
static float fx_vdown[3] = { 0, -1, 0 };       // world "down" in view space (set by fx_shell_fp)
static unsigned fx_seed = 0x9e3779b9u;
static int fx_stat_quads, fx_stat_flush; static float fx_stat_fill; static int fx_stat_cut;   // b35: screens of world-effect fill this frame, particles left out (total)
// b81 particle trimming (fxtrim.h): per emitter, its frames' polygons in fx_trim[fx_trim_at[em] + frame] (-1: none)
static FxTrim *fx_trim; static int *fx_trim_at, fx_ntrim, fx_trim_on = 1, fx_trim_budget = 1;   // fx_trim_budget: the fill budget counts the polygons' area
static int fx_stat_trim; static float fx_stat_trim_quad, fx_stat_trim_poly;   // this frame: particles drawn trimmed, their screens of quad / of polygon
static double fx_trim_ms;   // building the table at load
static volatile int fx_trim_ready; static int fx_trim_said; static void (*fx_logf)(const char *fmt, ...);   // the table is built on a worker thread (Vita): quads until fx_draw has seen it ready (fx_trim_said)

static float fx_rnd(void) { fx_seed ^= fx_seed << 13; fx_seed ^= fx_seed >> 17; fx_seed ^= fx_seed << 5; return (fx_seed >> 8) * (1.0f / 16777216.0f); }
static float fx_sym(void) { return fx_rnd() - 0.5f; }
static float fx_dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void fx_cross(float *o, const float *a, const float *b) { float r[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; memcpy(o, r, 12); }
static float fx_norm(float *a) { float l = sqrtf(fx_dot(a, a)); if (l > 1e-6f) { a[0] /= l; a[1] /= l; a[2] /= l; } return l; }
static void fx_basis(const float *n, float *t, float *b) {   // any two unit vectors perpendicular to n
	float h[3] = { 0, 0, 1 }; if (fabsf(n[2]) > 0.9f) { h[0] = 1; h[2] = 0; }
	fx_cross(t, h, n); fx_norm(t); fx_cross(b, n, t);
}

static void fx_uv(int cell, int fx_, int fy_, int frame, float *uv);
static int fx_tw = 1024, fx_th = 1024;   // atlas size (texels)
// b81: the trims of every emitter's frames (fxtrim.h), from mip 0 of the atlas; emitters on the same cell / grid / blend share
static void fx_trim_build(const unsigned char *mip0, int W, int H) {
	free(fx_trim); free(fx_trim_at); fx_trim = NULL; fx_trim_at = NULL; fx_ntrim = 0;
	if (!mip0 || W < 4 || H < 4 || (W & 3) || (H & 3)) return;
#ifdef __vita__
	SceUInt64 t0 = sceKernelGetProcessTimeWide();
#endif
	int tot = 0; for (int i = 0; i < fx_nem; i++) tot += fx_em[i].nfr;
	fx_trim = calloc(tot > 0 ? tot : 1, sizeof(FxTrim)); fx_trim_at = malloc(sizeof(int) * (fx_nem > 0 ? fx_nem : 1)); if (!fx_trim || !fx_trim_at) { free(fx_trim); free(fx_trim_at); fx_trim = NULL; fx_trim_at = NULL; return; }
	unsigned char *mk = fxt_mask(mip0, W, H, 0); if (!mk) { free(fx_trim); free(fx_trim_at); fx_trim = NULL; fx_trim_at = NULL; return; }
	for (int i = 0; i < fx_nem; i++) {
		const FxEmit *e = &fx_em[i]; fx_trim_at[i] = -1;
		if ((e->blend != 0 && e->blend != 1) || e->kind != 0) continue;   // (blend 2 = multiply x2: grey is the neutral value, not zero; only camera-facing particles are trimmed)
		int same = -1; for (int j = 0; j < i && same < 0; j++) { const FxEmit *o = &fx_em[j]; if (fx_trim_at[j] >= 0 && o->cell == e->cell && o->fx == e->fx && o->fy == e->fy && o->blend == e->blend && o->nfr >= e->nfr) same = j; }
		if (same >= 0) { fx_trim_at[i] = fx_trim_at[same]; continue; }
		fx_trim_at[i] = fx_ntrim;
		for (int fr = 0; fr < e->nfr; fr++) { float uv[4]; fx_uv(e->cell, e->fx, e->fy, fr, uv);
			fxt_frame_ex(mk, W, H, uv[0] * W, uv[1] * H, uv[2] * W, uv[3] * H, fxt_mtexel, e->blend == 1, &fx_trim[fx_ntrim++]); }
	}
	free(mk);
#ifdef __vita__
	fx_trim_ms = (sceKernelGetProcessTimeWide() - t0) / 1000.0;
#endif
}
// b81: the table on a low-priority worker thread (~1.5 M texels and ~540 polygons: a fraction of a second on the Vita's
// CPU, while the menus run); the particles are quads until fx_trim_ready. Elsewhere (host tests) it is built right away.
static unsigned char *fx_trim_m0; static int fx_trim_m0w, fx_trim_m0h;
#ifdef __vita__
static int fx_trim_thread(SceSize args, void *argp) {
	(void)args; (void)argp; fx_trim_build(fx_trim_m0, fx_trim_m0w, fx_trim_m0h); free(fx_trim_m0); fx_trim_m0 = NULL;
	__sync_synchronize(); fx_trim_ready = 1; sceKernelExitDeleteThread(0); return 0;
}
#endif
static void fx_trim_start(unsigned char *mip0, int W, int H) {
	fx_trim_ready = 0; fx_trim_said = 0;
#ifdef __vita__
	if (mip0) { fx_trim_m0 = mip0; fx_trim_m0w = W; fx_trim_m0h = H;
		static const int mask[3] = { SCE_KERNEL_CPU_MASK_USER_0 << 3, SCE_KERNEL_CPU_MASK_USER_0 << 2, 0 };   // the 4th core (CapUnlocker), else the 3rd, else any
		for (int k = 0; k < 3; k++) { SceUID t = sceKernelCreateThread("gitsfa_fxtrim", fx_trim_thread, 0x10000100 + 20, 0x10000, 0, mask[k], NULL);
			if (t >= 0) { if (sceKernelStartThread(t, 0, NULL) >= 0) return; sceKernelDeleteThread(t); } }
		fx_trim_m0 = NULL; }
#endif
	fx_trim_build(mip0, W, H); free(mip0); fx_trim_ready = 1;
}
static int fx_init(const char *path) {
	FILE *f = glz_fopen(path); if (!f) return -1;   // (b95: packed or plain - the effects pack read straight through)
	char mg[4]; unsigned short wh[2]; unsigned nmip, n;
	if (fread(mg, 4, 1, f) != 1 || memcmp(mg, "FXG1", 4) || fread(wh, 2, 2, f) != 2 || fread(&nmip, 4, 1, f) != 1) { fclose(f); return -2; }
	glGenTextures(1, &fx_tex); glBindTexture(GL_TEXTURE_2D, fx_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nmip > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	void *buf = malloc((size_t)wh[0] * wh[1]), *mip0 = NULL; if (!buf) { fclose(f); return -3; }
	for (unsigned m = 0; m < nmip; m++) { unsigned sz; if (fread(&sz, 4, 1, f) != 1 || sz > (unsigned)wh[0] * wh[1] || fread(buf, sz, 1, f) != 1) { free(buf); free(mip0); fclose(f); return -4; }
		glCompressedTexImage2D(GL_TEXTURE_2D, m, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, wh[0] >> m, wh[1] >> m, 0, sz, buf); sceGxmTransferFinish();
		if (m == 0 && sz == (unsigned)wh[0] * wh[1]) { mip0 = buf; buf = malloc((size_t)wh[0] * wh[1]); if (!buf) { free(mip0); fclose(f); return -3; } } }   // (b81: mip 0 kept for the trims)
	free(buf);
	if (fread(&n, 4, 1, f) != 1 || n > 256) { fclose(f); return -5; } fx_ncell = n; fx_cell = malloc(16 * n); if (fread(fx_cell, 16, n, f) != n) { fclose(f); return -5; }
	if (fread(&n, 4, 1, f) != 1 || n > 256) { fclose(f); return -6; } fx_nem = n; fx_em = malloc(sizeof(FxEmit) * n); if (fread(fx_em, sizeof(FxEmit), n, f) != n) { fclose(f); return -6; }
	if (fread(&n, 4, 1, f) != 1 || n > 64) { fclose(f); return -7; } fx_neff = n; fx_eff = malloc(sizeof(FxEffect) * n); if (fread(fx_eff, sizeof(FxEffect), n, f) != n || fx_neff < FX_NUM) { fclose(f); return -7; }
	if (fread(&n, 4, 1, f) != 1 || n < 10 || n > FX_MAXDD) { fclose(f); return -8; } if (fread(fx_ddef, sizeof(FxDecalDef), n, f) != n) { fclose(f); return -9; }
	fx_nddef = (int)n; fclose(f);
	for (int i = 0; i < fx_nem; i++) { FxEmit *e = &fx_em[i]; if (e->cell < 0 || e->cell >= fx_ncell || e->fx < 1 || e->fy < 1 || e->nfr < 1) { free(mip0); return -10; } }
	for (int i = 0; i < fx_nddef; i++) if (fx_ddef[i].cell < 0 || fx_ddef[i].cell >= fx_ncell || fx_ddef[i].grid < 1) { free(mip0); return -11; }
	fx_tw = wh[0]; fx_th = wh[1]; fx_trim_start(mip0, wh[0], wh[1]);   // (takes mip0)
	fx_np = fx_nd = fx_dhead = fx_nq = 0; fx_ok = 1; fx_kc_drop();
	return fx_neff;
}
static void fx_clear(void) { fx_np = fx_nd = fx_dhead = fx_nq = 0; fx_kc_drop(); }
static __attribute__((unused)) int fx_count(void) { return fx_np; }

// A full pool replaces the particle closest to dying: the smallest life - age below 1e9, the first such index (0 when none).
// perf (b35): that took a scan of all FX_MAXP particles for every new one (~125 k host instructions for an explosion into a
// full pool). Now one scan keeps the FX_KC best (remaining life, index) in order, and each allocation takes the better of
// the next kept one and the particles handed out since the scan (read again: their callers have refilled them). Every
// other particle ranks after the last kept one and nothing else changes them until fx_update / fx_clear, which drop the
// list: the same particle as the full scan, with a scan every FX_KC allocations at most.
#define FX_KC 32
static short fx_kc[FX_KC], fx_given[FX_KC]; static int fx_kc_n = -1, fx_kc_i, fx_ngiven;   // kept (in order), next kept; handed out
static inline int fx_rank_lt(float la, int a, float lb, int b) { return la < lb || (la == lb && a < b); }   // (remaining, index) order
static void fx_kc_drop(void) { fx_kc_n = -1; fx_ngiven = 0; }
static FxP *fx_alloc(void) {
	if (fx_np < FX_MAXP) return &fx_p[fx_np++];
	if (fx_kc_n < 0 || (fx_kc_i >= fx_kc_n && fx_kc_n == FX_KC)) {   // (re)scan: the FX_KC best, by insertion
		float kl[FX_KC]; int n = 0; fx_ngiven = 0; fx_kc_i = 0;
		for (int i = 0; i < FX_MAXP; i++) { float l = fx_p[i].life - fx_p[i].age; if (!(l < 1e9f)) continue;
			if (n == FX_KC && !fx_rank_lt(l, i, kl[n - 1], fx_kc[n - 1])) continue;
			int j = n < FX_KC ? n++ : n - 1; while (j > 0 && fx_rank_lt(l, i, kl[j - 1], fx_kc[j - 1])) { kl[j] = kl[j - 1]; fx_kc[j] = fx_kc[j - 1]; j--; }
			kl[j] = l; fx_kc[j] = (short)i; }
		fx_kc_n = n;
	}
	int best = -1; float bl = 0;
	if (fx_kc_i < fx_kc_n) { best = fx_kc[fx_kc_i]; bl = fx_p[best].life - fx_p[best].age; }
	int from_given = 0;
	for (int g = 0; g < fx_ngiven; g++) { int i = fx_given[g]; float l = fx_p[i].life - fx_p[i].age; if (!(l < 1e9f)) continue;
		if (best < 0 || fx_rank_lt(l, i, bl, best)) { best = i; bl = l; from_given = 1; } }
	if (best < 0) {   // nothing below 1e9: index 0, as the full scan (handed out: read again next time)
		int had = 0; for (int g = 0; g < fx_ngiven; g++) had |= fx_given[g] == 0; if (!had) fx_given[fx_ngiven++] = 0;
		return &fx_p[0]; }
	if (!from_given) { fx_kc_i++; fx_given[fx_ngiven++] = (short)best; }
	return &fx_p[best];
}
// spawn one emitter's particles. n: surface normal (or main direction), d: shot direction (may be NULL)
static void fx_emit(int ei, const float *pos, const float *n, const float *d, int view) {
	const FxEmit *e = &fx_em[ei];
	if (e->delay > 0 && fx_nq < FX_MAXQ) { FxQ *q = &fx_q[fx_nq++]; memcpy(q->p, pos, 12); memcpy(q->n, n, 12); if (d) memcpy(q->d, d, 12); else memcpy(q->d, n, 12);
		q->t = e->delay; q->sc = fx_scale; q->em = (short)ei; q->view = (short)view; return; }
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
		FxP *p = fx_alloc(); memset(p, 0, sizeof(*p)); p->em = (short)ei; p->view = (unsigned char)view; { static unsigned short seq_[256]; p->seq = seq_[ei & 255]++; }
		float v[3];
		if (e->dir == 3) { do { v[0] = fx_sym() * 2; v[1] = fx_sym() * 2; v[2] = fx_sym() * 2; } while (fx_dot(v, v) > 1 || fx_dot(v, v) < 0.01f); fx_norm(v); }
		else if (e->dir == 5) { float el = (fx_rnd() * 2 - 1) * e->spread, ph = fx_rnd() * 6.2831853f, ce = cosf(el), se = sinf(el), c = cosf(ph) * ce, s = sinf(ph) * ce;
			for (int i = 0; i < 3; i++) v[i] = base[i] * se + t1[i] * c + t2[i] * s; }
		else { float ct = 1 - fx_rnd() * (1 - cosf(e->spread)), st = sqrtf(fmaxf(0, 1 - ct * ct)), ph = fx_rnd() * 6.2831853f, c = cosf(ph) * st, s = sinf(ph) * st;
			for (int i = 0; i < 3; i++) v[i] = base[i] * ct + t1[i] * c + t2[i] * s; }
		float sp = (e->speed + e->speed_var * fx_sym()) * fx_scale, rad = (e->jitter < 0 ? -e->jitter : 0) * fx_scale;
		for (int i = 0; i < 3; i++) { p->p[i] = pos[i] + (n[i] * e->offset + (e->jitter > 0 ? fx_sym() * 2 * e->jitter : 0)) * fx_scale + v[i] * rad; p->v[i] = v[i] * sp; p->ax[i] = e->anchor == 2 ? v[i] : base[i]; }
		if (view) for (int i = 0; i < 3; i++) p->g[i] = fx_vdown[i] * e->gravity; else p->g[2] = -e->gravity;
		p->life = fmaxf(0.02f, e->life + e->life_var * fx_sym()); p->size = fmaxf(0.1f, e->size + e->size_var * fx_sym()) * fx_scale;
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
// b87 (user: shot drones left their bullet holes hanging in the air when they went): a hit on something that moves or can be
// destroyed - drones, sentries, barriers, Think Tanks - gets the material's sparks / dust and no hole (a decal is fixed in
// the world)
static void fx_impact_nodecal(int mat, const float *pos, const float *normal, const float *dir) {
	if (mat < 0 || mat > 9) mat = 0;
	fx_spawn(FX_IMPACT_DEFAULT + mat, pos, normal, dir);
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
	fx_kc_drop();   // perf (b35): the particles age (fx_alloc's kept list); again after the loop below
	for (int i = 0; i < fx_nq; ) { FxQ *q = &fx_q[i]; q->t -= dt;
		if (q->t <= 0) { FxQ c = *q; fx_q[i] = fx_q[--fx_nq]; FxEmit *e = &fx_em[c.em]; float dl = e->delay, sc0 = fx_scale; e->delay = 0; fx_scale = c.sc > 0 ? c.sc : 1; fx_emit(c.em, c.p, c.n, c.d, c.view); fx_scale = sc0; e->delay = dl; }
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
	fx_kc_drop();
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
static void fx_room_n(const float *VP, int mode, int n) { if (sp_n + n > SP_MAXQ * 6) { sprite_flush_mode(VP, fx_tex, mode); fx_stat_flush++; } }

// one pass over the particles of a blend mode (0 alpha, 1 additive) in world (view 0) or view space (view 1)
// b35/b36 (render): the fill of the world effects. A grenade 1.5 m away shades ~7 screens of blended fragments at its peak
// (host: tools/fx/host_test/expl.c), 4 m away ~1.7: a GPU spike. The fill is measured (the per-second "perf gpu" line: fx
// fill worst N screens) and capped: when a pass (alpha or additive) would shade more than FX_FILL_BUDGET screens, the
// LARGE particles (each over a quarter of the screen: the puffs of a fireball or smoke cloud right in front of the camera)
// are thinned by a factor f (1..FX_KMAX = 2.5, continuous: big fill / room): a particle is kept when its own fixed rank
// (golden-ratio steps over its emitter's spawn order: evenly spread within each emitter, fixed for its life) is below
// 1 / f, faded across a narrow band around that line (no popping when f changes), and each kept one stands for the f it
// replaces: alpha 1 - (1 - a)^f (f layers of the same puff over each other), additive light x f. Small particles (sparks,
// debris) are never touched, nor the first-person flash. The cloud keeps its density and colour; fewer, more opaque puffs.
// Host tests (tools/fx/host_test expl.c / flicker.c, a grenade at 1.5 m): peak 7.3 -> 4.4 screens of fragments, 347 -> 185
// screens over the 1.3 s explosion; mean picture difference ~10 / 255 at the peak (the puff layout), frame-to-frame change
// unchanged (3.7 / 255: no flicker); at 4 m nothing changes. Original / f <= 2 / f <= 2.5: rnd/sheets/b36/expl_thin.jpg
#ifndef FX_FILL_BUDGET
#define FX_FILL_BUDGET 3.0f
#endif
#define FX_BIG 0.25f
#ifndef FX_KMAX
#define FX_KMAX 2.5f
#endif
static float fx_cov[FX_MAXP], fx_kf[FX_MAXP]; static unsigned char fx_cut[FX_MAXP];
// b81 particle trimming (fxtrim.h): only camera-facing particles (kind 0: their quad is parallel to the screen, so the GPU's
// texels-per-pixel is the same all over it), only while the frame is magnified 2x or more (FXT_GATE texels per pixel: the
// GPU then samples mip 0 alone, the level the trims were made from) and only big ones (FXT_MIN screens: small particles
// keep the cheaper 4-corner quad). Fill only: the picture is the same (tools/fx/host_test/trim.c).
#ifndef FXT_GATE
#define FXT_GATE 0.5f
#endif
#ifndef FXT_MIN
#define FXT_MIN 0.004f
#endif
static int fx_frame_of(const FxP *p, const FxEmit *e, float t) {
	int fr = p->frame;
	if (e->anim == 0) fr = (int)(t * e->nfr); else if (e->anim == 2) fr = (int)(p->age * e->fps); else if (e->anim == 3) fr = (int)(p->age * e->fps) % e->nfr;
	if (fr >= e->nfr) fr = e->nfr - 1; if (fr < 0) fr = 0;
	return fr;
}
// the trim of a camera-facing particle (half size s, its clip w, pxu = pixels per world unit at w = 1), or NULL: the quad;
// *cov = its quad's screens
static const FxTrim *fx_trim_for(const FxP *p, const FxEmit *e, int fr, float s, float w, float pxu, float *cov) {
	float k = w > 1 ? pxu / w : 0, asp = e->aspect > 0 ? e->aspect : 1;   // pixels per world unit at the particle
	*cov = 4 * s * s * asp * k * k * (1.0f / (960.0f * 544.0f));
	if (!fx_trim_on || !fx_trim_said || !fx_trim || e->kind != 0 || w <= 1 || *cov < FXT_MIN) return NULL;   // (fx_trim_said: this thread has seen the table, after a barrier)
	int at = fx_trim_at[p->em]; if (at < 0) return NULL;
	const FxTrim *T = &fx_trim[at + fr]; if (!T->n) return NULL;
	const float *r = fx_cell[e->cell]; float tw = (r[2] - r[0]) / e->fx * fx_tw, th = (r[3] - r[1]) / e->fy * fx_th;   // the frame in texels
	if (tw / (2 * s * k) > FXT_GATE || th / (2 * s * asp * k) > FXT_GATE) return NULL;   // not magnified enough: smaller mips may show
	return T;
}
static float fx_pxu(const float *VP) {   // pixels per world unit at clip w = 1 (the smaller of the two screen axes)
	float fx = sqrtf(VP[0] * VP[0] + VP[4] * VP[4] + VP[8] * VP[8]) * 480, fy = sqrtf(VP[1] * VP[1] + VP[5] * VP[5] + VP[9] * VP[9]) * 272;
	return fx < fy ? fx : fy;
}
static void fx_budget(const float *VP, int mode, int view) {
#ifdef FT   // b38 safety net (frametime.h): while the GPU is behind, the budget shrinks (down to half), then grows back
	const float budget = FX_FILL_BUDGET * ft_fx_scale;
#else
	const float budget = FX_FILL_BUDGET;
#endif
	float fx = sqrtf(VP[0] * VP[0] + VP[4] * VP[4] + VP[8] * VP[8]), fy = sqrtf(VP[1] * VP[1] + VP[5] * VP[5] + VP[9] * VP[9]), tot = 0, big = 0, pxu = fx_pxu(VP);
	for (int i = 0; i < fx_np; i++) {
		const FxP *p = &fx_p[i]; fx_cut[i] = 0; fx_kf[i] = 1; fx_cov[i] = 0; if (p->view != view) continue;
		const FxEmit *e = &fx_em[p->em]; if (e->blend != mode) continue;
		float t = p->age / p->life, sz = p->size * (1 + (e->size_end - 1) * t); if (e->grow > 0 && p->age < e->grow) sz *= p->age / e->grow;
		float a2 = sz * sz * (e->aspect > 0 ? e->aspect : 1), w = VP[3] * p->p[0] + VP[7] * p->p[1] + VP[11] * p->p[2] + VP[15];   // the quad: 2s x 2s*aspect
		float c = w > 1 ? a2 * fx * fy / (w * w) : (w > -sz ? 1.f : 0.f); if (c > 1) c = 1;
		if (fx_trim_budget && e->kind == 0 && w > 1) { float qc; const FxTrim *T = fx_trim_for(p, e, fx_frame_of(p, e, t), sz, w, pxu, &qc); if (T) c *= T->n == FXT_EMPTY ? 0 : T->area; }   // b81: what it really shades
		fx_cov[i] = c; tot += c; if (c >= FX_BIG) big += c;
	}
	if (view == 0) fx_stat_fill += tot;
	if (view != 0 || tot <= budget || big <= 0) return;   // (the first-person flash is never thinned)
	float room = budget - (tot - big); if (room < budget * 0.25f) room = budget * 0.25f;
	float f = big / room; if (f > FX_KMAX) f = FX_KMAX;
	if (f <= 1.02f) return;
	const float keep = 1 / f, band = 0.08f;
	for (int i = 0; i < fx_np; i++) { if (fx_cov[i] < FX_BIG) continue;
		float r = fx_p[i].seq * 0.6180340f; r -= floorf(r);   // rank in [0, 1): golden-ratio steps over the emitter's spawn order (evenly spread, fixed for life)
		float w = (keep - r) / band + 0.5f;   // kept below 1 / f, faded across the band
		if (w <= 0) { fx_cut[i] = 1; fx_stat_cut++; continue; }
		if (w > 1) w = 1;
		fx_kf[i] = f * w;   // its weight: f when fully in, fading to 0 at the edge of the band
	}
}
static void fx_draw_pass(const float *VP, const float *cam, const float *R, const float *U, int mode, int view) {
	fx_budget(VP, mode, view); const float pxu = fx_pxu(VP);
	for (int i = 0; i < fx_np; i++) {
		const FxP *p = &fx_p[i]; if (p->view != view || fx_cut[i]) continue;
		const FxEmit *e = &fx_em[p->em]; if (e->blend != mode) continue;
		float t = p->age / p->life, s = p->size * (1 + (e->size_end - 1) * t);
		if (e->grow > 0 && p->age < e->grow) s *= p->age / e->grow;
		if (s <= 0.01f) continue;
		int fr = fx_frame_of(p, e, t);
		float uv[4], col[4]; fx_uv(e->cell, e->fx, e->fy, fr, uv); fx_color(e, t, col); if (col[3] <= 0.004f) continue;
		if (fx_kf[i] != 1) {   // stands for f thinned-out puffs (fx_budget)
			float k = fx_kf[i];
			if (mode == 0) col[3] = 1 - powf(1 - (col[3] < 1 ? col[3] : 1), k);
			else { float a = col[3] * k; if (a > 1) { col[0] *= a; col[1] *= a; col[2] *= a; a = 1; } col[3] = a; }
		}
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
		if (e->kind == 0) {   // b81: the trimmed polygon instead of the quad (same pixels, fewer fragments)
			float wc = VP[3] * p->p[0] + VP[7] * p->p[1] + VP[11] * p->p[2] + VP[15], qc;
			const FxTrim *T = fx_trim_for(p, e, fr, s, wc, pxu, &qc);
			if (T) {
				fx_stat_trim++; fx_stat_trim_quad += qc < 1 ? qc : 1;   // (stats: each particle at most one screen, as the budget counts)
				if (T->n == FXT_EMPTY) continue;
				fx_stat_trim_poly += qc * T->area < 1 ? qc * T->area : 1;
				float P_[FXT_K][3], UV_[FXT_K][2];
				for (int k = 0; k < T->n; k++) { float a = T->p[k][0], b = T->p[k][1];
					for (int j = 0; j < 3; j++) P_[k][j] = q0[j] + (q1[j] - q0[j]) * a + (q3[j] - q0[j]) * b;
					UV_[k][0] = uv[0] + (uv[2] - uv[0]) * a; UV_[k][1] = uv[1] + (uv[3] - uv[1]) * b; }
				fx_room_n(VP, mode, 3 * (T->n - 2)); sprite_poly(T->n, P_, UV_, col); fx_stat_quads++;
				continue;
			}
		}
		fx_room(VP, mode); sprite_quad(q0, q1, q2, q3, uv[0], uv[1], uv[2], uv[3], col); fx_stat_quads++;
	}
	sprite_flush_mode(VP, fx_tex, mode); fx_stat_flush++;
}
static void fx_trim_report(char *o, int n);
// world effects + decals: VP = world view-projection, cam = eye position, R / U = camera right / up (unit)
static void fx_draw(const float *VP, const float *cam, const float *R, const float *U) {
	if (!fx_ok) return;
	memcpy(fx_cam, cam, 12); fx_stat_quads = fx_stat_flush = 0; fx_stat_fill = 0; fx_stat_trim = 0; fx_stat_trim_quad = fx_stat_trim_poly = 0;
	if (fx_trim_ready && !fx_trim_said) { __sync_synchronize(); fx_trim_said = 1; if (fx_logf) { char tb[160]; fx_trim_report(tb, sizeof(tb)); fx_logf("%s\n", tb); } }   // (the barrier: the worker's table writes are seen before it is used)
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
#ifdef FT   // frametime.h's per-second timing line
	if (fx_stat_fill > ft_fx_fill_max) ft_fx_fill_max = fx_stat_fill;
	ft_fx_cut = fx_stat_cut;
	ft_fx_trim_n += fx_stat_trim; ft_fx_trim_q += fx_stat_trim_quad; ft_fx_trim_p += fx_stat_trim_poly; if (fx_stat_trim_quad - fx_stat_trim_poly > ft_fx_trim_worst) ft_fx_trim_worst = fx_stat_trim_quad - fx_stat_trim_poly;
#endif
}
// first-person (view-space) effects: call right after drawing the weapon, with the weapon's matrix
static void fx_draw_fp(const float *VMVP) {
	if (!fx_ok) return;
	static const float o[3] = { 0, 0, 0 }, R[3] = { 1, 0, 0 }, U[3] = { 0, 1, 0 };
	int any = 0; for (int i = 0; i < fx_np; i++) if (fx_p[i].view) { any = 1; break; }
	if (!any) return;
	int n0 = fx_stat_trim; float q0 = fx_stat_trim_quad, p0 = fx_stat_trim_poly;
	fx_draw_pass(VMVP, o, R, U, 0, 1);
	fx_draw_pass(VMVP, o, R, U, 1, 1);
#ifdef FT
	ft_fx_trim_n += fx_stat_trim - n0; ft_fx_trim_q += fx_stat_trim_quad - q0; ft_fx_trim_p += fx_stat_trim_poly - p0;
#else
	(void)n0; (void)q0; (void)p0;
#endif
}
// b81: one line for the boot log about the trims
static void fx_trim_report(char *o, int n) {
	if (!fx_trim_ready) { snprintf(o, n, "particle trims (b81): being built on a worker thread"); return; }
	int t = 0, e = 0; double a = 0; for (int i = 0; i < fx_ntrim; i++) { if (fx_trim[i].n == FXT_EMPTY) e++; else if (fx_trim[i].n) { t++; a += fx_trim[i].area; } }
	snprintf(o, n, "particle trims (b81): %d of %d frames trimmed (mean %.0f%% of the quad), %d empty, built in %.1f ms%s", t, fx_ntrim, t ? 100 * a / t : 0, e, fx_trim_ms, fx_trim_on ? "" : " - OFF");
}
// b81: a screen rectangle (clip space x0..x1, y0..y1; uv u0..u1, v0..v1 with v0 at y1) drawn through its shape (fxtrim.h): the
// polygon around what can show (t), or the ring around a black middle (h), or the whole quad (both NULL / empty).
// Returns the vertices it queued (0: nothing to draw). Room: up to 3 * FXT_RING_T vertices.
static int fx_rect_shaped(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const float *col, const FxTrim *t, const FxHole *h) {
#define FXR_V(a_, b_) do { float p_[3] = { x0 + (x1 - x0) * (a_), y1 + (y0 - y1) * (b_), 0 }; sp_vert(p_, u0 + (u1 - u0) * (a_), v0 + (v1 - v0) * (b_), col); } while (0)
	int n0 = sp_n;
	if (t && t->n == FXT_EMPTY) return 0;
	if (t && t->n >= 3) { if (sp_n + 3 * (t->n - 2) > SP_MAXQ * 6) return 0; for (int k = 1; k + 1 < t->n; k++) { FXR_V(t->p[0][0], t->p[0][1]); FXR_V(t->p[k][0], t->p[k][1]); FXR_V(t->p[k + 1][0], t->p[k + 1][1]); } }
	else if (h && h->n >= 3 && h->nt > 0) { if (sp_n + 3 * h->nt > SP_MAXQ * 6) return 0; for (int k = 0; k < h->nt; k++) for (int j = 0; j < 3; j++) FXR_V(h->t[k][j][0], h->t[k][j][1]); }
	else { float a[3] = { x0, y1, 0 }, b[3] = { x1, y1, 0 }, c[3] = { x1, y0, 0 }, d[3] = { x0, y0, 0 }; sprite_quad(a, b, c, d, u0, v0, u1, v1, col); }
#undef FXR_V
	return sp_n - n0;
}
