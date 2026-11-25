// Stilwell Farms -- detailed UI pages: item panels, status, calendar, farm map (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "decor.h"

BANKREF(item_panel)
extern void put16(uint8_t oam, int16_t x, int16_t y, uint8_t tile, uint8_t pal, uint8_t flip);
extern void hide16(uint8_t oam);

static void wait_btn(void) {
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); return; }
        wait_vbl_done();
    }
}

static const char *const season_short[4] = {"Spr", "Sum", "Fall", "Win"};

// ---------------------------------------------------------------- detail panels (rows 12-16)
static mt_t item_art(uint8_t it) {
    if (it < NUM_CROPS) return crops[it + 1].ripe_mt;
    switch (it) {
    case I_APPLE: return MT_FRUIT_TREE_RIPE;
    case I_PEACH: return MT_PEACH_TREE_RIPE;
    case I_ORANGE: return MT_ORANGE_TREE_RIPE;
    case I_CHERRY: return MT_CHERRY_TREE_RIPE;
    case I_HONEY: return MT_BEEHIVE;
    case I_BERRIES: return MT_BERRY_BUSH;
    case I_MUSHROOM: return MT_MUSHROOM;
    case I_HERB: return MT_WILD_FLOWERS;
    case I_COPPER: return MT_ORE_COPPER;
    case I_IRON: return MT_ORE_IRON;
    case I_GOLD: return MT_ORE_GOLD;
    case I_GEM: return MT_GEM_AMETHYST;
    case I_TOPAZ: return MT_GEM_TOPAZ;
    case I_EMERALD: return MT_GEM_EMERALD;
    case I_RUBY: return MT_GEM_RUBY;
    case I_DIAMOND: return MT_GEM_DIAMOND;
    case I_COAL: return MT_COAL_LUMP;
    case I_SHELL: return MT_BEACH_SHELL;
    case I_CORAL: return MT_BEACH_CORAL;
    case I_SEAGLASS: return MT_BEACH_GLASS;
    case I_DRIFTWOOD: return MT_BEACH_DRIFTWOOD;
    case I_JUICE: case I_CIDER: return MT_KEG_READY;
    case I_CHEESE: return MT_PRESS_READY;
    case I_JAM: case I_PICKLES: return MT_JAR_READY;
    case I_SALAD: case I_OMELET: case I_FISHFRY: case I_FISHSTEW: case I_CHOWDER: case I_PUMPSOUP: case I_BERRYPIE:
    case I_APPLEPIE: case I_CASSEROLE: case I_RISOTTO: case I_VEGSTEW: case I_SORBET: case I_MUFFIN: case I_COBBLER:
    case I_STIRFRY: case I_FEAST: return MT_DISH_SALAD + (it - I_DISH0);
    default: return 0xFFFF;
    }
}

static const char *item_kind(uint8_t it) {
    if (it < NUM_CROPS) return "Crop";
    if (it <= I_CHERRY) return "Fruit";
    if (it <= I_HONEY || it == I_WOOL) return "Animal good";
    if (it <= I_HERB) return "Forage";
    if (it <= I_KOI) return "Fish";
    if (it < I_GEM) return "Ore - for tools";
    if (it == I_GEM || (it >= I_TOPAZ && it <= I_DIAMOND)) return "Gem - precious";
    if (it == I_COAL) return "Coal - for fuel";
    if (it >= I_SARDINE && it <= I_SQUID) return "Ocean fish";
    if (it >= I_SHELL && it <= I_DRIFTWOOD) return "Beach find";
    if (it == I_WOOL) return "Animal good";
    return "Artisan good";
}

static void panel_art(uint8_t it) {
    mt_t a = item_art(it);
    if (a != 0xFFFF) ui_put_mt(1, 13, a);
    else { ui_char(1, 13, item_icon_tile[it]); ui_char(2, 13, GL_FILL); ui_char(1, 14, GL_FILL); ui_char(2, 14, GL_FILL); }
}

