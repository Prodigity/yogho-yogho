/* Engine object manager: per-tick engine step (1000:1411), pickups, slot dispatch, timers,
 * popups, activation scans and slot init, plus the overlay link (1000:04d2).
 * Spec: teardown/50-objects-api.md §3a-3d, 30-render.md §5.  Addresses are Ghidra (seg 1000).
 * Instructions re-read from the EXE image (capstone) for 1411-1aa7 and 04d2. */
#include "game.h"
#include "objects_ext.h"
#include "log.h"

static inline int sx(uint16_t v) { return (int16_t)v; }
#define ESW(o) rw(R.es, (uint16_t)(o))
#define ESB(o) rb(R.es, (uint16_t)(o))
#define CSW(o) rw(SEG_EXE, (uint16_t)(o))

/* ---------------------------------------------------------------- overlay link (1000:04d2) */
void overlay_link(void) {
    uint16_t ovl = dgw(0x2d64);
    for (uint16_t i = 0; i < 26; i++) {                     /* 26 x {EA, off, seg} from CS:050f */
        uint16_t di = (uint16_t)(0x0b + 5 * i), si = (uint16_t)(0x050f + 4 * i);
        wb(ovl, di, 0xea);
        ww(ovl, (uint16_t)(di + 1), CSW(si));
        ww(ovl, (uint16_t)(di + 3), CSW(si + 2));
    }
    uint16_t si = rw(ovl, 2);                               /* relocation table (where, addend), 0 */
    for (;;) {
        uint16_t where = rw(ovl, si);
        if (!where) break;
        ww(ovl, where, (uint16_t)(rw(ovl, (uint16_t)(si + 2)) + ovl));
        si = (uint16_t)(si + 4);
    }
}

/* ---------------------------------------------------------------- slot init */
void slot_init_pickup(void) {                               /* 1000:18f0  BP, SI=rec, AX=x, BX=y */
    uint16_t bp = R.bp;
    pick_set(bp, P_REC, R.si);
    pick_set(bp, P_X, R.ax);
    pick_set(bp, P_Y, R.bx);
    SET_AL(R, ESB(R.si + REC_PFL));
    DB((uint16_t)(PICK_BASE + bp + P_PFL)) = (uint8_t)R.ax;
    R.ax = ESW(R.si + REC_STAMP); pick_set(bp, P_CODE, R.ax);
    R.ax = ESW(R.si + REC_SIDX);  pick_set(bp, P_SIDX, R.ax);
    R.ax = 0;
    pick_set(bp, P_VIS, 0);
}

void slot_init_big(void) {                                  /* 1000:1922  BP, ES:SI=rec, AX=x, BX=y */
    uint16_t bp = R.bp;
    slot_set(bp, S_REC, R.si);
    slot_set(bp, S_X, R.ax);
    slot_set(bp, S_Y, R.bx);
    R.ax = 0;
    static const uint8_t zero[] = { S_XF, S_YF, S_VXF, S_VX, S_VYF, S_VY, S_AXF, S_AX, S_AYF, S_AY,
                                    S_W3C, S_T3E, S_FREE40, };
    for (unsigned k = 0; k < sizeof zero; k++) slot_set(bp, zero[k], 0);   /* +0x38 latch NOT cleared */
    R.bx = ESB(R.si + REC_ANIM0);
    slot_set(bp, S_ANIMOFS, R.bx);
    SET_AL(R, ESB(R.si + REC_FRAME0)); slot_bset(bp, S_FRAME, (uint8_t)R.ax);
    SET_AL(R, ESB(R.si + REC_DELAY0)); slot_bset(bp, S_DCNT, (uint8_t)R.ax);
    R.ax = ESW(R.si + REC_HANDLER); slot_set(bp, S_CODE, R.ax);
    R.ax = ESW(R.si + REC_PARAM);   slot_set(bp, S_PARAM, R.ax);
    R.di = ESW(R.si + REC_TYPE);
    R.cx = ESW(R.di);                                       /* sprite descriptor (CX stays = it) */
    R.di = (uint16_t)(R.di + 2);
    slot_set(bp, S_SPRITE, R.cx);
    slot_set(bp, S_ANIMS, R.di);
    uint16_t di = R.di;                                     /* push di */
    R.di = (uint16_t)(R.di + R.bx);
    R.ax = 0;
    SET_AL(R, ESB(R.di));
    slot_set(bp, S_FRAMESET, R.ax);
    R.bx = ESW(R.di + 2);
    slot_set(bp, S_AFLAGS, R.bx);
    R.di = R.cx;
    R.bx = ESB(R.di + 0x12);
    slot_set(bp, S_HITS, R.bx);
    R.di = (uint16_t)(R.di + R.ax);
    SET_AL(R, ESB(R.di + 0x16)); slot_bset(bp, S_LAST, (uint8_t)R.ax);
    SET_AL(R, ESB(R.di + 0x18)); slot_bset(bp, S_DELAY, (uint8_t)R.ax);
    R.di = di;                                              /* pop di */
    uint16_t si = R.si;
    A9_restart_motion();                                    /* returned BX/CX ignored */
    R.si = si;
}

