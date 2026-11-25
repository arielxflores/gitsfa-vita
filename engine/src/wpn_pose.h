// Viewmodel pose helpers (needs model.h + weapon.h). Host test: tools/weapons/pose_host.c
// The viewmodel pose kept in step with the aim amount: main.c draws the arms + gun with a projection lerped by `ads`
// (viewmodel fovy and offset, weapons.txt), so the pose must use the same weight. The base layer is evaluated twice,
// once with the aimed clips swapped for their hip versions (ads_idle -> idle, aimed walk -> run) and once the other
// way round, and the two are mixed by `ads`; a shot clip is the hip / aimed fire clip mixed by `ads`. Before, the
// hip <-> aimed cross-fade ran on its own timer and a hip shot still playing when aiming began (or an aimed one when it
// ended) held the gun in the other pose while the projection had already changed: the "arms out of place after aiming
// and shooting" report. Other one-shot clips (reload, draw, jump ...) are layered as anim_pose does.
#pragma once
#include <math.h>
#include <string.h>

static int wpn_hipver(const WSlot *w, int c) {
	if (c < 0) return c;
	if (c == w->c[CL_ADS_IDLE]) return w->c[CL_IDLE];
	if (c == w->c[CL_WALK]) return w->c[CL_RUN] >= 0 ? w->c[CL_RUN] : w->c[CL_IDLE];   // (walk = the aimed walk)
	return c;
}
static int wpn_adsver(const WSlot *w, int c) {
	if (c < 0 || w->c[CL_ADS_IDLE] < 0) return c;
	if (c == w->c[CL_IDLE]) return w->c[CL_ADS_IDLE];
	if (c == w->c[CL_RUN]) return w->c[CL_WALK] >= 0 ? w->c[CL_WALK] : w->c[CL_ADS_IDLE];
	return c;
}
// b60: 1 when every clip that goes into wpn_vm_pose's pose now is one the model's light first-person meshes were made for
// (Model.fp_clips: the base clip, its hip / aimed versions, a clip fading out, the action; both fire clips for a shot)
static int wvm_fp_state(const WSlot *w) {
	const Model *M = &w->m; const Anim *a = &w->a; unsigned long long cm = M->fp_clips;
	if (!M->fp || a->base < 0) return 0;
#define WFPC(c) ((c) >= 0 && (c) < 64 && ((cm >> (c)) & 1))
	if (!WFPC(a->base) || !WFPC(wpn_hipver(w, a->base)) || !WFPC(wpn_adsver(w, a->base))) return 0;
	if (a->base_fade < 1 && a->base_prev >= 0 && (!WFPC(a->base_prev) || !WFPC(wpn_hipver(w, a->base_prev)) || !WFPC(wpn_adsver(w, a->base_prev)))) return 0;
	if (a->act >= 0 && a->act_w > 0) {
		int cf = w->c[CL_FIRE], ca = w->c[CL_FIRE_ADS];
		if (!WFPC(a->act)) return 0;
		if (cf >= 0 && ca >= 0 && (a->act == cf || a->act == ca) && (!WFPC(cf) || !WFPC(ca))) return 0;
	}
#undef WFPC
	return 1;
}
#define WPN_PNB 96
static MBone wpn_pbuf[4][WPN_PNB];
// the base layer (with its cross-fade) with every clip mapped by ver (0 hip, 1 aimed) -> out
static void wpn_base_layer(const WSlot *w, int ver, MBone *out, MBone *tmp) {
	const Anim *a = &w->a; const Model *M = a->m;
	int b = ver ? wpn_adsver(w, a->base) : wpn_hipver(w, a->base); if (b < 0) b = a->base;
	model_sample(M, b, a->base_t, out);
	if (a->base_fade < 1 && a->base_prev >= 0) {
		int p = ver ? wpn_adsver(w, a->base_prev) : wpn_hipver(w, a->base_prev); if (p < 0) p = a->base_prev;
		float f = a->base_fade; model_sample(M, p, a->base_prev_t, tmp); model_blend(M->nb, tmp, out, f * f * (3 - 2 * f), NULL); memcpy(out, tmp, M->nb * sizeof(MBone));
	}
}
static MBone *wpn_vm_pose(WSlot *w, float ads) {
	Anim *a = &w->a; const Model *M = a->m;
	if (!M || M->nb > WPN_PNB || a->base < 0) return anim_pose(a, -1);
	float k = fmaxf(0.f, fminf(1.f, ads)); MBone *P = wpn_pbuf[0], *A = wpn_pbuf[1], *T = wpn_pbuf[2], *S = wpn_pbuf[3];
	if (w->c[CL_ADS_IDLE] < 0) k = 0;
	if (k < 1) wpn_base_layer(w, 0, P, T);
	if (k > 0) { wpn_base_layer(w, 1, k < 1 ? A : P, T); if (k < 1) model_blend(M->nb, P, A, k, NULL); }
	if (a->act >= 0 && a->act_w > 0) {
		int cf = w->c[CL_FIRE], ca = w->c[CL_FIRE_ADS];
		if (cf >= 0 && ca >= 0 && (a->act == cf || a->act == ca)) {   // the shot: hip and aimed fire clips mixed by ads
			model_sample(M, cf, a->act_t, S); model_sample(M, ca, a->act_t, T); model_blend(M->nb, S, T, k, NULL);
		} else model_sample(M, a->act, a->act_t, S);
		model_blend(M->nb, P, S, a->act_w, NULL);
	}
	return P;
}
