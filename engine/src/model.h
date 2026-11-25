// Skinned, animated models (GVM1 files built by vm/gvmlib.py): the first-person arms + weapon
// (bone poses in camera space) and third-person characters (bone poses in character space:
// z up, centimetres, the character faces -y). Several animation states can share one model.
#pragma once
#include <vitaGL.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "grade.h"
#include "skin.h"   // CPU skinning: load-time plans, NEON kernels, the per-frame job queue (chars, b35)
#ifndef LDP_PUSH   // perf (b45): ldprof.h's load-phase timers (main.c); nothing without it
#define LDP_PUSH(id) ((void)0)
#define LDP_POP() ((void)0)
#endif

struct SkinPlan;   // skin.h: the mesh's vertices regrouped for the CPU skinning (every mesh that is not rigid)
typedef struct { int tex, blend, nv, ni; GLuint vbo, ibo; unsigned char *cpu; int rigid; GLuint rvbo; int pbase; struct SkinPlan *sp; float lc[4]; int flags; float rim, fres; } MMesh;   // (b66 lc: a lens mesh's centre + radius, its own space: wpn_vm.h glass; b71 flags: MDF_*)
// b71: mesh flags (bits 16.. of the blend word in the file, vm/gvmlib.py MESH_*): MDF_EMIS = its texture carries the PC's glow
// map in alpha (alpha = 1 - glow: model.h MD_FS adds colour x (1 - alpha) x the emissive gain); MDF_MASK = drawn only on an
// operator wearing the enemy mask, MDF_UNMASK = hidden then (hair, goggles; md_masked, bots.h)
#define MDF_EMIS 1
#define MDF_MASK 2
#define MDF_UNMASK 4
// b116 blend mode 4 (vm/gvmlib.py MESH_CUT): a cut-out part (Kuro's torn sleeve edges, an alpha-tested two-sided mesh on the PC):
// drawn after the model's opaque meshes, blended over them by its texture's alpha, WITH depth written (a part without
// depth vanished behind whatever was drawn later, another operator behind this one for one)
#define MD_BM_CUT 4
#define MD_BM_LATE(bm) ((bm) == 2 || (bm) == 3 || (bm) == MD_BM_CUT)
#define MD_BM_ON(bm) do { glEnable(GL_BLEND); if ((bm) == MD_BM_CUT) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); else glBlendFunc(GL_SRC_ALPHA, (bm) == 3 ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE); glDepthMask((bm) == MD_BM_CUT ? GL_TRUE : GL_FALSE); } while (0)
#define MD_BM_OFF() do { glDisable(GL_BLEND); glDepthMask(GL_TRUE); } while (0)
#define MDF_GOGGLE 8   // b103: the skin's mask / goggles (set at load from chr_gog.txt, bots.h chr_gog_apply): hidden while md_nogog (the operative screen's Square)
// pbase: the mesh's palette window (its vertices index pal[pbase ...]; bits 8-15 of the blend word in the file, weapons
// agent: the Vita's skinning shader only reaches ~128 of the 144 'pal' rows, so no draw may index past entry ~42)
// cpu: vertex copy (model_keep_cpu); rigid: palette entry (pbase included) of a mesh on a single bone, -1 otherwise: model_load
// re-points its vertices at entry 0 with full weight, so it is drawn with a one-entry palette (rvbo = vbo, kept for the
// callers that bind rvbo); sp: the CPU skinning plan of every other mesh (skin.h, built at load)
typedef struct { char name[16]; int nf, loop; float fps; float *data; } MClip;   // data: nf * nb * 7 (qx qy qz qw px py pz)
typedef struct { float q[4], p[3]; } MBone;
typedef struct {
	int ntex, nb, npal, nmesh, nclip, natt, ok;
	GLuint *tex; MMesh *mesh; MClip *clip; int *pal_slot; float *pal_s2b; char (*bone_name)[32];
	float att[8][3]; char att_name[8][16];
	float *upper;   // per bone: 1 = upper body (spine and above, arms, weapon), 0 = pelvis and legs
	// b60: a viewmodel's light first-person meshes (vm_<gun>.gfp, tools/lod/make_fp.py; model_fp_load): per mesh a lighter
	// vertex + index buffer (ni 0: none for that mesh), right for the picture only while every clip in the pose is one of
	// fp_clips (bit per clip index); wvm_draw (wpn_vm.h) draws them when wvm_fp_on
	MMesh *fp; unsigned long long fp_clips;
	GLuint env_tex;   // b112: the model's own reflection picture (attachment 'envmap' = one of its textures; 0: env_ref.gtx) - md_env_use
} Model;

static int model_find_clip(const Model *m, const char *n) { for (int i = 0; i < m->nclip; i++) if (!strcmp(m->clip[i].name, n)) return i; return -1; }
static int model_find_bone(const Model *m, const char *n) { for (int i = 0; i < m->nb; i++) if (!strcmp(m->bone_name[i], n)) return i; return -1; }
static const float *model_attach(const Model *m, const char *n) { for (int i = 0; i < m->natt; i++) if (!strcmp(m->att_name[i], n)) return m->att[i]; return NULL; }
static float model_clip_len(const Model *m, int ci) { return ci < 0 ? 0 : (m->clip[ci].nf - 1) / m->clip[ci].fps; }

