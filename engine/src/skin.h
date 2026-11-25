// skin.h - CPU skinning for every skinned model (chars, build 35): load-time skin plans, 4-vertex NEON kernels, and a
// per-frame queue that runs on jobs.h's worker cores. Included at the end of model.h.
//
// Why: b31 measured 0.31-0.35 us per vertex on the device (chr.h's old per-vertex NEON loop: each vertex blends its
// bone matrices and runs three dependent multiply-add chains, and the Cortex-A9's in-order NEON pipe waits on every
// chain; three partial stores per vertex into uncached memory). A skin plan regroups a mesh's vertices at load time so
// that every block of 4 vertices shares its bones (one bone: 72-87% of the vertices; two bones: most of the rest), in
// structure-of-arrays form: one NEON lane per vertex, the bone matrix in registers, 6 independent chains per block and
// 64 contiguous bytes stored per 4 vertices (whole 16-byte stores, no reads). Blocks are cut into job items and skinned on all cores.
//
// Output: 16-byte vertices (position f32 x3, normal s8 x3, the source's 4th normal byte: b62 specular mask) in the plan's slot order: the mesh's vertices regrouped,
// each group padded to 4 (padding slots are written but never indexed). Draw them with the plan's own index buffer
// (ibo, the mesh's triangles in slot numbers) and static buffer (st: uv + bone 0 / weight 255 per slot), with model.h's
// shader and a one-entry identity palette (skin_bind_out / skin_draw_elems).
//
// Frame flow (main thread): skin_add() maps the instance's output buffer and queues its blocks (palette copied);
// skin_run() skins everything queued on the workers + the caller (jobs_parallel_for), then unmaps; then draw.
// Job items only read the plan / palette and write their own blocks of the mapped buffer (no GL, no logging).
#pragma once
#include <stdint.h>
#include "jobs.h"
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(CHR_NO_NEON)
#include <arm_neon.h>
#define SKIN_NEON 1
#endif

struct SkinPlan {
	int nv, ni, nb1, nb2, nb3, nbx, nblk;   // source vertices / indices; 4-vertex blocks: nb1 one bone, nb2 two, nb3 three or four, nbx other (in that order)
	unsigned char *bone;               // 4 per block: the bones (palette entries relative to the mesh's window), 255 = none
	float *xyz;                        // (nb1 + nb2 + nb3) x 12 floats: x[4] y[4] z[4] (16-byte aligned)
	short *nrm;                        // (nb1 + nb2 + nb3) x 12: nx[4] ny[4] nz[4], the s8 normals x 256
	short *nw;                         // b62: (nb1 + nb2 + nb3) x 4: the normals' 4th byte (model.h's specular mask), per lane
	float *w2;                         // nb2 x 4: weight of the block's first bone (the second one gets 1 - w)
	float *w4;                         // nb3 x 16: w[bone][lane]
	unsigned char *aos;                // nbx x 4 x 32: source vertices of no class above (no weight; weights not summing to 255)
	unsigned short *slot;              // source vertex -> output slot
	GLuint ibo, st;                    // the triangles in slot numbers; static uv + bone 0 / weight 255 (16 bytes per slot)
	void *mem;                         // (one allocation for the arrays above)
};
#define SKIN_OUT_BYTES(p) ((p)->nblk * 64)   // the output buffer of one instance of one mesh

