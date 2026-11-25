// tools/gchk (perf b46): makes <map>.gchk, world.h's chunk split precomputed, for every map of a folder, and checks it.
// For each map: world.h's own world_load (render b45's w_split_mesh, on tools/world_host's stub GL) writes each mesh's record
// (w_gen_mesh below); then the map is loaded again from the new file, and every mesh's result (its chunks and the order of
// its indices) must be the same bytes as the split made, and every mesh must come from the file.
// build + run: tools/gchk/gchk.sh [maps folder] [out folder]
#include <stdarg.h>
#include <dirent.h>
#include <time.h>
#define W_GCHK_GEN
#include "vitaGL.h"   // tools/world_host's stub GL
#include WORLDH
static FILE *g_out; static int g_pass, g_nm[2], g_bad; static uint64_t *g_dig[2]; static int g_cap; static unsigned long long g_tri, g_rk;
static void logq(const char *fmt, ...) { (void)fmt; }
static void logv(const char *fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void w_gen_mesh(int i, const WMeshW *m, uint64_t h, const unsigned short *ix) {
	if (i >= g_cap) { int c = g_cap ? g_cap * 2 : 1024; for (int p = 0; p < 2; p++) g_dig[p] = realloc(g_dig[p], sizeof(uint64_t) * c); g_cap = c; }
	uint64_t d = w_hash64(h, ix, 2 * (size_t)m->ni);   // the result: index order + chunks
	for (int c = m->c0; c < m->c1; c++) d = w_hash64(d, &w_chunk[c], sizeof(WChunk));
	g_dig[g_pass][i] = d; g_nm[g_pass] = i + 1;
	if (g_pass) return;
	unsigned nt = (unsigned)m->ni / 3, nc = (unsigned)(m->c1 - m->c0), sp = w_gen_split ? 1 : 0;
	if (sp && nc == 1 && w_chunk[m->c0].key == 0 && nt > 1) { /* (split asked, one chunk: still a split record) */ }
	fwrite(&h, 8, 1, g_out); fwrite(&nt, 4, 1, g_out); fwrite(&nc, 4, 1, g_out); fwrite(&sp, 4, 1, g_out);
	for (int c = m->c0; c < m->c1; c++) { const WChunk *C = &w_chunk[c]; unsigned t = (unsigned)C->count / 3;
		fwrite(&C->key, 4, 1, g_out); fwrite(&t, 4, 1, g_out); fwrite(C->mn, 4, 3, g_out); fwrite(C->mx, 4, 3, g_out); }
	g_tri += nt;
	if (sp && nc > 1) { size_t n = 0;
		for (unsigned t = 0; t < nt; t++) { unsigned k = w_gen_rank[t]; if (nc <= 256) { unsigned char b = (unsigned char)k; fwrite(&b, 1, 1, g_out); n++; } else { unsigned char b[2] = { (unsigned char)k, (unsigned char)(k >> 8) }; fwrite(b, 1, 2, g_out); n += 2; } }
		g_rk += n; while (n & 3) { fputc(0, g_out); n++; } }
}
static int cmpstr(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
int main(int argc, char **argv) {
	if (argc < 3) { fprintf(stderr, "usage: gchk <maps folder/> <out folder/> [map ...]\n"); return 2; }
	const char *M = argv[1], *O = argv[2]; char *stems[256]; int ns = 0;
	if (argc > 3) for (int i = 3; i < argc && ns < 256; i++) stems[ns++] = argv[i];
	else { DIR *d = opendir(M); struct dirent *e; if (!d) { perror(M); return 1; }
		while ((e = readdir(d)) && ns < 256) { size_t l = strlen(e->d_name); if (l > 5 && !strcmp(e->d_name + l - 5, ".gmap")) { stems[ns] = strdup(e->d_name); stems[ns][l - 5] = 0; ns++; } }
		closedir(d); qsort(stems, ns, sizeof(char *), cmpstr); }
	w_log = logq; unsigned long long all = 0; double tsplit = 0, tfile = 0; int fails = 0;
	for (int s = 0; s < ns; s++) {
		char p[512], q[512]; snprintf(p, sizeof(p), "%s%s.gchk.tmp", O, stems[s]); snprintf(q, sizeof(q), "%s%s.gchk", O, stems[s]);
		g_out = fopen(p, "wb"); if (!g_out) { perror(p); return 1; }
		unsigned hd[5] = { 0, 1, 1000, 0, 0 }; memcpy(hd, "GCHK", 4); fwrite(hd, 4, 5, g_out);
		g_pass = 0; w_gchk_on = 0; g_nm[0] = g_nm[1] = 0; g_tri = g_rk = 0; w_log = logq;
		double t0 = now(); int r = world_load(M, stems[s]); double t1 = now() - t0;
		if (r < 0) { printf("%-14s FAILED to load (%d)\n", stems[s], r); fclose(g_out); remove(p); fails++; continue; }
		int nm = world_nmesh, nch = w_nchunk; world_unload();
		hd[3] = (unsigned)nm; fseek(g_out, 0, SEEK_SET); fwrite(hd, 4, 5, g_out); fseek(g_out, 0, SEEK_END); long sz = ftell(g_out); fclose(g_out);
		if (rename(p, q)) { perror(q); return 1; }
		g_pass = 1; w_gchk_on = 1; w_gchk_dir = O;
		t0 = now(); r = world_load(M, stems[s]); double t2 = now() - t0; int used = w_gk.used, missed = w_gk.missed, nch2 = w_nchunk; world_unload(); w_gchk_dir = NULL;
		int same = r >= 0 && g_nm[1] == nm && nch2 == nch && !memcmp(g_dig[0], g_dig[1], sizeof(uint64_t) * nm);
		if (!same || used != nm) { fails++; g_bad++; }
		printf("%-14s %8ld bytes  %4d meshes, %5d chunks, %7llu triangles (chunk indices %llu bytes) | from the file: %d of %d meshes, %d missed, result %s | load %.1f -> %.1f ms (host)\n",
			stems[s], sz, nm, nch, g_tri, g_rk, used, nm, missed, same ? "identical" : "DIFFERENT", t1 * 1e3, t2 * 1e3);
		all += (unsigned long long)sz; tsplit += t1; tfile += t2;
	}
	printf("%d maps, %llu bytes of .gchk; host world_load %.0f -> %.0f ms; %s\n", ns, all, tsplit * 1e3, tfile * 1e3, fails ? "FAILED" : "all identical");
	(void)logv; return fails ? 1 : 0;
}
