// Stilwell Farms -- town businesses: buy a vacant storefront (or build on a Market Street lot), stock its
// pantry from your bag, and every night it turns ingredients into pies / ales / wines / plates... and
// sells them. Every business can be renovated in the farm's four building styles and given facade
// upgrades (awning, window boxes, patio + lanterns) that bring in customers, and can hire a manager.
// (banked)
//
// Businesses: 0 Bakery, 1 Juice Bar, 2 Creamery, 3 Florist (the storefronts town.map stamps VACANT_SHOP on;
// their state is the original G.biz_* block), 4 Brewery, 5 Winery, 6 Restaurant, 7 Inn (built on Market
// Street lots, invest.c; state in G.bz_*). G.biz_owned / G.biz_staffed have a bit per business, G.biz_look
// its style + facade tier. view.c asks biz_mt() for town cells: owned businesses draw their storefront kit
// (art/52_town_invest.art) over the vacant one / the lot.
//
// The night's takings: ingredients used = kitchen level (6/12/20) + facade tier + 4 with a manager; value =
// the ingredients x the recipe's multiple, x (100% + BIZ_PCT_TIER a facade tier + BIZ_PCT_PROSP a prosperity
// level + BIZ_PCT_MGR with a manager + BIZ_PCT_INN once the Inn brings visitors). docs/BALANCE.md has the why.
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"

BANKREF(biz_act)

static const uint8_t spot_x[NUM_BIZ] = {24, 28, 3, 26};
static const uint8_t spot_y[NUM_BIZ] = {7, 7, 16, 16};
static const uint8_t lot_x[NUM_LOTS] = {2, 7, 20, 26};
#define LOT_Y 23
static const uint16_t biz_price[NUM_BIZ_ALL] = {10000, 16000, 25000, 35000, 45000, 50000, 60000, 65000};
static const char *const biz_name[NUM_BIZ_ALL] = {"Bakery", "Juice Bar", "Creamery", "Florist",
                                                  "Brewery", "Winery", "Restaurant", "Inn"};
static const char *const biz_sign[NUM_BIZ_ALL] = {"Rise & Pie", "Main Squeeze", "Udder Delight", "Petal Pushers",
                                                  "Hoppy Days", "Grape Escape", "Thyme & Again", "Snooze Inn"};   // the name over the door, 13 columns
static const char *const prem_name[NUM_BIZ_ALL] = {"pies", "smoothies", "ice creams", "bouquets",
                                                   "craft ales", "reserve wines", "chef's specials", "breakfasts"};
static const char *const basic_name[NUM_BIZ_ALL] = {"tarts", "juices", "cheeses", "posies",
                                                    "lagers", "table wines", "plates", "snacks"};
static const char *const biz_pitch[NUM_BIZ_ALL] = {
    "Stock fruit, berries or pumpkins. With an egg AND milk too, they become pies worth far more!",
    "Stock fruit and veggies for juice. Two different kinds blend into pricier smoothies!",
    "Milk makes cheese. Milk, an egg and fruit or honey together make ice cream, the best seller!",
    "Flowers and harvest goods make posies. Add a herb for greenery and they become bouquets!",
    "Corn, pumpkins, berries and honey brew into lagers. A herb for the bitter makes craft ales!",
    "Fruit and cider make table wine. Juice from your kegs with fruit ages into reserve wines!",
    "Serve your cooked dishes! With fresh produce on the side, a dish becomes a chef's special.",
    "Guests pay for rooms every night. Eggs and milk with honey, jam or cheese make breakfasts!"};
static const char *const style_name[4] = {"Classic", "Modern", "Log Cabin", "Cottage"};
static const char *const tier_name[3] = {"Awning", "Window boxes", "Patio+lights"};
static const char *const mgr_name[NUM_BIZ_ALL] = {"Bea", "Cal", "Dell", "Fern", "Hugo", "Iris", "Jules", "Kit"};

