/* Front end: menu state machine, title screen, quit dialog, text lists, menu palette rotation,
 * joystick calibration + reading, software mouse cursor show/hide, INT 33h glue, getkey.
 * Spec: teardown/10-flow-frontend.md §3.2, 3.3, 3.8-3.11; 20-timing-input.md §3.7-3.10.
 * Addresses are Ghidra (segment 160a unless noted); DGROUP offsets are bare hex. */
#include "game.h"
#include "flow_ext.h"
#include "adlib.h"
#include "platform.h"

#define W(o)     dgw(o)
#define SW(o, v) dgw_set((uint16_t)(o), (uint16_t)(v))

static int host_quit(void) { return plat_should_quit(); }   /* port: window closed */

/* ---- 160a:2068: menu assets, consecutive in the work area, each rounded to 16 bytes ---- */
void menu_load_assets(void) {
    uint32_t acc = 0;                                       /* [bp-2]:[bp-4] */
    static const struct { uint16_t var, dat; } t[6] = {
        {0x5167, 0x0a}, {0x5165, 0x0b}, {0x5163, 0x0c}, {0x5161, 0x4b}, {0x515f, 0x0f}, {0x515d, 0x0e}
    };
    for (int i = 0; i < 6; i++) {
        SW(t[i].var, W(0x13fc) + (uint16_t)(acc >> 4));
        uint32_t n = dat_load(t[i].dat, 0, W(t[i].var));
        if (i < 5) acc += (n + 0xf) & 0xfffffff0u;          /* the last size is not accumulated */
    }
}

/* ---- 160a:2d49: one colour-rotation range on the master palette 0x4e20 ---- */
uint16_t pal_rotate(uint16_t t, int16_t from, uint16_t to, uint16_t period) {
    if ((int16_t)--t >= 0) return t;
    t = period;
    if (from < 0) {                                         /* "down": P[k] = P[k+1], k = a..b-1; P[b] = old P[a] */
        uint16_t a = (uint16_t)-from;
        uint8_t r = DB(0x4e20 + 3 * a), g = DB(0x4e21 + 3 * a), b = DB(0x4e22 + 3 * a);
        uint16_t di = (uint16_t)(3 * a), si = (uint16_t)(di + 3), end = (uint16_t)(3 * to);
        while (end > di) DB(0x4e20 + di++) = DB(0x4e20 + si++);
        DB(0x4e20 + 3 * to) = r; DB(0x4e21 + 3 * to) = g; DB(0x4e22 + 3 * to) = b;
    } else {                                                /* "up": bytes [3a .. 3(b-1)] move up by 3 */
        uint16_t a = (uint16_t)from, s = (uint16_t)(to - 1);
        uint8_t r = DB(0x4e20 + 3 * s), g = DB(0x4e21 + 3 * s), b = DB(0x4e22 + 3 * s);
        uint16_t di = (uint16_t)(3 * s), dst = (uint16_t)(di + 3 + 0x4e20), lo = (uint16_t)(3 * a);
        while (lo <= di) { DB(dst) = DB(0x4e20 + di); dst--; di--; }   /* R of colour b leaks (quirk) */
        DB(0x4e20 + 3 * a) = r; DB(0x4e21 + 3 * a) = g; DB(0x4e22 + 3 * a) = b;
    }
    return t;
}

/* ---- 160a:2cae ---- */
void menu_palette_tick(void) {
    dac_schedule(0x4e20, 0, 0x100);
    SW(0x1429, pal_rotate(W(0x1429), -0x8b, 0xa2, 2));
    SW(0x142b, pal_rotate(W(0x142b), 0xa3, 0xba, 2));
    SW(0x142d, pal_rotate(W(0x142d), -0xbb, 0xc7, 1));
    SW(0x142f, pal_rotate(W(0x142f), 0xc8, 0xd8, 2));
    SW(0x1431, pal_rotate(W(0x1431), 0xd9, 0xe5, 1));
    SW(0x1433, pal_rotate(W(0x1433), 0xe6, 0xf8, 1));
}

/* ---- software cursor 160a:2ff2 / 3005 ---- */
void mouse_show(void) {
    if (DB(0xac) == 0xff) { cursor_draw(); SW(0x2c8e, 1); }
}
void mouse_hide(void) {
    if (DB(0xac) == 0xff) { SW(0x2c8e, 0); cursor_restore(); }
}

/* ---- INT 33h ---- */
static uint16_t drv_x, drv_y;      /* driver position (range 0..0x12b x 0..0xad set by fn 7/8) */
static int drv_buttons;

