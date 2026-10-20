/* AdLib driver, port of Ghidra segment 2f6e (runtime 2792).  Spec: teardown/70-sound.md §2–3.
 * Driver variables are kept at their original offsets in segment 2792 (initialised from the EXE
 * image), so the port's driver state can be diffed against a DOSBox dump of that segment. */
#include "adlib.h"
#include "mem.h"

#define S SEG_ADLIB
/* --- driver variables (offsets in seg 2f6e) --- */
#define CUR_INS     0x0bf8   /* u8[9]  */
#define MEM_EFX     0x0c01   /* u8[9]  shared E/F/J info memory */
#define MEM_VIB     0x0c0a   /* u8[9]  H info memory */
#define ARP_PHASE   0x0c13
#define OPOFF       0x0c14   /* u8[32] */
#define OPLCH       0x0c34   /* u8[32] */
#define SEMI        0x0c54   /* u8[32] */
#define BLOCK       0x0c74   /* u8[32] */
#define BASEF       0x0c94   /* u16[32] */
#define CURF        0x0cd4   /* u16[32] */
#define VIBPOS      0x0d14   /* u8[32] */
#define OPTAB       0x0d34   /* u8[9]  */
#define SONG_OFF    0x0d3d
#define SONG_SEG    0x0d3f
#define SPEED       0x0d41
#define COUNT       0x0d42
#define INSTBL      0x0d43
#define PATTBL      0x0d45
#define ROWOFF      0x0d47
#define ORDER       0x0d49
#define ROW         0x0d4b
#define PLAYING     0x0d4c
#define ONCE        0x0d4d
#define DETECTED    0x0d4e
#define SFX_OFF     0x0d4f
#define SFX_SEG     0x0d51
#define SFX_SPEED   0x0d53
#define SFX_COUNT   0x0d54
#define SFX_INSTBL  0x0d55
#define SFX_PATTBL  0x0d57
#define SFX_ROWOFF  0x0d59
#define SFX_PAT     0x0d5b
#define SFX_ROW     0x0d5d
#define SFX_ARP     0x0d5e
#define SFX_ACTIVE  0x0d9f
#define ROWPTR      0x0da1
#define SFX_ROWPTR  0x0da3
#define INS_OCT     0x0da5   /* s8[64]  */
#define INS_FTAB    0x0de5   /* u16[64] pointers into the F-number cache */
#define CAR_TL      0x0e65   /* u8[32]  */
#define SINE        0x0e85   /* u8[32]  */
#define FCACHE      0x0ea5   /* slots of 0x1a: u16 key, u16[12] */
#define BASE_FNUM   0x0ea7   /* u16[12] (slot 0 table) */
#define ROWTAB      0x0a27
#define TICKTAB     0x0a5b
#define SFXROWTAB   0x0a8f
#define SFXTICKTAB  0x0ac3
#define SLIDE_TBL   0x0af7
#define SHADOW      0x127f

/* Handler entry points as stored in the tables (seg 2f6e offsets). */
enum { H_A = 0x0422, H_A_SFX = 0x042a, H_F = 0x0432, H_E = 0x048c, H_H = 0x04ee, H_B = 0x056e,
       H_B_SFX = 0x0580, H_C = 0x0592, H_C_SFX = 0x0599, H_J0 = 0x05a0, H_J0_SFX = 0x05a7,
       H_J_SFX = 0x05ae, H_J = 0x05cd, H_NONE = 0x0635 };

static opl_sink_fn sink;
void adlib_set_sink(opl_sink_fn f) { sink = f; }

static uint8_t  b(uint16_t o) { return rb(S, o); }
static void     sb(uint16_t o, uint8_t v) { wb(S, o, v); }
static uint16_t w(uint16_t o) { return rw(S, o); }
static void     sw(uint16_t o, uint16_t v) { ww(S, o, v); }

/* Song bytes: the driver addresses the module through ES/DS = song segment. */
static uint16_t song_seg(void) { return w(SONG_SEG); }
static uint16_t sfx_seg(void)  { return w(SFX_SEG); }

