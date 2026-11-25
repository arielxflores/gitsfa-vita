// redirect fopen of ux0: paths to the build data dir
#include <stdio.h>
#include <string.h>
static FILE *host_fopen(const char *path, const char *m) { char p[512]; const char *q = strstr(path, "gitsfa/"); snprintf(p, sizeof(p), "/root/gits/build/data/%s", q ? q + 7 : path); return fopen(p, m); }
#define fopen host_fopen
