// skin_gpu.h - does the GPU palette path (model.h's shader indexing pal[]) draw skinned models right on this device?
// (chars, b35; b41: the palette has 4 rows per entry and skinprobe.h's probe must pass too, see skinprobe.h for why).
// Included at the end of bots.h (needs model.h, chr.h, skin.h; skinprobe.h on the Vita).
//
// b19 (model lab) and b32 (the GPU default) drew every multi-bone character as spikes on the Vita; the CPU path (one-
// entry palettes) was right. b40's probe found why (skinprobe.h): with 3 rows per entry the shader fetched odd entries
// from an address 16 mod 32 and got garbage; b41 uses 4 rows per entry. skinprobe.h checks every entry (also with the
// uniform pool shifted), skin_gpu_dump_program() logs the compiled layout, and skin_gpu_select() then draws real models in
// real poses both ways into an offscreen target and compares them; a class of models is switched to the GPU only when
// its pictures agree (silhouette and colour). Otherwise (or when anything is off) the CPU path stays: skin.h, NEON on all
// cores, which is correct by construction. ux0:data/gitsfa/chrskin.txt: "0" = CPU always (no test), "1" = GPU always
// (no test), missing / anything else = the test decides.
//   classes: SKN_CHR  chr.h characters (bots, bodies, Think Tanks)           -> chr_gpu_skin
//            SKN_MODEL model.h model_draw_cpu (menu, tutorial, range, skills) -> md_gpu_skin
//            SKN_VM   wpn_vm.h first-person viewmodel (arms + gun)            -> md_gpu_vm
#pragma once
#include <malloc.h>
enum { SKN_CHR, SKN_MODEL, SKN_VM, SKN_N };
static const char *const skn_name[SKN_N] = { "characters", "menu/tutorial/range/skills models", "viewmodel" };
static int skin_gpu_force = -1;          // chrskin.txt: 0 / 1, -1 = test
static float skin_gpu_mis[SKN_N] = { -1, -1, -1 };   // worst silhouette mismatch measured per class (-1: not tested)
static int skn_cov[2] = { -1, -1 };   // the last skin_gpu_test's covered pixels: [0] GPU palette pass, [1] CPU pass (-1: not drawn)
static void skin_gpu_set(int cls, int on) { if (cls == SKN_CHR) chr_gpu_skin = on; else if (cls == SKN_MODEL) md_gpu_skin = on; else md_gpu_vm = on; }
static void skin_gpu_read_cfg(const char *dir) {
	char p[160]; snprintf(p, sizeof(p), "%schrskin.txt", dir); FILE *f = fopen(p, "rb"); if (!f) return;
	int k = -1; if (fscanf(f, "%d", &k) == 1 && (k == 0 || k == 1)) skin_gpu_force = k; fclose(f);
}

#ifdef __vita__
// the compiled vertex program of model.h's shader: every parameter (category, type, components, array size, resource and
// container index) and the size of its default uniform buffer. If 'pal' (144 x 4 words from its resource index) does not
// fit in that buffer, vitaGL copies only part of it per draw and the entries past the end are garbage on the GPU.
static void skin_gpu_dump_program(GLuint vs, void (*log)(const char *, ...)) {
	if (!vs) return;
	static unsigned char bin[65536] __attribute__((aligned(16))); GLsizei len = 0;
	vglGetShaderBinary(vs, sizeof(bin), &len, bin);
	int at = -1; for (int i = 0; i + 4 <= len && i + 4 <= (int)sizeof(bin); i += 4) if (!memcmp(bin + i, "GXP", 4)) { at = i; break; }
	if (at < 0) { log("skin gpu: model shader binary: no GXP header in %d bytes\n", (int)len); return; }
	const SceGxmProgram *prog = (const SceGxmProgram *)(bin + at);   // (4-byte aligned in bin)
	unsigned ub = sceGxmProgramGetDefaultUniformBufferSize(prog), np = sceGxmProgramGetParameterCount(prog);
	log("skin gpu: model vertex program: %u bytes, default uniform buffer size %u (as bytes: %u float4; as words: %u float4), %u parameters\n",
		sceGxmProgramGetSize(prog), ub, ub / 16, ub / 4, np);
	{ unsigned r = skp_dub(md_vs_r); if (r) log("skin gpu: one-bone vertex program: default uniform buffer %u bytes (copied per draw instead of %u)\n", r, ub); }
	for (unsigned i = 0; i < np && i < 40; i++) {
		const SceGxmProgramParameter *q = sceGxmProgramGetParameter(prog, i); if (!q) continue;
		unsigned cat = sceGxmProgramParameterGetCategory(q), ty = sceGxmProgramParameterGetType(q), nc = sceGxmProgramParameterGetComponentCount(q),
			as = sceGxmProgramParameterGetArraySize(q), ri = sceGxmProgramParameterGetResourceIndex(q), ci = sceGxmProgramParameterGetContainerIndex(q);
		log("skin gpu:   %-6s category %u type %u components %u array %3u resource %4u container %2u%s\n", sceGxmProgramParameterGetName(q), cat, ty, nc, as, ri, ci,
			cat == SCE_GXM_PARAMETER_CATEGORY_UNIFORM && (ri + as * 4) * 4 > ub ? "  <- past the end of the default buffer (if the size is in bytes)" : "");
	}
}
#else
static void skin_gpu_dump_program(GLuint vs, void (*log)(const char *, ...)) { (void)vs; (void)log; }
#endif

