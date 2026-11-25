// The game's own in-game HUD (InGameUI.swf / InGameUI_D.swf, Scaleform). tools/ui/make_hud.py renders every
// part flat in its own Flash plane and stores the plane's perspective (local -> screen homography) and
// colour transform; here each part is one perspective-correct quad. Gauges use a ramp texel per pixel
// (the fraction at which it lights up), text uses the Eurostile glyphs from fonts_en.swf.
#pragma once
#ifndef VGL_XFER_WAIT
#ifdef __vita__
#define VGL_XFER_WAIT() sceGxmTransferFinish()   // vitaGL uploads copy asynchronously: wait before the source buffer is reused
#else
#define VGL_XFER_WAIT() ((void)0)
#endif
#endif
#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef struct { char name[32]; unsigned char tex, p0, p1, p2; unsigned short x, y, w, h; float loc[4]; } HImg;
typedef struct { char name[32]; float H[9]; float mul[4], add[4]; } HNode;
typedef struct { char name[32]; int node; float b[4]; float size; int align; unsigned char col[4]; } HField;
// a pack = one .ghud file (the in-game HUD, the menus ...); hud_use() picks the one the draw calls refer to
typedef struct { HImg *img; int nimg; HNode *node; int nnode; HField *field; int nfield; short glyph[256]; float adv[256], asc, desc; GLuint tex[3]; int tw[3], th[3];   /* (b92: [2] = a pack's third page, version 2: the menu's weapon icons) */
	unsigned long long *occ; } HudPack;   // render b43: occ = per image, the 8 x 8 cells hud_img draws (hud_trim_atlas)
#define HUD_MAXPACK 4
static HudPack hud_pack[HUD_MAXPACK], *HP = &hud_pack[0]; static int hud_npack;
#define himg (HP->img)
#define nhimg (HP->nimg)
#define hnode (HP->node)
#define nhnode (HP->nnode)
#define hfield (HP->field)
#define nhfield (HP->nfield)
#define hglyph (HP->glyph)
#define hadv (HP->adv)
#define hasc (HP->asc)
#define hdesc (HP->desc)
#define htex (HP->tex)
#define htw (HP->tw)
#define hth (HP->th)
static GLuint hprog; static int hud_ok;
static float hud_dx, hud_dy;          // screen offset added to everything drawn (for parts that follow a 3D point)
#define HUD_VF 17
#define HUD_MAXQ 700
static float hvb[HUD_MAXQ * 6 * HUD_VF]; static int hvn; static GLuint hcur;
#ifdef MU_AUDIT
// b95 (host only, tools/menu/host: user - "no boxes overlap over their outlines in the menus"): every outlined box the menus draw
// (mu_cutline / mu_frame) is noted; a triangle drawn after it whose centre is inside the box but a corner more than 1 px past
// the box's outline (a thumbnail over a cut corner, text past the edge) is reported once per screen with mu_aud_name
typedef struct { float x0, y0, x1, y1, k[4], w; int hidden; } HAudBox;
static HAudBox hud_aud_box[1024]; static int hud_aud_nb, hud_aud_tn, hud_aud_found; static float hud_aud_t[3][3];
static const char *mu_aud_name = ""; static char hud_aud_seen[256][96]; static int hud_aud_nseen;
static struct { int box; float d, t[4]; } hud_aud_f[256]; static int hud_aud_nf;   // findings, printed at the end of the screen unless a later opaque panel covered the box
static struct { float x0, y0, x1, y1; int after; } hud_aud_cv[512]; static int hud_aud_ncv;   // opaque panels drawn after box 'after'
static void hud_aud_reset(void) { hud_aud_nb = 0; hud_aud_tn = 0; hud_aud_nseen = 0; hud_aud_nf = 0; hud_aud_ncv = 0; }
static void hud_aud_cover(float x0, float y0, float x1, float y1, float a) {   // an opaque panel: what it covers is hidden
	if (!*mu_aud_name || a < 0.85f || x1 - x0 < 120 || y1 - y0 < 24) return; x0 += hud_dx; x1 += hud_dx; y0 += hud_dy; y1 += hud_dy;
	for (int i = 0; i < hud_aud_nb; i++) { HAudBox *b = &hud_aud_box[i]; if (b->x0 >= x0 - 1 && b->y0 >= y0 - 1 && b->x1 <= x1 + 1 && b->y1 <= y1 + 1 && !(b->x0 <= x0 + 1 && b->y0 <= y0 + 1 && b->x1 >= x1 - 1 && b->y1 >= y1 - 1)) b->hidden = 1; }
	if (hud_aud_ncv < 512) { hud_aud_cv[hud_aud_ncv].x0 = x0; hud_aud_cv[hud_aud_ncv].y0 = y0; hud_aud_cv[hud_aud_ncv].x1 = x1; hud_aud_cv[hud_aud_ncv].y1 = y1; hud_aud_cv[hud_aud_ncv].after = hud_aud_nb; hud_aud_ncv++; } }
static void hud_aud_flush(void) {
	for (int i = 0; i < hud_aud_nf; i++) { const HAudBox *b = &hud_aud_box[hud_aud_f[i].box]; if (b->hidden) continue;
		int cov = 0; const float *t = hud_aud_f[i].t;   // (the overflowing part under a later opaque panel: not seen)
		for (int c = 0; c < hud_aud_ncv && !cov; c++) if (hud_aud_cv[c].after > hud_aud_f[i].box && t[0] >= hud_aud_cv[c].x0 - 1 && t[1] >= hud_aud_cv[c].y0 - 1 && t[2] <= hud_aud_cv[c].x1 + 1 && t[3] <= hud_aud_cv[c].y1 + 1) cov = 1;
		if (cov) continue; hud_aud_found++;
		printf("AUDIT %s: box (%.0f,%.0f)-(%.0f,%.0f) cuts %.0f/%.0f/%.0f/%.0f: drawn %.1f px past its outline by (%.0f,%.0f)-(%.0f,%.0f)\n", mu_aud_name,
			b->x0, b->y0, b->x1, b->y1, b->k[0], b->k[1], b->k[2], b->k[3], hud_aud_f[i].d, hud_aud_f[i].t[0], hud_aud_f[i].t[1], hud_aud_f[i].t[2], hud_aud_f[i].t[3]); }
	hud_aud_nf = 0; }
