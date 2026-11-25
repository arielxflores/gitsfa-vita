// b119: the data builder's software renderer for the intro video (tools/intro/render_intro.py drives it over a pipe).
// The PC game plays its intro cut scene live; the Vita plays a video of it (tut_video.h). This draws that video's frames
// on the player's own PC from the player's own files: no GPU, no library, one C file (gcc / mingw, -O2).
// b120: the actors and the room are shaded as the PC shades them. The PC's pixel shaders (the install's own
// shaders\data\generated\WeaponMaterial / WGStandardMaterial / CustomMaterial _ps_3_0_DX9.cache, read with tools/vision/d3dasm.py)
// are followed term by term - mesh kinds 1, 2 and 4 in shade_tri; kind 3 is a mesh without a shader of its own under an
// environment NiTextureEffect (the reflection shell on Motoko's eyes). The numbers come from the mesh (render_intro.py).
//
// Protocol (little endian, stdin -> stdout):
//   'I' i32 width, height, supersample, i32 n, n bytes       once, first; the n bytes name a file for the answers (n 0: stdout)
//   'T' i32 id, w, h, then w*h*4 bytes RGBA                 a texture (mip chain made here; wraps)
//   'C' i32 id, n, then 6 faces x n*n*3 f32 RGB             a cube map (D3D order +X -X +Y -Y +Z -Z; mip chain made here)
//   'M' i32 id, nv, ni, flags, tex, lightmap, glow, spec, dark (multiplied; its own uv transform), f32 color[4], f32 glow_gain, spec_gain,
//       i32 kind (0 plain, 1 the PC's WeaponMaterial, 2 its WGStandardMaterial, 3 base x own light + cube, 4 its CustomMaterial on a lightmap), normal map, shader map, cube, f32 P[NP] (the material's numbers, below),
//       f32 uv0[nv*2], (flags&MF_UV1) f32 uv1[nv*2], (flags&MF_VCOL) u8 vcol[nv*4], u32 index[ni],
//       (flags&MF_STATIC) f32 pos[nv*3], f32 nrm[nv*3], (kind) f32 tan[nv*3], bin[nv*3]       a mesh
//   'F' f32 eye[3], dir[3], up[3], tanx, tany, znear, zfar, f32 ambient[3] (from below), f32 sky[3] (from above, world +z), i32 lights, lights x (f32 dir[3] TO the light,
//       f32 colour[3]), f32 rim[4] (colour, power), f32 actor light: direction it travels [3], diffuse[3], specular[3], ambient[3] (kinds 1, 2),
//       i32 items, items x (i32 mesh, f32 colour[4], f32 uva[6], f32 uvb[6]
//       (u' = a u + b v + c, v' = d u + e v + f: the base's and the dark map's), f32 uvc[6] (the glow map's, of the plain uv), i32 base texture this frame (-2: the mesh's),
//       f32 sh[27] (the light probe at the item: 9 RGB coefficients), f32 env[9] (world -> the cube map's frame, by rows), i32 dynamic, (dynamic) f32 pos[nv*3], f32 nrm[nv*3], (kind) f32 tan[nv*3], bin[nv*3])
//       -> answers f32 rgb[w*h*3] (not clamped), f32 depth[w*h] (distance along the view, zfar where empty): on stdout, or
//          written to the answers' file (whole, from its start) and one byte 'K' on stdout when it is complete
//   'Q'                                                      quit
// Items are drawn in the order given: the driver sends opaque ones first, then the blended ones far to near.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

enum { MF_UV1 = 1, MF_VCOL = 2, MF_STATIC = 4, MF_ATEST = 8, MF_ADD = 16, MF_ALPHA = 32, MF_UNLIT = 64, MF_CULL = 128, MF_NOZ = 256, MF_LMADD = 512, MF_DARKUV1 = 1024, MF_CLAMPA = 2048, MF_CLAMPB = 4096, MF_NOFLIP = 8192 };   // (DARKUV1: the dark map reads uv1; CLAMP: the base / the dark map does not repeat; NOFLIP: a two-sided mesh lit by its stored normals on both sides)
typedef struct { int w, h, nl; float lb; uint8_t *lv[14]; int lw[14], lh[14]; } Tex;   // (lb: half the log2 of w x h)
// P: the material's numbers as the PC's shader names them (WeaponMaterial_ps_3_0 / WGStandardMaterial_ps_3_0)
enum { P_DIFV, P_AO, P_HLP, P_LSMIN, P_LSOVER, P_BLPOW, P_BLEXP, P_FRFALL, P_FRVAL, P_FRC, P_IBL = P_FRC + 3, P_CLOD, P_CBRIGHT, P_RFALL, P_RVAL, P_RIMEXP, P_RIMPOW,
       P_MDIF, P_MSPEC, P_MAMB = P_MSPEC + 3, P_MEMI = P_MAMB + 3, P_GLOWV = P_MEMI + 3, P_MPOW, NP };
typedef struct { int nv, ni, flags, tex, lm, glow, spec, dark, kind, nmap, cmap, cube; float col[4], glow_gain, spec_gain, P[NP]; float *uv0, *uv1, *pos, *nrm, *tan, *bin; uint8_t *vc; uint32_t *idx; } Mesh;
typedef struct { int n, nl; float *lv[12]; int ln[12]; } Cube;
#define MAXC 16
static Cube CUBE[MAXC];
#define MAXT 1024
#define MAXM 8192
static Tex TEX[MAXT]; static Mesh MESH[MAXM];
static int W, H, SS, RW, RH; static float *fb, *zb; static char outpath[1024];

