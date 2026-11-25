#include "fopen_shim.h"
#include <stdlib.h>
#include <math.h>
#include "audio.h"
int main(int argc, char **argv) {
	(ev_load("ux0:data/gitsfa/"), audio_load_all("ux0:data/gitsfa/"));
	float lis[3] = { 0, 0, 170 }; audio_listener(lis, 0);
	int e = ev_find(argv[1]); if (e < 0) { printf("no event\n"); return 1; }
	const EvDef *E = &EV_DEF[e];
	printf("%s vol %.3f flags %d mind %.0f maxd %.0f dmax %.0f maxpb %d pri %d\n", E->name, E->vol, E->flags, E->mind, E->maxd, E->dmax, E->maxpb, E->pri);
	for (int i = 0; i < E->nl; i++) { const EvLayer *L = &EV_LAYER[E->l0 + i];
		printf("  %-50s g %.2f %.2f %.2f %.2f [%.3f %.3f] sp %d kind %d vc %d pc %d pnull %d pk %.2f\n", L->snd, L->g[0], L->g[1], L->g[2], L->g[3], L->s0, L->s1, L->sp, L->kind, L->vc, L->pc, L->pnull, L->pk);
		if (L->vc) { printf("     vol curve:"); for (int k = 0; k < 17; k++) printf(" %.2f", EV_CURVE[L->vc][k] / 255.f); printf("\n"); } }
	for (int a = 2; a < argc; a++) {
		float d = atof(argv[a]), p[3] = { d, 0, 170 };
		cmd_w = cmd_r = 0; ev_play_ex(e, p, 0, 0, argc > 99 ? 1 : 0, 1);
		printf("d=%.0f:", d); for (unsigned c = cmd_r; c < cmd_w; c++) if (cmds[c].op == 1) printf(" [%s vol %.3f gl %d gr %d]", cmds[c].s->name, cmds[c].vol / 65536.f, cmds[c].gl, cmds[c].gr); printf("\n");
	}
}
