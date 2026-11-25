#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "reverb.h"
int main(void) {
	RevDef d = { "t", -200, -500, -1500, 2.5f, 0.8f, -300, 0.014f, 600, 0.02f, 100, 20, 6000, 200 }, d2 = { "u", -500, -1000, -1000, 3.0f, 0.65f, -400, 0.06f, 200, 0.1f, 100, 100, 5000, 250 };
	reverb_set(&d2, &d); rv_indoor = 0.4f;
	static int bus[1024], out[2048]; srand(1);
	struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a); long long acc = 0;
	for (int g = 0; g < 20000; g++) { for (int k = 0; k < 1024; k++) bus[k] = (rand() & 2047) - 1024; rv_indoor = 0.4f + 0.3f * ((g / 50) & 1); reverb_grain(bus, out, 1024); acc += out[100]; out[100] = 0; }
	clock_gettime(CLOCK_MONOTONIC, &b); double us = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 20000 / 1000;
	// (the rand() fill is part of the loop: measured apart)
	clock_gettime(CLOCK_MONOTONIC, &a); for (int g = 0; g < 20000; g++) { for (int k = 0; k < 1024; k++) bus[k] = (rand() & 2047) - 1024; acc += bus[5]; } clock_gettime(CLOCK_MONOTONIC, &b);
	double us0 = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 20000 / 1000;
	printf("reverb: %.1f us per grain on this host (%.1f with the fill), %lld\n", us - us0, us, acc); return 0; }