static void rd(void *p, size_t n) { if (n && fread(p, 1, n, stdin) != n) { fprintf(stderr, "introrender: input ended\n"); exit(3); } }
static int ri(void) { int32_t v; rd(&v, 4); return v; }
static void *al(size_t n) { void *p = malloc(n ? n : 1); if (!p) { fprintf(stderr, "introrender: out of memory (%lu KB)\n", (unsigned long)(n >> 10)); exit(4); } return p; }

static void tex_load(void) {
	int id = ri(), w = ri(), h = ri(); if (id < 0 || id >= MAXT) exit(5);
	Tex *t = &TEX[id]; for (int i = 0; i < t->nl; i++) free(t->lv[i]);
	t->w = w; t->h = h; t->lb = 0.5f * log2f((float)w * (float)h); t->nl = 1; t->lv[0] = al((size_t)w * h * 4); t->lw[0] = w; t->lh[0] = h; rd(t->lv[0], (size_t)w * h * 4);
	while ((t->lw[t->nl - 1] > 1 || t->lh[t->nl - 1] > 1) && t->nl < 14) {
		int pw = t->lw[t->nl - 1], ph = t->lh[t->nl - 1], nw = pw > 1 ? pw / 2 : 1, nh = ph > 1 ? ph / 2 : 1; uint8_t *s = t->lv[t->nl - 1], *d = al((size_t)nw * nh * 4);
		for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) {
			int x0 = x * pw / nw, x1 = (x0 + 1 < pw) ? x0 + 1 : x0, y0 = y * ph / nh, y1 = (y0 + 1 < ph) ? y0 + 1 : y0;
			for (int c = 0; c < 4; c++) d[(y * nw + x) * 4 + c] = (uint8_t)((s[(y0 * pw + x0) * 4 + c] + s[(y0 * pw + x1) * 4 + c] + s[(y1 * pw + x0) * 4 + c] + s[(y1 * pw + x1) * 4 + c] + 2) / 4); }
		t->lv[t->nl] = d; t->lw[t->nl] = nw; t->lh[t->nl] = nh; t->nl++; }
}
static inline void tex_px(const Tex *t, int l, float u, float v, float *o, int clamp) {   // bilinear; wrapping, or held at the edge
	int w = t->lw[l], h = t->lh[l]; const uint8_t *s = t->lv[l];
	if (clamp) { u = u < 0 ? 0 : u > 1 ? 1 : u; v = v < 0 ? 0 : v > 1 ? 1 : v; }
	float x = u * w - 0.5f, y = v * h - 0.5f; float fx = floorf(x), fy = floorf(y); float ax = x - fx, ay = y - fy;
	int x0 = (int)fx, y0 = (int)fy, x1, y1;
	if (clamp) { x1 = x0 + 1 >= w ? w - 1 : x0 + 1; y1 = y0 + 1 >= h ? h - 1 : y0 + 1; if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x0 >= w) x0 = w - 1; if (y0 >= h) y0 = h - 1; }
	else { x0 %= w; y0 %= h; if (x0 < 0) x0 += w; if (y0 < 0) y0 += h; x1 = x0 + 1 == w ? 0 : x0 + 1; y1 = y0 + 1 == h ? 0 : y0 + 1; }
	const uint8_t *a = s + (y0 * w + x0) * 4, *b = s + (y0 * w + x1) * 4, *c = s + (y1 * w + x0) * 4, *d = s + (y1 * w + x1) * 4;
	float w00 = (1 - ax) * (1 - ay) / 255.0f, w10 = ax * (1 - ay) / 255.0f, w01 = (1 - ax) * ay / 255.0f, w11 = ax * ay / 255.0f;
	for (int k = 0; k < 4; k++) o[k] = a[k] * w00 + b[k] * w10 + c[k] * w01 + d[k] * w11;
}
static inline void tex_sample_c(const Tex *t, float lod, float u, float v, float *o, int clamp) {   // trilinear
	if (lod <= 0) { tex_px(t, 0, u, v, o, clamp); return; }
	if (lod >= t->nl - 1) { tex_px(t, t->nl - 1, u, v, o, clamp); return; }
	int l = (int)lod; float f = lod - l, a[4], b[4]; tex_px(t, l, u, v, a, clamp); tex_px(t, l + 1, u, v, b, clamp); for (int k = 0; k < 4; k++) o[k] = a[k] + (b[k] - a[k]) * f;
}

