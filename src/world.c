// Stilwell Farms -- the farm world: generation, land plots, buildings, overnight (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "decor.h"

BANKREF(world_new)

// ---------------------------------------------------------------- land plots
static const char *const plot_names[16] = {
    "Pine Woods", "North Meadow", "Quarry Hill", "Old Orchard",
    "Birch Grove", "Home Field", "Home Field", "Wildflower Hill",
    "Lakeside", "Home Field", "Home Field", "River Bend",
    "Willow Marsh", "South Pasture", "Sunny Field", "Rocky Hollow"};
static const uint16_t plot_prices[16] = {
    4500, 2800, 10000, 7000,
    4000, 0, 0, 3200,
    9000, 0, 0, 8000,
    5000, 1500, 2200, 15000};

void plot_name_add(uint8_t p) BANKED { s_add(plot_names[p & 15]); }
uint16_t plot_price(uint8_t p) BANKED { return plot_prices[p & 15]; }

void buy_plot_prompt(uint8_t p) BANKED {
    s_clr(); s_add(plot_names[p]); s_add(" is for sale! Buy it for G"); s_num(plot_prices[p], 0); s_ch('?');
    if (!ui_yesno(sbuf)) return;
    if (!pay(plot_prices[p], 0, 0)) return;
    G.plots |= (uint16_t)1 << p;
    sfx(SFX_COIN);
    music_play(MUS_JINGLE_BUY);
    {
        uint8_t x0 = (p & 3) << 3, y0 = (p >> 2) << 3;
        redraw_rect(x0 ? x0 - 1 : 0, y0 ? y0 - 1 : 0, 10, 10);
    }
    s_clr(); s_add(plot_names[p]); s_add(" is yours! Your land keeps growing.");
    say(sbuf);
    music_for_scene();
    goals_check();
}

// ---------------------------------------------------------------- buildings
static const uint8_t bw[NUM_BTYPES] = {3, 3, 2, 3, 1, 2, 3, 2, 2, 2};
static const uint8_t bh[NUM_BTYPES] = {3, 3, 2, 3, 2, 2, 2, 3, 2, 2};
uint8_t bld_w(uint8_t type) BANKED { return bw[type]; }
uint8_t bld_h(uint8_t type) BANKED { return bh[type]; }

uint8_t bld_door(uint8_t slot) BANKED {  // bottom-middle (coop: bottom-left)
    uint8_t t = G.bld[slot].type, w = bw[t], h = bh[t];
    if (t == B_COOP) return (h - 1) * w;
    return (h - 1) * w + (w >> 1);
}

uint8_t bld_find(uint8_t type) BANKED {
    uint8_t k;
    for (k = 0; k < MAX_BLD; k++)
        if (G.bld[k].type == type) return k;
    return 0xFF;
}

static void stamp(uint8_t slot) {
    Building *b = &G.bld[slot];
    uint8_t x, y, p = 0;
    for (y = 0; y < bh[b->type]; y++)
        for (x = 0; x < bw[b->type]; x++) {
            uint16_t i = WIDX(b->x + x, b->y + y);
            G.cell[i] = C_STRUCT; G.a1[i] = slot; G.a2[i] = p++;
        }
}

uint8_t bld_new(uint8_t type, uint8_t x, uint8_t y, uint8_t flags) BANKED {
    uint8_t k;
    for (k = 0; k < MAX_BLD; k++)
        if (G.bld[k].type == 0xFF) {
            G.bld[k].type = type; G.bld[k].x = x; G.bld[k].y = y; G.bld[k].flags = flags;
            stamp(k);
            return k;
        }
    return 0xFF;
}

void bld_remove(uint8_t slot) BANKED {
    Building *b = &G.bld[slot];
    uint8_t x, y;
    for (y = 0; y < bh[b->type]; y++)
        for (x = 0; x < bw[b->type]; x++) {
            uint16_t i = WIDX(b->x + x, b->y + y);
            G.cell[i] = C_GRASS; G.a1[i] = 0; G.a2[i] = 0;
        }
    b->type = 0xFF;
}