static int model_keep_cpu;   // set before model_load to keep a CPU copy of the vertices (diagnostics)
static struct SkinPlan *skin_plan_build(const unsigned char *v, int nv, const unsigned short *ix, int ni);
static void skin_plan_free(struct SkinPlan *p);
// a mesh on one bone: every vertex on the same palette entry at full weight (>= 250 of 255: the exporter's rounding)
static int model_mesh_rigid(const unsigned char *v, int nv) {
	if (nv <= 0) return -1; int e = v[24];
	for (int i = 0; i < nv; i++) { const unsigned char *q = v + i * 32; if (q[24] != e || q[28] < 250 || q[29] || q[30] || q[31]) return -1; }
	return e;
}
#include "glz.h"   // perf b56: packed models
#include "texup.h"   // perf b56: mip chains with one reallocation
static int model_load(Model *M, const char *path) {
	memset(M, 0, sizeof(*M));
#ifdef LDRA_B   // perf (b45): ldbg.h's read-ahead: a reader thread reads the file while this thread uploads (main.c includes it)
	FILE *f = ldra_fopen(path); if (!f) { f = glz_fopen(path); if (f) setvbuf(f, NULL, _IOFBF, 128 * 1024); }   // (perf b56: glz.h: packed models)
#else
	FILE *f = glz_fopen(path); if (f) setvbuf(f, NULL, _IOFBF, 128 * 1024);   // perf (b45): small reads from a 128 KB buffer, not ~1 KB card reads
#endif
	if (!f) return -1;
	char mg[4]; unsigned h[5]; fread(mg, 4, 1, f); fread(h, 4, 5, f);
	if (memcmp(mg, "GVM1", 4)) { fclose(f); return -2; }
	M->ntex = h[0]; M->nb = h[1]; M->npal = h[2]; M->nmesh = h[3]; M->nclip = h[4];
	if (M->npal > 48) { fclose(f); return -3; }   // shader palette holds 48 matrices
	M->tex = calloc(M->ntex, sizeof(GLuint)); glGenTextures(M->ntex, M->tex);
	void *buf = malloc(2 << 20);
	for (int i = 0; i < M->ntex; i++) {
		unsigned short w, hh; unsigned char fmt, nmp; fread(&w, 2, 1, f); fread(&hh, 2, 1, f); fread(&fmt, 1, 1, f); fread(&nmp, 1, 1, f);
		glBindTexture(GL_TEXTURE_2D, M->tex[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nmp > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		{ GLenum ifmt = fmt == 1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; unsigned sz0 = 0; fread(&sz0, 4, 1, f); if (sz0 > (2u << 20)) sz0 = 2u << 20; fread(buf, sz0, 1, f);
		  LDP_PUSH(LDP_TEXUP); glCompressedTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, hh, 0, sz0, buf); sceGxmTransferFinish(); LDP_POP();   // perf (b45): phases
		  // perf b56: levels 1.. read into buf, then uploaded last level first (texup.h: one reallocation per texture)
		  const unsigned char *lv[16]; unsigned lsz[16], tot = (sz0 + 63) & ~63u; int n = nmp > 16 ? 16 : nmp;   // (after level 0, kept for the canary's re-check)
		  for (int m = 1; m < nmp; m++) { unsigned sz = 0; fread(&sz, 4, 1, f);
		    if (m < 16 && tot + sz <= (2u << 20)) { fread((char *)buf + tot, sz, 1, f); lv[m] = (unsigned char *)buf + tot; lsz[m] = sz; tot += sz; }
		    else { static unsigned char skip[4096]; for (unsigned r = sz; r; ) { unsigned c = r < sizeof(skip) ? r : sizeof(skip); fread(skip, c, 1, f); r -= c; } if (m < n) n = m; } }   // (no seeks: read-ahead FILEs)
		  LDP_PUSH(LDP_TEXUP); texup_rest(ifmt, w, hh, n, lv, lsz, (const unsigned char *)buf, sz0); LDP_POP(); }
	}
	free(buf);
	M->bone_name = calloc(M->nb, 32); fread(M->bone_name, 32, M->nb, f);
	M->pal_slot = calloc(M->npal, sizeof(int)); M->pal_s2b = calloc(M->npal * 12, sizeof(float));
	for (int i = 0; i < M->npal; i++) { fread(&M->pal_slot[i], 4, 1, f); fread(&M->pal_s2b[i * 12], 4, 12, f); }
	M->mesh = calloc(M->nmesh, sizeof(MMesh));
	for (int i = 0; i < M->nmesh; i++) {
		MMesh *m = &M->mesh[i]; fread(&m->tex, 4, 1, f); fread(&m->blend, 4, 1, f); fread(&m->nv, 4, 1, f); fread(&m->ni, 4, 1, f);
		m->pbase = (m->blend >> 8) & 0xff; m->flags = (int)((unsigned)m->blend >> 16); m->blend &= 0xff;
		unsigned char *v = malloc(m->nv * 32); unsigned short *ix = malloc(m->ni * 2); fread(v, 32, m->nv, f); fread(ix, 2, m->ni, f); if (m->ni & 1) { short pad; fread(&pad, 2, 1, f); }
		// chars b35: rigid meshes re-pointed at entry 0 (drawn with a one-entry palette on every path); the others get a skin plan
		m->rigid = model_mesh_rigid(v, m->nv);
		if (m->blend & 128) {   // b66: a lens (wpn_vm.h WVM_LENS_BIT): its centre and radius for the glass dome
			float c[3] = { 0, 0, 0 }, r2 = 0; for (int k = 0; k < m->nv; k++) { const float *q = (const float *)(v + k * 32); for (int j = 0; j < 3; j++) c[j] += q[j] / m->nv; }
			for (int k = 0; k < m->nv; k++) { const float *q = (const float *)(v + k * 32); float dx = q[0] - c[0], dy = q[1] - c[1], dz = q[2] - c[2], dd = dx * dx + dy * dy + dz * dz; if (dd > r2) r2 = dd; }
			memcpy(m->lc, c, 12); m->lc[3] = sqrtf(r2); }
		if (m->rigid >= 0) { for (int k = 0; k < m->nv; k++) { unsigned char *q = v + k * 32; q[24] = q[25] = q[26] = q[27] = 0; q[28] = 255; q[29] = q[30] = q[31] = 0; } m->rigid += m->pbase; }
		LDP_PUSH(LDP_BUFUP);
		glGenBuffers(1, &m->vbo); glBindBuffer(GL_ARRAY_BUFFER, m->vbo); glBufferData(GL_ARRAY_BUFFER, m->nv * 32, v, GL_STATIC_DRAW);
		glGenBuffers(1, &m->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, m->ni * 2, ix, GL_STATIC_DRAW);
		LDP_POP();
		if (m->rigid >= 0) m->rvbo = m->vbo; else { LDP_PUSH(LDP_SKINPLAN); m->sp = skin_plan_build(v, m->nv, ix, m->ni); LDP_POP(); }
		if (model_keep_cpu) m->cpu = v; else free(v);
		free(ix);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	M->clip = calloc(M->nclip, sizeof(MClip)); LDP_PUSH(LDP_CLIPS);
	for (int i = 0; i < M->nclip; i++) {
		MClip *c = &M->clip[i]; fread(c->name, 16, 1, f); fread(&c->nf, 4, 1, f); fread(&c->loop, 4, 1, f); fread(&c->fps, 4, 1, f);
		c->data = malloc(c->nf * M->nb * 7 * 4); fread(c->data, 4, c->nf * M->nb * 7, f);
	}
	LDP_POP();
	unsigned na = 0; if (fread(&na, 4, 1, f) == 1) for (unsigned i = 0; i < na && i < 8; i++) { fread(M->att_name[i], 16, 1, f); fread(M->att[i], 4, 3, f); M->natt++; }
	fclose(f);
	// b115: a material's own rim / Fresnel light (attachment 'mat<texture index>' = (g_RimPower, Fresnel_Value x the Fresnel
	// colour's brightness, 0): vm/gvmlib.py Builder.tex, for the materials whose rim is far above the operators' usual - Reiko's
	// silver hair 2.0, Sitara's 0.8) on the meshes that wear that texture: md_mesh_emis sets it for their draws
	for (int i = 0; i < M->natt; i++) if (!strncmp(M->att_name[i], "mat", 3) && M->att_name[i][3] >= '0' && M->att_name[i][3] <= '9') {
		const int t = atoi(M->att_name[i] + 3); for (int k = 0; k < M->nmesh; k++) if (M->mesh[k].tex == t) { M->mesh[k].rim = M->att[i][0]; M->mesh[k].fres = M->att[i][1]; } }
	{ const float *ea = NULL; for (int i = 0; i < M->natt; i++) if (!strcmp(M->att_name[i], "envmap")) ea = M->att[i];   // b112 (vm/gvmlib.py Builder.envmap: the cyborg's cybogCUBE)
	  if (ea && ea[0] >= 0 && (int)ea[0] < M->ntex) { M->env_tex = M->tex[(int)ea[0]]; glBindTexture(GL_TEXTURE_2D, M->env_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); } }
	M->upper = calloc(M->nb, sizeof(float));
	for (int b = 0; b < M->nb; b++) {
		const char *n = M->bone_name[b];
		int lower = strstr(n, "Pelvis") || strstr(n, "Thigh") || strstr(n, "Calf") || strstr(n, "Foot") || strstr(n, "Toe") || !strcmp(n, "Bip01") || strstr(n, "Footsteps");
		M->upper[b] = lower ? 0.f : 1.f;
	}
	M->ok = 1; return 0;
}

// b60: the light first-person meshes of a loaded viewmodel (see Model.fp). The device draws 250-480 thousand indices a frame
// (b58-7), 70-90 thousand of them the viewmodel: 23-31 thousand triangles, the arms alone 18,878 up to the shoulders, of
// which 2,500-3,000 can be on screen while only the everyday clips play. -> the number of meshes that got one, < 0: no file
// / not for this model (the model is drawn as before)
static int model_fp_load(Model *M, const char *path) {
	if (!M->ok || M->fp) return -1;
	FILE *f = glz_fopen(path); if (!f) return -1;
	setvbuf(f, NULL, _IOFBF, 64 * 1024);
	char mg[4]; unsigned h[2] = { 0, 0 };
	if (fread(mg, 4, 1, f) != 1 || memcmp(mg, "GFP1", 4) || fread(h, 4, 2, f) != 2 || (int)h[0] != M->nmesh || h[1] > 64) { fclose(f); return -2; }
	unsigned long long cm = 0;
	for (unsigned c = 0; c < h[1]; c++) { char nm[17]; nm[16] = 0; if (fread(nm, 16, 1, f) != 1) { fclose(f); return -2; } int ci = model_find_clip(M, nm); if (ci >= 0 && ci < 64) cm |= 1ull << ci; }
	MMesh *fp = calloc(M->nmesh, sizeof(MMesh)); int ok = 1, n = 0;
	for (int i = 0; i < M->nmesh && ok; i++) {
		unsigned nv = 0, ni = 0; if (fread(&nv, 4, 1, f) != 1 || fread(&ni, 4, 1, f) != 1) { ok = 0; break; }
		if (!ni) continue;
		if (nv > 65535 || nv > (unsigned)M->mesh[i].nv || ni > (unsigned)M->mesh[i].ni || ni % 3) { ok = 0; break; }
		unsigned char *v = malloc(nv * 32); unsigned short *ix = malloc((ni + 1) * 2);
		if (fread(v, 32, nv, f) != nv || fread(ix, 2, ni + (ni & 1), f) != ni + (ni & 1)) ok = 0;
		for (unsigned k = 0; ok && k < ni; k++) if (ix[k] >= nv) ok = 0;
		if (ok) {
			if (M->mesh[i].rigid >= 0) for (unsigned k = 0; k < nv; k++) { unsigned char *q = v + k * 32; q[24] = q[25] = q[26] = q[27] = 0; q[28] = 255; q[29] = q[30] = q[31] = 0; }   // as model_load: one-entry palette
			MMesh *m = &fp[i]; m->nv = (int)nv; m->ni = (int)ni; LDP_PUSH(LDP_BUFUP);
			glGenBuffers(1, &m->vbo); glBindBuffer(GL_ARRAY_BUFFER, m->vbo); glBufferData(GL_ARRAY_BUFFER, nv * 32, v, GL_STATIC_DRAW);
			glGenBuffers(1, &m->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, ni * 2, ix, GL_STATIC_DRAW); LDP_POP(); n++;
		}
		free(v); free(ix);
	}
	fclose(f); glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	if (!ok) { for (int i = 0; i < M->nmesh; i++) if (fp[i].ni) { glDeleteBuffers(1, &fp[i].vbo); glDeleteBuffers(1, &fp[i].ibo); } free(fp); return -3; }
	M->fp = fp; M->fp_clips = cm; return n;
}

// frees everything model_load allocated (GL textures and buffers, clips); the struct is zeroed
static void model_free(Model *M) {
	if (!M->ok) return;
	if (M->tex) { glDeleteTextures(M->ntex, M->tex); free(M->tex); }
	for (int i = 0; M->mesh && i < M->nmesh; i++) { MMesh *m = &M->mesh[i]; glDeleteBuffers(1, &m->vbo); glDeleteBuffers(1, &m->ibo); if (m->rvbo && m->rvbo != m->vbo) glDeleteBuffers(1, &m->rvbo); free(m->cpu); skin_plan_free(m->sp); }
	for (int i = 0; M->fp && i < M->nmesh; i++) if (M->fp[i].ni) { glDeleteBuffers(1, &M->fp[i].vbo); glDeleteBuffers(1, &M->fp[i].ibo); }   // (b60)
	free(M->fp); free(M->mesh);
	for (int i = 0; M->clip && i < M->nclip; i++) free(M->clip[i].data);
	free(M->clip); free(M->bone_name); free(M->pal_slot); free(M->pal_s2b); free(M->upper);
	memset(M, 0, sizeof(*M));
}

// ---- shader (shared by all models). Fog matches the map shader; fogc.a = 0 turns it off.
// b62 (the user: "looks the best it can ... lighting, reflections"; footage: the guns' metal and the operators' armour catch
// the light): a highlight and a rim, per vertex (the GPU's time is in vertex work, but these are a few ALU ops next to the
// 4-bone skinning; the fragment shader adds one term). nrm.w = the vertex's specular mask (vm/gvmlib.py: the game's _S
// map sampled at the vertex, 0 in every older model: no highlight); eye.xyz = the camera in model space (md_eye_set: solved
// from the MVP), eye.w = highlight strength; sld.xyz = the highlight's light direction (model space), sld.w = rim strength
// (bots and menu figures: md_rim). sp = mask x Blinn (exponent 16, as 4 squarings) on lit sides + rim^3: added to the lit
// colour before the tint, so the spot's light (bots.h lit, light.h) dims it like the rest.
// b68: nrm.w carries two weights (vm/gvmlib.py pack_masks: 11 x highlight level + reflection level, weight = level^3 / 1000,
// the PC material's own units - vm/matdb.py): ms = the highlight's, me = the reflection's. floor(x 127 + 0.25) reads the byte
// back under either signed-normalised convention (c / 127 or (2c + 1) / 255). A b62-b67 mask byte read this way would be
// wrong: every model with masks is rebuilt with b68 (the data builder makes them all).
#define MD_VS_MASK \
" float wq = floor(nrm.w * 127.0 + 0.25); float qs = floor(wq * (1.0 / 11.0) + 0.02); float qe = (wq - qs * 11.0) * 0.1; qs *= 0.1;" \
" float ms = qs * qs * qs, me = qe * qe * qe;"
#define MD_VS_SPEC(P) \
" { float3 v = normalize(eye.xyz - " P "); float nh = saturate(dot(nn, normalize(sld.xyz + v))); nh *= nh; nh *= nh; nh *= nh; nh *= nh;" \
" float rm = 1.0 - saturate(dot(nn, v)); sp = ms * eye.w * nh * saturate(dot(nn, sld.xyz) * 4.0 + 0.5) + sld.w * rm * rm * rm; }"
// b65 (the user: Digital Foundry's "Inside Killzone: Mercenary" - reflections "most notable on flooring and our first-person
// gun models"): the environment reflection the original's weapons, operators and cyborg carry (their NIFs: NiTextureEffect +
// NiSourceCubeMap CUBE.dds, g_CubeMapBrightness 1.0) - here a sphere map of that cube (env_ref.gtx, vm/make_env.py) looked up
// per vertex with the view-space reflection: rr = the reflection in model space, turned into view space with the MVP's rows
// 0 / 1 / 3 (x and y scaled by the projection, w = -z) and their lengths (envs.xyz = 1 / |row|, set by md_eye_set), mapped
// as a sphere map; ev.z = the strength: the vertex's specular mask x envs.w x (0.45 + 0.55 x a fresnel term). The fragment
// shader adds tex2D(env, ev.xy) x ev.z to the lit colour (one more texture read; TEXUNIT4, bound once: md_env_load).
// b68: the PC's Fresnel term in the lighting (WeaponMaterial: the diffuse light pulled toward Fresnel_Color x F, F = Fresnel_Value x
// (1 - n.v)^(8 x Fresnel_Falloff), by saturate(F)): toward the silhouette every side of the gun reads a soft grey whatever the
// light - the footage's grey edge sheen on the black guns. fogp.w = Fresnel_Value (md_fogp[3]: the first-person draw sets 2,
// the weapons' usual value with Fresnel_Falloff 0.6 -> exponent 4.8 ~ 5 and Fresnel_Color 0.62; 0 = off, everything else).
#define MD_VS_FRES(P) \
" { float3 vf = normalize(eye.xyz - " P "); float fr = 1.0 - saturate(abs(dot(nn, vf))); float f2 = fr * fr; float F = fogp.w * f2 * f2 * fr;" \
" sh = lerp(sh, 0.62 * F, saturate(F)); }"
#define MD_VS_ENV(P) \
" { float3 ve = normalize(eye.xyz - " P "); float3 rr = reflect(-ve, nn); float3 rv = float3(dot(m0.xyz, rr) * envs.x, dot(m1.xyz, rr) * envs.y, -dot(m3.xyz, rr) * envs.z);" \
" float em = 2.0 * sqrt(rv.x * rv.x + rv.y * rv.y + (rv.z + 1.0) * (rv.z + 1.0)) + 0.0001; float fe = 1.0 - saturate(dot(nn, ve));" \
" ev = float3(rv.x / em + 0.5, 0.5 - rv.y / em, me * envs.w * (1.0 - fe * fe * fe)); }"   // b68: the PC's (1 - (1 - n.v)^3): faces reflect, edges fade (was 0.45 + 0.55 fe^2: the edges brightest)
#define MD_VS_SRC(ENVU, ENVO, ENVC) \
"void main(float3 pos, float4 nrm, float2 uv, float4 bi, float4 bw," \
" uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 pal[192], uniform float4 ldir, uniform float4 fogp, uniform float4 eye, uniform float4 sld" ENVU "," \
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float out sh : TEXCOORD1, float out fog : TEXCOORD2, float out sp : TEXCOORD3" ENVO ") {" \
" float4 p = float4(pos, 1); float3 n = nrm.xyz;" \
" int i0 = (int)(bi.x * 4.0 + 0.5), i1 = (int)(bi.y * 4.0 + 0.5), i2 = (int)(bi.z * 4.0 + 0.5), i3 = (int)(bi.w * 4.0 + 0.5);" \
" float3 s = bw.x * float3(dot(pal[i0], p), dot(pal[i0 + 1], p), dot(pal[i0 + 2], p))" \
"          + bw.y * float3(dot(pal[i1], p), dot(pal[i1 + 1], p), dot(pal[i1 + 2], p))" \
"          + bw.z * float3(dot(pal[i2], p), dot(pal[i2 + 1], p), dot(pal[i2 + 2], p))" \
"          + bw.w * float3(dot(pal[i3], p), dot(pal[i3 + 1], p), dot(pal[i3 + 2], p));" \
" float3 sn = bw.x * float3(dot(pal[i0].xyz, n), dot(pal[i0 + 1].xyz, n), dot(pal[i0 + 2].xyz, n))" \
"           + bw.y * float3(dot(pal[i1].xyz, n), dot(pal[i1 + 1].xyz, n), dot(pal[i1 + 2].xyz, n));" \
" float4 c = float4(s, 1); vpos = float4(dot(m0, c), dot(m1, c), dot(m2, c), dot(m3, c)); t0 = uv;" \
" float3 nn = normalize(sn + float3(0, 0, 0.0001)); sh = 0.40 + 0.75 * saturate(dot(nn, ldir.xyz)); fog = fogp.z * (1.0 - exp(-max(vpos.w - fogp.x, 0.0) * fogp.y));" \
MD_VS_MASK MD_VS_SPEC("s") MD_VS_FRES("s") ENVC " }"
#define MD_ENV_U ", uniform float4 envs"
#define MD_ENV_O ", float3 out ev : TEXCOORD4"
static const char *MD_VS_E = MD_VS_SRC(MD_ENV_U, MD_ENV_O, MD_VS_ENV("s"));   // b65: with the reflection
static const char *MD_VS = MD_VS_SRC("", "", "");                               // without (the fallback)
// chars b41: the one-bone program for rigid meshes and CPU-skinned output (most model draws: CPU skinning is the default).
// No bone attributes and no palette indexing (the bone's 3 rows are plain uniforms, held in registers), and a default
// uniform buffer of 9 rows instead of MD_VS's 198: vitaGL copies the whole buffer into its uniform pool on every draw.
#define MD_VS_R_SRC(ENVU, ENVO, ENVC) \
"void main(float3 pos, float4 nrm, float2 uv," \
" uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 bm[3], uniform float4 ldir, uniform float4 fogp, uniform float4 eye, uniform float4 sld" ENVU "," \
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float out sh : TEXCOORD1, float out fog : TEXCOORD2, float out sp : TEXCOORD3" ENVO ") {" \
" float4 p = float4(pos, 1); float3 n = nrm.xyz;" \
" float4 c = float4(dot(bm[0], p), dot(bm[1], p), dot(bm[2], p), 1);" \
" float3 sn = float3(dot(bm[0].xyz, n), dot(bm[1].xyz, n), dot(bm[2].xyz, n));" \
" vpos = float4(dot(m0, c), dot(m1, c), dot(m2, c), dot(m3, c)); t0 = uv;" \
" float3 nn = normalize(sn + float3(0, 0, 0.0001)); sh = 0.40 + 0.75 * saturate(dot(nn, ldir.xyz)); fog = fogp.z * (1.0 - exp(-max(vpos.w - fogp.x, 0.0) * fogp.y));" \
MD_VS_MASK MD_VS_SPEC("c.xyz") MD_VS_FRES("c.xyz") ENVC " }"
static const char *MD_VS_R_E = MD_VS_R_SRC(MD_ENV_U, MD_ENV_O, MD_VS_ENV("c.xyz"));   // b65: with the reflection
static const char *MD_VS_R = MD_VS_R_SRC("", "", "");
// fragment shaders: ENVP / ENVT = the reflection's inputs and its term in the lit colour ("" / "" without it)
#define MD_ENV_P ", float3 ev : TEXCOORD4, uniform sampler2D env : TEXUNIT4"
#define MD_ENV_T " + tex2D(env, ev.xy).rgb * ev.z"
#define MD_ENV_TH " + (half3)(tex2D(env, ev.xy).rgb) * (half)ev.z"
// b71: + colour x (1 - alpha) x emis.rgb (the PC's glow maps, baked in alpha: model.h MDF_EMIS, vm/gvmlib.py emissive_image;
// emis = 0 on every other mesh) and alpha (c.a + (1 - c.a) x emis.w) x tint.a (emis.w = 1 on a glow mesh: its alpha is not
// coverage - the shadow and cloak passes see it opaque)
#define MD_FS_PLAIN_SRC(ENVP, ENVT) \
"float4 main(float2 t0 : TEXCOORD0, float sh : TEXCOORD1, float fog : TEXCOORD2, float sp : TEXCOORD3, uniform sampler2D dif : TEXUNIT0, uniform float4 tint, uniform float4 fogc, uniform float4 emis" ENVP ") : COLOR {" \
" float4 c = tex2D(dif, t0); float g = 1.0 - c.a; return float4(lerp((c.rgb * (sh + g * emis.rgb) + sp" ENVT ") * tint.rgb, fogc.rgb, fog * fogc.a), (c.a + g * emis.w) * tint.a); }"
static const char *MD_FS_PLAIN = MD_FS_PLAIN_SRC("", ""), *MD_FS_PLAIN_E = MD_FS_PLAIN_SRC(MD_ENV_P, MD_ENV_T);
// graded version: built at init with grade.h's way of grading (grade_src)
#define MD_FS_PARAMS "float4 main(float2 t0 : TEXCOORD0, float sh : TEXCOORD1, float fog : TEXCOORD2, float sp : TEXCOORD3, uniform sampler2D dif : TEXUNIT0, uniform float4 tint, uniform float4 fogc, uniform float4 emis"
#define MD_FS_BODY " float4 c = tex2D(dif, t0); float g = 1.0 - c.a; float3 fc = lerp((c.rgb * (sh + g * emis.rgb) + sp) * tint.rgb, fogc.rgb, fog * fogc.a);"
#define MD_FS_RET " return float4(gc, (c.a + g * emis.w) * tint.a); }"
#define MD_FS_PARAMS_E MD_FS_PARAMS MD_ENV_P
#define MD_FS_BODY_E " float4 c = tex2D(dif, t0); float g = 1.0 - c.a; float3 fc = lerp((c.rgb * (sh + g * emis.rgb) + sp" MD_ENV_T ") * tint.rgb, fogc.rgb, fog * fogc.a);"
// render b39: the same in half precision (half shading / fog varyings, grade.h grade_src_h half tails); tried first, float if it fails
#define MD_FS_PARAMS_H "float4 main(float2 t0 : TEXCOORD0, half sh : TEXCOORD1, half fog : TEXCOORD2, half sp : TEXCOORD3, uniform sampler2D dif : TEXUNIT0, uniform float4 tint, uniform float4 fogc, uniform float4 emis"
#define MD_FS_BODY_H " half4 c = (half4)tex2D(dif, t0); half g = (half)1.0 - c.a; half3 fc = lerp((c.rgb * (sh + g * (half3)emis.rgb) + sp) * (half3)tint.rgb, (half3)fogc.rgb, fog * (half)fogc.a);"
#define MD_FS_RET_H " return float4((float3)gc, (float)((c.a + g * (half)emis.w) * (half)tint.a)); }"
#define MD_FS_PARAMS_HE MD_FS_PARAMS_H MD_ENV_P
#define MD_FS_BODY_HE " half4 c = (half4)tex2D(dif, t0); half g = (half)1.0 - c.a; half3 fc = lerp((c.rgb * (sh + g * (half3)emis.rgb) + sp" MD_ENV_TH ") * (half3)tint.rgb, (half3)fogc.rgb, fog * (half)fogc.a);"
static int md_half;                // render b39: 1 = the model program is the half one
static int md_grade = GRADE_OFF;   // the grading the model program uses

