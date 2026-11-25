// 48 kHz stereo software mixer on its own thread (sceAudioOut main port).
// The game thread never touches voices directly: it posts commands to a ring buffer the mixer drains each grain.
//
// Sounds: sfx_N.gsnd ("GSN2": block IMA-ADPCM at each sound's own rate, sorted by name) are held in RAM and decoded
// by the mixer; old 48 kHz PCM "GSND" packs still load. Every rate is resampled to 48 kHz (linear).
// Streams: music (2 slots, crossfaded), two ambience beds, up to six voice lines and four positional ambience emitters
// are read from disk by an I/O thread (block ADPCM: ".gstr" files and the "voice_N.gvp" packs) into per-stream rings.
//
// Events: the original game's FMOD Designer project (sac.fev) is turned into EV_DEF / EV_LAYER below by
// tools/audio/make_events.py: per event the designed volume, 2D/3D, rolloff, min/max distance, max playbacks, priority,
// pitch/volume randomisation; per layer the samples, their volume, where on the distance / indoor / aim parameter they
// play and their volume and 3D-pan-level curves against distance. ev_play("weapon/ar/m4a1/fire_fp", pos) plays an event
// the way FMOD did (pos NULL = at the listener). Map ambience (beds crossfaded by an indoor probe, positional emitters
// from the maps' SoundData.sdf, the train / helicopter pass-bys) runs from amb_map_start + snd_update.
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
	unsigned frac, step; int blk; int ev, inst, pri, rv; short dec[2][ABF + 1]; } Voice;   // rv: the reverb send (b107, 0..256 of the voice's level)   // vol 0..524288 (b59: up to 8x the sample, the mixer caps a voice at 4x), fade per sample
static Voice voices[NVOICE];
// streams: 0,1 music (crossfade), 2,3 ambience beds, 4..9 voice lines, 10..13 ambience emitters / one-shots
#define NSTREAM 14
#define SRING 48                        // blocks buffered per stream (about 1 s at 48 kHz)
enum { ST_IDLE, ST_OPEN, ST_PLAY, ST_CLOSE };
typedef struct {
	volatile int state; int id;
	char path[112]; unsigned base; int hdr;   // hdr: the file starts with a GSTR header (else rate..nblk are given)
	int rate, ch, frames, nblk, loop_blk, loop;
	SceUID fd; volatile int wr, rd, eof; int fpos_blk; unsigned short rlen[SRING];  // valid frames of each ring block
	int i; unsigned frac, step; int vol, tvol, vstep, gl, gr, dec_ok, pri, cat, rv; volatile int stopping;
	short dec[2][ABF + 1];
	unsigned char ring[SRING][2 * ABB];
} Stream;
static Stream streams[NSTREAM];
typedef struct { int op; const Sound *s; int vol; int id; char prefix[48]; int ms; int gl, gr;
	int cls; char path[112]; unsigned base; int hdr, rate, ch, frames, nblk, loop;
	unsigned step; int ev, inst, pri, maxpb, first, cat, rv; } Cmd;
// op 1 = play, 2 = fade others, 3 = start stream (cls 0 music, 1 amb A, 2 amb B, 3 voice, 4 emitter),
// 4 = stop streams of class cls, 5 = set the volume / pan of stream id (ramp ms), 6 = fade out stream id
#define NCMD 128
static Cmd cmds[NCMD]; static volatile unsigned cmd_w = 0, cmd_r = 0;
// b37 (perf, host ThreadSanitizer): the hand-overs between the game thread, the mixer and the stream reader (the command
// ring, the streams' state, their block rings) read the published index / state with an acquire. Before, only the writer
// had a barrier: the Cortex-A9 may perform the reader's later loads (the command, the ring block, the stream's fields)
// before the load that saw them published, and use stale data (rare: a wrong or stale command, a block from the previous
// lap of the ring, a voice line cut one block early when the end-of-file flag showed before the last block's index).
#define AU_LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define AU_STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)
static Cmd pend[3]; static int pend_on[3];      // music/ambience starts waiting for their slot to finish fading out
static volatile int audio_run = 1, audio_maxvoices = 0, audio_limited = 0, voice_seq = 0;
static volatile int stream_underruns = 0, stream_open_fail = 0, audio_dropped = 0;
static int audio_port = -1;
static float master_gain = 0.55f;
// b59: the player's volume settings per category (1 = the design mix; main.c cfg_apply). Before, the events divided their
// level by these group gains (the levels of the b25 mixer) so the sliders had no effect on them; now every stream carries
// its category (cat: 0 music, 1 ambience, 2 voice, 3 effects) and the RAM sounds are effects.
enum { CAT_MUSIC, CAT_AMB, CAT_VOICE, CAT_SFX };
static float music_gain = 1.0f, amb_gain = 1.0f, voice_gain = 1.0f, sfx_gain = 1.0f;
// b59: the in-match mix (snd_set_ingame): music up, the RAM effects (gunfire, foley, impacts) down, so the stingers are heard
// and the limiter works less; the mixer glides to the targets (about 0.4 s)
static float bus_music_t = 1, bus_sfx_t = 1, bus_music = 1, bus_sfx = 1;
static void snd_set_ingame(int on) { bus_music_t = on ? 1.4f : 1.f; bus_sfx_t = on ? 0.8f : 1.f; }
static char snd_dir[96] = "";
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
static const Sound *find_sound_exact(const char *name) {
	int lo = 0, hi = nsnd - 1;
	while (lo <= hi) { int m = (lo + hi) >> 1, c = strcmp(snds[m].name, name); if (!c) return &snds[m]; if (c < 0) lo = m + 1; else hi = m - 1; }
	return NULL;
}
static __attribute__((unused)) int count_sounds(const char *prefix) {
	int pl = strlen(prefix), lo = 0, hi = nsnd, n = 0;
	while (lo < hi) { int m = (lo + hi) >> 1; if (strncmp(snds[m].name, prefix, pl) < 0) lo = m + 1; else hi = m; }
	for (int i = lo; i < nsnd && !strncmp(snds[i].name, prefix, pl); i++) n++;
	return n;
}
static int sounds_bytes(void) { long b = 0; for (int i = 0; i < nsnd; i++) b += snds[i].fmt ? (long)snds[i].nblk * snds[i].ch * ABB : (long)snds[i].frames * snds[i].ch * 2; return (int)b; }
// streamed voice-line packs (index in RAM, audio read from disk while it plays); flags = pack number
typedef struct { char name[48]; unsigned rate; unsigned short ch, flags; unsigned frames, nblk, off; } VEntry;
static VEntry *vlines; static int nvline, nvpack; static char vpack_path[8][112];
// b49 (user: "do not play voice lines that reference the pc controls"): the original's lines that name a PC key ("Press E to
// connect...", "Press Q to activate...") are never played; the on-screen text gives the Vita button instead. Found by
// transcribing every voice line (tools/audio/vo_pc_lines.txt). A line here is skipped wherever it is asked for (by name or prefix).
#define VL_PCONLY 0x8000
static const char *const VO_PC_ONLY[] = {
	"sh_tut_operator_start",              // "Press E to connect to the Firearms testing program."
	"sk_tut_operator_camo_start",         // "Press Q to activate the thermoptic camo."
	"sk_tut_operator_skillsync_start",    // "Approach your teammate and press E to download a skill."
	NULL };
static int vo_pc_only(const char *name) { for (int i = 0; VO_PC_ONLY[i]; i++) if (!strncmp(name, VO_PC_ONLY[i], strlen(VO_PC_ONLY[i]))) return 1; return 0; }
static int vline_cmp(const void *a, const void *b) { return strcmp(((const VEntry *)a)->name, ((const VEntry *)b)->name); }
static int voice_load(const char *path) {   // adds one voice_N.gvp pack
	if (nvpack >= 8) return -4;
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	char mg[4]; int n = 0; unsigned bf = 0, dsz = 0;
	if (fread(mg, 4, 1, f) != 1 || memcmp(mg, "GVP1", 4) || fread(&n, 4, 1, f) != 1 || fread(&bf, 4, 1, f) != 1 || fread(&dsz, 4, 1, f) != 1 || bf != ABF || n <= 0) { fclose(f); return -2; }
	VEntry *t = realloc(vlines, sizeof(VEntry) * (nvline + n)); if (!t) { fclose(f); return -3; } vlines = t;
	if (fread(vlines + nvline, sizeof(VEntry), n, f) != (size_t)n) { fclose(f); return -3; }
	fclose(f); for (int i = 0; i < n; i++) vlines[nvline + i].flags = nvpack | (vo_pc_only(vlines[nvline + i].name) ? VL_PCONLY : 0);
	strncpy(vpack_path[nvpack], path, 111); vpack_path[nvpack][111] = 0; nvpack++; nvline += n;
	qsort(vlines, nvline, sizeof(VEntry), vline_cmp); return n;
}
// loads sfx_0.gsnd, sfx_1.gsnd ... sfx_x.gsnd and voice_0.gvp ... voice_x.gvp from dir (ending in '/'); returns the number of sounds
static int audio_load_all(const char *dir) {
	char p[112]; int ok = 0;
	strncpy(snd_dir, dir, sizeof(snd_dir) - 1);
	for (int i = 0; i < 8; i++) { snprintf(p, sizeof(p), "%ssfx_%d.gsnd", dir, i); if (load_sounds(p) < 0) break; ok++; }
	snprintf(p, sizeof(p), "%ssfx_x.gsnd", dir); if (load_sounds(p) == 0) ok++;      // extras (indoor fire variants ...)
	for (int i = 0; i < 7; i++) { snprintf(p, sizeof(p), "%svoice_%d.gvp", dir, i); if (voice_load(p) < 0) break; }
	snprintf(p, sizeof(p), "%svoice_x.gvp", dir); voice_load(p);
	return ok ? nsnd : -1;
}
static const VEntry *find_vline(const char *prefix) {
	int pl = strlen(prefix), lo = 0, hi = nvline;
	while (lo < hi) { int m = (lo + hi) >> 1; if (strncmp(vlines[m].name, prefix, pl) < 0) lo = m + 1; else hi = m; }
	int n = 0; const VEntry *pick = NULL;
	for (int i = lo; i < nvline && !strncmp(vlines[i].name, prefix, pl); i++) { if (vlines[i].flags & VL_PCONLY) continue; n++; if (rand() % n == 0) pick = &vlines[i]; }
	return pick;
}