// ---------------------------------------------------------------- autotiles
static uint8_t fence_kind(int8_t x, int8_t y) {
    uint16_t i;
    if (x < 0 || y < 0 || x >= WW || y >= WH) return 0;
    i = WIDX(x, y);
    if (G.cell[i] != C_DECOR) return 0;
    return decor[G.a1[i]].fence;
}

void fix_autotile(uint8_t x, uint8_t y) BANKED {
    int8_t dx, dy;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
            int8_t cx = x + dx, cy = y + dy;
            uint8_t f;
            if (cx < 0 || cy < 0 || cx >= WW || cy >= WH) continue;
            f = fence_kind(cx, cy);
            if (f) {
                uint8_t h = fence_kind(cx - 1, cy) == f || fence_kind(cx + 1, cy) == f;
                uint8_t v = fence_kind(cx, cy - 1) == f || fence_kind(cx, cy + 1) == f;
                G.a2[WIDX(cx, cy)] = h ? 1 : (v ? 2 : 0);
            }
            redraw_cell(cx, cy);  // water edges and fences around the change
        }
}

// ---------------------------------------------------------------- world generation
static void setc(uint8_t x, uint8_t y, uint8_t c, uint8_t a1, uint8_t a2) {
    uint16_t i = WIDX(x, y);
    G.cell[i] = c; G.a1[i] = a1; G.a2[i] = a2;
}

static void gen_plot(uint8_t p) {
    uint8_t x0 = (p & 3) << 3, y0 = (p >> 2) << 3, x, y, r, c, a;
    for (y = y0; y < y0 + 8; y++)
        for (x = x0; x < x0 + 8; x++) {
            r = rnd() % 100;
            a = 0;
            switch (p) {
            case 0:  // pine woods
                c = r < 55 ? C_PINE : r < 62 ? C_STUMP : r < 68 ? C_BUSH : r < 72 ? C_FORAGE : C_GRASS2;
                if (c == C_FORAGE) a = 1;
                break;
            case 4:  // birch grove
                c = r < 45 ? C_BIRCH : r < 52 ? C_SAPLING_WILD : r < 58 ? C_FORAGE : r < 62 ? C_STUMP : C_GRASS;
                if (c == C_FORAGE) a = 1;
                break;
            case 1: case 13: case 14:  // meadows
                c = r < 18 ? C_WILDFLOWER : r < 30 ? C_WEEDS : r < 34 ? C_ROCK : r < 50 ? C_GRASS2 : C_GRASS;
                break;
            case 7:  // wildflower hill
                c = r < 40 ? C_WILDFLOWER : r < 48 ? C_FORAGE : r < 54 ? C_BUSH : C_GRASS;
                if (c == C_FORAGE) a = 2;
                break;
            case 3:  // old orchard
                c = (((x - x0) % 3) == 1 && ((y - y0) % 3) == 1) ? C_TREE : (r < 20 ? C_WEEDS : C_GRASS);
                if (c == C_TREE) a = (uint8_t)((rnd() & 3) << 5);
                break;
            case 2: case 15:  // quarry / rocky hollow
                if (y == y0 && p == 2) c = C_CLIFF;
                else c = r < 30 ? C_ROCK : r < 40 ? C_ORE : r < 46 ? C_BOULDER : r < 70 ? C_SAND : C_GRASS;
                if (c == C_ORE) { r = rnd(); a = r < 140 ? 0 : r < 210 ? 1 : r < 245 ? 2 : 3; }
                break;
            case 12:  // willow marsh
                c = r < 25 ? C_WATER : r < 40 ? C_WEEDS : r < 48 ? C_FORAGE : r < 55 ? C_BUSH : C_GRASS2;
                if (c == C_FORAGE) a = 1;
                break;
            default:  // home field: lovingly overgrown
                c = r < 34 ? C_WEEDS : r < 44 ? C_ROCK : r < 51 ? C_STUMP : r < 61 ? C_GRASS2 : r < 65 ? C_WILDFLOWER : C_GRASS;
                break;
            }
            setc(x, y, c, a, c == C_TREE ? 30 : 0);
        }
    if (p == 8) {  // lakeside: a big round lake with a sandy shore
        for (y = y0; y < y0 + 8; y++)
            for (x = x0; x < x0 + 8; x++) {
                int8_t dx = (int8_t)x - (x0 + 3), dy = (int8_t)y - (y0 + 4);
                uint8_t d2 = dx * dx + dy * dy;
                if (d2 <= 8) setc(x, y, C_WATER, 0, 0);
                else if (d2 <= 12) setc(x, y, C_SAND, 0, 0);
                else setc(x, y, (rnd() & 3) ? C_GRASS : C_PINE, 0, 0);
            }
    }
    if (p == 11) {  // river bend
        for (y = y0; y < y0 + 8; y++) {
            uint8_t rx = x0 + 3 + ((y - y0) > 3 ? 1 : 0);
            setc(rx, y, C_WATER, 0, 0);
            setc(rx + 1, y, C_WATER, 0, 0);
            setc(rx - 1, y, C_GRASS, 0, 0);
            setc(rx + 2, y, C_GRASS, 0, 0);
        }
    }
}