void item_panel(uint8_t it, uint8_t q) BANKED {
    ui_panel(0, 12, 20, 5);
    panel_art(it);
    s_clr(); s_add(items[it].name);
    if (q) { s_ch(' '); s_ch(q == 2 ? (char)0xF1 : (char)0xF0); s_add(q == 2 ? " gold" : " silver"); }
    ui_text(4, 13, sbuf);
    if (IS_DISH(it)) { dish_line(it); ui_text(4, 14, sbuf); }   // cooking.c: what eating it does
    else ui_text(4, 14, item_kind(it));
    s_clr(); s_add("Sells for G"); s_num(sell_value(it, q), 0);
    ui_text(4, 15, sbuf);
}

void crop_panel(uint8_t c) BANKED { crop_panel_r(c, R_COMMON); }

// rare / legendary packets trade the price line for what makes them special
void crop_panel_r(uint8_t c, uint8_t rar) BANKED {
    ui_panel(0, 12, 20, 5);
    ui_put_mt(1, 13, crops[c].ripe_mt);
    s_clr(); s_add(items[c - 1].name);
    if (rar) { s_ch(rar == R_RARE ? '+' : (char)0xF1); s_add(rar == R_RARE ? " Rare" : " Legend"); }
    else { s_add(" - "); s_add(season_name[crops[c].season]); }
    ui_text(4, 13, sbuf);
    s_clr(); s_add("Ripe in "); s_num(crops[c].days, 0); s_add(" days");
    if (crops[c].regrow) { s_add(", +"); s_num(crops[c].regrow, 0); }
    ui_text(4, 14, sbuf);
    s_clr();
    if (rar == R_RARE) s_add("x2 per harvest");
    else if (rar == R_LEGEND) { s_add("x3-4, top"); s_ch((char)0xF1); s_add(" odds"); }
    else { s_add("Sell G"); s_num(items[c - 1].price, 0); s_add(" \xF1G"); s_num(item_value(c - 1, 2), 0); }
    ui_text(4, 15, sbuf);
}

// ---------------------------------------------------------------- start menu side panel
void status_panel(void) BANKED {
    uint8_t h = (G.time / 60) % 24, k, segs;
    static const uint8_t wicon[4] = {GL_ICON_SUN, GL_ICON_RAIN, GL_ICON_SNOW, GL_ICON_RAIN};
    ui_panel(0, 0, 11, 11);
    s_clr(); s_add(G.farm_name);
    ui_text(1, 1, sbuf);
    ui_char(1, 2, GL_SEP); for (k = 2; k < 10; k++) ui_char(k, 2, GL_SEP);
    s_clr(); s_add(season_short[G.season]); s_ch(' '); s_num(G.day, 0); s_add(" Y"); s_num(G.year, 0);
    ui_text(1, 3, sbuf);
    ui_char(1, 4, G.time >= 1200 ? GL_ICON_MOON : wicon[G.weather & 3]);
    s_clr(); s_num(h == 0 ? 12 : (h > 12 ? h - 12 : h), 2); s_ch(':'); s_ch('0' + (G.time % 60) / 10); s_ch('0'); s_add(h < 12 ? "am" : "pm");
    ui_text(3, 4, sbuf);
    ui_char(1, 5, GL_COIN);
    s_clr(); s_num(G.gold > 9999999 ? 9999999 : G.gold, 0);
    ui_text(3, 5, sbuf);
    ui_char(1, 6, GL_HEART);
    segs = (uint8_t)(((uint16_t)G.energy * 7 + G.energy_max - 1) / G.energy_max);
    for (k = 0; k < 7; k++) ui_char(3 + k, 6, k < segs ? GL_BAR_FULL : GL_BAR_EMPTY);
    ui_char(1, 8, GL_ICON_PIN);
    ui_text(3, 8, scene == SCENE_FARM ? "Farm" : scene == MAP_BARN ? "Barn" : scene == MAP_COOP ? "Coop" :
                  IS_INTERIOR(scene) ? "Home" : scene == MAP_BEACH ? "Beach" : scene == MAP_WOODS ? "Woods" : IS_NEW(scene) ? "Away" : IS_MINE(scene) ? "Mine" : "Town");
    if (G.mail) { ui_char(1, 9, GL_ICON_LETTER); ui_text(3, 9, "Mail!"); }
}