static void opl_write(uint8_t reg, uint8_t val) {          /* 2f6e:0041 */
    sb((uint16_t)(SHADOW + reg), val);
    if (sink) sink(reg, val);
}
static void wc(uint8_t reg, uint8_t val) {                  /* 2f6e:0000 opl_write_cached */
    if (b((uint16_t)(SHADOW + reg)) == val) return;
    opl_write(reg, val);
}

void adlib_detect(void) {                                   /* 2f6e:007b — raw writes */
    opl_write(1, 0); opl_write(4, 0x60); opl_write(4, 0x80);
    opl_write(2, 0xff); opl_write(4, 0x21);
    opl_write(4, 0x60); opl_write(4, 0x80);
    sb(DETECTED, 0xff);   /* the emulated OPL2 always answers like a real AdLib */
}

static void silence(void) {                                 /* 2f6e:0790 */
    for (int c = 0; c < 9; c++) wc((uint8_t)(0xb0 + c), 0);
    static const uint8_t base[3] = {0x40, 0x48, 0x50};
    for (int k = 0; k < 3; k++) for (int r = 0; r < 6; r++) wc((uint8_t)(base[k] + r), 0xff);
    sb(PLAYING, 0);
}

static void song_init(uint16_t ax, uint16_t seg, uint16_t off) {   /* 2f6e:00f8 */
    sw(ORDER, 0); sb(ONCE, 0);
    if (ax) { sb(ONCE, 1); sw(ORDER, (uint16_t)(ax - 1)); }
    silence();
    sw(SONG_OFF, off); sw(SONG_SEG, seg);
    sb(SPEED, rb(seg, (uint16_t)(off + 0x31)));
    sb(COUNT, 1);
    for (int i = 0; i < 9; i++) sb((uint16_t)(CUR_INS + i), 0);
    for (int i = 0; i < 32; i++) {
        int8_t t = (int8_t)rb(seg, (uint16_t)(off + 0x40 + i));
        if (t >= 0x10 && t <= 0x1f) {
            sb((uint16_t)(OPLCH + i), (uint8_t)(t - 0x10));
            sb((uint16_t)(OPOFF + i), b((uint16_t)(OPTAB + t - 0x10)));
        } else sb((uint16_t)(OPOFF + i), 0xff);
    }
    uint16_t ordnum = rw(seg, (uint16_t)(off + 0x20)), insnum = rw(seg, (uint16_t)(off + 0x22));
    uint16_t patnum = rw(seg, (uint16_t)(off + 0x24));
    uint16_t instbl = (uint16_t)(off + 0x60 + ordnum), pattbl = (uint16_t)(instbl + 2 * insnum);
    sw(INSTBL, instbl); sw(PATTBL, pattbl);
    if (rb(seg, off) != 0xff) {                             /* relocate para-pointers once */
        wb(seg, off, 0xff);
        for (unsigned k = 0; k < (unsigned)(uint16_t)(insnum + patnum); k++) {
            uint16_t p = (uint16_t)(instbl + 2 * k);
            ww(seg, p, (uint16_t)((rw(seg, p) << 4) + off));
        }
    }
    unsigned nslots = 1;
    for (unsigned i = 0; i < (uint8_t)insnum; i++) {        /* C2SPD -> F-number cache */
        uint16_t ins = rw(seg, (uint16_t)(instbl + 2 * i));
        uint16_t c = rw(seg, (uint16_t)(ins + 0x20));
        int8_t adj = 0;
        if (c == 0) c = 0x20ab;   /* original would hang; never happens with the shipped data */
        for (;;) {
            if (c < 0x20ab) { adj -= 4; c <<= 1; }
            else if (c >= 0x4156) { adj += 4; c >>= 1; }
            else break;
        }
        sb((uint16_t)(INS_OCT + i), (uint8_t)adj);
        ww(seg, (uint16_t)(ins + 0x20), c);
        unsigned slot;
        for (slot = 0; slot < nslots; slot++) if (w((uint16_t)(FCACHE + slot * 0x1a)) == c) break;
        if (slot == nslots) {
            sw((uint16_t)(FCACHE + slot * 0x1a), c);
            for (int k = 0; k < 12; k++)
                sw((uint16_t)(FCACHE + slot * 0x1a + 2 + 2 * k),
                   (uint16_t)((uint32_t)w((uint16_t)(BASE_FNUM + 2 * k)) * c / 0x20ab));
            nslots++;
        }
        sw((uint16_t)(INS_FTAB + 2 * i), (uint16_t)(FCACHE + slot * 0x1a + 2));
    }
    sw(ROWOFF, 0); sb(ROW, 0);
    sb(OPOFF + 8, 0x12); sb(OPLCH + 8, 8);
    opl_write(0x01, 0x20); opl_write(0x08, 0); opl_write(0xbd, 0);
    for (int r = 0x20; r <= 0xf5; r++) opl_write((uint8_t)r, 0);
    sb(PLAYING, 1);
}

