// host stub of engine/src/audio.h: the calls bots.h makes, recorded in a log (tools/chars/host/bot_sim.c)
#pragma once
#include <stdio.h>
#include <string.h>
#include <time.h>
static FILE *snd_log; static float snd_t;
static void snd_rec(const char *what, const char *name, const float *pos) { if (snd_log) fprintf(snd_log, "%7.3f %-6s %s %s\n", snd_t, what, name, pos ? "" : "(2D)"); }
static int play_pan(const char *p, int vol, float pan) { (void)vol; (void)pan; snd_rec("play", p, NULL); return 0; }
static const char *ev_names[512]; static int nev_names;
static int ev_find(const char *n) { for (int i = 0; i < nev_names; i++) if (!strcmp(ev_names[i], n)) return i; if (nev_names < 512) { ev_names[nev_names] = strdup(n); return nev_names++; } return -1; }
static int ev_play_ex(int e, const float *pos, int indoor, int aim, float prm, float vol) { (void)indoor; (void)aim; (void)prm; (void)vol; if (e >= 0) snd_rec("ev", ev_names[e], pos); return 0; }
static int ev_play(const char *n, const float *pos) { return ev_play_ex(ev_find(n), pos, 0, 0, 0, 1); }
typedef struct { const char *sh, *base, *shell, *impact; } SndWeapon;
static const SndWeapon SND_WEAPON[] = { { "m4a1", "weapon/ar/m4a1", "ar", "ar" }, { "c30", "weapon/ar/seburo_c-30", "ar", "ar" }, { "scar", "weapon/ar/scar", "ar", "ar" },
	{ "acr", "weapon/ar/acr", "ar", "ar" }, { "mp5", "weapon/smg/mp5", "smg", "smg" }, { "kriss", "weapon/smg/kriss_super_v", "smg", "smg" }, { "m249", "weapon/mg/m249", "mg", "mg" }, { "l118", "weapon/sr/l118a", "sr_bolt", "sr" }, { "870", "weapon/sg/870mcs", "sg", "sg" } };
static const SndWeapon *snd_wpn(const char *sh) { for (unsigned i = 0; i < sizeof(SND_WEAPON) / sizeof(SND_WEAPON[0]); i++) if (!strcmp(SND_WEAPON[i].sh, sh)) return &SND_WEAPON[i]; return &SND_WEAPON[0]; }
static void snd_wfire_tp(const char *sh, const float *pos) { if (!strcmp(sh, "knife")) { snd_rec("fire", "weapon/knife/swing_tp", pos); return; } char n[80]; snprintf(n, 80, "%s/fire_tp", snd_wpn(sh)->base); snd_rec("fire", n, pos); }
static int snd_weapon(const char *b, const char *w, const float *pos, int indoor) { (void)indoor; char n[80]; snprintf(n, 80, "%s/%s", b, w); snd_rec("wpn", n, pos); return 0; }
static int snd_voice(const char *c, const char *w, const float *pos) { char n[80]; snprintf(n, 80, "voice/%s/%s", c, w); snd_rec("voice", n, pos); return 0; }
static const char *SND_MAT_FOOT3[9] = { "stone", "wood", "metal", "dirt", "glass", "metal_car", "paper", "paper", "stone" };
static int snd_foley(int who, const char *act, int mat, const float *pos) { char n[80]; snprintf(n, 80, "foley/%s/%s_%s", who == 2 ? "female" : "enemy", act, SND_MAT_FOOT3[mat < 0 || mat > 8 ? 0 : mat]); snd_rec("foley", n, pos); return 0; }
static int snd_chr_female(const char *c) { return !strcmp(c, "motoko") || !strcmp(c, "reiko") || !strcmp(c, "sitara") || !strcmp(c, "kuro"); }
typedef unsigned long long SceUInt64;
static SceUInt64 sceKernelGetProcessTimeWide(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (SceUInt64)t.tv_sec * 1000000 + t.tv_nsec / 1000; }
