// Stilwell Farms -- detailed UI pages: item panels, status, calendar, farm map (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "decor.h"

BANKREF(item_panel)

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
    case I_GEM: return MT_GEM_ROCK;
    case I_JUICE: case I_CIDER: return MT_KEG_READY;
    case I_CHEESE: return MT_PRESS_READY;
    case I_JAM: case I_PICKLES: return MT_JAR_READY;
    default: return 0xFFFF;
    }
}

static const char *item_kind(uint8_t it) {
    if (it < NUM_CROPS) return "Crop";
    if (it <= I_CHERRY) return "Fruit";
    if (it <= I_HONEY || it == I_WOOL) return "Animal good";
    if (it <= I_HERB) return "Forage";
    if (it <= I_KOI) return "Fish";
    if (it <= I_GEM) return "Ore - for tools";
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
    ui_text(4, 14, item_kind(it));
    s_clr(); s_add("Sells for G"); s_num(item_value(it, q), 0);
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
    ui_text(3, 8, scene == SCENE_FARM ? "Farm" : IS_INTERIOR(scene) ? "Home" : "Town");
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
// 16x16 tiles, each standing for 2x2 farm cells. Move the plot cursor to see each plot; A to buy.
static uint8_t classify(uint8_t x, uint8_t y) {   // returns minimap tile: 0 grass 1 water 2 trees 3 crops 4 bldg 5 rock 6 path 7 locked
    uint8_t i, j, best = 0, pri = 0, c, p;
    static const uint8_t prio[8] = {0, 5, 3, 4, 6, 2, 1, 7};
    for (j = 0; j < 2; j++)
        for (i = 0; i < 2; i++) {
            uint16_t idx = WIDX(x + i, y + j);
            c = G.cell[idx];
            if (!owned(x + i, y + j) && c != C_ROAD) { best = 7; pri = 9; continue; }
            switch (c) {
            case C_WATER: p = 1; break;
            case C_FOREST: case C_PINE: case C_BIRCH: case C_TREE: case C_BUSH: case C_SAPLING_WILD: p = 2; break;
            case C_SOIL: case C_GIANT: p = 3; break;
            case C_STRUCT: case C_MACHINE: p = 4; break;
            case C_ROCK: case C_BOULDER: case C_ORE: case C_CLIFF: p = 5; break;
            case C_DIRT: case C_ROAD: case C_SAND: p = 6; break;
            case C_DECOR: p = decor[G.a1[idx]].walk ? 6 : 4; break;
            default: p = 0; break;
            }
            if (prio[p] > pri) { pri = prio[p]; best = p; }
        }
    return best;
}

static void map_tile(uint8_t tx, uint8_t ty, uint8_t k) {
    ui_put_quad(2 + tx, ty, k < 4 ? MT_MINIMAP_A : MT_MINIMAP_B, k & 3);
}

void map_page(void) BANKED {
    uint8_t tx, ty, plot = PLOT_OF(G.px, G.py), t = 0, h = bld_find(B_HOUSE), last = 0xFF;
    if (scene != SCENE_FARM) plot = 13;
    hide_sprites();   // the minimap tiles have no BG priority: keep the player/actors off it
    for (ty = 0; ty < 16; ty++)
        for (tx = 0; tx < 16; tx++) map_tile(tx, ty, classify(tx * 2, ty * 2));
    for (ty = 0; ty < 17; ty++) { ui_char(0, ty, GL_FILL); ui_char(1, ty, GL_FILL); ui_char(18, ty, GL_FILL); ui_char(19, ty, GL_FILL); }
    ui_char(0, 16, GL_FILL);
    for (tx = 0; tx < 20; tx++) ui_char(tx, 16, GL_FILL);
    if (h != 0xFF) ui_char(2 + ((G.bld[h].x + 1) >> 1), (G.bld[h].y + 1) >> 1, GL_MAP_HOUSE);
    while (1) {
        uint8_t px0 = 2 + (plot & 3) * 4, py0 = (plot >> 2) * 4;
        if (plot != last) {
            // plot info strip on row 16
            for (tx = 0; tx < 20; tx++) ui_char(tx, 16, GL_FILL);
            s_clr(); plot_name_add(plot);
            if (!(G.plots & ((uint16_t)1 << plot))) { s_add(" G"); s_num(plot_price(plot), 0); }
            else s_add(" (yours)");
            ui_text(1, 16, sbuf);
            // corner brackets around the selected plot in the side gutters
            for (ty = 0; ty < 16; ty++) { ui_char(1, ty, GL_FILL); ui_char(18, ty, GL_FILL); }
            ui_char(1, py0 + 1, GL_CURSOR); ui_char(18, py0 + 1, GL_CURSOR);
            last = plot;
        }
        if (scene == SCENE_FARM) {
            uint8_t yx = 2 + (G.px >> 1), yy = G.py >> 1;
            if ((t & 16)) ui_char(yx, yy, GL_MAP_YOU);
            else map_tile(G.px >> 1, yy, classify(G.px & ~1, G.py & ~1));
        }
        (void)px0;
        read_input();
        t++;
        if (joy_press & J_LEFT) { plot = (plot & 12) | ((plot - 1) & 3); sfx(SFX_CURSOR); }
        else if (joy_press & J_RIGHT) { plot = (plot & 12) | ((plot + 1) & 3); sfx(SFX_CURSOR); }
        else if (joy_press & J_UP) { plot = (plot - 4) & 15; sfx(SFX_CURSOR); }
        else if (joy_press & J_DOWN) { plot = (plot + 4) & 15; sfx(SFX_CURSOR); }
        else if (joy_press & J_A) {
            if (!(G.plots & ((uint16_t)1 << plot))) { ui_end(); buy_plot_prompt(plot); ui_begin(); return; }
        } else if (joy_press & (J_B | J_START)) { sfx(SFX_CANCEL); return; }
        wait_vbl_done();
    }
}
