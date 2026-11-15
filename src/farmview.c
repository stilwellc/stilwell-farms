// Stilwell Farms -- what each farm cell looks like (bank 0, called per drawn cell)
#pragma bank 255
#include <gb/gb.h>
#include "game.h"
BANKREF(world_cell_mt)
#include "decor.h"
#include "designer.h"

static uint8_t is_water(int8_t x, int8_t y) {
    if (x < 0 || y < 0 || x >= WW || y >= WH) return 1;
    {
        uint16_t i = WIDX(x, y);
        uint8_t c = W_CELL[i];
        return c == C_WATER || (c == C_DECOR && W_A1[i] == DK_BRIDGE);  // bridges count
    }
}

static const mt_t water_mt[16] = {
    MT_WATER_0, MT_WATER_1, MT_WATER_2, MT_WATER_3, MT_WATER_4, MT_WATER_5, MT_WATER_6, MT_WATER_7,
    MT_WATER_8, MT_WATER_9, MT_WATER_10, MT_WATER_11, MT_WATER_12, MT_WATER_13, MT_WATER_14, MT_WATER_15};

static mt_t water_tile(uint8_t x, uint8_t y) {
    uint8_t m = 0;
    if (is_water(x, (int8_t)y - 1)) m |= 1;
    if (is_water(x + 1, y)) m |= 2;
    if (is_water(x, y + 1)) m |= 4;
    if (is_water((int8_t)x - 1, y)) m |= 8;
    return water_mt[m];
}

static const mt_t style_h1[4] = {ST_CLASSIC_HOUSE1, ST_MODERN_HOUSE1, ST_CABIN_HOUSE1, ST_COTTAGE_HOUSE1};
static const mt_t style_h2[4] = {ST_CLASSIC_HOUSE2, ST_MODERN_HOUSE2, ST_CABIN_HOUSE2, ST_COTTAGE_HOUSE2};
static const mt_t style_barn[4] = {ST_CLASSIC_BARN, ST_MODERN_BARN, ST_CABIN_BARN, ST_COTTAGE_BARN};
static const mt_t style_coop[4] = {ST_COOP, ST_MODERN_COOP, ST_CABIN_COOP, ST_COTTAGE_COOP};
static const mt_t style_green[4] = {ST_GREENHOUSE, ST_MODERN_GREENHOUSE, ST_CABIN_GREENHOUSE, ST_COTTAGE_GREENHOUSE};
static const mt_t style_silo[4] = {ST_SILO, ST_MODERN_SILO, ST_CABIN_SILO, ST_COTTAGE_SILO};
static const mt_t style_shed[4] = {ST_SHED, ST_MODERN_SHED, ST_CABIN_SHED, ST_COTTAGE_SHED};

static mt_t type_mt(uint8_t type, uint8_t ruin, uint8_t lv, uint8_t part) {
    uint8_t st = G.style & 3;
    switch (type) {
    case B_HOUSE: return (lv == 0 ? ST_HOUSE_L0 : lv == 1 ? style_h1[st] : style_h2[st]) + part;
    case B_BARN: return (ruin ? ST_BARN_RUIN : style_barn[st]) + part;
    case B_COOP: return (ruin ? ST_COOP_RUIN : style_coop[st]) + part;
    case B_GREENHOUSE: return (ruin ? ST_GREENHOUSE_RUIN : style_green[st]) + part;
    case B_SILO: return style_silo[st] + part;
    case B_SHED: return style_shed[st] + part;
    case B_STABLE: return ST_STABLE + part;
    case B_WINDMILL: return ST_WINDMILL + part;
    case B_STAND: return ST_FARM_STAND + part;
    default: return ST_GAZEBO + part;
    }
}

mt_t bld_mt(uint8_t slot, uint8_t part) BANKED {
    Building *b = &G.bld[slot];
    return type_mt(b->type, b->flags & BF_RUIN, (b->flags & BF_LVL) >> 1, part);
}

mt_t bld_type_mt(uint8_t type, uint8_t part) BANKED { return type_mt(type, 0, 1, part); }

