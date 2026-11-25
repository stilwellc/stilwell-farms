// Stilwell Farms -- cooking: recipes you learn, the kitchen (the fridge in your house), eating dishes
// from the bag, and the food buffs they give for the rest of the day (banked)
//
// Dish d is item I_DISH0 + d (game.h).  G.recipes has a bit per known dish.  Recipes come from
// Grandpa's cards in the fridge (2), the TV cooking show on Sundays (6), good friends in town (6),
// and the Valley Restoration (2, valley.c).  G.buff holds today's BUFF_* bits; the night clears it.
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(kitchen_open)

#define ANY_FISH 0xFE       // ingredient: any fish, the cheapest first
#define NO_ING 0xFF
#define FK_FRIDGE_KIND 4    // interior.c FK_FRIDGE
#define FULL 255            // energy: back to full

typedef struct { uint8_t it[3], n[3], energy, buff; } DishDef;
static const DishDef dish[NUM_DISHES] = {
    {{I_RADISH, I_CARROT, I_HERB}, {1, 1, 1}, 35, BUFF_GREEN},              // Salad
    {{I_EGG, I_MILK, NO_ING}, {2, 1, 0}, 45, 0},                            // Omelet
    {{ANY_FISH, I_HERB, NO_ING}, {1, 1, 0}, 30, BUFF_LUCKY},                // Fish Fry
    {{ANY_FISH, I_TOMATO, NO_ING}, {2, 1, 0}, 60, BUFF_LUCKY},              // Fish Stew
    {{I_CORN, I_MILK, I_HERB}, {1, 1, 1}, 60, BUFF_HEARTY},                 // Chowder
    {{I_PUMPKIN, I_MILK, NO_ING}, {1, 1, 0}, 80, BUFF_HEARTY},              // Pumpkin Soup
    {{I_BERRIES, I_STRAWB, I_EGG}, {1, 1, 1}, 40, BUFF_SPEED},              // Berry Pie
    {{I_APPLE, I_EGG, I_MILK}, {2, 1, 1}, 60, BUFF_SPEED},                  // Apple Pie
    {{I_CHEESE, I_EGG, I_MUSHROOM}, {1, 1, 1}, 75, BUFF_HEARTY},            // Casserole
    {{I_MUSHROOM, I_HERB, NO_ING}, {2, 1, 0}, 50, BUFF_GREEN},              // Risotto
    {{I_BEET, I_EGGPLANT, I_MUSHROOM}, {1, 1, 1}, 70, BUFF_GREEN},          // Veg Stew
    {{I_MELON, I_HONEY, NO_ING}, {1, 1, 0}, 40, BUFF_SPEED},                // Sorbet
    {{I_CRANBERRY, I_EGG, NO_ING}, {2, 1, 0}, 40, 0},                       // Muffin
    {{I_PEACH, I_MILK, NO_ING}, {2, 1, 0}, 55, BUFF_SPEED},                 // Cobbler
    {{I_SNOWPEA, I_MUSHROOM, I_HERB}, {1, 1, 1}, 55, BUFF_LUCKY},           // Stir-Fry
    {{I_PUMPSOUP, I_APPLEPIE, I_FISHSTEW}, {1, 1, 1}, FULL, BUFF_ALL},      // Feast
};
// how each recipe is learned: 0 Grandpa's cards, 1 the TV show, 2 a friend, 3 the valley
static const uint8_t source[NUM_DISHES] = {0, 0, 1, 2, 1, 2, 1, 2, 2, 1, 2, 3, 1, 2, 1, 3};
static const uint8_t tv_dish[6] = {2, 6, 4, 9, 12, 14};   // Fish Fry, Berry Pie, Chowder, Risotto, Muffin, Stir-Fry
static const uint8_t friend_dish[NUM_NPC] = {5, 8, 7, 13, 10, 3};   // Alyssa..Marcy: their own recipe
#define NUM_FISHO 11   // cheapest first: "any fish" ingredients use up minnows and sardines before the koi
static const uint8_t fish_order[NUM_FISHO] = {I_MINNOW, I_SARDINE, I_CARP, I_BASS, I_TROUT, I_FLOUNDER, I_CATFISH,
                                             I_SQUID, I_SNAPPER, I_TUNA, I_KOI};

static uint8_t known(uint8_t d) { return (G.recipes >> d) & 1; }

static uint16_t have(uint8_t it) {
    uint8_t k;
    uint16_t n = 0;
    if (it != ANY_FISH) return inv_count(it);
    for (k = 0; k < NUM_FISHO; k++) n += inv_count(fish_order[k]);
    return n;
}

static void take(uint8_t it, uint8_t n) {
    uint8_t k;
    if (it != ANY_FISH) { inv_take(it, n); return; }
    for (k = 0; k < NUM_FISHO && n; k++) n -= inv_take(fish_order[k], n);
}

static uint8_t can_make(uint8_t d) {   // how many we could cook right now
    const DishDef *f = &dish[d];
    uint8_t k, best = 99;
    uint16_t c;
    for (k = 0; k < 3; k++) {
        if (f->it[k] == NO_ING) continue;
        c = have(f->it[k]) / f->n[k];
        if (c < best) best = (uint8_t)c;
    }
    return best;
}