static GLuint md_prog; static GLint md_u_m[4], md_u_pal, md_u_ldir, md_u_tint, md_u_fog, md_u_fogp; static float md_fogp[4] = { 1000, 0.0001f, 0.3f, 0 }; static int md_shadow;   // shadow pass: skip additive meshes, leave the blend state alone
static GLuint md_vs;   // (skin_gpu.h: its compiled program's parameters are logged at start-up)
static GLuint md_prog_r, md_vs_r; static GLint mr_u_m[4], mr_u_bm, mr_u_ldir, mr_u_tint, mr_u_fog, mr_u_fogp;   // MD_VS_R (0: not built, md_prog does it all)
// b62: the highlight / rim uniforms (MD_VS_SPEC) and what the next draws use: md_rim (bots.h and the menu figures set it
// around their draws), md_spec (highlight strength), md_sld_dir (the highlight's light, model space; NULL = ldir tipped
// towards +z, the world's up for characters and props; the first-person draw passes its own, camera space)
static GLint md_u_eye = -1, md_u_sld = -1, mr_u_eye = -1, mr_u_sld = -1;
static float md_rim = 0, md_spec = 1; static const float *md_sld_dir;
// b65: the reflection (MD_VS_ENV): md_env_on = the programs have it (built and env_ref.gtx loaded, else its strength stays 0);
// md_env = the strength the next draws use (characters / props; the first-person draw sets its own around its draw)
static GLint md_u_envs = -1, mr_u_envs = -1; static int md_env_on, md_env_prog; static float md_env = 1.0f; static GLuint md_env_tex;
// b112: a model with its own reflection picture (Model.env_tex: the Ghost Assault cyborg - its NIF's cube is cybogCUBE.dds, not
// the weapons' street) has it on unit 4 for its draws: md_env_use(M) before, md_env_use(NULL) after (chr_draw, wvm_draw,
// model_draw_cpu). A no-op for every other model (one compare).
static GLuint md_env_cur;
static void md_env_use(const Model *M) {
	GLuint t = M && md_env_on ? M->env_tex : 0; if (t == md_env_cur) return;
	md_env_cur = t; glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, t ? t : md_env_tex); glActiveTexture(GL_TEXTURE0);
}
// b71: the glow (MD_FS emis): md_emis_k = the gain on MDF_EMIS meshes for the next draws (bots.h: 0 while cloaked), md_masked =
// the next character draws wear the enemy mask (MDF_MASK meshes drawn, MDF_UNMASK hidden); md_cur_r = the one-bone program is
// the current one (md_begin_bone; md_use) - every mesh draw sets emis for the program in use (md_mesh_emis)
static GLint md_u_emis = -1, mr_u_emis = -1; static float md_emis_k = 1.0f; static int md_masked, md_cur_r;
static int md_nogog;   // b103: the next character draws leave the MDF_GOGGLE meshes out (the player took the skin's mask off)
static int md_mesh_vis(const MMesh *m) { return !((m->flags & MDF_MASK) && !md_masked) && !((m->flags & MDF_UNMASK) && md_masked) && !((m->flags & MDF_GOGGLE) && md_nogog); }
// b115 (the user: Reiko's hair "isn't supposed to be that dark", with the PC's TOP OPERATIVE picture - silver-white): her
// hair material is the one of the game with Diffuse_Value 0.5 and MaterialDiffuse 0.59 (the others 1.0 / 0.99) AND g_RimPower
// 2.0 (the others 0.2-0.5), Fresnel_Value 2.5 with a pale Fresnel_Color: the PC darkens its lit colour and gives the light
// back as rim and Fresnel light (WeaponMaterial ps: colour = lighting x base + (1 - |n.v|)^g_RimExp x light x g_RimPower;
// lighting pulled toward Fresnel_Color x F). The port baked the darkening into the texture (b68) and had one rim strength
// for every mesh (md_rim) and no Fresnel light on characters - the hair came out as dark taupe. Now a mesh with its own
// numbers (MMesh.rim / fres, model_load) gets them for its draw: sld.w += MD_SHEEN_RIM x rim, fogp.w = MD_SHEEN_FRES x fres
// (the terms the programs already have: MD_VS_SPEC's rim^3, MD_VS_FRES). Two uniform sets on that mesh and on the next one;
// nothing for a model without such a mesh. Test switch sheen.txt "rimk fresk".
#define MD_SHEEN_RIM 0.73f    // (BOT_RIM 0.22 stands for the usual g_RimPower ~0.3)
#define MD_SHEEN_FRES 1.61f   // (MD_VS_FRES has the weapons' Fresnel_Color 0.62 built in)
static float md_sld_cur[4], md_sheen_k[2] = { MD_SHEEN_RIM, MD_SHEEN_FRES }; static GLint md_sld_u = -1; static int md_sheen_on;
static void md_mesh_sheen(const MMesh *m) {
	const int want = m && (m->rim > 0 || m->fres > 0) && !md_shadow;
	if ((!want && !md_sheen_on) || md_sld_u < 0 || md_sld_u != (md_cur_r ? mr_u_sld : md_u_sld)) return;
	float l[4], fp[4]; memcpy(l, md_sld_cur, 16); memcpy(fp, md_fogp, 16);
	if (want) { l[3] += md_sheen_k[0] * m->rim; if (md_sheen_k[1] * m->fres > fp[3]) fp[3] = md_sheen_k[1] * m->fres; }
	glUniform4fv(md_sld_u, 1, l); { GLint uf = md_cur_r ? mr_u_fogp : md_u_fogp; if (uf >= 0) glUniform4fv(uf, 1, fp); }
	md_sheen_on = want;
}
static void md_mesh_emis(const MMesh *m) {
	md_mesh_sheen(m);
	GLint u = md_cur_r ? mr_u_emis : md_u_emis; if (u < 0) return;
	float k = (m && (m->flags & MDF_EMIS) && !md_shadow) ? md_emis_k : 0, w = m && (m->flags & MDF_EMIS) ? 1.f : 0.f, v[4] = { k, k, k, w };
	glUniform4fv(u, 1, v);
}   // (b68: 1 = the PC's g_CubeMapBrightness for everything; the weights carry the differences - was 0.6, the guns 1.25)
static void md_eye_set(GLint ue, GLint us, const float *m, const float *ldir) {
	if (ue < 0 && us < 0) return;
	// the camera in model space: the point the MVP sends to x = y = w = 0 (rows 0, 1, 3 of the column-major matrix)
	float a00 = m[0], a01 = m[4], a02 = m[8], b0 = -m[12], a10 = m[1], a11 = m[5], a12 = m[9], b1 = -m[13], a20 = m[3], a21 = m[7], a22 = m[11], b2 = -m[15];
	float c0 = a11 * a22 - a12 * a21, c1 = a10 * a22 - a12 * a20, c2 = a10 * a21 - a11 * a20, det = a00 * c0 - a01 * c1 + a02 * c2, e[4];
	if (fabsf(det) > 1e-20f) {
		e[0] = (b0 * c0 - a01 * (b1 * a22 - a12 * b2) + a02 * (b1 * a21 - a11 * b2)) / det;
		e[1] = (a00 * (b1 * a22 - a12 * b2) - b0 * c1 + a02 * (a10 * b2 - b1 * a20)) / det;
		e[2] = (a00 * (a11 * b2 - b1 * a21) - a01 * (a10 * b2 - b1 * a20) + b0 * c2) / det;
	} else {   // orthographic: far out against the depth axis
		float d[3] = { a01 * a12 - a02 * a11, a02 * a10 - a00 * a12, a00 * a11 - a01 * a10 }, k = d[0] * m[2] + d[1] * m[6] + d[2] * m[10] > 0 ? -1e6f : 1e6f, n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 1e-20f;
		for (int i = 0; i < 3; i++) e[i] = d[i] / n * k;
	}
	e[3] = md_spec;
	float l[4] = { ldir[0], ldir[1], ldir[2] + 0.6f, md_rim }; if (md_sld_dir) memcpy(l, md_sld_dir, 12);
	{ float n = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]); if (n > 1e-6f) for (int i = 0; i < 3; i++) l[i] /= n; }
	if (ue >= 0) glUniform4fv(ue, 1, e); if (us >= 0) glUniform4fv(us, 1, l);
	memcpy(md_sld_cur, l, 16); md_sld_u = us; md_sheen_on = 0;   // (b115: md_mesh_sheen)
	{ GLint uv = ue >= 0 && ue == mr_u_eye ? mr_u_envs : md_u_envs;   // b65: the projection's row lengths (view-space reflection) and the strength
	  if (uv >= 0) { float n0 = sqrtf(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]), n1 = sqrtf(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]), n3 = sqrtf(m[3] * m[3] + m[7] * m[7] + m[11] * m[11]);
		float v[4] = { n0 > 1e-12f ? 1 / n0 : 0, n1 > 1e-12f ? 1 / n1 : 0, n3 > 1e-12f ? 1 / n3 : 0, md_env_on ? md_env : 0 }; glUniform4fv(uv, 1, v); } }
}
// b65: the reflection's sphere map (GTX1 RGBA8 / DXT1, vm/make_env.py), bound to texture unit 4 for good (no other program
// uses that unit); missing: a black 1x1 there and the strength 0, the picture as before
static void md_env_load(const char *path, void (*log)(const char *, ...)) {
	GLuint t = 0; FILE *f = fopen(path, "rb"); unsigned char h[16]; int w = 0, hh = 0, fmt = 0; unsigned size = 0; void *buf = NULL;
	if (f && fread(h, 1, 16, f) == 16 && !memcmp(h, "GTX1", 4)) {
		w = h[4] | h[5] << 8; hh = h[6] | h[7] << 8; fmt = h[8]; size = h[12] | h[13] << 8 | h[14] << 16 | (unsigned)h[15] << 24;
		if (w >= 4 && hh >= 4 && w <= 512 && hh <= 512 && size == (fmt == 1 ? (unsigned)(w / 4) * (hh / 4) * 8 : (unsigned)w * hh * 4)) { buf = malloc(size); if (buf && fread(buf, 1, size, f) != size) { free(buf); buf = NULL; } }
	}
	if (f) fclose(f);
	glActiveTexture(GL_TEXTURE4); glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	if (buf) { if (fmt == 1) glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, w, hh, 0, size, buf); else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf); }
	else { static const unsigned char black[4] = { 0, 0, 0, 255 }; glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black); }