static const mt_t fence_mt[4][3] = {
    {MT_FENCE_POST, MT_FENCE_H, MT_FENCE_V},
    {MT_PICKET_POST, MT_PICKET_H, MT_PICKET_V},
    {MT_STONEWALL_POST, MT_STONEWALL_H, MT_STONEWALL_V},
    {MT_MODERNFENCE_POST, MT_MODERNFENCE_H, MT_MODERNFENCE_V},
};
static const mt_t ripe_tree[4] = {MT_FRUIT_TREE_RIPE, MT_PEACH_TREE_RIPE, MT_ORANGE_TREE_RIPE, MT_CHERRY_TREE_RIPE};
static const mt_t ore_mt[4] = {MT_ORE_COPPER, MT_ORE_IRON, MT_ORE_GOLD, MT_GEM_ROCK};
static const mt_t machine_mt[3][2] = {{MT_KEG, MT_KEG_READY}, {MT_CHEESE_PRESS, MT_PRESS_READY}, {MT_PRESERVE_JAR, MT_JAR_READY}};
static const mt_t forage_mt[3] = {MT_BERRY_BUSH, MT_MUSHROOM, MT_WILD_FLOWERS};

// ---- blueprint preview (designer.c): the pieces that would go down, drawn in place of the clear ground
static uint8_t gh_at(int8_t gx, int8_t gy) {   // piece code at a blueprint cell, 0 empty / outside
    uint8_t k, nb;
    if (gx < 0 || gy < 0 || (uint8_t)gx >= ghost_w || (uint8_t)gy >= ghost_h) return 0;
    k = (uint8_t)(gy << 3) | gx;
    nb = G.bp_cell[ghost_slot][k >> 1];
    nb = (k & 1) ? nb >> 4 : nb & 15;
    return nb ? G.bp_pal[ghost_slot][nb - 1] : 0;
}

static uint8_t gh_fence(int8_t gx, int8_t gy) {
    uint8_t c = gh_at(gx, gy);
    return c && c < PC_MACHINE ? decor[c].fence : 0;
}

static mt_t ghost_mt(uint8_t x, uint8_t y) {
    int8_t gx = x - ghost_x, gy = y - ghost_y, dx, dy;
    uint8_t c = gh_at(gx, gy), f;
    if (!c) {   // a cell inside a building's footprint: find its top-left cell
        for (dy = 0; dy < 3; dy++)
            for (dx = 0; dx < 3; dx++) {
                uint8_t t = gh_at(gx - dx, gy - dy) - PC_BLDG, w, h, i, j;
                if (t >= 16) continue;
                w = bld_w(t); h = bld_h(t);
                if ((uint8_t)dx >= w || (uint8_t)dy >= h) continue;
                for (j = 0; j < h; j++)
                    for (i = 0; i < w; i++)
                        if (!bk_fits(x - dx + i, y - dy + j, BK_BLDG, t)) return 0xFFFF;
                return bld_type_mt(t, dy * w + dx);
            }
        return 0xFFFF;
    }
    if (c >= PC_BLDG && c < PC_POND) {   // the building's own top-left cell
        uint8_t t = c - PC_BLDG, w = bld_w(t), h = bld_h(t), i, j;
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++)
                if (!bk_fits(x + i, y + j, BK_BLDG, t)) return 0xFFFF;
        return bld_type_mt(t, 0);
    }
    if (c < PC_MACHINE) {
        if (!bk_fits(x, y, BK_DECOR, c)) return 0xFFFF;
        f = decor[c].fence;
        if (f) {
            uint8_t hz = gh_fence(gx - 1, gy) == f || gh_fence(gx + 1, gy) == f;
            uint8_t vt = gh_fence(gx, gy - 1) == f || gh_fence(gx, gy + 1) == f;
            return fence_mt[f - 1][hz ? 1 : (vt ? 2 : 0)];
        }
        return c == DK_BRIDGE ? MT_BRIDGE_H : decor[c].mt;
    }
    if (c >= PC_POND) return bk_fits(x, y, BK_POND, 0) ? MT_WATER_15 : 0xFFFF;
    if (c >= PC_TREE) return bk_fits(x, y, BK_TREE, c & 15) ? MT_SAPLING : 0xFFFF;
    return bk_fits(x, y, BK_MACHINE, c & 15) ? machine_mt[(c & 3) % 3][0] : 0xFFFF;
}

