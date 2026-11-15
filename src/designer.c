// Stilwell Farms -- design tools (banked): paint strokes and the eraser, undo, farm colours, blueprints
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "decor.h"
#include "designer.h"
BANKREF(designer)

#if WW == 32
#define WSH 5
#elif WW == 64
#define WSH 6
#else
#error designer.c: world width must be 32 or 64
#endif
#define IX(i) ((uint8_t)(i) & (WW - 1))
#define IY(i) ((uint8_t)((i) >> WSH))

// ================================================================ the world, one cell at a time
static void wset(uint16_t i, uint8_t c, uint8_t a, uint8_t b) { W_CELL[i] = c; W_A1[i] = a; W_A2_SET(i, b); }

static uint8_t needs_fix(uint16_t i) {   // water edges and fence rails depend on the neighbours
    uint8_t c = W_CELL[i];
    return c == C_WATER || (c == C_DECOR && (decor[W_A1[i]].fence || W_A1[i] == DK_BRIDGE));
}

static void show(uint16_t i, uint8_t was_fix) {
    if (was_fix || needs_fix(i)) fix_autotile(IX(i), IY(i));
    else redraw_cell(IX(i), IY(i));
}

static uint8_t ground(uint8_t c) { return c == C_GRASS || c == C_GRASS2 || c == C_WILDFLOWER || c == C_DIRT; }

static uint8_t occupied(uint16_t i) {
    uint8_t x = IX(i), y = IY(i);
    return (x == G.px && y == G.py) || actor_blocks(x, y);
}

// what was under a placed piece, in a nibble: 0..4 = grass, grass2, wildflowers, dirt, water; 8 | wet/fert = empty soil
static const uint8_t prior_c[5] = {C_GRASS, C_GRASS2, C_WILDFLOWER, C_DIRT, C_WATER};
static uint8_t prior_of(uint16_t i) {
    uint8_t c = W_CELL[i], k;
    if (c == C_SOIL) return 8 | (W_A1[i] >> 6);
    for (k = 1; k < 5; k++) if (c == prior_c[k]) return k;
    return 0;
}
static void prior_put(uint16_t i, uint8_t p) {
    if (p & 8) wset(i, C_SOIL, (uint8_t)(p << 6), 0);
    else wset(i, prior_c[p], 0, 0);
}

// is the piece (catalogue kind + arg) still standing at i, untouched since it was placed?
static uint8_t placed_is(uint16_t i, uint8_t kind, uint8_t arg) {
    uint8_t c = W_CELL[i], a = W_A1[i];
    switch (kind) {
    case BK_DECOR: return c == C_DECOR && a == arg;
    case BK_MACHINE: return c == C_MACHINE && (a & 3) == arg && !W_A2(i);   // one with goods inside stays
    case BK_TREE: return c == C_TREE && (a >> 5) == arg;
    case BK_POND: return c == C_WATER;
    case BK_FILL: return c == C_GRASS;
    }
    return 0;
}

// ================================================================ undo history
// A byte pool of whole records, newest last; the oldest are dropped to make room. Every record starts
// with a 4-byte header [type, a, b, c]; entries follow:
//   U_CELLS  a,b = kind,arg   entries: idx lo, idx hi | prior << 4 (one per cell; refund = unit cost each)
//   U_ERASE                   entries: idx lo, idx hi, cell, a1, a2
//   U_STAMP  a = slot, b,c = x0,y0; then an 8-byte mask of placed blueprint cells, then prior nibbles
//   U_BLD    a = slot, b = building type
//   U_MOVE   a,b = from idx, c + 1 byte = to idx
//   U_MOVEB  a = slot, b,c = where it stood
#define UPOOL 112
#define URECS 8
#define UHDR 4
enum { U_CELLS, U_ERASE, U_STAMP, U_BLD, U_MOVE, U_MOVEB };
static uint8_t upool[UPOOL], ulen[URECS];
uint8_t undo_n;                         // closed records (global: the tests read it)
static uint8_t utop, uopen = 0xFF, ulost;   // bytes used, start of the open record

static void u_shift(uint8_t at, uint8_t n) {   // drop n bytes at `at`, closing the gap
    uint8_t k;
    for (k = at; k + n < utop; k++) upool[k] = upool[k + n];
    utop -= n;
}