// pantry slot -> item, and what each ingredient can do
#define R_B 1   // base: one of each base goes into every premium product
#define R_M 2   // main: the star of a premium product
#define R_S 4   // sells alone as the basic product
#define R_X 8   // side: any one side + the best main makes a premium product (the restaurant)
#define NONE 0xFF
static const uint8_t pan_item[NUM_BIZ_ALL][PANTRY] = {
    {I_STRAWB, I_BLUEBERRY, I_BERRIES, I_PUMPKIN, I_APPLE, I_CHERRY, I_EGG, I_MILK},
    {I_APPLE, I_ORANGE, I_PEACH, I_MELON, I_TOMATO, I_CARROT, I_CRANBERRY, I_BEET},
    {I_MILK, I_EGG, I_HONEY, I_STRAWB, I_PEACH, I_CHERRY, I_MELON, I_BLUEBERRY},
    {I_SUNFLOWER, I_SNOWPEA, I_CHERRY, I_PUMPKIN, I_CORN, I_CRANBERRY, I_BERRIES, I_HERB},
    {I_CORN, I_PUMPKIN, I_BLUEBERRY, I_CRANBERRY, I_CHERRY, I_BERRIES, I_HONEY, I_HERB},
    {I_JUICE, I_CIDER, I_APPLE, I_PEACH, I_CHERRY, I_BLUEBERRY, I_CRANBERRY, I_STRAWB},
    {NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE},                       // the restaurant: rest_item[]
    {I_EGG, I_MILK, I_HONEY, I_BERRIES, I_CHEESE, I_JAM, I_JUICE, I_MUFFIN}};
static const uint8_t pan_role[NUM_BIZ_ALL][PANTRY] = {
    {6, 6, 6, 6, 6, 6, R_B, R_B},
    {6, 6, 6, 6, 6, 6, 6, 6},
    {R_B | R_S, R_B, R_M, R_M, R_M, R_M, R_M, R_M},
    {6, 6, 6, 6, 6, 6, 6, R_B},
    {6, 6, 6, 6, 6, 6, 6, R_B},
    {R_B, 6, 6, 6, 6, 6, 6, 6},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {R_B, R_B, 6, 6, 6, 6, 6, 6}};
static const uint8_t cap_tab[3] = {6, 12, 20};          // ingredients used per night by level
static const uint16_t up_price[2] = {10000, 25000};
static const uint16_t tier_price[3] = {5000, 10000, 20000};
#define STYLE_PRICE 4000
// value multiples in quarters, + the kitchen level: basic 1.5x / 1.75x / 2x the ingredient, premium 2x..2.5x
// the ingredients' total; the restaurant's dishes are worth a lot already: 1x..1.5x plates, 1.25x..1.75x specials
static const uint8_t bq_base[NUM_BIZ_ALL] = {6, 6, 6, 6, 6, 6, 4, 6};
static const uint8_t pq_base[NUM_BIZ_ALL] = {8, 8, 8, 8, 8, 8, 5, 8};
#define BIZ_PCT_TIER 5          // % more takings a facade tier
#define BIZ_PCT_PROSP 4         // ... a Town Prosperity level
#define BIZ_PCT_MGR 10          // ... with a manager
#define BIZ_PCT_INN 10          // ... at every other business once the Inn is open
#define INN_RATE 60             // a room a night
#define MGR_WAGE(lv) (40 + 20 * (lv))

static uint8_t m_prem[NUM_BIZ_ALL], m_basic[NUM_BIZ_ALL], m_guests, m_quit, m_opened, m_empty;
static uint16_t m_wages;

// ---------------------------------------------------------------- per-business state (two save blocks)
#define OWNED(b) (G.biz_owned & (1 << (b)))
#define BUILDING(b) (G.biz_look[b] & BL_BUILDING)
static uint8_t *pan(uint8_t b) { return b < NUM_BIZ ? G.biz_pantry[b] : G.bz_pantry[b - NUM_BIZ]; }
static uint8_t *lvp(uint8_t b) { return b < NUM_BIZ ? &G.biz_level[b] : &G.bz_level[b - NUM_BIZ]; }
static uint16_t *todayp(uint8_t b) { return b < NUM_BIZ ? &G.biz_today[b] : &G.bz_today[b - NUM_BIZ]; }
static uint16_t *madep(uint8_t b) { return b < NUM_BIZ ? &G.biz_made[b] : &G.bz_made[b - NUM_BIZ]; }
static uint8_t *soldp(uint8_t b) { return b < NUM_BIZ ? &G.biz_sold[b] : &G.bz_sold[b - NUM_BIZ]; }
static uint8_t tier(uint8_t b) { return (G.biz_look[b] & BL_TIER) >> 2; }
static uint8_t staffed(uint8_t b) { return (G.biz_staffed >> b) & 1; }
static uint8_t item_of(uint8_t b, uint8_t k) {
    if (b == BIZ_RESTAURANT) return G.rest_item[k] ? G.rest_item[k] - 1 : NONE;
    return pan_item[b][k];
}
static uint8_t role_of(uint8_t b, uint8_t k) {
    uint8_t it;
    if (b != BIZ_RESTAURANT) return pan_role[b][k];
    it = item_of(b, k);
    return it == NONE ? 0 : IS_DISH(it) ? R_M | R_S : R_X;
}
static uint8_t units_of(uint8_t b) { return cap_tab[*lvp(b)] + tier(b) + (staffed(b) ? 4 : 0); }

