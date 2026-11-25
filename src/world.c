// Stilwell Farms -- the farm world: generation, land plots, buildings, overnight (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "decor.h"

BANKREF(world_new)

// ---------------------------------------------------------------- land plots
// The valley is an 8x8 grid of 8x8-cell plots. The middle 4x4 plots are the old home valley (every save
// before v5 was exactly this, and migrates onto it); around it, more woods, a big lake, the river that
// runs down the east side, orchards, meadows and quarries. Plots share their region's name.
enum {
    RG_HOME, RG_PINES, RG_NMEADOW, RG_QUARRY, RG_OORCHARD, RG_BIRCH, RG_WILDFLOWER, RG_LAKESIDE,
    RG_RIVERBEND, RG_WILLOW, RG_SPASTURE, RG_SUNNY, RG_ROCKY,
    RG_OLDGROWTH, RG_RIDGE, RG_HIGHMEADOW, RG_APPLE, RG_FERN, RG_MIRROR, RG_REED, RG_BRAMBLE,
    RG_CLOVER, RG_MILL, RG_BARRENS, RG_FARBANK, RG_CHERRY, RG_BROOK, NUM_RG
};
static const char *const rg_name[NUM_RG] = {
    "Home Field", "Pine Woods", "North Meadow", "Quarry Hill", "Old Orchard", "Birch Grove",
    "Wildflower Hill", "Lakeside", "River Bend", "Willow Marsh", "South Pasture", "Sunny Field",
    "Rocky Hollow", "Old Growth", "Granite Ridge", "High Meadow", "Apple Hills", "Fern Hollow",
    "Mirror Lake", "Reed Shore", "Bramble Wood", "Clover Downs", "Mill Fields", "Stone Barrens",
    "Far Bank", "Cherry Slope", "Brookside"};
static const uint8_t rg_kind[NUM_RG] = {
    RK_HOME, RK_PINE, RK_MEADOW, RK_QUARRY, RK_ORCHARD, RK_BIRCH, RK_FLOWER, RK_LAKE,
    RK_RIVER, RK_MARSH, RK_MEADOW, RK_MEADOW, RK_ROCKY,
    RK_PINE, RK_QUARRY, RK_FLOWER, RK_ORCHARD, RK_BIRCH, RK_LAKE, RK_MARSH, RK_PINE,
    RK_MEADOW, RK_MEADOW, RK_ROCKY, RK_MEADOW, RK_ORCHARD, RK_MEADOW};
static const uint8_t plot_rg[NUM_PLOTS] = {
    RG_OLDGROWTH, RG_OLDGROWTH, RG_RIDGE, RG_RIDGE, RG_HIGHMEADOW, RG_HIGHMEADOW, RG_APPLE, RG_APPLE,
    RG_OLDGROWTH, RG_FERN, RG_RIDGE, RG_BROOK, RG_BROOK, RG_HIGHMEADOW, RG_APPLE, RG_APPLE,
    RG_FERN, RG_FERN, RG_PINES, RG_NMEADOW, RG_QUARRY, RG_OORCHARD, RG_FARBANK, RG_FARBANK,
    RG_MIRROR, RG_MIRROR, RG_BIRCH, RG_HOME, RG_HOME, RG_WILDFLOWER, RG_FARBANK, RG_FARBANK,
    RG_MIRROR, RG_MIRROR, RG_LAKESIDE, RG_HOME, RG_HOME, RG_RIVERBEND, RG_CHERRY, RG_CHERRY,
    RG_REED, RG_REED, RG_WILLOW, RG_SPASTURE, RG_SUNNY, RG_ROCKY, RG_CHERRY, RG_CHERRY,
    RG_BRAMBLE, RG_BRAMBLE, RG_CLOVER, RG_CLOVER, RG_MILL, RG_MILL, RG_BARRENS, RG_BARRENS,
    RG_BRAMBLE, RG_BRAMBLE, RG_CLOVER, RG_CLOVER, RG_MILL, RG_MILL, RG_BARRENS, RG_BARRENS};
// the middle 4x4 are yours; the outer ring costs more the further and richer it is (docs/BALANCE.md: land)
static const uint16_t plot_prices[NUM_PLOTS] = {
    18000, 19500, 48000, 45000, 16500, 18000, 33000, 36000,
    18000, 15000, 42000, 13500, 13500, 16500, 33000, 30000,
    15000, 14500, 13500, 8500, 30000, 21000, 19500, 21000,
    36000, 33000, 12000, 0, 0, 9500, 18000, 19500,
    36000, 33000, 27000, 0, 0, 24000, 30000, 33000,
    19500, 18000, 15000, 4500, 6500, 45000, 30000, 33000,
    18000, 19500, 10500, 9000, 9000, 10500, 45000, 48000,
    21000, 22500, 12000, 10500, 10500, 12000, 51000, 54000};

