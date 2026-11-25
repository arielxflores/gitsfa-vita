// 48 kHz stereo software mixer on its own thread (sceAudioOut main port).
// The game thread never touches voices directly: it posts commands to a ring buffer the mixer drains each grain.
//
// Sounds: sfx.gsnd ("GSN2": block IMA-ADPCM at each sound's own rate, sorted by name) is held in RAM and decoded
// by the mixer; old 48 kHz PCM "GSND" packs still load. Every rate is resampled to 48 kHz (linear).
// Streams: music (2 slots, crossfaded), two ambience beds and up to four voice lines are read from disk by an
// I/O thread (block ADPCM: ".gstr" files and the "voice.gvp" pack) into per-stream rings and decoded by the mixer.
#pragma once
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>

#define ABF 1024                        // ADPCM frames per block (per channel)
#define ABB (4 + ABF / 2)               // bytes per channel block
// fmt 0: 48 kHz PCM16 in pcm; fmt 1: block ADPCM in adp (nblk blocks at rate Hz)
typedef struct { char name[48]; int ch; int frames; short *pcm; int rate, fmt, nblk; const unsigned char *adp; } Sound;
static Sound *snds; static int nsnd;
#define NVOICE 24
typedef struct { int active; const Sound *s; int pos; int vol; int fade; int id; int gl, gr;
	unsigned frac, step; int blk; short dec[2][ABF + 1]; } Voice;   // vol 0..65536, fade per sample
static Voice voices[NVOICE];
// streams: 0,1 music (crossfade), 2,3 ambience, 4..9 voice lines
#define NSTREAM 10
#define SRING 48                        // blocks buffered per stream (about 1 s at 48 kHz)
enum { ST_IDLE, ST_OPEN, ST_PLAY, ST_CLOSE };
typedef struct {
	volatile int state; int id;
	char path[112]; unsigned base; int hdr;   // hdr: the file starts with a GSTR header (else rate..nblk are given)
	int rate, ch, frames, nblk, loop_blk, loop;
	SceUID fd; volatile int wr, rd, eof; int fpos_blk; unsigned short rlen[SRING];  // valid frames of each ring block
	int i; unsigned frac, step; int vol, tvol, vstep, gl, gr, dec_ok; volatile int stopping;
	short dec[2][ABF + 1];
	unsigned char ring[SRING][2 * ABB];
} Stream;
static Stream streams[NSTREAM];
typedef struct { int op; const Sound *s; int vol; int id; char prefix[48]; int ms; int gl, gr;
	int cls; char path[112]; unsigned base; int hdr, rate, ch, frames, nblk, loop; } Cmd;
// op 1 = play, 2 = fade others, 3 = start stream (cls 0 music, 1 amb A, 2 amb B, 3 voice), 4 = stop streams of class cls
#define NCMD 64
static Cmd cmds[NCMD]; static volatile unsigned cmd_w = 0, cmd_r = 0;
static Cmd pend[3]; static int pend_on[3];      // music/ambience starts waiting for their slot to finish fading out
static volatile int audio_run = 1, audio_maxvoices = 0, audio_limited = 0, voice_seq = 0;
static volatile int stream_underruns = 0, stream_open_fail = 0;
static int audio_port = -1;
static float master_gain = 0.55f;
static float music_gain = 0.5f, amb_gain = 0.55f, voice_gain = 1.0f;
// diagnostics: capture of the final mixed output, and timing of each grain
static short *cap_buf; static volatile int cap_frames = 0, cap_max = 0;
static volatile int audio_late = 0, audio_grains = 0, audio_worst_us = 0, audio_mix_us = 0;