static void hud_aud_add(float x0, float y0, float x1, float y1, const float *k, float w) {
	if (hud_aud_nb >= 1024 || x1 - x0 < 8 || y1 - y0 < 8) return; HAudBox *b = &hud_aud_box[hud_aud_nb++];
	b->x0 = x0 + hud_dx; b->y0 = y0 + hud_dy; b->x1 = x1 + hud_dx; b->y1 = y1 + hud_dy; for (int i = 0; i < 4; i++) b->k[i] = k ? k[i] : 0; b->w = w; b->hidden = 0; }
static float hud_aud_out(const HAudBox *b, float x, float y) {   // > 0: outside the cut-corner polygon by this many px
	float d = fmaxf(fmaxf(b->x0 - x, x - b->x1), fmaxf(b->y0 - y, y - b->y1)), r2 = 0.70710678f;
	if (b->k[0] > 0) d = fmaxf(d, ((b->x0 + b->k[0] - x) + (b->y0 - y)) * r2);   // corner cuts: the diagonal's outward distance
	if (b->k[1] > 0) d = fmaxf(d, ((x - (b->x1 - b->k[1])) + (b->y0 - y)) * r2);
	if (b->k[2] > 0) d = fmaxf(d, ((x - (b->x1 - b->k[2])) + (y - b->y1)) * r2);
	if (b->k[3] > 0) d = fmaxf(d, ((b->x0 + b->k[3] - x) + (y - b->y1)) * r2);
	return d;
}
static void hud_aud_v(const float *o) {
	if (!*mu_aud_name) return; float W = o[3] != 0 ? o[3] : 1;
	hud_aud_t[hud_aud_tn][0] = (o[0] / W + 1) * 480; hud_aud_t[hud_aud_tn][1] = (1 - o[1] / W) * 272; hud_aud_t[hud_aud_tn][2] = o[11];
	if (++hud_aud_tn < 3) return; hud_aud_tn = 0;
	float cx = 0, cy = 0, tx0 = 1e9f, ty0 = 1e9f, tx1 = -1e9f, ty1 = -1e9f, al = fmaxf(hud_aud_t[0][2], fmaxf(hud_aud_t[1][2], hud_aud_t[2][2]));
	if (al < 0.3f) return;
	for (int j = 0; j < 3; j++) { cx += hud_aud_t[j][0] / 3; cy += hud_aud_t[j][1] / 3; tx0 = fminf(tx0, hud_aud_t[j][0]); ty0 = fminf(ty0, hud_aud_t[j][1]); tx1 = fmaxf(tx1, hud_aud_t[j][0]); ty1 = fmaxf(ty1, hud_aud_t[j][1]); }
	for (int i = 0; i < hud_aud_nb; i++) { const HAudBox *b = &hud_aud_box[i];
		if (tx0 <= b->x0 + 1 && ty0 <= b->y0 + 1 && tx1 >= b->x1 - 1 && ty1 >= b->y1 - 1) continue;   // (a fill as big as the box: its own background, a backdrop)
		if (hud_aud_out(b, cx, cy) > -2) continue;                                                       // (the centre must be inside: content of this box)
		{ float ox = fminf(tx1, b->x1) - fmaxf(tx0, b->x0), oy = fminf(ty1, b->y1) - fmaxf(ty0, b->y0), tw = fmaxf(tx1 - tx0, 1), th = fmaxf(ty1 - ty0, 1);
		  if (ox <= 0 || oy <= 0 || (ox * oy) / (tw * th) < 0.5f) continue; }                         // (mostly inside it: not a long line or band passing over)
		float d = -1e9f; for (int j = 0; j < 3; j++) d = fmaxf(d, hud_aud_out(b, hud_aud_t[j][0], hud_aud_t[j][1]));
		if (d <= 1.0f) continue;
		char key[96]; snprintf(key, sizeof(key), "%s %d %d %d %d", mu_aud_name, (int)b->x0, (int)b->y0, (int)b->x1, (int)b->y1);
		int seen = 0; for (int s = 0; s < hud_aud_nseen; s++) if (!strcmp(hud_aud_seen[s], key)) seen = 1;
		if (seen) continue; if (hud_aud_nseen < 256) snprintf(hud_aud_seen[hud_aud_nseen++], 96, "%s", key);
		if (hud_aud_nf < 256) { hud_aud_f[hud_aud_nf].box = i; hud_aud_f[hud_aud_nf].d = d; hud_aud_f[hud_aud_nf].t[0] = tx0; hud_aud_f[hud_aud_nf].t[1] = ty0; hud_aud_f[hud_aud_nf].t[2] = tx1; hud_aud_f[hud_aud_nf].t[3] = ty1; hud_aud_nf++; }
	}
}
#define HUD_AUD_V(o) hud_aud_v(o)
#else
#define HUD_AUD_V(o) ((void)0)
#endif
#ifndef HUD_TAG   // ui b42: names the HUD element drawn next (host overlap checker, tools/ui/host/hudtag.h + overlap.py); nothing on the Vita
#define HUD_TAG(n) ((void)0)
#endif
static const char *HUD_VS =
"void main(float4 pos, float4 uv, float4 mul, float4 add, float thr,"
" float4 out vpos : POSITION, float4 out t0 : TEXCOORD0, float4 out m0 : TEXCOORD1, float4 out a0 : TEXCOORD2, float out th : TEXCOORD3) {"
" vpos = pos; t0 = uv; m0 = mul; a0 = add; th = thr; }";
static const char *HUD_FS =
"float4 main(float4 t0 : TEXCOORD0, float4 m0 : TEXCOORD1, float4 a0 : TEXCOORD2, float th : TEXCOORD3, uniform sampler2D tex : TEXUNIT0) : COLOR {"
" float4 c = tex2D(tex, t0.xy); float r = tex2D(tex, t0.zw).r; c *= saturate((th - r) * 200.0 + 0.5);"
" float4 o; o.rgb = (c.rgb * m0.rgb + a0.rgb * c.a) * m0.a; o.a = c.a * m0.a; o.rgb = min(o.rgb, o.aaa); return o; }";
#ifdef GRADE_HALF
// render b39: HUD_FS in half precision, half colour varyings (coordinates and the gauge threshold's scalar ramp stay float: a half th would move
// a gauge's edge by up to 0.1 of its 1/200 ramp); hud_load tries it first (grade.h grade_half: nohalf)
static const char *HUD_FS_H =
"float4 main(float4 t0 : TEXCOORD0, half4 m0 : TEXCOORD1, half4 a0 : TEXCOORD2, float th : TEXCOORD3, uniform sampler2D tex : TEXUNIT0) : COLOR {"
" float r = tex2D(tex, t0.zw).r; half4 c = (half4)tex2D(tex, t0.xy) * (half)saturate((th - r) * 200.0 + 0.5);"
" half4 m = m0; half4 o; o.rgb = (c.rgb * m.rgb + a0.rgb * c.a) * m.a; o.a = c.a * m.a; o.rgb = min(o.rgb, o.aaa); return (float4)o; }";
static int hud_half;   // render b39: 1 = the HUD program is the half one
#endif
// render b40: the HUD split. Quads without a gauge ramp (threshold 2: ~92% of the HUD's fill) draw with a lean program with
// no ramp read (HUD_FS at threshold 2 is exactly this: the ramp factor is 1); gauge quads keep hprog. A batch holds one kind:
// hud_img flushes when the kind changes, so the draws keep their order and every quad is drawn as before. The other quad
// writers (hud_poly_add, menu_ui.h, vita_prompt.h: all threshold 2) join the batch being filled, whichever kind it is.
// ux0:data/gitsfa/nohudsplit: every quad through hprog (as b39); nohalf: both programs in float; each falls back to float.
static const char *HUD_FS_LEAN =
"float4 main(float4 t0 : TEXCOORD0, float4 m0 : TEXCOORD1, float4 a0 : TEXCOORD2, uniform sampler2D tex : TEXUNIT0) : COLOR {"
" float4 c = tex2D(tex, t0.xy); float4 o; o.rgb = (c.rgb * m0.rgb + a0.rgb * c.a) * m0.a; o.a = c.a * m0.a; o.rgb = min(o.rgb, o.aaa); return o; }";
#ifdef GRADE_HALF
static const char *HUD_FS_LEAN_H =
"float4 main(float4 t0 : TEXCOORD0, half4 m0 : TEXCOORD1, half4 a0 : TEXCOORD2, uniform sampler2D tex : TEXUNIT0) : COLOR {"
" half4 c = (half4)tex2D(tex, t0.xy); half4 m = m0; half4 o; o.rgb = (c.rgb * m.rgb + a0.rgb * c.a) * m.a; o.a = c.a * m.a; o.rgb = min(o.rgb, o.aaa); return (float4)o; }";
#endif
static GLuint hprog_lean, hud_bound; static int hud_kind, hud_lean_half;   // hud_kind: 1 = the batch being filled holds gauge quads (hprog)
#ifdef HUD_SPLIT_CHECK   // render b40, host tests only: a lean draw must hold threshold-2 vertices only
static int hud_chk_draws[2], hud_chk_bad;
__attribute__((destructor)) static void hud_chk_print(void) { fprintf(stderr, "hud split check: %d lean draws, %d gauge draws, %d lean vertices with a gauge threshold\n", hud_chk_draws[0], hud_chk_draws[1], hud_chk_bad); }
#endif
// name -> index (perf, b35: hashed per pack; the linear strcmp searches cost ~40k instructions a HUD frame). The tables are
// rebuilt whenever a pack's arrays change (load, the menus' unload / reload); linear probing in index order returns the
// first image / node / field of a name, as the linear search did.
#define HUD_HSIZE 2048
typedef struct { short img[HUD_HSIZE], node[HUD_HSIZE], field[HUD_HSIZE]; const void *pi, *pn, *pf; int ni, nn, nf; } HudHash;
static HudHash hud_hash[HUD_MAXPACK];
static unsigned hud_fnv(const char *s) { unsigned h = 2166136261u; while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; } return h; }
static void hud_hash_fill(short *t, int n, const char *names, size_t stride) {
	for (int i = 0; i < HUD_HSIZE; i++) t[i] = -1;
	for (int i = 0; i < n; i++) { unsigned h = hud_fnv(names + (size_t)i * stride) & (HUD_HSIZE - 1); while (t[h] >= 0) h = (h + 1) & (HUD_HSIZE - 1); t[h] = (short)i; }
}
static HudHash *hud_hcur(void) {
	HudHash *T = &hud_hash[HP - hud_pack];
	if (T->pi != (const void *)himg || T->ni != nhimg || T->pn != (const void *)hnode || T->nn != nhnode || T->pf != (const void *)hfield || T->nf != nhfield) {
		hud_hash_fill(T->img, nhimg, himg ? himg[0].name : NULL, sizeof(HImg)); hud_hash_fill(T->node, nhnode, hnode ? hnode[0].name : NULL, sizeof(HNode));
		hud_hash_fill(T->field, nhfield, hfield ? hfield[0].name : NULL, sizeof(HField));
		T->pi = himg; T->ni = nhimg; T->pn = hnode; T->nn = nhnode; T->pf = hfield; T->nf = nhfield; }
	return T;
}
#define HUD_HFIND(tab, arr, n, cnt) do { if ((cnt) > HUD_HSIZE * 3 / 4) { for (int i_ = 0; i_ < (cnt); i_++) if (!strcmp(arr[i_].name, n)) return i_; return -1; } \
	const short *t_ = hud_hcur()->tab; unsigned h_ = hud_fnv(n) & (HUD_HSIZE - 1); \
	while (t_[h_] >= 0) { if (!strcmp(arr[t_[h_]].name, n)) return t_[h_]; h_ = (h_ + 1) & (HUD_HSIZE - 1); } return -1; } while (0)