static uint8_t kind_of(uint8_t p) { return rg_kind[plot_rg[p & 63]]; }
uint8_t plot_kind(uint8_t p) BANKED { return kind_of(p); }
void plot_name_add(uint8_t p) BANKED { s_add(rg_name[plot_rg[p & 63]]); }
uint8_t plot_region(uint8_t p) BANKED { return plot_rg[p & 63]; }
uint16_t plot_price(uint8_t p) BANKED { return plot_prices[p & 63]; }

uint8_t plots_owned(void) BANKED {
    uint8_t k, b, n = 0;
    for (k = 0; k < 8; k++)
        for (b = G.plots[k]; b; b >>= 1) n += b & 1;
    return n;
}

void buy_plot_prompt(uint8_t p) BANKED {
    p &= 63;
    s_clr(); plot_name_add(p); s_add(" is for sale! Buy it for G"); s_num(plot_prices[p], 0); s_ch('?');
    if (!ui_yesno(sbuf)) return;
    if (!pay(plot_prices[p], 0, 0)) return;
    G.plots[p >> 3] |= (uint8_t)(1 << (p & 7));
    sfx(SFX_COIN);
    music_play(MUS_JINGLE_BUY);
    {
        uint8_t x0 = (p & 7) << 3, y0 = (p >> 3) << 3;
        redraw_rect(x0 ? x0 - 1 : 0, y0 ? y0 - 1 : 0, 10, 10);
    }
    s_clr(); plot_name_add(p); s_add(" is yours! Your land keeps growing.");
    say(sbuf);
    music_for_scene();
    goals_check();
}

// ---------------------------------------------------------------- owned land, for the nightly scans
// Owned land as spans along each row (neighbouring plots merged), for the nightly scans: soil, sprinklers,
// machines, buildings and everything you planted can only be on your land, so a big valley costs nothing
// until you buy it. Returns the first span at or after cell `from` (a plot edge) and sets owned_len.
uint8_t owned_len;
uint16_t owned_run(uint16_t from) BANKED {
    uint8_t y, px, m, n;
    if (from >= WCELLS) return 0xFFFF;
    y = WIDX_Y(from); px = WIDX_X(from) >> 3;
    for (; y < WH; y++, px = 0) {
        m = G.plots[y >> 3];
        if (!m) { y |= 7; continue; }
        for (; px < 8; px++)
            if (m & (uint8_t)(1 << px)) {
                uint16_t r = WIDX(px << 3, y);
                for (n = 0; px < 8 && (m & (uint8_t)(1 << px)); px++) n += 8;
                owned_len = n;
                return r;
            }
    }
    return 0xFFFF;
}

// the cell types scan_cells (main.c) looks for, in a 32-byte table kept 32-aligned inside scan_buf
static uint8_t scan_buf[63];
extern uint8_t *scan_tab;   // main.c (scan_cells reads it)
void scan_types(uint8_t t1, uint8_t t2) BANKED {
    scan_tab = (uint8_t *)(((uint16_t)scan_buf + 31) & ~31);
    memset(scan_tab, 0, 32);
    scan_tab[t1 & 31] = scan_tab[t2 & 31] = 1;
}
void scan_add(uint8_t t) BANKED { scan_tab[t & 31] = 1; }
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
            W_CELL[i] = C_STRUCT; W_A1[i] = slot; W_A2_SET(i, p++);
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
            W_CELL[i] = C_GRASS; W_A1[i] = 0; W_A2_SET(i, 0);
        }
    b->type = 0xFF;
}

// ---------------------------------------------------------------- autotiles
static uint8_t fence_kind(int8_t x, int8_t y) {
    uint16_t i;
    if (x < 0 || y < 0 || x >= WW || y >= WH) return 0;
    i = WIDX(x, y);
    if (W_CELL[i] != C_DECOR) return 0;
    return decor[W_A1[i]].fence;
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
                W_A2_SET(WIDX(cx, cy), h ? 1 : (v ? 2 : 0));
            }
            redraw_cell(cx, cy);  // water edges and fences around the change
        }
}

// ---------------------------------------------------------------- world generation
static void setc(uint8_t x, uint8_t y, uint8_t c, uint8_t a1, uint8_t a2) {
    uint16_t i = WIDX(x, y);
    W_CELL[i] = c; W_A1[i] = a1; W_A2_SET(i, a2);
}