static void u_drop_oldest(void) {
    uint8_t n = ulen[0], k;
    u_shift(0, n);
    if (uopen != 0xFF) uopen -= n;
    for (k = 1; k < undo_n; k++) ulen[k - 1] = ulen[k];
    undo_n--;
}

static uint8_t u_room(uint8_t n) {
    while ((uint16_t)utop + n > UPOOL) {
        if (!undo_n) return 0;
        u_drop_oldest();
    }
    return 1;
}

static void u_begin(uint8_t t, uint8_t a, uint8_t b, uint8_t c) {
    uopen = 0xFF;
    ulost = 0;
    if (undo_n == URECS) u_drop_oldest();
    u_room(UHDR);
    uopen = utop;
    upool[utop++] = t; upool[utop++] = a; upool[utop++] = b; upool[utop++] = c;
}

static void u_put(const uint8_t *b, uint8_t n) {
    if (uopen == 0xFF) return;
    if (!u_room(n)) { utop = uopen; uopen = 0xFF; ulost = 1; return; }   // the record itself outgrew the pool
    memcpy(upool + utop, b, n);
    utop += n;
}

static void u_end(void) {
    if (uopen == 0xFF) return;
    if (utop - uopen > UHDR) ulen[undo_n++] = utop - uopen;
    else utop = uopen;   // nothing happened
    uopen = 0xFF;
}

static void u_record(const uint8_t *b, uint8_t n) {   // a whole record at once
    u_begin(b[0], b[1], b[2], b[3]);
    u_put(b + UHDR, n - UHDR);
    if (n == UHDR && uopen != 0xFF) { ulen[undo_n++] = UHDR; uopen = 0xFF; }
    else u_end();
}

uint8_t undo_count(void) BANKED { return undo_n; }

uint8_t copy_on;                        // first corner of Copy Area is set
static uint8_t copy_x, copy_y;
void undo_clear(void) BANKED {
    undo_n = 0; utop = 0; uopen = 0xFF;
    paint_on = 0;
    ghost_w = 0;
    copy_on = 0;
}

void undo_bld(uint8_t slot) BANKED {
    uint8_t r[UHDR];
    r[0] = U_BLD; r[1] = slot; r[2] = G.bld[slot].type; r[3] = 0;
    u_record(r, UHDR);
}

void undo_move(uint16_t from, uint16_t to) BANKED {
    uint8_t r[UHDR + 1];
    r[0] = U_MOVE; r[1] = (uint8_t)from; r[2] = (uint8_t)(from >> 8); r[3] = (uint8_t)to; r[4] = (uint8_t)(to >> 8);
    u_record(r, UHDR + 1);
}

void undo_moveb(uint8_t slot, uint8_t ox, uint8_t oy) BANKED {
    uint8_t r[UHDR];
    r[0] = U_MOVEB; r[1] = slot; r[2] = ox; r[3] = oy;
    u_record(r, UHDR);
}

// a blueprint slot is about to be overwritten: stamps made from it can no longer be read back
static void u_forget_slot(uint8_t slot) {
    uint8_t k = 0, off = 0;
    while (k < undo_n) {
        if (upool[off] == U_STAMP && upool[off + 1] == slot) {
            uint8_t n = ulen[k], j;
            u_shift(off, n);
            for (j = k + 1; j < undo_n; j++) ulen[j - 1] = ulen[j];
            undo_n--;
        } else off += ulen[k++];
    }
}

// ================================================================ blueprints: cells and pieces
static uint8_t bp_at(uint8_t s, uint8_t gx, uint8_t gy) {   // piece code of a blueprint cell, 0 = empty
    uint8_t k = (uint8_t)(gy << 3) | gx, nb = G.bp_cell[s][k >> 1];
    nb = (k & 1) ? nb >> 4 : nb & 15;
    return nb ? G.bp_pal[s][nb - 1] : 0;
}

static uint8_t code_kind(uint8_t code, uint8_t *arg) {
    *arg = code & 15;
    if (code < PC_MACHINE) { *arg = code; return BK_DECOR; }
    if (code < PC_TREE) return BK_MACHINE;
    if (code < PC_BLDG) return BK_TREE;
    if (code < PC_POND) return BK_BLDG;
    *arg = 0;
    return BK_POND;
}

static uint8_t piece_fits(uint8_t x, uint8_t y, uint8_t kind, uint8_t arg) {
    if (kind == BK_BLDG) {
        uint8_t i, j, w = bld_w(arg), h = bld_h(arg);
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++)
                if (!bk_fits(x + i, y + j, BK_BLDG, arg)) return 0;
        return 1;
    }
    return bk_fits(x, y, kind, arg);
}