#ifdef __vita__
	sceGxmTransferFinish();
#endif
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glActiveTexture(GL_TEXTURE0); md_env_tex = t; md_env_on = md_env_prog && buf != NULL; free(buf);
	if (log) log("model reflection: %s (%dx%d), %s\n", buf || md_env_on ? "env_ref.gtx" : "env_ref.gtx MISSING", w, hh, md_env_on ? "on" : md_env_prog ? "off (no map)" : "off (the programs have none)");
}
static GLuint (*md_mk)(GLenum, const char *);   // main.c's shader maker (skinprobe.h builds its comparison programs with it)
static int skin_probe_ok = -1;   // skinprobe.h: 1 = the GPU read every palette entry in every probe, 0 = not, -1 = not run
static int md_mesh0 = 0, md_mesh1 = 1 << 30;   // only meshes md_mesh0 <= i < md_mesh1 are drawn (first-person grenade: hide it after the throw)
// character models carry distance LODs (chr.h): blend bits 4..6 = the LODs that draw the mesh (0 = all). These generic
// paths draw the nearest LOD only; bits 0..3 keep the blend mode (2 = additive)
static int md_lod_sel;   // b73: the LOD model_draw / model_draw_cpu draw (0 = the nearest; range.h picks one per dummy by its size on screen)
static int md_lod_ok(const MMesh *m) { int l = (m->blend >> 4) & 7; return !l || (l & (1 << md_lod_sel)); }
typedef GLuint (*mkshader_fn)(GLenum, const char *);
static int model_shader(mkshader_fn mk, void (*log)(const char *, ...)) {
	// b65: the reflection's programs first (MD_VS_E + the env fragment shaders); if any part fails, the b64 programs
	int env = 1; GLuint vs = mk(GL_VERTEX_SHADER, MD_VS_E), fs = 0; GLint ok = 0; glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
	if (!ok) { log("model shader: the reflection's vertex shader failed: without it\n"); env = 0; vs = mk(GL_VERTEX_SHADER, MD_VS); glGetShaderiv(vs, GL_COMPILE_STATUS, &ok); if (!ok) return -1; }
	md_vs = vs; md_mk = mk;
	static char src[2400]; int gm;
	for (;;) {
	  for (gm = grade_mode; gm < GRADE_NMODES; gm++) {   // the map's grading, else the next ways down to none (MD_FS_PLAIN)
	    for (md_half = grade_half(), ok = 0; md_half >= 0; md_half--) {   // render b39: the half form first, then float
		if (md_half) { if (grade_src_h(src, sizeof(src), env ? MD_FS_PARAMS_HE : MD_FS_PARAMS_H, env ? MD_FS_BODY_HE : MD_FS_BODY_H, MD_FS_RET_H, gm) < 0) continue; fs = mk(GL_FRAGMENT_SHADER, src); }
		else if (gm == GRADE_OFF) fs = mk(GL_FRAGMENT_SHADER, env ? MD_FS_PLAIN_E : MD_FS_PLAIN);
		else if (grade_src(src, sizeof(src), env ? MD_FS_PARAMS_E : MD_FS_PARAMS, env ? MD_FS_BODY_E : MD_FS_BODY, MD_FS_RET, gm) >= 0) fs = mk(GL_FRAGMENT_SHADER, src);
		else continue;
		glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); if (ok) break;
		log("model shader: grading %s%s%s failed\n", grade_mode_name[gm], md_half ? " (half)" : "", env ? " (reflection)" : "");
	    }
	    if (ok) break;
	  }
	  if (ok && (!env || gm == grade_mode)) break;   // (the reflection only where it costs no grading quality)
	  if (!env) break;
	  log("model shader: the reflection's fragment shaders failed at the map grading: without it\n"); env = 0;
	  vs = mk(GL_VERTEX_SHADER, MD_VS); glGetShaderiv(vs, GL_COMPILE_STATUS, &ok); if (!ok) return -1; md_vs = vs;
	}
	if (!ok) return -1;
	md_env_prog = env;
	md_prog = glCreateProgram(); glAttachShader(md_prog, vs); glAttachShader(md_prog, fs);
	glBindAttribLocation(md_prog, 0, "pos"); glBindAttribLocation(md_prog, 1, "nrm"); glBindAttribLocation(md_prog, 2, "uv");
	glBindAttribLocation(md_prog, 3, "bi"); glBindAttribLocation(md_prog, 4, "bw");
	glLinkProgram(md_prog); glGetProgramiv(md_prog, GL_LINK_STATUS, &ok); if (!ok) return -2;
	for (int r = 0; r < 4; r++) { char nm[4] = { 'm', (char)('0' + r), 0 }; md_u_m[r] = glGetUniformLocation(md_prog, nm); }
	md_u_pal = glGetUniformLocation(md_prog, "pal"); md_u_ldir = glGetUniformLocation(md_prog, "ldir"); md_u_tint = glGetUniformLocation(md_prog, "tint"); md_u_fog = glGetUniformLocation(md_prog, "fogc"); md_u_fogp = glGetUniformLocation(md_prog, "fogp");
	md_u_eye = glGetUniformLocation(md_prog, "eye"); md_u_sld = glGetUniformLocation(md_prog, "sld");   // (b62)
	md_u_envs = env ? glGetUniformLocation(md_prog, "envs") : -1;   // (b65)
	md_u_emis = glGetUniformLocation(md_prog, "emis");   // (b71)
	glUseProgram(md_prog); glUniform1i(glGetUniformLocation(md_prog, "dif"), 0); if (env) glUniform1i(glGetUniformLocation(md_prog, "env"), 4);
	md_cur_r = 0; md_mesh_emis(NULL);   // (b71: 0 until a draw sets it)
	md_grade = gm; grade_register(md_prog, gm);
	log("model shader: pal %d ldir %d tint %d fog %d, colour grading: %s%s%s\n", md_u_pal, md_u_ldir, md_u_tint, md_u_fog, grade_mode_name[gm], md_half ? ", half precision" : "", env ? ", reflection" : "");   // (render b39: half note)
	{ GLuint vr = mk(GL_VERTEX_SHADER, env ? MD_VS_R_E : MD_VS_R); GLint okr = 0; md_vs_r = vr; glGetShaderiv(vr, GL_COMPILE_STATUS, &okr);   // chars b41: the one-bone program, same fragment shader
	  if (okr) { md_prog_r = glCreateProgram(); glAttachShader(md_prog_r, vr); glAttachShader(md_prog_r, fs);
		glBindAttribLocation(md_prog_r, 0, "pos"); glBindAttribLocation(md_prog_r, 1, "nrm"); glBindAttribLocation(md_prog_r, 2, "uv");
		glLinkProgram(md_prog_r); glGetProgramiv(md_prog_r, GL_LINK_STATUS, &okr); }
	  if (okr) {
		for (int r = 0; r < 4; r++) { char nm[4] = { 'm', (char)('0' + r), 0 }; mr_u_m[r] = glGetUniformLocation(md_prog_r, nm); }
		mr_u_bm = glGetUniformLocation(md_prog_r, "bm"); mr_u_ldir = glGetUniformLocation(md_prog_r, "ldir"); mr_u_tint = glGetUniformLocation(md_prog_r, "tint");
		mr_u_fog = glGetUniformLocation(md_prog_r, "fogc"); mr_u_fogp = glGetUniformLocation(md_prog_r, "fogp"); mr_u_eye = glGetUniformLocation(md_prog_r, "eye"); mr_u_sld = glGetUniformLocation(md_prog_r, "sld");
		mr_u_envs = env ? glGetUniformLocation(md_prog_r, "envs") : -1;   // (b65)
		mr_u_emis = glGetUniformLocation(md_prog_r, "emis");   // (b71)
		glUseProgram(md_prog_r); glUniform1i(glGetUniformLocation(md_prog_r, "dif"), 0); if (env) glUniform1i(glGetUniformLocation(md_prog_r, "env"), 4); grade_register(md_prog_r, gm);
		md_cur_r = 1; md_mesh_emis(NULL); md_cur_r = 0;
		if (mr_u_bm < 0 || mr_u_m[0] < 0) okr = 0; }
	  if (!okr) { md_prog_r = 0; mr_u_emis = -1; }
	  log("model shader: one-bone program %s\n", md_prog_r ? "ok (rigid meshes, CPU-skinned output)" : "FAILED (the palette program draws everything)");
	  glUseProgram(md_prog); md_cur_r = 0; }
	log("model shader: glow uniform %d / %d (b71)\n", md_u_emis, mr_u_emis);
	{ GLint nu = 0; glGetProgramiv(md_prog, GL_ACTIVE_UNIFORMS, &nu);   // (diagnostic, weapons agent: how many 'pal' rows the compiler kept)
	  for (int i = 0; i < nu && i < 64; i++) { char nm[32] = { 0 }; GLsizei l = 0; GLint sz = 0; GLenum ty = 0; glGetActiveUniform(md_prog, i, 31, &l, &sz, &ty, nm);
		if (!strncmp(nm, "pal", 3)) log("model shader: uniform '%s' size %d (192 declared: 48 entries x 4 rows)\n", nm, sz); } }
	return md_u_pal == -1 ? -3 : 0;
}