// ---------------------------------------------------------------- calendar
void calendar_page(void) BANKED {
    uint8_t d, x, y, k, fest = G.season == SEASON_WINTER ? 25 : 14;
    static const char *const wd[7] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    ui_box(0, 0, 20, 17);
    s_clr(); s_add(season_name[G.season]); s_add(" Y"); s_num(G.year, 0);
    ui_title(2, 0, sbuf);
    // weeks run across, weekdays down: every cell has room for a marker + 2 digits + a gap
    for (k = 0; k < 4; k++) { s_clr(); s_add("Wk"); s_num(k + 1, 0); ui_text(4 + k * 4, 2, sbuf); }
    for (k = 0; k < 7; k++) ui_text(1, 3 + k, wd[k]);
    for (d = 1; d <= 28; d++) {
        x = 4 + ((d - 1) / 7) * 4; y = 3 + ((d - 1) % 7);
        s_clr(); s_num(d, 2);
        ui_text(x + 1, y, sbuf);
        if (d == G.day) ui_char(x, y, GL_CAL_TODAY);
        else if (d == fest) ui_char(x, y, GL_CAL_FEST);
        else if (G.req_qty && d == G.day + G.req_day) ui_char(x, y, GL_CAL_DOT);
    }
    for (k = 1; k < 19; k++) ui_char(k, 11, GL_SEP);
    ui_char(1, 12, GL_CAL_TODAY); ui_text(3, 12, "Today");
    ui_char(9, 12, GL_CAL_FEST);
    s_clr(); s_add(G.season == SEASON_WINTER ? "Star Nite" : "Festival"); ui_text(11, 12, sbuf);
    ui_char(1, 13, GL_CAL_DOT); ui_text(3, 13, "Request due");
    s_clr(); s_add("Tomorrow: ");
    s_add(G.tomorrow == 1 ? "rain" : G.tomorrow == 2 ? "snow" : G.tomorrow == 3 ? "storm" : "sunny");
    ui_text(1, 15, sbuf);
    wait_btn();
}

// ---------------------------------------------------------------- farm map
// The whole 64x64 valley at 16x16 tiles: each tile stands for 4x4 cells and a land plot is 2x2 tiles.
// Each tile shows what most of its ground is (woods, orchard, crops, water, marsh, quarry, ridge, road,
// buildings...), bright on land you own and shaded on land for sale; dots mark the plot grid. A bracket
// sprite frames the selected plot (the D-pad moves it plot by plot); A buys one for sale.
enum { VM_MEADOW, VM_FLOWERS, VM_WOODS, VM_ORCHARD, VM_CROPS, VM_WATER, VM_MARSH, VM_ROCKS, VM_CLIFF, VM_PATH, VM_SAND, VM_BLDG };
static const mt_t vm_mt[24] = {   // [kind][for sale]
    MT_VM_MEADOW, MT_VMU_MEADOW, MT_VM_FLOWERS, MT_VMU_FLOWERS, MT_VM_WOODS, MT_VMU_WOODS, MT_VM_ORCHARD, MT_VMU_ORCHARD,
    MT_VM_CROPS, MT_VMU_CROPS, MT_VM_WATER, MT_VMU_WATER, MT_VM_MARSH, MT_VMU_MARSH, MT_VM_ROCKS, MT_VMU_ROCKS,
    MT_VM_CLIFF, MT_VMU_CLIFF, MT_VM_PATH, MT_VMU_PATH, MT_VM_SAND, MT_VMU_SAND, MT_VM_BLDG, MT_VMU_BLDG};
