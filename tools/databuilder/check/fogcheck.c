// databuilder check (b47): every map's fog as the engine gets it, before and after it moved out of main.c. Built with
// -DBEFORE="<file>" (the MAPS[] table of the old main.c: typedef + array) it prints the compiled-in values; without it, it
// loads each <map>.glgt with engine/src/light.h and prints the FOGP trailer. Both must print the same lines (%a: exact bits).
//   usage: fogcheck <maps dir/>      (see fogcheck.sh)
#include <stdio.h>
#include <string.h>
#include "light.h"
static const char *STEMS[] = { "downtown", "berutalube", "ecenter", "ghost_storage", "headquarter", "port", "pss9", "ruinednight",
	"ruinedtokyo", "trainstation", "underground", "urban", "zioplant", "shooting", "tutorial", "tutorial2", "tutorial3" };
#ifdef BEFORE
#include BEFORE
#endif
int main(int argc, char **argv) {
	int bad = 0;
	for (int m = 0; m < 17; m++) {
		float fog[3], fogp[3];
#ifdef BEFORE
		(void)argc; (void)argv;
		if (strcmp(MAPS[m].stem, STEMS[m])) { printf("table order differs at %d\n", m); return 1; }
		memcpy(fog, MAPS[m].fog, 12); memcpy(fogp, MAPS[m].fogp, 12);
#else
		char p[512]; snprintf(p, sizeof(p), "%s%s.glgt", argc > 1 ? argv[1] : "./", STEMS[m]);
		if (light_load(p) < 0 || !lg_fog_ok) { printf("%s: no FOGP in %s\n", STEMS[m], p); bad++; continue; }
		memcpy(fog, lg_fog, 12); memcpy(fogp, lg_fog + 3, 12);
#endif
		printf("%-13s fog %a %a %a  fogp %a %a %a\n", STEMS[m], fog[0], fog[1], fog[2], fogp[0], fogp[1], fogp[2]);
	}
	return bad != 0;
}