// ---------------------------------------------------------------- loading
static int snd_cmp(const void *a, const void *b) { return strcmp(((const Sound *)a)->name, ((const Sound *)b)->name); }
static void sort_sounds(void) { qsort(snds, nsnd, sizeof(Sound), snd_cmp); }
// loads a pack and adds it to the table (call before audio_init; packs can be stacked)
static int load_sounds(const char *path) {
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	char mg[4]; int n = 0; if (fread(mg, 4, 1, f) != 1 || fread(&n, 4, 1, f) != 1 || n <= 0) { fclose(f); return -2; }
	Sound *t = realloc(snds, sizeof(Sound) * (nsnd + n)); if (!t) { fclose(f); return -3; } snds = t;
	if (!memcmp(mg, "GSND", 4)) {
		for (int i = 0; i < n; i++) {
			Sound *s = &snds[nsnd + i]; memset(s, 0, sizeof(*s)); fread(s->name, 48, 1, f); fread(&s->ch, 4, 1, f); fread(&s->frames, 4, 1, f);
			s->rate = 48000; s->fmt = 0; s->pcm = malloc(s->frames * s->ch * 2); fread(s->pcm, 2, s->frames * s->ch, f);
		}
	} else if (!memcmp(mg, "GSN2", 4)) {
		unsigned bf = 0, dsz = 0; fread(&bf, 4, 1, f); fread(&dsz, 4, 1, f);
		if (bf != ABF) { fclose(f); return -4; }
		struct { char name[48]; unsigned rate; unsigned short ch, flags; unsigned frames, nblk, off; } e;
		unsigned char *blob = malloc(dsz); if (!blob) { fclose(f); return -5; }
		for (int i = 0; i < n; i++) {
			if (fread(&e, sizeof(e), 1, f) != 1) { fclose(f); return -6; }
			Sound *s = &snds[nsnd + i]; memset(s, 0, sizeof(*s));
			memcpy(s->name, e.name, 48); s->name[47] = 0; s->ch = e.ch; s->frames = e.frames; s->rate = e.rate; s->fmt = 1; s->nblk = e.nblk; s->adp = blob + e.off;
		}
		if (fread(blob, 1, dsz, f) != dsz) { fclose(f); return -7; }
	} else { fclose(f); return -8; }
	fclose(f); nsnd += n; sort_sounds(); return 0;
}
static Sound *new_sound(const char *name, int frames) {
	snds = realloc(snds, sizeof(Sound) * (nsnd + 1)); Sound *t = &snds[nsnd++]; memset(t, 0, sizeof(*t));
	strcpy(t->name, name); t->ch = 1; t->frames = frames; t->rate = 48000; t->pcm = malloc(frames * 2); return t;
}
static void add_melody(void) { // C5 E5 G5 C6, 0.35 s each: easy to tell clean from static
	Sound *t = new_sound("test_melody", 4 * 16800);
	static const float hz[4] = { 523.25f, 659.25f, 783.99f, 1046.5f };
	for (int n = 0; n < 4; n++) for (int i = 0; i < 16800; i++) {
		float e = i < 240 ? i / 240.f : i > 14000 ? fmaxf(0, (16800 - i) / 2800.f) : 1;
		t->pcm[n * 16800 + i] = (short)(11000 * e * sinf(2 * 3.14159265f * hz[n] * i / 48000.f)); }
	sort_sounds();
}
static void add_tone(void) { // 0.5 s 440 Hz sine, used to check pitch/timing of the output path
	Sound *t = new_sound("test_tone_440", 24000);
	for (int i = 0; i < 24000; i++) { float e = i < 480 ? i / 480.f : i > 23520 ? (24000 - i) / 480.f : 1; t->pcm[i] = (short)(12000 * e * sinf(2 * 3.14159265f * 440 * i / 48000.f)); }
	sort_sounds();
}
// a random sound whose name starts with prefix (the table is sorted: binary search to the first match)
static const Sound *find_sound(const char *prefix) {
	int pl = strlen(prefix), lo = 0, hi = nsnd;
	while (lo < hi) { int m = (lo + hi) >> 1; if (strncmp(snds[m].name, prefix, pl) < 0) lo = m + 1; else hi = m; }
	int n = 0; const Sound *pick = NULL;
	for (int i = lo; i < nsnd && !strncmp(snds[i].name, prefix, pl); i++) { n++; if (rand() % n == 0) pick = &snds[i]; }
	return pick;
}
static int count_sounds(const char *prefix) {
	int pl = strlen(prefix), lo = 0, hi = nsnd, n = 0;
	while (lo < hi) { int m = (lo + hi) >> 1; if (strncmp(snds[m].name, prefix, pl) < 0) lo = m + 1; else hi = m; }
	for (int i = lo; i < nsnd && !strncmp(snds[i].name, prefix, pl); i++) n++;
	return n;
}
static int sounds_bytes(void) { long b = 0; for (int i = 0; i < nsnd; i++) b += snds[i].fmt ? (long)snds[i].nblk * snds[i].ch * ABB : (long)snds[i].frames * snds[i].ch * 2; return (int)b; }
// streamed voice-line packs (index in RAM, audio read from disk while it plays); flags = pack number
typedef struct { char name[48]; unsigned rate; unsigned short ch, flags; unsigned frames, nblk, off; } VEntry;
static VEntry *vlines; static int nvline, nvpack; static char vpack_path[8][112];
static int vline_cmp(const void *a, const void *b) { return strcmp(((const VEntry *)a)->name, ((const VEntry *)b)->name); }
static int voice_load(const char *path) {   // adds one voice_N.gvp pack
	if (nvpack >= 8) return -4;
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	char mg[4]; int n = 0; unsigned bf = 0, dsz = 0;
	if (fread(mg, 4, 1, f) != 1 || memcmp(mg, "GVP1", 4) || fread(&n, 4, 1, f) != 1 || fread(&bf, 4, 1, f) != 1 || fread(&dsz, 4, 1, f) != 1 || bf != ABF || n <= 0) { fclose(f); return -2; }
	VEntry *t = realloc(vlines, sizeof(VEntry) * (nvline + n)); if (!t) { fclose(f); return -3; } vlines = t;
	if (fread(vlines + nvline, sizeof(VEntry), n, f) != (size_t)n) { fclose(f); return -3; }
	fclose(f); for (int i = 0; i < n; i++) vlines[nvline + i].flags = nvpack;
	strncpy(vpack_path[nvpack], path, 111); vpack_path[nvpack][111] = 0; nvpack++; nvline += n;
	qsort(vlines, nvline, sizeof(VEntry), vline_cmp); return n;
}
// loads sfx_0.gsnd, sfx_1.gsnd ... and voice_0.gvp ... from dir (ending in '/'); returns the number of sounds
static int audio_load_all(const char *dir) {
	char p[112]; int ok = 0;
	for (int i = 0; i < 8; i++) { snprintf(p, sizeof(p), "%ssfx_%d.gsnd", dir, i); if (load_sounds(p) < 0) break; ok++; }
	for (int i = 0; i < 8; i++) { snprintf(p, sizeof(p), "%svoice_%d.gvp", dir, i); if (voice_load(p) < 0) break; }
	return ok ? nsnd : -1;
}
static const VEntry *find_vline(const char *prefix) {
	int pl = strlen(prefix), lo = 0, hi = nvline;
	while (lo < hi) { int m = (lo + hi) >> 1; if (strncmp(vlines[m].name, prefix, pl) < 0) lo = m + 1; else hi = m; }
	int n = 0; const VEntry *pick = NULL;
	for (int i = lo; i < nvline && !strncmp(vlines[i].name, prefix, pl); i++) { n++; if (rand() % n == 0) pick = &vlines[i]; }
	return pick;
}

