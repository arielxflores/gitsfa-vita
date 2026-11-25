// assign (render b38, GPU cost model): which world program draws each mesh and each sky layer of a map, taken from
// world.h's own world_draw on the host GL stand-in (tools/world_host/vitaGL.h): every glDrawElements records the bound
// index buffer (a mesh or a sky layer) and the program. Program sets 0-3 (world.h versions with w_base_set; older ones:
// set 0 only, the program for set s being s * WP_N + k).
// build: gcc -O1 -w -I../world_host -DWORLDH='"<dir>/world.h"' [-DW_OLD] assign.c -o assign -lm
// run:   ./assign <maps dir/> <stem> <fog start> <fog density> <fog max>   -> JSON on stdout
#include <stdarg.h>
#include "vitaGL.h"
#define WORLD_PROBE_FILE "assign.c"
#include WORLDH

static int cap_mesh[8192], cap_sky[64], cap_set;
static void hook(void) {
	int pi = -1; for (int i = 0; i < WP_N * WPS_N; i++) if (w_prog[i] == h_cur) { pi = i; break; }
	for (int i = 0; i < world_nmesh; i++) if (w_mesh[i].ibo == h_elem_buf) { cap_mesh[i] = pi; return; }
	for (int i = 0; i < world_sky_layers; i++) if (w_sky[i].ibo == h_elem_buf) { cap_sky[i] = pi; return; }
}
static void logf_(const char *f, ...) { (void)f; }
static GLuint mk(GLenum type, const char *src) { GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, NULL); glCompileShader(s); return s; }
int main(int argc, char **argv) {
	if (argc < 6) { fprintf(stderr, "usage: assign <maps dir/> <stem> <fog start> <fog density> <fog max>\n"); return 2; }
	grade_mode = GRADE_LUT2;
	if (world_shaders(mk, logf_) < 0) { fprintf(stderr, "world_shaders failed\n"); return 1; }
	if (world_load(argv[1], argv[2]) < 0) { fprintf(stderr, "%s does not load\n", argv[2]); return 1; }
	float fogp[4] = { (float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5]), 0 };
	float M[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, cam[3] = { 0, 0, 0 }, fog[4] = { 0.5f, 0.5f, 0.5f, 1 };
	static int mk_[4][8192], sk_[4][64];
	h_draw_hook = hook;
	for (int s = 0; s < WPS_N; s++) {
		for (int i = 0; i < world_nmesh; i++) cap_mesh[i] = -1;
		for (int i = 0; i < world_sky_layers; i++) cap_sky[i] = -1;
#ifdef W_OLD
		if (s > 0) { for (int i = 0; i < world_nmesh; i++) mk_[s][i] = mk_[0][i] < 0 ? -1 : s * WP_N + mk_[0][i] % WP_N; for (int i = 0; i < world_sky_layers; i++) sk_[s][i] = sk_[0][i] < 0 ? -1 : s * WP_N + sk_[0][i] % WP_N; continue; }
#else
		w_base_set = s;
#endif
		world_draw(M, cam, fog, fogp, 1, 0, 0);
		for (int i = 0; i < world_nmesh; i++) mk_[s][i] = cap_mesh[i];
		for (int i = 0; i < world_sky_layers; i++) sk_[s][i] = cap_sky[i];
	}
	printf("{\"stem\": \"%s\", \"wp_n\": %d, \"meshes\": [", argv[2], WP_N);
	for (int i = 0; i < world_nmesh; i++) printf("%s[%d, %d, %d, %d]", i ? ", " : "", mk_[0][i], mk_[1][i], mk_[2][i], mk_[3][i]);
	printf("], \"sky\": [");
	for (int i = 0; i < world_sky_layers; i++) printf("%s[%d, %d, %d, %d]", i ? ", " : "", sk_[0][i], sk_[1][i], sk_[2][i], sk_[3][i]);
	printf("]}\n");
	return 0;
}
