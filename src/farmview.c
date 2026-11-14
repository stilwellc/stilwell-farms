// Stilwell Farms -- what each farm cell looks like (bank 0, called per drawn cell)
#pragma bank 255
#include <gb/gb.h>
#include "game.h"
BANKREF(world_cell_mt)
#include "decor.h"

static uint8_t is_water(int8_t x, int8_t y) {
    if (x < 0 || y < 0 || x >= WW || y >= WH) return 1;
    {
        uint16_t i = WIDX(x, y);
        uint8_t c = G.cell[i];
        return c == C_WATER || (c == C_DECOR && G.a1[i] == DK_BRIDGE);  // bridges count
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

mt_t bld_mt(uint8_t slot, uint8_t part) BANKED {
    Building *b = &G.bld[slot];
    uint8_t st = G.style & 3, ruin = b->flags & BF_RUIN;
    switch (b->type) {
    case B_HOUSE: {
        uint8_t lv = (b->flags & BF_LVL) >> 1;
        return (lv == 0 ? ST_HOUSE_L0 : lv == 1 ? style_h1[st] : style_h2[st]) + part;
    }
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

mt_t world_cell_mt(uint8_t x, uint8_t y) BANKED {
    uint16_t i = WIDX(x, y);
    uint8_t a = G.a1[i];
    switch (G.cell[i]) {
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
        if (G.a2[i] < 10) return MT_SAPLING;
        if ((a & 31) >= TREE_RIPE_DAYS && season_ok_tree(kind)) return ripe_tree[kind];
        return kind == 3 ? MT_CHERRY_TREE : MT_FRUIT_TREE;
    }
    case C_GIANT: return ST_GIANT_PUMPKIN + (a & 3);
    case C_MACHINE: return machine_mt[(a & 3) % 3][G.a2[i] >= 0x80 ? 1 : 0];
    case C_STRUCT: return bld_mt(a, G.a2[i]);
    case C_DECOR: {
        const DecorDef *d = &decor[a];
        if (d->fence) return fence_mt[d->fence - 1][G.a2[i] % 3];
        if (a == DK_BRIDGE) return G.a2[i] ? MT_BRIDGE_V : MT_BRIDGE_H;
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
    uint16_t i;
    for (i = 0; i < 1024; i++)
        if (G.cell[i] == C_DECOR && (G.a1[i] == DK_LAMP || G.a1[i] == DK_STRING)) redraw_cell(i & 31, i >> 5);
}