void world_new(void) BANKED {
    uint8_t p, x, y;
    memset(&G, 0, sizeof(G));
    G.rng_lo = 0x34; G.rng_hi = 0x12;  // same lovingly overgrown land every new game
    for (p = 0; p < 16; p++) gen_plot(p);
    for (p = 0; p < MAX_BLD; p++) G.bld[p].type = 0xFF;
    for (p = 0; p < MAX_ANIMALS; p++) G.anim[p].kind = 0xFF;
    G.plots = START_PLOTS;
    // the public road to town
    for (y = 12; y < WH; y++) { setc(ROAD_X, y, y >= 24 ? C_ROAD : C_DIRT, 0, 0); setc(ROAD_X + 1, y, y >= 24 ? C_ROAD : C_DIRT, 0, 0); }
    for (x = 10; x < ROAD_X; x++) setc(x, 12, C_DIRT, 0, 0);
    // a friendly clearing by the house
    for (y = 12; y <= 14; y++)
        for (x = 8; x <= 13; x++) if (G.cell[WIDX(x, y)] != C_DIRT) setc(x, y, (x + y) & 3 ? C_GRASS : C_GRASS2, 0, 0);
    for (x = 8; x <= 14; x++) for (y = 8; y <= 11; y++) setc(x, y, C_GRASS, 0, 0);
    // big obstacles that need better tools
    setc(13, 17, C_BOULDER, 0, 0); setc(18, 15, C_BOULDER, 0, 0); setc(21, 22, C_BOULDER, 0, 0);
    setc(17, 19, C_LOG, 0, 0); setc(11, 17, C_LOG, 0, 0); setc(22, 13, C_LOG, 0, 0);
    // grandpa's buildings
    bld_new(B_HOUSE, 9, 9, 0);
    bld_new(B_COOP, 17, 9, BF_RUIN);
    bld_new(B_BARN, 20, 9, BF_RUIN);
    bld_new(B_GREENHOUSE, 20, 18, BF_RUIN);
    for (x = 17; x <= 22; x++) setc(x, 12, C_GRASS, 0, 0);
    setc(21, 21, C_GRASS, 0, 0);
    setc(12, 11, C_DECOR, DK_BIN, 0);
    setc(13, 10, C_DECOR, DK_WELL, 0);
    setc(14, 11, C_DECOR, DK_SIGN, 0);
    setc(12, 9, C_DECOR, DK_MAILBOX, 0);

    G.gold = 500;
    G.day = 1; G.season = SEASON_SPRING; G.year = 1;
    G.time = 360;
    G.energy = G.energy_max = 100;
    G.px = 10; G.py = 12; G.facing = D_DOWN;
    G.scene = SCENE_FARM;
    G.tool = T_HAND;
    G.seeds[1][R_COMMON] = 6; G.seed_sel = 1;
    G.water = 30;
    G.tomorrow = 0;
    G.opt_music = G.opt_sfx = 1;
    G.opt_text = 1;
    G.opt_clock = 1;
    strcpy(G.farm_name, "Stilwell");
    strcpy(G.pet_name, "Buddy");   // pet_name[6]: 5 letters max
    G.rng_lo = DIV_REG | 1; G.rng_hi = sys_time;
}

// ---------------------------------------------------------------- overnight
static uint8_t m_crows, m_season, m_passed, m_storm, m_fairy, m_giant, m_produce;
static uint32_t m_income;
static uint8_t m_fruit;   // rarity: a fruit tree ripened overnight

