// databuilder: Azuma's shield panels and break pieces are DATA: ux0:data/gitsfa/sk_shield.gsg, written by
// tools/skills/make_shield.py from the player's own azuma_ally_shield_tier1 / tier2.nif (this header used to hold the
// generated tables). Same names and shapes as before; skills_init calls sksh_load(dir). Without the file the tables stay
// empty (no outline, no pieces, zero size): the shield draws nothing.
// NIF frame: x forward (away from the holder), y left, z up, cm; centred on the view axis (y = 0), z = 0 at the panel centre
//   'GSG1', then per tier: u32 nout, nfv, nfi, nfr; f32 out[nout][2]; f32 curv[2], uvmap[4], dim[3]; f32 fv[nfv][3];
//   u16 fi[nfi] (+ u16 pad when odd); nfr x { u16 v0, nv, i0, ni; f32 org[3], d[3], rot[3] }
//   then (optional, b50) 'SHX1', u32 size, u8 noise[32][32] (the hex surface's dark map, turret_laser_noise), u8 prof[16] (the
//   brightest texel of each sixteenth of the fill texture along u), u8 up[32] (Tier 2's rising band, upup.dds, v 0.50 .. 0.72)
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { unsigned short v0, nv, i0, ni; float org[3], d[3], rot[3]; } SkShFrag;
static int SKSH_NOUT[2];                      // panel outline loop (y, z)
static const float (*SKSH_OUT[2])[2];
static float SKSH_CURV[2][2];                 // x = a + b * y^2 (the panel bulges forward in the middle)
static float SKSH_UVMAP[2][4];                // u = a + b * y, v = c + d * z (the NIF's own planar UVs)
static float SKSH_DIM[2][3];                  // half width, half height, half depth (cm)
static int SKSH_NFR[2];                       // break fragments
static const SkShFrag *SKSH_FR[2];            // first vertex, count, first index, count, origin, flight in 0.17 s, spin (rad)
static const float (*SKSH_FV[2])[3];          // vertices about each piece's origin (x, y, z)
static const unsigned short *SKSH_FI[2];
static unsigned char SKSH_NZ[32 * 32], SKSH_PROF[16], SKSH_UP[32]; static int SKSH_HAVE_X;   // (b50) SHX1 chunk: 0 = absent (no shimmer, no fill culling, no band)
static unsigned sksh_u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static float sksh_f32(const unsigned char *p) { unsigned u = sksh_u32(p); float f; memcpy(&f, &u, 4); return f; }
// -> 0 ok, -1 missing / bad (tables left empty)
static int sksh_load(const char *dir) {
	char path[160]; snprintf(path, sizeof(path), "%ssk_shield.gsg", dir);
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
	unsigned char *d = sz > 4 ? malloc((size_t)sz) : NULL;
	if (!d || fread(d, 1, (size_t)sz, f) != (size_t)sz || memcmp(d, "GSG1", 4)) { fclose(f); free(d); return -1; }
	fclose(f);
	size_t o = 4; float *pool[2][2] = { { 0 } }; SkShFrag *fr[2] = { 0 }; unsigned short *fi[2] = { 0 }; unsigned cnt[2][4] = { { 0 } };
	float curv[2][2], uvm[2][4], dim[2][3]; int bad = 0;
	for (int t = 0; t < 2 && !bad; t++) {
		if (o + 16 > (size_t)sz) { bad = 1; break; }
		unsigned no = sksh_u32(d + o), nv = sksh_u32(d + o + 4), ni = sksh_u32(d + o + 8), nf = sksh_u32(d + o + 12); o += 16;
		if (no > 4096 || nv > 65535 || ni > 65535 || nf > 1024) { bad = 1; break; }
		size_t need = (size_t)no * 8 + 36 + (size_t)nv * 12 + (((size_t)ni * 2 + 3) & ~(size_t)3) + (size_t)nf * 44;
		if (o + need > (size_t)sz) { bad = 1; break; }
		cnt[t][0] = no; cnt[t][1] = nv; cnt[t][2] = ni; cnt[t][3] = nf;
		pool[t][0] = malloc(sizeof(float) * 2 * (no ? no : 1)); pool[t][1] = malloc(sizeof(float) * 3 * (nv ? nv : 1));
		fi[t] = malloc(sizeof(unsigned short) * (ni ? ni : 1)); fr[t] = malloc(sizeof(SkShFrag) * (nf ? nf : 1));
		if (!pool[t][0] || !pool[t][1] || !fi[t] || !fr[t]) { bad = 1; break; }
		for (unsigned i = 0; i < no * 2; i++) pool[t][0][i] = sksh_f32(d + o + 4 * i);
		o += (size_t)no * 8;
		for (int k = 0; k < 2; k++) curv[t][k] = sksh_f32(d + o + 4 * k);
		for (int k = 0; k < 4; k++) uvm[t][k] = sksh_f32(d + o + 8 + 4 * k);
		for (int k = 0; k < 3; k++) dim[t][k] = sksh_f32(d + o + 24 + 4 * k);
		o += 36;
		for (unsigned i = 0; i < nv * 3; i++) pool[t][1][i] = sksh_f32(d + o + 4 * i);
		o += (size_t)nv * 12;
		for (unsigned i = 0; i < ni; i++) { fi[t][i] = (unsigned short)(d[o + 2 * i] | d[o + 2 * i + 1] << 8); if (fi[t][i] >= nv) bad = 1; }
		o += ((size_t)ni * 2 + 3) & ~(size_t)3;
		for (unsigned i = 0; i < nf; i++, o += 44) {
			SkShFrag *F = &fr[t][i]; const unsigned char *p = d + o;
			F->v0 = (unsigned short)(p[0] | p[1] << 8); F->nv = (unsigned short)(p[2] | p[3] << 8); F->i0 = (unsigned short)(p[4] | p[5] << 8); F->ni = (unsigned short)(p[6] | p[7] << 8);
			for (int k = 0; k < 3; k++) { F->org[k] = sksh_f32(p + 8 + 4 * k); F->d[k] = sksh_f32(p + 20 + 4 * k); F->rot[k] = sksh_f32(p + 32 + 4 * k); }
			if ((unsigned)F->v0 + F->nv > nv || (unsigned)F->i0 + F->ni > ni) bad = 1;
		}
	}
	if (!bad && o + 8 <= (size_t)sz && !memcmp(d + o, "SHX1", 4) && sksh_u32(d + o + 4) >= 32 * 32 + 48 && o + 8 + 32 * 32 + 48 <= (size_t)sz) {
		memcpy(SKSH_NZ, d + o + 8, 32 * 32); memcpy(SKSH_PROF, d + o + 8 + 32 * 32, 16); memcpy(SKSH_UP, d + o + 8 + 32 * 32 + 16, 32); SKSH_HAVE_X = 1; }
	free(d);
	if (bad) { for (int t = 0; t < 2; t++) { free(pool[t][0]); free(pool[t][1]); free(fi[t]); free(fr[t]); } return -1; }
	for (int t = 0; t < 2; t++) {
		SKSH_NOUT[t] = (int)cnt[t][0]; SKSH_OUT[t] = (const float (*)[2])pool[t][0]; SKSH_FV[t] = (const float (*)[3])pool[t][1];
		SKSH_FI[t] = fi[t]; SKSH_FR[t] = fr[t]; SKSH_NFR[t] = (int)cnt[t][3];
		memcpy(SKSH_CURV[t], curv[t], sizeof(curv[t])); memcpy(SKSH_UVMAP[t], uvm[t], sizeof(uvm[t])); memcpy(SKSH_DIM[t], dim[t], sizeof(dim[t]));
	}
	return 0;
}