// ---- load time
typedef struct { unsigned key; int v, cls; float w[4]; } SkSort;
static int sk_sort_cmp(const void *a, const void *b) { const SkSort *x = a, *y = b; return x->cls != y->cls ? x->cls - y->cls : x->key != y->key ? (x->key < y->key ? -1 : 1) : x->v - y->v; }
// perf (b46): the order qsort with sk_sort_cmp gives (class, bone set, then vertex index), in linear time: the vertices are
// grouped by (class, bone set) with a small hash table, the few groups sorted, and each vertex placed after the ones of its
// group before it (S comes in vertex order, so the index order inside a group is kept). The start-up's skin plans were
// ~110 ms on Vita3K, most of it this sort (nv log nv comparisons through a function pointer per mesh).
typedef struct { unsigned key; int cls, n, at; } SkGroup;
static int sk_group_cmp(const void *a, const void *b) { const SkGroup *x = a, *y = b; return x->cls != y->cls ? x->cls - y->cls : x->key != y->key ? (x->key < y->key ? -1 : 1) : 0; }
static int sk_sort_groups(SkSort **pS, int nv) {
	SkSort *S = *pS; int hn = 64; while (hn < 2 * nv) hn *= 2;
	int *ht = malloc(sizeof(int) * hn), *gid = malloc(sizeof(int) * (nv + 1)), *rank = NULL; SkGroup *G = malloc(sizeof(SkGroup) * (nv + 1)); SkSort *T = malloc(sizeof(SkSort) * (nv + 1));
	if (!ht || !gid || !G || !T) { free(ht); free(gid); free(G); free(T); return -1; }
	for (int i = 0; i < hn; i++) ht[i] = -1;
	int ng = 0;
	for (int i = 0; i < nv; i++) {
		unsigned h = (S[i].key * 2654435761u ^ (unsigned)S[i].cls * 0x9E3779B9u) & (unsigned)(hn - 1);
		while (ht[h] >= 0 && (G[ht[h]].key != S[i].key || G[ht[h]].cls != S[i].cls)) h = (h + 1) & (unsigned)(hn - 1);
		if (ht[h] < 0) { ht[h] = ng; G[ng].key = S[i].key; G[ng].cls = S[i].cls; G[ng].n = 0; G[ng].at = ng; ng++; }
		gid[i] = ht[h]; G[ht[h]].n++;
	}
	rank = ht;   // (the table is done with: group id -> its start in sorted order)
	qsort(G, ng, sizeof(SkGroup), sk_group_cmp);
	int o = 0; for (int g = 0; g < ng; g++) { rank[G[g].at] = o; o += G[g].n; }
	for (int i = 0; i < nv; i++) T[rank[gid[i]]++] = S[i];
	free(ht); free(gid); free(G); free(S); *pS = T; return 0;
}
// v: nv x 32-byte source vertices (model.h layout), ix: the mesh's triangles. NULL when out of memory / too many slots.
// Vertices are sorted by their set of bones (class: 1, 2, 3-4 bones, other), each run of one set padded to a multiple of 4
// with copies of its last vertex (the padding slots are skinned but never indexed: 2-6% more slots).
static struct SkinPlan *skin_plan_build(const unsigned char *v, int nv, const unsigned short *ix, int ni) {
	SkSort *S = malloc(sizeof(SkSort) * (nv + 1)); if (!S) return NULL;
	for (int i = 0; i < nv; i++) {
		const unsigned char *q = v + i * 32; int bo[4], wo[4], n = 0, sum = 0;
		for (int j = 0; j < 4; j++) { if (!q[28 + j]) continue; int k = 0; while (k < n && bo[k] != q[24 + j]) k++; if (k == n) { bo[n] = q[24 + j]; wo[n] = 0; n++; } wo[k] += q[28 + j]; sum += q[28 + j]; }
		for (int a = 1; a < n; a++) for (int c = a; c > 0 && bo[c] < bo[c - 1]; c--) { int t = bo[c]; bo[c] = bo[c - 1]; bo[c - 1] = t; t = wo[c]; wo[c] = wo[c - 1]; wo[c - 1] = t; }   // bones in order
		SkSort *e = &S[i]; e->v = i; e->key = 0xFFFFFFFFu; memset(e->w, 0, sizeof(e->w));
		for (int k = 0; k < n; k++) { e->key = (e->key << 8) | (unsigned)bo[k]; e->w[k] = wo[k] * (1.0f / 255); }
		e->cls = n == 1 && sum == 255 ? 1 : n == 2 && sum == 255 ? 2 : n >= 3 ? 3 : 4;
	}
	if (sk_sort_groups(&S, nv) < 0) qsort(S, nv, sizeof(SkSort), sk_sort_cmp);   // perf (b46): linear; qsort if out of memory
	int nb[5] = { 0 };
	for (int i = 0; i < nv; ) { int j = i; while (j < nv && S[j].cls == S[i].cls && (S[i].cls == 4 || S[j].key == S[i].key)) j++; nb[S[i].cls] += (j - i + 3) / 4; i = j; }
	int nsoa = nb[1] + nb[2] + nb[3], nblk = nsoa + nb[4];
	if (nblk * 4 > 65535) { free(S); return NULL; }
	size_t o_xyz = 0, o_nrm = (size_t)nsoa * 48, o_w2 = o_nrm + (size_t)nsoa * 24, o_w4 = o_w2 + (size_t)nb[2] * 16, o_aos = o_w4 + (size_t)nb[3] * 64,
		o_bone = o_aos + (size_t)nb[4] * 128, o_slot = o_bone + (size_t)nblk * 4, o_nw = (o_slot + (size_t)nv * 2 + 7) & ~(size_t)7, total = o_nw + (size_t)nsoa * 8 + 16;
	struct SkinPlan *p = calloc(1, sizeof(*p)); unsigned char *mem = malloc(total + 15), *st = calloc((size_t)nblk * 4 + 1, 16);
	if (!p || !mem || !st) { free(p); free(mem); free(st); free(S); return NULL; }
	unsigned char *base = (unsigned char *)(((uintptr_t)mem + 15) & ~(uintptr_t)15);
	p->mem = mem; p->xyz = (float *)(base + o_xyz); p->nrm = (short *)(base + o_nrm); p->w2 = (float *)(base + o_w2); p->w4 = (float *)(base + o_w4);
	p->aos = base + o_aos; p->bone = base + o_bone; p->slot = (unsigned short *)(base + o_slot); p->nw = (short *)(base + o_nw);
	p->nv = nv; p->ni = ni; p->nb1 = nb[1]; p->nb2 = nb[2]; p->nb3 = nb[3]; p->nbx = nb[4]; p->nblk = nblk;
	int b = 0;
	for (int i = 0; i < nv; ) {
		int j = i; while (j < nv && S[j].cls == S[i].cls && (S[i].cls == 4 || S[j].key == S[i].key)) j++;
		for (int k = i; k < j; k += 4, b++) {
			int cls = S[i].cls; unsigned key = S[i].key;
			for (int t = 0; t < 4; t++) p->bone[b * 4 + t] = 255;
			if (cls < 4) { int nbo = cls == 3 ? (key >> 24 == 0xFF ? 3 : 4) : cls; for (int t = 0; t < nbo; t++) p->bone[b * 4 + t] = (unsigned char)(key >> (8 * (nbo - 1 - t))); }
			for (int l = 0; l < 4; l++) {
				const SkSort *e = &S[k + l < j ? k + l : j - 1]; const unsigned char *q = v + e->v * 32;
				if (k + l < j) p->slot[e->v] = (unsigned short)(b * 4 + l);
				memcpy(st + (b * 4 + l) * 16, q + 16, 8); st[(b * 4 + l) * 16 + 12] = 255;
				if (cls == 4) { memcpy(p->aos + ((b - nsoa) * 4 + l) * 32, q, 32); continue; }
				float pp[3]; memcpy(pp, q, 12); float *X = p->xyz + b * 12; short *N = p->nrm + b * 12;
				X[l] = pp[0]; X[4 + l] = pp[1]; X[8 + l] = pp[2];
				for (int c = 0; c < 3; c++) N[c * 4 + l] = (short)((signed char)q[12 + c] * 256);
				p->nw[b * 4 + l] = (short)(signed char)q[15];   // (b62)
				if (cls == 2) p->w2[(b - nb[1]) * 4 + l] = e->w[0];
				if (cls == 3) for (int t = 0; t < 4; t++) p->w4[(b - nb[1] - nb[2]) * 16 + t * 4 + l] = e->w[t];
			}
		}
		i = j;
	}
	unsigned short *ri = malloc(sizeof(unsigned short) * (ni + 1));
	for (int i = 0; ri && i < ni; i++) ri[i] = p->slot[ix[i] < nv ? ix[i] : 0];
	glGenBuffers(1, &p->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, ni * 2, ri, GL_STATIC_DRAW);
	glGenBuffers(1, &p->st); glBindBuffer(GL_ARRAY_BUFFER, p->st); glBufferData(GL_ARRAY_BUFFER, nblk * 64, st, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	free(ri); free(st); free(S);
	return p;
}
static void skin_plan_free(struct SkinPlan *p) {
	if (!p) return;
	if (p->ibo) glDeleteBuffers(1, &p->ibo); if (p->st) glDeleteBuffers(1, &p->st);
	free(p->mem); free(p);
}

// ---- the kernels: blocks [b0, b1) of plan p with palette pal (rows R0 R1 R2 with the translation in lane 3, as
// model.h's model_palette makes it, from the mesh's window), into out (16 floats per block)
// other vertices (none in the shipped models): per vertex, the bone matrices blended (the old chr_skin16_c maths)
static void sk_aos_vertex(const unsigned char *v, const float *pal, float *out) {
	float M[12]; const float *R = &pal[v[24] * 12]; float w0 = v[28] * (1.0f / 255);
	for (int k = 0; k < 12; k++) M[k] = R[k] * w0;
	for (int j = 1; j < 4; j++) { int wj = v[28 + j]; if (!wj) continue; const float *Q = &pal[v[24 + j] * 12]; float w = wj * (1.0f / 255); for (int k = 0; k < 12; k++) M[k] += Q[k] * w; }
	float p[3]; memcpy(p, v, 12); float o[4]; signed char *on = (signed char *)&o[3];
	for (int r = 0; r < 3; r++) o[r] = M[r * 4] * p[0] + M[r * 4 + 1] * p[1] + M[r * 4 + 2] * p[2] + M[r * 4 + 3];
	float nx = (signed char)v[12], ny = (signed char)v[13], nz = (signed char)v[14];
	for (int r = 0; r < 3; r++) { float t = M[r * 4] * nx + M[r * 4 + 1] * ny + M[r * 4 + 2] * nz; t = t > 127 ? 127 : t < -128 ? -128 : t; on[r] = (signed char)(int)(t + (t < 0 ? -0.5f : 0.5f)); }
	on[3] = (signed char)v[15];   // (b62: the specular mask)
#ifdef SKIN_NEON
	vst1q_f32(out, vld1q_f32(o));   // one 16-byte store (the output is uncached graphics memory)
#else
	memcpy(out, o, 16);
#endif
}
#ifdef SKIN_NEON
// x' = R.x + t for 4 vertices (lanes); normals: R.n (the plan's normals are x 256)
#define SK_ROW(r, x, y, z) vmlaq_lane_f32(vmlaq_lane_f32(vmlaq_lane_f32(vdupq_lane_f32(vget_high_f32(r), 1), x, vget_low_f32(r), 0), y, vget_low_f32(r), 1), z, vget_high_f32(r), 0)
#define SK_ROT(r, x, y, z) vmlaq_lane_f32(vmlaq_lane_f32(vmulq_lane_f32(x, vget_low_f32(r), 0), y, vget_low_f32(r), 1), z, vget_high_f32(r), 0)
// the three rotated normals of 4 vertices (narrowed to s16) + their 4th bytes (b62: the specular masks, sw) -> each vertex's
// 4th word: nx ny nz w as bytes (saturated)
static inline float32x4_t sk_pack_s(int16x4_t sx, int16x4_t sy, int16x4_t sz, int16x4_t sw) {
	int8x8_t bxy = vqmovn_s16(vcombine_s16(sx, sy)), bz = vqmovn_s16(vcombine_s16(sz, sw));
	int8x8x2_t a = vzip_s8(bxy, bz);                  // (x0 z0 x1 z1 x2 z2 x3 z3) (y0 w0 y1 w1 y2 w2 y3 w3)
	int8x8x2_t b = vzip_s8(a.val[0], a.val[1]);       // (x0 y0 z0 w0 x1 y1 z1 w1) (x2 y2 z2 w2 x3 y3 z3 w3)
	return vreinterpretq_f32_s8(vcombine_s8(b.val[0], b.val[1]));
}
static inline float32x4_t sk_pack_n(float32x4_t nx, float32x4_t ny, float32x4_t nz, int16x4_t sw) {
	return sk_pack_s(vqrshrn_n_s32(vcvtq_s32_f32(nx), 8), vqrshrn_n_s32(vcvtq_s32_f32(ny), 8), vqrshrn_n_s32(vcvtq_s32_f32(nz), 8), sw);
}
// 4 vertices (X, Y, Z, W: one lane each) -> 64 interleaved bytes (a 4x4 transpose, then two 32-byte stores)
static inline void sk_store4(float *out, float32x4_t X, float32x4_t Y, float32x4_t Z, float32x4_t W) {
	float32x4x2_t a = vtrnq_f32(X, Y), b = vtrnq_f32(Z, W);   // (x0 y0 x2 y2) (x1 y1 x3 y3), (z0 w0 z2 w2) (z1 w1 z3 w3)
	vst1q_f32(out, vcombine_f32(vget_low_f32(a.val[0]), vget_low_f32(b.val[0]))); vst1q_f32(out + 4, vcombine_f32(vget_low_f32(a.val[1]), vget_low_f32(b.val[1])));
	vst1q_f32(out + 8, vcombine_f32(vget_high_f32(a.val[0]), vget_high_f32(b.val[0]))); vst1q_f32(out + 12, vcombine_f32(vget_high_f32(a.val[1]), vget_high_f32(b.val[1])));
}
#define SK_LOAD_XYZN(b) const float *X = p->xyz + (b) * 12; float32x4_t x = vld1q_f32(X), y = vld1q_f32(X + 4), z = vld1q_f32(X + 8); const short *N = p->nrm + (b) * 12; \
	float32x4_t nx = vcvtq_f32_s32(vmovl_s16(vld1_s16(N))), ny = vcvtq_f32_s32(vmovl_s16(vld1_s16(N + 4))), nz = vcvtq_f32_s32(vmovl_s16(vld1_s16(N + 8)))
static void skin_blocks(const struct SkinPlan *p, const float *pal, int b0, int b1, float *out) {
	int b = b0, e = b1 < p->nb1 ? b1 : p->nb1;
	for (; b < e; b++) {   // one bone
		const float *R = pal + p->bone[b * 4] * 12; float32x4_t r0 = vld1q_f32(R), r1 = vld1q_f32(R + 4), r2 = vld1q_f32(R + 8);
		SK_LOAD_XYZN(b);
		sk_store4(out + b * 16, SK_ROW(r0, x, y, z), SK_ROW(r1, x, y, z), SK_ROW(r2, x, y, z), sk_pack_n(SK_ROT(r0, nx, ny, nz), SK_ROT(r1, nx, ny, nz), SK_ROT(r2, nx, ny, nz), vld1_s16(p->nw + b * 4)));
	}
	e = b1 < p->nb1 + p->nb2 ? b1 : p->nb1 + p->nb2;
	for (; b < e; b++) {   // two bones: B + w (A - B) per lane; positions, then normals (no register spills)
		const float *RA = pal + p->bone[b * 4] * 12, *RB = pal + p->bone[b * 4 + 1] * 12;
		float32x4_t w = vld1q_f32(p->w2 + (b - p->nb1) * 4), o[3]; int16x4_t sn[3];
		{ const float *X = p->xyz + b * 12; float32x4_t x = vld1q_f32(X), y = vld1q_f32(X + 4), z = vld1q_f32(X + 8);   // positions
#pragma GCC unroll 3
		  for (int r = 0; r < 3; r++) { float32x4_t t = SK_ROW(vld1q_f32(RB + r * 4), x, y, z); o[r] = vmlaq_f32(t, w, vsubq_f32(SK_ROW(vld1q_f32(RA + r * 4), x, y, z), t)); } }
		{ const short *N = p->nrm + b * 12;   // normals
		  float32x4_t nx = vcvtq_f32_s32(vmovl_s16(vld1_s16(N))), ny = vcvtq_f32_s32(vmovl_s16(vld1_s16(N + 4))), nz = vcvtq_f32_s32(vmovl_s16(vld1_s16(N + 8)));
#pragma GCC unroll 3
		  for (int r = 0; r < 3; r++) { float32x4_t u = SK_ROT(vld1q_f32(RB + r * 4), nx, ny, nz); sn[r] = vqrshrn_n_s32(vcvtq_s32_f32(vmlaq_f32(u, w, vsubq_f32(SK_ROT(vld1q_f32(RA + r * 4), nx, ny, nz), u))), 8); } }
		sk_store4(out + b * 16, o[0], o[1], o[2], sk_pack_s(sn[0], sn[1], sn[2], vld1_s16(p->nw + b * 4)));
	}
	e = b1 < p->nb1 + p->nb2 + p->nb3 ? b1 : p->nb1 + p->nb2 + p->nb3;
	for (; b < e; b++) {   // three or four bones: sum of w_j (R_j . v) per lane; positions, then normals (no register spills)
		SK_LOAD_XYZN(b); const float *W = p->w4 + (b - p->nb1 - p->nb2) * 16; const unsigned char *bo = p->bone + b * 4;
		int nbo = bo[3] == 255 ? 3 : 4;
		float32x4_t o[3]; int16x4_t sn[3];
#pragma GCC unroll 3
		for (int r = 0; r < 3; r++) {
			float32x4_t acc = vmulq_f32(vld1q_f32(W), SK_ROW(vld1q_f32(pal + bo[0] * 12 + r * 4), x, y, z)), an = vmulq_f32(vld1q_f32(W), SK_ROT(vld1q_f32(pal + bo[0] * 12 + r * 4), nx, ny, nz));
			for (int t = 1; t < nbo; t++) { float32x4_t R = vld1q_f32(pal + bo[t] * 12 + r * 4), w = vld1q_f32(W + t * 4); acc = vmlaq_f32(acc, w, SK_ROW(R, x, y, z)); an = vmlaq_f32(an, w, SK_ROT(R, nx, ny, nz)); }
			o[r] = acc; sn[r] = vqrshrn_n_s32(vcvtq_s32_f32(an), 8);
		}
		sk_store4(out + b * 16, o[0], o[1], o[2], sk_pack_s(sn[0], sn[1], sn[2], vld1_s16(p->nw + b * 4)));
	}
	for (; b < b1; b++) { const unsigned char *v = p->aos + (b - p->nb1 - p->nb2 - p->nb3) * 128; for (int l = 0; l < 4; l++) sk_aos_vertex(v + l * 32, pal, out + b * 16 + l * 4); }
}
#else
// scalar version (host builds, CHR_NO_NEON): the same blocks, one lane at a time
static void sk_out(float *o, const float *R, float x, float y, float z, float nx, float ny, float nz, int nw) {
	signed char n[4];
	for (int r = 0; r < 3; r++) {
		o[r] = R[r * 4] * x + R[r * 4 + 1] * y + R[r * 4 + 2] * z + R[r * 4 + 3];
		float t = (R[r * 4] * nx + R[r * 4 + 1] * ny + R[r * 4 + 2] * nz) * (1.0f / 256); t = t > 127 ? 127 : t < -128 ? -128 : t; n[r] = (signed char)(int)(t + (t < 0 ? -0.5f : 0.5f));
	}
	n[3] = (signed char)nw; memcpy(&o[3], n, 4);
}
static void skin_blocks(const struct SkinPlan *p, const float *pal, int b0, int b1, float *out) {
	int nsoa = p->nb1 + p->nb2 + p->nb3;
	for (int b = b0; b < b1; b++) {
		float *o = out + b * 16;
		if (b >= nsoa) { const unsigned char *v = p->aos + (b - nsoa) * 128; for (int l = 0; l < 4; l++) sk_aos_vertex(v + l * 32, pal, o + l * 4); continue; }
		const float *X = p->xyz + b * 12; const short *N = p->nrm + b * 12;
		for (int l = 0; l < 4; l++) {
			float M[12];
			if (b < p->nb1) memcpy(M, pal + p->bone[b * 4] * 12, sizeof(M));
			else if (b < p->nb1 + p->nb2) { const float *RA = pal + p->bone[b * 4] * 12, *RB = pal + p->bone[b * 4 + 1] * 12; float w = p->w2[(b - p->nb1) * 4 + l]; for (int k = 0; k < 12; k++) M[k] = RB[k] + w * (RA[k] - RB[k]); }
			else { memset(M, 0, sizeof(M)); for (int t = 0; t < 4 && p->bone[b * 4 + t] != 255; t++) { const float *R = pal + p->bone[b * 4 + t] * 12; float w = p->w4[(b - p->nb1 - p->nb2) * 16 + t * 4 + l]; for (int k = 0; k < 12; k++) M[k] += w * R[k]; } }
			sk_out(o + l * 4, M, X[l], X[4 + l], X[8 + l], N[l], N[4 + l], N[8 + l], p->nw[b * 4 + l]);
		}
	}
}
#endif

// ---- the per-frame queue
#define SKQ_ITEMS 1024
#define SKQ_PALS 96
#define SKQ_MAPS 256
#define SKQ_CHUNK 48   // blocks per job item (192 vertices: ~10-15 us of work on a Vita core)
typedef struct { const struct SkinPlan *p; const float *pal; float *out; int b0, b1; } SkinItem;
static struct {
	SkinItem it[SKQ_ITEMS]; int n;
	float pal[SKQ_PALS][48 * 12] __attribute__((aligned(16))); int npal;
	GLuint map[SKQ_MAPS]; int nmap;
	// statistics since skin_stats(): main-thread wall time of the runs, summed work time of the items, vertices, instances, runs
	unsigned long long wall_us, work_us, verts, inst, runs, items;
} SKQ;
static unsigned long long chr_skin_us; static int skin_verts;   // (main.c's "bots drawn" line: skinning wall time / vertices since its last line)
static void sk_job(void *ctx, int i) {
	(void)ctx; const SkinItem *s = &SKQ.it[i];
	unsigned long long t0 = jobs_now_us();
	skin_blocks(s->p, s->pal, s->b0, s->b1, s->out);
	__sync_fetch_and_add(&SKQ.work_us, jobs_now_us() - t0);
}
// skin everything queued (the workers + this thread), then unmap the output buffers; keep_pals: the palettes stay valid
// (a flush in the middle of an instance, when the item or map table is full)
static void sk_flush(int keep_pals) {
	if (!SKQ.n && !SKQ.nmap) { if (!keep_pals) SKQ.npal = 0; return; }
	unsigned long long t0 = jobs_now_us();
	jobs_parallel_for(sk_job, NULL, SKQ.n);
	for (int k = 0; k < SKQ.nmap; k++) { glBindBuffer(GL_ARRAY_BUFFER, SKQ.map[k]); glUnmapBuffer(GL_ARRAY_BUFFER); }
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	unsigned long long dt = jobs_now_us() - t0; SKQ.wall_us += dt; chr_skin_us += dt; SKQ.runs++; SKQ.items += SKQ.n;
	SKQ.n = 0; SKQ.nmap = 0; if (!keep_pals) SKQ.npal = 0;
}
static void skin_run(void) { sk_flush(0); }
// a palette for this run (copied: the caller's may be a stack array); the window of a mesh is pal + pbase * 12
static const float *skin_pal(const float *pal, int npal) {
	if (SKQ.npal >= SKQ_PALS) sk_flush(0);   // (between instances: nothing queued uses the old palettes after this)
	float *d = SKQ.pal[SKQ.npal++]; memcpy(d, pal, sizeof(float) * 12 * (npal < 48 ? npal : 48)); SKQ.inst++;
	return d;
}
// queue the skinning of plan p with palette window pal into buffer vbo (>= SKIN_OUT_BYTES(p) bytes). 0 when it could not map.
static int skin_add(const struct SkinPlan *p, const float *pal, GLuint vbo) {
	if (!p || !vbo) return 0;
	if (SKQ.nmap >= SKQ_MAPS || SKQ.n + p->nblk / SKQ_CHUNK + 1 > SKQ_ITEMS) sk_flush(1);
	glBindBuffer(GL_ARRAY_BUFFER, vbo); float *out = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY); glBindBuffer(GL_ARRAY_BUFFER, 0);
	if (!out) return 0;
	SKQ.map[SKQ.nmap++] = vbo;
	for (int b = 0; b < p->nblk; b += SKQ_CHUNK) { SkinItem *s = &SKQ.it[SKQ.n++]; s->p = p; s->pal = pal; s->out = out; s->b0 = b; s->b1 = b + SKQ_CHUNK < p->nblk ? b + SKQ_CHUNK : p->nblk; }
	SKQ.verts += p->nv; skin_verts += p->nv;
	return 1;
}
// totals since the last call (per-second log line); frames: frames in that time
static void skin_stats(int frames, float *wall_ms, float *work_ms, float *verts, float *inst, float *items) {
	float f = frames > 0 ? 1.0f / frames : 1;
	*wall_ms = SKQ.wall_us * f / 1000; *work_ms = SKQ.work_us * f / 1000; *verts = SKQ.verts * f; *inst = SKQ.inst * f; *items = SKQ.items * f;
	SKQ.wall_us = SKQ.work_us = SKQ.verts = SKQ.inst = SKQ.runs = SKQ.items = 0;
}

