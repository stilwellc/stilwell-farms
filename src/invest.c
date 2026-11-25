// Stilwell Farms -- investing in the town: Market Street (a district south of the park, bought at the Land
// Office: four lots for businesses you build, biz.c), community projects (Joe's INVEST board: a tiered
// track of gold + wood + stone donations that change the town), and Town Prosperity, which every
// business, storefront upgrade and project raises. Prosperity brings visitors: +5% business sales a level,
// traders' goods at Alyssa's (level 2), townsfolk who notice, and townsfolk who spend their evenings on
// Market Street. The Valley Restoration board (valley.c) stays separate: that one restores what was.
// (banked)
//
// Save: G.mk_flags (MK_OPEN + lot bits), G.town_proj (TP_* bits), G.invested, G.inv_tips (game.h).
#pragma bank 255
#include <gb/gb.h>
#include "game.h"
#include "routes.h"

BANKREF(invest_menu)

#define MARKET_PRICE 40000
#define TRADER_MULT 4           // traders' goods cost 4x: more than a business can sell them for (docs/BALANCE.md)
#define LOT_PRICE 15000
static const uint8_t pts_level[5] = {4, 12, 22, 34, 48};

// ---------------------------------------------------------------- the community projects
typedef struct { uint16_t gold; uint8_t wood, stone, need_p, market; } TProj;
static const TProj tproj[NUM_TPROJ] = {   // in pairs: tier I, II, III
    {20000, 0, 80, 0, 0},     // Square Paving: flagstones on every street, string lights over the plaza
    {15000, 40, 0, 0, 1},     // Playground (Market Street commons)
    {30000, 100, 0, 2, 0},    // Beach Boardwalk: planks down the south road to the shore
    {40000, 60, 30, 2, 1},    // Bandstand
    {35000, 40, 20, 3, 1},    // Community Garden
    {60000, 80, 80, 3, 1},    // Library
};
static const char *const tp_name[NUM_TPROJ] = {"Square Paving", "Playground", "Boardwalk", "Bandstand",
                                              "Comm. Garden", "Library"};   // 13 columns
static const char *const tp_desc[NUM_TPROJ][2] = {
    {"Flagstone streets,", "lights on the plaza"}, {"Swings and a slide", "on the commons"},
    {"Planks to the beach", "down the south road"}, {"Music on the", "commons, evenings"},
    {"Raised beds that", "the town tends"}, {"Books, history,", "a recipe or two"}};
static const char *const tp_done[NUM_TPROJ] = {
    "Look at those streets! Flagstones everywhere, and lights strung over the plaza. Lovely!",
    "The playground's open! Listen to those kids. Marcy's already been down the slide twelve times.",
    "The boardwalk's finished! A fine stroll from the square right down to the sea.",
    "The bandstand's up! There'll be music on the commons on warm evenings.",
    "The community garden's planted! Everyone takes a turn weeding. Even Brick.",
    "The library's open! Andy built the shelves, and half the town has donated books."};
// where the commons' projects stand (the town map reserves these sites: grass + a SITE_SIGN)
static const uint8_t site_x[NUM_TPROJ] = {0, 2, 0, 12, 7, 20};
static const uint8_t site_w[NUM_TPROJ] = {0, 3, 0, 4, 4, 3};
static const uint8_t site_h[NUM_TPROJ] = {0, 2, 0, 3, 2, 3};
#define SITE_Y 28