// ---------------------------------------------------------------- game-thread API
static void post(Cmd *c) {
	unsigned w = cmd_w; if (w - AU_LOAD(cmd_r) >= NCMD) { audio_dropped++; return; } // full: drop
	cmds[w % NCMD] = *c; __sync_synchronize(); AU_STORE(cmd_w, w + 1);
}
static void pan_gains(float pan, int *gl, int *gr) {
	if (pan < -1) pan = -1;
	if (pan > 1) pan = 1;
	*gl = (int)(256 * (pan > 0 ? 1 - 0.7f * pan : 1)); *gr = (int)(256 * (pan < 0 ? 1 + 0.7f * pan : 1));
}
static int post_rv = 0;   // b107: the reverb send of the event being played (ev_play_ex sets it around its posts; 0..256)
static void post_play(const Sound *s, int vol16, float pan, float pitch, int ev, int inst, int pri, int maxpb, int first) {
	Cmd c; memset(&c, 0, sizeof(c)); c.op = 1; c.s = s; c.vol = vol16 > 524288 ? 524288 : vol16 < 0 ? 0 : vol16; c.id = ++voice_seq; pan_gains(pan, &c.gl, &c.gr);
	c.step = (unsigned)((double)s->rate * pitch * 65536.0 / 48000.0); c.ev = ev; c.inst = inst; c.pri = pri; c.maxpb = maxpb; c.first = first; c.rv = post_rv;
	post(&c);
}
// vol: 0..256
// pan: -1 left .. +1 right (equal-power-ish)
static int play_pan(const char *prefix, int vol, float pan) {
	const Sound *s = find_sound(prefix); if (!s) return -1;
	post_play(s, vol * 256, pan, 1.0f, -1, 0, 128, 0, 0); return voice_seq;
}
static int play(const char *prefix, int vol) { return play_pan(prefix, vol, 0); }
// fade out every playing voice whose sound name starts with prefix, except voice keep_id
static void fade_others(const char *prefix, int keep_id, int ms) {
	Cmd c; memset(&c, 0, sizeof(c)); c.op = 2; c.id = keep_id; c.ms = ms; strncpy(c.prefix, prefix, 47); post(&c);
}
// music: a .gstr file; crossfades from whatever music is playing. vol 0..256, loop 0/1
static int music_play(const char *path, int loop, int vol, int fade_ms) {
	Cmd c; memset(&c, 0, sizeof(c)); c.op = 3; c.cls = 0; snprintf(c.path, sizeof(c.path), "%.111s", path); c.hdr = 1; c.loop = loop; c.vol = vol * 256; c.ms = fade_ms; c.id = ++voice_seq; c.cat = CAT_MUSIC; post(&c); return c.id;
}
static int music_cur_ev = -1;   // b59: the music event playing (music_ev_v), -1 none / other
static void music_stop(int fade_ms) { Cmd c; memset(&c, 0, sizeof(c)); c.op = 4; c.cls = 0; c.ms = fade_ms; post(&c); music_cur_ev = -1; }
static __attribute__((unused)) int music_playing(void) { return AU_LOAD(streams[0].state) != ST_IDLE || AU_LOAD(streams[1].state) != ST_IDLE || pend_on[0]; }
// looped ambience bed in slot 0 or 1
static int amb_play(int slot, const char *path, int vol) {
	Cmd c; memset(&c, 0, sizeof(c)); c.op = 3; c.cls = 1 + (slot & 1); snprintf(c.path, sizeof(c.path), "%.111s", path); c.hdr = 1; c.loop = 1; c.vol = vol * 256; c.ms = 1500; c.id = ++voice_seq; c.cat = CAT_AMB; post(&c); return c.id;
}
static void amb_stop(int slot, int fade_ms) { Cmd c; memset(&c, 0, sizeof(c)); c.op = 4; c.cls = 1 + (slot & 1); c.ms = fade_ms; post(&c); }
static void stream_set(int id, int vol16, float pan, int ms) {   // volume (0..65536) and pan of a playing stream
	Cmd c; memset(&c, 0, sizeof(c)); c.op = 5; c.id = id; c.vol = vol16; c.ms = ms; pan_gains(pan, &c.gl, &c.gr); post(&c);
}
static void stream_stop(int id, int ms) { Cmd c; memset(&c, 0, sizeof(c)); c.op = 6; c.id = id; c.ms = ms; post(&c); }
static float vline_secs(const VEntry *e) { return e->rate ? (float)e->frames / e->rate : 0; }
static int voice_play_entry(const VEntry *e, int vol16, float pan, int pri) {   // pri: 0 announcer / radio (kept), higher = less important
	Cmd c; memset(&c, 0, sizeof(c)); c.op = 3; c.cls = 3; c.pri = pri; strncpy(c.path, vpack_path[e->flags & 7], 111); c.base = e->off; c.hdr = 0;
	c.rate = e->rate; c.ch = e->ch; c.frames = e->frames; c.nblk = e->nblk; c.loop = 0; c.vol = vol16; c.ms = 0; pan_gains(pan, &c.gl, &c.gr); c.id = ++voice_seq;
	c.cat = !strncmp(e->name, "ui_", 3) || !strncmp(e->name, "is_", 3) ? CAT_SFX : CAT_VOICE;   // interface sounds filed with the voice lines are effects
	c.rv = post_rv;
	post(&c); return c.id;
}
// a voice line from the voice packs (streamed); -1 if the prefix matches nothing or no pack is loaded
static void logp(const char *fmt, ...);   // main.c
static int voice_play(const char *prefix, int vol, float pan) {
	if (!nvline) return -1;
	// b49: the line of exactly that name when there is one (a prefix also matched longer names: "sk_tut_operator_skillsync"
	// picked the _intro or the _start line at random), else one of the lines it begins (the _001 / _002 variants)
	const VEntry *e = NULL; { int lo = 0, hi = nvline - 1; while (lo <= hi) { int m = (lo + hi) >> 1, c = strcmp(vlines[m].name, prefix); if (!c) { if (!(vlines[m].flags & VL_PCONLY)) e = &vlines[m]; break; } if (c < 0) lo = m + 1; else hi = m - 1; } }
	if (!e) e = find_vline(prefix);
	if (!e) { if (vo_pc_only(prefix)) logp("voice: %s not played (it names a PC key)\n", prefix); return -1; }
	return voice_play_entry(e, vol * 256, pan, 50);
}
static void voice_stop_all(int fade_ms) { Cmd c; memset(&c, 0, sizeof(c)); c.op = 4; c.cls = 3; c.ms = fade_ms; post(&c); }
static __attribute__((unused)) int voice_busy(void) { for (int i = 4; i < 10; i++) if (AU_LOAD(streams[i].state) != ST_IDLE) return 1; return 0; }

// ---------------------------------------------------------------- FMOD event table
// databuilder: the table is data now (events.gev from tools/audio/make_events.py, loaded by ev_load in main.c): ev_table.h
#include "ev_table.h"

