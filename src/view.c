// Stilwell Farms -- tile cache + streaming renderer (banked, together with the metatile tables)
//
// Every metatile quad is either a static tile (bank 1, always resident) or a cache tile id.
// Cache tiles are uploaded into VRAM bank 0 slots on demand and reference-counted by the
// cells currently drawn; a slot is reused once nothing on screen references it.
// The drawn region is a 12x11-cell window that follows the camera through the 32x32 BG ring.
// The metatile tables live in this file's bank (art_mt.h), so drawing a cell reads them directly
// instead of making two banked mt_get calls per cell; this also keeps ~2KB of code out of bank 0.
// Only 'near' metatiles (ids < MT_NEAR) are here; 'far' ones (rare or off-farm art) are 17-byte records
// in other banks, copied into far_rec (WRAM) by far_get before use, so the art can keep growing.
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "game.h"
#include "art_mt.h"
BANKREF(view_reset)

void mt_get(mt_t mt, uint16_t *q, uint8_t *pf) BANKED {   // for callers outside this bank
    const uint8_t *s, *p;
    uint8_t *d = (uint8_t *)q, n;
    if (mt < MT_NEAR) { s = (const uint8_t *)&mt_q[mt << 2]; p = &mt_attr[mt << 2]; }
    else { far_get(mt - MT_NEAR); s = far_rec; p = far_rec + 12; }
    for (n = 8; n; n--) *d++ = *s++;
    for (n = 4; n; n--) *pf++ = *p++ & ~0x08;   // palette + flips (mt_attr minus the bank-1 bit)
}

#define NSLOT 255
#define NOSLOT 0xFF
static uint16_t slot_tile[NSLOT];        // slot -> cache id (0xFFFF none)
static uint16_t slot_ref[NSLOT];
static uint8_t slot_hand;
static uint8_t slot_free;                // slots nothing references (reusable): 0 = the cache is full
// cache id -> slot lookup: a small chained hash (RAM is tight; a flat table would cost 1 byte per art tile)
static uint8_t bucket[256];              // id & 255 -> first slot in chain
static uint8_t chain[NSLOT];             // slot -> next slot in the same bucket
static uint8_t find_slot(uint16_t id) {
    uint8_t s = bucket[(uint8_t)id];
    while (s != NOSLOT) { if (slot_tile[s] == id) return s; s = chain[s]; }
    return NOSLOT;
}
static void unlink_slot(uint8_t s) {
    uint8_t *pp = &bucket[(uint8_t)slot_tile[s]];
    while (*pp != NOSLOT) { if (*pp == s) { *pp = chain[s]; return; } pp = &chain[*pp]; }
}
#define slot_of_get(id) find_slot(id)
static mt_t drawn[256];                  // ring (cy&15)<<4 | (cx&15) -> mt drawn there (0xFFFF none; bit 15: all quads static)
static uint8_t vx0, vy0;                 // drawn region origin (cells)
// 11x10 cells cover the screen at any scroll; one spare column and row sit on the side the camera last
// moved toward, so the line that streams in is off screen for the next 16px and is drawn a few cells a
// frame (view_update) instead of all at once. Columns keep a spare on BOTH sides: the camera follows the
// player 72px from the left, so its x scroll is never cell-aligned, and with a spare on one side only,
// turning back (left <-> right) had to draw a whole column in one frame (a dropped frame on dense
// streets). Rows are aligned whenever the player stands still, and turns happen at rest.
#define VW 13
#define VH 11
#define BUDGET 4                         // spare-line cells drawn per frame
// lines still to draw: a column (x, rows r0..r1-1) and a row (y, columns c0..c1-1), in map cells; both can be
// pending at once (turning a corner), and are clipped to the window whenever it moves
static uint8_t pc_on, pc_x, pc_r0, pc_r1, pr_on, pr_y, pr_c0, pr_c1;
static uint8_t up_spare;                 // the spare row is on top
static uint16_t last_scx, last_scy;
uint8_t view_w = 32, view_h = 32;

// ---------------------------------------------------------------- cache
static void cache_clear(void) {
    uint8_t s;
    memset(bucket, NOSLOT, sizeof(bucket));
    for (s = 0; s < NSLOT; s++) { slot_tile[s] = 0xFFFF; slot_ref[s] = 0; chain[s] = NOSLOT; }
    memset(drawn, 0xFF, sizeof(drawn));
    slot_hand = 0;
    slot_free = NSLOT;
}

