// Host check of the first-person viewmodel draw (wpn_vm.h wvm_draw) after chars b35: CPU skinning (model.h / skin.h)
// against the shader's maths (the GPU palette path, md_gpu_vm = 1) for every weapon in 8 states (as
// tools/weapons/vmdraw_host.c poses them). Prints the silhouette mismatch per state; writes PPM sheets (top row: CPU
// path, bottom: GPU maths) for each weapon. Build: gcc -O2 -w -I. -Istub -o vm_test vm_test.c -lm -lpthread
//   ./vm_test build/data/ out_dir
#include "vitaGL.h"
#include <math.h>
typedef struct { char name[48]; } Snd; static Snd snds[1]; static int nsnd;
#include "../../../engine/src/model.h"
#include "../../../engine/src/weapon.h"
#include "../../../engine/src/wpn_pose.h"
#include "../../../engine/src/wpn_vm.h"
static void persp(float *m, float fovy, float asp, float n, float f) { float t = 1 / tanf(fovy / 2); memset(m, 0, 64); m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f); }
static void mul4(float *o, const float *a, const float *b) { float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; } memcpy(o, r, 64); }
typedef struct { const char *name; int base, act; float t, ads; } St;
static GLuint mk(GLenum t, const char *s) { (void)s; return glCreateShader(t); }
static void lg(const char *f, ...) { (void)f; }
static float shot[2][SW_H][SW_W][3];
int main(int argc, char **argv) {
	const char *dir = argc > 1 ? argv[1] : "build/data/", *out = argc > 2 ? argv[2] : "/tmp";
	char p[256]; snprintf(p, sizeof(p), "%sweapons.txt", dir); weapons_load_table(p);
	model_shader(mk, lg);
	jobs_init(); float worst = 0;
	for (int d = 0; d < nwdef; d++) {
		const WDef *D = &wdef[d]; WSlot W; memset(&W, 0, sizeof(W)); if (wslot_load_ptr(&W, d, dir) < 0) { printf("%s: load failed\n", D->sh); continue; }
		St st[8] = { { "idle", CL_IDLE, -1, 0.3f, 0 }, { "run", CL_RUN, -1, 0.25f, 0 }, { "sprint", CL_SPRINT, -1, 0.2f, 0 }, { "ads", CL_ADS_IDLE, -1, 0.3f, 1 },
			{ "fire_ads", CL_ADS_IDLE, CL_FIRE_ADS, 0.08f, 1 }, { "reload", CL_IDLE, CL_RELOAD, 1.0f, 0 }, { "draw", CL_IDLE, CL_DRAW, 0.3f, 0 }, { "vault", CL_RUN, CL_VAULT, 0.3f, 0 } };
		if (D->kind == WK_KNIFE) { st[3].base = CL_IDLE; st[3].act = CL_FIRE; st[3].name = "slash"; st[4].act = CL_FIRE2; st[4].base = CL_IDLE; st[4].name = "slash2"; st[3].ads = st[4].ads = 0; st[5].act = CL_CQC; st[5].t = 0.6f; st[5].name = "stealth"; }
		if (D->kind == WK_GRENADE) { st[3].base = CL_IDLE; st[3].act = CL_COOK; st[3].name = "cook"; st[4].base = CL_IDLE; st[4].act = CL_FIRE; st[4].name = "throw"; st[3].ads = st[4].ads = 0; st[5].act = CL_HOLSTER; st[5].t = 0.1f; st[5].name = "holster"; }
		printf("%-9s", D->sh);
		static unsigned char sheet[2 * SW_H / 4][8 * SW_W / 4][3];
		for (int s = 0; s < 8; s++) {
			St *S = &st[s]; anim_init(&W.a, &W.m); int b = W.c[S->base] >= 0 ? W.c[S->base] : W.c[CL_IDLE]; anim_set_base(&W.a, b, 0, 0);
			if (S->act >= 0 && W.c[S->act] >= 0) anim_play(&W.a, W.c[S->act], 0.01f, 0.01f, 0);
			for (int k = 0; k < 6; k++) anim_update(&W.a, S->t / 6);
			float ads = S->ads; MBone *P = wpn_vm_pose(&W, ads);
			float th = tanf(D->hip_vfov * 0.5f * 3.14159f / 180) * (1 - ads) + tanf(D->ads_vfov * 0.5f * 3.14159f / 180) * ads, PV[16], VMVP[16]; persp(PV, 2 * atanf(th), 1, 2, 400);
			float ox = D->hip_off[0] * (1 - ads) + D->ads_off[0] * ads, oy = D->hip_off[1] * (1 - ads) + D->ads_off[1] * ads, oz = D->hip_off[2] * (1 - ads) + D->ads_off[2] * ads;
			float K[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, ox, oy, oz, 1 }; mul4(VMVP, PV, K);
			const float ld[4] = { -0.35f, 0.75f, 0.56f, 0 }, tint[4] = { 1, 1, 1, 1 };
			for (int g = 0; g < 2; g++) { md_gpu_vm = g; skin_host_frame++; h_clear(0.45f, 0.5f, 0.56f); wvm_draw(&W.m, VMVP, P, ld, tint, ads, NULL); memcpy(shot[g], hfb, sizeof(hfb)); }
			long cov = 0, mis = 0;
			for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) { int a = fabsf(shot[0][y][x][0] - 0.45f) + fabsf(shot[0][y][x][2] - 0.56f) > 0.01f, c = fabsf(shot[1][y][x][0] - 0.45f) + fabsf(shot[1][y][x][2] - 0.56f) > 0.01f; cov += a | c; mis += a ^ c; }
			float f = cov ? (float)mis / cov : 0; if (f > worst) worst = f;
			printf(" | %s %.2f%%", S->name, 100 * f);
			for (int g = 0; g < 2; g++) for (int y = 0; y < SW_H / 4; y++) for (int x = 0; x < SW_W / 4; x++) for (int k = 0; k < 3; k++) { float v = shot[g][y * 4][x * 4][k]; sheet[g * SW_H / 4 + y][s * SW_W / 4 + x][k] = (unsigned char)(fminf(1, fmaxf(0, v)) * 255); }
			anim_free(&W.a);
		}
		printf("\n");
		snprintf(p, sizeof(p), "%s/vm_%s.ppm", out, D->sh); FILE *fo = fopen(p, "wb"); fprintf(fo, "P6 %d %d 255\n", 8 * SW_W / 4, 2 * SW_H / 4); fwrite(sheet, 1, sizeof(sheet), fo); fclose(fo);
		model_free(&W.m);
	}
	printf("worst silhouette mismatch CPU vs GPU maths: %.3f%%\n", worst * 100);
	return worst > 0.01f;
}
