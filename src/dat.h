/* YOGHODAT.DAT access.  Format (teardown 80-data): u32 LE offset table at 0, count = offs[0] / 4,
 * entry i spans offs[i] .. offs[i+1] (last ends at EOF).  The original reads entries with
 * 160a:12a0 (by index) and 160a:12e0 (raw read). */
#ifndef YOGHO_DAT_H
#define YOGHO_DAT_H
#include <stddef.h>
#include <stdint.h>

int dat_open(const char *path);
int dat_count(void);
const uint8_t *dat_entry(int idx, size_t *size);
const uint8_t *dat_raw(size_t *size);

#endif