// chars b41: start a batch of one-bone draws (rigid meshes, CPU-skinned output): MD_VS_R when it was built (bone
// attributes off), else md_prog as before. Returns the uniform that takes the bone's 3 rows.
static GLint md_begin_bone(const float *mvp, const float *ldir, const float *tint, const float *fogc) {
	int r1 = md_prog_r != 0; const GLint *um = r1 ? mr_u_m : md_u_m;
	glUseProgram(r1 ? md_prog_r : md_prog); md_cur_r = r1;
	for (int r = 0; r < 4; r++) { float row[4] = { mvp[r], mvp[4 + r], mvp[8 + r], mvp[12 + r] }; glUniform4fv(um[r], 1, row); }
	glUniform4fv(r1 ? mr_u_ldir : md_u_ldir, 1, ldir); glUniform4fv(r1 ? mr_u_tint : md_u_tint, 1, tint); glUniform4fv(r1 ? mr_u_fog : md_u_fog, 1, fogc);
	{ GLint uf = r1 ? mr_u_fogp : md_u_fogp; if (uf >= 0) glUniform4fv(uf, 1, md_fogp); }
	md_eye_set(r1 ? mr_u_eye : md_u_eye, r1 ? mr_u_sld : md_u_sld, mvp, ldir);   // (b62)
	if (r1) { glDisableVertexAttribArray(3); glDisableVertexAttribArray(4); } else { glEnableVertexAttribArray(3); glEnableVertexAttribArray(4); }
	glActiveTexture(GL_TEXTURE0);
	return r1 ? mr_u_bm : md_u_pal;
}