static uint8_t cache_get(uint16_t id) {
    uint8_t s = find_slot(id), n;
    if (s != NOSLOT) { if (!slot_ref[s]++) slot_free--; return s; }
    if (!slot_free) return NOSLOT;   // full: fail at once (scanning all 255 slots per quad cost ~25% of a frame a cell)
    for (n = 0; n < NSLOT; n++) {
        s = slot_hand;
        if (++slot_hand == NSLOT) slot_hand = 0;
        if (!slot_ref[s]) {
            if (slot_tile[s] != 0xFFFF) unlink_slot(s);
            slot_tile[s] = id;
            chain[s] = bucket[(uint8_t)id];
            bucket[(uint8_t)id] = s;
            slot_ref[s] = 1;
            slot_free--;
            VBK_REG = 0;
            ctile_upload(id, s);
            return s;
        }
    }
    return NOSLOT;  // cache full: caller falls back to a static tile
}

static void cache_put(uint16_t id) {
    uint8_t s = find_slot(id);
    if (s != NOSLOT && slot_ref[s] && !--slot_ref[s]) slot_free++;
}

static void release(uint8_t ring) {
    uint8_t cf;
    const uint16_t *qq;
    mt_t m = drawn[ring];
    if (m == 0xFFFF) return;
    drawn[ring] = 0xFFFF;
    if (m & 0x8000) return;              // nothing cached in this cell
    if (m < MT_NEAR) { qq = &mt_q[m << 2]; cf = mt_cf[m]; }
    else { far_get(m - MT_NEAR); qq = (const uint16_t *)far_rec; cf = far_rec[16]; }
    for (; cf; cf >>= 1, qq++)
        if (cf & 1) cache_put(*qq);
}

// ---------------------------------------------------------------- what a cell looks like
// boundary overlay: unowned cell touching owned land shows a rope (sign in the middle of the edge)
static mt_t boundary_mt(uint8_t x, uint8_t y) {
    uint8_t n = (y > 0 && owned(x, y - 1)), s = (y < WH - 1 && owned(x, y + 1));
    uint8_t w = (x > 0 && owned(x - 1, y)), e = (x < WW - 1 && owned(x + 1, y));
    if (!(n | s | w | e)) return 0xFFFF;
    if (W_CELL[WIDX(x, y)] == C_ROAD) return 0xFFFF;
    if (((n || s) && (x & 7) == 4) || ((w || e) && (y & 7) == 4)) return MT_FOR_SALE;
    if (n || s) return (w || e) ? MT_ROPE_POST : MT_ROPE_H;
    return MT_ROPE_V;
}


static mt_t look(uint8_t x, uint8_t y);
mt_t cell_mt(uint8_t x, uint8_t y) BANKED { return look(x, y); }   // a BANKED call costs a trampoline even in-bank

static mt_t look(uint8_t x, uint8_t y) {
    if (scene == SCENE_FARM) {
        if (!owned(x, y)) {
            mt_t b = boundary_mt(x, y);
            if (b != 0xFFFF) return b;
        }
        return world_cell_mt(x, y);
    }
    if (scene == MAP_TOWN) {
        mt_t m;
        // businesses (the storefront rows 7-9 and 16-18) and Market Street (rows 21+): biz.c, invest.c
        if ((G.biz_owned | G.mk_flags | G.town_proj) && ((uint8_t)(y - 7) < 3 || (uint8_t)(y - 16) < 3 || y >= MK_GATE_Y)) {
            m = biz_mt(x, y);
            if (m != 0xFFFF) return m;
        }
        if (G.valley_done) {   // valley.c: restored town art
            m = valley_mt(x, y);
            if (m != 0xFFFF) return m;
        }
        m = map_mt(MIDX(x, y));
        if (G.town_proj & TP_PAVING) {   // invest.c's Square Paving: brick streets, string lights (drawn here: every cell)
            if (m == MT_COBBLE) return ((x ^ y) & 1) ? MT_PAVING_B : MT_PAVING_A;
            if (m == MT_BUNTING) return MT_STRING_LIGHTS;
        }
        if ((G.town_proj & TP_BOARDWALK) && y == 15 && x >= 20 && m == MT_DIRT) return MT_BOARDWALK;
        return m;
    }
    if (IS_AREA(scene)) return area_mt(x, y);   // mine rocks + ladders, beach finds
    return IS_INTERIOR(scene) ? interior_mt(x, y) : map_mt(MIDX(x, y));
}

// ---------------------------------------------------------------- drawing
static uint8_t tbuf[4], abuf[4];
static const uint8_t *put_a, *put_t;     // put2x2 sources: 4 attrs (VRAM bank 1), 4 tiles (bank 0)

