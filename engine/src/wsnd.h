// b107: when each first-person clip fires its sound events - the game's own schedule (wpn_snd.txt, vm/make_wpn_snd.py).
// The PC's animation network names an event track per sound on every reload / bolt / pump clip (magazine out, magazine in,
// the bolt, a shotgun's pump after the shot), at its fraction of the clip; the engine used to spread reload_01..09 evenly
// (audio.h snd_wreload: up to 0.3 s off the hands, and the tactical reload also played the empty reload's bolt).
// A row may also give the seconds the PC plays the clip in: it fits every reload to the weapon's reload time, and the long
// empty reloads (PP-2200 11 s of clip, SR-V97 9.8 s, the SEBURO sniper 16 s) run 2 to 4 times faster there - measured from
// the user's showcase video by fitting each reload's sounds to this schedule (tools/audio/ana/reload_fit.py).
// Without the file (an older data set) every call answers "no row" and the old spread plays.
#pragma once
typedef struct { char sh[12]; unsigned char cl, n; float secs; struct { float f; char ev[26]; } e[10]; } WSnd;
static WSnd *wsnd; static int nwsnd;
static int wsnd_load(const char *dir, const char *const *clname, int ncl) {
	char p[160]; snprintf(p, sizeof(p), "%swpn_snd.txt", dir); FILE *f = fopen(p, "rb"); if (!f) return 0;
	char line[512]; int cap = 0;
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
		char *a = strchr(line, '|'); if (!a) continue;
		*a++ = 0; char *b = strchr(a, '|'); if (!b) continue;
		*b++ = 0; char *c = strchr(b, '|'); if (c) *c++ = 0;
		int cl = -1; for (int i = 0; i < ncl; i++) if (!strcmp(clname[i], a)) cl = i;
		if (cl < 0) continue;
		if (nwsnd >= cap) { cap = cap ? cap * 2 : 128; WSnd *n = realloc(wsnd, (size_t)cap * sizeof(WSnd)); if (!n) break; wsnd = n; }
		WSnd *w = &wsnd[nwsnd++]; memset(w, 0, sizeof(*w)); snprintf(w->sh, sizeof(w->sh), "%.11s", line); w->cl = (unsigned char)cl; w->secs = c ? (float)atof(c) : 0;
		for (char *t = strtok(b, " \r\n"); t && w->n < 10; t = strtok(NULL, " \r\n")) {
			char *q = strchr(t, ':'); if (!q) continue;
			*q++ = 0; w->e[w->n].f = (float)atof(t); snprintf(w->e[w->n].ev, sizeof(w->e[w->n].ev), "%s", q); w->n++; }
	}
	fclose(f); return nwsnd;
}
static const WSnd *wsnd_row(const char *sh, int cl) { for (int i = 0; i < nwsnd; i++) if (wsnd[i].cl == cl && !strcmp(wsnd[i].sh, sh)) return &wsnd[i]; return NULL; }
// the seconds the PC plays the clip in (0 = its own length)
static float wsnd_secs(const char *sh, int cl) { const WSnd *w = wsnd_row(sh, cl); return w ? w->secs : 0; }
// schedules the clip's sounds over secs seconds; -> how many (-1: the table has no row for the clip - the caller's old way)
static int wsnd_play(const char *sh, int cl, float secs) {
	const WSnd *w = wsnd_row(sh, cl); if (!w) return -1;
	const char *base = snd_wpn(sh)->base; int n = 0; char nm[96];
	for (int i = 0; i < w->n; i++) {
		snprintf(nm, sizeof(nm), "%s/%s", base, w->e[i].ev); if (ev_find(nm) < 0) continue;
		float t = w->e[i].f * secs; if (t > 0.004f) ev_play_later(nm, NULL, t); else ev_play(nm, NULL);
		n++; }
	return n;
}
