#include "platform.h"
#include "vga.h"
#include <SDL.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define VGA_HZ 70.086303   /* 25.175 MHz / (800 * 449) : 320x200 doubled-line mode */

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static uint32_t frame[320 * 200];
static isr_fn isr;
static key_fn keyh;
static audio_fn audio_cb;
static SDL_AudioDeviceID audio_dev;
static int audio_rate = 49716;   /* OPL2 native rate */
static int quit_flag;
static uint64_t ticks_done;
static double t0;

static double now_s(void) { return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); }

static void SDLCALL audio_thunk(void *u, Uint8 *stream, int len) {
    (void)u;
    if (audio_cb) audio_cb((int16_t *)stream, len / 2);
    else memset(stream, 0, len);
}

int plat_init(const char *title) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) return -1;
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720,
                           SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    /* 320x200 shown at 4:3 like a CRT: logical 320x240 with the texture stretched. */
    SDL_RenderSetLogicalSize(ren, 320, 240);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 320, 200);
    SDL_AudioSpec want = {0}, have;
    want.freq = audio_rate; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024;
    want.callback = audio_thunk;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (audio_dev) { audio_rate = have.freq; SDL_PauseAudioDevice(audio_dev, 0); }
    t0 = now_s();
    return 0;
}

void plat_set_isr(isr_fn f) { isr = f; }
void plat_set_key_handler(key_fn f) { keyh = f; }
void plat_set_audio(audio_fn f) { audio_cb = f; }
int plat_audio_rate(void) { return audio_rate; }
void plat_audio_lock(int lock) { if (audio_dev) { if (lock) SDL_LockAudioDevice(audio_dev); else SDL_UnlockAudioDevice(audio_dev); } }
uint64_t plat_ticks(void) { return ticks_done; }
void plat_quit(void) { quit_flag = 1; }
int plat_should_quit(void) { return quit_flag; }

static int mx, my, mb;
void plat_mouse(int *x, int *y, int *b) { *x = mx; *y = my; *b = mb; }

/* SDL scancode -> PC set-1 scancode (+ extended flag). Only keys the game reads matter. */
static int pc_scancode(SDL_Scancode s, int *ext) {
    *ext = 0;
    switch (s) {
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_1: return 0x02; case SDL_SCANCODE_2: return 0x03; case SDL_SCANCODE_3: return 0x04;
    case SDL_SCANCODE_4: return 0x05; case SDL_SCANCODE_5: return 0x06; case SDL_SCANCODE_6: return 0x07;
    case SDL_SCANCODE_7: return 0x08; case SDL_SCANCODE_8: return 0x09; case SDL_SCANCODE_9: return 0x0a;
    case SDL_SCANCODE_0: return 0x0b; case SDL_SCANCODE_MINUS: return 0x0c; case SDL_SCANCODE_EQUALS: return 0x0d;
    case SDL_SCANCODE_BACKSPACE: return 0x0e; case SDL_SCANCODE_TAB: return 0x0f;
    case SDL_SCANCODE_Q: return 0x10; case SDL_SCANCODE_W: return 0x11; case SDL_SCANCODE_E: return 0x12;
    case SDL_SCANCODE_R: return 0x13; case SDL_SCANCODE_T: return 0x14; case SDL_SCANCODE_Y: return 0x15;
    case SDL_SCANCODE_U: return 0x16; case SDL_SCANCODE_I: return 0x17; case SDL_SCANCODE_O: return 0x18;
    case SDL_SCANCODE_P: return 0x19; case SDL_SCANCODE_RETURN: return 0x1c; case SDL_SCANCODE_LCTRL: return 0x1d;
    case SDL_SCANCODE_A: return 0x1e; case SDL_SCANCODE_S: return 0x1f; case SDL_SCANCODE_D: return 0x20;
    case SDL_SCANCODE_F: return 0x21; case SDL_SCANCODE_G: return 0x22; case SDL_SCANCODE_H: return 0x23;
    case SDL_SCANCODE_J: return 0x24; case SDL_SCANCODE_K: return 0x25; case SDL_SCANCODE_L: return 0x26;
    case SDL_SCANCODE_LSHIFT: return 0x2a; case SDL_SCANCODE_Z: return 0x2c; case SDL_SCANCODE_X: return 0x2d;
    case SDL_SCANCODE_C: return 0x2e; case SDL_SCANCODE_V: return 0x2f; case SDL_SCANCODE_B: return 0x30;
    case SDL_SCANCODE_N: return 0x31; case SDL_SCANCODE_M: return 0x32; case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_LALT: return 0x38; case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_F1: return 0x3b; case SDL_SCANCODE_F2: return 0x3c; case SDL_SCANCODE_F3: return 0x3d;
    case SDL_SCANCODE_UP: *ext = 1; return 0x48; case SDL_SCANCODE_LEFT: *ext = 1; return 0x4b;
    case SDL_SCANCODE_RIGHT: *ext = 1; return 0x4d; case SDL_SCANCODE_DOWN: *ext = 1; return 0x50;
    case SDL_SCANCODE_KP_8: return 0x48; case SDL_SCANCODE_KP_4: return 0x4b;
    case SDL_SCANCODE_KP_6: return 0x4d; case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_RCTRL: *ext = 1; return 0x1d;
    default: return -1;
    }
}

static void pump(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) quit_flag = 1;
        else if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && !e.key.repeat) {
            int ext, sc = pc_scancode(e.key.keysym.scancode, &ext);
            if (sc >= 0 && keyh) keyh((uint8_t)sc, e.type == SDL_KEYDOWN, ext);
        } else if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            int wx, wy; float lx, ly;
            SDL_GetMouseState(&wx, &wy);
            SDL_RenderWindowToLogical(ren, wx, wy, &lx, &ly);
            mx = (int)(lx * 2.0f);                 /* int 33h x range 0..639 in mode 13h/X */
            my = (int)(ly * 200.0f / 240.0f);
            Uint32 st = SDL_GetMouseState(NULL, NULL);
            mb = (st & SDL_BUTTON_LMASK ? 1 : 0) | (st & SDL_BUTTON_RMASK ? 2 : 0);
        }
    }
}

static void present(void) {
    vga_scanout(frame, 320, 200);
    SDL_UpdateTexture(tex, NULL, frame, 320 * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

void plat_yield(void) {
    pump();
    double due = (now_s() - t0) * VGA_HZ;
    uint64_t target = (uint64_t)due;
    if (target > ticks_done + 8) ticks_done = target - 8;   /* after a stall don't spin forever */
    int ran = 0;
    while (ticks_done < target) {
        if (isr) isr();
        ticks_done++;
        ran = 1;
    }
    if (ran) present();
#ifdef __EMSCRIPTEN__
    emscripten_sleep(ran ? 0 : 2);
#else
    if (!ran) SDL_Delay(1);
#endif
}