// what a cell counts toward
enum { N_NONE, N_WATER, N_WOODS, N_TREE, N_SOIL, N_BLDG, N_ROCK, N_CLIFF, N_ROAD, N_PATH, N_SAND, N_FLOWER, N_DECOR, N_ALL };
static const uint8_t cell_n[26] = {
    N_NONE, N_NONE, N_FLOWER, N_NONE, N_ROCK, N_ROCK, N_NONE, N_NONE,       // grass grass2 wildflower weeds rock boulder stump log
    N_WOODS, N_PATH, N_SOIL, N_DECOR, N_TREE, N_BLDG, N_WATER, N_ROAD,     // forest dirt soil decor tree struct water road
    N_WOODS, N_WOODS, N_WOODS, N_NONE, N_ROCK, N_SAND, N_CLIFF, N_BLDG,    // pine birch bush forage ore sand cliff machine
    N_SOIL, N_WOODS};                                                      // giant sapling

static uint8_t vm_kind(uint8_t x, uint8_t y, uint8_t own) {
    uint8_t n[N_ALL], i, j, k, wrow, wcol = 15, wline = 0;
    memset(n, 0, sizeof(n));
    for (j = 0; j < 4; j++) {
        uint16_t idx = WIDX(x, y + j);
        wrow = 0;
        for (i = 0; i < 4; i++, idx++) {
            k = cell_n[W_CELL[idx]];
            if (k == N_DECOR) k = W_A1[idx] == DK_BRIDGE ? N_ROAD : decor[W_A1[idx]].walk ? N_PATH : N_NONE;
            else if (k == N_WATER) wrow |= 1 << i;
            n[k]++;
        }
        if (wrow == 15) wline = 1;
        wcol &= wrow;
    }
    if (n[N_BLDG]) return VM_BLDG;
    if (n[N_ROAD]) return VM_PATH;   // the public road and the bridges stay unbroken
    if (n[N_WATER] >= 8 || wline || wcol) return VM_WATER;   // (a 2-wide river can cross a tile by one column)
    if (n[N_WATER] >= 2) return n[N_SAND] ? VM_SAND : VM_MARSH;
    if (n[N_CLIFF] >= 2) return VM_CLIFF;
    if (n[N_SOIL] >= 2) return VM_CROPS;
    if (n[N_TREE] >= 2 || (n[N_TREE] && own)) return VM_ORCHARD;
    if (n[N_WOODS] >= 6) return VM_WOODS;
    if (n[N_ROCK] >= 5) return VM_ROCKS;
    if (n[N_SAND] >= 6) return VM_SAND;
    if (n[N_PATH] >= 3) return VM_PATH;
    if (n[N_FLOWER] >= 5) return VM_FLOWERS;
    return VM_MEADOW;
}

static void no_prio(uint8_t x, uint8_t y) {   // a window tile the plot bracket sprite may cover
    VBK_REG = 1; set_win_tile_xy(x, y, get_win_tile_xy(x, y) & 0x7F); VBK_REG = 0;
}

static void map_glyph(uint8_t tx, uint8_t ty, uint8_t gl) { ui_char(2 + tx, ty, gl); no_prio(2 + tx, ty); }

// one map tile at map position tx, ty (0..15), window column 2 + tx. Its BG priority bit is cleared so
// the plot bracket sprite shows on top.
static void map_tile(uint8_t tx, uint8_t ty) {
    uint8_t x = tx << 2, y = ty << 2, own = owned(x, y);
    ui_put_quad(2 + tx, ty, vm_mt[(vm_kind(x, y, own) << 1) | !own], ((ty & 1) << 1) | (tx & 1));
    no_prio(2 + tx, ty);
}