/* ---------------------------------------------------------------- activation (1805 / 19f7) */
static int find_free_big(void) {                            /* BP = 0x83c..0 step -0x44 */
    for (R.bp = 0x83c;; ) {
        R.dx = slot_w(R.bp, S_CODE);
        if (R.dx == 0) return 1;
        R.bp = (uint16_t)(R.bp - 0x44);
        if (R.bp & 0x8000) return 0;
    }
}
static int find_free_pick(void) {                           /* BP = 0x9b3..0 step -0xd */
    for (R.bp = 0x9b3;; ) {
        R.dx = pick_w(R.bp, P_CODE);
        if (R.dx == 0) return 1;
        R.bp = (uint16_t)(R.bp - 0xd);
        if (R.bp & 0x8000) return 0;
    }
}
/* 1891 / 1a4c: ES:SI = record, AX = x, BX = y.  No free slot: stays unspawned (retried). */
static void spawn_record(void) {
    R.cx = ESW(R.si + REC_FLAGS);
    if (R.cx & 0x8000) return;
    R.dx = (uint16_t)((R.dx & 0xff00) | ESB(R.si + REC_KIND));
    if ((uint8_t)R.dx == 0) {
        if (!find_free_big()) return;
        R.cx |= 0x8800;
        ww(R.es, (uint16_t)(R.si + REC_FLAGS), R.cx);
        slot_init_big();
    } else {
        if (!find_free_pick()) return;
        R.cx |= 0x8800;
        ww(R.es, (uint16_t)(R.si + REC_FLAGS), R.cx);
        slot_init_pickup();
    }
}

void activate_ring(void) {                                  /* 1000:1805 */
    uint16_t sbp = R.bp, sdi = R.di, ssi = R.si, ses = R.es;
    R.bx = dgw(0x5134);
    R.es = dgw(0x2d64);
    R.si = dgw(0x5136);
    R.di = R.si;
    R.ax = ESW(R.di);
    if (R.ax != 0 && sx(R.ax) < sx(R.bx)) {                 /* 1837 forward: skip x <= lo */
        for (;;) {
            R.ax = ESW(R.di);
            if (R.ax == 0 || sx(R.ax) > sx(R.bx)) break;
            R.di = (uint16_t)(R.di + REC_SIZE);
            R.si = R.di;
        }
    } else {                                                /* 1824 backward: include x >= lo */
        for (;;) {
            if (R.di == ESW(4)) break;
            R.di = (uint16_t)(R.di - REC_SIZE);
            if (sx(R.bx) > sx(ESW(R.di))) break;
            R.si = R.di;
        }
    }
    dgw_set(0x5136, R.si);
    for (;; R.si = (uint16_t)(R.si + REC_SIZE)) {           /* 184d */
        R.ax = ESW(R.si);
        R.bx = ESW(R.si + 2);
        if (R.ax == 0) break;
        int x = sx(R.ax), y = sx(R.bx), band;
        if (x < sx(dgw(0x512c))) band = 1;
        else if (x > sx(dgw(0x512a))) {
            if (x > sx(dgw(0x5132))) break;
            band = 1;
        } else {                                            /* middle columns: ring rows only */
            band = 0;
            if (y < sx(dgw(0x5130))) continue;
            if (!(y < sx(dgw(0x5128)))) {
                if (y < sx(dgw(0x5126))) continue;
                if (!(y < sx(dgw(0x512e)))) continue;
            }
        }
        if (band && (y < sx(dgw(0x5130)) || y > sx(dgw(0x512e)))) continue;   /* 1885 */
        spawn_record();
    }
    R.es = ses; R.si = ssi; R.di = sdi; R.bp = sbp;
}