// What each region grows: (percent limit, cell) pairs, the first limit above the roll wins
enum { MX_HOME, MX_PINE, MX_OLDGROWTH, MX_BRAMBLE, MX_BIRCH, MX_MEADOW, MX_FARBANK, MX_FLOWER, MX_ROCK, MX_MARSH, MX_MIRROR, MX_ORCHARD };
static const uint8_t mix[][12] = {
    {34, C_WEEDS, 44, C_ROCK, 51, C_STUMP, 61, C_GRASS2, 65, C_WILDFLOWER, 255, C_GRASS},   // home field: lovingly overgrown
    {55, C_PINE, 62, C_STUMP, 68, C_BUSH, 72, C_FORAGE, 255, C_GRASS2},
    {68, C_PINE, 73, C_STUMP, 78, C_BUSH, 82, C_FORAGE, 255, C_GRASS2},                    // old growth: the densest woods
    {34, C_PINE, 56, C_BUSH, 64, C_FORAGE, 68, C_STUMP, 255, C_GRASS2},                    // brambles: berry bushes
    {45, C_BIRCH, 52, C_SAPLING_WILD, 58, C_FORAGE, 62, C_STUMP, 255, C_GRASS},
    {18, C_WILDFLOWER, 30, C_WEEDS, 34, C_ROCK, 50, C_GRASS2, 255, C_GRASS},
    {18, C_WILDFLOWER, 30, C_WEEDS, 34, C_ROCK, 50, C_GRASS2, 96, C_GRASS, 255, C_TREE},   // far bank: a few wild fruit trees
    {40, C_WILDFLOWER, 48, C_FORAGE, 54, C_BUSH, 255, C_GRASS},
    {30, C_ROCK, 40, C_ORE, 46, C_BOULDER, 70, C_SAND, 255, C_GRASS},
    {25, C_WATER, 40, C_WEEDS, 48, C_FORAGE, 55, C_BUSH, 255, C_GRASS2},
    {22, C_PINE, 34, C_WILDFLOWER, 40, C_BUSH, 255, C_GRASS},                               // around the big lake
    {20, C_WEEDS, 255, C_GRASS}};                                                          // between orchard rows
static const uint8_t rg_mix[NUM_RG] = {
    MX_HOME, MX_PINE, MX_MEADOW, MX_ROCK, MX_ORCHARD, MX_BIRCH, MX_FLOWER, MX_HOME,
    MX_HOME, MX_MARSH, MX_MEADOW, MX_MEADOW, MX_ROCK,
    MX_OLDGROWTH, MX_ROCK, MX_FLOWER, MX_ORCHARD, MX_BIRCH, MX_MIRROR, MX_MARSH, MX_BRAMBLE,
    MX_MEADOW, MX_MEADOW, MX_ROCK, MX_FARBANK, MX_ORCHARD, MX_MEADOW};

static uint8_t pct(void) {   // 0..99 without a divide
    uint8_t r;
    do r = rnd() & 127; while (r >= 100);
    return r;
}

static void gen_plot(uint8_t p) {
    uint8_t rg = plot_rg[p], k = rg_kind[rg], x0 = (p & 7) << 3, y0 = (p >> 3) << 3, dx, dy, r, c, a;
    const uint8_t *m = mix[rg_mix[rg]], *t;
    // quarries get a cliff along their top edge where they meet other land (or the valley's rim)
    uint8_t cliff = k == RK_QUARRY && (p < 8 || plot_rg[p - 8] != rg);
    uint8_t forage = k == RK_FLOWER ? 2 : 1;
    for (dy = 0; dy < 8; dy++) {
        uint8_t *pc = &W_CELL[WIDX(x0, y0 + dy)];
        for (dx = 0; dx < 8; dx++, pc++) {
            if (k == RK_ORCHARD && ((0x92 >> dx) & 1) && ((0x92 >> dy) & 1)) c = C_TREE;   // trees every 3 cells
            else if (cliff && !dy) c = C_CLIFF;
            else {
                r = pct();
                for (t = m; r >= t[0]; t += 2) ;
                c = t[1];
            }
            a = 0;
            if (c == C_FORAGE) a = forage;
            else if (c == C_ORE) { r = rnd(); a = r < 140 ? 0 : r < 210 ? 1 : r < 245 ? 2 : 3; }
            else if (c == C_TREE) {   // orchards grow their own fruit: Apple Hills apples+peaches, Cherry Slope cherries+oranges
                a = (uint8_t)(rg == RG_APPLE ? (rnd() & 1) : rg == RG_CHERRY ? 2 + (rnd() & 1) : (rnd() & 3)) << 5;
                W_A2_SET(CELL_I(pc), 30);   // grown (a2 starts cleared)
            }
            *pc = c; pc[0x1000] = a;
        }
    }
    if (rg == RG_LAKESIDE) {  // a round lake with a sandy shore
        static const uint8_t sq[8] = {0, 1, 4, 9, 16, 25, 36, 49};
        for (dy = 0; dy < 8; dy++)
            for (dx = 0; dx < 8; dx++) {
                uint8_t d2 = sq[dx > 3 ? dx - 3 : 3 - dx] + sq[dy > 4 ? dy - 4 : 4 - dy];
                if (d2 <= 8) setc(x0 + dx, y0 + dy, C_WATER, 0, 0);
                else if (d2 <= 12) setc(x0 + dx, y0 + dy, C_SAND, 0, 0);
                else setc(x0 + dx, y0 + dy, (rnd() & 3) ? C_GRASS : C_PINE, 0, 0);
            }
    }
}