void adlib_music_start(uint16_t seg, uint16_t off, uint16_t arg) {
    if (b(DETECTED)) song_init(arg, seg, off);
}
void adlib_music_stop(void) { if (b(DETECTED)) silence(); }

void adlib_sfx_load(uint16_t seg, uint16_t off) {           /* 2f6e:07dc */
    if (!b(DETECTED)) return;
    sw(SFX_OFF, off); sw(SFX_SEG, seg);
    song_init(0, seg, off);
    silence();
    uint16_t ordnum = rw(seg, (uint16_t)(off + 0x20)), insnum = rw(seg, (uint16_t)(off + 0x22));
    sw(SFX_INSTBL, (uint16_t)(off + 0x60 + ordnum));
    sw(SFX_PATTBL, (uint16_t)(off + 0x60 + ordnum + 2 * insnum));
    silence();
    wc(0x01, 0); wc(0x08, 0); wc(0xdb, 0);
    sb(OPOFF + 8, 0x12); sb(OPLCH + 8, 8); sb(CUR_INS + 8, 0);
}

/* --- volume (038d / 03b2) --- */
static void set_vol_music(uint8_t reg, uint8_t v) {
    wc(reg, v);
    if ((v & 0x3f) == 0x3f) wc((uint8_t)(reg - 3), v);
}
static void set_vol_sfx(uint8_t reg, uint8_t v) {
    wc(reg, v);
    if ((v & 0x3f) == 0x3f) { wc((uint8_t)(reg - 3), v); wc(0xb8, 0); }
}

static void load_instrument(uint8_t op, uint8_t ch, uint16_t seg, uint16_t ins) {   /* 06f1 */
    wc((uint8_t)(0x20 + op), rb(seg, ins + 0)); wc((uint8_t)(0x23 + op), rb(seg, ins + 1));
    wc((uint8_t)(0x40 + op), rb(seg, ins + 2));
    wc((uint8_t)(0x60 + op), rb(seg, ins + 4)); wc((uint8_t)(0x63 + op), rb(seg, ins + 5));
    wc((uint8_t)(0x80 + op), rb(seg, ins + 6)); wc((uint8_t)(0x83 + op), rb(seg, ins + 7));
    wc((uint8_t)(0xe0 + op), rb(seg, ins + 8)); wc((uint8_t)(0xe3 + op), rb(seg, ins + 9));
    wc((uint8_t)(0xc0 + b((uint16_t)(OPLCH + ch))), rb(seg, ins + 10));
}