// ---------------------------------------------------------------- game-thread API
static void post(Cmd *c) {
	unsigned w = cmd_w; if (w - cmd_r >= NCMD) return; // full: drop
	cmds[w % NCMD] = *c; __sync_synchronize(); cmd_w = w + 1;
}
static void pan_gains(float pan, int *gl, int *gr) {
	if (pan < -1) pan = -1;
	if (pan > 1) pan = 1;
	*gl = (int)(256 * (pan > 0 ? 1 - 0.7f * pan : 1)); *gr = (int)(256 * (pan < 0 ? 1 + 0.7f * pan : 1));
}
// vol: 0..256
// pan: -1 left .. +1 right (equal-power-ish)
static int play_pan(const char *prefix, int vol, float pan) {
	const Sound *s = find_sound(prefix); if (!s) return -1;
	Cmd c; memset(&c, 0, sizeof(c)); c.op = 1; c.s = s; c.vol = vol * 256; c.id = ++voice_seq; pan_gains(pan, &c.gl, &c.gr); post(&c); return c.id;
}
static int play(const char *prefix, int vol) { return play_pan(prefix, vol, 0); }
// sounds scheduled for later (shell casings landing)
typedef struct { float t; char prefix[48]; int vol; } Delayed;
static Delayed delayed[16]; static int ndelayed;
static void play_later(const char *prefix, int vol, float delay) {
	if (ndelayed >= 16) return;
	Delayed *d = &delayed[ndelayed++]; d->t = delay; strncpy(d->prefix, prefix, 47); d->prefix[47] = 0; d->vol = vol;
}
static void update_delayed(float dt) {
	for (int i = 0; i < ndelayed; ) { delayed[i].t -= dt; if (delayed[i].t <= 0) { play(delayed[i].prefix, delayed[i].vol); delayed[i] = delayed[--ndelayed]; } else i++; }
}
// fade out every playing voice whose sound name starts with prefix, except voice keep_id
static void fade_others(const char *prefix, int keep_id, int ms) {
	Cmd c; memset(&c, 0, sizeof(c)); c.op = 2; c.id = keep_id; c.ms = ms; strncpy(c.prefix, prefix, 47); post(&c);
}
// music: a .gstr file; crossfades from whatever music is playing. vol 0..256, loop 0/1
static int music_play(const char *path, int loop, int vol, int fade_ms) {
	Cmd c; memset(&c, 0, sizeof(c)); c.op = 3; c.cls = 0; strncpy(c.path, path, 111); c.hdr = 1; c.loop = loop; c.vol = vol * 256; c.ms = fade_ms; c.id = ++voice_seq; post(&c); return c.id;
}
static void music_stop(int fade_ms) { Cmd c; memset(&c, 0, sizeof(c)); c.op = 4; c.cls = 0; c.ms = fade_ms; post(&c); }
static int music_playing(void) { return streams[0].state != ST_IDLE || streams[1].state != ST_IDLE || pend_on[0]; }
// looped ambience bed in slot 0 or 1
static int amb_play(int slot, const char *path, int vol) {
	Cmd c; memset(&c, 0, sizeof(c)); c.op = 3; c.cls = 1 + (slot & 1); strncpy(c.path, path, 111); c.hdr = 1; c.loop = 1; c.vol = vol * 256; c.ms = 1500; c.id = ++voice_seq; post(&c); return c.id;
}
static void amb_stop(int slot, int fade_ms) { Cmd c; memset(&c, 0, sizeof(c)); c.op = 4; c.cls = 1 + (slot & 1); c.ms = fade_ms; post(&c); }
// a voice line from voice.gvp (streamed); -1 if the prefix matches nothing or no pack is loaded
static int voice_play(const char *prefix, int vol, float pan) {
	if (!nvline) return -1;
	const VEntry *e = find_vline(prefix); if (!e) return -1;
	Cmd c; memset(&c, 0, sizeof(c)); c.op = 3; c.cls = 3; strncpy(c.path, vpack_path[e->flags & 7], 111); c.base = e->off; c.hdr = 0;
	c.rate = e->rate; c.ch = e->ch; c.frames = e->frames; c.nblk = e->nblk; c.loop = 0; c.vol = vol * 256; c.ms = 0; pan_gains(pan, &c.gl, &c.gr); c.id = ++voice_seq; post(&c); return c.id;
}
static void voice_stop_all(int fade_ms) { Cmd c; memset(&c, 0, sizeof(c)); c.op = 4; c.cls = 3; c.ms = fade_ms; post(&c); }
static int voice_busy(void) { for (int i = 4; i < NSTREAM; i++) if (streams[i].state != ST_IDLE) return 1; return 0; }

