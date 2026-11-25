// databuilder check: the FMOD event table as the engine sees it, hashed field by field (strings by content, floats by
// their bits). Built twice: -DOLD_TABLE=<header with the old compiled-in block> and with engine/src/ev_table.h + events.gev;
// both must print the same hash.   usage: evhash [data dir/]
#include <stdio.h>
#include <string.h>
#ifdef OLD_TABLE
#include OLD_TYPES
#include OLD_TABLE
static int nl_ = sizeof(EV_LAYER) / sizeof(EV_LAYER[0]), nc_ = sizeof(EV_CURVE) / sizeof(EV_CURVE[0]), nm_ = sizeof(AMB_MAP) / sizeof(AMB_MAP[0]),
           nx_ = sizeof(AMB_EMIT) / sizeof(AMB_EMIT[0]), nt_ = sizeof(AMB_TIMED) / sizeof(AMB_TIMED[0]);
#else
#include "ev_table.h"
#define nl_ ev_nlayer
#define nc_ ev_ncurve
#define nm_ amb_nmap
#define nx_ amb_nemit
#define nt_ amb_ntimed
#endif
static unsigned h = 2166136261u;
static void H(const void *p, size_t n) { const unsigned char *b = p; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u; }
static void HS(const char *s) { H(s, strlen(s) + 1); }
static void HF(float f) { H(&f, 4); }
int main(int argc, char **argv) {
#ifndef OLD_TABLE
	if (ev_load(argc > 1 ? argv[1] : "./") <= 0) { printf("events.gev missing or bad\n"); return 1; }
#else
	(void)argc; (void)argv;
#endif
	int n[6] = { nc_, nl_, EV_COUNT, nm_, nx_, nt_ }; H(n, sizeof(n));
	for (int i = 0; i < nc_; i++) H(EV_CURVE[i], 17);
	for (int i = 0; i < nl_; i++) { const EvLayer *L = &EV_LAYER[i]; HS(L->snd); for (int k = 0; k < 4; k++) HF(L->g[k]); HF(L->s0); HF(L->s1);
		unsigned char b[5] = { L->sp, L->kind, L->vc, L->pc, L->pnull }; H(b, 5); HF(L->pk); }
	for (int i = 0; i < EV_COUNT; i++) { const EvDef *E = &EV_DEF[i]; HS(E->name); H(&E->l0, 2); H(&E->nl, 1); H(&E->flags, 1);
		HF(E->vol); HF(E->vrand); HF(E->pitch); HF(E->prand); HF(E->mind); HF(E->maxd); HF(E->dmax); H(&E->maxpb, 1); H(&E->pri, 1); H(&E->fade, 2); }
	for (int i = 0; i < nm_; i++) { HS(AMB_MAP[i].map); HS(AMB_MAP[i].out); HS(AMB_MAP[i].in); }
	for (int i = 0; i < nx_; i++) { const AmbEmit *A = &AMB_EMIT[i]; HS(A->map); HS(A->ev); HF(A->x); HF(A->y); HF(A->z); HF(A->rin); HF(A->rout); }
	for (int i = 0; i < nt_; i++) { const AmbTimed *T = &AMB_TIMED[i]; HS(T->map); HS(T->ev); HF(T->period); HF(T->at); HF(T->dist); }
	printf("EVHASH %08x  curves %d layers %d events %d maps %d emitters %d timed %d\n", h, nc_, nl_, EV_COUNT, nm_, nx_, nt_);
	return 0;
}