/* 1000:0cbd: user event handler (AX = event mask, CX = x, DX = y). */
static void mouse_handler(uint16_t ax, uint16_t cx, uint16_t dx) {
    if (cx >= 0x12b) cx = 0x12b;                            /* unsigned clamps */
    if (dx >= 0xad) dx = 0xad;
    SW(0x2c8a, ax);
    SW(0x2c8c, W(0x2c8c) | ax);
    if (W(0x2c8e)) { cursor_restore(); cursor_draw(); }     /* draws at the OLD 0x2c86/88: one-event lag */
    SW(0x2c86, cx);
    SW(0x2c88, dx);
}

/* 1000:0c6c: reset driver, install the handler (fn 14h, mask 0xffff), position (0,0), ranges. */
void mouse_init(void) {
    int x, y, b;
    plat_mouse(&x, &y, &b);                                 /* fn 0: the host always has a mouse */
    /* fn 14h swap: the old handler (none) is stored with seg/off swapped (2c80 = ES, 2c82 = DX) */
    SW(0x2c84, 0); SW(0x2c82, 0); SW(0x2c80, 0);
    drv_x = 0; drv_y = 0;                                   /* fn 4 (0,0) */
    drv_buttons = b;
    DB(0xac) = 0xff;
}

/* Port glue: turn the host pointer into INT 33h events.  The original reads the driver's position
 * as screen pixels (the cursor sprite is drawn at x/4, plane x&3), so the host pointer position in
 * 320x200 pixels (plat_mouse x is in the 0..639 mode-13h driver space -> /2) is the driver
 * position, confined to the ranges set by fn 7/8.  One callback per poll with every event bit
 * that occurred since the previous poll (bit0 moved, 1 left down, 2 left up, 3 right down, 4 right up). */
void mouse_poll(void) {
    int x, y, b;
    plat_mouse(&x, &y, &b);
    x /= 2;
    if (x < 0) x = 0; else if (x > 0x12b) x = 0x12b;
    if (y < 0) y = 0; else if (y > 0xad) y = 0xad;
    uint16_t mask = 0;
    if ((uint16_t)x != drv_x || (uint16_t)y != drv_y) mask |= 1;
    if ((b & 1) && !(drv_buttons & 1)) mask |= 2;
    if (!(b & 1) && (drv_buttons & 1)) mask |= 4;
    if ((b & 2) && !(drv_buttons & 2)) mask |= 8;
    if (!(b & 2) && (drv_buttons & 2)) mask |= 0x10;
    drv_x = (uint16_t)x; drv_y = (uint16_t)y; drv_buttons = b;
    if (mask) mouse_handler(mask, drv_x, drv_y);
}

/* ---- 160a:31dd ---- */
uint16_t getkey_ascii(void) {
    if (DB(0x2d82) == 0) { DB(0x130) = 0; return 0; }
    DB(0x2d82) = 0;
    if (DB(0x2d83) >= 0x3e) { DB(0x130) = 0; return 0; }   /* JAE 320b: also clears 0x130 */
    uint8_t c = DB(0x2d27 + DB(0x2d83));                    /* index 0x3d reads 0x2d64 (quirk) */
    if (c == DB(0x130)) return 0;
    DB(0x130) = c;
    return c;
}

/* ---- 160a:30e9 ---- */
void wait_anykey(void) {
    DB(0x2d82) = 0;
    while (DB(0x2d82) == 0 && !host_quit()) plat_yield();
}

/* ---- 160a:26ab: {u16 x, u16 y, u16 str} records until x == 0, menu font DAT 0e ---- */
void draw_text_list(uint16_t list) {
    for (uint16_t n = 0; ; n++) {
        uint16_t rec = (uint16_t)(list + n * 6);
        if (W(rec) == 0) break;
        uint16_t x = W(rec), y = W(rec + 2), s = W(rec + 4);
        for (uint16_t k = 0; DB(s + k) != 0; k++) {
            uint8_t g = DB(0x1477 + (DB(s + k) & 0x3f));
            if (g == 0x3d) continue;
            if (g == 0x3e) { y += 9; x = 0; continue; }
            blit_planar_vga_t((uint16_t)(g * 0x3f), W(0x515d), (int16_t)x, (int16_t)y, 7, 9);
            x += 8;
        }
    }
}

/* dialog panel on both pages (24a8 / 257d / 2642) */
static void draw_panel(void) {
    blit_planar_vga(0x4eca, W(0x5161), 6, 0x38, 0xc8, 0x80, 0);
    blit_planar_vga(0x4eca, W(0x5161), 6, 0x100, 0xc8, 0x80, 0);
}

/* ---- 160a:2642 ---- */
int quit_confirm(void) {
    draw_panel();
    draw_text_list(0x2165);
    DB(0x2d82) = 0;
    uint8_t c;
    while ((c = (uint8_t)getkey_ascii()) == 0) {            /* tight loop, no tick wait */
        if (host_quit()) return -1;
        plat_yield();
    }
    return c == 'J' ? -1 : 1;
}