static uint8_t near_decor(uint16_t i, uint8_t kind, uint8_t r) {
    int8_t x = i & 31, y = i >> 5, dx, dy;
    for (dy = -r; dy <= (int8_t)r; dy++)
        for (dx = -r; dx <= (int8_t)r; dx++) {
            int8_t cx = x + dx, cy = y + dy;
            uint16_t j;
            if (cx < 0 || cy < 0 || cx >= WW || cy >= WH) continue;
            j = WIDX(cx, cy);
            if (G.cell[j] == C_DECOR && G.a1[j] == kind) return 1;
        }
    return 0;
}

#define MAX_SCARE 24
static uint8_t scared(uint8_t x, uint8_t y, const uint8_t *sx, const uint8_t *sy, uint8_t ns) {
    uint8_t k;
    for (k = 0; k < ns; k++) {
        uint8_t dx = x - sx[k] + 4, dy = y - sy[k] + 4;   // within 4 both ways <=> 0..8 after the offset
        if (dx <= 8 && dy <= 8) return 1;
    }
    return 0;
}

// G.cell, G.a1 and G.a2 are consecutive 1024-byte arrays: pc[A1] / pc[A2] are a cell's a1 / a2
#define A1 1024
#define A2 2048

static void water_around(uint16_t i, uint8_t r) {   // clamped box, unsigned (was ~250 cycles a cell)
    uint8_t x = (uint8_t)i & 31, y = (uint8_t)(i >> 5), cx, cy, w;
    uint8_t x0 = x >= r ? x - r : 0, x1 = x + r > WW - 1 ? WW - 1 : x + r;
    uint8_t y0 = y >= r ? y - r : 0, y1 = y + r > WH - 1 ? WH - 1 : y + r;
    w = x1 - x0 + 1;
    for (cy = y0; cy <= y1; cy++) {
        uint8_t *pc = &G.cell[WIDX(x0, cy)];
        for (cx = w; cx; cx--, pc++)
            if (*pc == C_SOIL) pc[A1] |= SOIL_WET;
    }
}

// rarity: two happy adults of a kind sharing a building may have a baby (named in the morning).
// The baby takes the better parent's breed, sometimes the other's, and now and then one tier better.
static uint8_t m_baby, m_mom, m_dad;   // m_baby = animal slot + 1 (0 none)
static void breed_overnight(void) {
    uint8_t b, k, mom, dad, n, free_k, t, br;
    m_baby = 0;
    for (b = 0; b < MAX_BLD && !m_baby; b++) {
        t = G.bld[b].type;
        if ((t != B_BARN && t != B_COOP) || (G.bld[b].flags & BF_RUIN)) continue;
        for (t = A_COW; t <= A_SHEEP; t++) {
            mom = dad = 0xFF; n = 0; free_k = 0xFF;
            for (k = 0; k < MAX_ANIMALS; k++) {
                Animal *a = &G.anim[k];
                if (a->kind == 0xFF) { if (free_k == 0xFF) free_k = k; continue; }
                if (a->home != b) continue;
                n++;
                if (a->kind != t || a->age < ADULT_AGE || a->happy < 180) continue;
                if (mom == 0xFF) mom = k; else if (dad == 0xFF) dad = k;
            }
            if (dad == 0xFF || free_k == 0xFF || n >= (G.bld[b].type == B_COOP ? 6 : BARN_CAP) || rnd() >= 40) continue;
            {
                uint8_t bm = BREED(&G.anim[mom]), bd = BREED(&G.anim[dad]), r = rnd();
                br = bm > bd ? bm : bd;
                if (r < 16) { if (br < R_LEGEND) br++; }
                else if (r >= 170) br = bm < bd ? bm : bd;
            }
            {
                Animal *c = &G.anim[free_k];
                c->kind = t; c->home = b; c->happy = 150; c->age = 0; c->flags = br << 6;
                strcpy(c->name, "Baby");
            }
            m_baby = free_k + 1; m_mom = mom; m_dad = dad;
            break;
        }
    }
}