// ---- per-frame output buffers for the models that have no buffers of their own (model.h model_skin_get: the viewmodel,
// the menu preview, tutorial allies, range targets, skills models; any number of draws per frame). Buffers are handed
// out in order each frame from one of SKP_FRAMES sets in rotation (by vitaGL's frame number), so a buffer is rewritten
// only SKP_FRAMES frames after the GPU was given it; they are kept and reused (grown when a larger one is needed).
#ifdef __vita__
#define SKIN_FRAME() ((unsigned)vglGetFrameNumber())
#else
static unsigned skin_host_frame;   // (host tests: bump it per frame)
#define SKIN_FRAME() skin_host_frame
#endif
#define SKP_FRAMES 4
#define SKP_MAX 40
static struct { GLuint vbo[SKP_MAX]; int bytes[SKP_MAX], n, used; } SKP[SKP_FRAMES];
static unsigned skp_frame = ~0u; static int skp_set, skp_full;
static int skin_new_frame(void) { unsigned f = SKIN_FRAME(); if (f == skp_frame) return 0; skp_frame = f; skp_set = (skp_set + 1) % SKP_FRAMES; SKP[skp_set].used = 0; return 1; }
static GLuint skin_pool_buf(int bytes) {
	skin_new_frame();
	__typeof__(SKP[0]) *P = &SKP[skp_set]; int u = P->used;
	for (int i = u; i < P->n; i++) if (P->bytes[i] >= bytes) {   // the smallest one that fits goes next
		int k = i; for (int j = i + 1; j < P->n; j++) if (P->bytes[j] >= bytes && P->bytes[j] < P->bytes[k]) k = j;
		GLuint v = P->vbo[k]; int b = P->bytes[k]; P->vbo[k] = P->vbo[u]; P->bytes[k] = P->bytes[u]; P->vbo[u] = v; P->bytes[u] = b; P->used++; return v; }
	if (u >= SKP_MAX) { skp_full++; return 0; }   // (more skinned draws in one frame than the pool holds: not drawn)
	int sz = (bytes + 16383) & ~16383;   // a new one (16 KB steps) in slot u: the smaller one there moves to the end, or is dropped when the set is full
	if (P->n < SKP_MAX) { if (u < P->n) { P->vbo[P->n] = P->vbo[u]; P->bytes[P->n] = P->bytes[u]; } P->n++; }
	else glDeleteBuffers(1, &P->vbo[u]);
	glGenBuffers(1, &P->vbo[u]); glBindBuffer(GL_ARRAY_BUFFER, P->vbo[u]); glBufferData(GL_ARRAY_BUFFER, sz, NULL, GL_DYNAMIC_DRAW); glBindBuffer(GL_ARRAY_BUFFER, 0);
	P->bytes[u] = sz; P->used++;
	return P->vbo[u];
}

// ---- drawing the output (model.h's program in use, attributes 3 / 4 enabled, identity palette uploaded)
static void skin_bind_out(const struct SkinPlan *p, GLuint out) {
	glBindBuffer(GL_ARRAY_BUFFER, out);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16, (void *)0); glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, 16, (void *)12);
	glBindBuffer(GL_ARRAY_BUFFER, p->st);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 16, (void *)0); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, 16, (void *)8);
	glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16, (void *)12);
}
static void skin_draw_elems(const struct SkinPlan *p) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->ibo); glDrawElements(GL_TRIANGLES, p->ni, GL_UNSIGNED_SHORT, 0); }
