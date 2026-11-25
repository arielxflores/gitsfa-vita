#include <stdarg.h>
#include "../../../engine/src/world.h"
static void logp(const char *fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }
static int dump;
static GLuint mk(GLenum t, const char *src) { if (dump) printf("---- %s\n%s\n", t == GL_VERTEX_SHADER ? "VS" : "FS", src); return stub_id++; }
int main(int argc, char **argv) {
	dump = argc > 2 && !strcmp(argv[2], "dump");
	if (argc > 3 && !strcmp(argv[3], "lut")) grade_mode = GRADE_LUT2;   // as on the device (b31: the LUT compiles)
	if (world_shaders(mk, logp) < 0) return 1;
	if (argc < 2) return 0;
	char dir[256]; snprintf(dir, sizeof(dir), "%s", argv[1]);
	const char *maps[] = { "downtown","berutalube","ecenter","ghost_storage","headquarter","port","pss9","ruinednight","ruinedtokyo","trainstation","underground","urban","zioplant","shooting","tutorial","tutorial2","tutorial3" };
	for (int i = 0; i < 17; i++) {
		if (world_load(dir, maps[i]) < 0) { printf("FAILED %s\n", maps[i]); continue; }
		float M[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }, cam[3] = { 0, 0, 0 }, fog[4] = { 0.5f, 0.6f, 0.7f, 1 }, fogp[4] = { 1000, 0.0001f, 0.5f, 0 };
		stub_draws = stub_tris = 0; int n = world_draw(M, cam, fog, fogp, 1, 0, 1.0f);
		printf("   drew %d meshes (%d draw calls, %d tris)\n", n, stub_draws, stub_tris);
	}
	world_unload(); return 0;
}