/* note-on: music (0636) and SFX (097c) differ in instrument table, F-table and octave adjust. */
static void note_on(uint8_t what, uint8_t op, uint8_t ch, uint16_t seg, uint16_t si, int sfx) {
    (void)what;
    uint8_t c = b((uint16_t)(OPLCH + ch));
    uint8_t old = b((uint16_t)(CUR_INS + c));
    uint8_t n = rb(seg, (uint16_t)(si + 1));
    sb((uint16_t)(CUR_INS + c), n);
    uint16_t instbl = w(sfx ? SFX_INSTBL : INSTBL);
    uint16_t ins = (uint16_t)(rw(seg, (uint16_t)(instbl + (uint8_t)((n - 1) * 2))) + 0x10);
    if (n != old) load_instrument(op, ch, seg, ins);
    sb((uint16_t)(CAR_TL + ch), rb(seg, (uint16_t)(ins + 3)));
    if (!(what & 0x40)) {
        wc((uint8_t)(op + 0x43), rb(seg, (uint16_t)(ins + 3)));
        wc((uint8_t)(op + 0x40), rb(seg, (uint16_t)(ins + 2)));
    }
    int8_t adj = sfx ? 0 : (int8_t)b((uint16_t)(INS_OCT + (uint8_t)(n - 1)));
    uint16_t tab = sfx ? w(INS_FTAB) : w((uint16_t)(INS_FTAB + 2 * (uint8_t)(n - 1)));
    uint8_t note = rb(seg, si);
    sb((uint16_t)(SEMI + ch), note & 15);
    uint16_t f = w((uint16_t)(tab + 2 * (note & 15)));
    sw((uint16_t)(BASEF + 2 * ch), f); sw((uint16_t)(CURF + 2 * ch), f);
    sb((uint16_t)(BLOCK + ch), (uint8_t)((note & 0x70) >> 2));
    uint8_t b0 = (uint8_t)((f >> 8) | (uint8_t)(((uint8_t)(b((uint16_t)(BLOCK + ch)) + adj)) | 0x20));
    wc((uint8_t)(0xb0 + c), 0);
    wc((uint8_t)(0xb0 + c), b0);
    wc((uint8_t)(0xa0 + c), (uint8_t)f);
}

/* --- tick handlers (indexed by OPL channel c) --- */
static void fx_slide(uint8_t c, uint8_t info, int up) {    /* F 0432 / E 048c */
    uint8_t a = info ? info : b((uint16_t)(MEM_EFX + c));
    sb((uint16_t)(MEM_EFX + c), a); sb((uint16_t)(MEM_VIB + c), 0);
    uint8_t d = b((uint16_t)(SLIDE_TBL + a));
    uint16_t f = w((uint16_t)(CURF + 2 * c));
    if (up) f = (uint16_t)(f + d);
    else { f = (uint16_t)(f - d); if ((int16_t)f < 10) f = 0; }
    sw((uint16_t)(BASEF + 2 * c), f); sw((uint16_t)(CURF + 2 * c), f);
    wc((uint8_t)(0xb0 + c), (uint8_t)((f >> 8) | b((uint16_t)(BLOCK + c)) | 0x20));
    wc((uint8_t)(0xa0 + c), (uint8_t)f);
}
static void fx_vibrato(uint8_t c, uint8_t info) {          /* H 04ee */
    uint8_t a = info ? info : b((uint16_t)(MEM_VIB + c));
    sb((uint16_t)(MEM_VIB + c), a);
    uint8_t pos = b((uint16_t)(VIBPOS + c));
    uint8_t s = b((uint16_t)(SINE + ((pos >> 2) & 31)));
    uint16_t delta = (uint16_t)(((a & 15) * 2 * s) >> 7);
    uint16_t f = (uint16_t)(w((uint16_t)(BASEF + 2 * c)) + ((int8_t)pos < 0 ? -delta : delta));
    sb((uint16_t)(VIBPOS + c), (uint8_t)(pos + ((a >> 2) & 0x3c)));
    sw((uint16_t)(CURF + 2 * c), f);
    wc((uint8_t)(0xb0 + c), (uint8_t)((f >> 8) | b((uint16_t)(BLOCK + c)) | 0x20));
    wc((uint8_t)(0xa0 + c), (uint8_t)f);
}
static void fx_arp(uint8_t c, uint8_t info, uint8_t phase) {   /* J 05cd / 05ae */
    uint8_t a = info ? info : b((uint16_t)(MEM_EFX + c));
    sb((uint16_t)(MEM_EFX + c), a);
    uint8_t k = phase == 1 ? (uint8_t)(a >> 4) : phase == 2 ? (uint8_t)(a & 15) : 0;
    uint8_t s = (uint8_t)(b((uint16_t)(SEMI + c)) + k), bl = b((uint16_t)(BLOCK + c));
    if (s >= 12) { bl = (uint8_t)(bl + 4); s = (uint8_t)(s - 12); }
    uint16_t f = w((uint16_t)(BASE_FNUM + 2 * s));
    wc((uint8_t)(0xa0 + c), (uint8_t)f);
    wc((uint8_t)(0xb0 + c), (uint8_t)((f >> 8) | bl | 0x20));
}