// ---------------------------------------------------------------- event playback
static float snd_lis[3], snd_lis_yaw;   // listener (camera) position and yaw (right = (sin yaw, -cos yaw), as bots.h)
static float snd_indoor = 0;            // smoothed 0 outdoor .. 1 indoor (beds crossfade)
static int snd_indoor_now = 0;          // current probe result (weapon variants)
// b107: the shooting range plays the OUTDOOR fire samples on the PC (the showcase video's sniper shots match the
// *_fp_outdoor samples at 0.98-0.99, the indoor ones at 0.92: tools/audio/ana/reverb_fit.py), though the room has a roof
static int snd_force_outdoor = 0;
static int snd_aim = 0;                 // aiming down sights (first-person fire layers)
static int ev_inst_seq = 0;
static char amb_timed_mute[32];       // b87: timed ambience events containing this are left to wobj.h (the train it draws)
static float ev_last_len = 0;           // length in seconds of the longest streamed layer started by the last ev_play
static void audio_listener(const float *pos, float yaw) { snd_lis[0] = pos[0]; snd_lis[1] = pos[1]; snd_lis[2] = pos[2]; snd_lis_yaw = yaw; }
static int ev_cmp(const void *k, const void *e) { return strcmp((const char *)k, ((const EvDef *)e)->name); }
static int ev_find(const char *name) {
	const EvDef *e = bsearch(name, EV_DEF, EV_COUNT, sizeof(EvDef), ev_cmp); return e ? (int)(e - EV_DEF) : -1;
}
static float ev_curve(int c, float x) {
	if (x <= 0) return EV_CURVE[c][0] / 255.f;
	if (x >= 1) return EV_CURVE[c][16] / 255.f;
	float f = x * 16; int i = (int)f; f -= i;
	return (EV_CURVE[c][i] + (EV_CURVE[c][i + 1] - EV_CURVE[c][i]) * f) / 255.f;
}
static float frand01(void) { return (rand() & 0xffff) / 65535.f; }
// one of "a|b|c" (or the only name) -> out
static void snd_token(const char *spec, char *out, int cap) {
	int n = 1; for (const char *p = spec; *p; p++) n += *p == '|';
	int k = n > 1 ? rand() % n : 0; const char *p = spec;
	while (k > 0) { if (*p++ == '|') k--; }
	int i = 0; while (*p && *p != '|' && i < cap - 1) out[i++] = *p++;
	out[i] = 0;
}
static const Sound *snd_pick(const char *spec) {
	int n = strlen(spec);
	if (n && spec[n - 1] == '*') { char p[64]; if (n > 63) n = 63; memcpy(p, spec, n - 1); p[n - 1] = 0; return find_sound(p); }
	char t[64]; snd_token(spec, t, sizeof(t)); return find_sound_exact(t);
}
static const VEntry *vline_pick(const char *spec) {
	if (!nvline) return NULL;
	int n = strlen(spec); char t[64];
	if (n && spec[n - 1] == '*') { if (n > 63) n = 63; memcpy(t, spec, n - 1); t[n - 1] = 0; return find_vline(t); }
	snd_token(spec, t, sizeof(t));
	int lo = 0, hi = nvline - 1;
	while (lo <= hi) { int m = (lo + hi) >> 1, c = strcmp(vlines[m].name, t); if (!c) return vlines[m].flags & VL_PCONLY ? NULL : &vlines[m]; if (c < 0) lo = m + 1; else hi = m - 1; }
	return NULL;
}
static int stream_file_c(int cls, int cat, const char *name, int loop, int vol16, float pan, int fade_ms) {
	Cmd c; memset(&c, 0, sizeof(c)); c.op = 3; c.cls = cls; snprintf(c.path, sizeof(c.path), "%.56s%.48s.gstr", snd_dir, name); c.hdr = 1; c.loop = loop;
	c.vol = vol16; c.ms = fade_ms; pan_gains(pan, &c.gl, &c.gr); c.id = ++voice_seq; c.cat = cat; post(&c); return c.id;
}
static int stream_file(int cls, const char *name, int loop, int vol16, float pan, int fade_ms) { return stream_file_c(cls, CAT_AMB, name, loop, vol16, pan, fade_ms); }
static int ev_music_fade = 300;
// plays event e. pos: world position (NULL = at the listener / 2D). indoor: 0/1 (the source's surroundings, < 0 = the
// listener's), aim: 1 when aiming down sights (first-person fire), prm: value of the event's own parameter (0..1),
// vol: extra volume factor. Returns the instance id or -1.
static int ev_play_ex(int e, const float *pos, int indoor, int aim, float prm, float vol) {
	if (e < 0 || e >= EV_COUNT) return -1;
	const EvDef *E = &EV_DEF[e];
	if (indoor < 0) indoor = snd_indoor_now;
	float d = 0, pan = 0, dg = 1; int is3d = (E->flags & 1) && pos;
	if (is3d) {
		float dx = pos[0] - snd_lis[0], dy = pos[1] - snd_lis[1], dz = pos[2] - snd_lis[2];
		d = sqrtf(dx * dx + dy * dy + dz * dz);
		if (d > 1) pan = (dx * sinf(snd_lis_yaw) - dy * cosf(snd_lis_yaw)) / d;
		if (E->flags & 4) dg = d <= E->mind ? 1 : E->mind / fminf(d, E->maxd);
		else if (E->flags & 8) dg = E->maxd > E->mind ? fminf(1, fmaxf(0, 1 - (d - E->mind) / (E->maxd - E->mind))) : 1;
	}
	float xd = E->dmax > 0 ? fminf(1, d / E->dmax) : 0;
	int k = (indoor ? 2 : 0) + (aim ? 1 : 0), inst = ++ev_inst_seq, first = 1, n = 0;
	float base = vol * E->vol * dg * (E->vrand > 0 ? 1 - frand01() * E->vrand : 1);
	float pitch = (E->pitch != 0 || E->prand != 0) ? powf(2.f, E->pitch + (frand01() * 2 - 1) * E->prand) : 1.f;
	ev_last_len = 0;
	post_rv = EV_WET && EV_WET[e] < 255 ? (int)(256.f * powf(10.f, -EV_WET[e] / 80.f) + 0.5f) : 0;   // b107: the event's reverb send (reverb.h; its wet level in quarter dB)
	for (int li = 0; li < E->nl; li++) {
		const EvLayer *L = &EV_LAYER[E->l0 + li];
		if (L->sp) {
			float x = L->sp == 1 ? xd : L->sp == 2 ? (indoor ? 0.75f : 0.2f) : L->sp == 3 ? (aim ? 1.f : 0.f) : prm;
			if (x < L->s0 - 1e-4f || x > L->s1 + 1e-4f) continue;
		}
		if (L->pnull && (rand() & 255) < L->pnull) continue;
		float g = base * L->g[k];
		if (L->vc) g *= ev_curve(L->vc, xd);
		if (g < 0.002f) continue;
		float pl = L->pc ? ev_curve(L->pc - 1, xd) : L->pk >= 0 ? L->pk : 1.f, lp = is3d ? pan * pl : 0;
		int kind = L->kind & 15, loop = L->kind >> 4 & 1, v16 = (int)(g * 65536);
		if (kind == 0) {
			const Sound *s = snd_pick(L->snd); if (!s) continue;
			post_play(s, v16, lp, pitch, e, inst, E->pri, E->maxpb, first); first = 0;
		} else if (kind == 1) {
			const VEntry *v = vline_pick(L->snd); if (!v) continue;
			voice_play_entry(v, v16, lp, strncmp(E->name, "voice/donut", 11) ? 50 + E->pri : 0); if (vline_secs(v) > ev_last_len) ev_last_len = vline_secs(v);
		} else {
			char t[64]; snd_token(L->snd, t, sizeof(t));
			if (kind == 2) { char p[112]; snprintf(p, sizeof(p), "%.56s%.48s.gstr", snd_dir, t); music_play(p, loop, (int)(g * 256), ev_music_fade); }
			else stream_file_c(4, strncmp(E->name, "ui/", 3) ? CAT_AMB : CAT_SFX, t, 0, v16, lp, 0);   // ambience one-shot (looped beds / emitters run from snd_update)
		}
		n++;
	}
	post_rv = 0;
	return n ? inst : -1;
}
static int ev_play(const char *name, const float *pos) { return ev_play_ex(ev_find(name), pos, -1, snd_aim, 0.f, 1.f); }
// music cue by event name ("bgm/dead", "bgm/in_game_tdm_01_ready" ...), crossfading over fade_ms; vol scales the design level
static int music_ev_v(const char *name, int fade_ms, float vol) { ev_music_fade = fade_ms; int e = ev_find(name), r = ev_play_ex(e, NULL, 0, 0, 0.f, vol); ev_music_fade = 300; music_cur_ev = r >= 0 ? e : -1; return r; }
static int music_ev(const char *name, int fade_ms) { return music_ev_v(name, fade_ms, 1.f); }
// b59 (user: the menus' sounds and music, mixed so neither drowns the other): the front end's music and interface sounds
// at the menu mix. The FMOD design levels assume a PC's speakers and a mouse: the hover tick peaks at -43 dBFS here, the
// click at -31, under lobby music whose level is -34 dBFS (short-term RMS, median); on the Vita's speakers the music was
// faint and the sounds inaudible. Measured with tools/audio/host/scene.c (one / menu modes): the menu music sits at about
// -24 dBFS and each sound gets its own factor to a peak level by its role: hover -17, click / tab -12..-15, windows -13,
// confirms -10..-11, the match cues -8..-11 (the factors: target / measured peak of the event alone at design level).
static float ui_sfx_gain(const char *n) {
	static const struct { const char *n; float g; } T[] = {
		{ "ui/mouse_over_basic", 19.f }, { "ui/click_basic", 8.6f }, { "ui/tab_on", 10.f }, { "ui/tab_off", 11.f },
		{ "ui/window_open_basic", 3.7f }, { "ui/window_close_basic", 3.5f }, { "ui/character_select", 1.9f }, { "ui/character_cancel", 2.1f },
		{ "ui/character_select_mouse_over", 10.f }, { "ui/weapon_change_on", 19.f }, { "ui/click_game_start", 2.f }, { "ui/match_success", 1.25f },
		{ "ui/ready_count", 0.82f }, { "ui/ready_game_start", 0.9f }, { "ui/ready_game_start_end", 1.f }, { "ui/lobby_on", 7.f },
		{ "ui/logo_loading", 4.3f }, { "ui/whoosh", 1.9f }, { "ui/mission_complete", 1.f }, { "ui/mission_progress", 1.f },
		{ "ui/result_gp_number_up", 10.f }, { "ui/result_exp_number_up", 10.f }, { "ui/result_gp_number_up_end", 3.1f }, { "ui/result_exp_number_up_end", 3.1f },
		{ "ui/result_unlock", 1.6f }, { "ui/result_mvp_team", 0.8f }, { "ui/result_mvp_player", 0.8f }, { "ui/result_mvp", 1.2f },
		{ "ui/result_victory_defeat", 0.9f }, { "ui/result_level_up", 0.9f }, { "ui/result_player_list", 1.f }, { "ui/lobby_autobalance", 0.6f },
		{ "ui/room_pop_up_on", 6.4f }, { "ui/autobalance_pop_up_on", 0.8f }, { "ui/logo_company", 1.4f }, { "ui/logo_team", 1.4f }, { "ui/logo_dive", 1.f } };
	for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) if (!strcmp(T[i].n, n)) return T[i].g;
	return 1.5f;
}
static int ui_sfx(const char *n) { return ev_play_ex(ev_find(n), NULL, 0, 0, 0.f, ui_sfx_gain(n)); }
static float menu_music_gain(const char *n) {   // the menu cues to about -24 dBFS (the tutorial's own music stays under its voice)
	static const struct { const char *n; float g; } T[] = { { "bgm/lobby", 3.35f }, { "bgm/logo", 0.9f }, { "bgm/ready_room", 2.f }, { "bgm/result", 1.35f },
		{ "bgm/intro", 3.2f }, { "bgm/tutorial_matrix", 2.f } };
	for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) if (!strcmp(T[i].n, n)) return T[i].g;
	return 1.f;
}
static int menu_music(const char *n, int fade_ms) { return music_ev_v(n, fade_ms, menu_music_gain(n)); }