// ---- poses
static void model_sample(const Model *M, int ci, float t, MBone *out) {
	const MClip *c = &M->clip[ci]; float fr = t * c->fps; int n = c->nf;
	if (c->loop) { float len = (float)(n - 1); fr = fmodf(fr, len); if (fr < 0) fr += len; }
	else { if (fr < 0) fr = 0; if (fr > n - 1) fr = (float)(n - 1); }
	int i0 = (int)fr; if (i0 >= n - 1) i0 = n - 2 < 0 ? 0 : n - 2; int i1 = i0 + 1 < n ? i0 + 1 : i0; float a = fr - i0; if (a > 1) a = 1;
	const float *A = c->data + (size_t)i0 * M->nb * 7, *B = c->data + (size_t)i1 * M->nb * 7;
	for (int b = 0; b < M->nb; b++, A += 7, B += 7) {
		float q[4], l = 0, d = A[0] * B[0] + A[1] * B[1] + A[2] * B[2] + A[3] * B[3], sb = d < 0 ? -a : a;
		for (int k = 0; k < 4; k++) { q[k] = A[k] * (1 - a) + B[k] * sb; l += q[k] * q[k]; }
		l = 1.0f / sqrtf(l > 1e-12f ? l : 1); for (int k = 0; k < 4; k++) out[b].q[k] = q[k] * l;
		for (int k = 0; k < 3; k++) out[b].p[k] = A[4 + k] + (B[4 + k] - A[4 + k]) * a;
	}
}
// dst = lerp(dst, src, w * mask[b])
static void model_blend(int nb, MBone *dst, const MBone *src, float w, const float *mask) {
	if (w <= 0) return; if (w >= 1 && !mask) { memcpy(dst, src, nb * sizeof(MBone)); return; }
	for (int b = 0; b < nb; b++) {
		float wb = mask ? w * mask[b] : w; if (wb <= 0) continue;
		float d = 0; for (int k = 0; k < 4; k++) d += dst[b].q[k] * src[b].q[k];
		float s = d < 0 ? -wb : wb, l = 0;
		for (int k = 0; k < 4; k++) { dst[b].q[k] = dst[b].q[k] * (1 - wb) + src[b].q[k] * s; l += dst[b].q[k] * dst[b].q[k]; }
		l = 1.0f / sqrtf(l); for (int k = 0; k < 4; k++) dst[b].q[k] *= l;
		for (int k = 0; k < 3; k++) dst[b].p[k] += (src[b].p[k] - dst[b].p[k]) * wb;
	}
}
static void quat_rotate(const float *q, const float *v, float *o) {
	float x = q[0], y = q[1], z = q[2], w = q[3];
	float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
	for (int r = 0; r < 3; r++) o[r] = R[r * 3] * v[0] + R[r * 3 + 1] * v[1] + R[r * 3 + 2] * v[2];
}
// a point given in a bone's own space -> model space
static void model_bone_xform(const MBone *B, const float *v, float *out) { quat_rotate(B->q, v, out); for (int r = 0; r < 3; r++) out[r] += B->p[r]; }