uint16_t biz_income(uint8_t b) BANKED { return b < NUM_BIZ_ALL && OWNED(b) ? *todayp(b) : 0; }
uint8_t biz_count(void) BANKED {
    uint8_t b, n = 0;
    for (b = 0; b < NUM_BIZ_ALL; b++) n += OWNED(b) ? 1 : 0;
    return n;
}
uint8_t biz_tiers(void) BANKED {
    uint8_t b, n = 0;
    for (b = 0; b < NUM_BIZ_ALL; b++) if (OWNED(b)) n += tier(b);
    return n;
}
uint8_t biz_kind_at(uint8_t lot) BANKED {
    uint8_t k;
    for (k = 0; k < NUM_LOTS; k++) if (G.biz_lot[k] == lot + 1) return NUM_BIZ + k;
    return 0xFF;
}

// ---------------------------------------------------------------- drawing
#define SF_KIT (MT_SF_MODERN_ROOF_0 - MT_SF_CLASSIC_ROOF_0)   // 16 metatiles a style, see art/52_town_invest.art
#if (MT_SF_COTTAGE_ROOF_0 - MT_SF_CLASSIC_ROOF_0) != 3 * SF_KIT || SF_KIT != 16
#error "storefront kits: four styles of 16 metatiles in the art file's order"
#endif
#define K_UP 3
#define K_UPBOX 5
#define K_GND 7
#define K_AWN 10
#define K_PATIO 13

static mt_t facade(uint8_t b, uint8_t dx, uint8_t dy) {   // cell (dx, dy) of business b's 3x3 storefront
    uint8_t look = G.biz_look[b], t = (look & BL_TIER) >> 2;
    mt_t base = MT_SF_CLASSIC_ROOF_0 + (look & BL_STYLE) * SF_KIT;
    if (look & BL_BUILDING) return ST_LOT_BUILD + dy * 3 + dx;
    if (!dy) return base + dx;
    if (dy == 1) return dx == 1 ? MT_SIGN_BAKERY + b : base + (t >= 2 ? K_UPBOX : K_UP) + (dx >> 1);
    return base + (t >= 3 ? K_PATIO : t ? K_AWN : K_GND) + dx;
}

static mt_t lot_cell(uint8_t lot, uint8_t dx, uint8_t dy) {
    uint8_t b = biz_kind_at(lot);
    if (b != 0xFF) return facade(b, dx, dy);
    if ((G.mk_flags & (1 << lot)) && dx == 1 && dy == 2) return MT_LOT_SOLD_SIGN;   // bought, nothing built yet
    return 0xFFFF;
}

mt_t biz_mt(uint8_t x, uint8_t y) BANKED {
    uint8_t b;
    if (y < MK_GATE_Y) {
        if (G.biz_owned & 0x0F)
            for (b = 0; b < NUM_BIZ; b++)
                if ((uint8_t)(x - spot_x[b]) < 3 && (uint8_t)(y - spot_y[b]) < 3) {
                    if (!OWNED(b)) break;
                    return facade(b, x - spot_x[b], y - spot_y[b]);
                }
    } else if ((uint8_t)(y - LOT_Y) < 3 && (G.mk_flags & 0x0F)) {
        for (b = 0; b < NUM_LOTS; b++)
            if ((uint8_t)(x - lot_x[b]) < 3) return lot_cell(b, x - lot_x[b], y - LOT_Y);
    }
    if ((y == MK_GATE_Y || y >= 28) && (G.town_proj || (G.mk_flags & MK_OPEN))) return invest_mt(x, y);   // gate, commons
    return 0xFFFF;
}

static void redraw_biz(uint8_t b) {
    if (b < NUM_BIZ) redraw_rect(spot_x[b], spot_y[b], 3, 3);
    else if (G.biz_lot[b - NUM_BIZ]) redraw_rect(lot_x[G.biz_lot[b - NUM_BIZ] - 1], LOT_Y, 3, 3);
}