static uint8_t popcount8(uint8_t v) { uint8_t n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

uint8_t prosperity_pts(void) BANKED {
    uint8_t b, n = 0;
    for (b = 0; b < NUM_BIZ_ALL; b++) if (G.biz_owned & (1 << b)) n += b < NUM_BIZ ? 2 : 3;
    n += biz_tiers();
    if (G.mk_flags & MK_OPEN) n += 2;
    n += popcount8(G.town_proj & 0x3F) * 4;
    return n;
}

uint8_t prosperity(void) BANKED {
    uint8_t p = 0, n = prosperity_pts();
    while (p < 5 && n >= pts_level[p]) p++;
    return p;
}

static uint8_t tp_open(uint8_t k) {   // unlocked: prosperity, and Market Street for the commons
    return prosperity() >= tproj[k].need_p && (!tproj[k].market || (G.mk_flags & MK_OPEN));
}

// ---------------------------------------------------------------- the town, invested in
mt_t invest_mt(uint8_t x, uint8_t y) BANKED {
    uint8_t k;
    if (y >= SITE_Y) {   // the commons
        for (k = 1; k < NUM_TPROJ; k++) {
            if (!site_w[k] || !(G.town_proj & (1 << k))) continue;
            if ((uint8_t)(x - site_x[k]) < site_w[k] && (uint8_t)(y - SITE_Y) < site_h[k]) {
                static const mt_t first[NUM_TPROJ] = {0, ST_PLAYGROUND, 0, ST_BANDSTAND, ST_COMMUNITY_GARDEN, ST_LIBRARY};
                return first[k] + (y - SITE_Y) * site_w[k] + (x - site_x[k]);
            }
        }
    }
    if (y == MK_GATE_Y && (G.mk_flags & MK_OPEN) && (uint8_t)(x - 13) < 2) return ST_GATE_OPEN + (x - 13);
    return 0xFFFF;   // (paving, lights and the boardwalk are drawn by view.c directly: they cover the town)
}

// ---------------------------------------------------------------- shared bits of UI
static void wait_btn(void) {
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); return; }
        wait_vbl_done();
    }
}

static void stars(uint8_t x, uint8_t y, uint8_t p) {
    uint8_t k;
    for (k = 0; k < 5; k++) ui_char(x + k, y, k < p ? GL_IT_STAR2 : GL_CAL_DOT);
}

static void celebrate(const char *msg) {   // msg: a literal from this bank (say copies into the box)
    music_play(MUS_JINGLE_FANFARE);
    set_speaker("Joe");
    say(msg);
    ui_speaker = 0;
    music_for_scene();
    s_clr(); s_ch((char)0xF0); s_add(" Prosperity "); s_num(prosperity(), 0); s_ch(' '); s_ch((char)0xF0);
    hud_flash(sbuf);
}

// ---------------------------------------------------------------- Market Street
static void buy_market(void) {
    if (G.mk_flags & MK_OPEN) return;
    if (prosperity() < 1) {
        say("The council won't sell Market Street until the town's on its feet. Open a business or two first!");
        return;
    }
    say("Market Street: the old lane past the park, four building lots and the town commons. The council will sell it to you.");
    s_clr(); s_add("Buy Market Street for G"); s_num(MARKET_PRICE, 0); s_ch('?');
    if (!ui_yesno(sbuf) || !pay(MARKET_PRICE, 0, 0)) return;
    G.mk_flags |= MK_OPEN;
    G.invested += MARKET_PRICE;
    redraw_rect(13, MK_GATE_Y, 2, 1);
    hud_draw();
    music_play(MUS_JINGLE_BUY);
    say("The gate south of the park is open! Buy a lot there, and build a business of your own.");
    music_for_scene();
    goals_check();
}

static void buy_lot(uint8_t lot) {
    s_clr(); s_add("Lot "); s_num(lot + 1, 0); s_add(" on Market Street is for sale. Buy it for G"); s_num(LOT_PRICE, 0); s_ch('?');
    if (!ui_yesno(sbuf) || !pay(LOT_PRICE, 0, 0)) return;
    G.mk_flags |= 1 << lot;
    G.invested += LOT_PRICE;
    redraw_rect(0, MK_GATE_Y + 2, 32, 3);
    hud_draw();
    sfx(SFX_COIN);
    say("SOLD! Now choose what to build on it: walk up to the lot's sign.");
}

static void market_page(void) {
    uint8_t k, r, b, n;
    while (1) {
        if (!(G.mk_flags & MK_OPEN)) { buy_market(); if (!(G.mk_flags & MK_OPEN)) return; }
        for (k = 0; k < NUM_LOTS; k++) {
            b = biz_kind_at(k);
            s_clr(); s_add("Lot "); s_num(k + 1, 0); s_pad(7);
            if (b != 0xFF) {   // its name: biz.c's names are another bank's, so spelled here
                static const char *const nm[NUM_LOTS] = {"Brewery", "Winery", "Restaurant", "Inn"};
                s_add(nm[b - NUM_BIZ]);
                if (G.biz_look[b] & BL_BUILDING) s_add(" *");
            } else if (G.mk_flags & (1 << k)) s_add("bought");
            else { s_ch('G'); s_num(LOT_PRICE, 0); }
            lbl_set(k);
        }
        ui_begin(); ui_money();
        ui_panel(0, 11, 20, 6);
        for (n = 0, k = 0; k < NUM_LOTS; k++) n += (G.mk_flags >> k) & 1;
        s_clr(); s_add("Lots owned "); s_num(n, 0); s_add("/4"); ui_text(1, 12, sbuf);
        ui_text(1, 14, "* opens tomorrow");
        r = ui_menu(0, 3, 20, lp, NUM_LOTS, 0);
        ui_end();
        if (r == 0xFF) return;
        if (!(G.mk_flags & (1 << r))) buy_lot(r);
        else say(biz_kind_at(r) != 0xFF ? "Walk over to Market Street to run it." :
                                          "Walk up to the lot's sign on Market Street to choose what to build.");
    }
}