#if defined(__vita__) || defined(SKIN_GPU_HOST)   // (host builds: only with a GL stub that has offscreen targets, tools/chars/host/vitaGL.h)
// draws M in pose P with the GPU palette path and with the CPU path into a W x H offscreen target (orthographic, fitted
// to the CPU-skinned vertices; view 0: model x / z on screen (characters: z up), 1: x / y (the viewmodel's camera space))
// and returns the share of the covered pixels where the two silhouettes differ (*cdiff: mean colour difference 0..255 where
// both cover). -1 when the test could not run.
static float skin_gpu_test(const Model *M, const MBone *P, int view, float *cdiff) {
	enum { W = 160, H = 160 };
	*cdiff = 0;
	float pal[48 * 12]; model_palette(M, P, pal);
	float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f }; int nv = 0;
	for (int i = 0; i < M->nmesh && i < 8; i++) {   // bounds of the CPU skinning (scratch memory, this thread)
		const MMesh *m = &M->mesh[i]; if (!m->sp || !md_lod_ok(m)) continue;
		float *o = memalign(16, SKIN_OUT_BYTES(m->sp)); if (!o) return -1;
		skin_blocks(m->sp, pal + m->pbase * 12, 0, m->sp->nblk, o);
		for (int v = 0; v < m->sp->nv; v++) { const float *q = o + m->sp->slot[v] * 4; for (int k = 0; k < 3; k++) { if (q[k] < lo[k]) lo[k] = q[k]; if (q[k] > hi[k]) hi[k] = q[k]; } nv++; }
		free(o);
	}
	if (!nv) return -1;
	int ax = 0, ay = view ? 1 : 2, az = view ? 2 : 1;
	float c[3], h[3]; for (int k = 0; k < 3; k++) { c[k] = (lo[k] + hi[k]) * 0.5f; h[k] = (hi[k] - lo[k]) * 0.55f + 1; }
	float s = h[ax] > h[ay] ? h[ax] : h[ay];   // one scale for both screen axes
	float mvp[16]; memset(mvp, 0, sizeof(mvp));   // column-major: screen x = (p[ax] - c) / s, y = (p[ay] - c) / s, depth from p[az]
	mvp[ax * 4 + 0] = 1 / s; mvp[ay * 4 + 1] = 1 / s; mvp[az * 4 + 2] = 0.5f / h[az];
	mvp[12] = -c[ax] / s; mvp[13] = -c[ay] / s; mvp[14] = -c[az] * 0.5f / h[az]; mvp[15] = 1;
	for (int k = 0; k < 16; k++) mvp[k] *= 2;   // (w = 2: the same picture; the host rasterizer drops w <= 1 as behind the eye)
	GLuint ct = 0, fb = 0; static unsigned char px[2][W * H * 4];
	glGenTextures(1, &ct); glBindTexture(GL_TEXTURE_2D, ct); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ct, 0);
	glViewport(0, 0, W, H); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDepthMask(GL_FALSE);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);   // (skinprobe.h left them off; the draws expect them on)
	const float ld[4] = { 0.3f, -0.5f, 0.8f, 0 }, tint[4] = { 1, 1, 1, 1 }, nofog[4] = { 0, 0, 0, 0 };
	int keep_gpu = md_gpu_skin; skn_cov[0] = skn_cov[1] = -1;
	for (int pass = 0; pass < 2; pass++) for (int tries = 0; tries < 2; tries++) {   // 0: the GPU palette path, 1: the CPU path
		glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
		if (!pass) model_draw(M, mvp, P, ld, tint, nofog); else { md_gpu_skin = 0; model_draw_cpu(M, NULL, mvp, P, ld, tint, nofog, 1); }
		memset(px[pass], 0, sizeof(px[pass])); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px[pass]);
		int cov = 0; for (int i = 0; i < W * H; i++) cov += px[pass][i * 4 + 3] > 128;
		skn_cov[pass] = cov; if (cov) break;   // (an empty read-back: once more. b41 in Vita3K: the first test of a run read nothing back)
	}
	md_gpu_skin = keep_gpu;
