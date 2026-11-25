// progsrc (render b38, GPU cost model): the world's shader programs exactly as world.h builds them on the Vita (all
// four program sets, as in the test run), with the shader-cache key of each source (shcache.h: FNV-64 over the GL type,
// the source and the build string), so the GXP binaries a run saved can be matched to world programs.
// build (from tools/gpumodel; add -DW_HAS_GM for world.h versions before b35, which have no w_gm[]): gcc -O1 -w -I../world_host -I<dir with world.h + grade.h> -DWORLDH='"<dir>/world.h"' progsrc.c -o progsrc -lm
// run: ./progsrc "<build string>" > programs.json     (the build string: __DATE__ " " __TIME__ of the eboot's main.c)
#include <stdarg.h>
#include "vitaGL.h"
#ifndef GL_SHADER_TYPE
#define GL_SHADER_TYPE 0x8B4F
#endif
#define WORLD_PROBE_FILE "progsrc.c"   // exists: sets 1-3 are built, like the autoplay test run
#include WORLDH
#ifndef W_HAS_GM
#define W_GM(i) w_gm[i]
#else
#define W_GM(i) (-1)
#endif

static unsigned long long h64(unsigned long long h, const void *p, int n) { const unsigned char *b = p; for (int i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h; }
static unsigned long long key(GLuint s, const char *build) {
	GLint type = (GLint)h_stype[s]; const char *src = h_src[s];
	return h64(h64(h64(14695981039346656037ull, &type, 4), src, (int)strlen(src)), build, (int)strlen(build));
}
static void js(const char *s) { putchar('"'); for (; *s; s++) { if (*s == '"' || *s == '\\') putchar('\\'); if (*s == '\n') { fputs("\\n", stdout); continue; } putchar(*s); } putchar('"'); }
static void logf_(const char *f, ...) { (void)f; }
static GLuint mk(GLenum type, const char *src) { GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, NULL); glCompileShader(s); return s; }
int main(int argc, char **argv) {
	const char *build = argc > 1 ? argv[1] : "";
	grade_mode = GRADE_LUT2;   // what the Vita's compiler accepts first ("colour grading test: LUT (2 reads) ok")
	if (world_shaders(mk, logf_) < 0) { fprintf(stderr, "world_shaders failed\n"); return 1; }
	printf("{\"wp_n\": %d, \"nsets\": %d, \"programs\": [\n", WP_N, w_nsets); int first = 1;
	for (int i = 0; i < WP_N * WPS_N; i++) {
		GLuint p = w_prog[i]; if (!p) continue;
		if (!first) printf(",\n"); first = 0;
		printf("{\"i\": %d, \"set\": %d, \"k\": %d, \"gl\": %u, \"gm\": %d, \"vs\": \"%016llx\", \"fs\": \"%016llx\", \"fs_src\": ", i, i / WP_N, i % WP_N, p, W_GM(i), key(h_prog[p].vs, build), key(h_prog[p].fs, build));
		js(h_src[h_prog[p].fs]); printf("}");
	}
	printf("\n]}\n");
	return 0;
}
