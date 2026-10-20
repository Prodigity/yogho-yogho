/* Platform layer (SDL2, desktop + Emscripten).
 *
 * The original runs two threads of control: the main program (C, segment 160a) that
 * busy-waits on counters, and the timer ISR (1000:05f0) that runs once per VGA retrace and
 * does the per-frame game work.  The port keeps that structure: game code calls plat_yield()
 * wherever the original busy-waits; plat_yield() runs the ISR for every elapsed retrace
 * (70.086 Hz), presents the frame and returns.  On the web it yields to the browser
 * (Emscripten ASYNCIFY).
 */
#ifndef YOGHO_PLATFORM_H
#define YOGHO_PLATFORM_H
#include <stdint.h>

typedef void (*isr_fn)(void);

int  plat_init(const char *title);
void plat_set_isr(isr_fn f);       /* called once per emulated retrace while yielding */
void plat_set_tick_hook(isr_fn f); /* called after the ISR on every retrace (audio generation) */
void plat_yield(void);             /* run due ISR ticks, present, process input */
void plat_quit(void);
int  plat_should_quit(void);

/* Keyboard as the original sees it: scancode make/break events delivered to the game's
 * keyboard handler (port of 1000:09c3). */
typedef void (*key_fn)(uint8_t scancode, int down, int extended);
void plat_set_key_handler(key_fn f);

/* Mouse in the int 33h coordinate space the menu uses (x 0..639, y 0..199). */
void plat_mouse(int *x, int *y, int *buttons);

/* Audio: the sound driver fills 16-bit mono samples at plat_audio_rate(). */
typedef void (*audio_fn)(int16_t *out, int frames);
void plat_set_audio(audio_fn f);
int  plat_audio_rate(void);
void plat_audio_lock(int lock);

uint64_t plat_ticks(void);         /* number of emulated retraces since start */

#endif