/* ---- joystick (port 201h).  The port has no game port yet: the stub reads 0xFF ("nothing
 * connected": axis one-shots never drop, button bits high = not pressed), which makes the
 * original's own loops report "no joystick". ---- */
static uint8_t gameport_in(void) { return 0xff; }

uint16_t joy_absent(void) {                                 /* 1000:4a96 */
    uint16_t bx = 0;
    for (;;) {
        if (++bx == 0) return 0xffff;
        if (!(gameport_in() & 1)) return 0;
    }
}

void joy_read(void) {                                       /* 1000:4ab3 */
    uint16_t bx = 0, cx = 0;
    for (;;) {
        bx++; cx++;
        if (bx == 0xffff) break;
        uint8_t s = gameport_in();
        if (!(s & 1)) { do cx++; while (gameport_in() & 2); break; }   /* X done: finish Y */
        if (!(s & 2)) { do bx++; while (gameport_in() & 1); break; }   /* Y done: finish X */
    }
    R.ax = bx; R.dx = cx;
}

uint16_t joy_button(void) { return (gameport_in() & 0x10) ? 0 : 1; }   /* 1000:4ae5 */

static void joy_cal_center(void)  { joy_read(); SW(0x5176, R.ax); SW(0x5174, R.dx); }   /* 2a33:0001 */
static void joy_cal_topleft(void) { joy_read(); SW(0x5172, R.ax); SW(0x5170, R.dx); }   /* 2a33:001e */
static void joy_cal_botright(void){ joy_read(); SW(0x516e, R.ax); SW(0x516c, R.dx); }   /* 2a33:003b */

uint16_t joy_cal_valid(void) {                              /* 2a33:0058, signed */
    int16_t cx = dgs(0x5176), cy = dgs(0x5174);
    if (cx <= dgs(0x5172) || cx >= dgs(0x516e)) return 0;
    if (cy <= dgs(0x5170) || cy >= dgs(0x516c)) return 0;
    return 1;
}

/* (a-b)*pct/100 as compiled: IMUL (low word kept), CWD, IDIV 100 */
static int16_t joy_pct(int16_t a, int16_t b, int16_t pct) {
    int16_t p = (int16_t)(uint16_t)((int32_t)(int16_t)(a - b) * pct);
    return (int16_t)(p / 100);
}

uint16_t joy_to_dirbits(void) {                             /* 2a33:007d */
    joy_read();
    int16_t x = (int16_t)R.ax, y = (int16_t)R.dx;
    uint16_t r = 0;
    int16_t cx = dgs(0x5176), cy = dgs(0x5174);
    if ((int16_t)(cy - joy_pct(cy, dgs(0x5170), 0x1e)) > y) r += 8;     /* up    30 % */
    if ((int16_t)(cx - joy_pct(cx, dgs(0x5172), 0x3c)) > x) r += 2;     /* left  60 % */
    if ((int16_t)(cx + joy_pct(dgs(0x516e), cx, 0x3c)) < x) r += 1;     /* right 60 % */
    if ((int16_t)(cy + joy_pct(dgs(0x516c), cy, 0x50)) < y) r += 4;     /* down  80 % */
    if (joy_button()) r += 0x80;
    return r;
}

/* ---- 160a:257d ---- */
static void joy_cal_step(uint16_t step, uint16_t img, uint16_t list) {
    do { DB(0x2d82) = 0; wait_ticks(10); } while (DB(0x2d82) && !host_quit());   /* 10 quiet ticks */
    draw_panel();
    if (img) blit_planar_vga(img, W(0x5161), 0x0c, 0x3d, 0x27, 0x4d, 0);
    if (list) draw_text_list(list);
    DB(0x2d82) = 0;
    do {
        wait_tick();
        if (step == 0) joy_cal_center();
        else if (step == 1) joy_cal_topleft();
        else if (step == 2) joy_cal_botright();
    } while (DB(0x2d82) == 0 && !host_quit());
    wait_tick();
}

/* ---- 160a:24a8 ---- */
void joystick_calibration(void) {
    draw_panel();
    if ((uint8_t)joy_absent() != 0) {                       /* OR AL,AL */
        draw_text_list(0x20ab);                             /* IK KAN GEEN JOYSTICK VINDEN SORRY */
        wait_anykey();
    } else {
        draw_text_list(0x2099);
        wait_tick();
        wait_anykey();
        joy_cal_step(0, 0xb2ca, 0x20ed);
        joy_cal_step(1, 0xbe85, 0x210b);
        joy_cal_step(2, 0xca40, 0x2129);
        if (joy_cal_valid()) { DB(0x12c) = 0xff; joy_cal_step(3, 0, 0x2147); }
        else                 { DB(0x12c) = 0;    joy_cal_step(3, 0, 0x20c9); }
    }
    fade_out(8);
}

