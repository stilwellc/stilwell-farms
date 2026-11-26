// Stilwell Farms -- the wider country: stagecoach and trading posts (banked)
//
// Eleven places beyond the farm and the town (areas.c draws them and grows their daily finds). Every one has a
// TRADING POST, the crate with a ledger on it, and every post has its own prices:
//   it BUYS each kind of good at its own rate, from half the usual price (what the place makes itself) to double
//   (what it cannot get): rate[place][group] in tenths. The shipping bin at home pays the plain price for
//   everything; carrying the right load to the right place is how a farmer gets rich.
//   it SELLS three local goods at four fifths of the usual price, three of each a day.
// Stonegate's collectors also pay double (not half again) for gold-star goods.
// The STAGECOACH waits at the Crossroads Market sign every day, to the six far places, for a fare.
//
// Save: G.post_stock[place] (two bits per good: how many bought today), cleared with the day (areas.c).
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(post_act)

#define NPLACE 11
#define AI(s) ((uint8_t)((s) - MAP_ISLAND))     // 0 island, market, pasture, quarry, woods, amberwood, harbour, lakeshore, mesa, pinecrest, stonegate
#define NGROUP 11                               // museum.c item_group: crops fruit farm wild river ore gems preserves sea beach dishes
static const uint8_t rate[NPLACE][NGROUP] = {
    {10, 12, 10, 10, 12, 10, 10, 12,  5,  5, 14},     // Shell Island: wants for nothing from the sea
    {11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},     // Crossroads Market: a little over the odds for anything
    {12, 12,  6,  6, 14, 10, 10, 14, 14, 12, 15},     // High Pasture
    {12, 13, 12, 12, 13,  5,  6, 13, 13, 10, 16},     // Granite Quarry
    {11, 11, 11,  5, 10, 12, 12, 13, 14, 13, 14},     // Old Woods
    {10, 10, 11,  5, 11, 16, 17, 12, 12, 12, 13},     // Amberwood: craftsmen want ore and gems
    {16, 15, 16, 11,  9, 11, 11, 13,  5,  6, 12},     // Saltmarsh Harbour: export ships want crops and farm goods
    {11, 12, 11, 11,  5, 11, 12, 12, 17, 18, 13},     // Lakeshore: never seen the sea
    {12, 20, 13, 13, 20,  6,  7, 14, 18, 10, 15},     // Sunbake Mesa: no fruit, no fish
    {11, 13, 10, 12, 12, 12, 12, 18, 12, 11, 19},     // Pinecrest: preserves and hot dishes
    {10, 10, 10, 10, 10, 12, 14, 12, 10, 10, 12}};    // Stonegate: collectors (gold stars count double)
static const uint8_t stock[NPLACE][3] = {
    {I_SHELL, I_CORAL, I_SNAPPER}, {I_HONEY, I_CHEESE, I_JAM}, {I_HERB, I_WOOL, I_HONEY}, {I_COPPER, I_IRON, I_COAL},
    {I_MUSHROOM, I_BERRIES, I_HERB}, {I_MUSHROOM, I_BERRIES, I_CIDER}, {I_SARDINE, I_FLOUNDER, I_TUNA},
    {I_BASS, I_TROUT, I_CARP}, {I_GOLD, I_GEM, I_TOPAZ}, {I_WOOL, I_COAL, I_CHEESE}, {I_RUBY, I_EMERALD, I_HONEY}};

// tenths this place pays for item it, before anything that changes day to day
uint8_t region_rate(uint8_t place, uint8_t it) BANKED { return rate[place][item_group(it)]; }
uint8_t region_stock(uint8_t place, uint8_t k) BANKED { return stock[place][k]; }

static uint8_t glut_of(uint8_t a, uint8_t g) {
    uint8_t i = a * NGROUP + g, v = G.glut[i >> 1];
    return (i & 1) ? v >> 4 : v & 15;
}
static void glut_add(uint8_t a, uint8_t g, uint8_t n) {
    uint8_t i = a * NGROUP + g, v = glut_of(a, g) + n;
    if (v > 15) v = 15;
    G.glut[i >> 1] = (i & 1) ? (G.glut[i >> 1] & 0x0F) | (v << 4) : (G.glut[i >> 1] & 0xF0) | v;
}

