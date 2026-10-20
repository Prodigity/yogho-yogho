/* Audio output: the AdLib driver's register writes (adlib.c sink) go to a Nuked OPL3 core
 * running in OPL2-compatible mode.  Samples are generated on the game side, one vsync tick's
 * worth after every timer ISR call, so music timing follows the game's 70.086 Hz ticks exactly;
 * the platform audio callback drains the ring buffer. */
#include "audio.h"
#include "adlib.h"
#include "platform.h"
#include "opl3.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define RING 16384
static opl3_chip chip;
static int16_t ring[RING];
static volatile unsigned rd, wr;
static double frac;
static int rate;

/* Writes go through Nuked's write buffer, which applies them a couple of samples apart.  The
 * driver keys notes off and on again in consecutive writes (B0=0, B0=on); on the real card the
 * 6+27 I/O-delay reads after each write (~25 us, more than one 49.7 kHz OPL sample) let the chip
 * see the key-off, so the note re-attacks.  Applying a tick's writes in the same sample would
 * merge the off/on pair and notes would never retrigger (music fading to silence). */
static void sink(uint8_t reg, uint8_t val) { OPL3_WriteRegBuffered(&chip, reg, val); }

static void pull(int16_t *out, int frames) {
    for (int i = 0; i < frames; i++) {
        if (rd != wr) { out[i] = ring[rd]; rd = (rd + 1) % RING; }
        else out[i] = 0;
    }
}

void audio_init(void) {
    rate = plat_audio_rate();
    OPL3_Reset(&chip, (uint32_t)rate);
    adlib_set_sink(sink);
    plat_set_audio(pull);
}

static FILE *wav;      /* developer aid: YOGHO_WAV=<path> writes the raw mono S16 stream */
static unsigned long opl_writes;

void audio_tick(void) {
    frac += (double)rate / 70.086303;
    int n = (int)frac;
    frac -= n;
    if (!wav && getenv("YOGHO_WAV")) wav = fopen(getenv("YOGHO_WAV"), "wb");
    plat_audio_lock(1);
    for (int i = 0; i < n; i++) {
        int16_t s[2];
        OPL3_GenerateResampled(&chip, s);
        unsigned next = (wr + 1) % RING;
        if (next == rd) break;           /* consumer stalled: drop */
        int v = (s[0] + s[1]);            /* mono mix; OPL2 output appears on both */
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        if (wav) { int16_t o = (int16_t)v; fwrite(&o, 2, 1, wav); }
        ring[wr] = (int16_t)v;
        wr = next;
    }
    /* keep latency bounded: if more than ~0.25 s is queued, drop the oldest */
    unsigned queued = (wr + RING - rd) % RING;
    unsigned maxq = (unsigned)rate / 4;
    if (queued > maxq) rd = (wr + RING - maxq) % RING;
    plat_audio_lock(0);
}
