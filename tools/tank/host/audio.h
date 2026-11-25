// Host stub of engine/src/audio.h for the tank host test (after tools/skills/host/sktest/audio.h): records what the code
// plays and checks every name against the real tables (FMOD events of audio.h, RAM sounds / voice lines / music streams of
// build/data/audio_manifest.txt, names_gen.h from gen_names.py). Voice lines: find_vline picks the first line of a prefix.
#pragma once
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "names_gen.h"
typedef unsigned long long SceUInt64;
static SceUInt64 sceKernelGetProcessTimeWide(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (SceUInt64)t.tv_sec * 1000000 + t.tv_nsec / 1000; }
typedef struct { char name[48]; unsigned rate; unsigned short ch, flags; unsigned frames, nblk, off; } VEntry;
static float snd_lis[3], snd_lis_yaw;
static float music_gain = 0.5f, amb_gain = 0.55f, voice_gain = 1.0f;
static char snd_dir[96] = "";
static int aud_events, aud_ram, aud_voice, aud_music, aud_bad; static char aud_last[96];
static char aud_log[8192][64]; static int aud_nlog;
static void aud_rec(const char *k, const char *n) { if (aud_nlog < 8192) snprintf(aud_log[aud_nlog++], 64, "%s %s", k, n); snprintf(aud_last, sizeof(aud_last), "%s", n); }
static int aud_has(const char *const *t, int n, const char *name, int prefix) {
	size_t l = strlen(name); for (int i = 0; i < n; i++) if (prefix ? !strncmp(t[i], name, l) : !strcmp(t[i], name)) return 1; return 0; }
#define AUD_N(t) ((int)(sizeof(t) / sizeof(t[0])))
static void aud_check(const char *kind, const char *name, int ok) { if (!ok) { aud_bad++; fprintf(stderr, "AUDIO: unknown %s '%s'\n", kind, name); } }
static int ev_find(const char *name) { for (int i = 0; i < AUD_N(TEST_EV); i++) if (!strcmp(TEST_EV[i], name)) return i; return -1; }
static int ev_play(const char *name, const float *pos) { (void)pos; aud_events++; aud_rec("ev", name); aud_check("event", name, ev_find(name) >= 0); return 1; }
static void ev_play_later(const char *name, const float *pos, float d) { (void)d; ev_play(name, pos); }
static void radio_play(const char *name) { ev_play(name, NULL); }
static int music_ev(const char *name, int fade_ms) { (void)fade_ms; return ev_play(name, NULL); }
static int play_pan(const char *p, int vol, float pan) { (void)vol; (void)pan; aud_ram++; aud_rec("ram", p); aud_check("RAM sound", p, aud_has(TEST_RAM, AUD_N(TEST_RAM), p, 1)); return 1; }
static int play(const char *p, int vol) { return play_pan(p, vol, 0); }
static void fade_others(const char *p, int keep, int ms) { (void)keep; (void)ms; aud_check("RAM sound (fade)", p, aud_has(TEST_RAM, AUD_N(TEST_RAM), p, 1)); }
static int voice_play(const char *p, int vol, float pan) { (void)vol; (void)pan; aud_voice++; aud_rec("voice", p); aud_check("voice line", p, aud_has(TEST_VOICE, AUD_N(TEST_VOICE), p, 1)); return 1; }
static int snd_voice(const char *chr, const char *what, const float *pos) { char n[96]; snprintf(n, sizeof(n), "voice/action/%s/%s_%s", chr, chr, what); return ev_play(n, pos); }
static void snd_wfire_tp(const char *sh, const float *pos) { (void)pos; char n[96]; snprintf(n, sizeof(n), "%s/fire_tp", snd_wpn(sh)->base); aud_rec("ev", n); }
static void snd_gren_exp(const float *pos) { ev_play("weapon/grenade/m67/exp", pos); }
static int snd_killvoice(const char *chr, const char *what) { char n[96]; snprintf(n, sizeof(n), "voice/action/%s/%s_%s", chr, chr, what); aud_rec("killvoice", n); return 1; }
// voice lines: a table of the manifest's names (find_vline returns the first match of a prefix, as the engine's picks one)
static VEntry aud_vl[4096]; static int nvline;
static void aud_init(void) { nvline = 0; for (int i = 0; i < AUD_N(TEST_VOICE) && i < 4096; i++) { VEntry *e = &aud_vl[nvline++]; memset(e, 0, sizeof(*e)); snprintf(e->name, 48, "%s", TEST_VOICE[i]); e->rate = 24000; e->frames = 72000; } }
static const VEntry *find_vline(const char *prefix) { size_t l = strlen(prefix); for (int i = 0; i < nvline; i++) if (!strncmp(aud_vl[i].name, prefix, l)) return &aud_vl[i]; aud_check("voice prefix", prefix, 0); return NULL; }
static float vline_secs(const VEntry *e) { return e->rate ? (float)e->frames / e->rate : 0; }
static int voice_play_entry(const VEntry *e, int vol16, float pan, int pri) { (void)vol16; (void)pan; (void)pri; aud_voice++; aud_rec("voice", e->name); return 1; }
static int music_play(const char *path, int loop, int vol, int fade_ms) {
	(void)loop; (void)vol; (void)fade_ms; aud_music++; const char *b = strrchr(path, '/'); b = b ? b + 1 : path; char n[64]; snprintf(n, sizeof(n), "%s", b); char *d = strstr(n, ".gstr"); if (d) *d = 0;
	aud_rec("music", n); aud_check("music stream", n, aud_has(TEST_MUSIC, AUD_N(TEST_MUSIC), n, 0)); return 1; }
static void music_stop(int fade_ms) { (void)fade_ms; aud_rec("music", "(stop)"); }
// (bots.h)
static int ev_play_ex(int e, const float *pos, int indoor, int aim, float prm, float vol) { (void)pos; (void)indoor; (void)aim; (void)prm; (void)vol; aud_events++; aud_rec("ev", e >= 0 ? TEST_EV[e] : "?"); aud_check("event index", "?", e >= 0); return 1; }
static int snd_chr_female(const char *chr) { return !strcmp(chr, "motoko") || !strcmp(chr, "reiko") || !strcmp(chr, "sitara") || !strcmp(chr, "kuro"); }
static int snd_foley(int who, const char *act, int mat, const float *pos) { (void)who; (void)act; (void)mat; (void)pos; return 1; }
static int snd_weapon(const char *base, const char *what, const float *pos, int indoor) { (void)base; (void)what; (void)pos; (void)indoor; return 1; }