// the river: two cells wide, top to bottom down the east side, drifting a cell at a time
static uint8_t river_x(uint8_t y) {
    static const uint8_t band[8] = {46, 45, 44, 43, 43, 45, 45, 46};
    if (y >= 36 && y < 40) return 44;   // the river bend (where it always was in the home valley)
    return band[y >> 3];
}

static void gen_water(void) {
    uint8_t x, y;
    for (y = 0; y < WH; y++) {
        x = river_x(y);
        setc(x - 1, y, C_GRASS, 0, 0);
        setc(x, y, C_WATER, 0, 0);
        setc(x + 1, y, C_WATER, 0, 0);
        setc(x + 2, y, C_GRASS, 0, 0);
        if (y == 10 || y == 54) {   // two old plank bridges cross it
            setc(x, y, C_DECOR, DK_BRIDGE, 0);
            setc(x + 1, y, C_DECOR, DK_BRIDGE, 0);
        }
    }
    // Mirror Lake: two overlapping rounds with a sandy beach and a little pine island
    for (y = 24; y < 40; y++)
        for (x = 0; x < 16; x++) {
            static const uint8_t sq[12] = {0, 1, 4, 9, 16, 25, 36, 49, 64, 81, 100, 121};
            uint8_t d1 = sq[x > 7 ? x - 7 : 7 - x] + sq[y > 30 ? y - 30 : 30 - y];
            uint8_t d2 = sq[x > 9 ? x - 9 : 9 - x] + sq[y > 35 ? y - 35 : 35 - y];
            if (d1 <= 26 || d2 <= 12) setc(x, y, C_WATER, 0, 0);
            else if (d1 <= 38 || d2 <= 20) setc(x, y, C_SAND, 0, 0);
        }
    setc(6, 29, C_PINE, 0, 0);
}