uint8_t invest_realtor(void) BANKED {
    static const char *const rm[2] = {"Farm land", "Market St"};
    uint8_t r;
    if (!(G.mk_flags & MK_OPEN) && prosperity() < 1) return 0;   // nothing in town for sale yet
    if (!(G.inv_tips & 2)) {
        G.inv_tips |= 2;
        set_speaker("Joe");
        say("Big news: the council's selling Market Street, past the park! Lots for new businesses.");
    }
    ui_begin(); ui_money();
    r = ui_menu(0, 3, 13, rm, 2, 0);
    ui_end();
    if (r == 0) return 0;
    if (r == 1) { set_speaker("Joe"); market_page(); }
    ui_speaker = 0;
    return 1;
}

// ---------------------------------------------------------------- the community board
static void tp_panel(uint8_t k) {
    ui_panel(0, 9, 20, 8);
    ui_text(1, 10, tp_desc[k][0]);
    ui_text(1, 11, tp_desc[k][1]);
    if (G.town_proj & (1 << k)) { ui_text(1, 13, "Built! Thank you."); return; }
    s_clr(); s_ch('G'); s_num(tproj[k].gold, 0);
    if (tproj[k].wood) { s_add(" W"); s_num(tproj[k].wood, 0); }
    if (tproj[k].stone) { s_add(" S"); s_num(tproj[k].stone, 0); }
    ui_text(1, 13, sbuf);
    s_clr();
    if (prosperity() < tproj[k].need_p) { s_add("Needs prosperity "); s_num(tproj[k].need_p, 0); }
    else if (tproj[k].market && !(G.mk_flags & MK_OPEN)) s_add("Needs Market St");
    else { s_add("Have W"); s_num(G.wood, 0); s_add(" S"); s_num(G.stone, 0); }
    ui_text(1, 15, sbuf);
}

static void complete(uint8_t k) {
    G.town_proj |= 1 << k;
    G.invested += tproj[k].gold;
    redraw_rect(0, 0, 32, 32);   // the town under the menus changes now
    ui_end();
    celebrate(tp_done[k]);
    goals_check();
    ui_begin();
}

static void projects(void) {
    uint8_t k, r, hv = 0;
    static const char *const tier_s[3] = {"I", "II", "III"};
    while (1) {
        for (k = 0; k < NUM_TPROJ; k++) {
            s_clr(); s_add(tp_name[k]); s_pad(14);
            if (G.town_proj & (1 << k)) s_ch((char)0xF1);
            else if (!tp_open(k)) s_ch('-');
            else s_add(tier_s[k >> 1]);
            lbl_set(k);
        }
        ui_begin();
        ui_hover = &hv;
        while (1) {
            tp_panel(hv);
            r = ui_menu(0, 0, 20, lp, NUM_TPROJ, hv);
            if (r != 0xFE) break;
        }
        ui_hover = 0;
        if (r == 0xFF) { ui_end(); return; }
        hv = r;
        if (G.town_proj & (1 << r)) ui_dialog("That one's built. The whole town thanks you!");
        else if (!tp_open(r)) {
            if (tproj[r].market && !(G.mk_flags & MK_OPEN)) ui_dialog("That goes on the commons on Market Street. Buy Market Street at Lots & Plots first!");
            else ui_dialog("The town isn't ready for that one yet. Raise its prosperity: businesses, storefronts, projects!");
        } else {
            s_clr(); s_add("Fund the "); s_add(tp_name[r]); s_add("?\n");
            cost_str(tproj[r].gold, tproj[r].wood, tproj[r].stone);
            if (ui_yesno(sbuf) && pay(tproj[r].gold, tproj[r].wood, tproj[r].stone)) {
                sfx(SFX_COIN);
                hud_draw();
                complete(r);
            }
        }
        ui_end();
    }
}

