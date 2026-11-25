// Host test of the viewmodel pose (engine/src/wpn_pose.h) with the real vm_<short>.gvm files, driven like main.c drives
// it (aim ramp 0.2 s smoothstep, base clip choice, shots), against the old anim_pose. Per frame: the left / right hand's
// distance from where the idle holds them on the gun (cm), and the jump of the gun's on-screen position between frames
// (px, the engine's viewmodel projection lerped by ads) - a "pop" when the pose and the projection disagree.
// build + run: tools/weapons/pose_host.sh
#include "vitaGL.h"
#include <math.h>
typedef struct { char name[48]; } Snd; static Snd snds[1]; static int nsnd;
#include "../../engine/src/model.h"
#include "../../engine/src/weapon.h"
#include "../../engine/src/wpn_pose.h"
static void persp(float *m, float fovy, float asp, float n, float f) { float t = 1 / tanf(fovy / 2); memset(m, 0, 64); m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f); }
static void relp(const MBone *P, int a, int b, float *o) { float d[3] = { P[b].p[0] - P[a].p[0], P[b].p[1] - P[a].p[1], P[b].p[2] - P[a].p[2] }, q[4] = { -P[a].q[0], -P[a].q[1], -P[a].q[2], P[a].q[3] }; quat_rotate(q, d, o); }
int main(int argc, char **argv) {
	const char *dir = argc > 1 ? argv[1] : "build/data/";
	char p[256]; snprintf(p, sizeof(p), "%sweapons.txt", dir); weapons_load_table(p);
	for (int d = 0; d < nwdef; d++) {
		const WDef *D = &wdef[d]; if (D->kind == WK_KNIFE || D->kind == WK_GRENADE) continue;
		WSlot W; memset(&W, 0, sizeof(W)); if (wslot_load_ptr(&W, d, dir) < 0) { printf("%s: load failed\n", D->sh); continue; }
		int pb = 0; for (int i = 0; i < W.m.nmesh; i++) if (W.m.mesh[i].pbase > pb) pb = W.m.mesh[i].pbase;
		int g = W.grip, L = model_find_bone(&W.m, "Bip01_L_Hand"), R = model_find_bone(&W.m, "Bip01_R_Hand");
		MBone ref[64]; model_sample(&W.m, W.c[CL_IDLE], 0, ref); float rl[3], rr[3]; relp(ref, g, L, rl); relp(ref, g, R, rr);
		for (int mode = 0; mode < 2; mode++) {   // 0 = old anim_pose, 1 = wpn_vm_pose
			anim_init(&W.a, &W.m); anim_set_base(&W.a, W.c[CL_IDLE], 0, 0);
			float tj = 0, ads_lin = 0, ads = 0, cd = 0, dt = 1 / 60.f, maxl = 0, maxr = 0, maxjump = 0, prev[2] = { -1, -1 }; int was_aim = -1;
			for (int f = 0; f < 60 * 4; f++) {
				float t = f * dt;
				int aim = (t > 0.5f && t < 1.6f) || (t > 2.15f && t < 3.0f), trig = (t > 0.8f && t < 1.9f) || (t > 2.0f && t < 2.4f) || (t > 2.8f && t < 3.3f);
				float tg = aim ? 1.f : 0.f; ads_lin += (tg > ads_lin ? 1 : -1) * fminf(fabsf(tg - ads_lin), dt / 0.2f); ads = ads_lin * ads_lin * (3 - 2 * ads_lin);
				if (aim != was_aim) { if (aim) anim_set_base(&W.a, W.c[CL_ADS_IDLE], 0.15f, 0); else anim_set_base(&W.a, W.c[CL_IDLE], 0.3f, 0); was_aim = aim; }
				cd -= dt; if (trig && cd <= 0) { cd = D->kind == WK_AUTO ? 60.f / D->rpm : 0.35f; anim_play(&W.a, ads > 0.5f && W.c[CL_FIRE_ADS] >= 0 ? W.c[CL_FIRE_ADS] : W.c[CL_FIRE], 0.03f, 0.15f, 0); }
				anim_update(&W.a, dt);
				MBone *P = mode ? wpn_vm_pose(&W, ads) : anim_pose(&W.a, -1);
				float l[3], r[3]; relp(P, g, L, l); relp(P, g, R, r);
				float dl = sqrtf((l[0] - rl[0]) * (l[0] - rl[0]) + (l[1] - rl[1]) * (l[1] - rl[1]) + (l[2] - rl[2]) * (l[2] - rl[2])), dr = sqrtf((r[0] - rr[0]) * (r[0] - rr[0]) + (r[1] - rr[1]) * (r[1] - rr[1]) + (r[2] - rr[2]) * (r[2] - rr[2]));
				if (dl > maxl) maxl = dl; if (dr > maxr) maxr = dr;
				// gun on screen: the Grip through the viewmodel projection (main.c: fovy and offset lerped by ads)
				float th = tanf(D->hip_vfov * 0.5f * 3.14159f / 180) * (1 - ads) + tanf(D->ads_vfov * 0.5f * 3.14159f / 180) * ads, PV[16]; persp(PV, 2 * atanf(th), 960.f / 544, 2, 400);
				float o[3]; for (int k = 0; k < 3; k++) o[k] = D->hip_off[k] * (1 - ads) + D->ads_off[k] * ads;
				float sg[3] = { 0, 0, 0 }, x[3]; if (W.mz) model_bone_xform(&P[g], W.mz, sg); else memcpy(sg, P[g].p, 12);
				for (int k = 0; k < 3; k++) x[k] = sg[k] + o[k];
				float cx = PV[0] * x[0] / -x[2], cy = PV[5] * x[1] / -x[2], sx = (cx + 1) * 480, sy = (1 - cy) * 272;
				if (prev[0] >= 0) { float j = hypotf(sx - prev[0], sy - prev[1]); if (j > maxjump) { maxjump = j; tj = t; } }
				prev[0] = sx; prev[1] = sy;
			}
			printf("%-9s %s  pal windows up to %2d | left hand off the gun max %5.1f cm, right %4.1f cm | muzzle on screen: biggest jump between frames %5.1f px (t %.2f)\n", D->sh, mode ? "new" : "old", pb, maxl, maxr, maxjump, tj);
			anim_free(&W.a);
		}
		model_free(&W.m);
	}
	return 0;
}