static uint8_t bld_free(void) {
    uint8_t k;
    for (k = 0; k < MAX_BLD; k++) if (G.bld[k].type == 0xFF) return 1;
    return 0;
}

static uint8_t homeless(uint8_t slot) {   // no animal lives in this building
    uint8_t k;
    for (k = 0; k < MAX_ANIMALS; k++) if (G.anim[k].kind != 0xFF && G.anim[k].home == slot) return 0;
    return 1;
}

static void stamp_bld(uint8_t slot) {
    Building *b = &G.bld[slot];
    uint8_t xx, yy, p = 0, w = bld_w(b->type), h = bld_h(b->type);
    for (yy = 0; yy < h; yy++)
        for (xx = 0; xx < w; xx++) wset(WIDX(b->x + xx, b->y + yy), C_STRUCT, slot, p++);
    redraw_rect(b->x, b->y, w, h);
}

// ================================================================ undo
static uint32_t rg;
static uint16_t rw, rs;
static void refund(uint8_t kind, uint8_t arg) {
    uint16_t g; uint8_t w, s;
    bk_cost(kind, arg, &g, &w, &s);
    rg += g; rw += w; rs += s;
}

static uint8_t undo_stamp(const uint8_t *r) {
    uint8_t slot = r[1], x0 = r[2], y0 = r[3], k, np = 0, cnt = 0, code, kind, arg, p, x, y;
    for (k = 0; k < BP_MAX * BP_MAX; k++) {
        uint16_t i;
        if (!(r[UHDR + (k >> 3)] & (1 << (k & 7)))) continue;
        p = r[UHDR + 8 + (np >> 1)];
        p = (np & 1) ? p >> 4 : p & 15;
        np++;
        x = x0 + (k & 7); y = y0 + (k >> 3);
        code = bp_at(slot, k & 7, k >> 3);
        kind = code_kind(code, &arg);
        i = WIDX(x, y);
        if (kind == BK_BLDG) {
            uint8_t s = W_A1[i];
            Building *b = &G.bld[s];
            if (W_CELL[i] != C_STRUCT || b->type != arg || b->x != x || b->y != y || !homeless(s)) continue;
            bld_remove(s);
            redraw_rect(x, y, bld_w(arg), bld_h(arg));
        } else {
            uint8_t f;
            if (!placed_is(i, kind, arg) || (p == 4 && occupied(i))) continue;
            f = needs_fix(i);
            prior_put(i, p);
            show(i, f);
        }
        refund(kind, arg);
        cnt++;
    }
    return cnt;
}

