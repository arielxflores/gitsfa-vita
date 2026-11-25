#pragma once
#include <time.h>
static void mul4(float *o, const float *a, const float *b) { float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; } memcpy(o, r, 64); }
static void persp(float *m, float fovy, float asp, float n, float f) { float t = 1.0f / tanf(fovy * 0.5f); memset(m, 0, 64); m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f); }
static void view(float *m, const float *p, float yaw, float pitch, float *F, float *R, float *U) {
	float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
	F[0] = cy * cp; F[1] = sy * cp; F[2] = sp; R[0] = sy; R[1] = -cy; R[2] = 0; U[0] = R[1] * F[2] - R[2] * F[1]; U[1] = R[2] * F[0] - R[0] * F[2]; U[2] = R[0] * F[1] - R[1] * F[0];
	float v[16] = { R[0], U[0], -F[0], 0, R[1], U[1], -F[1], 0, R[2], U[2], -F[2], 0, -(R[0] * p[0] + R[1] * p[1] + R[2] * p[2]), -(U[0] * p[0] + U[1] * p[1] + U[2] * p[2]), (F[0] * p[0] + F[1] * p[1] + F[2] * p[2]), 1 };
	memcpy(m, v, 64);
}
static GLuint mk(GLenum t, const char *s) { (void)s; return glCreateShader(t); }
static SwTex bgtex[2];
static void mkbg(void) {   // procedural concrete (wall) and floor
	for (int t = 0; t < 2; t++) { bgtex[t].w = bgtex[t].h = 256; bgtex[t].px = malloc(256 * 256 * 4);
		for (int i = 0; i < 256 * 256; i++) { int x = i % 256, y = i / 256; float n = 0.5f + 0.06f * sinf(x * 0.37f + y * 0.11f) + 0.05f * ((rand() % 100) / 100.f - 0.5f) + ((x % 64 < 2 || y % 64 < 2) ? -0.08f : 0);
			float base = t ? 0.42f : 0.55f; unsigned char g = (unsigned char)(255 * base * n * 1.8f); bgtex[t].px[i * 4] = g; bgtex[t].px[i * 4 + 1] = (unsigned char)(g * 0.97f); bgtex[t].px[i * 4 + 2] = (unsigned char)(g * 0.92f); bgtex[t].px[i * 4 + 3] = 255; } }
}
static void quad3d(const float *VP, const float p[4][3], const SwTex *T, float rep) {
	float c[4][4], a[4][6]; float uv[4][2] = { { 0, 0 }, { rep, 0 }, { rep, rep }, { 0, rep } };
	for (int i = 0; i < 4; i++) { for (int r = 0; r < 4; r++) c[i][r] = VP[r] * p[i][0] + VP[4 + r] * p[i][1] + VP[8 + r] * p[i][2] + VP[12 + r]; a[i][0] = uv[i][0]; a[i][1] = uv[i][1]; a[i][2] = a[i][3] = a[i][4] = a[i][5] = 1; }
	float c1[3][4], a1[3][6]; int tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
	for (int t = 0; t < 2; t++) { for (int k = 0; k < 3; k++) { memcpy(c1[k], c[tri[t][k]], 16); memcpy(a1[k], a[tri[t][k]], 24); } sw_tri(c1, a1, T); }
}
static void tiled(const float *VP, float x0, float y0, float z0, float ax, float ay, float az, float bx, float by, float bz, int n, const SwTex *T) {
	for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) { float p[4][3]; float s0 = (float)i / n, s1 = (float)(i + 1) / n, t0 = (float)j / n, t1 = (float)(j + 1) / n;
		float S[4] = { s0, s1, s1, s0 }, Tt[4] = { t0, t0, t1, t1 };
		for (int k = 0; k < 4; k++) { p[k][0] = x0 + ax * S[k] + bx * Tt[k]; p[k][1] = y0 + ay * S[k] + by * Tt[k]; p[k][2] = z0 + az * S[k] + bz * Tt[k]; }
		quad3d(VP, p, T, 1); }
}
static void clear(float r, float g, float b) { for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) { sw_fb[y][x][0] = r; sw_fb[y][x][1] = g; sw_fb[y][x][2] = b; sw_zb[y][x] = 1; } }
static void scene(const float *VP, float wallx) {
	clear(0.45f, 0.5f, 0.55f); sw_blend = 0; sw_dmask = 1; sw_depth = 1;
	tiled(VP, -300, -900, 0, 2400, 0, 0, 0, 1800, 0, 12, &bgtex[1]);            // floor z = 0
	tiled(VP, wallx, -900, 0, 0, 1800, 0, 0, 0, 500, 10, &bgtex[0]);              // wall x = wallx facing -x
}
static void save(const char *name) {
	char p[256]; snprintf(p, 256, "/root/fx/host/out/%s.ppm", name); FILE *f = fopen(p, "wb"); fprintf(f, "P6 %d %d 255\n", SW_W, SW_H);
	for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) for (int k = 0; k < 3; k++) fputc((int)(sw_fb[y][x][k] * 255 + 0.5f), f);
	fclose(f);
}
static float P[16], V[16], VP[16], F[3], R[3], U[3], cam[3];
static void setcam(float x, float y, float z, float yaw, float pitch, float fov) { cam[0] = x; cam[1] = y; cam[2] = z; persp(P, fov * 3.14159f / 180, 960.0f / 544, 10, 12000); view(V, cam, yaw, pitch, F, R, U); mul4(VP, P, V); }
static void run_to(float *t, float t1) { while (*t < t1 - 1e-4f) { fx_update(1 / 60.f); *t += 1 / 60.f; } }