mt_t world_cell_mt(uint8_t x, uint8_t y) BANKED {
    uint16_t i = WIDX(x, y);
    uint8_t a = W_A1[i];
    if (ghost_w && (uint8_t)(x - ghost_x) < ghost_w && (uint8_t)(y - ghost_y) < ghost_h) {
        mt_t m = ghost_mt(x, y);
        if (m != 0xFFFF) return m;
    }
    switch (W_CELL[i]) {
    case C_GRASS: return MT_GRASS;
    case C_GRASS2: return MT_GRASS2;
    case C_WILDFLOWER: return MT_WILDFLOWER;
    case C_WEEDS: return MT_WEEDS;
    case C_ROCK: return MT_ROCK;
    case C_BOULDER: return MT_BOULDER;
    case C_STUMP: return MT_STUMP;
    case C_LOG: return MT_LOG;
    case C_FOREST: return MT_FOREST;
    case C_DIRT: case C_ROAD: return MT_DIRT;
    case C_SAND: return MT_SAND;
    case C_CLIFF: return MT_CLIFF;
    case C_PINE: return MT_PINE;
    case C_BIRCH: return MT_BIRCH;
    case C_SAPLING_WILD: return MT_OAK_SAPLING;
    case C_BUSH: return a >= 5 ? MT_BERRY_BUSH : MT_BUSH;
    case C_FORAGE: return forage_mt[a % 3];
    case C_ORE: return ore_mt[a & 3];
    case C_WATER: return water_tile(x, y);
    case C_SOIL: {
        uint8_t c = a & SOIL_CROP, wet = a & SOIL_WET, g = SOIL_GROWN(i), d;
        if (!c) return wet ? MT_TILLED_WET : MT_TILLED_DRY;
        if (c == CROP_WITHERED) return MT_WITHERED;
        d = crops[c].days;
        if (g >= d) return crops[c].ripe_mt;
        if ((uint8_t)(g << 1) >= d) return wet ? MT_YOUNG_WET : MT_YOUNG_DRY;
        if (g) return wet ? MT_SPROUT_WET : MT_SPROUT_DRY;
        return wet ? MT_SEED_WET : MT_SEED_DRY;
    }
    case C_TREE: {
        uint8_t kind = a >> 5;
        if (W_A2(i) < 10) return MT_SAPLING;
        if ((a & 31) >= TREE_RIPE_DAYS && season_ok_tree(kind)) return ripe_tree[kind];
        return kind == 3 ? MT_CHERRY_TREE : MT_FRUIT_TREE;
    }
    case C_GIANT: return ST_GIANT_PUMPKIN + (a & 3);
    case C_MACHINE: return machine_mt[(a & 3) % 3][W_A2(i) >= 0x80 ? 1 : 0];
    case C_STRUCT: return bld_mt(a, W_A2(i));
    case C_DECOR: {
        const DecorDef *d = &decor[a];
        if (d->fence) return fence_mt[d->fence - 1][W_A2(i) % 3];
        if (a == DK_BRIDGE) return W_A2(i) ? MT_BRIDGE_V : MT_BRIDGE_H;
        if (G.time >= LAMP_TIME) {   // lamps glow from 7pm
            if (a == DK_LAMP) return MT_LAMP_ON;
            if (a == DK_STRING) return MT_STRINGLIGHTS_ON;
        }
        return d->mt;
    }
    }
    return MT_GRASS;
}

// the evening comes: redraw the lamps on screen in their lit look (mornings redraw the whole view)
void lamps_refresh(void) BANKED {
    uint16_t r, i;
    uint8_t n, *pc, *pe;
    scan_types(C_DECOR, C_DECOR);
    FOR_OWNED_CELLS(r, n, pc, pe)   // decor only stands on your land
        if (pc[0x1000] == DK_LAMP || pc[0x1000] == DK_STRING) { i = CELL_I(pc); redraw_cell(WIDX_X(i), WIDX_Y(i)); }
}