// ---- animation state: a looping base layer with crossfades + a one-shot action layer (optionally upper body only)
typedef struct {
	const Model *m; MBone *tmp[3];
	int base, base_prev, base_frozen; float base_t, base_prev_t, base_fade, base_fade_len, rate;
	int act; float act_t, act_in, act_out, act_w; int act_upper;
	float act_rate;   // b91: the action's playback speed (0 = 1; anim_play resets it): a sniper's bolt played to fit its fire rate
} Anim;
static void anim_init(Anim *a, const Model *m) {
	memset(a, 0, sizeof(*a)); a->m = m; a->base = a->base_prev = a->act = -1; a->base_fade = 1; a->base_fade_len = 0.2f; a->rate = 1;
	for (int i = 0; i < 3; i++) a->tmp[i] = calloc(m->nb, sizeof(MBone));
}
static void anim_free(Anim *a) { for (int i = 0; i < 3; i++) { free(a->tmp[i]); a->tmp[i] = NULL; } a->m = NULL; }
static void anim_set_base(Anim *a, int ci, float fade, int frozen) {
	if (ci < 0 || (ci == a->base && frozen == a->base_frozen)) return;
	a->base_prev = a->base; a->base_prev_t = a->base_t; a->base = ci; a->base_t = 0; a->base_fade = a->base_prev < 0 ? 1 : 0; a->base_fade_len = fade; a->base_frozen = frozen;
}
static void anim_play(Anim *a, int ci, float fin, float fout, int upper) {
	if (ci < 0) return;
	if (ci == a->act && a->act_w > 0.5f) { a->act_t = 0; a->act_out = fout; return; }  // restart (automatic fire)
	a->act = ci; a->act_t = 0; a->act_in = fin; a->act_out = fout; a->act_upper = upper; a->act_rate = 1; if (a->act_w > 1) a->act_w = 1;
}
static void anim_stop(Anim *a) { a->act = -1; a->act_w = 0; }
static int anim_busy(const Anim *a) { return a->act >= 0 && a->act_t < model_clip_len(a->m, a->act) - a->act_out; }
static void anim_update(Anim *a, float dt) {
	if (!a->base_frozen) a->base_t += dt * a->rate; a->base_prev_t += dt * a->rate;
	if (a->base_fade < 1) { a->base_fade += dt / (a->base_fade_len > 0.01f ? a->base_fade_len : 0.01f); if (a->base_fade > 1) a->base_fade = 1; }
	if (a->act >= 0) {
		float r_ = a->act_rate > 0 ? a->act_rate : 1; a->act_t += dt * r_; float len = model_clip_len(a->m, a->act);
		float target = a->act_t > len - a->act_out ? (len - a->act_t) / (a->act_out * r_ > 0.01f ? a->act_out * r_ : 0.01f) : 1;
		if (target < 0) target = 0;
		if (a->act_w < target) { a->act_w += dt / (a->act_in > 0.01f ? a->act_in : 0.01f); if (a->act_w > target) a->act_w = target; } else a->act_w = target;
		if (a->act_t >= len) { a->act = -1; a->act_w = 0; }
	}
}
// root: bone whose position anchors an upper-body action on top of the base pose (character pelvis), -1 for none
static MBone *anim_pose(Anim *a, int root) {
	const Model *M = a->m; MBone *P = a->tmp[0];
	model_sample(M, a->base, a->base_t, P);
	if (a->base_fade < 1 && a->base_prev >= 0) { model_sample(M, a->base_prev, a->base_prev_t, a->tmp[1]); model_blend(M->nb, a->tmp[1], P, a->base_fade * a->base_fade * (3 - 2 * a->base_fade), NULL); memcpy(P, a->tmp[1], M->nb * sizeof(MBone)); }
	if (a->act >= 0 && a->act_w > 0) {
		MBone *S = a->tmp[2]; model_sample(M, a->act, a->act_t, S);
		if (a->act_upper && root >= 0) { // keep the upper body on top of the base pelvis
			float d[3]; for (int k = 0; k < 3; k++) d[k] = P[root].p[k] - S[root].p[k];
			for (int b = 0; b < M->nb; b++) if (M->upper[b] > 0) for (int k = 0; k < 3; k++) S[b].p[k] += d[k];
		}
		model_blend(M->nb, P, S, a->act_w, a->act_upper ? M->upper : NULL);
	}
	return P;
}
// tilt the upper body about the model x axis through a pivot bone (aim up/down; + = up for a character facing -y)
static __attribute__((unused)) void pose_aim_pitch(const Model *M, MBone *P, int pivot, float pitch) {
	if (pivot < 0 || fabsf(pitch) < 1e-3f) return;
	float h = -pitch * 0.5f, r[4] = { sinf(h), 0, 0, cosf(h) }, c[3] = { P[pivot].p[0], P[pivot].p[1], P[pivot].p[2] };
	for (int b = 0; b < M->nb; b++) {
		if (M->upper[b] <= 0) continue;
		float v[3] = { P[b].p[0] - c[0], P[b].p[1] - c[1], P[b].p[2] - c[2] }, o[3]; quat_rotate(r, v, o);
		for (int k = 0; k < 3; k++) P[b].p[k] = c[k] + o[k];
		float *q = P[b].q, x = q[0], y = q[1], z = q[2], w = q[3];  // q = r * q
		q[0] = r[3] * x + r[0] * w + r[1] * z - r[2] * y; q[1] = r[3] * y - r[0] * z + r[1] * w + r[2] * x;
		q[2] = r[3] * z + r[0] * y - r[1] * x + r[2] * w; q[3] = r[3] * w - r[0] * x - r[1] * y - r[2] * z;
	}
}

