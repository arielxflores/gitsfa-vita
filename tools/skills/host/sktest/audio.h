// Host stub of audio.h for the skills.h test: records what the skill code plays and checks every name against the real
// tables (FMOD events of engine/src/audio.h, RAM sounds and voice lines of build/data/audio_manifest.txt; names_gen.h).
#pragma once
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "names_gen.h"
static float snd_lis[3], snd_lis_yaw;
static int aud_events, aud_ram, aud_voice, aud_bad; static char aud_last[96];
static char aud_log[4096][64]; static int aud_nlog;
static void aud_rec(const char *k, const char *n) { if (aud_nlog < 4096) snprintf(aud_log[aud_nlog++], 64, "%s %s", k, n); snprintf(aud_last, sizeof(aud_last), "%s", n); }
static int aud_has(const char *const *t, int n, const char *name, int prefix) {
	size_t l = strlen(name); for (int i = 0; i < n; i++) if (prefix ? !strncmp(t[i], name, l) : !strcmp(t[i], name)) return 1; return 0; }
#define AUD_N(t) ((int)(sizeof(t) / sizeof(t[0])))
static void aud_check(const char *kind, const char *name, int ok) { if (!ok) { aud_bad++; fprintf(stderr, "AUDIO: unknown %s '%s'\n", kind, name); } }
static int ev_find(const char *name) { for (int i = 0; i < AUD_N(TEST_EV); i++) if (!strcmp(TEST_EV[i], name)) return i; return -1; }
static int ev_play(const char *name, const float *pos) { (void)pos; aud_events++; aud_rec("ev", name); aud_check("event", name, ev_find(name) >= 0); return 1; }
static void ev_play_later(const char *name, const float *pos, float d) { (void)d; ev_play(name, pos); }
static void radio_play(const char *name) { ev_play(name, NULL); }
static int play_pan(const char *p, int vol, float pan) { (void)vol; (void)pan; aud_ram++; aud_rec("ram", p); aud_check("RAM sound", p, aud_has(TEST_RAM, AUD_N(TEST_RAM), p, 1)); return 1; }
static int play(const char *p, int vol) { return play_pan(p, vol, 0); }
static void fade_others(const char *p, int keep, int ms) { (void)keep; (void)ms; aud_check("RAM sound (fade)", p, aud_has(TEST_RAM, AUD_N(TEST_RAM), p, 1)); }
static int voice_play(const char *p, int vol, float pan) { (void)vol; (void)pan; aud_voice++; aud_rec("voice", p); aud_check("voice line", p, aud_has(TEST_VOICE, AUD_N(TEST_VOICE), p, 1)); return 1; }
static int snd_voice(const char *chr, const char *what, const float *pos) { char n[96]; snprintf(n, sizeof(n), "voice/action/%s/%s_%s", chr, chr, what); return ev_play(n, pos); }
static void snd_wfire_tp(const char *sh, const float *pos) { (void)sh; (void)pos; aud_rec("ev", "weapon fire tp"); }
