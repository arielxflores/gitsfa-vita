// Light under foot: a grid of floor samples taken from the map's lightmaps (tools/make_light.py), used to light
// characters and the first-person weapon like the surfaces around them.
// Optional trailer 'ACTR', u32 1, f32 ambient[3], diffuse[3], specular[3], dimmer (the map's own light.ini [ACTOR],
// tools/glgt_actor.py): the original lights characters per map (dimmer 1.0 in Ghost Storage .. 1.62 in Ruined Tokyo);
// light_at() returns floor light x gain + ambient, the gain relative to the average map (1.17) at 60 %.
// databuilder (b47): optional trailer 'FOGP', u32 1, f32 colour[3], start (cm), density (1/cm), amount: the map's fog, from
// its World.ini (tools/glgt_actor.py); main.c's map_fog() uses it (it was the MAPS[] table's fog columns).
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef struct { short z; unsigned char r, g, b; } LgtF;
static float lg_x0, lg_y0, lg_cell; static int lg_w, lg_h; static int *lg_start; static unsigned char *lg_n; static LgtF *lg_f; static float lg_avg[3] = { 1, 1, 1 };
static float lg_gain[3] = { 1, 1, 1 }, lg_amb[3] = { 0, 0, 0 };   // per-map actor light (ACTR trailer); identity without one
static float lg_fog[6]; static int lg_fog_ok;   /* databuilder: the map's fog (FOGP trailer): colour rgb, start, density, amount */
static void light_free(void) { free(lg_start); free(lg_n); free(lg_f); lg_start = NULL; lg_n = NULL; lg_f = NULL; lg_w = lg_h = 0; }
static int light_load(const char *path) {
	light_free(); lg_avg[0] = lg_avg[1] = lg_avg[2] = 1; for (int c = 0; c < 3; c++) { lg_gain[c] = 1; lg_amb[c] = 0; }
	lg_fog_ok = 0;   /* databuilder */
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
	unsigned char *b = malloc(sz); if (fread(b, 1, sz, f) != (size_t)sz || memcmp(b, "GLGT", 4)) { fclose(f); free(b); return -2; } fclose(f);
	float hd[4]; memcpy(hd, b + 4, 16); unsigned dims[2]; memcpy(dims, b + 20, 8);
	lg_x0 = hd[0]; lg_y0 = hd[1]; lg_cell = hd[2]; lg_w = dims[0]; lg_h = dims[1];
	int nc = lg_w * lg_h; lg_start = malloc(sizeof(int) * nc); lg_n = malloc(nc);
	long o = 28, tot = 0; for (int i = 0; i < nc && o < sz; i++) { int n = b[o]; lg_start[i] = tot; lg_n[i] = n; tot += n; o += 1 + 5 * n; }
	lg_f = malloc(sizeof(LgtF) * (tot ? tot : 1)); o = 28; long k = 0; double s[3] = { 0, 0, 0 };
	for (int i = 0; i < nc && o < sz; i++) { int n = b[o++]; for (int j = 0; j < n; j++, o += 5, k++) {
		short z; memcpy(&z, b + o, 2); lg_f[k].z = z; lg_f[k].r = b[o + 2]; lg_f[k].g = b[o + 3]; lg_f[k].b = b[o + 4]; s[0] += b[o + 2]; s[1] += b[o + 3]; s[2] += b[o + 4]; } }
	if (tot) for (int c = 0; c < 3; c++) lg_avg[c] = (float)(s[c] / tot / 100.0);
	for (;;) {   /* databuilder: the trailers, in order (ACTR, then FOGP) */
	if (o + 48 <= sz && !memcmp(b + o, "ACTR", 4)) {   // amb[3] dif[3] spec[3] dimmer, colours 0..1
		float a[10]; memcpy(a, b + o + 8, 40);
		for (int c = 0; c < 3; c++) { float g = a[3 + c] * a[9] / 1.17f; g = 1 + 0.6f * (g - 1); lg_gain[c] = g < 0.75f ? 0.75f : g > 1.3f ? 1.3f : g; lg_amb[c] = a[c] * 0.6f; }
		o += 48; continue;   /* databuilder */
	}
	if (o + 32 <= sz && !memcmp(b + o, "FOGP", 4)) { memcpy(lg_fog, b + o + 8, 24); lg_fog_ok = 1; o += 32; continue; }   /* databuilder */
	break; }   /* databuilder */
	free(b); return (int)tot;
}
// the floor surface under (x, y) at or below z+60 cm in column (cx, cy); returns 0 if none
static int light_col(int cx, int cy, float z, float *rgb) {
	if (cx < 0 || cy < 0 || cx >= lg_w || cy >= lg_h) return 0;
	int c = cy * lg_w + cx; const LgtF *f = lg_f + lg_start[c];
	for (int i = 0; i < lg_n[c]; i++) if (f[i].z * 2.0f <= z + 60) { rgb[0] = f[i].r * 0.01f; rgb[1] = f[i].g * 0.01f; rgb[2] = f[i].b * 0.01f; return 1; }  // top floor first
	return 0;
}
static void light_at_raw(float x, float y, float z, float *out);
static void light_at(float x, float y, float z, float *out) {
	light_at_raw(x, y, z, out);
	if (lg_w) for (int c = 0; c < 3; c++) out[c] = out[c] * lg_gain[c] + lg_amb[c];
}
static void light_at_raw(float x, float y, float z, float *out) {
	if (!lg_w) { out[0] = out[1] = out[2] = 1; return; }
	float fx = (x - lg_x0) / lg_cell - 0.5f, fy = (y - lg_y0) / lg_cell - 0.5f; int ix = (int)floorf(fx), iy = (int)floorf(fy); float tx = fx - ix, ty = fy - iy;
	float acc[3] = { 0, 0, 0 }, wsum = 0, rgb[3];
	for (int k = 0; k < 4; k++) { int dx = k & 1, dy = k >> 1; float w = (dx ? tx : 1 - tx) * (dy ? ty : 1 - ty) + 1e-3f;
		if (light_col(ix + dx, iy + dy, z, rgb)) { acc[0] += w * rgb[0]; acc[1] += w * rgb[1]; acc[2] += w * rgb[2]; wsum += w; } }
	for (int r = 1; wsum == 0 && r <= 3; r++)   // no floor sample right here: look a little further out
		for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if ((abs(dx) == r || abs(dy) == r) && light_col(ix + dx, iy + dy, z, rgb)) { acc[0] += rgb[0]; acc[1] += rgb[1]; acc[2] += rgb[2]; wsum += 1; }
	if (wsum == 0) { memcpy(out, lg_avg, 12); return; }
	for (int c = 0; c < 3; c++) out[c] = acc[c] / wsum;
}
// Muzzle-flash light on the first-person weapon and arms: in the footage (EA tutorial, M4A1 hip fire) the whole gun
// turns warm orange-brown for one 30 fps frame per shot (dark grey ~45 -> ~150/105/70). k = 1 at the shot, fading out.
static void light_muzzle(float *tint, float k) {
	if (k <= 0) return; if (k > 1) k = 1;
	tint[0] += 2.3f * k; tint[1] += 1.3f * k; tint[2] += 0.5f * k;
}