// ---------------------------------------------------------------- the overview page
static void overview(void) {
    static const char *const nm[NUM_BIZ_ALL] = {"Bakery", "Juice Bar", "Creamery", "Florist",
                                                "Brewery", "Winery", "Restaurant", "Inn"};
    uint8_t p = prosperity(), n = prosperity_pts(), b, k, segs;
    uint32_t total = 0;
    ui_begin();
    ui_box(0, 0, 20, 17);
    ui_title(2, 0, "INVESTMENTS");
    ui_text(1, 2, "Prosperity");
    stars(13, 2, p);
    if (p < 5) {   // progress toward the next level
        uint8_t lo = p ? pts_level[p - 1] : 0, hi = pts_level[p];
        segs = (uint8_t)((uint16_t)(n - lo) * 10 / (hi - lo));
        for (k = 0; k < 10; k++) ui_char(1 + k, 3, k < segs ? GL_BAR_FULL : GL_BAR_EMPTY);
        s_clr(); s_num(hi - n, 0); s_add(" to go"); ui_text(12, 3, sbuf);
    } else if (G.fund_lvl) { s_clr(); s_add("Town Fund level "); s_num(G.fund_lvl, 0); ui_text(1, 3, sbuf); }
    else ui_text(1, 3, "A thriving town!");
    s_clr(); s_add("Visitors +"); s_num(p * 5 + fund_pct(), 0); s_add("% sales"); ui_text(1, 4, sbuf);
    ui_text(1, 6, "Business    Night");
    for (b = 0; b < NUM_BIZ_ALL; b++) {
        uint16_t v = biz_income(b);
        s_clr(); s_add(nm[b]); s_pad(11);
        if (!(G.biz_owned & (1 << b))) s_add("   --");
        else { s_ch('G'); s_num(v, 5); }
        total += v;
        ui_text(1, 7 + b, sbuf);
    }
    s_clr(); s_add("Total"); s_pad(11); s_ch('G'); s_num(total > 99999 ? 99999 : total, 5); ui_text(1, 15, sbuf);
    s_clr(); s_add("Invested G"); s_num(G.invested, 0); ui_text(1, 16, sbuf);
    wait_btn();
    ui_end();
}

void invest_menu(void) BANKED {
    static const char *const im[5] = {"Overview", "Community", "Market St", "Town Fund", "Back"};
    uint8_t r;
    set_speaker("Joe");
    if (!(G.inv_tips & 1)) {
        G.inv_tips |= 1;
        say("You want to invest in our town? Wonderful! Every business you run makes the town more prosperous.");
        say("A prosperous town draws visitors, and visitors spend money. Fund a project and the whole town grows!");
    }
    ui_speaker = 0;
    while (1) {
        ui_begin();
        r = ui_menu(0, 0, 12, im, 5, 0);
        ui_end();
        if (r == 0) overview();
        else if (r == 1) projects();
        else if (r == 2) { set_speaker("Joe"); market_page(); ui_speaker = 0; }
        else if (r == 3) fund_page();
        else return;
    }
}

// ---------------------------------------------------------------- Market Street on foot
static void library_visit(void) {
    static const char *const books[4] = {
        "\"A History of Stilwell Valley\". Your grandpa's name is in the index, twice!",
        "\"The Patient Gardener\": fertilized soil gives better crops, and gold-star crops sell for double.",
        "\"Fish of the Valley\": the rarest fish bite in the rain. And they say luck helps.",
        "\"Running a Shop\": a fine storefront brings customers. A manager brings more."};
    uint8_t d;
    if (!(G.inv_tips & 4)) {   // the librarian's welcome present: a recipe you don't know yet
        G.inv_tips |= 4;
        for (d = 0; d < NUM_DISHES; d++)
            if (!(G.recipes & (1 << d))) {
                say("A donated cookbook falls open on a page with a card tucked into it...");
                cook_learn(d);
                return;
            }
    }
    if (!museum_act()) return;   // museum.c: donate / exhibits; 1 = a book off the shelf
    say(books[rnd() & 3]);
}

