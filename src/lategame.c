// Stilwell Farms -- the years after the valley is restored (banked)
//
// By the second winter every letter is read, the fountain runs and the town is at full prosperity. These
// keep the farm worth running after that, and both grow with the year:
//   Town Fund   Joe's INVEST board, once the town is at prosperity 5: give gold toward the next fund level.
//               Each level costs more than the last and never runs out; every level (up to FUND_CAP) adds
//               FUND_PCT % to what your businesses take and to what contracts pay.
//   Contracts   from year 2 the request board also posts three bulk orders each season: a field crop, a
//               farm good and something caught or dug. Deliver in parts, whenever you have some. Each pays
//               three times the goods' price; filling all three earns a contract star and a bonus. The
//               orders grow with the year and with your stars.
// (Grandpa's review comes every spring now and counts both: goals.c.)
//
// Save: G.fund_lvl, G.fund_paid, G.con_id, G.con_item/need/have[3], G.con_stars (game.h): all valid at zero.
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(fund_page)

#define FUND_PCT 2
#define FUND_CAP 25             // levels that still add to takings: +50 % at most
#define CON_PAY 3               // a contract pays this many times the goods' price

uint8_t fund_pct(void) BANKED { return (G.fund_lvl > FUND_CAP ? FUND_CAP : G.fund_lvl) * FUND_PCT; }

static uint32_t fund_cost(void) { return 100000UL + 50000UL * G.fund_lvl; }

// ---------------------------------------------------------------- the Town Fund (Joe)
void fund_page(void) BANKED {
    uint32_t left, give;
    set_speaker("Joe");
    if (prosperity() < 5) {
        say("The Town Fund opens once our town is at full prosperity: five stars. We're getting there!");
        ui_speaker = 0;
        return;
    }
    if (!G.fund_lvl && !G.fund_paid)
        say("Five stars! So what now? The Town Fund. Every level brings more visitors, and it never runs out.");
    while (1) {
        left = fund_cost() - G.fund_paid;
        s_clr(); s_add("Town Fund level "); s_num(G.fund_lvl, 0); s_add(": +"); s_num(fund_pct(), 0);
        s_add("% sales. Next level: G"); s_num(left, 0); s_add(" to go.");
        say(sbuf);
        if (!G.gold) { say("Come back when you've gold to spare!"); break; }
        give = G.gold < left ? G.gold : left;
        s_clr(); s_add("Give G"); s_num(give, 0); s_add(" to the fund?");
        if (!ui_yesno(sbuf)) break;
        G.gold -= give; G.invested += give; G.fund_paid += give;
        hud_draw();
        if (G.fund_paid < fund_cost()) { say("Every coin counts. Thank you!"); break; }
        G.fund_paid = 0;
        if (G.fund_lvl < 255) G.fund_lvl++;
        music_play(MUS_JINGLE_FANFARE);
        s_clr(); s_add("Town Fund level "); s_num(G.fund_lvl, 0); s_add("! Word's getting round: folks are coming from all over.");
        say(sbuf);
        music_for_scene();
    }
    ui_speaker = 0;
}

// ---------------------------------------------------------------- seasonal contracts (the request board)
static uint8_t con_tier(void) {   // 0..8: a year a step, and a step for every two stars
    uint8_t t = (G.year > 2 ? G.year - 2 : 0) + (G.con_stars >> 1);
    return t > 8 ? 8 : t;
}

static void con_post(void) {
    static const uint8_t crop[4][3] = {{I_RADISH, I_CARROT, I_CAULI}, {I_TOMATO, I_CORN, I_MELON},
                                       {I_PUMPKIN, I_BEET, I_EGGPLANT}, {I_SNOWPEA, I_SNOWPEA, I_SNOWPEA}};
    static const uint8_t good[4] = {I_MILK, I_EGG, I_WOOL, I_HONEY};
    static const uint8_t wild[4][3] = {{I_MINNOW, I_MUSHROOM, I_COPPER}, {I_BASS, I_BERRIES, I_COPPER},
                                       {I_TROUT, I_APPLE, I_IRON}, {I_CARP, I_COPPER, I_IRON}};
    uint8_t t = con_tier(), se = G.season & 3;
    G.con_item[0] = crop[se][rnd() % 3];
    G.con_need[0] = se == SEASON_WINTER ? 12 + 6 * t : 30 + 15 * t;   // winter fields are greenhouses
    G.con_item[1] = good[rnd() & 3];
    G.con_need[1] = 10 + 5 * t;
    G.con_item[2] = wild[se][rnd() % 3];
    G.con_need[2] = 6 + 3 * t;
    G.con_have[0] = G.con_have[1] = G.con_have[2] = 0;
    G.con_id = (uint8_t)(G.year * 4 + se) + 1;
}

static uint32_t con_reward(uint8_t k) {
    return (uint32_t)items[G.con_item[k]].price * G.con_need[k] * CON_PAY * (100 + fund_pct()) / 100;
}

static void con_deliver(uint8_t k) {
    uint8_t left = G.con_need[k] - G.con_have[k], n;
    uint16_t own = inv_count(G.con_item[k]);
    uint32_t pay;
    if (!left) { say("That order's filled and paid. Nicely done!"); return; }
    s_clr(); s_add(items[G.con_item[k]].name); s_add(": "); s_num(left, 0); s_add(" more wanted. It pays G");
    s_num(con_reward(k), 0); s_add(" when the order's full.");
    say(sbuf);
    if (!own) return;
    n = own < left ? (uint8_t)own : left;
    s_clr(); s_add("Hand over "); s_num(n, 0); s_ch(' '); s_add(items[G.con_item[k]].name); s_ch('?');
    if (!ui_yesno(sbuf)) return;
    G.con_have[k] += inv_take(G.con_item[k], n);
    if (G.con_have[k] < G.con_need[k]) { sfx(SFX_CONFIRM); say("Loaded on the cart. Bring the rest when you can!"); return; }
    pay = con_reward(k);
    G.gold += pay; G.earned += pay;
    music_play(MUS_JINGLE_FANFARE);
    s_clr(); s_add("Order filled! G"); s_num(pay, 0); s_add(" for you.");
    say(sbuf);
    if (G.con_have[0] >= G.con_need[0] && G.con_have[1] >= G.con_need[1] && G.con_have[2] >= G.con_need[2]) {
        pay = 5000UL * (con_tier() + 1);
        G.gold += pay; G.earned += pay;
        if (G.con_stars < 255) G.con_stars++;
        s_clr(); s_add("All three orders filled! A contract star, and a G"); s_num(pay, 0); s_add(" bonus. Star ");
        s_num(G.con_stars, 0); s_ch('!');
        say(sbuf);
    }
    music_for_scene();
    hud_draw();
}

void contracts_board(void) BANKED {
    uint8_t k, r;
    if (G.year < 2) return;
    if (G.con_id != (uint8_t)(G.year * 4 + (G.season & 3)) + 1) {
        con_post();
        say("New on the board: this season's CONTRACTS. Three big orders from out of town, due by season's end.");
    }
    while (1) {
        for (k = 0; k < 3; k++) {
            s_clr(); s_add(items[G.con_item[k]].name); s_pad(11);
            if (G.con_have[k] >= G.con_need[k]) s_add("   DONE");
            else { s_num(G.con_have[k], 3); s_ch('/'); s_num(G.con_need[k], 0); }
            lbl_set(k);
        }
        s_clr(); s_add("Back"); lbl_set(3);
        ui_begin();
        r = ui_menu(0, 0, 20, lp, 4, 0);
        ui_end();
        if (r > 2) return;
        con_deliver(r);
    }
}