// events scheduled for later (shell casings landing, radio voice lines) and legacy delayed sounds
typedef struct { float t; char prefix[48]; int vol; int ev; float pos[3]; int haspos, radio; } Delayed;
static Delayed delayed[24]; static int ndelayed;
static __attribute__((unused)) void play_later(const char *prefix, int vol, float delay) {
	if (ndelayed >= 24) return;
	Delayed *d = &delayed[ndelayed++]; memset(d, 0, sizeof(*d)); d->t = delay; strncpy(d->prefix, prefix, 47); d->vol = vol; d->ev = -1;
}
static void ev_play_later(const char *name, const float *pos, float delay) {
	int e = ev_find(name); if (e < 0 || ndelayed >= 24) return;
	Delayed *d = &delayed[ndelayed++]; memset(d, 0, sizeof(*d)); d->t = delay; d->ev = e;
	if (pos) { memcpy(d->pos, pos, 12); d->haspos = 1; }
}
// a line over the radio ("doughnut" HUD): click on, the line 0.14 s later, click off when it ends (as the original)
static void radio_play_at(const char *name, float delay) {
	if (delay > 0) ev_play_later("ui/doughnut_on", NULL, delay); else ev_play("ui/doughnut_on", NULL);
	int e = ev_find(name); if (e < 0 || ndelayed >= 24) return;
	Delayed *d = &delayed[ndelayed++]; memset(d, 0, sizeof(*d)); d->t = delay + 0.14f; d->ev = e; d->radio = 1;
}
static void radio_play(const char *name) { radio_play_at(name, 0); }
static void update_delayed(float dt) {
	for (int i = 0; i < ndelayed; ) {
		delayed[i].t -= dt;
		if (delayed[i].t <= 0) {
			Delayed d = delayed[i]; delayed[i] = delayed[--ndelayed];
			if (d.ev < 0) play(d.prefix, d.vol);
			else {
				ev_play_ex(d.ev, d.haspos ? d.pos : NULL, -1, 0, 0.f, 1.f);
				if (d.radio && ev_last_len > 0) ev_play_later("ui/doughnut_off", NULL, ev_last_len + 0.1f);
			}
		} else i++;
	}
}

// ---------------------------------------------------------------- helpers for the game's common sounds
// material id (player.h: 0 player mesh, 1 wood, 2 metal, 3 sand, 4 glass, 5 car, 6 paperbox, 7 soft, 8 concrete) -> event names
static const char *SND_MAT_IMPACT[9] = { "stone", "wood", "metal", "dirt", "glass", "metal_car", "soft", "soft", "stone" };
static const char *SND_MAT_FOOT[9]   = { "stone", "wood", "metal", "dirt", "stone", "metal_car", "paper", "soft", "stone" };
static const char *SND_MAT_FOOT3[9]  = { "stone", "wood", "metal", "dirt", "glass", "metal_car", "paper", "paper", "stone" };
static const char *SND_MAT_SHELL[9]  = { "stone", "wood", "metal", "dirt", "stone", "metal_car", "soft", "soft", "stone" };
// footsteps & co. who: 0 the player (first person), 1 a male character, 2 a female character (third person)
// act: "foot_walk", "foot_run", "foot_fastrun", "jump", "land", "run_jump", "run_land", "fastrun_jump", "fastrun_land"
static int snd_foley(int who, const char *act, int mat, const float *pos) {
	char n[64]; if (mat < 0 || mat > 8) mat = 0;
	snprintf(n, sizeof(n), "foley/%s/%s_%s", who == 0 ? "female_1st" : who == 2 ? "female" : "enemy", act, who == 0 ? SND_MAT_FOOT[mat] : SND_MAT_FOOT3[mat]);
	int e = ev_find(n);
	if (e < 0) { snprintf(n, sizeof(n), "foley/%s/%s_stone", who == 0 ? "female_1st" : who == 2 ? "female" : "enemy", act); e = ev_find(n); }
	return ev_play_ex(e, who ? pos : NULL, -1, 0, 0.f, 1.f);
}
// bullet impact: cls "ar" "smg" "sr" "sg" "hg" "mg" "knife"; mat as above (9 = body)
static int snd_impact(const char *cls, int mat, const float *pos) {
	char n[48]; snprintf(n, sizeof(n), "impact/%s/%s", cls, mat == 9 ? "body" : SND_MAT_IMPACT[mat < 0 || mat > 8 ? 0 : mat]);
	return ev_play(n, pos);
}
// a shell casing landing: cls "ar" "smg" "sr" "sr_bolt" "sg" "hg" "mg"
static void snd_shell(const char *cls, int mat, const float *pos, float delay) {
	char n[48]; snprintf(n, sizeof(n), "weapon/shelldrop/%s_%s", cls, SND_MAT_SHELL[mat < 0 || mat > 8 ? 0 : mat]);
	ev_play_later(n, pos, delay);
}
// weapon event: base "weapon/ar/m4a1", what "fire_fp" "fire_tp" "reload_01" ...; pos NULL = the player's own weapon
static int snd_weapon(const char *base, const char *what, const float *pos, int indoor) {
	char n[80]; snprintf(n, sizeof(n), "%s/%s", base, what); return ev_play_ex(ev_find(n), pos, indoor, pos ? 0 : snd_aim, 0.f, 1.f);
}