void invest_act(uint8_t act) BANKED {
    uint8_t lot, b, k;
    if (act == ACT_GATE) {
        if (G.mk_flags & MK_OPEN) say("MARKET STREET. Shops, lots and the town commons.");
        else say("MARKET STREET - FOR SALE. Enquire at Lots & Plots.");
        return;
    }
    if (act >= ACT_LOT0 && act <= ACT_LOT3) {
        lot = act - ACT_LOT0;
        b = biz_kind_at(lot);
        if (b != 0xFF) biz_act(b);
        else if (G.mk_flags & (1 << lot)) biz_build(lot);
        else buy_lot(lot);
        return;
    }
    k = act == ACT_PLAYGROUND ? 1 : act == ACT_BANDSTAND ? 3 : act == ACT_GARDEN ? 4 : 5;
    if (!(G.town_proj & (1 << k))) {
        s_clr(); s_add("FUTURE SITE: "); s_add(tp_name[k]); s_add(". Fund it on Mayor Joe's INVEST board.");
        say(sbuf);
        return;
    }
    switch (k) {
    case 1: say(milo_beach() ? "The swings creak in the breeze. Marcy's at the beach today." :
                               "Kids' shoes have worn a groove under every swing. Marcy's favourite place."); break;
    case 3: sfx(SFX_COIN); say("The bandstand. Someone's left a fiddle case on the steps for tonight."); break;
    case 4: say(G.season == SEASON_WINTER ? "The beds are tucked under straw for winter." :
                "Neat rows, a watering can, a sign: TAKE WHAT YOU NEED, PLANT WHAT YOU CAN."); break;
    default: library_visit(); break;
    }
}

// ---------------------------------------------------------------- what prosperity brings
static const char *const prosper_lines[NUM_NPC][2] = {
    {"Business is booming! Folks come from the next valley just to shop here.",
     "Traders stop by every week now. I've never seen the town so lively!"},
    {"More folk in town, more horseshoes to make. Hmph. Not complaining.",
     "The Anvil has a waiting list now. Your doing, farmer."},
    {"Market Street! I framed half those buildings, you know.",
     "Every storefront in town has had a facelift. I've never been busier!"},
    {"Bean There's full every lunch now. Visitors, everywhere!",
     "Folks drive in from the city for supper. Who'd have thought?"},
    {"The town's prospering, farmer! The council's never been happier.",
     "We're on the map! The county paper called us the gem of the valley."},
    {"There's so many new kids in town! We play tag on Market Street!",
     "Visitors ask ME for directions now. I'm basically the mayor."}};

uint8_t invest_line(uint8_t npc) BANKED {
    uint8_t p = prosperity();
    if (p < 2 || npc >= NUM_NPC || (rnd() & 1)) return 0;
    if (npc == NPC_MILO && (G.town_proj & TP_PLAYGROUND) && (rnd() & 1)) {
        say("The playground's the BEST. I went down the slide headfirst! Don't tell anyone.");
        return 1;
    }
    say(prosper_lines[npc][p >= 4]);
    return 1;
}

void invest_store(void) BANKED {   // Alyssa's traders' goods: other valleys' farm goods, at a mark-up
    static const uint8_t goods[3] = {I_EGG, I_MILK, I_HONEY};
    uint8_t k, r, q, it;
    uint16_t price;
    for (k = 0; k < 3; k++) {
        it = goods[k];
        s_clr(); s_ch((char)(0x80 + it)); s_add(items[it].name); s_pad(13); s_num(items[it].price * TRADER_MULT, 4);
        lbl_set(k);
    }
    ui_begin(); ui_money();
    r = ui_menu(0, 3, 20, lp, 3, 0);
    if (r != 0xFF) {
        it = goods[r];
        price = items[it].price * TRADER_MULT;
        q = ui_quantity(G.gold / price > 99 ? 99 : (uint8_t)(G.gold / price), price);
        if (q) { G.gold -= (uint32_t)q * price; inv_add(it, 0, q); hud_draw(); }
    }
    ui_end();
}

// townlife: once Market Street is open, some of the townsfolk's stops move there
uint8_t invest_spot(uint8_t npc, uint8_t s) BANKED {
    if (!(G.mk_flags & MK_OPEN)) return s;
    if (s == SP_MARKET) return SP_MKT_W;
    if (s == SP_BOARD) return SP_MKT_E;
    if ((G.town_proj & TP_BANDSTAND) && s == SP_PARK_C) return SP_BAND;
    if ((G.town_proj & TP_PLAYGROUND) && npc == NPC_MILO && (s == SP_PARK_S || s == SP_PARK_W)) return SP_PLAYGND;
    return s;
}
