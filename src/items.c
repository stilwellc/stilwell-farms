// Stilwell Farms -- item tables and inventory helpers (bank 0)
#include <gb/gb.h>
#include "game.h"

const char *const season_name[4] = {"Spring", "Summer", "Fall", "Winter"};

const ItemDef items[NUM_ITEMS] = {
    {"Radish", 30}, {"Carrot", 60}, {"Strawb.", 45}, {"Cauli.", 170}, {"Tomato", 40}, {"Corn", 105},
    {"Melon", 220}, {"Blueberry", 30}, {"Sunflower", 80}, {"Pumpkin", 195}, {"Beet", 50},
    {"Eggplant", 45}, {"Cranberry", 35}, {"Snow Pea", 100},
    {"Apple", 45}, {"Peach", 70}, {"Orange", 60}, {"Cherry", 50},
    {"Milk", 75}, {"Egg", 35}, {"Honey", 150},
    {"Berries", 30}, {"Mushroom", 60}, {"Herb", 40},
    {"Minnow", 15}, {"Bass", 70}, {"Trout", 100}, {"Catfish", 120}, {"Carp", 45}, {"Gold Koi", 600},
    {"Copper", 30}, {"Iron", 60}, {"Gold Ore", 150}, {"Amethyst", 250},
    {"Juice", 110}, {"Cider", 130}, {"Cheese", 180}, {"Jam", 100}, {"Pickles", 90},
    {"Wool", 260},
    {"Coal", 30}, {"Topaz", 400}, {"Emerald", 600}, {"Ruby", 800}, {"Diamond", 1200},
    {"Sardine", 30}, {"Flounder", 100}, {"Snapper", 150}, {"Tuna", 300}, {"Squid", 120},
    {"Shell", 35}, {"Coral", 80}, {"Sea Glass", 60}, {"Driftwood", 25},
    {"Salad", 210}, {"Omelet", 230}, {"Fish Fry", 150}, {"Fish Stew", 240}, {"Chowder", 350},
    {"Pump.Soup", 430}, {"Berry Pie", 180}, {"Apple Pie", 320}, {"Casserole", 440}, {"Risotto", 260},
    {"Veg Stew", 250}, {"Sorbet", 590}, {"Muffin", 170}, {"Cobbler", 340}, {"Stir-Fry", 320}, {"Feast", 1250},
};

// bank 0: callers pass their own string literals
void flash_gain(const char *what, uint8_t n) {
    sfx(SFX_COIN);
    s_clr(); s_ch('+'); s_num(n, 0); s_ch(' '); s_add(what);
    hud_flash(sbuf);
}

// crop id c (1..14) harvests item c-1
const CropDef crops[NUM_CROPS + 1] = {
    {0, 0, 0, 0, 0},
    {SEASON_SPRING, 15, 4, 0, MT_RIPE_RADISH},
    {SEASON_SPRING, 30, 6, 0, MT_RIPE_CARROT},
    {SEASON_SPRING, 80, 8, 3, MT_RIPE_STRAWB},
    {SEASON_SPRING, 70, 10, 0, MT_RIPE_CAULI},
    {SEASON_SUMMER, 55, 7, 3, MT_RIPE_TOMATO},
    {SEASON_SUMMER, 50, 9, 0, MT_RIPE_CORN},
    {SEASON_SUMMER, 95, 12, 0, MT_RIPE_MELON},
    {SEASON_SUMMER, 65, 10, 2, MT_RIPE_BLUEBERRY},
    {SEASON_SUMMER, 50, 6, 0, MT_RIPE_SUNFLOWER},
    {SEASON_FALL, 80, 10, 0, MT_RIPE_PUMPKIN},
    {SEASON_FALL, 25, 5, 0, MT_RIPE_BEET},
    {SEASON_FALL, 55, 7, 3, MT_RIPE_EGGPLANT},
    {SEASON_FALL, 80, 9, 3, MT_RIPE_CRANBERRY},
    {SEASON_WINTER, 50, 6, 0, MT_RIPE_SNOWPEA},
};

const uint8_t item_icon_tile[NUM_ITEMS] = {
    GL_IT_RADISH, GL_IT_CARROT, GL_IT_STRAWB, GL_IT_FLOWER, GL_IT_TOMATO, GL_IT_CORN, GL_IT_MELON,
    GL_IT_STRAWB, GL_IT_FLOWER, GL_IT_PUMPKIN, GL_IT_BEET, GL_IT_BEET, GL_IT_STRAWB, GL_IT_FORAGE,
    GL_IT_APPLE, GL_IT_FRUIT, GL_IT_FRUIT, GL_IT_STRAWB,
    GL_IT_MILK, GL_IT_EGG, GL_IT_HONEY,
    GL_IT_STRAWB, GL_IT_FORAGE, GL_IT_FLOWER,
    GL_IT_FISH, GL_IT_FISH, GL_IT_FISH, GL_IT_FISH, GL_IT_FISH, GL_IT_FISH,
    GL_IT_ORE, GL_IT_ORE, GL_IT_ORE, GL_IT_GEM,
    GL_IT_JUICE, GL_IT_JUICE, GL_IT_CHEESE, GL_IT_JAM, GL_IT_JAM,
    GL_IT_EGG,   // wool: a fluffy white ball
    GL_IT_COAL, GL_IT_JEWEL, GL_IT_JEWEL, GL_IT_JEWEL, GL_IT_JEWEL,
    GL_IT_FISH, GL_IT_FISH, GL_IT_FISH, GL_IT_FISH, GL_IT_FISH,
    GL_IT_SHELL, GL_IT_CORAL, GL_IT_GEM, GL_IT_WOOD,
    // dishes: bowls, pies, and plates that reuse the closest ingredient icon
    GL_IT_BOWL, GL_IT_EGG, GL_IT_FISH, GL_IT_BOWL, GL_IT_BOWL, GL_IT_BOWL, GL_IT_PIE, GL_IT_PIE,
    GL_IT_CHEESE, GL_IT_BOWL, GL_IT_BOWL, GL_IT_MELON, GL_IT_PIE, GL_IT_PIE, GL_IT_FORAGE, GL_IT_STAR2,
};

