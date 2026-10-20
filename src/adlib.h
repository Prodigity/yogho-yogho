/* AdLib music/SFX driver — port of Ghidra segment 2f6e (runtime 2792). Spec: teardown/70-sound.md.
 * Modules are S3M files living in MEM at seg:off (the driver patches them in place like the original). */
#ifndef YOGHO_ADLIB_H
#define YOGHO_ADLIB_H
#include <stdint.h>

/* Register sink: every write that reaches the "hardware" (after the shadow cache). */
typedef void (*opl_sink_fn)(uint8_t reg, uint8_t val);
void adlib_set_sink(opl_sink_fn f);

void adlib_detect(void);                          /* 2f6e:007b (always "detected") */
void adlib_music_start(uint16_t seg, uint16_t off, uint16_t arg);   /* 2f6e:00ec */
void adlib_music_stop(void);                      /* 2f6e:0784 */
void adlib_sfx_load(uint16_t seg, uint16_t off);  /* 2f6e:07dc */
void adlib_tick(uint16_t sfx_request);            /* 2f6e:0292 */

/* Engine-side wrappers (seg 1000) and the ISR hook. */
void MusicPlay(uint16_t seg, uint16_t off, uint16_t arg);  /* 1000:47bb */
void SfxLoad(uint16_t seg, uint16_t off);         /* 1000:47df */
void AdlibDetect(void);                           /* 1000:47f4 */
void MusicStop(void);                             /* 1000:4804 */
void isr_music_hook(void);                        /* 1000:090c */

#endif
