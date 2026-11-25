// Host test of the merged engine's first-person draw path: weapon.h loads vm_<short>.gvm (palette windows in the
// mesh blend word), wpn_pose.h wpn_vm_pose() poses it as main.c drives it, wpn_vm.h wvm_draw() draws it with main.c's
// viewmodel projection (vm fovy + offset lerped by ads, near 2 cm) through a software GL that runs model.h's MD_VS
// maths (tools/weapons/sgl/vitaGL.h). Arms (meshes 0-1) orange, gun blue. One PPM per weapon with 8 states, and a
// line per weapon: triangles drawn, non-finite vertices, arm / gun pixels per state (0 arm pixels where hands belong
// = missing arms). build + run: tools/weapons/vmdraw_host.sh [out dir]
#include "vitaGL.h"
#include <math.h>
typedef struct { char name[48]; } Snd; static Snd snds[1]; static int nsnd;
#include "../../engine/src/model.h"
#include "../../engine/src/weapon.h"
#include "../../engine/src/wpn_pose.h"
#include "../../engine/src/wpn_vm.h"
static void persp(float *m, float fovy, float asp, float n, float f) { float t = 1 / tanf(fovy / 2); memset(m, 0, 64); m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f); }
static void mul4(float *o, const float *a, const float *b) { float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; } memcpy(o, r, 64); }
typedef struct { const char *name; int base, act; float t, ads; } St;
static unsigned char sheet[SGL_H / 2 * 2][SGL_W / 2 * 4][3];
int main(int argc, char **argv) {
	const char *dir = argc > 1 ? argv[1] : "build/data/", *out = argc > 2 ? argv[2] : "/tmp";
	char p[256]; snprintf(p, sizeof(p), "%sweapons.txt", dir); weapons_load_table(p);
	md_u_m[0] = 1; md_u_m[1] = 2; md_u_m[2] = 3; md_u_m[3] = 4; md_u_pal = 5; md_u_ldir = 6; md_u_tint = 7; md_u_fog = 8; md_u_fogp = 9;
	md_gpu_vm = 1;   // (chars b35: wvm_draw skins on the CPU by default; this test checks the poses through the shader's maths. CPU path: tools/chars/host/vm_test.c)
	for (int d = 0; d < nwdef; d++) {
		const WDef *D = &wdef[d]; WSlot W; memset(&W, 0, sizeof(W)); if (wslot_load_ptr(&W, d, dir) < 0) { printf("%s: load failed\n", D->sh); continue; }
		for (int i = 0; i < W.m.nmesh; i++) sgl_vbo_class[W.m.mesh[i].vbo] = i < 2 ? 1 : 2;
		St st[8] = { { "idle", CL_IDLE, -1, 0.3f, 0 }, { "run", CL_RUN, -1, 0.25f, 0 }, { "sprint", CL_SPRINT, -1, 0.2f, 0 }, { "ads", CL_ADS_IDLE, -1, 0.3f, 1 },
			{ "fire_ads", CL_ADS_IDLE, CL_FIRE_ADS, 0.08f, 1 }, { "reload", CL_IDLE, CL_RELOAD, 1.0f, 0 }, { "draw", CL_IDLE, CL_DRAW, 0.3f, 0 }, { "vault", CL_RUN, CL_VAULT, 0.3f, 0 } };
		{ const char *e = getenv("VMST");   // b55: VMST="base,act,t,ads;..." (CL_* numbers, act -1 = none): 8 custom states
		  for (int k = 0; e && *e && k < 8; k++) { int b_, a_; float t_, s_; if (sscanf(e, "%d,%d,%f,%f", &b_, &a_, &t_, &s_) == 4) { st[k].base = b_; st[k].act = a_; st[k].t = t_; st[k].ads = s_; st[k].name = "custom"; } e = strchr(e, ';'); if (!e) break; e++; } }
		if (getenv("VMW") && strcmp(getenv("VMW"), D->sh)) { model_free(&W.m); continue; }
#ifdef ADSWALK
		{ St aw = { "ads_walk", CL_WALK, -1, ADSWALK, 1 }; st[7] = aw; }   // b55: the aimed walk (SEBURO M5 report)
#endif
		if (D->kind == WK_KNIFE) { st[3].base = CL_IDLE; st[3].act = CL_FIRE; st[3].name = "slash"; st[4].act = CL_FIRE2; st[4].base = CL_IDLE; st[4].name = "slash2"; st[3].ads = st[4].ads = 0; st[5].act = CL_CQC; st[5].t = 0.6f; st[5].name = "stealth"; }
		if (D->kind == WK_GRENADE) { st[3].base = CL_IDLE; st[3].act = CL_COOK; st[3].name = "cook"; st[4].base = CL_IDLE; st[4].act = CL_FIRE; st[4].name = "throw"; st[3].ads = st[4].ads = 0; st[5].act = CL_HOLSTER; st[5].t = 0.1f; st[5].name = "holster"; }
		printf("%-9s", D->sh); int bad0 = sgl_bad;
		for (int s = 0; s < 8; s++) {
			St *S = &st[s]; anim_init(&W.a, &W.m); int b = W.c[S->base] >= 0 ? W.c[S->base] : W.c[CL_IDLE]; anim_set_base(&W.a, b, 0, 0);
			if (S->act >= 0 && W.c[S->act] >= 0) anim_play(&W.a, W.c[S->act], 0.01f, 0.01f, 0);
			for (int k = 0; k < 6; k++) anim_update(&W.a, S->t / 6);   // (the base clip and the action advance together)
			float ads = S->ads; MBone *P = wpn_vm_pose(&W, ads);
			float th = tanf(D->hip_vfov * 0.5f * 3.14159f / 180) * (1 - ads) + tanf(D->ads_vfov * 0.5f * 3.14159f / 180) * ads, PV[16], VMVP[16]; persp(PV, 2 * atanf(th), 960.0f / 544, 2, 400);
			float ox = D->hip_off[0] * (1 - ads) + D->ads_off[0] * ads, oy = D->hip_off[1] * (1 - ads) + D->ads_off[1] * ads, oz = D->hip_off[2] * (1 - ads) + D->ads_off[2] * ads;
			float K[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, ox, oy, oz, 1 }; mul4(VMVP, PV, K);
			sgl_clear(0.45f, 0.5f, 0.56f); int t0 = sgl_tris; const float ld[4] = { -0.35f, 0.75f, 0.56f, 0 }, tint[4] = { 1, 1, 1, 1 };
			wvm_draw(&W.m, VMVP, P, ld, tint, ads, NULL);
			long arm = 0, gun = 0; for (int y = 0; y < SGL_H; y++) for (int x = 0; x < SGL_W; x++) { unsigned char *c = sgl_img[y][x]; if (c[0] > c[2] + 30) arm++; else if (c[2] > c[0] + 30) gun++; }
			printf(" | %s arm %4.1f%% gun %4.1f%%", S->name, 100.0 * arm / (SGL_W * SGL_H), 100.0 * gun / (SGL_W * SGL_H)); (void)t0;
			for (int y = 0; y < SGL_H / 2; y++) for (int x = 0; x < SGL_W / 2; x++) memcpy(sheet[(s / 4) * (SGL_H / 2) + y][(s % 4) * (SGL_W / 2) + x], sgl_img[y * 2][x * 2], 3);
			anim_free(&W.a);
		}
		printf(" | non-finite vertices %d\n", sgl_bad - bad0);
		snprintf(p, sizeof(p), "%s/vmdraw_%s.ppm", out, D->sh); FILE *f = fopen(p, "wb"); fprintf(f, "P6\n%d %d\n255\n", SGL_W / 2 * 4, SGL_H); fwrite(sheet, 1, sizeof(sheet), f); fclose(f);
		model_free(&W.m);
	}
	return 0;
}