// VRAM writes without set_bkg_tiles: wait for STAT mode 0/1, then write within the next 40 cycles
// (VRAM stays writable through mode 2: 80 dots = 40 cycles at double speed; the only interrupt, VBL,
// returns inside VBlank). In C the compiler's pointer shuffling put the second write ~60 cycles
// after the STAT read, into mode 3, where the write is dropped (a tile showing a glyph: lost attr).
// put2x2: de = row 0 address, bc = row 1 address; put_a -> VRAM bank 1, put_t -> bank 0.
static void put2x2(uint8_t *p0, uint8_t *p1) __naked {
    (void)p0; (void)p1;
    __asm
        ld   a, #1
        ldh  (_VBK_REG + 0), a
        ld   hl, #_put_a
        ld   a, (hl+)
        ld   h, (hl)
        ld   l, a
        call 20$
        xor  a, a
        ldh  (_VBK_REG + 0), a
        ld   hl, #_put_t
        ld   a, (hl+)
        ld   h, (hl)
        ld   l, a
    20$:
        ldh  a, (_STAT_REG + 0)
        and  a, #2
        jr   nz, 20$
        ld   a, (hl+)
        ld   (de), a
        inc  de
        ld   a, (hl+)
        ld   (de), a
        dec  de
    21$:
        ldh  a, (_STAT_REG + 0)
        and  a, #2
        jr   nz, 21$
        ld   a, (hl+)
        ld   (bc), a
        inc  bc
        ld   a, (hl+)
        ld   (bc), a
        dec  bc
        ret
    __endasm;
}

// copy vc_n (> 0) bytes VRAM -> VRAM in the current VRAM bank, 2 per STAT wait: de = dst, bc = src
static uint8_t vc_n;
static void vcopy(uint8_t *dst, const uint8_t *src) __naked {
    (void)dst; (void)src;
    __asm
        ld   h, b
        ld   l, c
        ld   a, (_vc_n)
        ld   c, a
    30$:
        ldh  a, (_STAT_REG + 0)
        and  a, #2
        jr   nz, 30$
        ld   a, (hl+)
        ld   (de), a
        inc  de
        dec  c
        ret  z
        ld   a, (hl+)
        ld   (de), a
        inc  de
        dec  c
        jr   nz, 30$
        ret
    __endasm;
}

static void draw(uint8_t x, uint8_t y) {
    uint8_t ring = ((y & 15) << 4) | (x & 15), cf;
    mt_t m;
    const uint16_t *qq;
    release(ring);
    if (x >= view_w || y >= view_h) m = MT_FOREST;
    else m = look(x, y);
    if (m < MT_NEAR) {
        uint16_t i4 = m << 2;
        put_t = &mt_tile[i4];
        put_a = &mt_attr[i4];
        qq = &mt_q[i4];
        cf = mt_cf[m];
    } else {   // a far record: copied into far_rec (it stays put until the next far_get)
        far_get(m - MT_NEAR);
        put_t = far_rec + 8;
        put_a = far_rec + 12;
        qq = (const uint16_t *)far_rec;
        cf = far_rec[16];
    }
    if (!cf) drawn[ring] = m | 0x8000;   // all static: written straight from the tables
    else {
        uint8_t k;
        memcpy(tbuf, put_t, 4);   // (an indexed C loop here cost ~140 cycles a byte)
        memcpy(abuf, put_a, 4);
        for (k = 0; k < 4; k++, cf >>= 1, qq++)
            if (cf & 1) {
                uint8_t s = cache_get(*qq);
                if (s == NOSLOT) { tbuf[k] = font_map[0]; abuf[k] = 0x08 | PAL_GRASS; }
                else tbuf[k] = s;
            }
        put_t = tbuf; put_a = abuf;
        drawn[ring] = m;
    }
    {   // straight into the BG map (0x9800); ~10x cheaper than 4 set_bkg_tiles calls
        uint8_t bx = (x << 1) & 31, by = (y << 1) & 31;
        put2x2((uint8_t *)(0x9800 + ((uint16_t)by << 5) + bx), (uint8_t *)(0x9800 + ((uint16_t)((by + 1) & 31) << 5) + bx));
    }
}

static uint8_t in_view(uint8_t x, uint8_t y) {
    return (uint8_t)(x - vx0) < VW && (uint8_t)(y - vy0) < VH;
}

void redraw_cell(uint8_t x, uint8_t y) BANKED {
    if (in_view(x, y)) draw(x, y);
}

void redraw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) BANKED {
    uint8_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (in_view(x + i, y + j)) draw(x + i, y + j);
}

