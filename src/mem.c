#include "mem.h"
#include <stdio.h>
#include <stdlib.h>

uint8_t MEM[MEM_SIZE];

int mem_load_exe(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return -1; }
    fclose(f);
    if (n < 0x40 || b[0] != 'M' || b[1] != 'Z') { free(b); return -1; }
    unsigned cblp = b[2] | b[3] << 8, cp = b[4] | b[5] << 8, nrel = b[6] | b[7] << 8;
    unsigned hdr = (b[8] | b[9] << 8) * 16u, relo = b[0x18] | b[0x19] << 8;
    long size = (long)(cp - 1) * 512 + (cblp ? cblp : 512) - (long)hdr;
    if (hdr + size > (unsigned long)n) { free(b); return -1; }
    memcpy(&MEM[lin(SEG_EXE, 0)], b + hdr, (size_t)size);
    for (unsigned i = 0; i < nrel; i++) {
        const uint8_t *r = b + relo + i * 4;
        uint16_t off = (uint16_t)(r[0] | r[1] << 8), seg = (uint16_t)(r[2] | r[3] << 8);
        uint16_t s = (uint16_t)(SEG_EXE + seg);
        ww(s, off, (uint16_t)(rw(s, off) + SEG_EXE));
    }
    free(b);
    return 0;
}
