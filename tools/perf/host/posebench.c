// pose evaluation micro-benchmark (perf b35): a bot running, aiming and firing; N ca_pose calls; prints a checksum of the pose (posebench.sh)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "model.h"
#include "chr.h"
int main(int argc, char **argv) {
	static CBank B; if (cb_load(&B, argv[1]) < 0) { printf("bank?\n"); return 1; }
	int n = argc > 2 ? atoi(argv[2]) : 100;
	static CAnim A; ca_init(&A, &B, 0);
	CInput in; memset(&in, 0, sizeof(in)); in.vf = 400; in.vr = 80; in.grounded = 1; in.zoom = 0.7f; in.aim_pitch = 0.2f;
	for (int f = 0; f < 120; f++) { if (f == 115) { ca_fire(&A); } ca_update(&A, &in, 1 / 60.f); }
	double cs = 0;
	for (int k = 0; k < n; k++) { MBone *P = ca_pose(&A, &in); for (int b = 0; b < B.nb; b++) cs += P[b].q[0] * 0.5 + P[b].p[2] * 1e-3; }
	printf("bones %d, %d poses, checksum %.6f\n", B.nb, n, cs);
	return 0;
}