/* ---- title screen 160a:2192 ---- */
static void draw_music_icon(void) {                         /* 160a:2209 / 235e / 2425 */
    blit_planar_vga_t((W(0x2ca6) & 0x80) ? 0x4a25 : 0x4580, W(0x5161), 0x24, 1, 0x1d, 0x29);
    blit_planar_vga_t((W(0x2ca6) & 0x80) ? 0x4a25 : 0x4580, W(0x5161), 0x24, 0xc9, 0x1d, 0x29);
}
static void toggle_music(void) {                            /* 160a:2333 / 23fa */
    SW(0x2ca6, W(0x2ca6) ^ 0x80);
    if (W(0x2ca6) != 0) MusicStop();
    else MusicPlay(W(0x2d7a), W(0x2d78), 0);                /* restarts from the top */
    mouse_hide();
    draw_music_icon();
    mouse_show();
}

int title_screen(void) {
    int16_t result = -1, si = -1;                           /* [bp-2], SI */
    int16_t timeout = 0x41a;                                /* [bp-4] */
    uint16_t latch = 0;                                     /* DI */
    if (W(0xca) | W(0xcc)) return 2;                        /* after a game: straight to the hi-scores */
    vga_reglist(0x34e5, SEG_DG);
    set_palette(0, W(0x5167), 0, 0x100);
    blit_linear_vga(0, W(0x5165), 0, 0, 0x140, 0xc8);
    blit_linear_vga(0, W(0x5165), 0, 0xc8, 0x140, 0xc8);
    draw_music_icon();
    fade_in(8);
    mouse_show();
    while (result < 0) {
        wait_tick();
        menu_palette_tick();
        if (W(0x2c8a) & 2) {                                /* last mouse event = left press */
            uint16_t mx = W(0x2c86), my = W(0x2c88);        /* strict unsigned compares */
            if (mx > 0 && mx < 0x20 && my > 0 && my < 0x14) si = 3;          /* quit */
            if (mx > 0xf1 && mx < 0x12f && my > 0x75 && my < 0x8e) si = 0;   /* start */
            if (mx > 0xe1 && mx < 0x13c && my > 0x8d && my < 0xa7) si = 1;   /* intro */
            if (mx > 0xee && mx < 0x12f && my > 0xa7 && my < 0xc8) si = 4;   /* joystick */
            if (!latch && mx > 0x23 && mx < 0x41 && my > 0 && my < 0x2a) {   /* music icon */
                latch = 1;
                toggle_music();
            }
        } else latch = 0;
        if (--timeout < 0) si = 2;                          /* attract: hi-score table */
        if (DB(0x2d15)) si = 3;                             /* X */
        if (DB(0x2ce9)) si = 3;                             /* Esc */
        if (DB(0x2d07)) si = 0;                             /* S */
        if (DB(0x2cff)) si = 1;                             /* I */
        if (DB(0x2d0c)) si = 4;                             /* J */
        if (DB(0x2d0a)) { DB(0x2d0a) = 0; toggle_music(); } /* G */
        if (si >= 0 && !(W(0x2c8a) & 2)) {
            result = si;
            if (si < 3) fade_out(8);
        }
        if (result < 0 && host_quit()) { mouse_hide(); return -2; }   /* port */
    }
    mouse_hide();
    return result;
}

/* ---- 160a:1fad ---- */
int menu(void) {
    int16_t si = 1;
    SW(0x2cd6, 0); SW(0x2cb4, 0); SW(0x2ca4, 0);
    menu_load_assets();
    SW(0x2d7a, W(0x1402) + 0x6d0);
    SW(0x2d78, 0);
    dat_load(0x45, W(0x2d78), W(0x2d7a));                   /* title music DAT 69 */
    MusicPlay(W(0x2d7a), W(0x2d78), 0);
    SW(0x2ca6, W(0x2ca6) & 0xfffe);                         /* allow music ticks */
    while (si > 0) {
        int r = title_screen();
        switch (r) {                                        /* jump table CS:205e */
        case 0: si = 0; break;                              /* 2011 start game */
        case 1:                                             /* 2015 intro */
            MusicStop();
            intro();
            menu_load_assets();                             /* the intro overwrote the work area */
            MusicPlay(W(0x2d7a), W(0x2d78), 0);
            break;
        case 2: si = (int16_t)highscore_screen(); break;    /* 2038 */
        case 3: si = (int16_t)quit_confirm(); break;        /* 2041 */
        case 4: joystick_calibration(); break;              /* 204a */
        case -2: si = -1; break;                            /* port: window closed */
        default: break;                                     /* > 4 (unsigned JA) */
        }
    }
    SW(0x2ca6, W(0x2ca6) | 1);
    return si;
}
