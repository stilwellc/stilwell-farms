// Stilwell Farms -- item tables and inventory helpers (bank 0)
#include <gb/gb.h>
#include "game.h"

const char *const season_name[4] = {"Spring", "Summer", "Fall", "Winter"};

const ItemDef items[NUM_ITEMS] = {
    {"Radish", 50}, {"Carrot", 90}, {"Strawb.", 70}, {"Cauli.", 260}, {"Tomato", 60}, {"Corn", 160},
    {"Melon", 340}, {"Blueberry", 50}, {"Sunflower", 120}, {"Pumpkin", 300}, {"Beet", 80},
    {"Eggplant", 70}, {"Cranberry", 55}, {"Snow Pea", 150},
    {"Apple", 60}, {"Peach", 90}, {"Orange", 80}, {"Cherry", 70},
    {"Milk", 100}, {"Egg", 50}, {"Honey", 150},
    {"Berries", 30}, {"Mushroom", 60}, {"Herb", 40},
    {"Minnow", 15}, {"Bass", 70}, {"Trout", 100}, {"Catfish", 120}, {"Carp", 45}, {"Gold Koi", 600},
    {"Copper", 40}, {"Iron", 90}, {"Gold Ore", 200}, {"Gem", 400},
    {"Juice", 150}, {"Cider", 170}, {"Cheese", 240}, {"Jam", 140}, {"Pickles", 120},
    {"Wool", 340},
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
    {SEASON_SPRING, 20, 4, 0, MT_RIPE_RADISH},
    {SEASON_SPRING, 35, 6, 0, MT_RIPE_CARROT},
    {SEASON_SPRING, 100, 8, 3, MT_RIPE_STRAWB},
    {SEASON_SPRING, 90, 10, 0, MT_RIPE_CAULI},
    {SEASON_SUMMER, 70, 7, 3, MT_RIPE_TOMATO},
    {SEASON_SUMMER, 60, 9, 0, MT_RIPE_CORN},
    {SEASON_SUMMER, 120, 12, 0, MT_RIPE_MELON},
    {SEASON_SUMMER, 80, 10, 2, MT_RIPE_BLUEBERRY},
    {SEASON_SUMMER, 60, 6, 0, MT_RIPE_SUNFLOWER},
    {SEASON_FALL, 100, 10, 0, MT_RIPE_PUMPKIN},
    {SEASON_FALL, 30, 5, 0, MT_RIPE_BEET},
    {SEASON_FALL, 70, 7, 3, MT_RIPE_EGGPLANT},
    {SEASON_FALL, 100, 9, 3, MT_RIPE_CRANBERRY},
    {SEASON_WINTER, 60, 6, 0, MT_RIPE_SNOWPEA},
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
};