// overnight pass 1, one cell. Out of line so the 1024-cell loop stays a few instructions: SDCC hoisted
// every branch's address math to the top of the inline version (~5 frames for the pass on a big farm).
static const uint8_t grows[32] = {   // cell types pass 1 touches: grass, grass2, soil, decor, tree, bush, sand, machine, wild sapling
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0};
static uint8_t g_crops, g_rod;
static void grow(uint8_t *pc, uint16_t i) {
    uint8_t c = *pc, a1 = pc[A1], a2 = pc[A2], crop;
    switch (c) {
    case C_SOIL:
        crop = a1 & SOIL_CROP;
        if (crop && crop != CROP_WITHERED) {
            if (g_crops < 255) g_crops++;
            if (((a1 & SOIL_WET) || m_fairy) && (a2 & SOIL_DAYS) < crops[crop].days) pc[A2] = a2 + 1;
        }
        pc[A1] = a1 & ~SOIL_WET;
        break;
    case C_TREE:
        if (a2 < 250) pc[A2] = ++a2;
        if (a2 >= 10 && (a1 & 31) < TREE_RIPE_DAYS && season_ok_tree(a1 >> 5)) {
            pc[A1] = ++a1;
            if ((a1 & 31) == TREE_RIPE_DAYS) m_fruit = 1;
        }
        break;
    case C_BUSH:
        if (a1 < 9) pc[A1] = a1 + 1;
        break;
    case C_SAPLING_WILD:
        if (++a2 >= 8) { *pc = PLOT_OF(i & 31, i >> 5) == 4 ? C_BIRCH : C_PINE; a2 = 0; }
        pc[A2] = a2;
        break;
    case C_DECOR:
        if (a1 == DK_BEEHIVE && a2 < 3) pc[A2] = a2 + 1;
        if (a1 == DK_ROD) g_rod = 1;
        break;
    case C_MACHINE:
        if (a2 && a2 < 0x80) {
            if ((a1 >> 2) <= 1) { pc[A1] = a1 & 3; pc[A2] = a2 | 0x80; }
            else pc[A1] = a1 - 4;
        }
        break;
    default: {   // grass, sand
        uint8_t x = (uint8_t)i & 31, y = (uint8_t)(i >> 5);
        if (owned(x, y)) {
            if (c != C_SAND && rnd() < 2) { *pc = C_WEEDS; pc[A1] = 1; }   // a1 = 1: regrown weed (the groundskeeper's job)
        } else if (c != C_GRASS2) {
            uint8_t p = PLOT_OF(x, y);
            if ((p == 0 || p == 4 || p == 7 || p == 12) && rnd() < 3) { *pc = C_FORAGE; pc[A1] = rnd() % 3; }
            if ((p == 2 || p == 15) && rnd() < 3) { *pc = C_ORE; pc[A1] = rnd() < 200 ? 0 : 1; }
        }
    }
    }
}

static uint8_t ripe_pumpkin(const uint8_t *pc) {
    return *pc == C_SOIL && (pc[A1] & SOIL_CROP) == 10 && (pc[A2] & SOIL_DAYS) >= crops[10].days;
}

