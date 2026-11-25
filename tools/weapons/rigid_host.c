// Host check of the viewmodel clips (b55, user: the SEBURO M5's barrel came off the gun and turned right when moving in
// ADS): for every gun, every bone's pose relative to the Grip bone is compared, frame by frame, with the same relation in
// the idle clip's first frame. The gun's own parts (no arm / hand / finger bones) must stay put in the base clips (idle,
// walk = the aimed walk, run, sprint, ADS idle); fire / reload / draw clips may move a slide or a magazine.
// Prints the worst offset (cm) and turn (degrees) per clip and bone above the thresholds.
// build + run: gcc -O1 -w -Itools/weapons/host -o /tmp/rigid tools/weapons/rigid_host.c -lm && /tmp/rigid build/data/
#include "vitaGL.h"
#include <math.h>
typedef struct { char name[48]; } Snd; static Snd snds[1]; static int nsnd;
#include "../../engine/src/model.h"
#include "../../engine/src/weapon.h"
static void rel(const MBone *P, int a, int b, float *o, float *q) {
	float d[3] = { P[b].p[0] - P[a].p[0], P[b].p[1] - P[a].p[1], P[b].p[2] - P[a].p[2] }, iq[4] = { -P[a].q[0], -P[a].q[1], -P[a].q[2], P[a].q[3] };
	quat_rotate(iq, d, o);
	// q = iq * P[b].q
	const float *x = iq, *y = P[b].q;
	q[0] = x[3]*y[0] + x[0]*y[3] + x[1]*y[2] - x[2]*y[1]; q[1] = x[3]*y[1] - x[0]*y[2] + x[1]*y[3] + x[2]*y[0];
	q[2] = x[3]*y[2] + x[0]*y[1] - x[1]*y[0] + x[2]*y[3]; q[3] = x[3]*y[3] - x[0]*y[0] - x[1]*y[1] - x[2]*y[2];
}
static int is_body(const char *n) { return strstr(n, "Bip01") || strstr(n, "Hand") || strstr(n, "Finger") || strstr(n, "Arm") || strstr(n, "Clavicle") || strstr(n, "Spine") || strstr(n, "Neck") || strstr(n, "Head") || strstr(n, "Camera") || strstr(n, "camera"); }
#include <stdarg.h>
static void hlog(const char *f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); }
int main(int argc, char **argv) {
	wvm_log = hlog;
	const char *dir = argc > 1 ? argv[1] : "build/data/"; int verbose = argc > 2;
	char p[256]; snprintf(p, sizeof(p), "%sweapons.txt", dir); weapons_load_table(p);
	static const char *CN[CL_N] = { 0 }; CN[CL_IDLE] = "idle"; CN[CL_WALK] = "ads_walk"; CN[CL_RUN] = "run"; CN[CL_SPRINT] = "sprint"; CN[CL_ADS_IDLE] = "ads_idle"; CN[CL_FIRE] = "fire"; CN[CL_FIRE_ADS] = "fire_ads"; CN[CL_RELOAD] = "reload"; CN[CL_DRAW] = "draw";
	int bad = 0;
	for (int d = 0; d < nwdef; d++) {
		const WDef *D = &wdef[d]; if (D->kind == WK_KNIFE || D->kind == WK_GRENADE) continue;
		WSlot W; memset(&W, 0, sizeof(W)); if (wslot_load_ptr(&W, d, dir) < 0) { printf("%s: load failed\n", D->sh); continue; }
		const Model *M = &W.m; int g = W.grip; if (g < 0) { printf("%s: no Grip\n", D->sh); continue; }
		if (verbose) { printf("%s bones:", D->sh); for (int b = 0; b < M->nb; b++) printf(" %d:%s", b, M->bone_name[b]); printf("\n"); }
		MBone ref[128], P[128]; model_sample(M, W.c[CL_IDLE], 0, ref);
		float ro[128][3], rq[128][4]; for (int b = 0; b < M->nb; b++) rel(ref, g, b, ro[b], rq[b]);
		for (int ci = 0; ci < CL_N; ci++) { int c = W.c[ci]; if (c < 0 || !CN[ci]) continue; int base = ci == CL_IDLE || ci == CL_WALK || ci == CL_RUN || ci == CL_SPRINT || ci == CL_ADS_IDLE;
			const MClip *C = &M->clip[c]; float len = C->nf / (C->fps > 0 ? C->fps : 30);
			for (int b = 0; b < M->nb; b++) { if (b == g || is_body(M->bone_name[b])) continue;
				float wo = 0, wa = 0, wt = 0;
				for (int f = 0; f < C->nf; f++) { model_sample(M, c, f / (C->fps > 0 ? C->fps : 30), P); float o[3], q[4]; rel(P, g, b, o, q);
					float dd = sqrtf((o[0]-ro[b][0])*(o[0]-ro[b][0]) + (o[1]-ro[b][1])*(o[1]-ro[b][1]) + (o[2]-ro[b][2])*(o[2]-ro[b][2]));
					float dot = fabsf(q[0]*rq[b][0] + q[1]*rq[b][1] + q[2]*rq[b][2] + q[3]*rq[b][3]); if (dot > 1) dot = 1; float ang = 2 * acosf(dot) * 57.29578f;
					if (dd > wo) { wo = dd; wt = f / (C->fps > 0 ? C->fps : 30); } if (ang > wa) wa = ang; }
				if (wo > 0.5f || wa > 3) { printf("%s%-10s %-9s %-24s offset %6.2f cm  turn %6.1f deg  (worst at %.2f of %.2f s)\n", base ? "BAD " : "    ", D->sh, CN[ci], M->bone_name[b], wo, wa, wt, len); if (base) bad++; }
			}
		}
	}
	printf("%d base-clip gun parts out of place\n", bad);
	return 0;
}