#define tex_sample(t, lod, u, v, o) tex_sample_c(t, lod, u, v, o, 0)
static void mesh_load(void) {
	int id = ri(); if (id < 0 || id >= MAXM) exit(5);
	Mesh *m = &MESH[id]; free(m->uv0); free(m->uv1); free(m->vc); free(m->idx); free(m->pos); free(m->nrm); free(m->tan); free(m->bin); memset(m, 0, sizeof(*m));
	m->nv = ri(); m->ni = ri(); m->flags = ri(); m->tex = ri(); m->lm = ri(); m->glow = ri(); m->spec = ri(); m->dark = ri(); rd(m->col, 16); rd(&m->glow_gain, 4); rd(&m->spec_gain, 4); m->kind = ri(); m->nmap = ri(); m->cmap = ri(); m->cube = ri(); rd(m->P, sizeof(m->P));
	m->uv0 = al((size_t)m->nv * 8); rd(m->uv0, (size_t)m->nv * 8);
	if (m->flags & MF_UV1) { m->uv1 = al((size_t)m->nv * 8); rd(m->uv1, (size_t)m->nv * 8); }
	if (m->flags & MF_VCOL) { m->vc = al((size_t)m->nv * 4); rd(m->vc, (size_t)m->nv * 4); }
	m->idx = al((size_t)m->ni * 4); rd(m->idx, (size_t)m->ni * 4);
	m->pos = al((size_t)m->nv * 12); m->nrm = al((size_t)m->nv * 12);
	if (m->kind) { m->tan = al((size_t)m->nv * 12); m->bin = al((size_t)m->nv * 12); }
	if (m->flags & MF_STATIC) { rd(m->pos, (size_t)m->nv * 12); rd(m->nrm, (size_t)m->nv * 12); if (m->kind) { rd(m->tan, (size_t)m->nv * 12); rd(m->bin, (size_t)m->nv * 12); } }
}
static void cube_load(void) {
	int id = ri(), n = ri(); if (id < 0 || id >= MAXC || n < 1 || n > 4096) exit(5);
	Cube *c = &CUBE[id]; for (int i = 0; i < c->nl; i++) free(c->lv[i]);
	c->n = n; c->nl = 1; c->ln[0] = n; c->lv[0] = al((size_t)6 * n * n * 12); rd(c->lv[0], (size_t)6 * n * n * 12);
	while (c->ln[c->nl - 1] > 1 && c->nl < 12) { int pn = c->ln[c->nl - 1], nn = pn / 2; float *s = c->lv[c->nl - 1], *d = al((size_t)6 * nn * nn * 12);
		for (int f = 0; f < 6; f++) for (int y = 0; y < nn; y++) for (int x = 0; x < nn; x++) for (int k = 0; k < 3; k++) { const float *p = s + ((size_t)f * pn * pn + (size_t)(2 * y) * pn + 2 * x) * 3 + k;
			d[((size_t)f * nn * nn + (size_t)y * nn + x) * 3 + k] = (p[0] + p[3] + p[(size_t)pn * 3] + p[(size_t)pn * 3 + 3]) * 0.25f; }
		c->lv[c->nl] = d; c->ln[c->nl] = nn; c->nl++; }
}
static inline void cube_px(const Cube *c, int l, int f, float u, float v, float *o) {   // bilinear inside one face, held at its edges
	int n = c->ln[l]; const float *s = c->lv[l] + (size_t)f * n * n * 3; float x = u * n - 0.5f, y = v * n - 0.5f; if (x < 0) x = 0; if (y < 0) y = 0; if (x > n - 1) x = (float)(n - 1); if (y > n - 1) y = (float)(n - 1);
	int x0 = (int)x, y0 = (int)y, x1 = x0 + 1 < n ? x0 + 1 : x0, y1 = y0 + 1 < n ? y0 + 1 : y0; float ax = x - x0, ay = y - y0;
	const float *a = s + ((size_t)y0 * n + x0) * 3, *b = s + ((size_t)y0 * n + x1) * 3, *cc = s + ((size_t)y1 * n + x0) * 3, *d = s + ((size_t)y1 * n + x1) * 3;
	for (int k = 0; k < 3; k++) o[k] = (a[k] * (1 - ax) + b[k] * ax) * (1 - ay) + (cc[k] * (1 - ax) + d[k] * ax) * ay;
}
static void cube_sample(const Cube *c, float lod, const float *r, float *o) {   // the Direct3D face layout
	float x = r[0], y = r[1], z = r[2], ax = fabsf(x), ay = fabsf(y), az = fabsf(z), sc, tc, ma; int f;
	if (ax >= ay && ax >= az) { ma = ax; if (x > 0) { f = 0; sc = -z; tc = -y; } else { f = 1; sc = z; tc = -y; } }
	else if (ay >= az) { ma = ay; if (y > 0) { f = 2; sc = x; tc = z; } else { f = 3; sc = x; tc = -z; } }
	else { ma = az; if (z > 0) { f = 4; sc = x; tc = -y; } else { f = 5; sc = -x; tc = -y; } }
	if (ma < 1e-12f) { o[0] = o[1] = o[2] = 0; return; }
	float u = (sc / ma + 1) * 0.5f, v = (tc / ma + 1) * 0.5f;
	if (lod <= 0) { cube_px(c, 0, f, u, v, o); return; }
	if (lod >= c->nl - 1) { cube_px(c, c->nl - 1, f, u, v, o); return; }
	int l = (int)lod; float t = lod - l, a[3], b[3]; cube_px(c, l, f, u, v, a); cube_px(c, l + 1, f, u, v, b); for (int k = 0; k < 3; k++) o[k] = a[k] + (b[k] - a[k]) * t;
}

// ---------------------------------------------------------------- one frame
#define NA 17
typedef struct { float x, y, z, w; float a[NA]; } V;   // clip position; uv0 2, uv1 2, normal 3, vertex colour 4, tangent 3, binormal 3
static struct { float eye[3], dir[3], up[3], right[3], tanx, tany, zn, zf, amb[3], sky[3], ld[8][3], lc[8][3], rim[4], ad[3], adif[3], aspec[3], aamb[3]; int nl; } FR;
static struct { const Mesh *m; float col[4], ua[6], ub[6], uc[6], sh[27], em[9]; int tex; } IT;

