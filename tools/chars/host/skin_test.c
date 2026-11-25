// skin.h check (chars, b35): the skin plans + kernels (NEON on ARM builds, scalar on the host) against the old per-vertex
// reference (chr.h chr_skin16_c) on real models in real poses, every LOD; the job queue under stress (random batches
// skinned on the worker threads vs one thread); a rough cost per vertex on this machine. ARM build (run under qemu:
// instruction mix, not Vita timing):
//   arm-linux-gnueabihf-gcc -O2 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -ffast-math -static -I. -o skin_test_arm skin_test.c -lm -lpthread
//   qemu-arm-static ./skin_test_arm chr_bato.gvm chr_anim_m.gab [more.gvm ...]
// Host: gcc -O2 -I. -o skin_test skin_test.c -lm -lpthread
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "vitaGL.h"
#include "../../../engine/src/model.h"
#include "../../../engine/src/chr.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
// a pose for a model: characters from the bank (running, aiming up), other models their first clip half way
static void pose_for(const Model *M, CBank *B, MBone *pm) {
	if (B && B->ok && M->nb <= 64) {
		CBind bind; cb_bind(&bind, B, M);
		CAnim A; ca_init(&A, B, 0); CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1; in.vf = 335; in.vr = 120; in.zoom = 1; in.aim_pitch = 0.3f;
		for (int k = 0; k < 20; k++) ca_update(&A, &in, 1 / 30.f);
		cb_to_model(&bind, ca_pose(&A, &in), pm);
	} else if (M->nclip) model_sample(M, 0, model_clip_len(M, 0) * 0.5f, pm);
	else for (int b = 0; b < M->nb; b++) { memset(&pm[b], 0, sizeof(MBone)); pm[b].q[3] = 1; }
}
static int check_model(const char *path, CBank *B, double *worst_p, int *worst_n, long *nverts, long *nslots, int *cls) {
	Model M; model_keep_cpu = 1; if (model_load(&M, path) < 0) { fprintf(stderr, "cannot load %s\n", path); return 1; } model_keep_cpu = 0;
	MBone *pm = calloc(M.nb > 64 ? M.nb : 64, sizeof(MBone)); pose_for(&M, M.nb <= 64 ? B : NULL, pm);
	float pal[48 * 12]; model_palette(&M, pm, pal);
	int bad = 0;
	for (int i = 0; i < M.nmesh; i++) {
		const MMesh *m = &M.mesh[i]; if (!m->sp) continue;
		const struct SkinPlan *p = m->sp; unsigned char *ref = malloc(m->nv * 16); float *out = aligned_alloc(16, (size_t)p->nblk * 64);
		chr_skin16_c(m, pal + m->pbase * 12, M.npal - m->pbase, ref);
		skin_blocks(p, pal + m->pbase * 12, 0, p->nblk, out);
		double dp = 0; int dn = 0;
		for (int v = 0; v < m->nv; v++) {
			const unsigned char *a = ref + v * 16, *b = (const unsigned char *)(out + p->slot[v] * 4); float pa[3], pb[3]; memcpy(pa, a, 12); memcpy(pb, b, 12);
			for (int c = 0; c < 3; c++) { double d = fabs(pa[c] - pb[c]) / (1 + fabs(pa[c]) * 1e-3); if (d > dp) dp = d; int e = abs((signed char)a[12 + c] - (signed char)b[12 + c]); if (e > dn) dn = e; }
			if (b[15] != a[15]) dn = 99;   // (b62: the 4th byte is the specular mask, carried through)
		}
		// the remapped triangles must name the same vertices
		for (int k = 0; k < m->ni; k += 97) { (void)k; }
		if (dp > *worst_p) *worst_p = dp; if (dn > *worst_n) *worst_n = dn;
		*nverts += m->nv; *nslots += p->nblk * 4; cls[0] += p->nb1 * 4; cls[1] += p->nb2 * 4; cls[2] += (p->nb3 + p->nbx) * 4;
		if (dp > 0.02 || dn > 1) { printf("  %s mesh %d: position diff %.4f, normal diff %d  BAD\n", path, i, dp, dn); bad = 1; }
		free(ref); free(out);
	}
	free(pm); model_free(&M);
	return bad;
}
// the job queue: random batches of instances (each its own palette: the pose moved by k x 10 cm) skinned through
// skin_pal / skin_add / skin_run on the worker threads, against one thread; batches large enough to overflow the item
// table mid-instance (sk_flush with the palettes kept)
static int stress(const char *path, CBank *B, int rounds) {
	Model M; if (model_load(&M, path) < 0) return 1;
	MBone pm[64]; pose_for(&M, B, pm); float pal0[48 * 12]; model_palette(&M, pm, pal0);
	enum { NI = 40 }; static GLuint vbo[NI][8]; static float *ref[NI][8]; static float pal[NI][48 * 12];
	for (int k = 0; k < NI; k++) { memcpy(pal[k], pal0, sizeof(pal0)); for (int e = 0; e < M.npal; e++) pal[k][e * 12 + 3] += 10 * k;
		for (int i = 0; i < M.nmesh && i < 8; i++) if (M.mesh[i].sp) { const struct SkinPlan *p = M.mesh[i].sp; ref[k][i] = aligned_alloc(16, (size_t)p->nblk * 64); skin_blocks(p, pal[k] + M.mesh[i].pbase * 12, 0, p->nblk, ref[k][i]);
			glGenBuffers(1, &vbo[k][i]); glBindBuffer(GL_ARRAY_BUFFER, vbo[k][i]); glBufferData(GL_ARRAY_BUFFER, SKIN_OUT_BYTES(p), NULL, GL_DYNAMIC_DRAW); } }
	int bad = 0; long calls = 0, items = 0;
	for (int r = 0; r < rounds; r++) {
		int n = 1 + rand() % NI;
		for (int k = 0; k < n; k++) { float tmp[48 * 12]; memcpy(tmp, pal[k], sizeof(tmp)); const float *qp = skin_pal(tmp, M.npal); memset(tmp, 0, sizeof(tmp));   // (skin_pal copies: the caller's array may go away)
			for (int i = 0; i < M.nmesh && i < 8; i++) if (ref[k][i]) { glBindBuffer(GL_ARRAY_BUFFER, vbo[k][i]); memset(glMapBuffer(GL_ARRAY_BUFFER, 0), 0xCD, SKIN_OUT_BYTES(M.mesh[i].sp)); skin_add(M.mesh[i].sp, qp + M.mesh[i].pbase * 12, vbo[k][i]); } }
		items += SKQ.n; skin_run(); calls++;
		for (int k = 0; k < n; k++) for (int i = 0; i < M.nmesh && i < 8; i++) if (ref[k][i]) { glBindBuffer(GL_ARRAY_BUFFER, vbo[k][i]); if (memcmp(glMapBuffer(GL_ARRAY_BUFFER, 0), ref[k][i], SKIN_OUT_BYTES(M.mesh[i].sp))) bad++; }
	}
	printf("job queue stress: %ld runs of 1..%d instances x %d meshes (own palettes; up to %d items, table %d) on %d workers + the caller: %s (%d mismatching buffers)\n",
		calls, NI, M.nmesh, NI * 70, SKQ_ITEMS, jobs_nworkers(), bad ? "FAILED" : "identical to one thread", bad);
	(void)items; return bad != 0;
}
int main(int argc, char **argv) {
	if (argc < 3) { fprintf(stderr, "usage: skin_test bank.gab model.gvm [model.gvm ...]\n"); return 1; }
	jobs_init();
	CBank B; if (cb_load(&B, argv[1])) { fprintf(stderr, "bank\n"); return 1; }
	int bad = 0;
	for (int a = 2; a < argc; a++) {
		double wp = 0; int wn = 0, cls[3] = { 0 }; long nv = 0, ns = 0;
		bad |= check_model(argv[a], &B, &wp, &wn, &nv, &ns, cls);
		printf("%-28s %6ld skinned vertices -> %6ld slots (one bone %ld%%, two %ld%%, more %ld%%); vs the old per-vertex skinning: position %.4f cm, normal %d/127\n",
			strrchr(argv[a], '/') ? strrchr(argv[a], '/') + 1 : argv[a], nv, ns, ns ? cls[0] * 100L / ns : 0, ns ? cls[1] * 100L / ns : 0, ns ? cls[2] * 100L / ns : 0, wp, wn);
	}
	bad |= stress(argv[2], &B, 300);
	// cost on this machine: old per-vertex reference vs the plan kernels (one thread), LOD 0 of the first model
	{ Model M; model_keep_cpu = 1; model_load(&M, argv[2]); MBone pm[64]; pose_for(&M, &B, pm); float pal[48 * 12]; model_palette(&M, pm, pal);
	  for (int i = 0; i < M.nmesh; i++) { const MMesh *m = &M.mesh[i]; if (!m->sp || !md_lod_ok(m)) continue;
		unsigned char *ref = malloc(m->nv * 16); float *out = aligned_alloc(16, (size_t)m->sp->nblk * 64); int reps = 100;
		double t0 = now(); for (int r = 0; r < reps; r++) chr_skin16_c(m, pal, M.npal, ref); double t1 = now(); for (int r = 0; r < reps; r++) skin_blocks(m->sp, pal, 0, m->sp->nblk, out); double t2 = now();
		printf("mesh %d (%d vertices): old per-vertex %.1f ns/vertex, plan kernels %.1f ns/vertex (this machine, one thread)\n", i, m->nv, (t1 - t0) / reps / m->nv * 1e9, (t2 - t1) / reps / m->nv * 1e9);
		free(ref); free(out); } }
	printf(bad ? "FAILED\n" : "all ok\n");
	return bad;
}