static void map_strip(uint8_t plot) {   // rows 16-17: the plot's name, then whether it's yours or its price
    uint8_t x, own = PLOT_OWNED(plot);
    for (x = 0; x < 20; x++) { ui_char(x, 16, GL_FILL); ui_char(x, 17, GL_FILL); }
    s_clr(); plot_name_add(plot); ui_text(1, 16, sbuf);
    s_clr();
    if (own) { s_add("Yours: "); s_num(plots_owned(), 0); s_add("/64 owned"); }
    else { s_add("Price G"); s_num(plot_price(plot), 0); }   // (G18000 at most: ends at column 12)
    ui_text(1, 17, sbuf);
    if (!own) ui_text(15, 17, "A:Buy");
    ui_char(18, 16, own ? GL_MAP_HOUSE : GL_COIN);
}

void map_page(void) BANKED {
    uint8_t tx, ty, plot = PLOT_OF(G.px, G.py), t = 0, h = bld_find(B_HOUSE), last = 0xFF;
    uint8_t yx = G.px >> 2, yy = G.py >> 2, here = scene == SCENE_FARM, hx = 0xFF, hy = 0;
    if (IS_AREA(scene)) { area_map(); return; }
    if (!here) plot = PLOT_OF(ROAD_X, WH - 17);   // the road's plot below the home field
    hide_sprites();
    for (ty = 0; ty < 16; ty++) {
        ui_char(0, ty, GL_FILL); ui_char(1, ty, GL_FILL); ui_char(18, ty, GL_FILL); ui_char(19, ty, GL_FILL);
        for (tx = 0; tx < 16; tx++) map_tile(tx, ty);
    }
    if (h != 0xFF) { hx = (G.bld[h].x + 1) >> 2; hy = (G.bld[h].y + 1) >> 2; map_glyph(hx, hy, GL_MAP_HOUSE); }
    while (1) {
        if (plot != last) {
            uint8_t py0 = (plot >> 3) << 1;
            map_strip(plot);
            // markers beside the selected plot's two rows, in the side gutters
            if (last != 0xFF) {
                ty = (last >> 3) << 1;
                ui_char(1, ty, GL_FILL); ui_char(18, ty, GL_FILL); ui_char(1, ty + 1, GL_FILL); ui_char(18, ty + 1, GL_FILL);
            }
            ui_char(1, py0, GL_CURSOR); ui_char(18, py0, GL_CURSOR);
            ui_char(1, py0 + 1, GL_CURSOR); ui_char(18, py0 + 1, GL_CURSOR);
            last = plot; t = 0;
        }
        // the bracket around the plot (window at 7,0: map column 2 + 2 * plot column is screen x 16 + 16 * column)
        if (t & 16) hide16(22);
        else put16(22, 16 + ((plot & 7) << 4), (plot >> 3) << 4, SPR_CURSOR, SPR_CURSOR_PAL, 0);
        if (here) {   // you are here: blinks over your tile
            if ((t & 31) == 0) map_glyph(yx, yy, GL_MAP_YOU);
            else if ((t & 31) == 20) {
                if (yx == hx && yy == hy) map_glyph(hx, hy, GL_MAP_HOUSE);
                else map_tile(yx, yy);
            }
        }
        read_input();
        t++;
        if (joy_press & J_LEFT) { plot = (plot & 56) | ((plot - 1) & 7); sfx(SFX_CURSOR); }
        else if (joy_press & J_RIGHT) { plot = (plot & 56) | ((plot + 1) & 7); sfx(SFX_CURSOR); }
        else if (joy_press & J_UP) { plot = (plot - 8) & 63; sfx(SFX_CURSOR); }
        else if (joy_press & J_DOWN) { plot = (plot + 8) & 63; sfx(SFX_CURSOR); }
        else if (joy_press & J_A) {
            if (!PLOT_OWNED(plot)) { hide16(22); hud_draw_row(17); ui_end(); buy_plot_prompt(plot); ui_begin(); return; }
        } else if (joy_press & (J_B | J_START)) { sfx(SFX_CANCEL); hide16(22); hud_draw_row(17); return; }
        wait_vbl_done();
    }
}