static int hud_find_img(const char *n) { HUD_HFIND(img, himg, n, nhimg); }
static int hud_find_node(const char *n) { HUD_HFIND(node, hnode, n, nhnode); }
static int hud_find_field(const char *n) { HUD_HFIND(field, hfield, n, nhfield); }
typedef GLuint (*hud_mkshader_fn)(GLenum, const char *);
static void hud_flush(void);
// render b43: GPU fill (~1 screen of HUD quads a frame, every pixel of a quad shaded). Most big HUD images are rings,
// arcs and frames: whole regions of them are rgba 0 0 0 0, which HUD_FS turns into 0 and the premultiplied blend leaves
// out. hud_img draws an image of 64 texels or more on a side as an 8 x 8 grid of cells and skips the cells with no
// non-zero texel within HUD_OCC_MARGIN of them (the bilinear footprint, and mip levels 0-2 of the mipmapped atlas 1),
// one quad per run of drawn cells in a row; every drawn pixel keeps its texel (the same local -> texel map) and its
// gauge ramp lookup. ux0:data/gitsfa/nohudtrim: one quad per image as before.
#define HUD_OCC_MARGIN(t) ((t) == 1 ? 8 : 2)   // (b92: page 2 is not mipmapped either)
static int hud_trim_on = -1;
static void hud_trim_atlas(int t, const unsigned char *px) {
	if (hud_trim_on < 0) { FILE *nf = fopen("ux0:data/gitsfa/nohudtrim", "rb"); hud_trim_on = nf == NULL; if (nf) fclose(nf); }
	if (!hud_trim_on || !nhimg) return;
	if (!HP->occ) { HP->occ = malloc(nhimg * sizeof(*HP->occ)); if (!HP->occ) return; for (int i = 0; i < nhimg; i++) HP->occ[i] = ~0ull; }
	int W = htw[t], Hh = hth[t], m = HUD_OCC_MARGIN(t);
	for (int i = 0; i < nhimg; i++) {
		const HImg *I = &himg[i]; if (I->tex != t || I->w < 64 || I->h < 64 || I->x + I->w > W || I->y + I->h > Hh) continue;
		unsigned long long mask = 0;
		for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) {
			int y0 = r * I->h / 8 - m, y1 = (r + 1) * I->h / 8 + m, x0 = c * I->w / 8 - m, x1 = (c + 1) * I->w / 8 + m, hit = 0;
			y0 = y0 < 0 ? 0 : y0; x0 = x0 < 0 ? 0 : x0; y1 = y1 > I->h ? I->h : y1; x1 = x1 > I->w ? I->w : x1;
			for (int y = y0; y < y1 && !hit; y++) { const unsigned *row = (const unsigned *)(px + ((size_t)(I->y + y) * W + I->x) * 4);
				for (int x = x0; x < x1; x++) if (row[x]) { hit = 1; break; } }
			if (hit) mask |= 1ull << (r * 8 + c);
		}
		HP->occ[i] = mask;
	}
}
static void hud_use(int pack) { if (pack >= 0 && pack < hud_npack && HP != &hud_pack[pack]) { hud_flush(); HP = &hud_pack[pack]; } }
// loads a .ghud into a new pack and makes it current; returns the pack index (<0 on error)
#include "glz.h"   // perf b56: packed HUD packs
static int hud_load(const char *path, hud_mkshader_fn mk) {
	if (hud_npack >= HUD_MAXPACK) return -9;
#ifdef LDRA_B   // perf (b45): ldbg.h's read-ahead (a reader thread reads the pack while this thread uploads); else stdio
	FILE *f = ldra_fopen(path); if (!f) f = glz_fopen(path); if (!f) return -1;   // (perf b56: glz.h: packed HUD packs)
#else
	FILE *f = glz_fopen(path); if (!f) return -1;
#endif
	HP = &hud_pack[hud_npack]; memset(HP, 0, sizeof(*HP));
	char mg[4]; unsigned ver, d[4], n; if (fread(mg, 1, 4, f) != 4 || memcmp(mg, "GHUD", 4) || fread(&ver, 4, 1, f) != 1) { fclose(f); return -2; }
	if (fread(d, 4, 4, f) != 4) { fclose(f); return -3; } htw[0] = d[0]; hth[0] = d[1]; htw[1] = d[2]; hth[1] = d[3];
	int ntex = 2; if (ver >= 2) { unsigned d2[2]; if (fread(d2, 4, 2, f) != 2) { fclose(f); return -3; } htw[2] = d2[0]; hth[2] = d2[1]; ntex = 3; }   // b92: version 2 = a third page
	fread(&n, 4, 1, f); nhimg = n; himg = malloc(sizeof(HImg) * n);
	for (unsigned i = 0; i < n; i++) { fread(himg[i].name, 1, 32, f); fread(&himg[i].tex, 1, 4, f); fread(&himg[i].x, 2, 4, f); fread(himg[i].loc, 4, 4, f); }
	fread(&n, 4, 1, f); nhnode = n; hnode = malloc(sizeof(HNode) * n);
	for (unsigned i = 0; i < n; i++) { fread(hnode[i].name, 1, 32, f); fread(hnode[i].H, 4, 9, f); fread(hnode[i].mul, 4, 4, f); fread(hnode[i].add, 4, 4, f); }
	fread(&n, 4, 1, f); nhfield = n; hfield = malloc(sizeof(HField) * (n ? n : 1));
	for (unsigned i = 0; i < n; i++) { fread(hfield[i].name, 1, 32, f); fread(&hfield[i].node, 4, 1, f); fread(hfield[i].b, 4, 4, f); fread(&hfield[i].size, 4, 1, f); fread(&hfield[i].align, 4, 1, f); fread(hfield[i].col, 1, 4, f); }
	fread(&n, 4, 1, f); fread(&hasc, 4, 1, f); fread(&hdesc, 4, 1, f);
	for (int i = 0; i < 256; i++) { hglyph[i] = -2; hadv[i] = 0.5f; }
	for (unsigned i = 0; i < n; i++) { unsigned short code; short ix; float adv; fread(&code, 2, 1, f); fread(&ix, 2, 1, f); fread(&adv, 4, 1, f); if (code < 256) { hglyph[code] = ix; hadv[code] = adv; } }
	for (int t = 0; t < ntex; t++) {
		size_t sz = (size_t)htw[t] * hth[t] * 4; unsigned char *px = malloc(sz);
		if (!px || fread(px, 1, sz, f) != sz) { free(px); fclose(f); return -4; }
		hud_trim_atlas(t, px);   // render b43
		glGenTextures(1, &htex[t]); glBindTexture(GL_TEXTURE_2D, htex[t]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, htw[t], hth[t], 0, GL_RGBA, GL_UNSIGNED_BYTE, px); VGL_XFER_WAIT();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if (t == 1) { glGenerateMipmap(GL_TEXTURE_2D); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); }
		else glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		free(px);
	}
	fclose(f);
	int pack = hud_npack++;
	if (hprog) return pack;
	GLuint vs = mk(GL_VERTEX_SHADER, HUD_VS), fs = 0; GLint ok = 0;
