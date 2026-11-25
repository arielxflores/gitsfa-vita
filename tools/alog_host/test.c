// host test of engine/src/alog.h: lines come out complete and in order through ring wrap-arounds, with 3 producer
// threads and a concurrent drainer; a full ring drops (and counts) instead of blocking.
// build: gcc -O2 -pthread -Wall test.c -o /tmp/alt && /tmp/alt /tmp/alog_test.txt
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "../../engine/src/alog.h"
static void lp(const char *fmt, ...) { va_list a; va_start(a, fmt); alog_vprintf(fmt, a); va_end(a); }
static volatile int stop;
static void *drainer(void *p) { (void)p; while (!stop) { alog_drain(); } alog_drain(); return NULL; }
static void *producer(void *p) { int k = (int)(intptr_t)p; for (int i = 0; i < 20000; i++) lp("P%d line %06d %s\n", k, i, "abcdefghijklmnopqrstuvwxyz0123456789"); return NULL; }
int main(int argc, char **argv) {
	const char *path = argc > 1 ? argv[1] : "/tmp/alog_test.txt";
	if (alog_open(path, 0) < 0) return 2;
	pthread_t d, p[3]; pthread_create(&d, NULL, drainer, NULL);
	for (int k = 0; k < 3; k++) pthread_create(&p[k], NULL, producer, (void *)(intptr_t)k);
	for (int k = 0; k < 3; k++) pthread_join(p[k], NULL);
	stop = 1; pthread_join(d, NULL);
	unsigned dropped_concurrent = alog_drop;
	// no drainer: 2 MB into the 512 KB ring -> drops, never blocks
	for (int i = 0; i < 40000; i++) lp("X line %06d %s\n", i, "abcdefghijklmnopqrstuvwxyz0123456789");
	unsigned dropped = alog_drop;
	alog_close();
	// check: every P line complete, per producer in order
	FILE *f = fopen(path, "r"); char s[256]; int next[3] = { 0, 0, 0 }, bad = 0, xs = 0, notes = 0;
	while (fgets(s, sizeof(s), f)) {
		int k, i; char tail[64];
		if (s[0] == 'P' && sscanf(s, "P%d line %d %63s", &k, &i, tail) == 3 && k >= 0 && k < 3 && !strcmp(tail, "abcdefghijklmnopqrstuvwxyz0123456789")) { if (i < next[k]) bad++; next[k] = i + 1; }
		else if (s[0] == 'X') xs++;
		else if (s[0] == '[') notes++;
		else bad++;
	}
	fclose(f);
	printf("producers: last lines %d %d %d (want 19999+1 unless dropped), dropped while draining %u B; no drainer: %d X lines kept, %u B dropped, %d notes; bad lines %d\n",
		next[0], next[1], next[2], dropped_concurrent, xs, dropped - dropped_concurrent, notes, bad);
	int ok = !bad && xs > 0 && xs < 40000 && dropped > dropped_concurrent && (dropped_concurrent || (next[0] == 20000 && next[1] == 20000 && next[2] == 20000));
	printf(ok ? "PASS\n" : "FAIL\n"); return ok ? 0 : 1;
}