// what this post pays right now: the place's rate, +5 % a star of standing here, +10 % once its errand is done,
// +50 % if today's news is about this good here, +30 % on the place's fair day, -3 % a notch of glut
static uint16_t pays(uint8_t it, uint8_t q) {
    uint8_t a = AI(scene), g = item_group(it);
    uint16_t pct = 100 + 5 * region_stars(a) - 3 * glut_of(a, g);
    uint32_t v = items[it].price;
    if (region_errand(a) == 3) pct += 10;
    if (news_place() == a && news_group() == g) pct += 50;
    if (region_fair(a)) pct += 30;
    if (q == 2) v += (scene == MAP_STONEGATE) ? v : v >> 1;
    else if (q == 1) v += v >> 2;
    v = v * rate[a][g] / 10 * pct / 100;
    return v > 65535 ? 65535 : (uint16_t)v;
}

// the best your offices can do for a good shipped from home: tenths, never under 10 (sell_value, daily.c)
uint8_t office_rate(uint8_t it) BANKED {
    uint8_t a, best = 10, g = item_group(it);
    if (!(G.office[0] | G.office[1])) return 10;
    for (a = 0; a < NPLACE; a++)
        if ((G.office[a >> 3] & (1 << (a & 7))) && rate[a][g] > best) best = rate[a][g];
    return best;
}

static void post_sell(void) {
    static uint8_t page;
    uint8_t list[9], lq[9], n, k, it, q, skip, r, qty;
    uint16_t v;
    while (1) {
        n = 0; skip = page * 8;
        for (it = 0; it < NUM_ITEMS && n < 9; it++)
            for (q = 0; q < 3 && n < 9; q++) {
                if (!G.inv[it][q]) continue;
                if (skip) { skip--; continue; }
                list[n] = it; lq[n++] = q;
            }
        if (!n && !page) { say("Your bag's empty! Bring a load next time. I pay by what's scarce round here."); return; }
        if (!n) { page = 0; continue; }
        for (k = 0; k < n && k < 8; k++) {
            s_clr(); s_ch((char)(0x80 + list[k])); s_add(items[list[k]].name);
            if (lq[k]) s_ch(lq[k] == 2 ? (char)0xF1 : (char)0xF0);
            s_pad(12); s_num(pays(list[k], lq[k]), 5); lbl_set(k);
        }
        s_clr(); s_add(n > 8 ? "More..." : page ? "First..." : "Back"); lbl_set(k);
        ui_begin();
        r = ui_menu(0, 0, 20, lp, k + 1, 0);
        if (r == 0xFF) { ui_end(); page = 0; return; }
        if (r == k) {
            ui_end();
            if (n > 8) page++;
            else if (page) page = 0;
            else return;
            continue;
        }
        it = list[r]; q = lq[r]; v = pays(it, q);
        qty = ui_quantity(G.inv[it][q] > 99 ? 99 : G.inv[it][q], v);
        ui_end();
        if (!qty) continue;
        G.inv[it][q] -= qty;
        G.gold += (uint32_t)qty * v; G.earned += (uint32_t)qty * v;
        mark_shipped(it);
        region_sold(AI(scene), (uint32_t)qty * v);              // regions2.c: your standing here
        glut_add(AI(scene), item_group(it), (qty + 4) >> 3);    // a big load floods the market for a few days
        sfx(SFX_COIN);
        hud_draw();
    }
}

static void post_buy(void) {
    uint8_t a = AI(scene), k, r, left, it;
    uint16_t cost;
    while (1) {
        for (k = 0; k < 3; k++) {
            it = stock[a][k]; left = 3 - ((G.post_stock[a] >> (k * 2)) & 3);   // (three a day of each)
            s_clr(); s_ch((char)(0x80 + it)); s_add(items[it].name); s_pad(11); s_ch('x'); s_num(left, 0); s_num(items[it].price - items[it].price / 5, 5);
            lbl_set(k);
        }
        s_clr(); s_add("Back"); lbl_set(3);
        ui_begin();
        r = ui_menu(0, 0, 20, lp, 4, 0);
        ui_end();
        if (r > 2) return;
        it = stock[a][r]; cost = items[it].price - items[it].price / 5;
        if (((G.post_stock[a] >> (r * 2)) & 3) == 3) { say("That's the last of it till tomorrow. Three a day is all I can spare."); continue; }
        if (G.gold < cost) { say("You're a little short for that one."); continue; }
        G.gold -= cost;
        G.post_stock[a] += 1 << (r * 2);
        inv_add(it, 0, 1);
        sfx(SFX_COIN);
        hud_draw();
    }
}