void world_overnight(uint8_t passed_out) BANKED {
    uint16_t i;
    uint8_t c, crop, n = 0, k;
    uint8_t has_rod = 0, green = 0;
    m_crows = m_season = m_storm = m_fairy = m_giant = m_produce = 0;
    m_passed = passed_out;
    m_fruit = 0;
    for (k = 0; k < MAX_BLD; k++)
        if (G.bld[k].type == B_GREENHOUSE && !(G.bld[k].flags & BF_RUIN)) green = 1;
    m_fairy = rnd() < 6;
    // 1. growth and nature: a tight scan, with the per-cell work out of line (see grow)
    {
        uint8_t *pc = G.cell;
        g_crops = 0; g_rod = 0;
        for (i = 0; i < 1024; i++, pc++)
            if (grows[*pc & 31]) grow(pc, i);
        n = g_crops; has_rod = g_rod;
    }
    // 2. giant pumpkins: a ripe 2x2 block may fuse
    for (i = 0; i < 1024 - 33; i++) {
        uint8_t *pc = &G.cell[i];
        if (*pc != C_SOIL || (pc[A1] & SOIL_CROP) != 10 || (i & 31) == 31) continue;
        if (ripe_pumpkin(pc) && ripe_pumpkin(pc + 1) && ripe_pumpkin(pc + 32) && ripe_pumpkin(pc + 33) && rnd() < 64) {
            G.cell[i] = G.cell[i + 1] = G.cell[i + 32] = G.cell[i + 33] = C_GIANT;
            G.a1[i] = 0; G.a1[i + 1] = 1; G.a1[i + 32] = 2; G.a1[i + 33] = 3;
            m_giant = 1;
        }
    }
    // 3. crows visit bigger farms without scarecrows
    if (n >= 6 && rnd() < 90) {
        uint16_t start = ((uint16_t)rnd() << 2), j;
        // list the scarecrows once (a 9x9 scan per crop was up to 80k cell reads on a guarded farm)
        uint8_t sx[MAX_SCARE], sy[MAX_SCARE], ns = 0;
        for (j = 0; j < 1024; j++)
            if (G.cell[j] == C_DECOR && G.a1[j] == DK_SCARECROW) {
                if (ns == MAX_SCARE) { ns = 0xFF; break; }
                sx[ns] = (uint8_t)j & 31; sy[ns] = (uint8_t)(j >> 5); ns++;
            }
        for (j = 0; j < 1024; j++) {
            i = (start + j) & 1023;
            crop = G.a1[i] & SOIL_CROP;
            if (G.cell[i] == C_SOIL && crop && crop != CROP_WITHERED &&
                !(ns == 0xFF ? near_decor(i, DK_SCARECROW, 4) : scared((uint8_t)i & 31, (uint8_t)(i >> 5), sx, sy, ns))) {
                G.a1[i] &= SOIL_WET | SOIL_FERT; G.a2[i] = 0;
                m_crows = 1;
                break;
            }
        }
    }
    // 4. storms: lightning finds a crop unless a lightning rod stands anywhere on the farm
    if (G.weather == 3 && !has_rod && n && rnd() < 160) {
        uint16_t start = ((uint16_t)rnd() << 2), j;
        for (j = 0; j < 1024; j++) {
            i = (start + j) & 1023;
            if (G.cell[i] == C_SOIL && (G.a1[i] & SOIL_CROP)) {
                G.a1[i] = (G.a1[i] & SOIL_FERT) | CROP_WITHERED;
                m_storm = 1;
                break;
            }
        }
    }
    // 5. calendar
    if (++G.day > 28) {
        G.day = 1;
        G.season = (G.season + 1) & 3;
        if (G.season == SEASON_SPRING) G.year++;
        m_season = 1;
        if (!green)
            for (i = 0; i < 1024; i++) {
                crop = G.a1[i] & SOIL_CROP;
                if (G.cell[i] == C_SOIL && crop && crop != CROP_WITHERED && crops[crop].season != G.season)
                    G.a1[i] = (G.a1[i] & (SOIL_WET | SOIL_FERT)) | CROP_WITHERED;
            }
    }
    // 6. weather: today is yesterday's forecast; roll tomorrow
    G.weather = G.tomorrow;
    if (G.season == SEASON_WINTER) G.tomorrow = rnd() < 70 ? 2 : 0;
    else {
        uint8_t r = rnd();
        G.tomorrow = r < 50 ? 1 : (G.season == SEASON_SUMMER && r < 70 ? 3 : 0);
    }
    {
        uint8_t rain = G.weather == 1 || G.weather == 3, *pc = G.cell, *p1 = G.a1;
        for (i = 0; i < 1024; i++, pc++, p1++) {
            c = *pc;
            if (c == C_SOIL && rain) *p1 |= SOIL_WET;
            else if (c == C_DECOR) {
                if (*p1 == DK_SPRINKLER) water_around(i, 1);
                else if (*p1 == DK_QSPRINKLER) water_around(i, 2);
            }
        }
    }
    // 7. animals: fed ones give produce (quality from happiness); happiness drifts
    for (k = 0; k < MAX_ANIMALS; k++) {
        Animal *a = &G.anim[k];
        if (a->kind == 0xFF) continue;
        if (a->flags & AF_FED) {
            if (a->happy < 235) a->happy += 20;
            if (a->age < ADULT_AGE) ;   // babies just grow
            else if (a->kind != A_SHEEP) { a->flags |= AF_READY; m_produce = 1; }
            else if ((a->flags & AF_WOOL) >= ((WOOL_DAYS - 1) << 3)) { a->flags = (a->flags & ~AF_WOOL) | AF_READY; m_produce = 1; }
            else if (!(a->flags & AF_READY)) a->flags += 1 << 3;   // fleece grows while fed
        }
        else if (a->happy > 30) a->happy -= 30;
        else a->happy = 0;
        if (a->flags & AF_PET) { if (a->happy < 245) a->happy += 10; }
        a->flags &= AF_READY | AF_BREED | AF_WOOL;
        if (a->age < 255) a->age++;
    }
    breed_overnight();
    G.pet_flags = 0;
    G.talked = G.gifted = 0;
    // farmhands: wages come out of gold first, then they water/harvest/ship/tend/weed
    staff_overnight();
    // 8. money, rest, reset
    biz_overnight();
    m_income = G.ship_value;
    G.gold += G.ship_value;
    G.earned += G.ship_value;
    G.ship_value = 0;
    if (G.req_qty && G.req_day && --G.req_day == 0) G.req_qty = 0;
    G.energy = passed_out ? G.energy_max >> 1 : G.energy_max;
    G.time = 360;
    {
        uint8_t h = bld_find(B_HOUSE);
        if (h != 0xFF) { G.px = G.bld[h].x + 1; G.py = G.bld[h].y + bld_h(B_HOUSE); }
    }
    G.facing = D_DOWN;
    G.scene = SCENE_FARM;
    build_cancel();
    goals_check();
    save_write();
}

