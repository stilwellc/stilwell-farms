// Stilwell Farms -- town businesses: buy a vacant storefront, stock its pantry from your bag,
// and every night it turns ingredients into pies / ice cream / juice / bouquets and sells them.
// (banked)
//
// Map: art/maps/town.map stamps VACANT_SHOP at the four spots below; view.c asks biz_mt() for
// town cells and owned businesses draw their opened art (art/51_town_biz.art) over the vacant one.
// Save: G.biz_* (game.h "businesses" block). G.biz_staffed is the employees hook (see BIZ_NEEDS_STAFF).
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"

BANKREF(biz_act)

// Employees hook: when the employees system lands, set this to 1 (or make it a per-business rule)
// and have it set/clear bits in G.biz_staffed. Staffed businesses also make +2 products a night.
#define BIZ_NEEDS_STAFF 0

static const uint8_t spot_x[NUM_BIZ] = {24, 28, 3, 26};
static const uint8_t spot_y[NUM_BIZ] = {7, 7, 16, 16};
static const mt_t open_mt[NUM_BIZ] = {MT_BAKERY_0, MT_JUICE_BAR_0, MT_CREAMERY_0, MT_FLORIST_0};
static const uint16_t biz_price[NUM_BIZ] = {8000, 12000, 20000, 30000};
static const char *const biz_name[NUM_BIZ] = {"Bakery", "Juice Bar", "Creamery", "Florist"};
static const char *const prem_name[NUM_BIZ] = {"pies", "smoothies", "ice creams", "bouquets"};
static const char *const basic_name[NUM_BIZ] = {"tarts", "juices", "cheeses", "posies"};
static const char *const biz_pitch[NUM_BIZ] = {
    "Stock fruit, berries or pumpkins. With an egg AND milk too, they become pies worth far more!",
    "Stock fruit and veggies for juice. Two different kinds blend into pricier smoothies!",
    "Milk makes cheese. Milk, an egg and fruit or honey together make ice cream, the best seller!",
    "Flowers and harvest goods make posies. Add a herb for greenery and they become bouquets!"};

// pantry slot -> item, and what each ingredient can do
#define R_B 1   // base: one of each base goes into every premium product
#define R_M 2   // main: the star of a premium product
#define R_S 4   // sells alone as the basic product
static const uint8_t pan_item[NUM_BIZ][PANTRY] = {
    {I_STRAWB, I_BLUEBERRY, I_BERRIES, I_PUMPKIN, I_APPLE, I_CHERRY, I_EGG, I_MILK},
    {I_APPLE, I_ORANGE, I_PEACH, I_MELON, I_TOMATO, I_CARROT, I_CRANBERRY, I_BEET},
    {I_MILK, I_EGG, I_HONEY, I_STRAWB, I_PEACH, I_CHERRY, I_MELON, I_BLUEBERRY},
    {I_SUNFLOWER, I_SNOWPEA, I_CHERRY, I_PUMPKIN, I_CORN, I_CRANBERRY, I_BERRIES, I_HERB}};
static const uint8_t pan_role[NUM_BIZ][PANTRY] = {
    {6, 6, 6, 6, 6, 6, R_B, R_B},
    {6, 6, 6, 6, 6, 6, 6, 6},
    {R_B | R_S, R_B, R_M, R_M, R_M, R_M, R_M, R_M},
    {6, 6, 6, 6, 6, 6, 6, R_B}};
static const uint8_t cap_tab[3] = {6, 12, 20};          // ingredients used per night by level
static const uint16_t up_price[2] = {6000, 15000};
#define BASIC_Q(l) (10 + (l))    // basic product = 2.5x / 2.75x / 3x the ingredient (quarters)
#define PREM_Q(l) (14 + (l))     // premium = 3.5x / 3.75x / 4x the ingredients' total

static uint8_t m_prem[NUM_BIZ], m_basic[NUM_BIZ];

static uint8_t biz_at(uint8_t x, uint8_t y) {
    uint8_t b;
    for (b = 0; b < NUM_BIZ; b++)
        if ((uint8_t)(x - spot_x[b]) < 3 && (uint8_t)(y - spot_y[b]) < 3) return b;
    return 0xFF;
}

mt_t biz_mt(uint8_t x, uint8_t y) BANKED {
    uint8_t b = biz_at(x, y);
    if (b == 0xFF || !(G.biz_owned & (1 << b))) return 0xFFFF;
    return open_mt[b] + (y - spot_y[b]) * 3 + (x - spot_x[b]);
}

// ---------------------------------------------------------------- overnight
static uint8_t best(uint8_t b, uint8_t role, uint8_t skip) {   // highest-value slot with stock
    uint8_t k, r = 0xFF;
    for (k = 0; k < PANTRY; k++) {
        if (k == skip || !G.biz_pantry[b][k] || !(pan_role[b][k] & role)) continue;
        if (r == 0xFF || items[pan_item[b][k]].price > items[pan_item[b][r]].price) r = k;
    }
    return r;
}