static const char *const keeper[NPLACE] = {"Pearl", "Hank", "Shep", "Flint", "Fern", "Wren", "Captain", "Isla", "Dusty", "Nora", "Sage"};

void post_act(void) BANKED {
    static const char *const pitch[NPLACE] = {
        "Out here it's shells, shells, shells. Bring me something that grew in dirt!",
        "Market rates: a little over the odds for anything, and no questions.",
        "Herbs and wool we've got. Fish, though, or a pot of something cooked...",
        "Rock we have. I'd give my hammer for a hot dinner or a basket of fruit.",
        "The woods feed us mushrooms. Fish from the sea, though: now you're talking.",
        "Amberwood's carvers and smiths will take all the ore and gems you can carry.",
        "The ships sail full or not at all. Crops, milk, eggs, wool: top price, friend!",
        "Never seen the sea, me. A shell! A tuna! Name your price. Well, I'll name it.",
        "Nothing grows here and nothing swims. Fruit and fish: I pay DOUBLE.",
        "Long nights up here. Jam, cheese, anything hot in a bowl: we'll pay.",
        "Collectors, the lot of us. Bring your very finest. Gold stars count double here."};
    static const char *const friendly[NPLACE] = {
        "My favourite farmer! The gulls have been asking after you.",
        "There's my best customer. I kept the good scales out for you.",
        "The herd knows your step now. So do I. What've you brought us?",
        "You again! Good. The lads were hoping it was pie day.",
        "The woods are glad of you, and that's not a thing they say often.",
        "Come in, come in. I've carved your name on the good bench.",
        "Farmer on deck! Stand by to load. You're half the reason we sail full.",
        "I saved you the calm side of the dock. Tell me about the sea again?",
        "Well if it ain't the only green thing in this desert. Sit. Trade.",
        "In out of the cold with you! There's a chair by the stove with your name on it.",
        "Ah. The one visitor whose taste I trust. Show me everything."};
    uint8_t a = AI(scene), r, n, act[7];
    set_speaker(keeper[a]);
    say(region_stars(a) >= 2 ? friendly[a] : pitch[a]);
    region_news_line();                                  // regions2.c: today's word, if it is about this place
    while (1) {
        n = 0;
        s_clr(); s_add("Sell"); lbl_set(n); act[n++] = 0;
        s_clr(); s_add("Buy"); lbl_set(n); act[n++] = 1;
        if (region_errand_open(a)) { s_clr(); s_add("Errand"); lbl_set(n); act[n++] = 2; }
        if (carry_here(a)) { s_clr(); s_add("Carrier job"); lbl_set(n); act[n++] = 3; }
        if (region_stars(a) >= 2 && !(G.office[a >> 3] & (1 << (a & 7)))) { s_clr(); s_add("Open an office"); lbl_set(n); act[n++] = 4; }
        s_clr(); s_add("Almanac"); lbl_set(n); act[n++] = 5;
        s_clr(); s_add("Bye"); lbl_set(n); act[n++] = 6;
        ui_begin();
        r = ui_menu(0, 0, 18, lp, n, 0);
        ui_end();
        if (r >= n) break;
        r = act[r];
        if (r == 0) post_sell();
        else if (r == 1) post_buy();
        else if (r == 2) region_errand_do(a);
        else if (r == 3) carry_do(a);
        else if (r == 4) region_office(a);
        else if (r == 5) almanac_page();
        else break;
        set_speaker(keeper[a]);
    }
    ui_speaker = 0;
}

// the stagecoach at the Crossroads Market sign
void coach_act(void) BANKED {
    static const char *const cm[7] = {"Amberwood     150", "Saltm. Harbour 200", "Lakeshore     200", "Sunbake Mesa  300",
                                      "Pinecrest     300", "Stonegate     400", "Stay here"};
    static const uint16_t fare[6] = {150, 200, 200, 300, 300, 400};
    uint8_t r;
    ui_begin();
    ui_title(1, 0, "STAGECOACH");
    r = ui_menu(0, 1, 20, cm, 7, 0);
    ui_end();
    if (r > 5) return;
    if (G.weather == 3) { say("No coach today: the driver won't take the horses out in a thunderstorm."); return; }
    if (G.gold < fare[r]) { say("The driver shakes his head. That's not the fare."); return; }
    G.gold -= fare[r];
    hud_draw();
    sfx(SFX_STEP);
    enter_scene(MAP_AMBERWOOD + r, 8, 11, D_UP);
}