// At a cell-aligned scroll the screen needs one line fewer, so the window keeps a spare on both sides:
// starting to move either way from rest (always cell-aligned) never needs a line drawn at once.
static uint8_t origin(uint16_t px, uint8_t span, uint8_t size, uint8_t spare_before) {
    uint8_t o = px >> 4;
    if ((spare_before || !((uint8_t)px & 15)) && o) o--;
    if (size <= span) return 0;
    if (o > size - span) o = size - span;
    return o;
}

static void pend_col(void) {
    draw(pc_x, pc_r0);
    if (++pc_r0 >= pc_r1) pc_on = 0;
}
static void pend_row(void) {
    draw(pr_c0, pr_y);
    if (++pr_c0 >= pr_c1) pr_on = 0;
}
// Draw up to n more pending cells, the column first. After the first cell it also stops once the frame
// is half used (the loop starts in VBlank at LY 144; LY 0..69 is the first ~half of the visible lines):
// dense cells with fresh cache tiles cost ~15% of a frame each. Lines left over are finished in later
// frames, or at once when they are needed on screen (view_update), so stopping early is always safe.
#define LY_LIMIT 70
static void pend_step(uint8_t n) {
    for (; n; n--) {
        if (pc_on) pend_col();
        else if (pr_on) pend_row();
        else return;
        if (LY_REG >= LY_LIMIT && LY_REG < 144) return;
    }
}
static void pend_clip(void) {        // the window moved: drop what is outside it now
    uint8_t e;
    if (pc_on) {
        e = vy0 + VH;
        if (pc_r0 < vy0) pc_r0 = vy0;
        if (pc_r1 > e) pc_r1 = e;
        if ((uint8_t)(pc_x - vx0) >= VW || pc_r0 >= pc_r1) pc_on = 0;
    }
    if (pr_on) {
        e = vx0 + VW;
        if (pr_c0 < vx0) pr_c0 = vx0;
        if (pr_c1 > e) pr_c1 = e;
        if ((uint8_t)(pr_y - vy0) >= VH || pr_c0 >= pr_c1) pr_on = 0;
    }
}

void view_reset(void) BANKED {
    uint8_t i, j;
    cache_clear();
    pc_on = pr_on = 0;
    last_scx = scx; last_scy = scy;
    vx0 = origin(scx, VW, view_w, 1);
    vy0 = origin(scy, VH, view_h, up_spare);
    for (j = 0; j < VH; j++)
        for (i = 0; i < VW; i++) draw(vx0 + i, vy0 + j);
}

void view_update(void) BANKED {
    uint8_t nx, ny, j, budget = BUDGET;
    if (scx != last_scx) last_scx = scx;
    if (scy != last_scy) { up_spare = scy < last_scy; last_scy = scy; }
    nx = origin(scx, VW, view_w, 1);
    ny = origin(scy, VH, view_h, up_spare);
    // each step releases the line leaving the window and queues the one entering it (off screen for now);
    // a column still pending from the last step is finished first unless it is the one leaving
    while (nx != vx0) {
        uint8_t out = nx > vx0 ? vx0 : vx0 + VW - 1;
        if (pc_on && pc_x == out) pc_on = 0;
        while (pc_on) pend_col();
        for (j = 0; j < VH; j++) release((((vy0 + j) & 15) << 4) | (out & 15));
        if (nx > vx0) { vx0++; pc_x = vx0 + VW - 1; } else { vx0--; pc_x = vx0; }
        pc_on = 1; pc_r0 = vy0; pc_r1 = vy0 + VH;
        pend_clip();
        budget = BUDGET / 2;   // releasing a line costs about as much as drawing half of one
    }
    while (ny != vy0) {
        uint8_t out = ny > vy0 ? vy0 : vy0 + VH - 1;
        if (pr_on && pr_y == out) pr_on = 0;
        while (pr_on) pend_row();
        for (j = 0; j < VW; j++) release(((out & 15) << 4) | ((vx0 + j) & 15));
        if (ny > vy0) { vy0++; pr_y = vy0 + VH - 1; } else { vy0--; pr_y = vy0; }
        pr_on = 1; pr_c0 = vx0; pr_c1 = vx0 + VW;
        pend_clip();
        budget = BUDGET / 2;
    }
    // a pending line already on screen (the camera outran the spare, e.g. turning at a cell edge) is finished now
    if (pc_on) {
        uint16_t e = (uint16_t)pc_x << 4;
        if (e < scx + 160 && e + 16 > scx) while (pc_on) pend_col();
    }
    if (pr_on) {
        uint16_t e = (uint16_t)pr_y << 4;
        if (e < scy + 144 && e + 16 > scy) while (pr_on) pend_row();
    }
    pend_step(budget);
}