void world_new(void) BANKED {
    uint8_t p, x, y;
    memset(&G, 0, sizeof(G));
    SWITCH_RAM(WB_A2);
    memset((uint8_t *)0xA000, 0, WCELLS);   // a2 = 0 everywhere; gen_plot only writes the exceptions
    SWITCH_RAM(WB_LIVE);
    G.rng_lo = 0x34; G.rng_hi = 0x12;  // same lovingly overgrown land every new game
    for (p = 0; p < NUM_PLOTS; p++) gen_plot(p);
    gen_water();
    for (p = 0; p < MAX_BLD; p++) G.bld[p].type = 0xFF;
    for (p = 0; p < MAX_ANIMALS; p++) G.anim[p].kind = 0xFF;
    G.plots[3] = G.plots[4] = START_ROW;
    // the home valley (the old 32x32 world) sits at FARM_OX: coordinates below are in it
#define O FARM_OX
    // the public road to town
    for (y = O + 12; y < WH; y++) {
        uint8_t c = y >= O + 24 ? C_ROAD : C_DIRT;
        setc(ROAD_X, y, c, 0, 0); setc(ROAD_X + 1, y, c, 0, 0);
    }
    for (x = O + 10; x < ROAD_X; x++) setc(x, O + 12, C_DIRT, 0, 0);
    // a friendly clearing by the house
    for (y = O + 12; y <= O + 14; y++)
        for (x = O + 8; x <= O + 13; x++) if (W_CELL[WIDX(x, y)] != C_DIRT) setc(x, y, (x + y) & 3 ? C_GRASS : C_GRASS2, 0, 0);
    for (x = O + 8; x <= O + 14; x++) for (y = O + 8; y <= O + 11; y++) setc(x, y, C_GRASS, 0, 0);
    // big obstacles that need better tools
    setc(O + 13, O + 17, C_BOULDER, 0, 0); setc(O + 18, O + 15, C_BOULDER, 0, 0); setc(O + 21, O + 22, C_BOULDER, 0, 0);
    setc(O + 17, O + 19, C_LOG, 0, 0); setc(O + 11, O + 17, C_LOG, 0, 0); setc(O + 22, O + 13, C_LOG, 0, 0);
    // grandpa's buildings
    bld_new(B_HOUSE, O + 9, O + 9, 0);
    bld_new(B_COOP, O + 17, O + 9, BF_RUIN);
    bld_new(B_BARN, O + 20, O + 9, BF_RUIN);
    bld_new(B_GREENHOUSE, O + 20, O + 18, BF_RUIN);
    for (x = O + 17; x <= O + 22; x++) setc(x, O + 12, C_GRASS, 0, 0);
    setc(O + 21, O + 21, C_GRASS, 0, 0);
    setc(O + 12, O + 11, C_DECOR, DK_BIN, 0);
    setc(O + 13, O + 10, C_DECOR, DK_WELL, 0);
    setc(O + 14, O + 11, C_DECOR, DK_SIGN, 0);
    setc(O + 12, O + 9, C_DECOR, DK_MAILBOX, 0);

    G.gold = 500;
    G.day = 1; G.season = SEASON_SPRING; G.year = 1;
    G.time = 360;
    G.energy = G.energy_max = 100;
    G.px = O + 10; G.py = O + 12; G.facing = D_DOWN;
#undef O
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
// The nightly passes visit only your own land (FOR_OWNED: spans of owned plots along each row): soil,
// sprinklers, machines, trees you planted, scarecrows all stand on it, so the 64x64 valley costs no more
// than the plots you have bought. Wild land changes through a few random samples a night (wild_growth).
static uint8_t m_crows, m_season, m_passed, m_storm, m_fairy, m_giant, m_produce;
static uint32_t m_income;
static uint8_t m_fruit;   // rarity: a fruit tree ripened overnight

static uint8_t near_decor(uint16_t i, uint8_t kind, uint8_t r) {
    int8_t x = WIDX_X(i), y = WIDX_Y(i), dx, dy;
    for (dy = -r; dy <= (int8_t)r; dy++)
        for (dx = -r; dx <= (int8_t)r; dx++) {
            int8_t cx = x + dx, cy = y + dy;
            uint16_t j;
            if (cx < 0 || cy < 0 || cx >= WW || cy >= WH) continue;
            j = WIDX(cx, cy);
            if (W_CELL[j] == C_DECOR && W_A1[j] == kind) return 1;
        }
    return 0;
}

#define MAX_SCARE 40
#define MAX_SPR 64
static uint8_t scared(uint8_t x, uint8_t y, const uint8_t *sx, const uint8_t *sy, uint8_t ns) {
    uint8_t k;
    for (k = 0; k < ns; k++) {
        uint8_t dx = x - sx[k] + 4, dy = y - sy[k] + 4;   // within 4 both ways <=> 0..8 after the offset
        if (dx <= 8 && dy <= 8) return 1;
    }
    return 0;
}

// cell[] (0xA000) and a1[] (0xB000) are consecutive in the live SRAM bank: pc[A1] is a cell's a1
#define A1 0x1000

// owned land in 8-cell runs (a row of one plot), for searches that start at a random spot and wrap around
static uint16_t run_next(uint16_t r) {
    r = owned_run(r + 8);
    return r == 0xFFFF ? owned_run(0) : r;
}
static uint16_t run_random(void) {   // a random owned run: 0..63 runs past the first
    uint16_t r = owned_run(0);
    uint8_t n = rnd() & 63;
    if (r == 0xFFFF) return r;
    while (n--) r = run_next(r);
    return r;
}

static void water_around(uint16_t i, uint8_t r) {   // clamped box, unsigned (was ~250 cycles a cell)
    uint8_t x = WIDX_X(i), y = WIDX_Y(i), cx, cy, w;
    uint8_t x0 = x >= r ? x - r : 0, x1 = x + r > WW - 1 ? WW - 1 : x + r;
    uint8_t y0 = y >= r ? y - r : 0, y1 = y + r > WH - 1 ? WH - 1 : y + r;
    w = x1 - x0 + 1;
    for (cy = y0; cy <= y1; cy++) {
        uint8_t *pc = &W_CELL[WIDX(x0, cy)];
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
        for (t = 0; t < NUM_KINDS; t++) {
            mom = dad = 0xFF; n = 0; free_k = 0xFF;
            for (k = 0; k < MAX_ANIMALS; k++) {
                Animal *a = &G.anim[k];
                if (a->kind == 0xFF) { if (free_k == 0xFF) free_k = k; continue; }
                if (a->home != b) continue;
                n++;
                if (a->kind != t || a->age < ADULT_AGE || a->happy < 180 || !(a->flags & AF_PAIR)) continue;   // only pairs you chose
                if (mom == 0xFF) mom = k; else if (dad == 0xFF) dad = k;
            }
            if (dad == 0xFF || free_k == 0xFF || n >= (G.bld[b].type == B_COOP ? 6 : BARN_CAP) || rnd() >= 80) continue;
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
                anim_born(free_k, mom, dad);   // animals.c: its temperament, and now and then a sparkle
            }
            m_baby = free_k + 1; m_mom = mom; m_dad = dad;
            break;
        }
    }
}