// ---------------------------------------------------------------- text helpers
static const char *const buff_name[4] = {"Brisk", "Hearty", "Lucky", "Green"};

void dish_line(uint8_t it) BANKED {   // "+60 energy" / "+60 Hearty" / "Full, all buffs" (fits 15 columns)
    const DishDef *f = &dish[it - I_DISH0];
    uint8_t k;
    s_clr();
    if (f->energy == FULL) { s_add("Full, all buffs"); return; }
    s_ch('+'); s_num(f->energy, 0); s_ch(' ');
    if (!f->buff) { s_add("energy"); return; }
    for (k = 0; k < 4; k++) if (f->buff & (1 << k)) { s_add(buff_name[k]); return; }
}

void cook_learn(uint8_t d) BANKED {
    G.recipes |= 1 << d;
    music_play(MUS_JINGLE_FANFARE);
    s_clr(); s_add("You learned the recipe for "); s_add(items[I_DISH0 + d].name); s_add("!");
    say(sbuf);
    music_for_scene();
}

static uint8_t has_fridge(void) {
    uint8_t k;
    for (k = 0; k < MAX_FURN; k++) if (G.furn[k][0] == FK_FRIDGE_KIND) return 1;
    return 0;
}

static void fridge_hint(void) {
    if (!has_fridge()) say("(Cooking needs a kitchen: buy a Fridge from your house's BUILD furniture list.)");
}

// TV: "Valley Kitchen" airs on Sundays (days 7, 14, 21, 28) and teaches the next recipe; 1 if it aired
uint8_t cook_tv(void) BANKED {
    uint8_t k, d;
    if (G.day % 7) return 0;
    set_speaker("TV");
    for (k = 0; k < 6; k++) {
        d = tv_dish[k];
        if (known(d)) continue;
        s_clr(); s_add("It's Valley Kitchen! Today we're making "); s_add(items[I_DISH0 + d].name); s_add(". Grab a pencil!");
        say(sbuf);
        ui_speaker = 0;
        cook_learn(d);
        fridge_hint();
        return 1;
    }
    say("It's Valley Kitchen! A rerun today, but it sure makes you hungry.");
    ui_speaker = 0;
    return 1;
}

// a good friend (2 hearts) shares their own recipe, once; the caller has set the speaker
void cook_friend(uint8_t npc) BANKED {
    uint8_t d = friend_dish[npc];
    if (G.friend[npc] < 100 || known(d)) return;
    s_clr(); s_add("You've been such a good friend. Here, my recipe for "); s_add(items[I_DISH0 + d].name); s_add("!");
    say(sbuf);
    cook_learn(d);
    fridge_hint();
}

// ---------------------------------------------------------------- eating (from the bag)
static const char *const buff_msg[4] = {
    "You feel light on your feet! (Brisk: you walk at a run today.)",
    "You feel strong! (Hearty: work costs about half the energy today.)",
    "You feel lucky! (Lucky: rarer fish bite, and sooner, today.)",
    "Your fingers feel extra green! (Green: better quality harvests today.)"};

uint8_t dish_eat(uint8_t it) BANKED {
    const DishDef *f = &dish[it - I_DISH0];
    uint16_t e;
    uint8_t k;
    if ((G.energy >= G.energy_max && (!f->buff || (G.buff & f->buff) == f->buff)) || G.ate >= G.energy_max) {
        say("You're too full to eat another bite.");   // food restores at most one full bar a day (docs/BALANCE.md)
        return 0;
    }
    dish_line(it);
    lbl_set(13);   // the effect line goes under the question
    s_clr(); s_add("Eat the "); s_add(items[it].name); s_add("?\n"); s_add(lbl[13]);
    if (!ui_yesno(sbuf)) return 0;
    inv_take(it, 1);
    e = (uint16_t)G.energy + f->energy;
    if (e > G.energy_max) e = G.energy_max;
    G.ate = (uint16_t)G.ate + (e - G.energy) > 255 ? 255 : G.ate + (uint8_t)(e - G.energy);
    G.energy = (uint8_t)e;
    G.buff |= f->buff;
    sfx(SFX_CONFIRM);
    hud_draw();
    s_clr(); s_add("Mmm, delicious! ");
    s_add(f->energy == FULL ? "You feel brand new." : "Your energy is back up.");
    say(sbuf);
    if (f->buff == BUFF_ALL) say("What a feast! Every food buff is yours for the rest of the day.");
    else for (k = 0; k < 4; k++) if (f->buff & (1 << k)) say(buff_msg[k]);
    return 1;
}

// ---------------------------------------------------------------- the kitchen
static void ing_line(uint8_t d) {   // icon + have/need for each ingredient: "@1/1 @0/2"
    const DishDef *f = &dish[d];
    uint8_t k, h;
    s_clr();
    for (k = 0; k < 3; k++) {
        if (f->it[k] == NO_ING) continue;
        if (k) s_ch(' ');
        s_ch((char)(0x80 + (f->it[k] == ANY_FISH ? I_MINNOW : f->it[k])));
        h = have(f->it[k]) > 9 ? 9 : (uint8_t)have(f->it[k]);
        s_num(h, 0); s_ch('/'); s_num(f->n[k], 0);
    }
}

