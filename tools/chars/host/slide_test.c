// Foot sliding of chr.h's locomotion: the character moves at a constant velocity (character frame) and the animator
// runs as in the game; for every frame the lower foot (the planted one) is followed in world space and its horizontal
// speed during contact is reported (0 = the foot stays put). Also the stride cadence (steps per second).
// usage: slide_test bank.gab set   (prints a table of speeds / directions / stances)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vitaGL.h"
#include "../../../engine/src/model.h"
#include "../../../engine/src/chr.h"
static void run(const CBank *B, int set, const char *label, float spd, float dirdeg, int crouch, int sprint) {
	CAnim A; ca_init(&A, B, set); CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1; in.crouch = crouch; in.sprint = sprint;
	float a = dirdeg * 3.14159265f / 180; in.vf = cosf(a) * spd; in.vr = sinf(a) * spd;
	const float dt = 1 / 60.f; float root[2] = { 0, 0 }, prev[2][3] = { { 0 } }; double slide = 0; int ns = 0, steps = 0, lowprev = -1; float T = 4;
	for (int f = 0; f < (int)(T / dt); f++) {
		ca_update(&A, &in, dt); MBone *P = ca_pose(&A, &in);
		// character frame: faces -y, right = -x; world = character frame moving with velocity (vf along -y, vr along -x)
		root[0] += -in.vr * dt; root[1] += -in.vf * dt;
		float fw[2][3]; static int fb[2] = { -1, -1 }; if (fb[0] < 0) { fb[0] = cb_find_bone(B, "Bip01_L_Toe0"); fb[1] = cb_find_bone(B, "Bip01_R_Toe0"); }
		for (int k = 0; k < 2; k++) { fw[k][0] = P[fb[k]].p[0] + root[0]; fw[k][1] = P[fb[k]].p[1] + root[1]; fw[k][2] = P[fb[k]].p[2]; }
		int low = fw[0][2] < fw[1][2] ? 0 : 1;
		for (int k = 0; k < 2; k++) if (f > 60 && fw[k][2] < 3 && prev[k][2] < 3) {   // a toe on the ground, moving back relative to the body (stance, not a low swing)
			float dx = fw[k][0] - prev[k][0], dy = fw[k][1] - prev[k][1], cx = dx + in.vr * dt, cy = dy + in.vf * dt;   // world and character-frame motion
			if (cx * (-in.vr) + cy * (-in.vf) >= 0) continue;
			slide += sqrtf(dx * dx + dy * dy) / dt; ns++; }
		if (f > 60 && low != lowprev && lowprev >= 0) steps++;
		lowprev = low; memcpy(prev, fw, sizeof(prev));
	}
	printf("%-22s speed %4.0f dir %4.0f: planted foot slides %5.1f cm/s (%3.0f%% of the speed), %.2f steps/s, run_w %.2f\n", label, spd, dirdeg, slide / (ns ? ns : 1), 100 * slide / (ns ? ns : 1) / (spd > 1 ? spd : 1), steps / (T - 1), A.run_w);
}
int main(int argc, char **argv) {
	if (argc < 2) return 1;
	CBank B; if (cb_load(&B, argv[1])) return 1; int set = argc > 2 ? cb_find_set(&B, argv[2]) : 0; if (set < 0) set = 0;
	static const float D[] = { 0, 45, 90, 135, 180, -90 };
	for (int i = 0; i < 6; i++) run(&B, set, "run (335)", 335, D[i], 0, 0);
	for (int i = 0; i < 6; i++) run(&B, set, "walk (220)", 220, D[i], 0, 0);
	for (int i = 0; i < 4; i++) run(&B, set, "bot strafe (141)", 141, D[i], 0, 0);
	run(&B, set, "bot turning walk (84)", 84, 0, 0, 0);
	for (int i = 0; i < 3; i++) run(&B, set, "crouch (175)", 175, D[i * 2], 1, 0);
	run(&B, set, "sprint (535)", 535, 0, 0, 1);
	run(&B, set, "sprint diag (535)", 535, 30, 0, 1);
	return 0;
}
