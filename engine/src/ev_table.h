// databuilder: the FMOD event table (event volumes, 3D settings, layers, envelope curves, per-map ambience) is DATA:
// ux0:data/gitsfa/events.gev, written by tools/audio/make_events.py from the player's own sac.fev and the maps'
// SoundData.sdf (it used to be compiled into audio.h as a generated block). Same structs and the same names as before
// (EV_DEF, EV_LAYER, EV_CURVE, AMB_MAP, AMB_EMIT, AMB_TIMED, EV_COUNT), now filled at start-up by ev_load(dir), which
// main.c calls before anything plays an event. A missing or bad file leaves the tables empty: every event is silent.
//
// events.gev (little endian):
//   'GEV1', u32 ncurve, nlayer, nev, nmap, nemit, ntimed, strbytes
//   curves  ncurve x 17 u8 (padded to 4)
//   strings strbytes (NUL-terminated; fields below hold byte offsets into them)
//   layers  nlayer x { u32 snd; f32 g[4], s0, s1; u8 sp, kind, vc, pc, pnull, pad[3]; f32 pk }              40 bytes
//   events  nev x { u32 name; u16 l0; u8 nl, flags; f32 vol, vrand, pitch, prand, mind, maxd, dmax; u8 maxpb, pri; u16 fade }  40 bytes
//   ambmap  nmap x { u32 map, out, in }
//   emit    nemit x { u32 map, ev; f32 x, y, z, rin, rout }
//   timed   ntimed x { u32 map, ev; f32 period, at, dist }
// b107, optional (older files end here: no reverb): 'GRV1', u32 nev, nrev, nmap
//   wet     nev x u8: the event's reverb send, -dB x 4 (255 = none), padded to 4
//   revdef  nrev x { char name[28]; f32 room, room_hf, room_lf, decay, decay_hf, refl, refl_delay, reverb, reverb_delay, diffusion, density, hf_ref, lf_ref }  80 bytes
//   maprev  nmap x { u8 outdoor, indoor (indices into revdef, 255 = none), pad[2] }   (in ambmap's order)
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { const char *snd; float g[4]; float s0, s1; unsigned char sp, kind, vc, pc, pnull; float pk; } EvLayer;
// snd: "name" | "prefix*" | "a|b|c"; g[indoor*2 + aim]: layer volume; [s0,s1): range on parameter sp (0 none, 1 distance,
// 2 indoor, 3 aim, 4 the event's own parameter, e.g. a grenade bounce's strength); kind: 0 RAM sound, 1 voice line, 2 music, 3 ambience (+16 looped); vc: volume-vs-distance curve,
// pc: 3D-pan-level-vs-distance curve + 1 (0 none), pk: constant 3D pan level (-1 none); pnull: chance of silence (/255)
typedef struct { const char *name; unsigned short l0; unsigned char nl, flags; float vol, vrand, pitch, prand, mind, maxd, dmax;
	unsigned char maxpb, pri; unsigned short fade; } EvDef;
// flags: 1 3D, 2 custom rolloff (distance curves), 4 log rolloff (min/max distance), 8 linear rolloff; dmax: distance parameter range
typedef struct { const char *map, *out, *in; } AmbMap;
typedef struct { const char *map, *ev; float x, y, z, rin, rout; } AmbEmit;
typedef struct { const char *map, *ev; float period, at, dist; } AmbTimed;

static unsigned char (*EV_CURVE)[17];
static EvLayer *EV_LAYER;
static EvDef *EV_DEF;
static AmbMap *AMB_MAP;
static AmbEmit *AMB_EMIT;
static AmbTimed *AMB_TIMED;
static int ev_count, ev_nlayer, ev_ncurve, amb_nmap, amb_nemit, amb_ntimed;
#include "reverb.h"
static const unsigned char *EV_WET; static RevDef *REV_DEF; static int rev_ndef; static const unsigned char (*AMB_REV)[4];
static char *ev_blob;   // the file (strings point into it)
#define EV_COUNT ev_count

