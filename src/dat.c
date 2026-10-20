#include "dat.h"
#include <stdio.h>
#include <stdlib.h>

static uint8_t *dat_buf;
static size_t dat_len;
static int dat_n;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int dat_open(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    dat_len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    dat_buf = malloc(dat_len);
    if (!dat_buf || fread(dat_buf, 1, dat_len, f) != dat_len) { fclose(f); return -1; }
    fclose(f);
    dat_n = (int)(rd32(dat_buf) / 4);
    return 0;
}

int dat_count(void) { return dat_n; }

const uint8_t *dat_entry(int idx, size_t *size) {
    if (idx < 0 || idx >= dat_n) return NULL;
    uint32_t a = rd32(dat_buf + 4 * idx);
    uint32_t b = idx + 1 < dat_n ? rd32(dat_buf + 4 * (idx + 1)) : (uint32_t)dat_len;
    if (size) *size = b - a;
    return dat_buf + a;
}

const uint8_t *dat_raw(size_t *size) { if (size) *size = dat_len; return dat_buf; }