// ---------------------------------------------------------------- overnight
static uint8_t best(uint8_t b, uint8_t role, uint8_t skip) {   // highest-value slot with stock
    uint8_t k, r = 0xFF, *p = pan(b);
    for (k = 0; k < PANTRY; k++) {
        if (k == skip || !p[k] || !(role_of(b, k) & role)) continue;
        if (r == 0xFF || items[item_of(b, k)].price > items[item_of(b, r)].price) r = k;
    }
    return r;
}

static uint32_t run_one(uint8_t b) {
    uint8_t units = units_of(b), lv = *lvp(b), k, nb = 0, m, m2, *p = pan(b);
    uint8_t bq = bq_base[b] + lv, pq = pq_base[b] + lv;
    uint32_t v = 0, sum;
    for (k = 0; k < PANTRY; k++) if (role_of(b, k) & R_B) nb++;
    while (units) {
        if (b == BIZ_RESTAURANT) {   // a dish with any fresh side
            if (units >= 2 && (m = best(b, R_M, 0xFF)) != 0xFF && (m2 = best(b, R_X, 0xFF)) != 0xFF) {
                sum = (uint32_t)items[item_of(b, m)].price + items[item_of(b, m2)].price;
                p[m]--; p[m2]--;
                v += sum * pq / 4;
                units -= 2;
                m_prem[b]++;
                continue;
            }
        } else if (nb) {   // premium: one of every base + the best main
            uint8_t ok = units > nb;
            for (k = 0; k < PANTRY; k++)
                if ((role_of(b, k) & R_B) && !p[k]) ok = 0;
            m = best(b, R_M, 0xFF);
            if (ok && m != 0xFF) {
                sum = items[item_of(b, m)].price;
                p[m]--;
                for (k = 0; k < PANTRY; k++)
                    if (role_of(b, k) & R_B) { p[k]--; sum += items[item_of(b, k)].price; }
                v += sum * pq / 4;
                units -= nb + 1;
                m_prem[b]++;
                continue;
            }
        } else if (units >= 2 && (m = best(b, R_M, 0xFF)) != 0xFF && (m2 = best(b, R_M, m)) != 0xFF) {
            sum = (uint32_t)items[item_of(b, m)].price + items[item_of(b, m2)].price;   // two different mains
            p[m]--; p[m2]--;
            v += sum * pq / 4;
            units -= 2;
            m_prem[b]++;
            continue;
        }
        m = best(b, R_S, 0xFF);
        if (m == 0xFF) break;
        p[m]--;
        v += (uint32_t)items[item_of(b, m)].price * bq / 4;
        units--;
        m_basic[b]++;
    }
    if (b == BIZ_RESTAURANT)   // emptied shelves are free for another dish
        for (k = 0; k < PANTRY; k++) if (!p[k]) G.rest_item[k] = 0;
    return v;
}

static uint8_t pantry_total(uint8_t b) {
    uint8_t k, n = 0, *p = pan(b);
    for (k = 0; k < PANTRY; k++) n += p[k] ? 1 : 0;
    return n;
}

void biz_overnight(void) BANKED {
    uint8_t b, P = prosperity(), inn = OWNED(BIZ_INN) && !BUILDING(BIZ_INN), bit, fund = fund_pct();
    uint32_t v;
    uint16_t mult;
    m_guests = m_quit = m_opened = m_empty = 0;
    m_wages = 0;
    for (b = 0; b < NUM_BIZ_ALL; b++) {
        bit = 1 << b;
        m_prem[b] = m_basic[b] = 0;
        *todayp(b) = 0;
        *soldp(b) = 0;
        if (!OWNED(b)) continue;
        if (BUILDING(b)) { G.biz_look[b] &= ~BL_BUILDING; m_opened |= bit; continue; }   // opens this morning
        if (staffed(b)) {   // the manager's wage first; no pay, no manager
            uint16_t w = MGR_WAGE(*lvp(b));
            if (G.gold >= w) { G.gold -= w; m_wages += w; }
            else { G.biz_staffed &= ~bit; m_quit |= bit; }
        }
        v = run_one(b);
        if (b == BIZ_INN) {   // rooms: more with a bigger inn, a finer facade and a busier town
            m_guests = 4 + 2 * *lvp(b) + tier(b) + P;
            v += (uint16_t)m_guests * INN_RATE;
        }
        if (!v && !pantry_total(b)) m_empty |= bit;
        mult = 100 + BIZ_PCT_TIER * tier(b) + BIZ_PCT_PROSP * P + (staffed(b) ? BIZ_PCT_MGR : 0) + (inn && b != BIZ_INN ? BIZ_PCT_INN : 0) + fund;
        v = v * mult / 100;
        if (v > 65535) v = 65535;
        *todayp(b) = (uint16_t)v;
        *soldp(b) = m_prem[b] + m_basic[b];
        *madep(b) = *madep(b) > 0xFF00 ? 0xFFFF : *madep(b) + *soldp(b);
        G.gold += v;
        G.earned += v;
    }
}