#ifdef SKIN_GPU_DUMP
	{ static int nd; char fn[64]; for (int k = 0; k < 2; k++) { snprintf(fn, sizeof(fn), "/tmp/skgpu_%d_%d.ppm", nd, k); FILE *f = fopen(fn, "wb"); fprintf(f, "P6 %d %d 255\n", W, H); for (int i = 0; i < W * H; i++) fwrite(px[k] + i * 4, 1, 3, f); fclose(f); } nd++; }
#endif
	glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &fb); glDeleteTextures(1, &ct);
	glViewport(0, 0, 960, 544); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
	long un = 0, x = 0, both = 0; double cd = 0;
	for (int i = 0; i < W * H; i++) {
		int a = px[0][i * 4 + 3] > 128, b = px[1][i * 4 + 3] > 128; un += a | b; x += a ^ b;
		if (a && b) { both++; for (int k = 0; k < 3; k++) cd += abs(px[0][i * 4 + k] - px[1][i * 4 + k]) / 3.0; }
	}
	*cdiff = both ? (float)(cd / both) : 0;
	return un > 50 ? (float)x / un : -1;   // (fewer than 50 pixels: nothing to compare)
}
#else
static float skin_gpu_test(const Model *M, const MBone *P, int view, float *cdiff) { (void)M; (void)P; (void)view; *cdiff = 0; return -1; }
#endif
// one test into a class's verdict (worst case kept)
static void skin_gpu_note(int cls, const char *what, float mis, float cd, void (*log)(const char *, ...)) {
	log("skin gpu: %s: %s: gpu vs cpu silhouettes differ in %.2f%% of the covered pixels, colour by %.1f / 255 (pixels covered: gpu %d, cpu %d of 25600)%s\n", skn_name[cls], what, mis * 100, cd,
		skn_cov[0], skn_cov[1], mis < 0 ? " (not measured)" : skn_cov[0] == 0 && skn_cov[1] > 0 ? " (the GPU pass drew nothing)" : skn_cov[1] == 0 ? " (READBACK BROKEN: the CPU pass read back nothing)" : "");
	if (mis < 0) mis = 1;   // not measured: not proven
	if (cd > 12) mis = mis > 0.5f ? mis : 0.5f;   // right shape, wrong shading: not proven either
	if (mis > skin_gpu_mis[cls]) skin_gpu_mis[cls] = mis;
}
// the verdicts (after the tests): GPU for the classes whose every test agreed (under 1% of the silhouette)
static void skin_gpu_decide(void (*log)(const char *, ...)) {
	for (int c = 0; c < SKN_N; c++) {
		int on = skin_gpu_force >= 0 ? skin_gpu_force : skin_probe_ok == 1 && skin_gpu_mis[c] >= 0 && skin_gpu_mis[c] < 0.01f;   // (b41: and skinprobe.h's palette probe)
		skin_gpu_set(c, on);
		log("skin gpu: %s skinned on the %s (%s)\n", skn_name[c], on ? "GPU" : "CPU (skin.h, NEON on the worker cores)",
			skin_gpu_force >= 0 ? "forced by chrskin.txt" : skin_gpu_mis[c] < 0 ? "not tested" : skin_probe_ok != 1 && skin_gpu_mis[c] < 0.01f ? "the self-test agreed, the palette probe did not" :
			on ? "the self-test and the palette probe agreed" : "the self-test disagreed");
	}
}
// the per-second log line (main.c): skinning cost and which path each class uses
static void skin_log(void (*log)(const char *, ...), int frames) {
	float wall, work, verts, inst, items; skin_stats(frames, &wall, &work, &verts, &inst, &items);
	log("  skinning: %.2f ms/frame on the main thread (%.2f ms of work on %d workers + main: %.3f us/vertex), %.0f vertices, %.1f instances, %.0f job items/frame; chars %s, models %s, viewmodel %s%s\n",
		wall, work, jobs_nworkers(), verts > 0 ? work * 1000 / verts : 0, verts, inst, items, chr_gpu_skin ? "GPU" : "CPU", md_gpu_skin ? "GPU" : "CPU", md_gpu_vm ? "GPU" : "CPU", skp_full ? ", OUTPUT POOL FULL (draws skipped)" : "");
	skp_full = 0;
}
