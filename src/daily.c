// Stilwell Farms -- one small surprise most mornings (banked)
//
// Rolled overnight from the date (not rnd(): the night's other rolls stay where they were), announced in the
// morning report and on the TV, and gone the next day:
//   D_TRADER  Mike the travelling trader is under the west market tent in the square: three things at a
//             trader's price, one of each: rare seeds for a crop out of season, and two goods from far off
//   D_BOTTLE  a bottle on the beach: a recipe you don't know, or gold
//   D_HOT     one of the season's crops sells for double today (bin, stand and shops)
//   D_FISH    the fish are running: the first good cast today lands a koi (inland) or a tuna (the pier)
//
// Save: G.daily (which, 0 none), G.daily_arg (the hot item / the trader's stock seed), G.daily_flags
// (bit 0 bottle taken or fish landed; bits 1-3 the trader's three offers bought).
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(daily_roll)

static uint8_t mix(uint8_t v) { v ^= v << 3; v ^= v >> 5; v += 0x9B; v ^= v << 2; return v; }

static const uint8_t hot_pool[4][4] = {{I_RADISH, I_CARROT, I_STRAWB, I_CAULI}, {I_TOMATO, I_CORN, I_MELON, I_BLUEBERRY},
                                       {I_PUMPKIN, I_BEET, I_EGGPLANT, I_CRANBERRY}, {I_SNOWPEA, I_MILK, I_EGG, I_WOOL}};

void daily_roll(void) BANKED {
    uint8_t h = mix(mix((uint8_t)(G.day * 9 + G.season * 67)) ^ (uint8_t)(G.year * 29) ^ G.rng_hi), k = h & 7;
    G.daily_flags = 0;
    G.daily_arg = mix(h);
    if (G.year == 1 && G.season == SEASON_SPRING && G.day < 4) { G.daily = D_NONE; return; }   // the first days are busy enough
    G.daily = k < 2 ? D_TRADER : k < 4 ? D_HOT : k == 4 ? D_BOTTLE : k == 5 ? D_FISH : D_NONE;
    if (G.daily == D_HOT) G.daily_arg = hot_pool[G.season & 3][G.daily_arg & 3];
}

// twice the price for today's hot item (the bin, the stand and the shops ask here; farmhands and businesses don't)
uint16_t sell_value(uint8_t it, uint8_t q) BANKED {
    uint16_t v = item_value(it, q);
    uint8_t r = office_rate(it);                // regions.c: your offices away sell what you ship from home
    if (r > 10) v = (uint16_t)((uint32_t)v * r / 10);
    return (G.daily == D_HOT && it == G.daily_arg) ? v * 2 : v;
}

static void tell(void) {   // -> sbuf: today's news, or empty
    s_clr();
    switch (G.daily) {
    case D_TRADER: s_add("Mike the trader's wagon is in the town square today. He never stays long!"); break;
    case D_BOTTLE: if (!(G.daily_flags & 1)) s_add("Something washed up on the beach overnight. It glints in the sun."); break;
    case D_HOT: s_add("Buyers are in town for "); s_add(items[G.daily_arg].name); s_add(": it sells for DOUBLE today!"); break;
    case D_FISH: if (!(G.daily_flags & 1)) s_add("The fish are running today! They say the big ones are biting."); break;
    }
}

void daily_report(void) BANKED { tell(); if (sbuf[0]) say(sbuf); }

void daily_tv(void) BANKED {
    if (G.seen[0] | G.seen[1]) { regions_tv(); carry_tv(); }   // regions2.c: once you have been out in the country
    tell();
    if (!sbuf[0]) return;
    set_speaker("TV");
    say(sbuf);
    ui_speaker = 0;
}

// the fish that's running: 0xFF none (called once per cast, fishing.c)
uint8_t daily_fish(void) BANKED {
    if (G.daily != D_FISH || (G.daily_flags & 1) || rnd() >= 96) return 0xFF;
    G.daily_flags |= 1;
    return scene == MAP_BEACH ? I_TUNA : I_KOI;
}

void daily_bottle(void) BANKED {
    uint8_t d;
    uint16_t gold;
    G.daily_flags |= 1;
    sfx(SFX_COIN);
    say("A bottle, corked tight, with something rolled up inside...");
    for (d = 2; d < NUM_DISHES; d++)                         // (the first two cards are in the fridge)
        if (!(G.recipes & (1 << d)) && (G.daily_arg & 1)) { say("A recipe, in careful handwriting!"); cook_learn(d); return; }
    gold = 300 + (uint16_t)(G.daily_arg & 0x3F) * 40;
    G.gold += gold;
    s_clr(); s_add("A note: \"For whoever finds this.\" And G"); s_num(gold, 0); s_add(" in old coins!");
    say(sbuf);
    hud_draw();
}

// ---------------------------------------------------------------- Mike, under the west tent
static uint8_t far_good(uint8_t h) {   // something this valley doesn't hand you easily (never a crop or a dish)
    static const uint8_t far[16] = {I_HONEY, I_HERB, I_KOI, I_CATFISH, I_GOLD, I_GEM, I_CHEESE, I_JAM,
                                    I_TOPAZ, I_EMERALD, I_SNAPPER, I_SQUID, I_CORAL, I_SEAGLASS, I_PICKLES, I_WOOL};
    return far[h & 15];
}

static const uint8_t ahead[4][4] = {{5, 6, 7, 8}, {10, 11, 12, 13}, {14, 3, 7, 10}, {1, 2, 3, 4}};   // crops of the season to come

void daily_trader(void) BANKED {
    uint8_t r, k, crop, g1, g2;
    uint32_t cost[3];
    if (G.daily != D_TRADER) { say("An empty market tent. Traders from out of the valley stop here some days."); return; }
    crop = ahead[G.season & 3][G.daily_arg & 3];
    g1 = far_good(G.daily_arg >> 2); g2 = far_good(mix(G.daily_arg));
    if (g2 == g1) g2 = far_good(g1 + 5);
    cost[0] = (uint32_t)crops[crop].seed_cost * 3 * 5;
    cost[1] = (uint32_t)items[g1].price * 4;
    cost[2] = (uint32_t)items[g2].price * 4;
    set_speaker("Mike");
    say("Mike's the name, far-off goods are the game. Here today, gone tomorrow. Have a look!");
    while (1) {
        s_clr(); s_add("5 rare "); s_add(items[crop - 1].name); s_pad(13); s_num(cost[0], 5); lbl_set(0);
        s_clr(); s_ch((char)(0x80 + g1)); s_add(items[g1].name); s_pad(13); s_num(cost[1], 5); lbl_set(1);
        s_clr(); s_ch((char)(0x80 + g2)); s_add(items[g2].name); s_pad(13); s_num(cost[2], 5); lbl_set(2);
        s_clr(); s_add("Bye"); lbl_set(3);
        ui_begin();
        r = ui_menu(0, 0, 20, lp, 4, 0);
        ui_end();
        if (r > 2) break;
        k = 2 << r;
        if (G.daily_flags & k) { say("Sold that one already, friend. One of each is all the wagon carries."); continue; }
        if (G.gold < cost[r]) { say("A bit short today? It'll be gone tomorrow, mind."); continue; }
        G.gold -= cost[r];
        G.daily_flags |= k;
        if (r == 0) G.seeds[crop][R_RARE] = G.seeds[crop][R_RARE] > 250 ? 255 : G.seeds[crop][R_RARE] + 5;
        else inv_add(r == 1 ? g1 : g2, 0, 1);
        sfx(SFX_COIN);
        hud_draw();
        say("Pleasure doing business!");
    }
    ui_speaker = 0;
}
