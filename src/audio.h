#ifndef YOGHO_AUDIO_H
#define YOGHO_AUDIO_H
void audio_init(void);   /* connect adlib driver -> OPL core -> platform audio */
void audio_tick(void);   /* generate one vsync tick of samples (called after each ISR tick) */
#endif