// overnight pass 1, one cell that changes (soil with a crop, trees, wild saplings, hives, rods, working
// machines); out of line so the scan loop stays small.
static uint8_t g_crops, g_rod, g_rain;   // g_rain: SOIL_WET if tomorrow's (= today's) weather rains
#define MAX_PUMP 24
static uint16_t g_pumps[MAX_PUMP];       // ripe pumpkins pass 1 saw (pass 2 tries them for giants)
static uint8_t g_npump;                  // 0xFF: too many to list
static void grow(uint8_t *pc, uint16_t i) {
    uint8_t c = *pc, a1 = pc[A1], a2 = W_A2(i), crop;
    switch (c) {
    case C_SOIL:
        crop = a1 & SOIL_CROP;
        if (crop && crop != CROP_WITHERED) {
            if (g_crops < 255) g_crops++;
            if (((a1 & SOIL_WET) || m_fairy) && (a2 & SOIL_DAYS) < crops[crop].days) W_A2_SET(i, ++a2);
            if (crop == 10 && (a2 & SOIL_DAYS) >= crops[10].days) {   // a ripe pumpkin: pass 2 tries it
                if (g_npump < MAX_PUMP) g_pumps[g_npump++] = i; else g_npump = 0xFF;
            }
        }
        pc[A1] = (a1 & ~SOIL_WET) | g_rain;   // dries overnight, unless it rains
        break;
    case C_TREE:
        if (a2 < 250) W_A2_SET(i, ++a2);
        if (a2 >= 10 && (a1 & 31) < TREE_RIPE_DAYS && season_ok_tree(a1 >> 5)) {
            pc[A1] = ++a1;
            if ((a1 & 31) == TREE_RIPE_DAYS) m_fruit = 1;
        }
        break;
    case C_BUSH:
        if (a1 < 9) pc[A1] = a1 + 1;
        break;
    case C_SAPLING_WILD:
        if (++a2 >= 8) { *pc = kind_of(PLOT_OF(WIDX_X(i), WIDX_Y(i))) == RK_BIRCH ? C_BIRCH : C_PINE; a2 = 0; }
        W_A2_SET(i, a2);
        break;
    case C_DECOR:
        if (a1 == DK_BEEHIVE && a2 < 3) W_A2_SET(i, a2 + 1);
        if (a1 == DK_ROD) g_rod = 1;
        break;
    case C_MACHINE:
        if (a2 && a2 < 0x80) {
            if ((a1 >> 2) <= 1) { pc[A1] = a1 & 3; W_A2_SET(i, a2 | 0x80); }
            else pc[A1] = a1 - 4;
        }
        break;
    }
}

// Weeds creep back onto your grass, forage and ore turn up in the wild woods, meadows and quarries.
// Rolling every grass cell of the valley each night (2 or 3 in 256) took longer than everything else
// together; 64 random cells with the odds scaled up (x128) give the same expected changes per cell.
static void wild_growth(void) {
    uint8_t n, x, y, c, k;
    uint16_t i;
    for (n = 64; n; n--) {
        x = rnd() & 63; y = rnd() & 63;
        i = WIDX(x, y);
        c = W_CELL[i];
        if (c != C_GRASS && c != C_GRASS2 && c != C_SAND) continue;
        if (owned(x, y)) {
            if (c != C_SAND && rnd() < 128) { W_CELL[i] = C_WEEDS; W_A1[i] = 1; }   // a1 = 1: regrown weed (the groundskeeper's job)
        } else if (c != C_GRASS2) {
            k = kind_of(PLOT_OF(x, y));
            if ((k == RK_PINE || k == RK_BIRCH || k == RK_FLOWER || k == RK_MARSH) && rnd() < 192) { W_CELL[i] = C_FORAGE; W_A1[i] = rnd() % 3; }
            if ((k == RK_QUARRY || k == RK_ROCKY) && rnd() < 192) { W_CELL[i] = C_ORE; W_A1[i] = rnd() < 200 ? 0 : 1; }
        }
    }
}

static uint8_t ripe_pumpkin(uint16_t i) {
    return W_CELL[i] == C_SOIL && (W_A1[i] & SOIL_CROP) == 10 && (W_A2(i) & SOIL_DAYS) >= crops[10].days;
}

// The passes are small functions of their own: in one big function SDCC kept every local on a ~190-byte
// stack frame and spent ~100 cycles a cell just reaching them.

