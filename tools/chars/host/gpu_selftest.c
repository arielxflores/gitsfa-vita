// Host check of skin_gpu.h's start-up self-test (chars b35): the GPU palette path vs the CPU path offscreen, with the
// host rasterizer running the shader's maths correctly (BIMODE unset: the test must pass) and with device-bug models
// (BIMODE=1..6 in vitaGL.h: indices read as 0, as i/255, rows past BILIM zero, shared per vertex group, transposed,
// wrapping): the test must fail and keep the CPU path. Build: gcc -O2 -w -I. -Istub -o gpu_selftest gpu_selftest.c -lm -lpthread
//   ./gpu_selftest chr_bato.gvm chr_anim_m.gab vm_m4a1.gvm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#define SKIN_GPU_HOST 1
#include "vitaGL.h"
#include "../../../engine/src/model.h"
#include "../../../engine/src/chr.h"
#include "../../../engine/src/skin_gpu.h"
static GLuint mk(GLenum t, const char *s) { (void)s; return glCreateShader(t); }
static void lg(const char *f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); }
int main(int argc, char **argv) {
	if (argc < 4) return 1;
	model_shader(mk, lg); jobs_init(); skin_probe_ok = getenv("PROBE_BAD") ? 0 : 1;   // (the device probe, assumed)
	CBank B; if (cb_load(&B, argv[2])) return 1;
	Model M; if (model_load(&M, argv[1]) < 0) return 1; CBind bind; cb_bind(&bind, &B, &M);
	Model V; int vm = model_load(&V, argv[3]) == 0;
	for (int k = 0; k < 2; k++) {
		CAnim A; ca_init(&A, &B, 0); CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1;
		if (k == 0) { in.vf = 335; in.vr = 120; in.zoom = 1; in.aim_pitch = 0.35f; } else { in.vf = -175; in.crouch = 1; in.zoom = 1; in.aim_pitch = -0.3f; }
		for (int f = 0; f < 24; f++) ca_update(&A, &in, 1 / 30.f);
		MBone pm[64]; cb_to_model(&bind, ca_pose(&A, &in), pm);
		float cd, mis = skin_gpu_test(&M, pm, 0, &cd); skin_gpu_note(SKN_CHR, k ? "crouch-walking back" : "running, aiming up", mis, cd, lg);
	}
	if (vm) { int ci = model_find_clip(&V, "sprint"); if (ci < 0) ci = 0; MBone pm[64]; model_sample(&V, ci, model_clip_len(&V, ci) * 0.4f, pm);
		float cd, mis = skin_gpu_test(&V, pm, 1, &cd); skin_gpu_note(SKN_VM, "clip sprint", mis, cd, lg); }
	skin_gpu_decide(lg);
	h_save(getenv("OUT") ? getenv("OUT") : "/dev/null");
	return 0;
}
