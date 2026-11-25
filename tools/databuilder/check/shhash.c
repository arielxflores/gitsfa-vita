// databuilder check: Azuma's shield tables hashed value by value; built with -DOLD_HDR=<old generated header> and with
// engine/src/sk_shield_geo.h + sk_shield.gsg: both must print the same hash.   usage: shhash [data dir/]
#include <stdio.h>
#include <string.h>
#ifdef OLD_HDR
#include OLD_HDR
#else
#include "sk_shield_geo.h"
#endif
static unsigned h = 2166136261u;
static void H(const void *p, size_t n) { const unsigned char *b = p; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u; }
int main(int argc, char **argv) {
#ifndef OLD_HDR
	if (sksh_load(argc > 1 ? argv[1] : "./") < 0) { printf("sk_shield.gsg missing or bad\n"); return 1; }
#else
	(void)argc; (void)argv;
#endif
	int nv[2] = { 0, 0 }, ni[2] = { 0, 0 };
	for (int t = 0; t < 2; t++) for (int i = 0; i < SKSH_NFR[t]; i++) {   // vertex / index counts as the fragments use them
		const SkShFrag *F = &SKSH_FR[t][i]; if (F->v0 + F->nv > nv[t]) nv[t] = F->v0 + F->nv; if (F->i0 + F->ni > ni[t]) ni[t] = F->i0 + F->ni; }
	for (int t = 0; t < 2; t++) {
		H(&SKSH_NOUT[t], 4); H(SKSH_OUT[t], sizeof(float) * 2 * SKSH_NOUT[t]); H(SKSH_CURV[t], 8); H(SKSH_UVMAP[t], 16); H(SKSH_DIM[t], 12);
		H(&SKSH_NFR[t], 4); H(&nv[t], 4); H(&ni[t], 4); H(SKSH_FV[t], sizeof(float) * 3 * nv[t]); H(SKSH_FI[t], 2 * ni[t]);
		for (int i = 0; i < SKSH_NFR[t]; i++) { const SkShFrag *F = &SKSH_FR[t][i]; H(&F->v0, 8); H(F->org, 12); H(F->d, 12); H(F->rot, 12); }
	}
	printf("SHHASH %08x  outline %d/%d  pieces %d/%d  verts %d/%d  indices %d/%d\n", h, SKSH_NOUT[0], SKSH_NOUT[1], SKSH_NFR[0], SKSH_NFR[1], nv[0], nv[1], ni[0], ni[1]);
	return 0;
}