static void dish_panel(uint8_t d) {
    static const char *const hint[4][2] = {
        {"Grandpa's card", ""}, {"Watch the cooking", "show on Sundays"},
        {"A good friend in", "town might share"}, {"Help restore the", "valley to earn it"}};
    ui_art_release();
    ui_panel(0, 12, 20, 5);
    if (!known(d)) {
        ui_char(1, 13, font_map['?' - 32]);
        ui_text(4, 13, "Unknown recipe");
        ui_text(2, 14, hint[source[d]][0]);
        ui_text(2, 15, hint[source[d]][1]);
        return;
    }
    ui_put_mt(1, 13, MT_DISH_SALAD + d);
    s_clr(); s_add(items[I_DISH0 + d].name); s_pad(10); s_ch('G'); s_num(items[I_DISH0 + d].price, 0);
    ui_text(4, 13, sbuf);
    ing_line(d);
    ui_text(4, 14, sbuf);
    if (dish[d].it[0] == ANY_FISH) ui_text(1, 15, "any");   // under the art: the fish can be any fish
    dish_line(I_DISH0 + d);
    ui_text(4, 15, sbuf);
}

static void cook(uint8_t d, uint8_t q, uint8_t fx, uint8_t fy) {
    const DishDef *f = &dish[d];
    uint8_t k, n, t;
    for (n = 0; n < q; n++)
        for (k = 0; k < 3; k++)
            if (f->it[k] != NO_ING) take(f->it[k], f->n[k]);
    inv_add(I_DISH0 + d, 0, q);
    G.stat_cooked += q;
    // chop, sizzle, steam: three puffs over the stove, then hold the dish up
    for (k = 0; k < 3; k++) {
        sfx(k == 0 ? SFX_CHOP : SFX_SIZZLE);
        fx_show(SPR_SMOKE, SPR_SMOKE_PAL, fx, fy);
        for (t = 0; t < 20; t++) { update_sprites(); wait_vbl_done(); }
    }
    music_play(MUS_JINGLE_BUY);
    s_clr(); s_ch('+'); s_num(q, 0); s_ch(' '); s_add(items[I_DISH0 + d].name);
    hud_flash(sbuf);
    player_pose(1, 24);   // hold the dish up (the field loop finishes the pose)
    for (t = 0; t < 16; t++) { update_sprites(); wait_vbl_done(); }
    music_for_scene();
}

// A on the fridge: the recipe book, two pages of 8, and cooking
void kitchen_open(uint8_t fx, uint8_t fy) BANKED {
    static uint8_t page, hv;
    uint8_t r, k, d, q;
    if (!(G.cook_flags & 1)) {
        G.cook_flags |= 1;
        G.recipes |= 3;
        say("Taped inside the fridge door are two of Grandpa's old recipe cards: Salad and Omelet!");
        say("Cook from the recipe book here, then eat dishes from your BAG for energy and a daily buff.");
        say("More recipes come from the TV's Sunday cooking show, good friends, and the valley itself.");
    }
    while (1) {
        ui_begin();
        for (k = 0; k < 8; k++) {
            d = page * 8 + k;
            s_clr();
            if (known(d)) {
                s_ch((char)(0x80 + I_DISH0 + d)); s_ch(' '); s_add(items[I_DISH0 + d].name);
                if (can_make(d)) { s_pad(15); s_ch((char)0xF1); }   // gold star: you have everything
            } else s_add("? ---------");
            lbl_set(k);
        }
        s_clr(); s_add(page ? "Back..." : "More..."); lbl_set(8);
        ui_hover = &hv;
        while (1) {
            if (hv < 8) dish_panel(page * 8 + hv);
            else {
                ui_art_release();
                ui_panel(0, 12, 20, 5);
                for (q = 0, k = 0; k < NUM_DISHES; k++) q += known(k);
                s_clr(); s_add("Recipes known "); s_num(q, 0); s_ch('/'); s_num(NUM_DISHES, 0);
                ui_text(2, 13, sbuf);
                s_clr(); s_add("Dishes cooked "); s_num(G.stat_cooked, 0);
                ui_text(2, 15, sbuf);
            }
            r = ui_menu(0, 0, 20, lp, 9, hv);
            if (r != 0xFE) break;
        }
        ui_hover = 0;
        if (r == 0xFF) { ui_end(); return; }
        if (r == 8) { page ^= 1; ui_end(); continue; }
        d = page * 8 + r;
        if (!known(d)) { ui_dialog("You haven't learned that recipe yet."); ui_end(); continue; }
        q = can_make(d);
        if (!q) { ui_dialog("You're missing some of the ingredients."); ui_end(); continue; }
        q = ui_quantity(q, items[I_DISH0 + d].price);
        ui_end();
        if (!q) continue;
        cook(d, q, fx, fy);
        goals_check();
        return;
    }
}
