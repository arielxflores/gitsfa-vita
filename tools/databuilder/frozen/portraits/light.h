// Light under foot: a grid of floor samples taken from the map's lightmaps (tools/make_light.py), used to light
// characters and the first-person weapon like the surfaces around them.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef struct { short z; unsigned char r, g, b; } LgtF;
static float lg_x0, lg_y0, lg_cell; static int lg_w, lg_h; static int *lg_start; static unsigned char *lg_n; static LgtF *lg_f; static float lg_avg[3] = { 1, 1, 1 };
static void light_free(void) { free(lg_start); free(lg_n); free(lg_f); lg_start = NULL; lg_n = NULL; lg_f = NULL; lg_w = lg_h = 0; }
static int light_load(const char *path) {
	light_free(); lg_avg[0] = lg_avg[1] = lg_avg[2] = 1;
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
	free(b); return (int)tot;
}
// the floor surface under (x, y) at or below z+60 cm in column (cx, cy); returns 0 if none
static int light_col(int cx, int cy, float z, float *rgb) {
	if (cx < 0 || cy < 0 || cx >= lg_w || cy >= lg_h) return 0;
	int c = cy * lg_w + cx; const LgtF *f = lg_f + lg_start[c];
	for (int i = 0; i < lg_n[c]; i++) if (f[i].z * 2.0f <= z + 60) { rgb[0] = f[i].r * 0.01f; rgb[1] = f[i].g * 0.01f; rgb[2] = f[i].b * 0.01f; return 1; }  // top floor first
	return 0;
}
static void light_at(float x, float y, float z, float *out) {
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