// per-map ambience beds (two looped files in <data>/audio/), taken from the game's ambience bank
static const struct { const char *map, *a, *b; } AMB_MAPS[] = {
	{ "downtown", "ambi_hongkong_outside_01", "ambi_distance_city" }, { "berutalube", "ambi_outside", "ambi_distance_city" },
	{ "ecenter", "ambi_ecenter_outside", "ambi_ecenter_main_hall" }, { "ghost_storage", "ambi_ghost_01", "ambi_large_room" },
	{ "headquarter", "ambi_dm01_outside_01", "ambi_dm01_lobby_01" }, { "port", "ambi_port_outside_01", "ambi_port_sea_01" },
	{ "pss9", "ambi_pss9_machine_hall_01", "ambi_pss9_hallway_01" }, { "ruinednight", "ambi_wind", "ambi_urban_fire" },
	{ "ruinedtokyo", "ambi_wind", "ambi_distance_siren" }, { "trainstation", "ambi_train_station", "ambi_distance_train_01" },
	{ "underground", "ambi_urban_duct", "ambi_basement" }, { "urban", "ambi_urban_outside", "ambi_urban_distant_siren_01" },
	{ "zioplant", "ambi_zio_construction", "ambi_zio_fan" }, { "shooting", "ambi_matrix_loop", NULL },
	{ "tutorial", "ambi_matrix_loop", NULL }, { "tutorial2", "ambi_matrix_loop", NULL }, { "tutorial3", "ambi_matrix_loop", NULL },
};
// starts the map's ambience (dir = the data folder, ending in '/'); returns how many beds started
static int amb_for_map(const char *dir, const char *stem, int vol) {
	for (unsigned i = 0; i < sizeof(AMB_MAPS) / sizeof(AMB_MAPS[0]); i++) if (!strcmp(AMB_MAPS[i].map, stem)) {
		char p[112]; int n = 1;
		snprintf(p, sizeof(p), "%s%s.gstr", dir, AMB_MAPS[i].a); amb_play(0, p, vol);
		if (AMB_MAPS[i].b) { snprintf(p, sizeof(p), "%s%s.gstr", dir, AMB_MAPS[i].b); amb_play(1, p, vol * 3 / 4); n++; } else amb_stop(1, 800);
		return n;
	}
	amb_stop(0, 800); amb_stop(1, 800); return 0;
}