// 1. growth and nature: scan your land for the cells that change (scan_cells), the quick cases inline,
// the rest out of line (grow). Today's rain (tomorrow's forecast becomes today's weather in step 6) soaks
// the soil here too, and the sprinklers water once everything has grown.
static void night_growth(void) {
    uint16_t r, spr[MAX_SPR];   // sprinklers (bit 15: quality sprinkler)
    uint8_t n, c, a, nsp = 0, *pc, *pe;
    g_crops = 0; g_rod = 0; g_npump = 0;
    g_rain = (G.tomorrow == 1 || G.tomorrow == 3) ? SOIL_WET : 0;
    scan_types(C_SOIL, C_DECOR); scan_add(C_TREE); scan_add(C_BUSH); scan_add(C_MACHINE); scan_add(C_SAPLING_WILD);
    FOR_OWNED_CELLS(r, n, pc, pe) {
        c = *pc; a = pc[A1];
        if (c == C_SOIL) { if (!(a & SOIL_CROP)) { pc[A1] = (a & ~SOIL_WET) | g_rain; continue; } }   // bare soil
        else if (c == C_DECOR) {
            if (a == DK_SPRINKLER || a == DK_QSPRINKLER) {
                if (nsp < MAX_SPR) spr[nsp++] = CELL_I(pc) | (a == DK_QSPRINKLER ? 0x8000 : 0);
                else nsp = 0xFF;   // too many to list: scanned for again below
                continue;
            }
            if (a != DK_BEEHIVE && a != DK_ROD) continue;
        }
        else if (c == C_MACHINE) { if (a < 4) continue; }                                // idle or ready
        else if (c == C_BUSH) { if (a < 9) pc[A1] = a + 1; continue; }
        grow(pc, CELL_I(pc));
    }
    wild_growth();
    if (nsp != 0xFF)
        for (c = 0; c < nsp; c++) water_around(spr[c] & 0x7FFF, spr[c] & 0x8000 ? 2 : 1);
    else {
        scan_types(C_DECOR, C_DECOR);
        FOR_OWNED_CELLS(r, n, pc, pe) {
            if (pc[A1] == DK_SPRINKLER) water_around(CELL_I(pc), 1);
            else if (pc[A1] == DK_QSPRINKLER) water_around(CELL_I(pc), 2);
        }
    }
}

// 2. giant pumpkins: a ripe 2x2 block may fuse (each ripe pumpkin pass 1 listed, as its block's top-left)
static void pumpkin_block(uint16_t i) {
    if (WIDX_X(i) == WW - 1 || WIDX_Y(i) == WH - 1 || W_CELL[i] != C_SOIL) return;   // (fused already)
    if (ripe_pumpkin(i) && ripe_pumpkin(i + 1) && ripe_pumpkin(i + WW) && ripe_pumpkin(i + WW + 1) && rnd() < 64) {
        W_CELL[i] = W_CELL[i + 1] = W_CELL[i + WW] = W_CELL[i + WW + 1] = C_GIANT;
        W_A1[i] = 0; W_A1[i + 1] = 1; W_A1[i + WW] = 2; W_A1[i + WW + 1] = 3;
        m_giant = 1;
    }
}
static void night_pumpkins(void) {
    uint16_t r;
    uint8_t n, *pc, *pe;
    if (g_npump != 0xFF) {
        for (n = 0; n < g_npump; n++) pumpkin_block(g_pumps[n]);
        return;
    }
    scan_types(C_SOIL, C_SOIL);
    FOR_OWNED_CELLS(r, n, pc, pe) {
        if ((pc[A1] & SOIL_CROP) != 10) continue;
        pumpkin_block(CELL_I(pc));
    }
}
// 3. crows peck the first unguarded crop from a random spot on
static void night_crows(void) {
    // list the scarecrows once (a 9x9 scan per crop was up to 80k cell reads on a guarded farm)
    uint8_t sx[MAX_SCARE], sy[MAX_SCARE], ns = 0, n, k, crop, *pc, *pe;
    uint16_t r, r0, i;
    scan_types(C_DECOR, C_DECOR);
    FOR_OWNED_CELLS(r, n, pc, pe)
        if (pc[A1] == DK_SCARECROW && ns != 0xFF) {
            if (ns == MAX_SCARE) { ns = 0xFF; continue; }
            i = CELL_I(pc);
            sx[ns] = WIDX_X(i); sy[ns] = WIDX_Y(i); ns++;
        }
    r = r0 = run_random();
    if (r != 0xFFFF)
        do {
            for (k = 8, i = r; k; k--, i++) {
                crop = W_A1[i] & SOIL_CROP;
                if (W_CELL[i] == C_SOIL && crop && crop != CROP_WITHERED &&
                    !(ns == 0xFF ? near_decor(i, DK_SCARECROW, 4) : scared(WIDX_X(i), WIDX_Y(i), sx, sy, ns))) {
                    W_A1[i] &= SOIL_WET | SOIL_FERT; W_A2_SET(i, 0);
                    m_crows = 1;
                    return;
                }
            }
            r = run_next(r);
        } while (r != r0);
}