// water shimmer: the art compiler emits art_water(phase) that rewrites the open-water tiles
void view_tick(void) BANKED {
    static uint8_t t, phase;
    if (++t < 40) return;
    t = 0;
    phase ^= 1;
    art_water(phase);
}

// one window row (20 tiles, the UI attribute): the HUD bar
static const uint8_t attr_row[20] = {GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR,
                                     GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR, GL_ATTR};
void win_row(uint8_t row, const uint8_t *tiles) BANKED {
    uint8_t *dst = (uint8_t *)(0x9C00 + ((uint16_t)row << 5));
    VBK_REG = 1; vc_n = 20; vcopy(dst, attr_row);
    VBK_REG = 0; vc_n = 20; vcopy(dst, tiles);
}

// copy the visible screen (17 rows x 20 tiles, tiles + attributes) from the BG ring into the window
// map, VRAM to VRAM (vcopy), instead of get_bkg_tiles + set_win_tiles row by row
void bg_to_win(void) BANKED {
    uint8_t row, bank, bx = (scx >> 3) & 31, by = (scy >> 3) & 31, n1 = 32 - bx;
    if (n1 > 20) n1 = 20;
    for (row = 0; row < 17; row++) {
        const uint8_t *src = (const uint8_t *)(0x9800 + ((uint16_t)by << 5));
        uint8_t *dst = (uint8_t *)(0x9C00 + ((uint16_t)row << 5));
        for (bank = 2; bank--;) {   // attributes (VRAM bank 1), then tiles (bank 0); the row wraps at 32
            VBK_REG = bank;
            vc_n = n1; vcopy(dst, src + bx);
            if (n1 < 20) { vc_n = 20 - n1; vcopy(dst + n1, src); }
        }
        by = (by + 1) & 31;
    }
}

// ---------------------------------------------------------------- art inside menus (window layer)
// Menus can show metatile art (portraits, item art, minimap tiles). Cache tiles used this way are
// held until ui_art_release(), which ui_end() calls when the last menu closes.
#define UI_HELD 64
static uint16_t ui_held[UI_HELD];
static uint8_t ui_nheld;

static void ui_quad(uint8_t x, uint8_t y, uint16_t qv, uint8_t p) {
    uint8_t t, a;
    if (qv & 0x8000) { t = (uint8_t)qv; a = p | 0x08; }
    else {
        uint8_t s = NOSLOT, k;
        for (k = 0; k < ui_nheld; k++) if (ui_held[k] == qv) { s = find_slot(qv); break; }   // already held by the UI
        if (s == NOSLOT && ui_nheld < UI_HELD) { s = cache_get(qv); if (s != NOSLOT) ui_held[ui_nheld++] = qv; }
        if (s == NOSLOT) { t = font_map[0]; a = GL_ATTR; }
        else { t = s; a = p; }
    }
    VBK_REG = 1; set_win_tile_xy(x, y, a | 0x80);
    VBK_REG = 0; set_win_tile_xy(x, y, t);
}

static const uint16_t *u_q;              // the quads + attrs of the metatile put_mt is drawing
static const uint8_t *u_a;
static void ui_rec(mt_t m) {
    if (m < MT_NEAR) { u_q = &mt_q[m << 2]; u_a = &mt_attr[m << 2]; }
    else { far_get(m - MT_NEAR); u_q = (const uint16_t *)far_rec; u_a = far_rec + 12; }
}

static void put_mt(uint8_t x, uint8_t y, mt_t m) {
    uint8_t k;
    ui_rec(m);
    for (k = 0; k < 4; k++) ui_quad(x + (k & 1), y + (k >> 1), u_q[k], u_a[k] & ~0x08);
}

void ui_put_mt(uint8_t x, uint8_t y, mt_t m) BANKED { put_mt(x, y, m); }

void ui_put_quad(uint8_t x, uint8_t y, mt_t m, uint8_t k) BANKED {
    ui_rec(m);
    ui_quad(x, y, u_q[k], u_a[k] & ~0x08);
}

void ui_put_struct(uint8_t x, uint8_t y, mt_t first, uint8_t w, uint8_t h) BANKED {
    uint8_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) put_mt(x + i * 2, y + j * 2, first + j * w + i);
}

void ui_art_release(void) BANKED {
    while (ui_nheld) cache_put(ui_held[--ui_nheld]);
}