static int snd_indoor_at(const float *p);
// the weapons' FMOD event groups (weapons.txt short names), shell-casing and bullet-impact classes
typedef struct { const char *sh, *base, *shell, *impact; } SndWeapon;
static const SndWeapon SND_WEAPON[] = {
	{ "m4a1", "weapon/ar/m4a1", "ar", "ar" }, { "m4a1rd", "weapon/ar/m4a1", "ar", "ar" }, { "c30", "weapon/ar/seburo_c-30", "ar", "ar" }, { "scar", "weapon/ar/scar", "ar", "ar" },
	{ "acr", "weapon/ar/acr", "ar", "ar" },
	// b89: the rifles added from the user's AR video, each with its own event group (fire, reloads, equip, dry fire)
	{ "ak47", "weapon/ar/ak47", "ar", "ar" }, { "k2", "weapon/ar/k2", "ar", "ar" }, { "arx160", "weapon/ar/arx-160", "ar", "ar" }, { "hk417", "weapon/ar/hk417", "ar", "ar" },
	{ "f2000", "weapon/ar/f2000", "ar", "ar" }, { "famas", "weapon/ar/famas", "ar", "ar" }, { "x95r", "weapon/ar/x95-r", "ar", "ar" },
	{ "mp5", "weapon/smg/mp5", "smg", "smg" },
	// b90: the SMGs (+ the PP-2200 PDW), each with its own event group
	{ "c26a", "weapon/smg/seburo_c-26a", "smg", "smg" }, { "evo", "weapon/smg/scorpion_evo", "smg", "smg" }, { "kacpdw", "weapon/smg/kac-pdw", "smg", "smg" },
	{ "mp5k", "weapon/smg/mp5k", "smg", "smg" }, { "mpx", "weapon/smg/sig_mpx", "smg", "smg" }, { "p90", "weapon/smg/p90", "smg", "smg" }, { "pp2000", "weapon/smg/pp2000", "smg", "smg" }, { "kriss", "weapon/smg/kriss_super_v", "smg", "smg" },
	{ "ump45", "weapon/smg/ump45", "smg", "smg" }, { "sigmpx", "weapon/smg/sig_mpx", "smg", "smg" }, { "l118", "weapon/sr/l118a", "sr_bolt", "sr" },
	// b91: the sniper rifles, each with its own event group (the semi-autos drop the bolt-action casing class)
	{ "cm2010", "weapon/sr/xm2010", "sr_bolt", "sr" }, { "mm200", "weapon/sr/m200", "sr_bolt", "sr" }, { "m99d", "weapon/sr/m82", "sr_bolt", "sr" },
	{ "sa50", "weapon/sr/as50", "sr", "sr" }, { "seburosr", "weapon/sr/seburo_sr", "sr_bolt", "sr" }, { "seo90", "weapon/sr/msg90", "sr", "sr" },
	{ "srv97", "weapon/sr/sv98", "sr_bolt", "sr" }, { "ws2088", "weapon/sr/wa2000", "sr", "sr" },
	// b92: the pistols and light machine guns (each with its own event group; the 2008M's group plays the Mateba's shots)
	{ "mag44", "weapon/hg/44magnum", NULL, "hg" }, { "m2008", "weapon/hg/mateba_2008m", NULL, "hg" }, { "b93se", "weapon/hg/93r", "hg", "hg" },
	{ "kcz85", "weapon/hg/bul_storm", "hg", "hg" }, { "sn57", "weapon/hg/fn_5-7", "hg", "hg" }, { "p226", "weapon/hg/sig-p226", "hg", "hg" },
	{ "l86", "weapon/mg/l86_lsw", "mg", "mg" }, { "mg33", "weapon/mg/mg3", "mg", "mg" }, { "seburomg", "weapon/mg/seburo_mg", "mg", "mg" },
	// b97: the shotguns, each with its own event group
	{ "ksg", "weapon/sg/ksg", "sg", "sg" }, { "sta12", "weapon/sg/uts15", "sg", "sg" }, { "uss12", "weapon/sg/usas-12", "sg", "sg" },
	{ "870", "weapon/sg/870mcs", "sg", "sg" }, { "m249", "weapon/mg/m249", "mg", "mg" }, { "mg36", "weapon/mg/mg36", "mg", "mg" },
	{ "seburom5", "weapon/hg/m-5", "hg", "hg" }, { "p99", "weapon/hg/p99", "hg", "hg" }, { "mateba", "weapon/hg/mateba", NULL, "hg" },
	{ "knife", "weapon/knife", NULL, "knife" }, { "axe", "weapon/doubleblade", NULL, "knife" }, { "m67", "weapon/grenade/common", NULL, NULL },   // (b98: the DOUBLE KATAR, the game's "doubleblade" events)
};
static const SndWeapon *snd_wpn(const char *sh) {
	for (unsigned i = 0; i < sizeof(SND_WEAPON) / sizeof(SND_WEAPON[0]); i++) if (!strcmp(SND_WEAPON[i].sh, sh)) return &SND_WEAPON[i];
	return &SND_WEAPON[0];
}
// the player's shot: the first-person event (outdoor / indoor variant by the indoor probe, quieter aimed) and its tail
static void snd_wfire(const char *sh) {
	const SndWeapon *W = snd_wpn(sh);
	if (!strcmp(sh, "knife") || !strcmp(sh, "axe")) { char n[64]; snprintf(n, sizeof(n), "%s/swing", W->base); ev_play(n, NULL); return; }
	snd_weapon(W->base, "fire_fp", NULL, -1); snd_weapon(W->base, "fire_fp_sustain", NULL, -1);
}
// someone else's shot at pos (third person: near / distant layers by distance, indoor variant from the shooter's place)
static void snd_wfire_tp(const char *sh, const float *pos) {
	const SndWeapon *W = snd_wpn(sh);
	if (!strcmp(sh, "knife") || !strcmp(sh, "axe")) { char n[64]; snprintf(n, sizeof(n), "%s/swing_tp", W->base); ev_play(n, pos); return; }
	snd_weapon(W->base, "fire_tp", pos, snd_indoor_at(pos));
}
static void snd_wev(const char *sh, const char *what, float delay) {   // equip_on, equip_off, dryfire, aim_on, aim_off, shot_reload, reload_0N ...
	char n[80]; snprintf(n, sizeof(n), "%s/%s", snd_wpn(sh)->base, what);
	if (delay > 0) ev_play_later(n, NULL, delay); else ev_play(n, NULL);
}
// a magazine reload over clip seconds: the weapon's reload_01.. events spread like the clip (the magazine drop early)
static void snd_wreload(const char *sh, float clip) {
	const char *base = snd_wpn(sh)->base; int ev[10], n = 0, drop = -1; char nm[80];
	for (int k = 1; k <= 9; k++) {
		snprintf(nm, sizeof(nm), "%s/reload_%02d", base, k); int e = ev_find(nm); if (e < 0) continue;
		const EvDef *E = &EV_DEF[e]; int isdrop = E->nl && strstr(EV_LAYER[E->l0].snd, "magazine_drop");
		if (isdrop) drop = e; else if (n < 10) ev[n++] = e;
	}
	for (int k = 0; k < n; k++) { float t = clip * (n > 1 ? 0.16f + 0.62f * k / (n - 1) : 0.16f); ev_play_later(EV_DEF[ev[k]].name, NULL, t); }
	if (drop >= 0) ev_play_later(EV_DEF[drop].name, NULL, clip * 0.3f);
}
static void snd_wshell(const char *sh, int mat, float delay) {   // the casing lands next to the player
	const SndWeapon *W = snd_wpn(sh); if (!W->shell) return;
	float p[3] = { snd_lis[0] + 30 * sinf(snd_lis_yaw), snd_lis[1] - 30 * cosf(snd_lis_yaw), snd_lis[2] - 150 };
	snd_shell(W->shell, mat, p, delay);
}
static void snd_whit(const char *sh, int mat, const float *pos) { const SndWeapon *W = snd_wpn(sh); snd_impact(W->impact ? W->impact : "ar", mat, pos); }
// grenade: bounce strength 0..1 (1 = the first, hard bounce), explosion (indoor variant from the blast's place)
static void snd_gren_bounce(int mat, float strength, const float *pos) {
	char n[48]; snprintf(n, sizeof(n), "weapon/grenade/m67/bounce/%s", SND_MAT_SHELL[mat < 0 || mat > 8 ? 0 : mat]);
	float prm = strength > 0.66f ? 0.08f : strength > 0.33f ? 0.21f : 0.42f;
	ev_play_ex(ev_find(n), pos, -1, 0, prm, 1.f);
}
static void snd_gren_exp(const float *pos) { ev_play_ex(ev_find("weapon/grenade/m67/exp"), pos, snd_indoor_at(pos), 0, 0.f, 1.f); }
// character voices: chr = motoko, bato, ... (kuro has its own set); what = damage_1st, death_1st, death_large_1st,
// grenadethrow_1st, knife, jump, land, damage_3rd, death_3rd, reload_3rd, enemykill_3rd, enemydetect_3rd, teamkilled_3rd ...
static int snd_voice(const char *chr, const char *what, const float *pos) {
	char n[80]; snprintf(n, sizeof(n), "voice/action/%s/%s_%s", chr, chr, what); return ev_play(n, pos);
}
static int snd_killvoice(const char *chr, const char *what) {   // doublekill, revenge, bombkill, killtachikoma
	char n[80]; snprintf(n, sizeof(n), "voice/action/%s/k_%s_%s", chr, chr, what); return ev_play(n, NULL);
}
static int snd_chr_female(const char *chr) { return !strcmp(chr, "motoko") || !strcmp(chr, "reiko") || !strcmp(chr, "sitara") || !strcmp(chr, "kuro"); }

