#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_SEEN 256
static char seen[MAX_SEEN][128];
static int nseen;

void log_gap(const char *fmt, ...) {
    char buf[128];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    for (int i = 0; i < nseen; i++) if (!strcmp(seen[i], buf)) return;
    if (nseen < MAX_SEEN) strcpy(seen[nseen++], buf);
    fprintf(stderr, "[gap] %s\n", buf);
}

void log_info(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[info] "); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}