uint8_t undo_last(void) BANKED {
    uint8_t *r, n, k, cnt = 0, f;
    uint16_t i;
    if (paint_on) paint_end();
    if (!undo_n) { sfx(SFX_BUMP); hud_flash("Nothing to undo."); return 0; }
    n = ulen[--undo_n];
    utop -= n;
    r = upool + utop;
    rg = 0; rw = 0; rs = 0;
    switch (r[0]) {
    case U_CELLS:
        for (k = UHDR; k < n; k += 2) {
            uint8_t p = r[k + 1] >> 4;
            i = r[k] | ((uint16_t)(r[k + 1] & 15) << 8);
            if (!placed_is(i, r[1], r[2]) || (p == 4 && occupied(i))) continue;
            f = needs_fix(i);
            prior_put(i, p);
            show(i, f);
            refund(r[1], r[2]);
            cnt++;
        }
        break;
    case U_ERASE:
        for (k = UHDR; k < n; k += 5) {
            uint8_t c, bridge = r[k + 2] == C_DECOR && r[k + 3] == DK_BRIDGE;
            i = r[k] | ((uint16_t)r[k + 1] << 8);
            c = W_CELL[i];
            if (bridge ? c != C_WATER : (!ground(c) || occupied(i))) continue;
            f = needs_fix(i);
            wset(i, r[k + 2], r[k + 3], r[k + 4]);
            show(i, f);
            cnt++;
        }
        break;
    case U_STAMP:
        cnt = undo_stamp(r);
        break;
    case U_BLD: {
        Building *b = &G.bld[r[1]];
        if (b->type != r[2] || !homeless(r[1])) break;
        i = WIDX(b->x, b->y);
        if (W_CELL[i] != C_STRUCT || W_A1[i] != r[1]) break;   // in hand (being moved)
        {
            uint8_t bx = b->x, by = b->y;
            bld_remove(r[1]);
            redraw_rect(bx, by, bld_w(r[2]), bld_h(r[2]));
        }
        refund(BK_BLDG, r[2]);
        cnt = 1;
        break;
    }
    case U_MOVE: {
        uint16_t from = r[1] | ((uint16_t)r[2] << 8), to = r[3] | ((uint16_t)r[4] << 8);
        uint8_t c = W_CELL[to];
        if ((c != C_DECOR && c != C_MACHINE && c != C_TREE) || !ground(W_CELL[from]) || occupied(from)) break;
        f = needs_fix(to);
        wset(from, c, W_A1[to], W_A2(to));
        wset(to, C_GRASS, 0, 0);
        show(to, f);
        show(from, 0);
        cnt = 1;
        break;
    }
    case U_MOVEB: {
        Building *b = &G.bld[r[1]];
        uint8_t xx, yy, w, h, ok = 1, cx, cy;
        if (b->type == 0xFF) break;
        cx = b->x; cy = b->y;
        if (W_CELL[WIDX(cx, cy)] != C_STRUCT || W_A1[WIDX(cx, cy)] != r[1]) break;
        w = bld_w(b->type); h = bld_h(b->type);
        for (yy = 0; yy < h; yy++)
            for (xx = 0; xx < w; xx++) wset(WIDX(cx + xx, cy + yy), C_GRASS, 0, 0);
        for (yy = 0; yy < h && ok; yy++)
            for (xx = 0; xx < w; xx++) {
                i = WIDX(r[2] + xx, r[3] + yy);
                if (!ground(W_CELL[i]) || occupied(i)) { ok = 0; break; }
            }
        if (ok) { b->x = r[2]; b->y = r[3]; }
        stamp_bld(r[1]);
        if (ok) { redraw_rect(cx, cy, w, h); cnt = 1; }
        break;
    }
    }
    G.gold += rg; G.wood += rw; G.stone += rs;
    if (!cnt) { sfx(SFX_BUMP); hud_flash("Can't undo that now"); return 0; }
    sfx(SFX_CANCEL);
    s_clr(); s_add("Undone");
    if (rg) { s_add("  +G"); s_num(rg, 0); }
    else if (rw) { s_add("  +"); s_num(rw, 0); s_ch('W'); }
    else if (rs) { s_add("  +"); s_num(rs, 0); s_ch('S'); }
    hud_flash(sbuf);
    return 1;
}

// ================================================================ paint strokes and the eraser
// A press in build mode starts a stroke. Let go without moving and the item goes in front of you (a tap);
// walk with A held and each step lays it on the cell just left behind, so a line starts where you stood
// and fences or hedges never block the way. The eraser clears the cell ahead at once and as you go.
uint8_t paint_on;
static uint8_t pkind, parg, pn, ptap;
static uint16_t pgold, pwood, pstone;   // spent on this stroke

static void paint_hud(void) {
    s_clr();
    if (pkind == BK_REMOVE) { s_add("ERASE x"); s_num(pn, 0); }
    else {
        s_add("PAINT x"); s_num(pn, 0);
        if (pgold) { s_add(" -G"); s_num(pgold, 0); }
        else if (pwood) { s_add(" -"); s_num(pwood, 0); s_ch('W'); }
        else if (pstone) { s_add(" -"); s_num(pstone, 0); s_ch('S'); }
    }
    hud_flash(sbuf);
}

void paint_begin(uint8_t kind, uint8_t arg) BANKED {
    if (paint_on) paint_end();
    pkind = kind; parg = arg; pn = 0;
    pgold = 0; pwood = 0; pstone = 0;
    ptap = kind != BK_REMOVE;
    paint_on = 1;
    if (kind == BK_REMOVE) u_begin(U_ERASE, 0, 0, 0);
    else u_begin(U_CELLS, kind, arg, 0);
}

