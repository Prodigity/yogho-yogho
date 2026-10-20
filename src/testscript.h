#ifndef YOGHO_TESTSCRIPT_H
#define YOGHO_TESTSCRIPT_H
#include <stdint.h>
#include "platform.h"
extern int script_fast, script_mouse_override, script_mx, script_my, script_mb;
int script_load(const char *path);
void script_add(uint64_t tick, const char *cmd, const char *arg);   /* events must be added in tick order */
int script_tick(uint64_t tick, key_fn keyh);   /* returns 1 to quit */
#endif