static uint32_t run_one(uint8_t b) {
    uint8_t units = cap_tab[G.biz_level[b]], lv = G.biz_level[b], k, nb = 0, m, m2;
    uint32_t v = 0, sum;
    if (G.biz_staffed & (1 << b)) units += 2;
    for (k = 0; k < PANTRY; k++) if (pan_role[b][k] & R_B) nb++;
    while (units) {
        // premium: one of every base + the best main (no bases: two different mains)
        if (nb) {
            uint8_t ok = units > nb;
            for (k = 0; k < PANTRY; k++)
                if ((pan_role[b][k] & R_B) && !G.biz_pantry[b][k]) ok = 0;
            m = best(b, R_M, 0xFF);
            if (ok && m != 0xFF) {
                sum = items[pan_item[b][m]].price;
                G.biz_pantry[b][m]--;
                for (k = 0; k < PANTRY; k++)
                    if (pan_role[b][k] & R_B) { G.biz_pantry[b][k]--; sum += items[pan_item[b][k]].price; }
                v += sum * PREM_Q(lv) / 4;
                units -= nb + 1;
                m_prem[b]++;
                continue;
            }
        } else if (units >= 2 && (m = best(b, R_M, 0xFF)) != 0xFF && (m2 = best(b, R_M, m)) != 0xFF) {
            sum = (uint32_t)items[pan_item[b][m]].price + items[pan_item[b][m2]].price;
            G.biz_pantry[b][m]--; G.biz_pantry[b][m2]--;
            v += sum * PREM_Q(lv) / 4;
            units -= 2;
            m_prem[b]++;
            continue;
        }
        m = best(b, R_S, 0xFF);
        if (m == 0xFF) break;
        G.biz_pantry[b][m]--;
        v += (uint32_t)items[pan_item[b][m]].price * BASIC_Q(lv) / 4;
        units--;
        m_basic[b]++;
    }
    return v;
}

void biz_overnight(void) BANKED {
    uint8_t b;
    uint32_t v;
    for (b = 0; b < NUM_BIZ; b++) {
        m_prem[b] = m_basic[b] = 0;
        G.biz_today[b] = 0;
        G.biz_sold[b] = 0;
        if (!(G.biz_owned & (1 << b))) continue;
        if (BIZ_NEEDS_STAFF && !(G.biz_staffed & (1 << b))) continue;
        v = run_one(b);
        if (v > 65535) v = 65535;
        G.biz_today[b] = (uint16_t)v;
        G.biz_sold[b] = m_prem[b] + m_basic[b];
        G.biz_made[b] = G.biz_made[b] > 0xFF00 ? 0xFFFF : G.biz_made[b] + G.biz_sold[b];
        G.gold += v;
        G.earned += v;
    }
}

static uint8_t pantry_total(uint8_t b) {
    uint8_t k, n = 0;
    for (k = 0; k < PANTRY; k++) n += G.biz_pantry[b][k] ? 1 : 0;
    return n;
}

void biz_report(void) BANKED {
    uint8_t b;
    for (b = 0; b < NUM_BIZ; b++) {
        if (!(G.biz_owned & (1 << b))) continue;
        s_clr(); s_add("Your "); s_add(biz_name[b]);
        if (G.biz_today[b]) {
            s_add(" sold ");
            if (m_prem[b]) { s_num(m_prem[b], 0); s_ch(' '); s_add(prem_name[b]); }
            if (m_prem[b] && m_basic[b]) s_add(" and ");
            if (m_basic[b]) { s_num(m_basic[b], 0); s_ch(' '); s_add(basic_name[b]); }
            s_add(". It earned G"); s_num(G.biz_today[b], 0); s_ch('!');
        } else if (!pantry_total(b)) s_add("'s pantry is empty. Stock it up in town!");
        else continue;
        say(sbuf);
    }
}

// ---------------------------------------------------------------- the shop menu
static void wait_btn(void) {
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); return; }
        wait_vbl_done();
    }
}

static void stock(uint8_t b) {
    uint8_t k, n, map[PANTRY], r, it, have, room, q;
    while (1) {
        n = 0;
        for (k = 0; k < PANTRY; k++) {
            it = pan_item[b][k];
            if (!inv_count(it)) continue;
            s_clr(); s_ch((char)(0x80 + it)); s_add(items[it].name); s_pad(10);
            s_ch('x'); s_num(inv_count(it) > 99 ? 99 : inv_count(it), 2);
            s_pad(15); s_num(G.biz_pantry[b][k], 2);
            lbl_set(n); map[n++] = k;
        }
        if (!n) {
            say(b == 0 ? "You have nothing a bakery can use. Fruit, berries, pumpkins, eggs, milk!" :
                      b == 1 ? "You have nothing to juice. Bring fruit or veggies!" :
                      b == 2 ? "You have nothing for the creamery. Milk, eggs, honey or fruit!" :
                               "You have nothing for the florist. Flowers, harvest goods, herbs!");
            return;
        }
        ui_begin();
        if (n <= 5) {
            ui_box(0, 12, 20, 5);
            ui_text(2, 13, "x## = in your bag");
            ui_text(2, 15, " ## = on the shelf");
        }
        r = ui_menu(0, 0, 20, lp, n, 0);
        if (r == 0xFF) { ui_end(); return; }
        k = map[r]; it = pan_item[b][k];
        have = inv_count(it) > 99 ? 99 : (uint8_t)inv_count(it);
        room = 99 - G.biz_pantry[b][k];
        if (have > room) have = room;
        if (!have) { ui_dialog("That shelf is full!"); ui_end(); continue; }
        q = ui_quantity(have, items[it].price);
        ui_end();
        if (!q) continue;
        q = inv_take(it, q);
        G.biz_pantry[b][k] += q;
    }
}