static unsigned ev_u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static float ev_f32(const unsigned char *p) { unsigned u = ev_u32(p); float f; memcpy(&f, &u, 4); return f; }
// -> number of events (0: none loaded)
static int ev_load(const char *dir) {
	char path[160]; snprintf(path, sizeof(path), "%sevents.gev", dir);
	FILE *f = fopen(path, "rb"); if (!f) return 0;
	fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
	unsigned char *d = sz > 32 ? malloc((size_t)sz) : NULL;
	if (!d || fread(d, 1, (size_t)sz, f) != (size_t)sz || memcmp(d, "GEV1", 4)) { fclose(f); free(d); return 0; }
	fclose(f);
	unsigned nc = ev_u32(d + 4), nl = ev_u32(d + 8), ne = ev_u32(d + 12), nm = ev_u32(d + 16), nx = ev_u32(d + 20), nt = ev_u32(d + 24), sb = ev_u32(d + 28);
	size_t o = 32, oc = o; o += ((size_t)nc * 17 + 3) & ~(size_t)3;
	size_t os = o; o += sb; size_t ol = o; o += (size_t)nl * 40; size_t oe = o; o += (size_t)ne * 40;
	size_t om = o; o += (size_t)nm * 12; size_t ox = o; o += (size_t)nx * 28; size_t ot = o; o += (size_t)nt * 20;
	if (o > (size_t)sz || !sb || d[os + sb - 1]) { free(d); return 0; }
	const char *S = (const char *)d + os;
#define EV_STR(p) (ev_u32(p) < sb ? S + ev_u32(p) : "")
	EV_CURVE = (unsigned char (*)[17])(d + oc);
	EV_LAYER = calloc(nl ? nl : 1, sizeof(EvLayer)); EV_DEF = calloc(ne ? ne : 1, sizeof(EvDef));
	AMB_MAP = calloc(nm ? nm : 1, sizeof(AmbMap)); AMB_EMIT = calloc(nx ? nx : 1, sizeof(AmbEmit)); AMB_TIMED = calloc(nt ? nt : 1, sizeof(AmbTimed));
	if (!EV_LAYER || !EV_DEF || !AMB_MAP || !AMB_EMIT || !AMB_TIMED) { free(EV_LAYER); free(EV_DEF); free(AMB_MAP); free(AMB_EMIT); free(AMB_TIMED); free(d); EV_CURVE = NULL; EV_LAYER = NULL; EV_DEF = NULL; AMB_MAP = NULL; AMB_EMIT = NULL; AMB_TIMED = NULL; return 0; }
	for (unsigned i = 0; i < nl; i++) {
		const unsigned char *p = d + ol + 40 * (size_t)i; EvLayer *L = &EV_LAYER[i];
		L->snd = EV_STR(p); for (int k = 0; k < 4; k++) L->g[k] = ev_f32(p + 4 + 4 * k);
		L->s0 = ev_f32(p + 20); L->s1 = ev_f32(p + 24); L->sp = p[28]; L->kind = p[29]; L->vc = p[30]; L->pc = p[31]; L->pnull = p[32]; L->pk = ev_f32(p + 36);
	}
	for (unsigned i = 0; i < ne; i++) {
		const unsigned char *p = d + oe + 40 * (size_t)i; EvDef *E = &EV_DEF[i];
		E->name = EV_STR(p); E->l0 = (unsigned short)(p[4] | p[5] << 8); E->nl = p[6]; E->flags = p[7];
		E->vol = ev_f32(p + 8); E->vrand = ev_f32(p + 12); E->pitch = ev_f32(p + 16); E->prand = ev_f32(p + 20);
		E->mind = ev_f32(p + 24); E->maxd = ev_f32(p + 28); E->dmax = ev_f32(p + 32); E->maxpb = p[36]; E->pri = p[37]; E->fade = (unsigned short)(p[38] | p[39] << 8);
	}
	for (unsigned i = 0; i < nm; i++) { const unsigned char *p = d + om + 12 * (size_t)i; AMB_MAP[i].map = EV_STR(p); AMB_MAP[i].out = EV_STR(p + 4); AMB_MAP[i].in = EV_STR(p + 8); }
	for (unsigned i = 0; i < nx; i++) {
		const unsigned char *p = d + ox + 28 * (size_t)i; AmbEmit *A = &AMB_EMIT[i];
		A->map = EV_STR(p); A->ev = EV_STR(p + 4); A->x = ev_f32(p + 8); A->y = ev_f32(p + 12); A->z = ev_f32(p + 16); A->rin = ev_f32(p + 20); A->rout = ev_f32(p + 24);
	}
	for (unsigned i = 0; i < nt; i++) {
		const unsigned char *p = d + ot + 20 * (size_t)i; AmbTimed *T = &AMB_TIMED[i];
		T->map = EV_STR(p); T->ev = EV_STR(p + 4); T->period = ev_f32(p + 8); T->at = ev_f32(p + 12); T->dist = ev_f32(p + 16);
	}
#undef EV_STR
	for (unsigned i = 0; i < ne; i++) if (EV_DEF[i].nl && EV_DEF[i].l0 + EV_DEF[i].nl > nl) EV_DEF[i].nl = 0;   // (a bad file never reads past the layers)
	for (unsigned i = 0; i < nl; i++) { if (EV_LAYER[i].vc >= nc) EV_LAYER[i].vc = 0; if (EV_LAYER[i].pc > nc) EV_LAYER[i].pc = 0; }
	if (o + 16 <= (size_t)sz && !memcmp(d + o, "GRV1", 4) && ev_u32(d + o + 4) == ne && ev_u32(d + o + 12) == nm) {   // b107: the reverb chunk
		unsigned nr = ev_u32(d + o + 8); size_t ow = o + 16, od = ow + (((size_t)ne + 3) & ~(size_t)3), omr = od + (size_t)nr * 80;
		if (nr < 255 && omr + (size_t)nm * 4 <= (size_t)sz && (REV_DEF = calloc(nr ? nr : 1, sizeof(RevDef)))) {
			for (unsigned i = 0; i < nr; i++) { const unsigned char *p = d + od + 80 * (size_t)i; memcpy(REV_DEF[i].name, p, 27); float *f = &REV_DEF[i].room; for (int k = 0; k < 13; k++) f[k] = ev_f32(p + 28 + 4 * k); }
			EV_WET = d + ow; AMB_REV = (const unsigned char (*)[4])(d + omr); rev_ndef = (int)nr; }
	}
	ev_blob = (char *)d; ev_ncurve = (int)nc; ev_nlayer = (int)nl; amb_nmap = (int)nm; amb_nemit = (int)nx; amb_ntimed = (int)nt; ev_count = (int)ne;
	return ev_count;
}