void activate_full(void) {                                  /* 1000:19f7 */
    uint16_t sbp = R.bp, sdi = R.di, ssi = R.si, ses = R.es;
    R.es = dgw(0x2d64);
    R.si = ESW(4);
    for (;;) {                                              /* 1a04 */
        R.ax = ESW(R.si);
        if (R.ax == 0 || !(sx(R.ax) < sx(dgw(0x5134)))) break;
        R.si = (uint16_t)(R.si + REC_SIZE);
    }
    dgw_set(0x5136, R.si);
    if (R.ax != 0 && sx(R.ax) <= sx(dgw(0x5132))) {         /* 1a23 */
        for (;;) {
            R.bx = ESW(R.si + 2);                           /* 1a37 */
            if (!(sx(R.bx) < sx(dgw(0x5130))) && sx(R.bx) <= sx(dgw(0x512e)))
                spawn_record();
            R.si = (uint16_t)(R.si + REC_SIZE);             /* 1a47 / 1a7d */
            R.ax = ESW(R.si);                               /* 1a2a */
            if (R.ax == 0 || sx(R.ax) > sx(dgw(0x5132))) break;
        }
    }
    R.es = ses; R.si = ssi; R.di = sdi; R.bp = sbp;
}

/* ---------------------------------------------------------------- popups (1000:413c) */
void popup_tick(void) {
    for (uint16_t bp = 0xb4; !(bp & 0x8000); bp = (uint16_t)(bp - 0xc)) {
        R.bp = bp;
        R.si = pop_w(bp, Q_TYPE);
        if (R.si == 0) continue;
        if (dgw(0x2ca0) == 0) {
            R.bx = (uint16_t)(dgw((uint16_t)(0x276c + R.si)) + pop_w(bp, Q_X));
            R.di = (uint16_t)(dgw((uint16_t)(0x276e + R.si)) + pop_w(bp, Q_Y));
            R.si = (uint16_t)(R.si + 0x2744);
            draw_cs_world_ds();
            R.bp = bp;
        }
        uint32_t sp = ((uint32_t)pop_w(bp, Q_SP) << 16 | pop_w(bp, Q_SPF)) - 0x800;   /* sub/sbb */
        pop_set(bp, Q_SPF, (uint16_t)sp);
        pop_set(bp, Q_SP, (uint16_t)(sp >> 16));
        if (sp & 0x80000000u) { R.ax = 0; pop_set(bp, Q_TYPE, 0); continue; }
        uint32_t y = ((uint32_t)pop_w(bp, Q_Y) << 16 | pop_w(bp, Q_YF)) - sp;
        pop_set(bp, Q_YF, (uint16_t)y);
        pop_set(bp, Q_Y, (uint16_t)(y >> 16));
    }
}