static void shade_tri(const V *v0, const V *v1, const V *v2) {
	const Mesh *m = IT.m; int fl = m->flags;
	float sx[3], sy[3], iw[3]; const V *vv[3] = { v0, v1, v2 };
	for (int i = 0; i < 3; i++) { iw[i] = 1.0f / vv[i]->w; sx[i] = (vv[i]->x * iw[i] * 0.5f + 0.5f) * RW; sy[i] = (0.5f - vv[i]->y * iw[i] * 0.5f) * RH; }
	float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
	if (area == 0 || area != area) return;
	if ((fl & MF_CULL) && area > 0) return;
	float minx = fminf(sx[0], fminf(sx[1], sx[2])), maxx = fmaxf(sx[0], fmaxf(sx[1], sx[2])), miny = fminf(sy[0], fminf(sy[1], sy[2])), maxy = fmaxf(sy[0], fmaxf(sy[1], sy[2]));
	if (maxx < 0 || maxy < 0 || minx >= RW || miny >= RH) return;
	int x0 = minx < 0 ? 0 : (int)minx, x1 = maxx >= RW - 1 ? RW - 1 : (int)maxx, y0 = miny < 0 ? 0 : (int)miny, y1 = maxy >= RH - 1 ? RH - 1 : (int)maxy;
	int ti = IT.tex != -2 ? IT.tex : m->tex;
	const Tex *T = ti >= 0 && ti < MAXT && TEX[ti].nl ? &TEX[ti] : NULL, *D = m->dark >= 0 && TEX[m->dark].nl ? &TEX[m->dark] : NULL, *L = m->lm >= 0 && TEX[m->lm].nl ? &TEX[m->lm] : NULL, *G = m->glow >= 0 && TEX[m->glow].nl ? &TEX[m->glow] : NULL, *S = m->spec >= 0 && TEX[m->spec].nl ? &TEX[m->spec] : NULL,
		*Nm = m->kind && m->nmap >= 0 && TEX[m->nmap].nl ? &TEX[m->nmap] : NULL, *Cm = m->kind && m->cmap >= 0 && TEX[m->cmap].nl ? &TEX[m->cmap] : NULL;
	// mip level of the triangle: texels per pixel (by area)
	float lod = 0, lodl = 0, lodd = 0, lodu = -100;   // (lodu: half the log2 of the uv area a pixel covers - a map's level is that + half the log2 of its size)
	{ float ua0 = fabsf((v1->a[0] - v0->a[0]) * (v2->a[1] - v0->a[1]) - (v2->a[0] - v0->a[0]) * (v1->a[1] - v0->a[1])), ua = ua0 * fabsf(IT.ua[0] * IT.ua[4] - IT.ua[1] * IT.ua[3]), ud = ua0 * fabsf(IT.ub[0] * IT.ub[4] - IT.ub[1] * IT.ub[3]);
	  if (D && ud > 0) lodd = 0.5f * log2f(ud * D->w * D->h / fabsf(area));
	  if (T && ua > 0) lod = 0.5f * log2f(ua * T->w * T->h / fabsf(area));
	  if (ua > 0) lodu = 0.5f * log2f(ua / fabsf(area));
	  float la = fabsf((v1->a[2] - v0->a[2]) * (v2->a[3] - v0->a[3]) - (v2->a[2] - v0->a[2]) * (v1->a[3] - v0->a[3]));
	  if (L && la > 0) lodl = 0.5f * log2f(la * L->w * L->h / fabsf(area)); }
	float ia = 1.0f / area; float pa[3][NA];
	for (int i = 0; i < 3; i++) for (int k = 0; k < NA; k++) pa[i][k] = vv[i]->a[k] * iw[i];
	for (int y = y0; y <= y1; y++) {
		float py = y + 0.5f; float *frow = fb + (size_t)y * RW * 3, *zrow = zb + (size_t)y * RW;
		for (int x = x0; x <= x1; x++) {
			float px = x + 0.5f;
			float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * ia, w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * ia, w2 = 1 - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0) continue;
			float q = w0 * iw[0] + w1 * iw[1] + w2 * iw[2], z = 1.0f / q;
			if (!(fl & MF_NOZ) && z >= zrow[x]) continue;
			float a[NA]; for (int k = 0; k < NA; k++) a[k] = (w0 * pa[0][k] + w1 * pa[1][k] + w2 * pa[2][k]) * z;
			float c[4] = { 1, 1, 1, 1 }, u = a[0] * IT.ua[0] + a[1] * IT.ua[1] + IT.ua[2], v = a[0] * IT.ua[3] + a[1] * IT.ua[4] + IT.ua[5];
			if (T) tex_sample_c(T, lod, u, v, c, fl & MF_CLAMPA);
			if (D) { float dk[4], du = (fl & MF_DARKUV1) ? a[2] : a[0], dv = (fl & MF_DARKUV1) ? a[3] : a[1]; tex_sample_c(D, lodd, du * IT.ub[0] + dv * IT.ub[1] + IT.ub[2], du * IT.ub[3] + dv * IT.ub[4] + IT.ub[5], dk, fl & MF_CLAMPB); c[0] *= dk[0]; c[1] *= dk[1]; c[2] *= dk[2]; }
			if (fl & MF_VCOL) { c[0] *= a[7]; c[1] *= a[8]; c[2] *= a[9]; c[3] *= a[10]; }
			for (int k = 0; k < 4; k++) c[k] *= m->col[k] * IT.col[k];
			if ((fl & MF_ATEST) && c[3] < 0.5f) continue;
			float o[3];
			if (fl & MF_UNLIT) { o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; }
			else if (m->kind) {   // the PC's own actor shaders, term by term (WeaponMaterial_ps_3_0, WGStandardMaterial_ps_3_0)
				const float *P = m->P; float ng[3] = { a[4], a[5], a[6] }, tg[3] = { a[11], a[12], a[13] }, bn[3] = { a[14], a[15], a[16] }, n[3], l_;
				l_ = sqrtf(ng[0] * ng[0] + ng[1] * ng[1] + ng[2] * ng[2]); if (l_ > 1e-12f) { ng[0] /= l_; ng[1] /= l_; ng[2] /= l_; }
				l_ = sqrtf(tg[0] * tg[0] + tg[1] * tg[1] + tg[2] * tg[2]); if (l_ > 1e-12f) { tg[0] /= l_; tg[1] /= l_; tg[2] /= l_; }
				l_ = sqrtf(bn[0] * bn[0] + bn[1] * bn[1] + bn[2] * bn[2]); if (l_ > 1e-12f) { bn[0] /= l_; bn[1] /= l_; bn[2] /= l_; }
				float nm[4] = { 0.5f, 0.5f, 1, 1 }; if (Nm) tex_sample(Nm, lodu + Nm->lb, u, v, nm);
				nm[0] = nm[0] * 2 - 1; nm[1] = nm[1] * 2 - 1; nm[2] = nm[2] * 2 - 1;
				for (int k = 0; k < 3; k++) n[k] = tg[k] * nm[0] + bn[k] * nm[1] + ng[k] * nm[2];
				l_ = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); if (l_ > 1e-12f) { n[0] /= l_; n[1] /= l_; n[2] /= l_; }
				float fxn = (px / RW * 2 - 1) * FR.tanx, fyn = (1 - py / RH * 2) * FR.tany, vd[3];
				for (int k = 0; k < 3; k++) vd[k] = -(FR.dir[k] + FR.right[k] * fxn + FR.up[k] * fyn);
				l_ = sqrtf(vd[0] * vd[0] + vd[1] * vd[1] + vd[2] * vd[2]); vd[0] /= l_; vd[1] /= l_; vd[2] /= l_;
				const float *Ld = FR.ad; float nl = -(n[0] * Ld[0] + n[1] * Ld[1] + n[2] * Ld[2]), nv = n[0] * vd[0] + n[1] * vd[1] + n[2] * vd[2];
				float hv[3] = { vd[0] - Ld[0], vd[1] - Ld[1], vd[2] - Ld[2] }; l_ = sqrtf(hv[0] * hv[0] + hv[1] * hv[1] + hv[2] * hv[2]); if (l_ > 1e-12f) { hv[0] /= l_; hv[1] /= l_; hv[2] /= l_; }
				float nh = n[0] * hv[0] + n[1] * hv[1] + n[2] * hv[2];
				float gl[4] = { 1, 1, 1, 1 }; if (S) tex_sample(S, lodu + S->lb, u, v, gl);
				float rv[3], env[3] = { 0, 0, 0 }; { float d2 = 2 * nv, rw_[3]; for (int k = 0; k < 3; k++) rw_[k] = n[k] * d2 - vd[k]; for (int k = 0; k < 3; k++) rv[k] = IT.em[k * 3] * rw_[0] + IT.em[k * 3 + 1] * rw_[1] + IT.em[k * 3 + 2] * rw_[2]; }   // (the mirrored view, in the cube map's own frame)
				const Cube *Cb = m->cube >= 0 && m->cube < MAXC && CUBE[m->cube].nl ? &CUBE[m->cube] : NULL;
				float base[3] = { c[0] * P[P_DIFV], c[1] * P[P_DIFV], c[2] * P[P_DIFV] };
				if (m->kind == 1) {
					float x = n[0], y = n[1], zz = -n[2], E[3]; const float *H_ = IT.sh;   // the probe's light on this normal (the shader's own constants)
					for (int k = 0; k < 3; k++) E[k] = 0.429043f * H_[24 + k] * (x * x - y * y) + 0.743125f * H_[18 + k] * zz * zz + 0.886227f * H_[k] - 0.247708f * H_[18 + k]
						+ 0.858086f * (H_[12 + k] * x * y + H_[21 + k] * x * zz + H_[15 + k] * y * zz) + 1.02333f * (H_[9 + k] * x + H_[3 + k] * y + H_[6 + k] * zz);
					float bl = 1 - (ng[0] * n[0] + ng[1] * n[1] + ng[2] * n[2]); if (bl < 0) bl = 0; if (bl > P[P_BLPOW]) bl = P[P_BLPOW]; bl = bl > 0 ? powf(bl, P[P_BLEXP]) : 0;
					float hl = powf(nl * 0.5f + 0.5f, P[P_HLP]), w = bl + (1 - bl) * hl; w = w * (P[P_LSOVER] - P[P_LSMIN] + 1) + P[P_LSMIN];
					float lt[3] = { w * FR.adif[0], w * FR.adif[1], w * FR.adif[2] };
					float fx = nv >= 0 ? 1 - nv : 1; if (fx < 1e-4f) fx = 1e-4f; { float f2 = fx * fx, f4 = f2 * f2, f8 = f4 * f4; fx = powf(f8 >= 1e-4f ? f8 : 1e-4f, P[P_FRFALL]); } fx *= P[P_FRVAL];
					float fs = fx < 0 ? 0 : fx > 1 ? 1 : fx, ib = P[P_IBL] < 0 ? 0 : P[P_IBL] > 1 ? 1 : P[P_IBL];
					float cm[4] = { 1, 1, 1, 1 }; if (Cm) tex_sample(Cm, lodu + Cm->lb, u, v, cm);
					for (int k = 0; k < 3; k++) { lt[k] += fs * (P[P_FRC + k] * fx - lt[k]); lt[k] += ib * (lt[k] * E[k] - lt[k]);
						float lit = P[P_MDIF] * lt[k] + FR.aamb[k] * P[P_MAMB + k] + P[P_MEMI + k];
						base[k] += P[P_AO] * (base[k] * cm[1] - base[k]); o[k] = lit * base[k]; }
					if (P[P_RIMPOW] != 0 && P[P_RIMEXP] != 0) { float r1 = 1 - fabsf(ng[0] * vd[0] + ng[1] * vd[1] + ng[2] * vd[2]), lv_ = Ld[0] * vd[0] + Ld[1] * vd[1] + Ld[2] * vd[2];
						if (lv_ > 0 && r1 > 0) { float r = powf(r1, P[P_RIMEXP]) * lv_ * P[P_RIMPOW]; o[0] += r * FR.adif[0]; o[1] += r * FR.adif[1]; o[2] += r * FR.adif[2]; } }
					if (G) { float g[4]; tex_sample(G, lodu + G->lb, u, v, g); o[0] += g[0]; o[1] += g[1]; o[2] += g[2]; }
					if (Cb) cube_sample(Cb, (1 - cm[3]) * P[P_CLOD], rv, env);
					float r3 = 1 - (nv < 0 ? 0 : nv > 1 ? 1 : nv); if (r3 < 1e-4f) r3 = 1e-4f; r3 = r3 * r3 * r3; float rw = cm[2] * P[P_RVAL] * powf(r3 <= 0.9999f ? 1 - r3 : 1e-4f, P[P_RFALL]);
					float sp = powf(nh > 1e-5f ? nh : 1e-5f, cm[0] * 255.0f);
					for (int k = 0; k < 3; k++) o[k] += rw * env[k] * P[P_CBRIGHT] * gl[k] + sp * FR.aspec[k] * P[P_MSPEC + k] * gl[k];
				} else if (m->kind == 4) {   // the room (the PC's CustomMaterial on baked light): the lightmap, the normal map's relief against the sun, the sun's highlight
					float lm[4] = { 0.5f, 0.5f, 0.5f, 1 }, g[4] = { 0, 0, 0, 0 }; if (L) tex_sample(L, lodl, a[2], a[3], lm);
					float d = fabsf(fabsf(ng[0] * FR.ad[0] + ng[1] * FR.ad[1] + ng[2] * FR.ad[2]) - fabsf(nl)), bp = d > 1e-4f ? P[P_BLPOW] * powf(d, P[P_BLEXP]) : 0; bp = 1 - (bp > 1 ? 1 : bp);
					float sp = nh > 1e-5f ? powf(nh, P[P_MPOW]) : 0;
					if (G) tex_sample(G, lodu + G->lb, u, v, g);
					if (Cm) { float e4[4]; tex_sample_c(Cm, 0, 0.5f + 0.5f * (rv[0] * FR.right[0] + rv[1] * FR.right[1] + rv[2] * FR.right[2]), 0.5f - 0.5f * (rv[0] * FR.up[0] + rv[1] * FR.up[1] + rv[2] * FR.up[2]), e4, 1); env[0] = e4[0]; env[1] = e4[1]; env[2] = e4[2]; }   // (the mesh's sphere map: a picture of the room, looked up with the mirrored view as the camera sees it)
					for (int k = 0; k < 3; k++) o[k] = base[k] * lm[k] * (bp + P[P_MAMB + k] * FR.aamb[k]) + g[k] * P[P_GLOWV] + gl[k] * (sp * P[P_MSPEC + k] + env[k]);
				} else if (m->kind == 3) {   // no shader of its own (the eye's reflection shell): the base x the material's own light, and the cube
					if (Cb) cube_sample(Cb, 0, rv, env);
					for (int k = 0; k < 3; k++) o[k] = base[k] * P[P_MEMI + k] + env[k];
				} else {
					float sp = nl > 0 && nh > 1e-5f ? powf(nh, P[P_MPOW]) : 0, df = nl > 0 ? nl : 0, g[4] = { 0, 0, 0, 0 };
					if (Cb) cube_sample(Cb, 0, rv, env);
					if (G) tex_sample(G, lodu + G->lb, u, v, g);
					for (int k = 0; k < 3; k++) o[k] = (df * FR.adif[k] * P[P_MDIF] + FR.aamb[k] * P[P_MAMB + k] + P[P_MEMI + k]) * base[k] + g[k] * P[P_GLOWV] + gl[k] * (env[k] + sp * FR.aspec[k] * P[P_MSPEC + k]);
				}
			}
			else {
				float n[3] = { a[4], a[5], a[6] }, nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); if (nl > 1e-9f) { n[0] /= nl; n[1] /= nl; n[2] /= nl; }
				// the view vector at this pixel (for the two-sided normal, the rim and the highlight)
				float fxn = (px / RW * 2 - 1) * FR.tanx, fyn = (1 - py / RH * 2) * FR.tany, vd[3];
				for (int k = 0; k < 3; k++) vd[k] = -(FR.dir[k] + FR.right[k] * fxn + FR.up[k] * fyn);
				float vl = sqrtf(vd[0] * vd[0] + vd[1] * vd[1] + vd[2] * vd[2]); vd[0] /= vl; vd[1] /= vl; vd[2] /= vl;
				float nv = n[0] * vd[0] + n[1] * vd[1] + n[2] * vd[2];
				if (nv < 0 && !(fl & (MF_CULL | MF_NOFLIP))) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; nv = -nv; }
				if (nv < 0) nv = -nv;
				float li[3], sp[3] = { 0, 0, 0 }, sg = m->spec_gain;
				if (L && !(fl & MF_LMADD)) { float lm[4]; tex_sample(L, lodl, a[2], a[3], lm); li[0] = lm[0]; li[1] = lm[1]; li[2] = lm[2]; }
				else {
					{ float hz = n[2] * 0.5f + 0.5f; li[0] = FR.amb[0] + (FR.sky[0] - FR.amb[0]) * hz; li[1] = FR.amb[1] + (FR.sky[1] - FR.amb[1]) * hz; li[2] = FR.amb[2] + (FR.sky[2] - FR.amb[2]) * hz; }   // (light from all round: more from above)
					float sm[4] = { 1, 1, 1, 1 }; if (S && sg > 0) tex_px(S, lod <= 0 ? 0 : lod >= S->nl - 1 ? S->nl - 1 : (int)(lod + 0.5f), u, v, sm, 0);   // (the nearest level is enough for the gloss)
					for (int i = 0; i < FR.nl; i++) {
						float d = n[0] * FR.ld[i][0] + n[1] * FR.ld[i][1] + n[2] * FR.ld[i][2], dw = d * 0.5f + 0.5f; dw *= dw;   // (a wrapped, soft light: the PC lights the actors from all round)
						li[0] += FR.lc[i][0] * dw; li[1] += FR.lc[i][1] * dw; li[2] += FR.lc[i][2] * dw;
						if (sg > 0 && d > 0) { float hx = FR.ld[i][0] + vd[0], hy = FR.ld[i][1] + vd[1], hz = FR.ld[i][2] + vd[2], hl = sqrtf(hx * hx + hy * hy + hz * hz);
							float nh = (n[0] * hx + n[1] * hy + n[2] * hz) / (hl > 1e-9f ? hl : 1); if (nh > 0) { float n2 = nh * nh, n4 = n2 * n2, n8 = n4 * n4, s_ = n8 * n8 * n8 * sg;   /* nh^24 */ sp[0] += FR.lc[i][0] * s_ * sm[0]; sp[1] += FR.lc[i][1] * s_ * sm[1]; sp[2] += FR.lc[i][2] * s_ * sm[2]; } } }
					if (FR.rim[3] > 0) { float r1 = 1 - (nv > 1 ? 1 : nv), r = FR.rim[3] == 3.0f ? r1 * r1 * r1 : powf(r1, FR.rim[3]); li[0] += FR.rim[0] * r; li[1] += FR.rim[1] * r; li[2] += FR.rim[2] * r; }
				}
				o[0] = c[0] * li[0] + sp[0]; o[1] = c[1] * li[1] + sp[1]; o[2] = c[2] * li[2] + sp[2];
			}
			if (G && !m->kind) { float g[4]; tex_sample(G, lod, a[0] * IT.uc[0] + a[1] * IT.uc[1] + IT.uc[2], a[0] * IT.uc[3] + a[1] * IT.uc[4] + IT.uc[5], g); o[0] += g[0] * m->glow_gain; o[1] += g[1] * m->glow_gain; o[2] += g[2] * m->glow_gain; }
			float *f = frow + x * 3;
			if (fl & MF_ADD) { f[0] += o[0] * c[3]; f[1] += o[1] * c[3]; f[2] += o[2] * c[3]; }
			else if (fl & MF_ALPHA) { float al_ = c[3] > 1 ? 1 : c[3]; f[0] += (o[0] - f[0]) * al_; f[1] += (o[1] - f[1]) * al_; f[2] += (o[2] - f[2]) * al_; }
			else { f[0] = o[0]; f[1] = o[1]; f[2] = o[2]; zrow[x] = z; }
		}
	}
}
static void clip_tri(const V *t0, const V *t1, const V *t2) {   // against the near plane (w >= zn), then drawn
	const V *in[3] = { t0, t1, t2 }; V out[4]; int n = 0; float zn = FR.zn;
	for (int i = 0; i < 3; i++) { const V *a = in[i], *b = in[(i + 1) % 3]; int ia = a->w >= zn, ib = b->w >= zn;
		if (ia) out[n++] = *a;
		if (ia != ib) { float t = (zn - a->w) / (b->w - a->w); V *o = &out[n++]; o->x = a->x + (b->x - a->x) * t; o->y = a->y + (b->y - a->y) * t; o->z = 0; o->w = zn; for (int k = 0; k < NA; k++) o->a[k] = a->a[k] + (b->a[k] - a->a[k]) * t; } }
	if (n < 3) return;
	shade_tri(&out[0], &out[1], &out[2]); if (n == 4) shade_tri(&out[0], &out[2], &out[3]);
}
static V *vbuf; static int vcap;
static void draw_item(void) {
	const Mesh *m = IT.m; if (m->nv > vcap) { free(vbuf); vcap = m->nv + 1024; vbuf = al(sizeof(V) * vcap); }
	for (int i = 0; i < m->nv; i++) { const float *p = m->pos + i * 3; float d[3] = { p[0] - FR.eye[0], p[1] - FR.eye[1], p[2] - FR.eye[2] }; V *v = &vbuf[i];
		float vx = d[0] * FR.right[0] + d[1] * FR.right[1] + d[2] * FR.right[2], vy = d[0] * FR.up[0] + d[1] * FR.up[1] + d[2] * FR.up[2], vz = d[0] * FR.dir[0] + d[1] * FR.dir[1] + d[2] * FR.dir[2];
		v->x = vx / FR.tanx; v->y = vy / FR.tany; v->z = 0; v->w = vz;
		v->a[0] = m->uv0[i * 2]; v->a[1] = m->uv0[i * 2 + 1]; if (m->uv1) { v->a[2] = m->uv1[i * 2]; v->a[3] = m->uv1[i * 2 + 1]; } else v->a[2] = v->a[3] = 0;
		v->a[4] = m->nrm[i * 3]; v->a[5] = m->nrm[i * 3 + 1]; v->a[6] = m->nrm[i * 3 + 2];
		if (m->tan) { for (int k = 0; k < 3; k++) { v->a[11 + k] = m->tan[i * 3 + k]; v->a[14 + k] = m->bin[i * 3 + k]; } } else for (int k = 11; k < NA; k++) v->a[k] = 0;
		if (m->vc) for (int k = 0; k < 4; k++) v->a[7 + k] = m->vc[i * 4 + k] / 255.0f; else v->a[7] = v->a[8] = v->a[9] = v->a[10] = 1; }
	for (int i = 0; i + 2 < m->ni; i += 3) { uint32_t a = m->idx[i], b = m->idx[i + 1], c = m->idx[i + 2]; if (a >= (uint32_t)m->nv || b >= (uint32_t)m->nv || c >= (uint32_t)m->nv) continue;
		const V *v0 = &vbuf[a], *v1 = &vbuf[b], *v2 = &vbuf[c];
		if (v0->w < FR.zn && v1->w < FR.zn && v2->w < FR.zn) continue;
		if (v0->w >= FR.zn && v1->w >= FR.zn && v2->w >= FR.zn) {   // (outside one side of the view: skipped)
			if ((v0->x > v0->w && v1->x > v1->w && v2->x > v2->w) || (v0->x < -v0->w && v1->x < -v1->w && v2->x < -v2->w) || (v0->y > v0->w && v1->y > v1->w && v2->y > v2->w) || (v0->y < -v0->w && v1->y < -v1->w && v2->y < -v2->w)) continue;
			shade_tri(v0, v1, v2); }
		else clip_tri(v0, v1, v2); }
}
static void frame(void) {
	rd(FR.eye, 12); rd(FR.dir, 12); rd(FR.up, 12); rd(&FR.tanx, 4); rd(&FR.tany, 4); rd(&FR.zn, 4); rd(&FR.zf, 4); rd(FR.amb, 12); rd(FR.sky, 12); FR.nl = ri(); if (FR.nl > 8) exit(5);
	for (int i = 0; i < FR.nl; i++) { rd(FR.ld[i], 12); rd(FR.lc[i], 12); float l = sqrtf(FR.ld[i][0] * FR.ld[i][0] + FR.ld[i][1] * FR.ld[i][1] + FR.ld[i][2] * FR.ld[i][2]); if (l > 0) for (int k = 0; k < 3; k++) FR.ld[i][k] /= l; }
	rd(FR.rim, 16); rd(FR.ad, 12); rd(FR.adif, 12); rd(FR.aspec, 12); rd(FR.aamb, 12);
	{ float l = sqrtf(FR.ad[0] * FR.ad[0] + FR.ad[1] * FR.ad[1] + FR.ad[2] * FR.ad[2]); if (l > 0) for (int k = 0; k < 3; k++) FR.ad[k] /= l; }
	{ float *d = FR.dir, *u = FR.up, l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); for (int k = 0; k < 3; k++) d[k] /= l;
	  float r[3] = { d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0] }; l = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]); for (int k = 0; k < 3; k++) FR.right[k] = r[k] / l;
	  u[0] = FR.right[1] * d[2] - FR.right[2] * d[1]; u[1] = FR.right[2] * d[0] - FR.right[0] * d[2]; u[2] = FR.right[0] * d[1] - FR.right[1] * d[0]; }
	size_t np = (size_t)RW * RH; memset(fb, 0, np * 12); for (size_t i = 0; i < np; i++) zb[i] = FR.zf;
	int n = ri();
	for (int i = 0; i < n; i++) { int id = ri(); if (id < 0 || id >= MAXM || !MESH[id].idx) exit(6);
		Mesh *m = &MESH[id]; IT.m = m; rd(IT.col, 16); rd(IT.ua, 24); rd(IT.ub, 24); rd(IT.uc, 24); IT.tex = ri(); rd(IT.sh, sizeof(IT.sh)); rd(IT.em, sizeof(IT.em)); if (ri()) { rd(m->pos, (size_t)m->nv * 12); rd(m->nrm, (size_t)m->nv * 12); if (m->kind) { rd(m->tan, (size_t)m->nv * 12); rd(m->bin, (size_t)m->nv * 12); } }
		draw_item(); }
	// the answer: the supersamples' mean
	static float *out, *outz; if (!out) { out = al((size_t)W * H * 12); outz = al((size_t)W * H * 4); }
	float k = 1.0f / (SS * SS);
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { float r = 0, g = 0, b = 0, z = 0;
		for (int j = 0; j < SS; j++) { const float *f = fb + ((size_t)(y * SS + j) * RW + x * SS) * 3, *zz = zb + (size_t)(y * SS + j) * RW + x * SS; for (int i = 0; i < SS; i++) { r += f[i * 3]; g += f[i * 3 + 1]; b += f[i * 3 + 2]; z += zz[i]; } }
		float *o = out + ((size_t)y * W + x) * 3; o[0] = r * k; o[1] = g * k; o[2] = b * k; outz[(size_t)y * W + x] = z * k; }
	if (outpath[0]) { FILE *f = fopen(outpath, "wb"); if (!f || fwrite(out, 1, (size_t)W * H * 12, f) != (size_t)W * H * 12 || fwrite(outz, 1, (size_t)W * H * 4, f) != (size_t)W * H * 4 || fclose(f)) { fprintf(stderr, "introrender: cannot write %s\n", outpath); exit(7); }
		fputc('K', stdout); }
	else { fwrite(out, 1, (size_t)W * H * 12, stdout); fwrite(outz, 1, (size_t)W * H * 4, stdout); }
	fflush(stdout);
}
int main(void) {
#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
	static char ibuf[1 << 20]; setvbuf(stdin, ibuf, _IOFBF, sizeof(ibuf));
	for (;;) { int c = getchar();
		if (c == 'I') { W = ri(); H = ri(); SS = ri(); int pn = ri(); if (W < 1 || H < 1 || SS < 1 || SS > 4 || pn < 0 || pn >= (int)sizeof(outpath)) return 5; rd(outpath, pn); outpath[pn] = 0; RW = W * SS; RH = H * SS; free(fb); free(zb); fb = al((size_t)RW * RH * 12); zb = al((size_t)RW * RH * 4); }
		else if (c == 'T') tex_load();
		else if (c == 'M') mesh_load();
		else if (c == 'C') cube_load();
		else if (c == 'F') { if (!fb) return 5; frame(); }
		else if (c == 'Q' || c == EOF) return 0;
		else { fprintf(stderr, "introrender: unknown command %d\n", c); return 2; } }
}