void morning_report(void) BANKED {
    static const char *const wx[4] = {"Sunny", "Rainy: crops are watered", "Snowy", "Stormy! Watch for lightning"};
    static const char *const fc[4] = {"sun", "rain", "snow", "a thunderstorm"};
    s_clr();
    s_add(season_name[G.season]); s_ch(' '); s_num(G.day, 0); s_add(", Year "); s_num(G.year, 0); s_add(". ");
    s_add(wx[G.weather]); s_add(". Tomorrow: "); s_add(fc[G.tomorrow]); s_ch('.');
    say(sbuf);
    if (m_passed) say("You overslept and feel worn out.");
    if (!(G.tips & 0x20)) {   // first morning
        G.tips |= 0x20;
        say("Tip: the game saves every morning. Get to bed before 2am, or you'll wake up worn out.");
    }
    if (m_season) {
        s_clr(); s_add("It's "); s_add(season_name[G.season]); s_add(" now!");
        say(sbuf);
    }
    staff_report();
    if (m_income) {
        s_clr(); s_add("Shipping paid G"); s_num(m_income, 0); s_add(" overnight!");
        say(sbuf);
    }
    biz_report();
    if (m_fairy) say("A crop fairy visited in the night... everything grew a little!");
    if (m_giant) say("Whoa! Your pumpkins grew into a GIANT pumpkin!");
    if (m_crows) say("Crows pecked at a crop overnight! A scarecrow would help.");
    if (m_storm) say("Lightning struck a crop in the storm! A lightning rod protects the whole farm.");
    if (m_produce) say("Your animals have produce waiting for you.");
    if (m_baby && G.anim[m_baby - 1].kind != 0xFF) {
        Animal *c = &G.anim[m_baby - 1];
        static const char *const kind_w[3] = {"calf", "chick", "lamb"};
        static const char *const breed_w[NUM_RARITY] = {"", "rare ", "GOLDEN "};
        s_clr(); s_add(G.anim[m_mom].name); s_add(" and "); s_add(G.anim[m_dad].name); s_add(" had a ");
        s_add(breed_w[BREED(c)]); s_add(kind_w[c->kind % 3]); s_add("! It will be grown in a week.");
        music_play(MUS_JINGLE_FANFARE);
        say(sbuf);
        music_for_scene();
        s_clr(); s_add("Name the "); s_add(kind_w[c->kind % 3]);   // prompt lives in sbuf: ui_name is in another bank
        ui_name(sbuf, c->name, 5);
        m_baby = 0;
        save_write();
    }
    if (m_fruit && G.season != SEASON_WINTER) say("Fresh fruit is ripe on your fruit trees!");
    if (G.mail) say("There's a letter in your mailbox!");
}