/* ---------------------------------------------------------------- pickup tick (14fc..1640) */
static void pickup_tick(uint16_t bp) {
    uint16_t code = pick_w(bp, P_CODE);
    if (code == 0) return;
    uint16_t x = pick_w(bp, P_X), y = pick_w(bp, P_Y);
    if (!(pick_w(bp, P_VIS) & 0x8000)) {
        if ((uint16_t)(x - dgw(0x2cc4)) >= dgw(0x2cc8)) return;
        if ((uint16_t)(y - dgw(0x2cc6)) >= dgw(0x2cca)) return;
        pick_set(bp, P_VIS, (uint16_t)(pick_w(bp, P_VIS) | 0x8000));
        /* stamp into the background buffer, plane-0 block (RAM-type compiled sprite) */
        R.si = (uint16_t)((y & 0xf0) * 2);
        R.di = (uint16_t)((x & 0xfff0) >> 2);
        R.es = (uint16_t)(dgw((uint16_t)(R.si + 0x30f6)) + 0x1200);
        R.ax = code;
        R.bp = bp;
        ww(SEG_EXE, 0x1746, code);                          /* self-modified far ptr CS:1746 */
        farcall(CSW(0x1748), CSW(0x1746));                  /* 238e:code */
        R.ds = SEG_DG;
        return;
    }
    if ((uint16_t)(x - dgw(0x2cbc)) >= dgw(0x2cc0) || (uint16_t)(y - dgw(0x2cbe)) >= dgw(0x2cc2)) {
        uint16_t rec = pick_w(bp, P_REC);                   /* left the keep window: respawnable */
        rec_set(rec, REC_FLAGS, (uint16_t)(rec_w(rec, REC_FLAGS) & 0x77ff));
        pick_set(bp, P_CODE, 0);
        return;
    }
    if ((uint16_t)(x - dgw(0x2cc4)) >= dgw(0x2cc8) || (uint16_t)(y - dgw(0x2cc6)) >= dgw(0x2cca)) {
        pick_set(bp, P_VIS, (uint16_t)(pick_w(bp, P_VIS) & 0x7fff));   /* restamped on re-entry */
        return;
    }
    if (dgw(0x2c9c) & 0x40) return;
    uint16_t ax = (uint16_t)(x - dgw(0xee));
    if (ax & 0x8000) { ax = (uint16_t)(ax + 0x10); if (ax & 0x8000) return; }
    else if (ax > 0x10) return;
    uint16_t bx = (uint16_t)(y - dgw(0xf0));                /* 15d9 */
    if (!(bx & 0x8000) && bx > 0x1a) return;
    bx = (uint16_t)(bx + 0x10);                             /* 15e4 (also for 0..26) */
    if (bx & 0x8000) return;
    uint16_t rec = pick_w(bp, P_REC);                       /* collect: SPAWNED stays set */
    rec_set(rec, REC_FLAGS, (uint16_t)(rec_w(rec, REC_FLAGS) & 0xf7ff));
    R.bx = dgw((uint16_t)(pick_w(bp, P_SIDX) + 0x2dc6));
    if (DB((uint16_t)(PICK_BASE + bp + P_PFL)) & 1) dgw_set(0xc0, (uint16_t)(dgw(0xc0) + 1));
    dgw_set(0x2ca4, (uint16_t)((dgw(0xd6) & 1) + 2));       /* sound 2 or 3 */
    R.bp = bp;
    R.ax = x; R.dx = y;
    A20_add_score();
    R.ax = x; R.bx = y;
    bg_tile_one();                                          /* erase the stamp */
    pick_set(bp, P_CODE, 0);
}