static void run_tick_handler(uint16_t addr, uint8_t c, uint8_t info) {
    switch (addr) {
    case H_F: fx_slide(c, info, 1); break;
    case H_E: fx_slide(c, info, 0); break;
    case H_H: fx_vibrato(c, info); break;
    case H_J: fx_arp(c, info, b(ARP_PHASE)); break;
    case H_J_SFX: fx_arp(c, info, b(SFX_ARP)); break;
    default: break;
    }
}
static void run_row_handler(uint16_t addr, uint8_t info) {
    switch (addr) {
    case H_A: sb(SPEED, info); break;
    case H_A_SFX: sb(SFX_SPEED, info); break;
    case H_B: sw(ORDER, (uint8_t)(info - 1)); sb(ROW, 0x3f); break;
    case H_B_SFX: sb(SFX_ROW, 0x3f); break;
    case H_C: sb(ROW, 0x3f); break;
    case H_C_SFX: sb(SFX_ROW, 0x3f); break;
    case H_J0: sb(ARP_PHASE, 0); break;
    case H_J0_SFX: sb(SFX_ARP, 0); break;
    default: break;
    }
}

static void music_tick_effects(void) {                      /* 03e2 */
    uint16_t seg = song_seg(), si = w(ROWPTR);
    uint8_t what;
    while ((what = rb(seg, si)) != 0) {
        si++;
        if (what & 0x20) si += 2;
        if (what & 0x40) si++;
        if (what & 0x80) {
            uint8_t cmd = rb(seg, si), info = rb(seg, (uint16_t)(si + 1));
            run_tick_handler(w((uint16_t)(TICKTAB + (uint8_t)(cmd * 2))), b((uint16_t)(OPLCH + (what & 31))), info);
            si += 2;
        }
    }
}

static void sfx_tick_effects(void) {                        /* 0939 */
    uint16_t seg = sfx_seg(), si = w(SFX_ROWPTR);
    uint8_t what;
    while ((what = rb(seg, si)) != 0) {
        si++;
        what |= 8;
        if (what & 0x20) si += 2;
        if (what & 0x40) si++;
        if (what & 0x80) {
            uint8_t cmd = rb(seg, si), info = rb(seg, (uint16_t)(si + 1));
            run_tick_handler(w((uint16_t)(SFXTICKTAB + (uint8_t)(cmd * 2))), b((uint16_t)(OPLCH + (what & 31))), info);
            si += 2;
        }
    }
}

static void sfx_start(uint16_t n) {                         /* 086d */
    sw(SFX_PAT, n); sw(SFX_ACTIVE, n); sb(SFX_ROW, 0); sw(SFX_ROWOFF, 0);
    sb(SFX_SPEED, 6); sb(SFX_COUNT, 1);
}

static void sfx_tick(void) {                                /* 088c */
    sb(SFX_ARP, (uint8_t)(b(SFX_ARP) + 1));
    if (b(SFX_ARP) == 3) sb(SFX_ARP, 0);
    sb(SFX_COUNT, (uint8_t)(b(SFX_COUNT) - 1));
    if (b(SFX_COUNT) != 0) { sfx_tick_effects(); return; }
    uint16_t seg = sfx_seg();
    uint16_t si = (uint16_t)(rw(seg, (uint16_t)(w(SFX_PATTBL) + 2 * (uint16_t)(w(SFX_PAT) - 1))) + 2 + w(SFX_ROWOFF));
    uint16_t rowptr = si;
    sw(SFX_ROWPTR, si);
    uint8_t what = rb(seg, si++);
    if (what) {
        what = (uint8_t)(what + 8);
        uint8_t ch = what & 31, op = b((uint16_t)(OPOFF + 8));
        if (what & 0x20) { note_on(what, op, ch, seg, si, 1); si += 2; }
        if (what & 0x40) {
            set_vol_sfx((uint8_t)(op + 0x43), (uint8_t)((b((uint16_t)(CAR_TL + ch)) | 0x3f) - rb(seg, si)));
            si++;
        }
        if (what & 0x80) {
            run_row_handler(w((uint16_t)(SFXROWTAB + (uint8_t)(rb(seg, si) * 2))), rb(seg, (uint16_t)(si + 1)));
            si += 2;
        }
        si++;
    }
    sb(SFX_COUNT, b(SFX_SPEED));
    sw(SFX_ROWOFF, (uint16_t)(w(SFX_ROWOFF) + (uint16_t)(si - rowptr)));
    sb(SFX_ROW, (uint8_t)(b(SFX_ROW) + 1));
    if (b(SFX_ROW) == 0x40) sw(SFX_ACTIVE, 0);
}

