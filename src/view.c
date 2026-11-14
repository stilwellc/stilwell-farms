// Stilwell Farms -- tile cache + streaming renderer (bank 0)
//
// Every metatile quad is either a static tile (bank 1, always resident) or a cache tile id.
// Cache tiles are uploaded into VRAM bank 0 slots on demand and reference-counted by the
// cells currently drawn; a slot is reused once nothing on screen references it.
// The drawn region is an 11x10-cell window that follows the camera through the 32x32 BG ring.
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "game.h"

#define NSLOT 255
#define NOSLOT 0xFF
static uint8_t slot_of[NUM_CTILES];      // cache id -> slot
static uint16_t slot_tile[NSLOT];        // slot -> cache id (0xFFFF none)
static uint16_t slot_ref[NSLOT];
static uint8_t slot_hand;
static mt_t drawn[256];                  // ring (cy&15)<<4 | (cx&15) -> mt drawn there (0xFFFF none)
static uint8_t vx0, vy0;                 // drawn region origin (cells)
#define VW 11
#define VH 10
uint8_t view_w = 32, view_h = 32;

// ---------------------------------------------------------------- cache
static void cache_clear(void) {
    uint8_t s;
    memset(slot_of, NOSLOT, sizeof(slot_of));
    for (s = 0; s < NSLOT; s++) { slot_tile[s] = 0xFFFF; slot_ref[s] = 0; }
    memset(drawn, 0xFF, sizeof(drawn));
    slot_hand = 0;
}

static uint8_t cache_get(uint16_t id) {
    uint8_t s = slot_of[id], n;
    if (s != NOSLOT) { slot_ref[s]++; return s; }
    for (n = 0; n < NSLOT; n++) {
        s = slot_hand;
        if (++slot_hand == NSLOT) slot_hand = 0;
        if (!slot_ref[s]) {
            if (slot_tile[s] != 0xFFFF) slot_of[slot_tile[s]] = NOSLOT;
            slot_tile[s] = id;
            slot_of[id] = s;
            slot_ref[s] = 1;
            VBK_REG = 0;
            ctile_upload(id, s);
            return s;
        }
    }
    return NOSLOT;  // cache full: caller falls back to a static tile
}

static void cache_put(uint16_t id) {
    uint8_t s = slot_of[id];
    if (s != NOSLOT && slot_ref[s]) slot_ref[s]--;
}

static uint16_t q[4];
static uint8_t pf[4];

static void release(uint8_t ring) {
    uint8_t k;
    mt_t m = drawn[ring];
    if (m == 0xFFFF) return;
    mt_get(m, q, pf);
    for (k = 0; k < 4; k++)
        if (!(q[k] & 0x8000)) cache_put(q[k]);
    drawn[ring] = 0xFFFF;
}

// ---------------------------------------------------------------- what a cell looks like
uint8_t owned(uint8_t x, uint8_t y) {
    return (G.plots >> PLOT_OF(x, y)) & 1;
}

static uint8_t is_water(int8_t x, int8_t y) {
    if (x < 0 || y < 0 || x >= WW || y >= WH) return 1;
    {
        uint8_t c = G.cell[WIDX(x, y)];
        return c == C_WATER || (c == C_DECOR && (G.a1[WIDX(x, y)] == 1 || G.a1[WIDX(x, y)] == 2));  // bridges count
    }
}


static const mt_t water_mt[16] = {
    MT_WATER_0, MT_WATER_1, MT_WATER_2, MT_WATER_3, MT_WATER_4, MT_WATER_5, MT_WATER_6, MT_WATER_7,
    MT_WATER_8, MT_WATER_9, MT_WATER_10, MT_WATER_11, MT_WATER_12, MT_WATER_13, MT_WATER_14, MT_WATER_15};

mt_t water_tile(uint8_t x, uint8_t y) {
    uint8_t m = 0;
    if (is_water(x, (int8_t)y - 1)) m |= 1;
    if (is_water(x + 1, y)) m |= 2;
    if (is_water(x, y + 1)) m |= 4;
    if (is_water((int8_t)x - 1, y)) m |= 8;
    return water_mt[m];
}

// boundary overlay: unowned cell touching owned land shows a rope (sign in the middle of the edge)
static mt_t boundary_mt(uint8_t x, uint8_t y) {
    uint8_t n = (y > 0 && owned(x, y - 1)), s = (y < WH - 1 && owned(x, y + 1));
    uint8_t w = (x > 0 && owned(x - 1, y)), e = (x < WW - 1 && owned(x + 1, y));
    if (!(n | s | w | e)) return 0xFFFF;
    if (G.cell[WIDX(x, y)] == C_ROAD) return 0xFFFF;
    if (((n || s) && (x & 7) == 4) || ((w || e) && (y & 7) == 4)) return MT_FOR_SALE;
    if (n || s) return (w || e) ? MT_ROPE_POST : MT_ROPE_H;
    return MT_ROPE_V;
}


mt_t cell_mt(uint8_t x, uint8_t y) {
    if (scene == SCENE_FARM) {
        if (!owned(x, y)) {
            mt_t b = boundary_mt(x, y);
            if (b != 0xFFFF) return b;
        }
        return world_cell_mt(x, y);
    }
    return IS_INTERIOR(scene) ? interior_mt(x, y) : map_mt(WIDX(x, y));
}

// ---------------------------------------------------------------- drawing
static uint8_t tbuf[4], abuf[4];