/* ---------------------------------------------------------------- engine step (1000:1411) */
void engine_step(void) {
    Regs save = R;                                          /* PUSHA, PUSH ES, PUSH DS */
    if (dgw(0x2ca0) == 0) { bg_to_page(); R.ds = save.ds; } /* AX = page segment */
    {
        int t;
        t = sx(dgw(0x2cb4)) - 0xc0; dgw_set(0x2cbc, (uint16_t)(t >= 0 ? t : 0));   /* keep window */
        t = sx(dgw(0x2cb6)) - 0xc0; dgw_set(0x2cbe, (uint16_t)(t >= 0 ? t : 0));
        dgw_set(0x2cc0, 0x2c0);                             /* (the clamp vs [514d]/[514b] is dead code) */
        dgw_set(0x2cc2, 0x220);
        t = sx(dgw(0x2cb4)) - 0x10; dgw_set(0x2cc4, (uint16_t)(t >= 0 ? t : 0));   /* draw window */
        t = sx(dgw(0x2cb6)) - 0x10; dgw_set(0x2cc6, (uint16_t)(t >= 0 ? t : 0));
        dgw_set(0x2cc8, 0x150);
        dgw_set(0x2cca, 0xb0);
        dgw_set(0x2ccc, dgw(0x2cb4) < 0x40 ? 0 : (uint16_t)(dgw(0x2cb4) - 0x40));    /* A0 window (jae) */
        dgw_set(0x2cce, dgw(0x2cb6) < 0x40 ? 0 : (uint16_t)(dgw(0x2cb6) - 0x40));
        dgw_set(0xee, (uint16_t)(dgw(0xe8) - 8));           /* player box */
        dgw_set(0xf0, (uint16_t)(dgw(0xec) - 0x1a));
    }
    if (!(dgw(0x2c9c) & 0x200))                             /* 14f9 pickups 191 -> 0 */
        for (uint16_t bp = 0x9b3; !(bp & 0x8000); bp = (uint16_t)(bp - 0xd)) pickup_tick(bp);
    R.ds = SEG_DG;                                          /* 1643 */
    if (!(dgw(0x2c9c) & 0x800)) {                           /* 1650 active objects 31 -> 0 */
        for (uint16_t bp = 0x83c; !(bp & 0x8000); bp = (uint16_t)(bp - 0x44)) {
            R.di = slot_w(bp, S_CODE);
            if (R.di == 0) continue;
            R.es = dgw(0x2d64);
            R.ax = slot_w(bp, S_X);
            R.dx = slot_w(bp, S_Y);
            R.bp = bp;
            farcall(slot_w(bp, S_CSEG), slot_w(bp, S_CODE)); /* 166c CALLF [bp+131] */
            R.ds = SEG_DG;                                  /* pop ds, bp */
            R.bp = bp;
        }
    }
    if (!(dgw(0x2c9c) & 0x2000)) {                          /* 1680 player timers */
        if (DB(0xb4) != 0) {
            DB(0xb4) = (uint8_t)(DB(0xb4) - 1);
            if (DB(0xb4) == 0) DB(0xbd) &= 0xfe;
        }
        if (dgw(0xb7) != 0) {
            dgw_set(0xb7, (uint16_t)(dgw(0xb7) - 1));
            if (dgw(0xb7) == 0) {
                if (DB(0xbd) & 0x10) { dgw_set(0xb7, 0x12c); dgw_set(0xc2, 1); }
                else { dgw_set(0x2c9c, (uint16_t)(dgw(0x2c9c) & 0xfe7f)); DB(0xb4) = 0x50; }
            }
        }
        dgw_set(0xb9, (uint16_t)(dgw(0xb9) - 1));
        if (dgw(0xb9) & 0x8000) {
            dgw_set(0xb9, 5);
            dgw_set(0xbb, (uint16_t)(dgw(0xbb) - 1));
            if (dgw(0xbb) & 0x8000) dgw_set(0xbb, 5);
        }
        if (dgw(0x2ca0) == 0) { R.ax = dgw(0xae); player_draw(R.ax); R.ds = SEG_DG; }   /* 160a:1bcc */
    }
    popup_tick();                                           /* 16e7 */
    if (dgw(0x2ca0) == 0 && !(dgw(0x2c9c) & 0x1000)) {      /* 16ec activation bounds */
        uint16_t a = (uint16_t)(dgw(0x2cb4) - 0xa0);
        dgw_set(0x5134, a); a = (uint16_t)(a + 0x60);
        dgw_set(0x512c, a); a = (uint16_t)(a + 0x1c0);
        dgw_set(0x512a, a); a = (uint16_t)(a + 0x60);
        dgw_set(0x5132, a);
        a = (uint16_t)(dgw(0x2cb6) - 0xa0);
        dgw_set(0x5130, a); a = (uint16_t)(a + 0x60);
        dgw_set(0x5128, a); a = (uint16_t)(a + 0x120);
        dgw_set(0x5126, a); a = (uint16_t)(a + 0x60);
        dgw_set(0x512e, a);
        activate_ring();
    }
    /* 1736: if [2ca0]==0 CALLF 160a:3213 — an empty RETF, nothing to port */
    R = save;                                               /* POP DS, POP ES, POPA */
}