uint8_t erase_at(uint8_t x, uint8_t y) BANKED {
    uint16_t i = WIDX(x, y);
    uint8_t c = W_CELL[i], e[5], f;
    if (c != C_DECOR && c != C_MACHINE && c != C_TREE) return 0;
    e[0] = (uint8_t)i; e[1] = (uint8_t)(i >> 8); e[2] = c; e[3] = W_A1[i]; e[4] = W_A2(i);
    f = needs_fix(i);
    wset(i, (c == C_DECOR && e[3] == DK_BRIDGE) ? C_WATER : C_GRASS, 0, 0);
    show(i, f);
    if (paint_on == 1 && pkind == BK_REMOVE) {
        u_put(e, 5);
        if (++pn > 1) paint_hud();
    }
    fx_show(SPR_DUST, SPR_DUST_PAL, x, y);
    sfx(SFX_HIT);
    return 1;
}

uint8_t paint_cell(uint8_t x, uint8_t y) BANKED {
    uint16_t g, i;
    uint8_t w, s, e[2];
    if (paint_on != 1) return 0;
    if (pkind == BK_REMOVE) return erase_at(x, y);
    if (!bk_fits(x, y, pkind, parg)) return 0;
    bk_cost(pkind, parg, &g, &w, &s);
    if (G.gold < g || G.wood < w || G.stone < s) {
        paint_on = 2;   // the stroke stops here; nothing more is tried until A is let go
        sfx(SFX_BUMP);
        hud_flash(pn ? "Out of materials!" : "Need materials.");
        return 0;
    }
    i = WIDX(x, y);
    e[0] = (uint8_t)i; e[1] = (uint8_t)(i >> 8) | (uint8_t)(prior_of(i) << 4);
    bk_put(x, y, pkind, parg);
    G.gold -= g; G.wood -= w; G.stone -= s;
    pgold += g; pwood += w; pstone += s;
    u_put(e, 2);
    sfx(SFX_HIT);
    fx_show(SPR_DUST, SPR_DUST_PAL, x, y);
    if (++pn > 1) paint_hud();
    return 1;
}

void paint_step(uint8_t lx, uint8_t ly) BANKED {
    uint8_t fx, fy;
    if (paint_on != 1) return;
    ptap = 0;
    if (pkind == BK_REMOVE) { if (front_cell(&fx, &fy)) erase_at(fx, fy); }
    else paint_cell(lx, ly);
}

void paint_bump(void) BANKED {
    uint8_t fx, fy;
    if (paint_on == 1 && pkind == BK_REMOVE && front_cell(&fx, &fy)) erase_at(fx, fy);
}

void paint_end(void) BANKED {
    uint8_t n, fx, fy;
    if (!paint_on) return;
    if (paint_on == 1 && ptap && front_cell(&fx, &fy) && !paint_cell(fx, fy) && paint_on == 1) {
        sfx(SFX_BUMP);
        hud_flash("Can't build there.");
    }
    n = pn;
    u_end();
    paint_on = 0;
    if (n > 1) {
        s_clr();
        s_add(pkind == BK_REMOVE ? "Cleared " : "Laid "); s_num(n, 0);
        if (ulost) s_add(" (no undo)");
        else if (pgold) { s_add(" -G"); s_num(pgold, 0); }
        hud_flash(sbuf);
    }
    if (n && pkind != BK_REMOVE) goals_check();
}

// ================================================================ farm colours
// Three palette entries of the farm's look can be repainted: the red of roofs and brick, the wood
// stain, and the stone. Palette RAM edits only (no tiles), applied by main.c's palette builder on the
// farm, under the same day/night/weather ramps and fades as every other colour.
#define C5(rgb) ((uint16_t)(((rgb) >> 19) & 31) | ((uint16_t)(((rgb) >> 11) & 31) << 5) | ((uint16_t)(((rgb) >> 3) & 31) << 10))
#define NUM_SWATCH 5
static const uint16_t swatch[NUM_ACCENT][NUM_SWATCH] = {
    {C5(0xE04838), C5(0xB02828), C5(0xD86838), C5(0xE87090), C5(0x9050A0)},
    {C5(0xD08C48), C5(0xC8A060), C5(0xC07048), C5(0x986038), C5(0xB0A088)},
    {C5(0xB8C0C8), C5(0xD8C8A0), C5(0x98A0B8), C5(0xD8B8B0), C5(0xA8C0A0)}};
static const char *const sw_name[NUM_ACCENT][NUM_SWATCH] = {
    {"Crimson", "Barn Red", "Terracotta", "Rose", "Plum"},
    {"Honey", "Oak", "Cedar", "Walnut", "Driftwood"},
    {"Grey", "Sandstone", "Slate", "Rose Stone", "Moss"}};