static void add_list(uint8_t bits, const char *const *names) {
    uint8_t b, n = 0;
    for (b = 0; b < NUM_BIZ_ALL; b++)
        if (bits & (1 << b)) { if (n++) s_add(", "); s_add(names[b]); }
}
#define add_names(bits) add_list(bits, biz_name)

void biz_report(void) BANKED {
    uint8_t b, n = 0, one = 0, top = 0xFF;
    uint32_t total = 0;
    for (b = 0; b < NUM_BIZ_ALL; b++) {
        if (!OWNED(b) || (m_opened & (1 << b))) continue;
        n++; one = b;
        total += *todayp(b);
        if (*todayp(b) && (top == 0xFF || *todayp(b) > *todayp(top))) top = b;
    }
    if (n == 1 && total) {   // one business: what it sold
        b = one;
        s_clr(); s_add(biz_sign[b]); s_add(" sold ");
        if (m_prem[b]) { s_num(m_prem[b], 0); s_ch(' '); s_add(prem_name[b]); }
        if (m_prem[b] && m_basic[b]) s_add(" and ");
        if (m_basic[b]) { s_num(m_basic[b], 0); s_ch(' '); s_add(basic_name[b]); }
        if (b == BIZ_INN) { if (m_prem[b] | m_basic[b]) s_add(", "); s_num(m_guests, 0); s_add(" guests"); }
        s_add(". It earned G"); s_num(*todayp(b), 0); s_ch('!');
        say(sbuf);
    } else if (total) {      // several: one line for the lot
        s_clr(); s_add("Your "); s_num(n, 0); s_add(" businesses earned G"); s_num(total, 0); s_add(" overnight! Best: ");
        s_add(biz_sign[top]); s_add(" G"); s_num(*todayp(top), 0); s_ch('.');
        say(sbuf);
    }
    if (m_empty) {
        s_clr(); s_add(m_empty & (m_empty - 1) ? "Empty pantries: " : "Empty pantry: ");
        add_names(m_empty); s_add(". Stock up in town!");
        say(sbuf);
    }
    if (m_wages) { s_clr(); s_add("Paid G"); s_num(m_wages, 0); s_add(" to your shop managers."); say(sbuf); }
    if (m_quit) { s_clr(); s_add("No gold for wages: the manager quit at your "); add_names(m_quit); s_ch('.'); say(sbuf); }
    if (m_opened) {
        s_clr(); s_add("Grand opening on Market St: "); add_list(m_opened, biz_sign); s_ch('!');
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

static uint8_t takes(uint8_t b, uint8_t it) {   // can business b shelve item it? returns the slot or 0xFF
    uint8_t k;
    for (k = 0; k < PANTRY; k++) if (item_of(b, k) == it) return k;
    if (b == BIZ_RESTAURANT && (IS_DISH(it) || it <= I_CHERRY))
        for (k = 0; k < PANTRY; k++) if (!G.rest_item[k]) return k;
    return 0xFF;
}

static void stock(uint8_t b) {
    uint8_t k, n, map[13], r, it, have, room, q, *p = pan(b);
    while (1) {
        n = 0;
        for (it = 0; it < NUM_ITEMS && n < 13; it++) {
            if (!inv_count(it) || (k = takes(b, it)) == 0xFF) continue;
            s_clr(); s_ch((char)(0x80 + it)); s_add(items[it].name); s_pad(10);
            s_ch('x'); s_num(inv_count(it) > 99 ? 99 : inv_count(it), 2);
            s_pad(15); s_num(item_of(b, k) == it ? p[k] : 0, 2);
            lbl_set(n); map[n++] = it;
        }
        if (!n) {
            say(b == 0 ? "You have nothing a bakery can use. Fruit, berries, pumpkins, eggs, milk!" :
                b == 1 ? "You have nothing to juice. Bring fruit or veggies!" :
                b == 2 ? "You have nothing for the creamery. Milk, eggs, honey or fruit!" :
                b == 3 ? "You have nothing for the florist. Flowers, harvest goods, herbs!" :
                b == 4 ? "Nothing to brew! Corn, pumpkins, berries, cherries, honey or a herb." :
                b == 5 ? "Nothing for the cellar! Fruit, berries, cider, or juice from your kegs." :
                b == 6 ? "The kitchen needs dishes you've cooked, or crops and fruit for the sides!" :
                         "Nothing for the breakfast table: eggs, milk, honey, berries, cheese, jam, juice, muffins.");
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
        it = map[r];
        k = takes(b, it);
        if (item_of(b, k) != it) { G.rest_item[k] = it + 1; p[k] = 0; }   // a free restaurant shelf
        have = inv_count(it) > 99 ? 99 : (uint8_t)inv_count(it);
        room = 99 - p[k];
        if (have > room) have = room;
        if (!have) { ui_dialog("That shelf is full!"); ui_end(); continue; }
        q = ui_quantity(have, items[it].price);
        ui_end();
        if (!q) { if (!p[k] && b == BIZ_RESTAURANT) G.rest_item[k] = 0; continue; }
        q = inv_take(it, q);
        p[k] += q;
    }
}

static void status(uint8_t b) {
    uint8_t k, y = 8, x, lv = *lvp(b), *p = pan(b), P = prosperity();
    uint16_t bonus = BIZ_PCT_TIER * tier(b) + BIZ_PCT_PROSP * P + (staffed(b) ? BIZ_PCT_MGR : 0) +
                     (OWNED(BIZ_INN) && !BUILDING(BIZ_INN) && b != BIZ_INN ? BIZ_PCT_INN : 0) + fund_pct();
    ui_begin();
    ui_box(0, 0, 20, 17);
    s_clr(); s_add(biz_sign[b]); s_add(" Lv"); s_num(lv + 1, 0);
    ui_text(1, 1, sbuf);
    s_clr(); s_add(style_name[G.biz_look[b] & BL_STYLE]); s_add(" * facade "); s_num(tier(b), 0);
    ui_text(1, 2, sbuf);
    s_clr(); s_add("Uses "); s_num(units_of(b), 0); s_add(" a night");
    if (staffed(b)) { s_add(" ("); s_add(mgr_name[b]); s_ch(')'); }
    ui_text(1, 3, sbuf);
    s_clr(); s_add("Last night G"); s_num(*todayp(b), 0);
    ui_text(1, 4, sbuf);
    s_clr(); s_add("Sold "); s_num(*soldp(b), 0); s_add(" (ever "); s_num(*madep(b), 0); s_ch(')');
    ui_text(1, 5, sbuf);
    ui_text(1, 7, b == BIZ_INN ? "Shelf (+rooms):" : "Shelf:");
    for (k = 0; k < PANTRY; k++) {
        uint8_t it = item_of(b, k), i;
        const char *nm;
        x = (k & 1) ? 11 : 1;
        s_clr();
        if (it == NONE) s_add("  --");
        else {
            nm = items[it].name;
            s_ch((char)(0x80 + it));
            for (i = 0; i < 5 && nm[i] && nm[i] != '.'; i++) s_ch(nm[i]);
            s_pad(6); s_num(p[k], 2);
        }
        ui_text(x, y + (k >> 1), sbuf);
    }
    {
        static const char *const frac[4] = {"", ".25", ".5", ".75"};
        uint8_t pq = pq_base[b] + lv, bq = bq_base[b] + lv;
        s_clr(); s_num(pq >> 2, 0); s_add(frac[pq & 3]); s_add("x "); s_add(prem_name[b]);
        ui_text(1, 13, sbuf);
        s_clr(); s_num(bq >> 2, 0); s_add(frac[bq & 3]); s_add("x "); s_add(basic_name[b]);
        ui_text(1, 14, sbuf);
        s_clr(); s_ch('+'); s_num(bonus, 0); s_add("% customers");
        ui_text(1, 15, sbuf);
    }
    wait_btn();
    ui_end();
}

static void kitchen(uint8_t b) {
    uint8_t lv = *lvp(b);
    if (lv >= 2) { say("This kitchen is as grand as it gets!"); return; }
    s_clr(); s_add(lv ? "Hire a master chef? Uses 20 a night, best recipes.\nG" :
                        "Bigger kitchen? Uses 12 a night, better recipes.\nG");
    s_num(up_price[lv], 0);
    if (ui_yesno(sbuf) && pay(up_price[lv], 0, 0)) {
        (*lvp(b))++;
        G.invested += up_price[lv];
        music_play(MUS_JINGLE_FANFARE);
        say("Done! It'll make more, and sell for more, starting tonight.");
        music_for_scene();
        hud_draw();
    }
}

// the storefront drawn into the menu window at (x, y), 6x6 tiles, with a given look
static void preview(uint8_t b, uint8_t look, uint8_t x, uint8_t y) {
    uint8_t keep = G.biz_look[b], dx, dy;
    ui_art_release();
    G.biz_look[b] = look & ~BL_BUILDING;
    for (dy = 0; dy < 3; dy++)
        for (dx = 0; dx < 3; dx++) ui_put_mt(x + dx * 2, y + dy * 2, facade(b, dx, dy));
    G.biz_look[b] = keep;
}

// pick one of the four styles, previewed as the cursor moves; 0xFF = cancelled
static uint8_t pick_style(uint8_t b, uint8_t cur, uint8_t free) {
    uint8_t k, r, hv = cur & BL_STYLE;
    for (k = 0; k < 4; k++) {
        s_clr(); s_add(style_name[k]); s_pad(10);
        if (!free && k == (cur & BL_STYLE)) s_add("now");
        else if (!free) s_num(STYLE_PRICE, 4);
        lbl_set(k);
    }
    ui_begin();
    ui_box(6, 7, 8, 8);
    ui_hover = &hv;
    while (1) {
        preview(b, (cur & ~BL_STYLE) | hv, 7, 8);
        r = ui_menu(0, 0, 20, lp, 4, hv);
        if (r != 0xFE) break;
    }
    ui_hover = 0;
    ui_end();
    return r;
}

static void facade_menu(uint8_t b) {
    static const char *const fm[3] = {"Style", "Upgrade", "Back"};
    uint8_t r, t, look;
    while (1) {
        look = G.biz_look[b];
        t = tier(b);
        ui_begin(); ui_money();
        ui_box(12, 5, 8, 8);
        preview(b, look, 13, 6);
        ui_box(0, 5, 12, 3);
        s_clr(); s_add("Facade "); s_num(t, 0); s_add("/3");
        ui_text(1, 6, sbuf);
        r = ui_menu(0, 0, 12, fm, 3, 0);
        ui_end();
        if (r == 0) {
            r = pick_style(b, look, 0);
            if (r == 0xFF || r == (look & BL_STYLE)) continue;
            s_clr(); s_add("Renovate "); s_add(biz_sign[b]); s_add(" in "); s_add(style_name[r]); s_add(" style for G");
            s_num(STYLE_PRICE, 0); s_ch('?');
            if (ui_yesno(sbuf) && pay(STYLE_PRICE, 0, 0)) {
                G.biz_look[b] = (look & ~BL_STYLE) | r;
                G.invested += STYLE_PRICE;
                redraw_biz(b);
                music_play(MUS_JINGLE_FANFARE);
                say("Andy's crew had it done by lunch. Looks wonderful!");
                music_for_scene();
                hud_draw();
            }
        } else if (r == 1) {
            if (t >= 3) { say("Awning, window boxes, a patio with lanterns... this storefront has it all!"); continue; }
            ui_begin();
            ui_box(6, 1, 8, 8);
            preview(b, look + BL_TIER1, 7, 2);
            s_clr(); s_add("Add "); s_add(tier_name[t]); s_add("? More customers every night.\nG"); s_num(tier_price[t], 0);
            r = ui_yesno(sbuf);
            ui_end();
            if (r && pay(tier_price[t], 0, 0)) {
                G.biz_look[b] = look + BL_TIER1;
                G.invested += tier_price[t];
                redraw_biz(b);
                music_play(MUS_JINGLE_FANFARE);
                say(t == 0 ? "A smart new awning! Passers-by stop to look in the window now." :
                    t == 1 ? "Window boxes, full of flowers. The whole street looks brighter!" :
                             "Tables out front and lanterns by the door. It's the place to be!");
                music_for_scene();
                hud_draw();
                goals_check();
            }
        } else return;
    }
}

static void staff_menu(uint8_t b) {
    uint16_t w = MGR_WAGE(*lvp(b));
    if (staffed(b)) {
        s_clr(); s_add(mgr_name[b]); s_add(" runs "); s_add(biz_sign[b]); s_add(" for G"); s_num(w, 0);
        s_add(" a night. Let them go?");
        if (ui_yesno(sbuf)) {
            G.biz_staffed &= ~(1 << b);
            s_clr(); s_add(mgr_name[b]); s_add(": It was a pleasure. Good luck, boss!");
            say(sbuf);
        }
        return;
    }
    s_clr(); s_add(mgr_name[b]); s_add(" can manage it: uses 4 more a night, +10% prices, for G"); s_num(w, 0);
    s_add(" a night. Hire?");
    if (ui_yesno(sbuf)) {
        G.biz_staffed |= 1 << b;
        sfx(SFX_COIN);
        s_clr(); s_add(mgr_name[b]); s_add(" starts tonight! Wages are paid nightly, before sales.");
        say(sbuf);
    }
}

static void buy(uint8_t b) {
    say("A boarded-up storefront. A sign on the door reads FOR SALE.");
    say(biz_pitch[b]);
    s_clr(); s_add("Open a "); s_add(biz_name[b]); s_add(" here for G"); s_num(biz_price[b], 0); s_ch('?');
    if (!ui_yesno(sbuf)) return;
    if (!pay(biz_price[b], 0, 0)) return;
    G.biz_owned |= 1 << b;
    G.invested += biz_price[b];
    sfx(SFX_COIN);
    music_play(MUS_JINGLE_BUY);
    redraw_biz(b);
    hud_draw();
    s_clr(); s_add("It's yours! Up goes the sign: "); s_add(biz_sign[b]); s_add(". Stock the pantry and it sells nightly.");
    say(sbuf);
    music_for_scene();
    goals_check();
}

// a bought, empty Market Street lot: choose what to build and its style
static const char *const lot_desc[NUM_LOTS][2] = {
    {"Ales from corn,", "pumpkins, berries."}, {"Wines from fruit,", "cider and juice."},
    {"Serves the dishes", "you cook."}, {"Rooms, breakfasts", "+10% town sales."}};
void biz_build(uint8_t lot) BANKED {
    uint8_t n = 0, k, map[NUM_LOTS], r, st, hv = 0;
    for (k = 0; k < NUM_LOTS; k++) {
        if (G.biz_lot[k]) continue;
        s_clr(); s_add(biz_name[NUM_BIZ + k]); s_pad(11); s_num(biz_price[NUM_BIZ + k], 5);
        lbl_set(n); map[n++] = NUM_BIZ + k;
    }
    if (!n) { say("You've built every kind of business there is. What a street!"); return; }
    say("Your lot on Market Street. What shall we build here?");
    ui_begin(); ui_money();
    ui_hover = &hv;
    while (1) {
        k = map[hv < n ? hv : 0] - NUM_BIZ;
        ui_panel(0, 11, 20, 6);
        ui_text(1, 12, lot_desc[k][0]);
        ui_text(1, 14, lot_desc[k][1]);
        r = ui_menu(0, 0, 20, lp, n, hv);
        if (r != 0xFE) break;
    }
    ui_hover = 0;
    ui_end();
    if (r == 0xFF) return;
    k = map[r];
    st = pick_style(k, G.style & 3, 1);
    if (st == 0xFF) return;
    s_clr(); s_add("Build a "); s_add(style_name[st]); s_ch(' '); s_add(biz_name[k]); s_add(" here for G");
    s_num(biz_price[k], 0); s_ch('?');
    if (!ui_yesno(sbuf) || !pay(biz_price[k], 0, 0)) return;
    G.biz_owned |= 1 << k;
    G.biz_lot[k - NUM_BIZ] = lot + 1;
    G.biz_look[k] = st | BL_BUILDING;
    G.invested += biz_price[k];
    sfx(SFX_COIN);
    music_play(MUS_JINGLE_BUY);
    redraw_biz(k);
    hud_draw();
    s_clr(); s_add("The builders start tonight. "); s_add(biz_sign[k]); s_add(" opens tomorrow morning!");
    say(sbuf);
    music_for_scene();
    goals_check();
}

void biz_act(uint8_t b) BANKED {
    static const char *const bm[6] = {"Stock", "Status", "Kitchen", "Facade", "Staff", "Bye"};
    uint8_t r;
    if (b >= NUM_BIZ_ALL) return;
    if (!OWNED(b)) { if (b < NUM_BIZ) buy(b); return; }
    if (BUILDING(b)) { say("Builders are hard at work behind the scaffolding. It opens tomorrow morning!"); return; }
    while (1) {
        ui_begin(); ui_money();
        s_clr(); s_add(biz_sign[b]);
        ui_text(1, 14, sbuf);
        r = ui_menu(0, 0, 11, bm, 6, 0);
        ui_end();
        if (r == 0) stock(b);
        else if (r == 1) status(b);
        else if (r == 2) kitchen(b);
        else if (r == 3) facade_menu(b);
        else if (r == 4) staff_menu(b);
        else return;
    }
}
