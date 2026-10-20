/* Player module (player.c, map.c): routines it needs from other modules or exports beyond game.h. */
#ifndef YOGHO_PLAYER_EXT_H
#define YOGHO_PLAYER_EXT_H
#include <stdint.h>

/* Provided by the flow module (joystick owner): 2a33:007d, C far call, no arguments; returns AX,
 * the caller ORs AL into the 0x3510 direction byte (R 1, L 2, D 4, U 8, button 0x80).
 * read_input (160a:1cb4) calls it every logic step when DGROUP byte 0x12c == 0xFF. */
uint16_t joy_to_dirbits(void);

/* Developer trace (player_selftest.c): needs MEM holding the exe (mem_load_exe) and the DAT
 * opened (dat_open).  Loads the L1 collision data, places the player at the L1 start and prints
 * one line per player_tick() for a scripted input.  Returns 0 on success. */
int player_selftest(void);

#endif