static const char *const acc_label[NUM_ACCENT] = {"Roofs", "Wood", "Stone"};
static const mt_t acc_mt[NUM_ACCENT] = {MT_BRICK, MT_PLANK, MT_PATH};

static const uint8_t acc_slot[NUM_ACCENT] = {PAL_RED * 4 + 2, PAL_WOOD * 4 + 1, PAL_STONE * 4 + 1};
void accent_patch(void) BANKED {
    uint8_t k;
    for (k = 0; k < NUM_ACCENT; k++) if (G.accent[k]) pbuf[acc_slot[k]] = G.accent[k];
}

static uint8_t sw_index(uint8_t k) {
    uint8_t j;
    for (j = 1; j < NUM_SWATCH; j++) if (G.accent[k] == swatch[k][j]) return j;
    return 0;
}

static void ui_char_flip(uint8_t x, uint8_t y, uint8_t tile) {   // ui_char, mirrored (the left arrow)
    volatile uint8_t *p = (volatile uint8_t *)(0x9C00 + ((uint16_t)y << 5) + x);
    while (STAT_REG & STATF_BUSY);
    VBK_REG = 1; *p = GL_ATTR | 0x20;
    VBK_REG = 0; *p = tile;
}

static void colour_row(uint8_t k) {
    uint8_t y = 2 + k * 3, j, sel = sw_index(k);
    ui_clear(5, y, 14, 1);
    ui_char_flip(5, y, GL_CURSOR);
    ui_text(7, y, sw_name[k][sel]);
    ui_char(18, y, GL_CURSOR);
    for (j = 0; j < NUM_SWATCH; j++) ui_char(14 + j, y - 1, j == sel ? GL_BAR_FULL : GL_BAR_EMPTY);
}

void colour_menu(void) BANKED {
    uint8_t row = 0, k, t = 0;
    uint16_t old[NUM_ACCENT];
    for (k = 0; k < NUM_ACCENT; k++) old[k] = G.accent[k];
    ui_box(0, 0, 20, 11);
    ui_title(1, 0, "FARM COLOURS");
    for (k = 0; k < NUM_ACCENT; k++) {
        ui_put_mt(2, 1 + k * 3, acc_mt[k]);
        ui_text(5, 1 + k * 3, acc_label[k]);
        colour_row(k);
    }
    ui_text(2, 9, "A keep  B undo");
    while (1) {
        ui_char(1, 1 + row * 3, (++t & 16) ? GL_CURSOR2 : GL_CURSOR);
        read_input();
        if (joy_press & (J_UP | J_DOWN)) {
            ui_char(1, 1 + row * 3, GL_FILL);
            if (joy_press & J_DOWN) row = row + 1 < NUM_ACCENT ? row + 1 : 0;
            else row = row ? row - 1 : NUM_ACCENT - 1;
            sfx(SFX_CURSOR);
        } else if (joy_press & (J_LEFT | J_RIGHT)) {
            uint8_t s = sw_index(row);
            if (joy_press & J_RIGHT) s = s + 1 < NUM_SWATCH ? s + 1 : 0;
            else s = s ? s - 1 : NUM_SWATCH - 1;
            G.accent[row] = s ? swatch[row][s] : 0;
            pal_refresh();
            colour_row(row);
            sfx(SFX_CURSOR);
        } else if (joy_press & J_A) {
            sfx(SFX_CONFIRM);
            break;
        } else if (joy_press & J_B) {
            for (k = 0; k < NUM_ACCENT; k++) G.accent[k] = old[k];
            pal_refresh();
            sfx(SFX_CANCEL);
            break;
        }
        wait_vbl_done();
    }
}

// ================================================================ blueprints: copy an area
static uint8_t code_at(uint8_t x, uint8_t y, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h) {
    uint16_t i = WIDX(x, y);
    uint8_t a = W_A1[i];
    if (!owned(x, y)) return 0;
    switch (W_CELL[i]) {
    case C_DECOR: return a;
    case C_MACHINE: return PC_MACHINE | (a & 3);
    case C_TREE: return PC_TREE | (a >> 5);
    case C_WATER: return PC_POND;
    case C_STRUCT: {   // a whole building inside the area, at its top-left cell
        Building *b = &G.bld[a];
        if (W_A2(i) || b->type == B_HOUSE) return 0;
        if (b->x + bld_w(b->type) > x0 + w || b->y + bld_h(b->type) > y0 + h) return 0;
        return PC_BLDG | b->type;
    }
    }
    return 0;
}