static void draw(uint8_t x, uint8_t y) {
    uint8_t ring = ((y & 15) << 4) | (x & 15), k;
    mt_t m;
    release(ring);
    if (x >= view_w || y >= view_h) m = MT_FOREST;
    else m = cell_mt(x, y);
    mt_get(m, q, pf);
    for (k = 0; k < 4; k++) {
        if (q[k] & 0x8000) {
            tbuf[k] = (uint8_t)q[k];
            abuf[k] = pf[k] | 0x08;
        } else {
            uint8_t s = cache_get(q[k]);
            if (s == NOSLOT) { tbuf[k] = font_map[0]; abuf[k] = 0x08 | PAL_GRASS; }
            else { tbuf[k] = s; abuf[k] = pf[k]; }
        }
    }
    drawn[ring] = m;
    {
        uint8_t bx = (x << 1) & 31, by = (y << 1) & 31;
        VBK_REG = 1;
        set_bkg_tiles(bx, by, 2, 1, abuf);
        set_bkg_tiles(bx, (by + 1) & 31, 2, 1, abuf + 2);
        VBK_REG = 0;
        set_bkg_tiles(bx, by, 2, 1, tbuf);
        set_bkg_tiles(bx, (by + 1) & 31, 2, 1, tbuf + 2);
    }
}

static uint8_t in_view(uint8_t x, uint8_t y) {
    return (uint8_t)(x - vx0) < VW && (uint8_t)(y - vy0) < VH;
}

void redraw_cell(uint8_t x, uint8_t y) {
    if (in_view(x, y)) draw(x, y);
}

void redraw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    uint8_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) redraw_cell(x + i, y + j);
}

static uint8_t origin(uint16_t px, uint8_t span, uint8_t size) {
    uint8_t o = px >> 4;
    if (size <= span) return 0;
    if (o > size - span) o = size - span;
    return o;
}

void view_reset(void) {
    uint8_t i, j;
    cache_clear();
    vx0 = origin(scx, VW, view_w);
    vy0 = origin(scy, VH, view_h);
    for (j = 0; j < VH; j++)
        for (i = 0; i < VW; i++) draw(vx0 + i, vy0 + j);
}

void view_update(void) {
    uint8_t nx = origin(scx, VW, view_w), ny = origin(scy, VH, view_h), j;
    while (nx > vx0) {
        for (j = 0; j < VH; j++) release((((vy0 + j) & 15) << 4) | (vx0 & 15));
        vx0++;
        for (j = 0; j < VH; j++) draw(vx0 + VW - 1, vy0 + j);
    }
    while (nx < vx0) {
        for (j = 0; j < VH; j++) release((((vy0 + j) & 15) << 4) | ((vx0 + VW - 1) & 15));
        vx0--;
        for (j = 0; j < VH; j++) draw(vx0, vy0 + j);
    }
    while (ny > vy0) {
        for (j = 0; j < VW; j++) release(((vy0 & 15) << 4) | ((vx0 + j) & 15));
        vy0++;
        for (j = 0; j < VW; j++) draw(vx0 + j, vy0 + VH - 1);
    }
    while (ny < vy0) {
        for (j = 0; j < VW; j++) release((((vy0 + VH - 1) & 15) << 4) | ((vx0 + j) & 15));
        vy0--;
        for (j = 0; j < VW; j++) draw(vx0 + j, vy0);
    }
}

// water shimmer: the art compiler emits art_water(phase) that rewrites the open-water tiles
void view_tick(void) {
    static uint8_t t, phase;
    if (++t < 40) return;
    t = 0;
    phase ^= 1;
    art_water(phase);
}

// copy one visible screen row (20 tiles) of the BG ring, handling wrap
void bg_copy_row(uint8_t row, uint8_t *tiles, uint8_t *attrs) {
    uint8_t bx = (scx >> 3) & 31, by = ((scy >> 3) + row) & 31, n1 = 32 - bx;
    if (n1 > 20) n1 = 20;
    VBK_REG = 1;
    get_bkg_tiles(bx, by, n1, 1, attrs);
    if (n1 < 20) get_bkg_tiles(0, by, 20 - n1, 1, attrs + n1);
    VBK_REG = 0;
    get_bkg_tiles(bx, by, n1, 1, tiles);
    if (n1 < 20) get_bkg_tiles(0, by, 20 - n1, 1, tiles + n1);
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
        for (k = 0; k < ui_nheld; k++) if (ui_held[k] == qv) { s = slot_of[qv]; break; }   // already held by the UI
        if (s == NOSLOT && ui_nheld < UI_HELD) { s = cache_get(qv); if (s != NOSLOT) ui_held[ui_nheld++] = qv; }
        if (s == NOSLOT) { t = font_map[0]; a = GL_ATTR; }
        else { t = s; a = p; }
    }
    VBK_REG = 1; set_win_tile_xy(x, y, a | 0x80);
    VBK_REG = 0; set_win_tile_xy(x, y, t);
}

void ui_put_mt(uint8_t x, uint8_t y, mt_t m) {
    uint8_t k;
    mt_get(m, q, pf);
    for (k = 0; k < 4; k++) ui_quad(x + (k & 1), y + (k >> 1), q[k], pf[k]);
}

void ui_put_quad(uint8_t x, uint8_t y, mt_t m, uint8_t k) {
    mt_get(m, q, pf);
    ui_quad(x, y, q[k], pf[k]);
}

void ui_put_struct(uint8_t x, uint8_t y, mt_t first, uint8_t w, uint8_t h) {
    uint8_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) ui_put_mt(x + i * 2, y + j * 2, first + j * w + i);
}

void ui_art_release(void) {
    while (ui_nheld) cache_put(ui_held[--ui_nheld]);
}