#ifdef GRADE_HALF
	if (grade_half()) { fs = mk(GL_FRAGMENT_SHADER, HUD_FS_H); glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); hud_half = ok; }   // render b39: half first
#endif
	if (!ok) fs = mk(GL_FRAGMENT_SHADER, HUD_FS);
	glGetShaderiv(vs, GL_COMPILE_STATUS, &ok); if (!ok) return -5; glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); if (!ok) return -6;
	hprog = glCreateProgram(); glAttachShader(hprog, vs); glAttachShader(hprog, fs);
	glBindAttribLocation(hprog, 0, "pos"); glBindAttribLocation(hprog, 1, "uv"); glBindAttribLocation(hprog, 2, "mul"); glBindAttribLocation(hprog, 3, "add"); glBindAttribLocation(hprog, 4, "thr");
	glLinkProgram(hprog); glGetProgramiv(hprog, GL_LINK_STATUS, &ok); if (!ok) return -7;
	glUseProgram(hprog); glUniform1i(glGetUniformLocation(hprog, "tex"), 0);
	{ FILE *nf = fopen("ux0:data/gitsfa/nohudsplit", "rb"); if (nf) fclose(nf); else {   // render b40: the lean program (HUD_VS shared)
		GLuint lvs = mk(GL_VERTEX_SHADER, HUD_VS), lfs = 0; GLint lok = 0;
#ifdef GRADE_HALF
		if (grade_half()) { lfs = mk(GL_FRAGMENT_SHADER, HUD_FS_LEAN_H); glGetShaderiv(lfs, GL_COMPILE_STATUS, &lok); hud_lean_half = lok; }
#endif
		if (!lok) { lfs = mk(GL_FRAGMENT_SHADER, HUD_FS_LEAN); glGetShaderiv(lfs, GL_COMPILE_STATUS, &lok); }
		if (lok) glGetShaderiv(lvs, GL_COMPILE_STATUS, &lok);
		if (lok) { GLuint p = glCreateProgram(); glAttachShader(p, lvs); glAttachShader(p, lfs);
			glBindAttribLocation(p, 0, "pos"); glBindAttribLocation(p, 1, "uv"); glBindAttribLocation(p, 2, "mul"); glBindAttribLocation(p, 3, "add"); glBindAttribLocation(p, 4, "thr");
			glLinkProgram(p); glGetProgramiv(p, GL_LINK_STATUS, &lok);
			if (lok) { glUseProgram(p); glUniform1i(glGetUniformLocation(p, "tex"), 0); hprog_lean = p; glUseProgram(hprog); } } } }
	hud_ok = 1; return pack;
}
static void hud_flush(void) {
	if (!hvn) return;
	{ GLuint want = hud_kind || !hprog_lean ? hprog : hprog_lean; if (want != hud_bound) { glUseProgram(want); hud_bound = want; } }   // render b40
#ifdef HUD_SPLIT_CHECK
	{ int lean = !hud_kind && hprog_lean; hud_chk_draws[!lean]++; if (lean) for (int i = 0; i < hvn; i++) hud_chk_bad += hvb[i * HUD_VF + 16] < 1.01f; }
#endif
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hcur);
	for (int a = 0; a < 5; a++) glEnableVertexAttribArray(a);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, HUD_VF * 4, hvb); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, HUD_VF * 4, hvb + 4);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, HUD_VF * 4, hvb + 8); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, HUD_VF * 4, hvb + 12);
	glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, HUD_VF * 4, hvb + 16);
	glDrawArrays(GL_TRIANGLES, 0, hvn); hvn = 0;
	glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
}
static void hud_begin(void) {
	if (!hud_ok) return;
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(hprog); hvn = 0; hcur = 0; hud_dx = hud_dy = 0;
	hud_bound = hprog; hud_kind = 0;   // render b40
}
static void hud_end(void) {
	if (!hud_ok) return;
	hud_flush(); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
}
static const float HUD_W1[4] = { 1, 1, 1, 1 }, HUD_Z4[4] = { 0, 0, 0, 0 };
// one quad of image I in node N's plane: corners lx / ly (node-local, before A), texels u / v (render b43: hud_img's tail)
static float hud_clipx = -1e9f;   // b109: upright quads are cut off left of this screen x (the TOP OPERATIVE title's lines)
static void hud_quad(const HImg *I, const HNode *N, const float *A, const float *mul, const float *add, int ramp, float thr, const float *lx, const float *ly, const float *u, const float *v) {
	float tw = (float)htw[I->tex], th_ = (float)hth[I->tex];
	float ru[4], rv[4];
	for (int k = 0; k < 4; k++) {
		if (ramp >= 0) { const HImg *R = &himg[ramp];
			ru[k] = (R->x + (lx[k] - R->loc[0]) / (R->loc[2] - R->loc[0]) * R->w) / tw; rv[k] = (R->y + (ly[k] - R->loc[1]) / (R->loc[3] - R->loc[1]) * R->h) / th_; }
		else { ru[k] = 2.0f / tw; rv[k] = 2.0f / th_; }
	}
	float th2 = ramp >= 0 ? thr : 2.0f, P[4][4];
	for (int k = 0; k < 4; k++) {
		float x = lx[k], y = ly[k];
		if (A) { float x2 = A[0] * x + A[1] * y + A[2], y2 = A[3] * x + A[4] * y + A[5]; x = x2; y = y2; }
		const float *H = N->H; float X = H[0] * x + H[1] * y + H[2], Y = H[3] * x + H[4] * y + H[5], W = H[6] * x + H[7] * y + H[8];
		X += hud_dx * W; Y += hud_dy * W;
		P[k][0] = X / 480.0f - W; P[k][1] = W - Y / 272.0f; P[k][2] = 0; P[k][3] = W;
	}
	float uu[4], vv[4]; memcpy(uu, u, sizeof(uu)); memcpy(vv, v, sizeof(vv));
	if (hud_clipx > -1e8f) { float sx[4]; for (int k = 0; k < 4; k++) sx[k] = (P[k][0] / P[k][3] + 1) * 480.0f;
		if (fabsf(sx[0] - sx[3]) < 1 && fabsf(sx[1] - sx[2]) < 1 && sx[1] > sx[0]) {
			if (sx[1] <= hud_clipx) return;
			if (sx[0] < hud_clipx) { const float f = (hud_clipx - sx[0]) / (sx[1] - sx[0]); static const int L[2] = { 0, 3 }, Rr[2] = { 1, 2 };
				for (int q = 0; q < 2; q++) { const int a = L[q], b = Rr[q]; for (int c = 0; c < 4; c++) P[a][c] += (P[b][c] - P[a][c]) * f;
					uu[a] += (uu[b] - uu[a]) * f; vv[a] += (vv[b] - vv[a]) * f; ru[a] += (ru[b] - ru[a]) * f; rv[a] += (rv[b] - rv[a]) * f; } } } }
	static const int tri[6] = { 0, 1, 2, 0, 2, 3 };
	for (int t = 0; t < 6; t++) { int k = tri[t]; float *o = &hvb[hvn * HUD_VF];
		o[0] = P[k][0]; o[1] = P[k][1]; o[2] = 0; o[3] = P[k][3]; o[4] = uu[k]; o[5] = vv[k]; o[6] = ru[k]; o[7] = rv[k];
		memcpy(o + 8, mul, 16); memcpy(o + 12, add, 16); o[16] = th2; HUD_AUD_V(o); hvn++; }
}
// image im in the plane of node nd; A (2x3 row-major, may be NULL) maps image-local -> node-local; ramp/thr for gauges
static void hud_img(int im, int nd, const float *A, const float *mul, const float *add, int ramp, float thr) {
	if (!hud_ok || im < 0 || nd < 0) return;
	const HImg *I = &himg[im]; const HNode *N = &hnode[nd];
	if (hprog_lean && (ramp >= 0) != hud_kind) { hud_flush(); hud_kind = ramp >= 0; }   // render b40: a batch holds one program's quads
	if (htex[I->tex] != hcur) { hud_flush(); hcur = htex[I->tex]; }
	if (hvn + 6 > HUD_MAXQ * 6) hud_flush();
	mul = mul ? mul : N->mul; add = add ? add : N->add;
	float tw = (float)htw[I->tex], th_ = (float)hth[I->tex];
	const float *l = I->loc; float lx[4] = { l[0], l[2], l[2], l[0] }, ly[4] = { l[1], l[1], l[3], l[3] };
	float u[4] = { I->x / tw, (I->x + I->w) / tw, (I->x + I->w) / tw, I->x / tw }, v[4] = { I->y / th_, I->y / th_, (I->y + I->h) / th_, (I->y + I->h) / th_ };
	unsigned long long M = HP->occ ? HP->occ[im] : ~0ull;   // render b43: the drawn cells (hud_trim_atlas)
	if (!M) return;
	if (M == ~0ull) { hud_quad(I, N, A, mul, add, ramp, thr, lx, ly, u, v); return; }
	for (int r = 0; r < 8; r++) for (int c0 = 0; c0 < 8; ) {
		if (!(M >> (r * 8 + c0) & 1)) { c0++; continue; }
		int c1 = c0; while (c1 + 1 < 8 && (M >> (r * 8 + c1 + 1) & 1)) c1++;
		int tx0 = c0 * I->w / 8, tx1 = (c1 + 1) * I->w / 8, ty0 = r * I->h / 8, ty1 = (r + 1) * I->h / 8;
		float fa = (float)tx0 / I->w, fb = (float)tx1 / I->w, fc = (float)ty0 / I->h, fd = (float)ty1 / I->h;
		float X0 = l[0] + (l[2] - l[0]) * fa, X1 = l[0] + (l[2] - l[0]) * fb, Y0 = l[1] + (l[3] - l[1]) * fc, Y1 = l[1] + (l[3] - l[1]) * fd;
		float U0 = (I->x + tx0) / tw, U1 = (I->x + tx1) / tw, V0 = (I->y + ty0) / th_, V1 = (I->y + ty1) / th_;
		float qx[4] = { X0, X1, X1, X0 }, qy[4] = { Y0, Y0, Y1, Y1 }, qu[4] = { U0, U1, U1, U0 }, qv[4] = { V0, V0, V1, V1 };
		if (hvn + 6 > HUD_MAXQ * 6) hud_flush();
		hud_quad(I, N, A, mul, add, ramp, thr, qx, qy, qu, qv);
		c0 = c1 + 1;
	}
}
// a convex (or star-shaped from vertex 0) polygon (fan) in node nd's plane with its own texture (e.g. the minimap image, or part
// of an atlas image); uv per vertex; add: an optional colour add (Flash colour transform), as hud_img's
static void hud_poly_add(GLuint tex, int nd, const float (*xy)[2], const float (*uv)[2], int n, const float *mul, const float *add) {
	if (!hud_ok || nd < 0 || n < 3) return;
	if (tex != hcur) { hud_flush(); hcur = tex; }
	if (hvn + (n - 2) * 3 > HUD_MAXQ * 6) hud_flush();
	const HNode *N = &hnode[nd]; const float *H = N->H; float P[16][4]; if (n > 16) n = 16;
	for (int k = 0; k < n; k++) { float x = xy[k][0], y = xy[k][1], X = H[0] * x + H[1] * y + H[2], Y = H[3] * x + H[4] * y + H[5], W = H[6] * x + H[7] * y + H[8];
		X += hud_dx * W; Y += hud_dy * W; P[k][0] = X / 480.0f - W; P[k][1] = W - Y / 272.0f; P[k][2] = 0; P[k][3] = W; }
	for (int t = 1; t + 1 < n; t++) { int ix[3] = { 0, t, t + 1 };
		for (int j = 0; j < 3; j++) { int k = ix[j]; float *o = &hvb[hvn * HUD_VF];
			o[0] = P[k][0]; o[1] = P[k][1]; o[2] = 0; o[3] = P[k][3]; o[4] = uv[k][0]; o[5] = uv[k][1]; o[6] = uv[k][0]; o[7] = uv[k][1];
			memcpy(o + 8, mul ? mul : HUD_W1, 16); if (add) memcpy(o + 12, add, 16); else memset(o + 12, 0, 16); o[16] = 2.0f; HUD_AUD_V(o); hvn++; } }
}
static void hud_poly(GLuint tex, int nd, const float (*xy)[2], const float (*uv)[2], int n, const float *mul) { hud_poly_add(tex, nd, xy, uv, n, mul, NULL); }
// width of a string in em units
static float hud_text_w(const char *s) { float w = 0; for (; *s; s++) w += hadv[(unsigned char)*s]; return w; }
// text in node nd's plane: pen at (x, baseline y), size = em in node units; align 0 left, 1 right, 2 centre
// b49: hud_text_sx < 1 draws the glyphs narrower (same height): a label that does not fit its box even at fine print is
// condensed rather than overflowing it (menu_ui.h mu_textbox sets it for one call)
static float hud_text_sx = 1.0f;
static void hud_text(int nd, float x, float y, float size, int align, const float *mul, const float *add, const char *s) {
	float sx = size * hud_text_sx, w = hud_text_w(s) * sx;
	if (align == 1) x -= w; else if (align == 2) x -= w / 2;
	for (; *s; s++) { int c = (unsigned char)*s; int g = hglyph[c];
		if (g >= 0) { float A[6] = { sx, 0, x, 0, size, y }; hud_img(g, nd, A, mul ? mul : HUD_W1, add ? add : HUD_Z4, -1, 1); }
		x += hadv[c] * sx; }
}
// a dynamic text field from the SWF (its bounds, size, alignment and colour)
static void hud_field(int fi, const char *s, const float *mul) {
	if (fi < 0) return; const HField *F = &hfield[fi]; float col[4] = { F->col[0] / 255.f, F->col[1] / 255.f, F->col[2] / 255.f, F->col[3] / 255.f };
	if (mul) for (int k = 0; k < 4; k++) col[k] *= mul[k];
	float w = hud_text_w(s) * F->size, x = F->align == 0 ? F->b[0] + 2 : F->align == 1 ? F->b[1] - 2 - w : (F->b[0] + F->b[1]) / 2 - w / 2;
	hud_text(F->node, x, F->b[2] + 2 + hasc * F->size, F->size, 0, col, HUD_Z4, s);
}
// b55: a field condensed (glyphs narrower, height kept, down to 72 %) so its text stays inside its own box ("TIER 1" ran
// out of the small skill ring)
static void hud_field_fit(int fi, const char *s, const float *mul, float maxw) {   // maxw > 0: at most this wide (screen px)
	if (fi < 0) return; const HField *F = &hfield[fi]; float room = F->b[1] - F->b[0] - 4; if (maxw > 0 && maxw < room) room = maxw; float w = hud_text_w(s) * F->size;
	float sx = w > room && w > 0 ? fmaxf(0.72f, room / w) : 1; hud_text_sx = sx; w *= sx;
	float col[4] = { F->col[0] / 255.f, F->col[1] / 255.f, F->col[2] / 255.f, F->col[3] / 255.f }; if (mul) for (int k = 0; k < 4; k++) col[k] *= mul[k];
	float x = F->align == 0 ? F->b[0] + 2 : F->align == 1 ? F->b[1] - 2 - w : (F->b[0] + F->b[1]) / 2 - w / 2;
	hud_text(F->node, x, F->b[2] + 2 + hasc * F->size, F->size, 0, col, HUD_Z4, s); hud_text_sx = 1;
}
// screen position (Vita pixels) of a node-local point
static void hud_node_screen(int nd, float x, float y, float *sx, float *sy) {
	const float *H = hnode[nd].H; float W = H[6] * x + H[7] * y + H[8]; *sx = (H[0] * x + H[1] * y + H[2]) / W; *sy = (H[3] * x + H[4] * y + H[5]) / W;
}
// ---- ui b42 (HUD de-clutter): claims. The transient elements that share the screen - banners, notices, timer warnings, the kill
// notify, TIER 2 SKILL READY, the commander box - claim the screen rectangle they draw each frame, with a priority; one of lower
// priority places itself clear of the higher ones (hud_place: the nearest free spot up or down, else hidden), and the kill feed's
// rows / the terminal letters hide while a claim covers them (hud_blocked). Claims are read one frame late (the previous frame's,
// plus the same priority's made earlier in this frame), so the draw order does not matter (and every claimant fades in from 0).
// main.c calls hud_claims_frame() once a frame before the HUD. Priorities: HUD_P_*.
enum { HUD_P_FEED, HUD_P_NOTIFY, HUD_P_SKILL, HUD_P_NOTICE, HUD_P_BANNER, HUD_P_RESULT, HUD_P_END };
#define HUD_NCLAIM 24
#define HUD_GAP 7.0f       // the space kept between claimed elements (px)
#define HUD_HIDE 1e9f      // hud_place: no free spot
typedef struct { float x0, y0, x1, y1; int prio; } HudClaim;
static HudClaim hud_cl[2][HUD_NCLAIM]; static int hud_ncl[2], hud_clf;
static void hud_claims_frame(void) { hud_clf ^= 1; hud_ncl[hud_clf] = 0; }
static void hud_claim(float x0, float y0, float x1, float y1, int prio) {
	if (hud_ncl[hud_clf] < HUD_NCLAIM) { HudClaim *c = &hud_cl[hud_clf][hud_ncl[hud_clf]++]; c->x0 = x0; c->y0 = y0; c->x1 = x1; c->y1 = y1; c->prio = prio; } }