static uint8_t capture(uint8_t s, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h, uint8_t keep) {
    uint8_t gx, gy, code, np = 0, n = 0, j, k;
    if (keep) {
        memset(G.bp_pal[s], 0, BP_PAL);
        memset(G.bp_cell[s], 0, BP_MAX * BP_MAX / 2);
        G.bp_wh[s] = (uint8_t)(w << 4) | h;
    }
    for (gy = 0; gy < h; gy++)
        for (gx = 0; gx < w; gx++) {
            code = code_at(x0 + gx, y0 + gy, x0, y0, w, h);
            if (!code) continue;
            n++;
            if (!keep) continue;
            for (j = 0; j < np && G.bp_pal[s][j] != code; j++) {}
            if (j == np) {
                if (np == BP_PAL) { n--; continue; }   // a 16th kind of piece: left out
                G.bp_pal[s][np++] = code;
            }
            k = (uint8_t)(gy << 3) | gx;
            G.bp_cell[s][k >> 1] |= (uint8_t)((j + 1) << ((k & 1) ? 4 : 0));
        }
    return n;
}

void bp_copy_reset(void) BANKED { copy_on = 0; }

uint8_t bp_copy_cursor(uint8_t fx, uint8_t fy, uint8_t *x0, uint8_t *y0, uint8_t *w, uint8_t *h) BANKED {
    uint8_t ok = 1, ww, hh;
    if (!copy_on) { *x0 = fx; *y0 = fy; *w = 1; *h = 1; return owned(fx, fy); }
    if (fx < copy_x) { *x0 = fx; ww = copy_x - fx + 1; } else { *x0 = copy_x; ww = fx - copy_x + 1; }
    if (fy < copy_y) { *y0 = fy; hh = copy_y - fy + 1; } else { *y0 = copy_y; hh = fy - copy_y + 1; }
    if (ww > BP_MAX) { ok = 0; if (fx < copy_x) *x0 = copy_x - (BP_MAX - 1); ww = BP_MAX; }
    if (hh > BP_MAX) { ok = 0; if (fy < copy_y) *y0 = copy_y - (BP_MAX - 1); hh = BP_MAX; }
    *w = ww; *h = hh;
    return ok;
}

void bp_copy_click(void) BANKED {
    uint8_t fx, fy, x0, y0, w, h, r, k, n;
    if (!front_cell(&fx, &fy)) return;
    if (!copy_on) {
        copy_on = 1; copy_x = fx; copy_y = fy;
        sfx(SFX_CONFIRM);
        hud_flash("Corner set! Walk");
        return;
    }
    if (!bp_copy_cursor(fx, fy, &x0, &y0, &w, &h)) { sfx(SFX_BUMP); hud_flash("8x8 at the most!"); return; }
    if (!capture(0, x0, y0, w, h, 0)) { sfx(SFX_BUMP); hud_flash("Nothing built here"); return; }
    for (k = 0; k < BP_SLOTS; k++) {
        s_clr(); s_add("Blueprint "); s_num(k + 1, 0);
        if (G.bp_wh[k]) { s_add("  "); s_num(G.bp_wh[k] >> 4, 0); s_ch('x'); s_num(G.bp_wh[k] & 15, 0); }
        else s_add("  empty");
        lbl_set(k);
    }
    ui_begin();
    ui_box(0, 12, 20, 6);
    s_clr(); s_add("Save this "); s_num(w, 0); s_ch('x'); s_num(h, 0); s_add(" area"); ui_text(2, 13, sbuf);
    ui_text(2, 14, "as a blueprint?");
    r = ui_menu(0, 0, 20, lp, BP_SLOTS, 0);
    ui_end();
    if (r == 0xFF) return;
    if (G.bp_wh[r]) {
        s_clr(); s_add("Replace Blueprint "); s_num(r + 1, 0); s_ch('?');
        if (!ui_yesno(sbuf)) return;
    }
    u_forget_slot(r);
    n = capture(r, x0, y0, w, h, 1);
    copy_on = 0;
    G.build_cat = CAT_BLUEPRINT; G.build_sel = 1 + r;   // straight on to stamping it
    music_play(MUS_JINGLE_FANFARE);
    s_clr(); s_add("Saved! "); s_num(n, 0); s_add(" pieces");
    hud_flash(sbuf);
    music_for_scene();
}