void adlib_tick(uint16_t ax) {                              /* 2f6e:0292 */
    if (!b(DETECTED)) return;
    if (ax) sfx_start(ax);
    if (w(SFX_ACTIVE)) sfx_tick();
    if (!b(PLAYING)) return;
    sb(ARP_PHASE, (uint8_t)(b(ARP_PHASE) + 1));
    if (b(ARP_PHASE) == 3) sb(ARP_PHASE, 0);
    sb(COUNT, (uint8_t)(b(COUNT) - 1));
    if (b(COUNT) != 0) { music_tick_effects(); return; }
    uint16_t seg = song_seg();
    uint16_t ordoff = (uint16_t)(w(SONG_OFF) + 0x60);
    uint8_t p = rb(seg, (uint16_t)(ordoff + (w(ORDER) & 0xff)));
    if (p == 0xff) { sw(ORDER, 0); p = rb(seg, ordoff); }
    uint16_t si = (uint16_t)(rw(seg, (uint16_t)(w(PATTBL) + 2 * p)) + 2 + w(ROWOFF));
    uint16_t rowptr = si;
    sw(ROWPTR, si);
    uint8_t what;
    while ((what = rb(seg, si++)) != 0) {
        uint8_t ch = what & 31, op = b((uint16_t)(OPOFF + ch));
        if (what & 0x20) { note_on(what, op, ch, seg, si, 0); si += 2; }
        if (what & 0x40) {
            set_vol_music((uint8_t)(op + 0x43), (uint8_t)((b((uint16_t)(CAR_TL + ch)) | 0x3f) - rb(seg, si)));
            si++;
        }
        if (what & 0x80) {
            run_row_handler(w((uint16_t)(ROWTAB + (uint8_t)(rb(seg, si) * 2))), rb(seg, (uint16_t)(si + 1)));
            si += 2;
        }
    }
    sb(COUNT, b(SPEED));
    sw(ROWOFF, (uint16_t)(w(ROWOFF) + (uint16_t)(si - rowptr)));
    sb(ROW, (uint8_t)(b(ROW) + 1));
    if (b(ROW) == 0x40) {
        sb(ROW, 0); sw(ROWOFF, 0); sw(ORDER, (uint16_t)(w(ORDER) + 1));
        if (b(ONCE) == 1) sb(PLAYING, 0);
    }
}

/* ---- engine wrappers (seg 1000) and ISR hook ---- */
void MusicPlay(uint16_t seg, uint16_t off, uint16_t arg) {   /* 1000:47bb */
    dgw_set(0x2d7c, 0);
    adlib_music_start(seg, off, arg);
    dgw_set(0x2d7c, 0xffff);
}
void SfxLoad(uint16_t seg, uint16_t off) { adlib_sfx_load(seg, off); }   /* 1000:47df */
void AdlibDetect(void) { adlib_detect(); }                              /* 1000:47f4 */
void MusicStop(void) { dgw_set(0x2d7c, 0); adlib_music_stop(); }        /* 1000:4804 */

void isr_music_hook(void) {                                 /* 1000:090c */
    if (dgw(0x2ca6) != 0) return;
    int16_t budget = dgs(0x2d7c);
    if (budget >= 0) {
        if (budget == 0) return;
        dgw_set(0x2d7c, (uint16_t)(budget - 1));
        if (budget - 1 == 0) return;
    }
    uint16_t ax = dgw(0x2ca4);
    dgw_set(0x2ca4, 0);
    adlib_tick(ax);
}
