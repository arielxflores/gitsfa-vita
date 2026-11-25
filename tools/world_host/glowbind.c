// world.h host test (render b37): every world draw samples the textures of the mesh it draws, in every program set.
// For each map: build the four program sets as the test run does (set 0 LUT opaque, 1 polynomial, 2 no grading, 3 all
// LUT), load the real .gmap, then draw every mesh with each set, with fog on and off (the lean programs), glow maps on and
// off, lightmaps on and off. At each glDrawElements the texture each sampler of the current program reads (the unit its
// glUniform1i named, as vitaGL resolves it) must be the mesh's: dif = its texture (or white), lm = its lightmap (or the
// neutral one), glw = its glow map. b32-b34 failed this in sets 1-3 for glw (see world.h w_glw).
// build: gcc -O1 -Wall -Wextra -Wno-unused-function -I. glowbind.c -o /tmp/wglow -lm && /tmp/wglow /root/gits/build/maps/ tutorial2 tutorial ...
// (run from tools/world_host)
#include <stdarg.h>
#include "vitaGL.h"
#define WORLD_PROBE_FILE "glowbind.c"   // any file that exists: build sets 1-3 like the test run
#include "../../engine/src/world.h"

static long n_checked, n_bad[3], n_draw_set[WPS_N]; static int cur_lmon, n_logged;
static const char *const smp_label[3] = { "dif", "lm", "glw" };
static void hook(void) {
	const WMeshW *m = NULL; for (int i = 0; i < world_nmesh; i++) if (w_mesh[i].ibo == h_elem_buf) { m = &w_mesh[i]; break; }
	if (!m) return;   // a sky layer
	n_draw_set[w_set]++;
	GLuint want[3] = { m->tex >= 0 ? w_tex[m->tex] : w_white, (cur_lmon && m->lm >= 0) ? w_tex[m->lm] : w_neutral, m->glow >= 0 ? w_tex[m->glow] : 0 };
	int smp[3] = { 0, 1, 3 };   // h_smp_name indices of dif, lm, glw
	for (int k = 0; k < 3; k++) {
		if (!h_reads(h_cur, smp[k])) continue;
		n_checked++; GLuint got = h_sampled(smp[k]);
		if (got != want[k]) { n_bad[k]++; if (n_logged++ < 12) printf("  BAD: set %d mesh %d (%s %d) program %u: %s samples gl %u, wants gl %u\n", w_set, (int)(m - w_mesh),
			smp_label[k], k == 0 ? m->tex : k == 1 ? m->lm : m->glow, h_cur, smp_label[k], got, want[k]); }
	}
}
static void logf_(const char *f, ...) { (void)f; }
static GLuint mk(GLenum type, const char *src) { GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, NULL); glCompileShader(s); return s; }
int main(int argc, char **argv) {
	if (argc < 3) { printf("usage: %s <maps dir/> <stem>...\n", argv[0]); return 2; }
	grade_mode = GRADE_LUT2;
	if (world_shaders(mk, logf_) < 0 || w_nsets != WPS_N) { printf("FAIL: program sets not built (%d)\n", w_nsets); return 1; }
	h_draw_hook = hook; int fail = 0;
	for (int a = 2; a < argc; a++) {
		if (world_load(argv[1], argv[a]) < 0) { printf("FAIL: %s does not load\n", argv[a]); fail = 1; continue; }
		int nglow = 0; for (int i = 0; i < world_nmesh; i++) nglow += w_mesh[i].glow >= 0 && w_mesh[i].glowk > 0;
		long c0 = n_checked, b0 = n_bad[0] + n_bad[1] + n_bad[2], g0 = n_bad[2]; memset(n_draw_set, 0, sizeof(n_draw_set));
		float M[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, cam[3] = { 0, 0, 0 }, fog[4] = { 0.5f, 0.5f, 0.5f, 1 };
		for (int s = 0; s < WPS_N; s++) for (int fo = 0; fo < 2; fo++) for (int gl = 0; gl < 2; gl++) for (int lm = 0; lm < 2; lm++) {
			float fogp[4] = { 300, 0.0001f, fo ? 0.9f : 0, 0 };
			w_base_set = s; world_set_glow(gl); cur_lmon = lm;
			world_draw(M, cam, fog, fogp, lm, 0, 0);
		}
		world_set_glow(1);
		long bad = n_bad[0] + n_bad[1] + n_bad[2] - b0;
		printf("%-12s %3d meshes (%d with a glow map): %ld sampler reads checked over sets 0-3 (draws per set %ld %ld %ld %ld), %ld wrong (%ld of them glow)\n",
			argv[a], world_nmesh, nglow, n_checked - c0, n_draw_set[0], n_draw_set[1], n_draw_set[2], n_draw_set[3], bad, n_bad[2] - g0);
		if (bad) fail = 1;
	}
	printf("%s\n", fail ? "FAIL" : "PASS");
	return fail;
}