// ================================================================ blueprints: the ghost and the stamp
uint8_t ghost_x, ghost_y, ghost_w, ghost_h, ghost_slot;
static uint8_t ghost_ok;

void ghost_hide(void) BANKED {
    uint8_t w = ghost_w;
    if (!w) return;
    ghost_w = 0;
    redraw_rect(ghost_x, ghost_y, w, ghost_h);
}

uint8_t bp_ghost(uint8_t slot, uint8_t x0, uint8_t y0) BANKED {
    uint8_t wh = G.bp_wh[slot], gx, gy, code, kind, arg;
    if (ghost_w && ghost_x == x0 && ghost_y == y0 && ghost_slot == slot) return ghost_ok;
    ghost_hide();
    ghost_slot = slot; ghost_x = x0; ghost_y = y0; ghost_h = wh & 15;
    ghost_ok = 0;
    for (gy = 0; gy < ghost_h && !ghost_ok; gy++)
        for (gx = 0; gx < (wh >> 4); gx++) {
            code = bp_at(slot, gx, gy);
            if (!code) continue;
            kind = code_kind(code, &arg);
            if (piece_fits(x0 + gx, y0 + gy, kind, arg)) { ghost_ok = 1; break; }
        }
    ghost_w = wh >> 4;
    redraw_rect(x0, y0, ghost_w, ghost_h);
    return ghost_ok;
}

void bp_stamp(uint8_t slot, uint8_t x0, uint8_t y0) BANKED {
    uint8_t wh = G.bp_wh[slot], gx, gy, n = 0, fit = 0, skip = 0, code, kind, arg, w, s, pass, np = 0, k, x, y;
    uint16_t g, tw, ts;
    uint32_t tg;
    uint8_t rec[UHDR + 8 + BP_MAX * BP_MAX / 2];
    for (pass = 0; pass < 2; pass++) {
        tg = 0; tw = 0; ts = 0;
        for (gy = 0; gy < (wh & 15); gy++)
            for (gx = 0; gx < (wh >> 4); gx++) {
                code = bp_at(slot, gx, gy);
                if (!code) continue;
                kind = code_kind(code, &arg);
                x = x0 + gx; y = y0 + gy;
                if (!piece_fits(x, y, kind, arg) || (kind == BK_BLDG && !bld_free())) { skip++; continue; }
                fit++;
                bk_cost(kind, arg, &g, &w, &s);
                if (G.gold < tg + g || G.wood < tw + w || G.stone < ts + s) { skip++; continue; }
                if (!pass) { tg += g; tw += w; ts += s; n++; continue; }
                {   // pass 2: build it (paid as it goes, so the affordability check stays live)
                    uint16_t i = WIDX(x, y);
                    uint8_t p = prior_of(i);
                    if (kind == BK_BLDG) {
                        if (bld_new(arg, x, y, 0) == 0xFF) continue;
                        redraw_rect(x, y, bld_w(arg), bld_h(arg));
                    } else bk_put(x, y, kind, arg);
                    G.gold -= g; G.wood -= w; G.stone -= s;
                    k = (uint8_t)(gy << 3) | gx;
                    rec[UHDR + (k >> 3)] |= (uint8_t)(1 << (k & 7));
                    rec[UHDR + 8 + (np >> 1)] |= (np & 1) ? (uint8_t)(p << 4) : p;
                    np++;
                }
            }
        if (!pass) {
            if (!n) { sfx(SFX_BUMP); hud_flash(fit ? "Need materials." : "Nothing fits here."); return; }
            s_clr(); s_add("Stamp "); s_num(n, 0);   // two dialog lines: what, then the bill
            if (skip) { s_add(" of "); s_num(n + skip, 0); s_ch('?'); }
            else s_add(n == 1 ? " piece?" : " pieces?");
            s_ch('\n');
            cost_str(tg, tw, ts);
            if (!ui_yesno(sbuf)) return;
            memset(rec, 0, sizeof(rec));
            rec[0] = U_STAMP; rec[1] = slot; rec[2] = x0; rec[3] = y0;
        }
    }
    if (np) u_record(rec, UHDR + 8 + ((np + 1) >> 1));
    ghost_hide();
    sfx(SFX_HIT);
    fx_show(SPR_DUST, SPR_DUST_PAL, x0, y0);
    s_clr(); s_add("Stamped "); s_num(np, 0); s_add(" pieces");
    hud_flash(sbuf);
    goals_check();
}