// ---------------------------------------------------------------- indoor probe & map ambience
#ifdef P_HEIGHT   // player.h's collision raycast is available
// 1 when the place has a roof: rays up and up-diagonally hitting something within 25 m
static int snd_probe_indoor(const float *p, int full) {
	static const float D[5][3] = { { 0, 0, 1 }, { 0.7f, 0, 0.7f }, { -0.7f, 0, 0.7f }, { 0, 0.7f, 0.7f }, { 0, -0.7f, 0.7f } };
	int hits = 0, n = full ? 5 : 1;
	for (int i = 0; i < n; i++) if (raycast(v3(p[0], p[1], p[2] + 20), v3(D[i][0], D[i][1], D[i][2]), 2500, NULL, NULL) >= 0) hits++;
	return full ? hits >= 3 : hits;
}
static int snd_indoor_at(const float *p) { return snd_probe_indoor(p, 0); }
#else
static int snd_probe_indoor(const float *p, int full) { return 0; }
static int snd_indoor_at(const float *p) { return snd_indoor_now; }
#endif
static int amb_cur = -1, amb_bed_id[2] = { -1, -1 }, amb_bed_ev[2] = { -1, -1 };
static float amb_bed_g[2], amb_bed_sent[2] = { -1, -1 }, amb_t = 0, amb_tick = 0, snd_probe_t = 0, amb_next[8];
#define NEMIT 48
static struct { int ev, id; float g; } amb_em[NEMIT]; static int namb_em;
static int amb_ev_file(int e, char *out, int cap, float *gain, float *pk) {   // first ambience layer of event e
	if (e < 0) return 0;
	const EvDef *E = &EV_DEF[e];
	for (int li = 0; li < E->nl; li++) { const EvLayer *L = &EV_LAYER[E->l0 + li]; if ((L->kind & 15) != 3) continue;
		snd_token(L->snd, out, cap); *gain = E->vol * L->g[0]; *pk = (E->flags & 1) ? (L->pk >= 0 ? L->pk : 1) : 0; return 1; }
	return 0;
}
// starts the map's ambience (stem as MAPS[].stem); call after audio_load_all
static void amb_map_start(const char *stem) {
	for (int i = 0; i < namb_em; i++) if (amb_em[i].id >= 0) stream_stop(amb_em[i].id, 500);
	namb_em = 0; amb_cur = -1; amb_t = 0; snd_force_outdoor = !strcmp(stem, "shooting");
	for (unsigned i = 0; i < (unsigned)amb_ntimed /* databuilder */ && i < 8; i++) amb_next[i] = AMB_TIMED[i].at;
	for (int s = 0; s < 2; s++) { amb_bed_id[s] = -1; amb_bed_ev[s] = -1; amb_bed_sent[s] = -1; }
	for (unsigned i = 0; i < (unsigned)amb_nmap /* databuilder */; i++) if (!strcmp(AMB_MAP[i].map, stem)) { amb_cur = i; break; }
	if (amb_cur < 0) { amb_stop(0, 800); amb_stop(1, 800); reverb_set(NULL, NULL); return; }
	{ int ro = AMB_REV ? AMB_REV[amb_cur][0] : 255, ri = AMB_REV ? AMB_REV[amb_cur][1] : 255;   // b107: the map's reverb definitions (outdoor / indoor zone)
	  reverb_set(ro < rev_ndef ? &REV_DEF[ro] : NULL, ri < rev_ndef ? &REV_DEF[ri] : NULL); }
	amb_bed_ev[0] = ev_find(AMB_MAP[amb_cur].out); amb_bed_ev[1] = ev_find(AMB_MAP[amb_cur].in);
	if (amb_bed_ev[1] == amb_bed_ev[0]) amb_bed_ev[1] = -1;
	for (int s = 0; s < 2; s++) {
		char f[64], p[112]; float g, pk;
		if (!amb_ev_file(amb_bed_ev[s], f, sizeof(f), &g, &pk)) { amb_stop(s, 800); continue; }
		amb_bed_g[s] = g; float w = s == 0 ? 1 - snd_indoor : snd_indoor; if (amb_bed_ev[1] < 0) w = 1;
		snprintf(p, sizeof(p), "%.56s%.48s.gstr", snd_dir, f);
		amb_bed_id[s] = amb_play(s, p, (int)(g * w * 256)); amb_bed_sent[s] = g * w;
	}
	for (unsigned i = 0; i < (unsigned)amb_nemit /* databuilder */ && namb_em < NEMIT; i++) if (!strcmp(AMB_EMIT[i].map, stem)) {
		amb_em[namb_em].ev = i; amb_em[namb_em].id = -1; amb_em[namb_em].g = 0; namb_em++; }
}
// per frame: indoor probe, bed crossfade, positional emitters, timed pass-bys, delayed events
static void snd_update(float dt) {
	update_delayed(dt);
	snd_probe_t -= dt;
	if (snd_probe_t <= 0) { snd_probe_t = 0.2f; snd_indoor_now = snd_force_outdoor ? 0 : snd_probe_indoor(snd_lis, 1); }
	float k = fminf(1, dt / 1.2f); snd_indoor += ((float)snd_indoor_now - snd_indoor) * k; rv_indoor = snd_indoor;
	if (amb_cur < 0) return;
	amb_t += dt; amb_tick -= dt;
	if (amb_tick > 0) return;
	amb_tick = 0.1f;
	if (amb_bed_ev[1] >= 0) for (int s = 0; s < 2; s++) {   // beds: outdoor <-> indoor
		if (amb_bed_id[s] < 0) continue;
		float w = s == 0 ? 1 - snd_indoor : snd_indoor, g = amb_bed_g[s] * (0.08f + 0.92f * w);   // the other bed stays faintly audible
		if (fabsf(g - amb_bed_sent[s]) > 0.01f * amb_bed_g[s]) { stream_set(amb_bed_id[s], (int)(g * 65536), 0, 150); amb_bed_sent[s] = g; }
	}
	// emitters: the 2 loudest audible ones stream (the other slots are kept for one-shots: train, helicopter ...)
	float rx = sinf(snd_lis_yaw), ry = -cosf(snd_lis_yaw);
	int order[NEMIT], no = 0;
	for (int i = 0; i < namb_em; i++) {
		const AmbEmit *A = &AMB_EMIT[amb_em[i].ev];
		float dx = A->x - snd_lis[0], dy = A->y - snd_lis[1], dz = A->z - snd_lis[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
		float rg = d <= A->rin ? 1 : d >= A->rout ? 0 : (A->rout - d) / (A->rout - A->rin);
		char f[64]; float g = 0, pk = 0; amb_ev_file(ev_find(A->ev), f, sizeof(f), &g, &pk);
		amb_em[i].g = g * rg;
		if (amb_em[i].g > 0.003f) order[no++] = i;
	}
	for (int a = 0; a < no; a++) for (int b = a + 1; b < no; b++) if (amb_em[order[b]].g > amb_em[order[a]].g) { int t = order[a]; order[a] = order[b]; order[b] = t; }
	if (no > 2) no = 2;
	for (int i = 0; i < namb_em; i++) {
		int on = 0; for (int j = 0; j < no; j++) on |= order[j] == i;
		if (!on && amb_em[i].id >= 0) { stream_stop(amb_em[i].id, 600); amb_em[i].id = -1; }
	}
	for (int j = 0; j < no; j++) {
		int i = order[j]; const AmbEmit *A = &AMB_EMIT[amb_em[i].ev];
		char f[64]; float g, pk; if (!amb_ev_file(ev_find(A->ev), f, sizeof(f), &g, &pk)) continue;
		float dx = A->x - snd_lis[0], dy = A->y - snd_lis[1], d = sqrtf(dx * dx + dy * dy) + 1, pan = (dx * rx + dy * ry) / d * pk;
		int v16 = (int)(amb_em[i].g * 65536);
		if (amb_em[i].id < 0) amb_em[i].id = stream_file(4, f, 1, v16, pan, 800);
		else stream_set(amb_em[i].id, v16, pan, 120);
	}
	// timed objects (train, helicopter): their path is not known, play them at an assumed distance to the side
	// (b87: not the ones a moving object of wobj.h plays itself, in time with what is seen: amb_timed_mute names them)
	for (unsigned i = 0; i < (unsigned)amb_ntimed /* databuilder */ && i < 8; i++) {
		const AmbTimed *T = &AMB_TIMED[i]; if (strcmp(T->map, AMB_MAP[amb_cur].map)) continue;
		if (amb_timed_mute[0] && strstr(T->ev, amb_timed_mute)) continue;
		if (amb_t >= amb_next[i]) {
			amb_next[i] += T->period;
			float p[3] = { snd_lis[0] + rx * T->dist, snd_lis[1] + ry * T->dist, snd_lis[2] };
			ev_play_ex(ev_find(T->ev), p, 0, 0, 0.f, 1.f);
		}
	}
}
// legacy entry point: starts the map's ambience (the volume argument is ignored: the original event volumes are used)
static __attribute__((unused)) int amb_for_map(const char *dir, const char *stem, int vol) {
	(void)vol; if (!snd_dir[0]) strncpy(snd_dir, dir, sizeof(snd_dir) - 1);
	amb_map_start(stem); return amb_cur >= 0 ? 1 : 0;
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
static int stream_pick(int cls, int pri) {
	if (cls == 0) { for (int i = 0; i < 2; i++) if (AU_LOAD(streams[i].state) == ST_IDLE) return i; return -1; }
	if (cls == 1 || cls == 2) return AU_LOAD(streams[cls + 1].state) == ST_IDLE ? cls + 1 : -1;
	if (cls == 4) { for (int i = 10; i < 14; i++) if (AU_LOAD(streams[i].state) == ST_IDLE) return i; return -1; }
	int best = -1;
	for (int i = 4; i < 10; i++) { Stream *s = &streams[i]; if (AU_LOAD(s->state) == ST_IDLE) return i; if (AU_LOAD(s->state) != ST_PLAY || s->stopping) continue;
		if (best < 0 || s->pri > streams[best].pri || (s->pri == streams[best].pri && s->id < streams[best].id)) best = i; }
	// make room (10 ms fade) unless everything playing matters more (the announcer is never cut by a bark)
	if (best >= 0 && streams[best].pri >= pri) { streams[best].tvol = 0; streams[best].vstep = streams[best].vol / 480 + 1; streams[best].stopping = 1; }
	return -1;
}
static void stream_fade_out(Stream *s, int ms) {
	if (AU_LOAD(s->state) == ST_OPEN) { s->tvol = 0; s->vstep = 1 << 20; s->stopping = 1; return; }
	if (AU_LOAD(s->state) != ST_PLAY) return;
	int n = ms * 48; s->tvol = 0; s->vstep = s->vol / (n > 0 ? n : 1) + 1; s->stopping = 1;
}
static int stream_start(const Cmd *c) {
	int si = stream_pick(c->cls, c->pri); if (si < 0) return 0;
	Stream *s = &streams[si]; memcpy(s->path, c->path, sizeof(s->path)); s->base = c->base; s->hdr = c->hdr;
	s->rate = c->rate; s->ch = c->ch; s->frames = c->frames; s->nblk = c->nblk; s->loop = c->loop; s->loop_blk = 0;
	s->id = c->id; s->gl = c->gl ? c->gl : 256; s->gr = c->gr ? c->gr : 256; s->stopping = 0; s->pri = c->pri; s->cat = c->cat & 3; s->rv = c->rv;
	int n = c->ms * 48; s->tvol = c->vol; s->vol = n > 0 ? 0 : c->vol; s->vstep = n > 0 ? c->vol / n + 1 : 0;
	s->wr = s->rd = 0; s->eof = 0; s->i = 0; s->frac = 0; s->dec_ok = 0;
	__sync_synchronize(); AU_STORE(s->state, ST_OPEN); return 1;
}
static void voice_start(const Cmd *c) {
	if (c->ev >= 0 && c->first && c->maxpb > 0) {   // the event's max playbacks: the oldest instances fade out (fade-out time >= 10 ms)
		int inst[NVOICE], n = 0;
		for (int i = 0; i < NVOICE; i++) if (voices[i].active && voices[i].ev == c->ev && !voices[i].fade) {
			int j = 0; while (j < n && inst[j] != voices[i].inst) j++;
			if (j == n) inst[n++] = voices[i].inst;
		}
		while (n >= c->maxpb) {
			int o = 0; for (int j = 1; j < n; j++) if (inst[j] < inst[o]) o = j;
			for (int i = 0; i < NVOICE; i++) if (voices[i].active && voices[i].ev == c->ev && voices[i].inst == inst[o]) voices[i].fade = voices[i].vol / 480 + 1;
			inst[o] = inst[--n];
		}
	}
	// free voice, else the least important (highest priority number; fading ones first), oldest first
	int best = -1;
	for (int i = 0; i < NVOICE; i++) {
		Voice *v = &voices[i]; if (!v->active) { best = i; break; }
		if (best < 0) { best = i; continue; }
		Voice *b = &voices[best];
		int sv = (v->fade ? 1000 : 0) + v->pri, sb = (b->fade ? 1000 : 0) + b->pri;
		if (sv > sb || (sv == sb && v->id < b->id)) best = i;
	}
	Voice *v = &voices[best];
	if (v->active && !v->fade && v->pri < c->pri) { audio_dropped++; return; }   // everything playing matters more
	v->s = c->s; v->pos = 0; v->vol = c->vol; v->fade = 0; v->id = c->id; v->gl = c->gl; v->gr = c->gr;
	v->frac = 0; v->step = c->step ? c->step : (unsigned)(((unsigned long long)c->s->rate << 16) / 48000); v->blk = -1;
	v->ev = c->ev; v->inst = c->inst; v->pri = c->pri; v->rv = c->rv; v->active = 1;
}
static void run_cmds(void) {
	for (int k = 0; k < 3; k++) if (pend_on[k] && stream_start(&pend[k])) pend_on[k] = 0;
	while (cmd_r != AU_LOAD(cmd_w)) {
		Cmd *c = &cmds[cmd_r % NCMD];
		if (c->op == 1) voice_start(c);
		else if (c->op == 2) {
			int pl = strlen(c->prefix);
			for (int i = 0; i < NVOICE; i++) if (voices[i].active && voices[i].id != c->id && !voices[i].fade && !strncmp(voices[i].s->name, c->prefix, pl)) {
				int n = c->ms * 48; voices[i].fade = voices[i].vol / (n > 0 ? n : 1) + 1; }
		} else if (c->op == 3) {
			if (c->cls == 0) for (int i = 0; i < 2; i++) stream_fade_out(&streams[i], c->ms > 0 ? c->ms : 300);
			if (c->cls == 1 || c->cls == 2) stream_fade_out(&streams[c->cls + 1], 700);
			if (!stream_start(c) && c->cls < 3) { pend[c->cls] = *c; pend_on[c->cls] = 1; }
		} else if (c->op == 4) {
			int a = c->cls == 0 ? 0 : c->cls == 3 ? 4 : c->cls == 4 ? 10 : c->cls + 1, b = c->cls == 0 ? 2 : c->cls == 3 ? 10 : c->cls == 4 ? 14 : c->cls + 2;
			for (int i = a; i < b; i++) stream_fade_out(&streams[i], c->ms);
			if (c->cls < 3) pend_on[c->cls] = 0;
		} else if (c->op == 5 || c->op == 6) {
			for (int i = 0; i < NSTREAM; i++) {
				Stream *s = &streams[i]; if (s->id != c->id || AU_LOAD(s->state) == ST_IDLE || s->stopping) continue;
				if (c->op == 6) { stream_fade_out(s, c->ms); continue; }
				int n = c->ms * 48, cur = AU_LOAD(s->state) == ST_PLAY ? s->vol : s->tvol;
				s->tvol = c->vol; s->vstep = abs(c->vol - cur) / (n > 0 ? n : 1) + 1; s->gl = c->gl; s->gr = c->gr;
			}
			for (int k = 0; k < 3; k++) if (pend_on[k] && pend[k].id == c->id) { if (c->op == 6) pend_on[k] = 0; else pend[k].vol = c->vol; }
		}
		__sync_synchronize(); AU_STORE(cmd_r, cmd_r + 1);
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
static int rvb[GRAIN];   // b107: the reverb's bus (reverb.h): each voice's wet share, mono, before its pan
#include "reverb.h"
static void mix_voice(Voice *v, int mg) {
	const Sound *s = v->s; int pos = v->pos, vol = v->vol, ch = s->ch; unsigned frac = v->frac, step = v->step;
	int gl = v->gl, gr = v->gr, k = 0;
	if (s->fmt == 0) {   // 48 kHz PCM (test sounds)
		for (; k < GRAIN && pos < s->frames; k++) {
			int l, r, fr = (int)(frac >> 1), p1 = pos + 1 < s->frames ? pos + 1 : pos;
			if (ch == 2) { l = s->pcm[pos * 2]; r = s->pcm[pos * 2 + 1]; if (fr) { l += ((s->pcm[p1 * 2] - l) * fr) >> 15; r += ((s->pcm[p1 * 2 + 1] - r) * fr) >> 15; } }
			else { l = s->pcm[pos]; if (fr) l += ((s->pcm[p1] - l) * fr) >> 15; r = l; }
			int g = ((vol >> 8) * mg) >> 8; if (g > 1024) g = 1024;
			mix[k * 2] += (((l * g) >> 8) * gl) >> 8; mix[k * 2 + 1] += (((r * g) >> 8) * gr) >> 8;
			rvb[k] += (((((l + r) >> 1) * g) >> 8) * v->rv) >> 8;
			if (v->fade) { vol -= v->fade; if (vol <= 0) { pos = s->frames; break; } }
			frac += step; pos += frac >> 16; frac &= 0xffff;
		}
	} else while (k < GRAIN && pos < s->frames) {
		// block IMA ADPCM: run without per-sample block checks until the end of the decoded block
		int b = pos >> 10; if (b != v->blk) voice_block(v, b);
		int bend = (b + 1) << 10; if (bend > s->frames) bend = s->frames;
		const short *d0 = v->dec[0], *d1 = v->dec[ch == 2 ? 1 : 0]; int base = b << 10;
		int gv = ((vol >> 8) * mg) >> 8; if (gv > 1024) gv = 1024;   // b59: 4x the sample at most (quiet interface sounds), so
		int gL = (gv * gl) >> 2, gR = (gv * gr) >> 2;                  // gL <= 65536 and l * gL stays within 32 bits (>> 14 below)
		int gS = (gv * v->rv) >> 2;                                    // (b107: the reverb's share, as gL)
		if (!v->fade && ch == 1) {   // the common case: mono, steady volume
			int *o = mix + k * 2, kend = GRAIN;
			for (; k < kend && pos < bend; k++, o += 2) {
				int i = pos - base, l = d0[i] + (((d0[i + 1] - d0[i]) * (int)(frac >> 1)) >> 15);
				o[0] += (l * gL) >> 14; o[1] += (l * gR) >> 14; rvb[k] += (l * gS) >> 14;
				frac += step; pos += frac >> 16; frac &= 0xffff;
			}
		} else if (!v->fade) {
			for (; k < GRAIN && pos < bend; k++) {
				int i = pos - base, fr = (int)(frac >> 1);
				int l = d0[i] + (((d0[i + 1] - d0[i]) * fr) >> 15), r = d1[i] + (((d1[i + 1] - d1[i]) * fr) >> 15);
				mix[k * 2] += (l * gL) >> 14; mix[k * 2 + 1] += (r * gR) >> 14; rvb[k] += (((l + r) >> 1) * gS) >> 14;
				frac += step; pos += frac >> 16; frac &= 0xffff;
			}
		} else {
			for (; k < GRAIN && pos < bend; k++) {
				int i = pos - base, fr = (int)(frac >> 1), g = ((vol >> 8) * mg) >> 8; if (g > 1024) g = 1024;
				int l = d0[i] + (((d0[i + 1] - d0[i]) * fr) >> 15), r = ch == 2 ? d1[i] + (((d1[i + 1] - d1[i]) * fr) >> 15) : l;
				mix[k * 2] += (((l * g) >> 8) * gl) >> 8; mix[k * 2 + 1] += (((r * g) >> 8) * gr) >> 8; rvb[k] += (((((l + r) >> 1) * g) >> 8) * v->rv) >> 8;
				vol -= v->fade; if (vol <= 0) { pos = s->frames; break; }
				frac += step; pos += frac >> 16; frac &= 0xffff;
			}
		}
	}
	v->pos = pos; v->vol = vol; v->frac = frac; if (pos >= s->frames) v->active = 0;
}
static int stream_dec(Stream *s) {   // decode ring block rd; 0 if it has not been read yet
	if (s->rd >= AU_LOAD(s->wr)) return 0;
	int slot = s->rd % SRING, len = s->rlen[slot]; const unsigned char *p = s->ring[slot];
	for (int c = 0; c < s->ch; c++) {
		adp_decode(p + c * ABB, s->dec[c]);
		short nx = s->dec[c][len - 1];
		if (s->rd + 1 < AU_LOAD(s->wr)) nx = adp_first(s->ring[(s->rd + 1) % SRING] + c * ABB);
		s->dec[c][len] = nx;   // sample after the last valid one (next block, or the loop start)
	}
	s->dec_ok = 1; return 1;
}
static void mix_stream(Stream *s, int mg, float gain) {
	int gg = (int)(gain * 256), ch = s->ch, k = 0;
	while (k < GRAIN) {
		if (!s->dec_ok && !stream_dec(s)) { if (AU_LOAD(s->eof)) { if (!stream_dec(s)) AU_STORE(s->state, ST_CLOSE); } else if (s->wr > 0) stream_underruns++; return; }   // (the end flag seen: the last block too)
		int len = s->rlen[s->rd % SRING], i = s->i; unsigned frac = s->frac, step = s->step;
		const short *d0 = s->dec[0], *d1 = s->dec[ch == 2 ? 1 : 0];
		if (s->vol == s->tvol && !s->stopping) {   // steady volume: no per-sample ramp
			int g = ((((s->vol >> 8) * mg) >> 8) * gg) >> 8; if (g > 1024) g = 1024;   // b59: up to 4x (as the voices)
			int gL = (g * s->gl) >> 2, gR = (g * s->gr) >> 2, gS = (g * s->rv) >> 2;
			if (ch == 1) for (; k < GRAIN && i < len; k++) {
				int l = d0[i] + (((d0[i + 1] - d0[i]) * (int)(frac >> 1)) >> 15);
				mix[k * 2] += (l * gL) >> 14; mix[k * 2 + 1] += (l * gR) >> 14; rvb[k] += (l * gS) >> 14;
				frac += step; i += frac >> 16; frac &= 0xffff;
			} else for (; k < GRAIN && i < len; k++) {
				int fr = (int)(frac >> 1);
				int l = d0[i] + (((d0[i + 1] - d0[i]) * fr) >> 15), r = d1[i] + (((d1[i + 1] - d1[i]) * fr) >> 15);
				mix[k * 2] += (l * gL) >> 14; mix[k * 2 + 1] += (r * gR) >> 14; rvb[k] += (((l + r) >> 1) * gS) >> 14;
				frac += step; i += frac >> 16; frac &= 0xffff;
			}
		} else {
			for (; k < GRAIN && i < len; k++) {
				int fr = (int)(frac >> 1), g = ((((s->vol >> 8) * mg) >> 8) * gg) >> 8; if (g > 1024) g = 1024;
				int l = d0[i] + (((d0[i + 1] - d0[i]) * fr) >> 15), r = ch == 2 ? d1[i] + (((d1[i + 1] - d1[i]) * fr) >> 15) : l;
				mix[k * 2] += (((l * g) >> 8) * s->gl) >> 8; mix[k * 2 + 1] += (((r * g) >> 8) * s->gr) >> 8; rvb[k] += (((((l + r) >> 1) * g) >> 8) * s->rv) >> 8;
				if (s->vol != s->tvol) { if (s->vol < s->tvol) { s->vol += s->vstep; if (s->vol > s->tvol) s->vol = s->tvol; } else { s->vol -= s->vstep; if (s->vol < s->tvol) s->vol = s->tvol; } }
				else if (s->stopping && s->vol <= 0) { AU_STORE(s->state, ST_CLOSE); return; }
				frac += step; i += frac >> 16; frac &= 0xffff;
			}
		}
		s->i = i; s->frac = frac;
		while (s->i >= len) {
			s->i -= len; s->dec_ok = 0; __sync_synchronize(); AU_STORE(s->rd, s->rd + 1);
			if (!stream_dec(s) && !(AU_LOAD(s->eof) && stream_dec(s))) { if (AU_LOAD(s->eof)) AU_STORE(s->state, ST_CLOSE); else stream_underruns++; s->i = 0; return; }
			len = s->rlen[s->rd % SRING];
		}
	}
}
// ---------------------------------------------------------------- stream reader (I/O thread)
static int stream_open(Stream *s) {
	s->fd = sceIoOpen(s->path, SCE_O_RDONLY, 0);
	if (s->fd < 0 && s->hdr) {   // data laid out with an audio/ folder
		char p[128]; const char *sl = strrchr(s->path, '/');
		if (sl && strncmp(sl - 5, "audio", 5)) { snprintf(p, sizeof(p), "%.*saudio%s", (int)(sl - s->path + 1), s->path, sl); s->fd = sceIoOpen(p, SCE_O_RDONLY, 0); }
	}
	if (s->fd < 0) return -1;
	if (s->hdr) {   // GSTR: magic, rate, ch (u16) + pad, frames, nblk, frames per block, loop start frame, data offset
		unsigned h[8]; if (sceIoRead(s->fd, h, 32) != 32 || memcmp(h, "GSTR", 4) || h[5] != ABF) return -2;
		s->rate = h[1]; s->ch = h[2] & 0xffff; s->frames = h[3]; s->nblk = h[4]; s->loop_blk = h[6] / ABF; s->base = h[7];
	}
	if (s->ch < 1 || s->ch > 2 || s->nblk <= 0 || s->rate <= 0) return -3;
	s->step = (unsigned)(((unsigned long long)s->rate << 16) / 48000); s->fpos_blk = -1; return 0;
}
static void stream_fill(Stream *s) {
	int bsz = s->ch * ABB;
	while (s->wr - AU_LOAD(s->rd) < SRING && !s->eof) {
		int j = s->wr, fb;
		if (j < s->nblk) fb = j;
		else if (s->loop) { int span = s->nblk - s->loop_blk; fb = s->loop_blk + (j - s->nblk) % (span > 0 ? span : 1); }
		else { AU_STORE(s->eof, 1); break; }
		if (fb != s->fpos_blk) sceIoLseek(s->fd, (SceOff)s->base + (SceOff)fb * bsz, SCE_SEEK_SET);
		if (sceIoRead(s->fd, s->ring[j % SRING], bsz) != bsz) { AU_STORE(s->eof, 1); break; }
		s->fpos_blk = fb + 1;
		s->rlen[j % SRING] = fb == s->nblk - 1 ? (unsigned short)(s->frames - fb * ABF) : ABF;
		__sync_synchronize(); AU_STORE(s->wr, j + 1);
		if (!s->loop && j + 1 >= s->nblk) AU_STORE(s->eof, 1);
		if (AU_LOAD(s->state) == ST_CLOSE) break;
	}
}
static void audio_io_step(void) {
	for (int i = 0; i < NSTREAM; i++) {
		Stream *s = &streams[i]; int st = AU_LOAD(s->state);
		if (st == ST_OPEN) {
			if (s->stopping || stream_open(s) < 0) {
				if (s->fd >= 0) sceIoClose(s->fd);
				s->fd = -1; if (!s->stopping) stream_open_fail++;
				__sync_synchronize(); AU_STORE(s->state, ST_IDLE); continue; }
			stream_fill(s); __sync_synchronize(); AU_STORE(s->state, ST_PLAY);   // primed before the mixer sees it
		} else if (st == ST_PLAY) stream_fill(s);
		else if (st == ST_CLOSE) { if (s->fd >= 0) { sceIoClose(s->fd); } s->fd = -1; __sync_synchronize(); AU_STORE(s->state, ST_IDLE); }
	}
}
static int audio_io_thread(SceSize args, void *argp) {
	while (audio_run) { audio_io_step(); sceKernelDelayThread(3000); }
	return 0;
}
// one grain of the mixer into mix[] (the audio thread's work; separate so it can be tested off-device)
static int audio_mix_grain(void) {
	run_cmds();
	memset(mix, 0, sizeof(mix)); memset(rvb, 0, sizeof(rvb)); int nact = 0;
	bus_music += (bus_music_t - bus_music) * 0.05f; bus_sfx += (bus_sfx_t - bus_sfx) * 0.05f;
	int mg = (int)(master_gain * 256), mgs = (int)(master_gain * sfx_gain * bus_sfx * 256);
	const float cg[4] = { music_gain * bus_music, amb_gain, voice_gain, sfx_gain };
	for (int i = 0; i < NVOICE; i++) { Voice *v = &voices[i]; if (!v->active) continue; nact++; mix_voice(v, mgs); }
	for (int i = 0; i < NSTREAM; i++) { Stream *s = &streams[i]; if (AU_LOAD(s->state) != ST_PLAY) continue; nact++;
		mix_stream(s, mg, cg[s->cat & 3]); }
	reverb_grain(rvb, mix, GRAIN);   // b107: the room (reverb.h)
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
	audio_open_res = open_variant(audio_variant); AU_STORE(audio_alive, 1);
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
	for (int i = 0; i < 60 && !AU_LOAD(audio_alive); i++) sceKernelDelayThread(5000);
	return audio_open_res < 0 ? audio_open_res : 0;
}