// 4. lightning finds a crop (the soil keeps the night's rain or sprinkler water)
static void night_storm(void) {
    uint16_t r, r0, i;
    uint8_t k;
    r = r0 = run_random();
    if (r != 0xFFFF)
        do {
            for (k = 8, i = r; k; k--, i++)
                if (W_CELL[i] == C_SOIL && (W_A1[i] & SOIL_CROP)) {
                    W_A1[i] = (W_A1[i] & (SOIL_FERT | SOIL_WET)) | CROP_WITHERED;
                    m_storm = 1;
                    return;
                }
            r = run_next(r);
        } while (r != r0);
}

// 5. a new season: crops of the old one wither (unless a greenhouse stands)
static void night_wither(void) {
    uint16_t r;
    uint8_t n, crop, *pc, *pe;
    scan_types(C_SOIL, C_SOIL);
    FOR_OWNED_CELLS(r, n, pc, pe) {
        crop = pc[A1] & SOIL_CROP;
        if (crop && crop != CROP_WITHERED && crops[crop].season != G.season)
            pc[A1] = (pc[A1] & (SOIL_WET | SOIL_FERT)) | CROP_WITHERED;
    }
}

void world_overnight(uint8_t passed_out) BANKED {
    uint8_t n, k, has_rod, green = 0;
    m_crows = m_season = m_storm = m_fairy = m_giant = m_produce = 0;
    m_passed = passed_out;
    m_fruit = 0;
    for (k = 0; k < MAX_BLD; k++)
        if (G.bld[k].type == B_GREENHOUSE && !(G.bld[k].flags & BF_RUIN)) green = 1;
    m_fairy = rnd() < 6;
    night_growth();
    n = g_crops; has_rod = g_rod;
    if (g_npump) night_pumpkins();
    // crows visit bigger farms without scarecrows; storms find a crop unless a lightning rod stands anywhere
    if (n >= 6 && rnd() < 90) night_crows();
    if (G.weather == 3 && !has_rod && n && rnd() < 160) night_storm();
    // calendar
    if (++G.day > 28) {
        G.day = 1;
        G.season = (G.season + 1) & 3;
        if (G.season == SEASON_SPRING) G.year++;
        m_season = 1;
        if (!green) night_wither();
    }
    // 6. weather: today is yesterday's forecast; roll tomorrow
    G.weather = G.tomorrow;
    if (G.season == SEASON_WINTER) G.tomorrow = rnd() < 70 ? 2 : 0;
    else {
        uint8_t rr = rnd();
        G.tomorrow = rr < 50 ? 1 : (G.season == SEASON_SUMMER && rr < 70 ? 3 : 0);
    }
    // 7. animals: fed ones give produce (quality from happiness); happiness drifts
    for (k = 0; k < MAX_ANIMALS; k++) {
        Animal *a = &G.anim[k];
        if (a->kind == 0xFF) continue;
        if (a->flags & AF_FED) {
            if (a->happy < 235) a->happy += 20;
            if (a->age < ADULT_AGE) ;   // babies just grow
            else if ((a->kind % 3) != A_SHEEP) { a->flags |= AF_READY; m_produce = 1; }
            else if ((a->flags & AF_WOOL) >= ((WOOL_DAYS - 1) << 3)) { a->flags = (a->flags & ~AF_WOOL) | AF_READY; m_produce = 1; }
            else if (!(a->flags & AF_READY)) a->flags += 1 << 3;   // fleece grows while fed
        }
        else if (a->happy > 30) a->happy -= 30;
        else a->happy = 0;
        if (a->flags & AF_PET) { if (a->happy < 245) a->happy += 10; }
        anim_night(k);   // animals.c: temperament, and a baby's care
        a->flags &= AF_READY | AF_BREED | AF_WOOL | AF_PAIR;
        if (a->age < 255) a->age++;
    }
    breed_overnight();
    G.pet_flags = 0;
    G.horse_day = 0;   // horse.c: brush and ride again tomorrow
    G.talked = G.gifted = 0;
    daily_roll();      // daily.c: today's surprise
    friends_overnight();   // friends2.c: letters for new hearts
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
    G.buff = 0;                     // food buffs last one day
    G.ate = 0;                      // ... and so does a full stomach
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
        s_clr(); s_add(G.anim[m_mom].name); s_add(" and "); s_add(G.anim[m_dad].name); s_add(" had a baby: a ");
        s_breed(m_baby - 1, 1); s_add("! Pet it every day and it will grow up big.");
        music_play(MUS_JINGLE_FANFARE);
        say(sbuf);
        music_for_scene();
        s_clr(); s_add("Name the baby");   // prompt lives in sbuf: ui_name is in another bank
        ui_name(sbuf, c->name, 5);
        m_baby = 0;
        save_write();
    }
    if (m_fruit && G.season != SEASON_WINTER) say("Fresh fruit is ripe on your fruit trees!");
    daily_report();
    if (G.mail) say("There's a letter in your mailbox!");
}