// ---- draw. mvp: column-major model->clip; ldir: light direction in model space; fogc.a = fog amount (0 = none)
static float md_palbuf[48 * 12];
// chars b41: the shaders' palette (MD_VS, vision.h VIS_VS / VIS_VS_R) has 4 rows per entry: entry e = pal[4e .. 4e+2]
// (row 4e+3 unused). b40 on the device: with 3 rows per entry the vertex program fetches an entry with one 48-byte load
// from pal + 48e (GXP: idx = u16(bi * 3 + 0.5); addr = idx * 16 + base; LDR 48 bytes) and odd entries (pal + 16 mod 32)
// came back wrong. One-entry uploads (rigid meshes, the identity of CPU-skinned output) are entry 0 = 3 rows, as before.
static float md_pal4buf[48 * 16] __attribute__((aligned(16)));
// upload entries pb .. npal-1 of a model_palette palette (3 rows per entry) to uniform u in the 4-row layout
static void md_upload_window(GLint u, const float *pal, int pb, int npal) {
	int n = npal - pb; if (n <= 0) return; if (n > 48) n = 48;
	for (int e = 0; e < n; e++) { memcpy(md_pal4buf + e * 16, pal + (pb + e) * 12, 48); md_pal4buf[e * 16 + 12] = md_pal4buf[e * 16 + 13] = md_pal4buf[e * 16 + 14] = md_pal4buf[e * 16 + 15] = 0; }
	glUniform4fv(u, n * 4, md_pal4buf);
}
static void model_palette(const Model *M, const MBone *P, float *out) {
	for (int e = 0; e < M->npal; e++) {
		const MBone *B = &P[M->pal_slot[e]];
		float x = B->q[0], y = B->q[1], z = B->q[2], w = B->q[3];
		float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
		const float *S = &M->pal_s2b[e * 12]; float *o = &out[e * 12];
		for (int r = 0; r < 3; r++) for (int k = 0; k < 4; k++)
			o[r * 4 + k] = R[r * 3] * S[k] + R[r * 3 + 1] * S[4 + k] + R[r * 3 + 2] * S[8 + k] + (k == 3 ? B->p[r] : 0);
	}
}
static void model_draw_pal(const Model *M, const float *mvp, const float *pal, int npal, const float *ldir, const float *tint, const float *fogc, const GLuint *vbos);
static void model_draw(const Model *M, const float *mvp, const MBone *P, const float *ldir, const float *tint, const float *fogc) {
	model_palette(M, P, md_palbuf);
	model_draw_pal(M, mvp, md_palbuf, M->npal, ldir, tint, fogc, NULL);
}
// draw with a ready palette; vbos (optional) replace the meshes' vertex buffers
static void model_draw_pal(const Model *M, const float *mvp, const float *pal, int npal, const float *ldir, const float *tint, const float *fogc, const GLuint *vbos) {
	int all_rigid = 1; for (int i = 0; i < M->nmesh; i++) if (M->mesh[i].rigid < 0 && md_lod_ok(&M->mesh[i]) && i >= md_mesh0 && i < md_mesh1) all_rigid = 0;
	GLint ub = md_u_pal;
	if (all_rigid) ub = md_begin_bone(mvp, ldir, tint, fogc);   // (chars b41: weapons, the bomb, rigid props: the one-bone program)
	else {
		glUseProgram(md_prog); md_cur_r = 0;
		for (int r = 0; r < 4; r++) { float row[4] = { mvp[r], mvp[4 + r], mvp[8 + r], mvp[12 + r] }; glUniform4fv(md_u_m[r], 1, row); }
		glUniform4fv(md_u_ldir, 1, ldir); glUniform4fv(md_u_tint, 1, tint); glUniform4fv(md_u_fog, 1, fogc); if (md_u_fogp >= 0) glUniform4fv(md_u_fogp, 1, md_fogp);
		md_eye_set(md_u_eye, md_u_sld, mvp, ldir);   // (b62)
		glEnableVertexAttribArray(3); glEnableVertexAttribArray(4); glActiveTexture(GL_TEXTURE0);
	}
	int pb = -1;
	for (int pass = 0; pass < 2; pass++)
	for (int i = 0; i < M->nmesh; i++) {
		const MMesh *m = &M->mesh[i]; int bm = m->blend & 15, add = MD_BM_LATE(bm);   // (b88: 3 = alpha-blended, the helicopter's rotor discs; with the additive ones after the opaque)
		if (!md_lod_ok(m) || !md_mesh_vis(m) || add != pass || (md_shadow && add) || i < md_mesh0 || i >= md_mesh1) continue;
		md_mesh_emis(m);
		if (m->rigid >= 0) { if (m->rigid >= npal) continue; glUniform4fv(ub, 3, pal + m->rigid * 12); pb = -1; }   // one bone: a one-entry palette
		else if (m->pbase != pb && m->pbase < npal) { pb = m->pbase; md_upload_window(md_u_pal, pal, pb, npal); }   // the mesh's palette window (4 rows per entry)
		if (add) MD_BM_ON(bm);
		glBindTexture(GL_TEXTURE_2D, M->tex[m->tex]);
		glBindBuffer(GL_ARRAY_BUFFER, vbos ? vbos[i] : m->vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0);
		glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, 32, (void *)12);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)16);
		glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, 32, (void *)24);
		glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void *)28);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, 0);
		if (add) { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
	}
	glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// ---- CPU skinning (skin.h) for the models drawn with model_draw_cpu: the menu preview, tutorial allies, range targets,
// skills models, the first-person viewmodel. On the Vita the shader's palette path drew multi-bone meshes wrong (build
// 19 model lab, b32: spikes) while the one-entry palette path (rigid meshes, CPU-skinned output) is right, so these
// models are skinned on the CPU unless md_gpu_skin is set (only after the start-up self-test passed: skin_gpu.h, or
// forced by chrskin.txt).
// The output buffers come from skin.h's per-frame pool; a model drawn twice in a frame with the same pose (and mesh
// range) is skinned once (model_skin_get finds it by a hash of the pose).
typedef struct { int cur, ok; } SkinCache;   // (b35: kept for the callers; the buffers are per frame now)
static int skin_all_cpu;   // (debug file "cpuall"; b35: no effect, CPU skinning is the default everywhere)
static int md_gpu_skin = 0;   // model_draw_cpu goes to the GPU palette path (model_draw)
static int md_gpu_vm = 0;     // wpn_vm.h's viewmodel draw uses the GPU palette path
static void model_prepare_rigid(Model *M) { (void)M; }   // (b35: model_load re-points rigid meshes itself)
static void skincache_init(SkinCache *c, const Model *M) { (void)M; c->cur = 0; c->ok = 1; }
static void skincache_free(SkinCache *c) { c->ok = 0; }
static const float md_identity[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
#define SKF_MESH 16
typedef struct { const Model *M; unsigned hash; int m0, m1, lod; GLuint vbo[SKF_MESH]; } SkFrameInst;   // (b73: lod = md_lod_sel when skinned)
static SkFrameInst skf[48]; static int nskf; static unsigned skf_frame = ~0u;
static unsigned skin_pose_hash(const Model *M, const MBone *P) {
	unsigned h = 2166136261u ^ (unsigned)(uintptr_t)M; const unsigned *w = (const unsigned *)P; int n = M->nb * (int)(sizeof(MBone) / 4);
	for (int i = 0; i < M->nmesh && i < SKF_MESH; i++) h = (h ^ (unsigned)(uintptr_t)M->mesh[i].sp) * 16777619u;   // (mesh subsets: skills.h passes a Model on the stack)
	for (int i = 0; i < n; i++) h = (h ^ w[i]) * 16777619u;
	return h;
}
// this frame's skinned buffers of the meshes m0 <= i < m1 of M in pose P (index i < SKF_MESH; 0 = no buffer: rigid / other
// LOD / out of range). Skinned now unless queue_only (then skin_run() before drawing). NULL when the pool is full.
static const GLuint *model_skin_get(const Model *M, const MBone *P, int m0, int m1, int queue_only) {
	if (skf_frame != SKIN_FRAME()) { skf_frame = SKIN_FRAME(); nskf = 0; }
	unsigned h = skin_pose_hash(M, P);
	for (int k = 0; k < nskf; k++) if (skf[k].M == M && skf[k].hash == h && skf[k].m0 == m0 && skf[k].m1 == m1 && skf[k].lod == md_lod_sel) return skf[k].vbo;
	if (nskf >= (int)(sizeof(skf) / sizeof(skf[0]))) return NULL;
	SkFrameInst *e = &skf[nskf]; memset(e, 0, sizeof(*e)); e->M = M; e->hash = h; e->m0 = m0; e->m1 = m1; e->lod = md_lod_sel;
	float pal[48 * 12]; model_palette(M, P, pal); const float *qp = skin_pal(pal, M->npal);
	for (int i = m0 < 0 ? 0 : m0; i < M->nmesh && i < SKF_MESH && i < m1; i++) { const MMesh *m = &M->mesh[i]; if (!m->sp || !md_lod_ok(m)) continue;
		GLuint v = skin_pool_buf(SKIN_OUT_BYTES(m->sp)); if (!v) return NULL;
		if (skin_add(m->sp, qp + m->pbase * 12, v)) e->vbo[i] = v; }
	nskf++;
	if (!queue_only) skin_run();
	return e->vbo;
}
// batch use: model_skin_queue for every instance, one skin_run(), then model_draw_cpu for each (it finds them skinned)
static void model_skin_queue(const Model *M, SkinCache *c, const MBone *P) { (void)c; if (!md_gpu_skin) model_skin_get(M, P, 0, 1 << 30, 1); }
static void model_draw_cpu_(const Model *M, SkinCache *c, const float *mvp, const MBone *P, const float *ldir, const float *tint, const float *fogc, int reskin);
static void model_draw_cpu(const Model *M, SkinCache *c, const float *mvp, const MBone *P, const float *ldir, const float *tint, const float *fogc, int reskin) {
	md_env_use(M); model_draw_cpu_(M, c, mvp, P, ldir, tint, fogc, reskin); md_env_use(NULL);   // (b112)
}
static void model_draw_cpu_(const Model *M, SkinCache *c, const float *mvp, const MBone *P, const float *ldir, const float *tint, const float *fogc, int reskin) {
	(void)c; (void)reskin;
	if (md_gpu_skin) { model_draw(M, mvp, P, ldir, tint, fogc); return; }   // a verified GPU path
	const GLuint *vb = model_skin_get(M, P, 0, 1 << 30, 0); if (!vb) return;
	float pal[48 * 12]; model_palette(M, P, pal);
	GLint ub = md_begin_bone(mvp, ldir, tint, fogc);
	for (int pass = 0; pass < 2; pass++)
	for (int i = 0; i < M->nmesh; i++) {
		const MMesh *m = &M->mesh[i]; int bm = m->blend & 15, add = bm == 2 || bm == MD_BM_CUT; if (!md_lod_ok(m) || !md_mesh_vis(m) || add != pass || (md_shadow && add) || i < md_mesh0 || i >= md_mesh1) continue;
		md_mesh_emis(m);
		if (m->rigid >= 0) { glUniform4fv(ub, 3, &pal[m->rigid * 12]); glBindBuffer(GL_ARRAY_BUFFER, m->rvbo);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0); glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, 32, (void *)12);
			glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)16); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, 32, (void *)24);
			glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void *)28); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); }
		else if (i < SKF_MESH && vb[i]) { glUniform4fv(ub, 3, md_identity); skin_bind_out(m->sp, vb[i]); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->sp->ibo); }
		else continue;
		if (add) MD_BM_ON(bm);
		glBindTexture(GL_TEXTURE_2D, M->tex[m->tex]);
		glDrawElements(GL_TRIANGLES, m->rigid >= 0 ? m->ni : m->sp->ni, GL_UNSIGNED_SHORT, 0);
		if (add) MD_BM_OFF();
	}
	glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