// the claims a box of priority prio must keep clear of: last frame's of a higher priority, this frame's of the same or a higher one so far
static int hud_claims_for(int prio, const HudClaim **out) {
	int n = 0; for (int i = 0; i < hud_ncl[hud_clf ^ 1]; i++) if (hud_cl[hud_clf ^ 1][i].prio > prio) out[n++] = &hud_cl[hud_clf ^ 1][i];
	for (int i = 0; i < hud_ncl[hud_clf] && n < 2 * HUD_NCLAIM; i++) if (hud_cl[hud_clf][i].prio >= prio) out[n++] = &hud_cl[hud_clf][i];
	return n;
}
static int hud_hits(const HudClaim *const *c, int n, float x0, float y0, float x1, float y1) {
	for (int i = 0; i < n; i++) if (x0 < c[i]->x1 + HUD_GAP && x1 > c[i]->x0 - HUD_GAP && y0 < c[i]->y1 + HUD_GAP && y1 > c[i]->y0 - HUD_GAP) return 1;
	return 0;
}
static int hud_blocked(float x0, float y0, float x1, float y1, int prio) { const HudClaim *c[2 * HUD_NCLAIM]; int n = hud_claims_for(prio, c); return hud_hits(c, n, x0, y0, x1, y1); }
// the vertical move (px) that puts the box clear of the claims it must avoid, inside ymin..ymax: 0 when it is already clear, else
// the smallest move to just below / above one of them that is clear of all; HUD_HIDE when there is none
static float hud_place(float x0, float y0, float x1, float y1, int prio, float ymin, float ymax) {
	const HudClaim *c[2 * HUD_NCLAIM]; int n = hud_claims_for(prio, c);
	if (!hud_hits(c, n, x0, y0, x1, y1)) return 0;
	float best = HUD_HIDE;
	for (int i = 0; i < n; i++) { if (x0 >= c[i]->x1 + HUD_GAP || x1 <= c[i]->x0 - HUD_GAP) continue;
		float d[2] = { c[i]->y1 + HUD_GAP - y0, c[i]->y0 - HUD_GAP - y1 };
		for (int k = 0; k < 2; k++) if (fabsf(d[k]) < fabsf(best) && y0 + d[k] >= ymin && y1 + d[k] <= ymax && !hud_hits(c, n, x0, y0 + d[k], x1, y1 + d[k])) best = d[k]; }
	return best;
}
// the screen box a capital-letter / digit string covers in a text field (hud_field's layout; the field's plane may be tilted)
static void hud_field_box(int fi, const char *s, float *x0, float *y0, float *x1, float *y1) {
	const HField *F = &hfield[fi]; float w = hud_text_w(s) * F->size, x = F->align == 0 ? F->b[0] + 2 : F->align == 1 ? F->b[1] - 2 - w : (F->b[0] + F->b[1]) / 2 - w / 2;
	float ty = F->b[2] + 2 + (hasc - 0.76f) * F->size, by = F->b[2] + 2 + (hasc + 0.06f) * F->size, X[4], Y[4];
	hud_node_screen(F->node, x, ty, &X[0], &Y[0]); hud_node_screen(F->node, x + w, ty, &X[1], &Y[1]); hud_node_screen(F->node, x, by, &X[2], &Y[2]); hud_node_screen(F->node, x + w, by, &X[3], &Y[3]);
	*x0 = fminf(fminf(X[0], X[1]), fminf(X[2], X[3])); *x1 = fmaxf(fmaxf(X[0], X[1]), fmaxf(X[2], X[3])); *y0 = fminf(fminf(Y[0], Y[1]), fminf(Y[2], Y[3])); *y1 = fmaxf(fmaxf(Y[0], Y[1]), fmaxf(Y[2], Y[3]));
}
// digits of v into slot nodes base[0] = ones, base[1] = tens ... (n slots); left: start in the leftmost slot
static void hud_number(const int *slots, int n, int glyph0, int v, int left, const float *mul, const float *add, const int *zslots, int zimg) {
	char t[16]; int len = snprintf(t, sizeof(t), "%d", v < 0 ? 0 : v); if (len > n) len = n;
	if (left) { for (int j = 0; j < len; j++) hud_img(glyph0 + (t[j] - '0'), slots[n - 1 - j], NULL, mul, add, -1, 1); return; }
	for (int i = 0; i < n; i++) {
		if (i < len) hud_img(glyph0 + (t[len - 1 - i] - '0'), slots[i], NULL, mul, add, -1, 1);
		else if (zslots && zslots[i] >= 0) hud_img(zimg, zslots[i], NULL, NULL, NULL, -1, 1);
	}
}