// ---------------------------------------------------------------- ADPCM
static const short ima_step[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const signed char ima_idx[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
// one channel block -> out[0..ABF-1]
static void adp_decode(const unsigned char *p, short *out) {
	int pred = (short)(p[0] | (p[1] << 8)), ix = p[2] > 88 ? 88 : p[2]; out[0] = (short)pred; const unsigned char *q = p + 4;
	for (int k = 1; k < ABF; k += 2) {
		int byte = *q++;
		for (int h = 0; h < 2 && k + h < ABF; h++) {
			int nib = h ? byte >> 4 : byte & 15, st = ima_step[ix], diff = st >> 3;
			if (nib & 4) diff += st;
			if (nib & 2) diff += st >> 1;
			if (nib & 1) diff += st >> 2;
			if (nib & 8) diff = -diff;
			pred += diff; if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
			ix += ima_idx[nib]; if (ix < 0) ix = 0; else if (ix > 88) ix = 88;
			out[k + h] = (short)pred;
		}
	}
}
static inline short adp_first(const unsigned char *p) { return (short)(p[0] | (p[1] << 8)); }
// decode block b of an in-RAM sound into v->dec (plus the next block's first sample, for interpolation)
static void voice_block(Voice *v, int b) {
	const Sound *s = v->s; int ch = s->ch;
	for (int c = 0; c < ch; c++) {
		adp_decode(s->adp + ((size_t)b * ch + c) * ABB, v->dec[c]);
		v->dec[c][ABF] = b + 1 < s->nblk ? adp_first(s->adp + ((size_t)(b + 1) * ch + c) * ABB) : v->dec[c][ABF - 1];
	}
	v->blk = b;
}

// ---------------------------------------------------------------- mixer (audio thread)
static int stream_pick(int cls) {
	if (cls == 0) { for (int i = 0; i < 2; i++) if (streams[i].state == ST_IDLE) return i; return -1; }
	if (cls == 1 || cls == 2) return streams[cls + 1].state == ST_IDLE ? cls + 1 : -1;
	int best = -1, oldest = 0x7fffffff;
	for (int i = 4; i < NSTREAM; i++) { if (streams[i].state == ST_IDLE) return i; if (streams[i].id < oldest && streams[i].state == ST_PLAY && !streams[i].stopping) { oldest = streams[i].id; best = i; } }
	if (best >= 0) { streams[best].tvol = 0; streams[best].vstep = streams[best].vol / 480 + 1; streams[best].stopping = 1; }  // make room: 10 ms fade
	return -1;
}
static void stream_fade_out(Stream *s, int ms) {
	if (s->state == ST_OPEN) { s->tvol = 0; s->vstep = 1 << 20; s->stopping = 1; return; }
	if (s->state != ST_PLAY) return;
	int n = ms * 48; s->tvol = 0; s->vstep = s->vol / (n > 0 ? n : 1) + 1; s->stopping = 1;
}
static int stream_start(const Cmd *c) {
	int si = stream_pick(c->cls); if (si < 0) return 0;
	Stream *s = &streams[si]; memcpy(s->path, c->path, sizeof(s->path)); s->base = c->base; s->hdr = c->hdr;
	s->rate = c->rate; s->ch = c->ch; s->frames = c->frames; s->nblk = c->nblk; s->loop = c->loop; s->loop_blk = 0;
	s->id = c->id; s->gl = c->gl ? c->gl : 256; s->gr = c->gr ? c->gr : 256; s->stopping = 0;
	int n = c->ms * 48; s->tvol = c->vol; s->vol = n > 0 ? 0 : c->vol; s->vstep = n > 0 ? c->vol / n + 1 : 0;
	s->wr = s->rd = 0; s->eof = 0; s->i = 0; s->frac = 0; s->dec_ok = 0;
	__sync_synchronize(); s->state = ST_OPEN; return 1;
}
static void run_cmds(void) {
	for (int k = 0; k < 3; k++) if (pend_on[k] && stream_start(&pend[k])) pend_on[k] = 0;
	while (cmd_r != cmd_w) {
		Cmd *c = &cmds[cmd_r % NCMD];
		if (c->op == 1) {
			int best = -1, oldest = 0x7fffffff;
			for (int i = 0; i < NVOICE; i++) { if (!voices[i].active) { best = i; break; } if (voices[i].id < oldest) { oldest = voices[i].id; best = i; } }
			Voice *v = &voices[best]; v->s = c->s; v->pos = 0; v->vol = c->vol; v->fade = 0; v->id = c->id; v->gl = c->gl; v->gr = c->gr;
			v->frac = 0; v->step = (unsigned)(((unsigned long long)c->s->rate << 16) / 48000); v->blk = -1; v->active = 1;
		} else if (c->op == 2) {
			int pl = strlen(c->prefix);
			for (int i = 0; i < NVOICE; i++) if (voices[i].active && voices[i].id != c->id && !voices[i].fade && !strncmp(voices[i].s->name, c->prefix, pl)) {
				int n = c->ms * 48; voices[i].fade = voices[i].vol / (n > 0 ? n : 1) + 1; }
		} else if (c->op == 3) {
			if (c->cls == 0) for (int i = 0; i < 2; i++) stream_fade_out(&streams[i], c->ms > 0 ? c->ms : 300);
			if (c->cls == 1 || c->cls == 2) stream_fade_out(&streams[c->cls + 1], 700);
			if (!stream_start(c) && c->cls < 3) { pend[c->cls] = *c; pend_on[c->cls] = 1; }
		} else if (c->op == 4) {
			int a = c->cls == 0 ? 0 : c->cls == 3 ? 4 : c->cls + 1, b = c->cls == 0 ? 2 : c->cls == 3 ? NSTREAM : c->cls + 2;
			for (int i = a; i < b; i++) stream_fade_out(&streams[i], c->ms);
			if (c->cls < 3) pend_on[c->cls] = 0;
		}
		__sync_synchronize(); cmd_r++;
	}
}
// soft knee limiter: linear below 22000, smoothly approaches 32767 above
static inline short limit(int x) {
	int a = x < 0 ? -x : x;
	if (a > 22000) { audio_limited++; int e = a - 22000; a = 22000 + (int)((long long)e * 10767 / (e + 10767)); }
	return (short)(x < 0 ? -a : a);
}
#define GRAIN 1024
static int mix[GRAIN * 2];
static void mix_voice(Voice *v, int mg) {
	const Sound *s = v->s; int pos = v->pos, vol = v->vol, ch = s->ch; unsigned frac = v->frac, step = v->step;
	for (int k = 0; k < GRAIN; k++) {
		if (pos >= s->frames) break;
		int l, r, fr = (int)(frac >> 1);
		if (s->fmt == 0) { int p1 = pos + 1 < s->frames ? pos + 1 : pos;
			if (ch == 2) { l = s->pcm[pos * 2]; r = s->pcm[pos * 2 + 1]; if (fr) { l += ((s->pcm[p1 * 2] - l) * fr) >> 15; r += ((s->pcm[p1 * 2 + 1] - r) * fr) >> 15; } }
			else { l = s->pcm[pos]; if (fr) l += ((s->pcm[p1] - l) * fr) >> 15; r = l; } }
		else {
			int b = pos >> 10, i = pos & (ABF - 1); if (b != v->blk) voice_block(v, b);
			l = v->dec[0][i]; l += ((v->dec[0][i + 1] - l) * fr) >> 15;
			if (ch == 2) { r = v->dec[1][i]; r += ((v->dec[1][i + 1] - r) * fr) >> 15; } else r = l;
		}
		int g = ((vol >> 8) * mg) >> 8;  // 0..256
		mix[k * 2] += (((l * g) >> 8) * v->gl) >> 8; mix[k * 2 + 1] += (((r * g) >> 8) * v->gr) >> 8;
		if (v->fade) { vol -= v->fade; if (vol <= 0) { pos = s->frames; break; } }
		frac += step; pos += frac >> 16; frac &= 0xffff;
	}
	v->pos = pos; v->vol = vol; v->frac = frac; if (pos >= s->frames) v->active = 0;
}
static int stream_dec(Stream *s) {   // decode ring block rd; 0 if it has not been read yet
	if (s->rd >= s->wr) return 0;
	int slot = s->rd % SRING, len = s->rlen[slot]; const unsigned char *p = s->ring[slot];
	for (int c = 0; c < s->ch; c++) {
		adp_decode(p + c * ABB, s->dec[c]);
		short nx = s->dec[c][len - 1];
		if (s->rd + 1 < s->wr) nx = adp_first(s->ring[(s->rd + 1) % SRING] + c * ABB);
		s->dec[c][len] = nx;   // sample after the last valid one (next block, or the loop start)
	}
	s->dec_ok = 1; return 1;
}
static void mix_stream(Stream *s, int mg, float gain) {
	int gg = (int)(gain * 256), ch = s->ch;
	for (int k = 0; k < GRAIN; k++) {
		if (!s->dec_ok && !stream_dec(s)) { if (s->eof) s->state = ST_CLOSE; else if (s->wr > 0) stream_underruns++; return; }
		int len = s->rlen[s->rd % SRING], i = s->i, fr = (int)(s->frac >> 1);
		int l = s->dec[0][i]; l += ((s->dec[0][i + 1] - l) * fr) >> 15;
		int r = l; if (ch == 2) { r = s->dec[1][i]; r += ((s->dec[1][i + 1] - r) * fr) >> 15; }
		int g = ((((s->vol >> 8) * mg) >> 8) * gg) >> 8;
		mix[k * 2] += (((l * g) >> 8) * s->gl) >> 8; mix[k * 2 + 1] += (((r * g) >> 8) * s->gr) >> 8;
		if (s->vol != s->tvol) { if (s->vol < s->tvol) { s->vol += s->vstep; if (s->vol > s->tvol) s->vol = s->tvol; } else { s->vol -= s->vstep; if (s->vol < s->tvol) s->vol = s->tvol; } }
		else if (s->stopping && s->vol <= 0) { s->state = ST_CLOSE; return; }
		s->frac += s->step; s->i += s->frac >> 16; s->frac &= 0xffff;
		while (s->i >= len) {
			s->i -= len; s->dec_ok = 0; __sync_synchronize(); s->rd++;
			if (!stream_dec(s)) { if (s->eof) s->state = ST_CLOSE; else stream_underruns++; s->i = 0; return; }
			len = s->rlen[s->rd % SRING];
		}
	}
}
// ---------------------------------------------------------------- stream reader (I/O thread)
static int stream_open(Stream *s) {
	s->fd = sceIoOpen(s->path, SCE_O_RDONLY, 0); if (s->fd < 0) return -1;
	if (s->hdr) {   // GSTR: magic, rate, ch (u16) + pad, frames, nblk, frames per block, loop start frame, data offset
		unsigned h[8]; if (sceIoRead(s->fd, h, 32) != 32 || memcmp(h, "GSTR", 4) || h[5] != ABF) return -2;
		s->rate = h[1]; s->ch = h[2] & 0xffff; s->frames = h[3]; s->nblk = h[4]; s->loop_blk = h[6] / ABF; s->base = h[7];
	}
	if (s->ch < 1 || s->ch > 2 || s->nblk <= 0 || s->rate <= 0) return -3;
	s->step = (unsigned)(((unsigned long long)s->rate << 16) / 48000); s->fpos_blk = -1; return 0;
}
static void stream_fill(Stream *s) {
	int bsz = s->ch * ABB;
	while (s->wr - s->rd < SRING && !s->eof) {
		int j = s->wr, fb;
		if (j < s->nblk) fb = j;
		else if (s->loop) { int span = s->nblk - s->loop_blk; fb = s->loop_blk + (j - s->nblk) % (span > 0 ? span : 1); }
		else { s->eof = 1; break; }
		if (fb != s->fpos_blk) sceIoLseek(s->fd, (SceOff)s->base + (SceOff)fb * bsz, SCE_SEEK_SET);
		if (sceIoRead(s->fd, s->ring[j % SRING], bsz) != bsz) { s->eof = 1; break; }
		s->fpos_blk = fb + 1;
		s->rlen[j % SRING] = fb == s->nblk - 1 ? (unsigned short)(s->frames - fb * ABF) : ABF;
		__sync_synchronize(); s->wr = j + 1;
		if (!s->loop && j + 1 >= s->nblk) s->eof = 1;
		if (s->state == ST_CLOSE) break;
	}
}
static void audio_io_step(void) {
	for (int i = 0; i < NSTREAM; i++) {
		Stream *s = &streams[i];
		if (s->state == ST_OPEN) {
			if (s->stopping || stream_open(s) < 0) {
				if (s->fd >= 0) sceIoClose(s->fd);
				s->fd = -1; if (!s->stopping) stream_open_fail++;
				__sync_synchronize(); s->state = ST_IDLE; continue; }
			stream_fill(s); __sync_synchronize(); s->state = ST_PLAY;   // primed before the mixer sees it
		} else if (s->state == ST_PLAY) stream_fill(s);
		else if (s->state == ST_CLOSE) { if (s->fd >= 0) { sceIoClose(s->fd); } s->fd = -1; __sync_synchronize(); s->state = ST_IDLE; }
	}
}
static int audio_io_thread(SceSize args, void *argp) {
	while (audio_run) { audio_io_step(); sceKernelDelayThread(3000); }
	return 0;
}
// one grain of the mixer into mix[] (the audio thread's work; separate so it can be tested off-device)
static int audio_mix_grain(void) {
	run_cmds();
	memset(mix, 0, sizeof(mix)); int nact = 0;
	int mg = (int)(master_gain * 256);
	for (int i = 0; i < NVOICE; i++) { Voice *v = &voices[i]; if (!v->active) continue; nact++; mix_voice(v, mg); }
	for (int i = 0; i < NSTREAM; i++) { Stream *s = &streams[i]; if (s->state != ST_PLAY) continue; nact++;
		mix_stream(s, mg, i < 2 ? music_gain : i < 4 ? amb_gain : voice_gain); }
	return nact;
}
static volatile int audio_paused = 0, audio_alive = 0, audio_out_err = 0, audio_out_errcode = 0, audio_open_res = 0;
// audio_variant: 1 = MAIN port + static buffers, 2 = MAIN + uncached memblock, 3 = BGM port + static, 4 = BGM + uncached
static volatile int audio_variant = 1, audio_variant_cur = 0;
static short *ubuf[2];
static int open_variant(int v) {
	if (audio_port >= 0) { sceAudioOutReleasePort(audio_port); audio_port = -1; }
	int type = (v == 3 || v == 4) ? SCE_AUDIO_OUT_PORT_TYPE_BGM : SCE_AUDIO_OUT_PORT_TYPE_MAIN;
	audio_port = sceAudioOutOpenPort(type, GRAIN, 48000, SCE_AUDIO_OUT_MODE_STEREO);
	if (audio_port >= 0) { int vols[2] = { SCE_AUDIO_OUT_MAX_VOL, SCE_AUDIO_OUT_MAX_VOL }; sceAudioOutSetVolume(audio_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols); }
	audio_variant_cur = v; return audio_port;
}
static int audio_thread(SceSize args, void *argp) {
	SceUID mb = sceKernelAllocMemBlock("gitsfa_audiobuf", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 16 * 1024, NULL);
	if (mb >= 0) { void *base; sceKernelGetMemBlockBase(mb, &base); ubuf[0] = base; ubuf[1] = (short *)((char *)base + 8192); memset(base, 0, 16384); }
	audio_open_res = open_variant(audio_variant); audio_alive = 1;
	if (audio_port < 0) return 0;
	static short out[2][GRAIN * 2]; int buf = 0;
	SceUInt64 last = sceKernelGetProcessTimeWide();
	while (audio_run) {
		if (audio_variant != audio_variant_cur) { if (open_variant(audio_variant) < 0) audio_out_errcode = audio_port; last = sceKernelGetProcessTimeWide(); }
		if (audio_paused) { sceKernelDelayThread(5000); last = sceKernelGetProcessTimeWide(); continue; }
		SceUInt64 t_start = sceKernelGetProcessTimeWide();
		int nact = audio_mix_grain();
		if (nact > audio_maxvoices) audio_maxvoices = nact;
		int unc = (audio_variant_cur == 2 || audio_variant_cur == 4) && ubuf[0];
		short *o = unc ? ubuf[buf] : out[buf]; buf ^= 1;
		for (int k = 0; k < GRAIN * 2; k++) o[k] = limit(mix[k]);
		if (cap_buf && cap_frames + GRAIN <= cap_max) { memcpy(cap_buf + cap_frames * 2, o, GRAIN * 4); cap_frames += GRAIN; }
		int mus = (int)(sceKernelGetProcessTimeWide() - t_start); if (mus > audio_mix_us) audio_mix_us = mus;
		int orr = sceAudioOutOutput(audio_port, o); if (orr < 0) { audio_out_err++; audio_out_errcode = orr; }
		SceUInt64 now = sceKernelGetProcessTimeWide(); int gap = (int)(now - last); last = now; audio_grains++;
		if (audio_grains > 4) { if (gap > audio_worst_us) audio_worst_us = gap; if (gap > 30000) audio_late++; }
	}
	return 0;
}
static int audio_core = -1;
static int audio_init(void) {
	cap_max = 48000 * 30; cap_buf = malloc(cap_max * 4);
	for (int i = 0; i < NSTREAM; i++) { streams[i].fd = -1; streams[i].state = ST_IDLE; }
	// mixer on core 2: core 3 is shared with system work (screenshots, etc.), which caused short gaps
	SceUID th = sceKernelCreateThread("gitsfa_audio", audio_thread, 0x10000100 - 16, 0x40000, 0, 0x40000, NULL);
	if (th < 0) return th;
	audio_core = 2; sceKernelStartThread(th, 0, NULL);
	// stream reader: below the mixer's priority, any core
	SceUID io = sceKernelCreateThread("gitsfa_audio_io", audio_io_thread, 0x10000100 + 10, 0x4000, 0, 0, NULL);
	if (io >= 0) sceKernelStartThread(io, 0, NULL);
	for (int i = 0; i < 60 && !audio_alive; i++) sceKernelDelayThread(5000);
	return audio_open_res < 0 ? audio_open_res : 0;
}