static void status(uint8_t b) {
    uint8_t k, y = 8, x;
    ui_begin();
    ui_box(0, 0, 20, 17);
    s_clr(); s_add(biz_name[b]); s_add("  Level "); s_num(G.biz_level[b] + 1, 0);
    ui_text(1, 1, sbuf);
    s_clr(); s_add("Uses "); s_num(cap_tab[G.biz_level[b]] + ((G.biz_staffed >> b) & 1 ? 2 : 0), 0); s_add(" a night");
    ui_text(1, 3, sbuf);
    s_clr(); s_add("Last night G"); s_num(G.biz_today[b], 0);
    ui_text(1, 4, sbuf);
    s_clr(); s_add("Sold "); s_num(G.biz_sold[b], 0); s_add(" (ever "); s_num(G.biz_made[b], 0); s_ch(')');
    ui_text(1, 5, sbuf);
    ui_text(1, 7, "Shelf:");
    for (k = 0; k < PANTRY; k++) {
        uint8_t it = pan_item[b][k], i;
        const char *nm = items[it].name;
        x = (k & 1) ? 11 : 1;
        s_clr(); s_ch((char)(0x80 + it));
        for (i = 0; i < 5 && nm[i] && nm[i] != '.'; i++) s_ch(nm[i]);
        s_pad(6); s_num(G.biz_pantry[b][k], 2);
        ui_text(x, y + (k >> 1), sbuf);
    }
    {
        static const char *const frac[4] = {"", ".25", ".5", ".75"};
        uint8_t pq = PREM_Q(G.biz_level[b]), bq = BASIC_Q(G.biz_level[b]);
        s_clr(); s_num(pq >> 2, 0); s_add(frac[pq & 3]); s_add("x value "); s_add(prem_name[b]);
        ui_text(1, 13, sbuf);
        s_clr(); s_num(bq >> 2, 0); s_add(frac[bq & 3]); s_add("x value "); s_add(basic_name[b]);
        ui_text(1, 14, sbuf);
    }
    wait_btn();
    ui_end();
}

static void upgrade(uint8_t b) {
    uint8_t lv = G.biz_level[b];
    if (lv >= 2) { say("This shop is as grand as it gets!"); return; }
    s_clr(); s_add(lv ? "Hire a master chef? Uses 20 a night, best recipes.\nG" :
                        "Bigger kitchen? Uses 12 a night, better recipes.\nG");
    s_num(up_price[lv], 0);
    if (ui_yesno(sbuf) && pay(up_price[lv], 0, 0)) {
        G.biz_level[b]++;
        music_play(MUS_JINGLE_FANFARE);
        say("Done! It'll make more, and sell for more, starting tonight.");
        music_for_scene();
        hud_draw();
    }
}

static void buy(uint8_t b) {
    say("A boarded-up storefront. A sign on the door reads FOR SALE.");
    say(biz_pitch[b]);
    s_clr(); s_add("Open a "); s_add(biz_name[b]); s_add(" here for G"); s_num(biz_price[b], 0); s_ch('?');
    if (!ui_yesno(sbuf)) return;
    if (!pay(biz_price[b], 0, 0)) return;
    G.biz_owned |= 1 << b;
    sfx(SFX_COIN);
    music_play(MUS_JINGLE_BUY);
    redraw_rect(spot_x[b], spot_y[b], 3, 3);
    hud_draw();
    s_clr(); s_add("The "); s_add(biz_name[b]); s_add(" is yours! Stock its pantry and it sells every night.");
    say(sbuf);
    music_for_scene();
    goals_check();
}

void biz_act(uint8_t b) BANKED {
    static const char *const bm[4] = {"Stock", "Status", "Upgrade", "Bye"};
    uint8_t r;
    if (b >= NUM_BIZ) return;
    if (!(G.biz_owned & (1 << b))) { buy(b); return; }
    while (1) {
        ui_begin(); ui_money();
        s_clr(); s_add(biz_name[b]);
        ui_text(1, 10, sbuf);
        r = ui_menu(0, 0, 11, bm, 4, 0);
        ui_end();
        if (r == 0) stock(b);
        else if (r == 1) status(b);
        else if (r == 2) upgrade(b);
        else return;
    }
}
