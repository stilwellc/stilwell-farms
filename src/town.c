// Stilwell Farms -- town: shops, land office, townsfolk (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"

BANKREF(town_act)


static const char *const npc_names[NUM_NPC] = {"Alyssa", "Brick", "Andy", "Raza", "Joe", "Marcy"};

static void refresh(void) { ui_end(); ui_begin(); ui_money(); }

// ---------------------------------------------------------------- selling (any shop)
static uint8_t sell_filter;   // 0 all, 1 ore/gems only
static void sell_menu(void) {
    uint8_t it, q, n, r, k, qty;
    uint8_t mi[13], mq[13];
    while (1) {
        n = 0;
        for (it = 0; it < NUM_ITEMS && n < 10; it++) {
            if (sell_filter && !IS_MINERAL(it)) continue;
            for (q = 0; q < 3 && n < 10; q++) {
                if (!G.inv[it][q]) continue;
                s_clr(); s_ch((char)(0x80 + it)); s_add(items[it].name);
                if (q) s_ch(q == 2 ? (char)0xF1 : (char)0xF0);
                {   // 4-digit prices (gems) start a column early, so the row still fits the 17 columns
                    uint8_t wide = sell_value(it, q) >= 1000;
                    s_pad(wide ? 9 : 10); s_ch('x'); s_num(G.inv[it][q], 2); s_ch(' '); s_pad(wide ? 13 : 14); s_num(sell_value(it, q), 3);
                }
                lbl_set(n); mi[n] = it; mq[n] = q; n++;
            }
        }
        refresh();
        if (!n) { ui_dialog("You've got nothing I can buy right now."); return; }
        {
            uint8_t hv = 0;
            ui_hover = &hv;
            while (1) {
                item_panel(mi[hv], mq[hv]);
                r = ui_menu(0, 0, 20, lp, n, hv);
                if (r != 0xFE) break;
            }
            ui_hover = 0;
        }
        if (r == 0xFF) return;
        k = r; it = mi[k]; q = mq[k];
        qty = ui_quantity(G.inv[it][q] > 99 ? 99 : G.inv[it][q], sell_value(it, q));
        if (!qty) continue;
        G.inv[it][q] -= qty;
        G.gold += (uint32_t)qty * sell_value(it, q);
        G.earned += (uint32_t)qty * sell_value(it, q);
        mark_shipped(it);
        hud_draw();
        goals_check();
    }
}

// ---------------------------------------------------------------- general store (Alyssa)
// rarity: Rare packets cost 3x and yield 2; Legendary cost 8x, yield 3-4 at gold odds, and (like golden
// animals) Alyssa only stocks them for proven farmers: Year 2, or LEGEND_EARNED earned (docs/BALANCE.md).
static const uint8_t rar_mult[NUM_RARITY] = {1, 3, 8};
#define LEGEND_EARNED 250000UL
static uint8_t legend_ok(void) { return G.year > 1 || G.earned >= LEGEND_EARNED; }
static void legend_locked(void) {
    ui_dialog("The legendary stock is for proven farmers. Earn G250000 or come back next year, and it's yours!");
}
static const char *const tier_items[NUM_RARITY] = {"Common", "Rare  x3", "Legend x8"};

static void buy_seeds(void) {
    uint8_t k, n = 0, map[NUM_CROPS], r, q, max, green = 0, rar;
    for (k = 0; k < MAX_BLD; k++)   // only a repaired greenhouse grows out-of-season seeds
        if (G.bld[k].type == B_GREENHOUSE && !(G.bld[k].flags & BF_RUIN)) green = 1;
    uint16_t cost;
    refresh();
    rar = ui_menu(0, 0, 12, tier_items, NUM_RARITY, 0);   // "Legend x8" + cursor + borders
    if (rar == 0xFF) return;
    if (rar == R_LEGEND && !legend_ok()) { legend_locked(); return; }
    for (k = 1; k <= NUM_CROPS && n < 10; k++) {
        if (crops[k].season != G.season && !green) continue;
        s_clr(); s_ch((char)(0x80 + k - 1)); s_add(items[k - 1].name);
        if (rar) s_ch(rar == R_RARE ? '+' : (char)0xF1); else s_add(" seed");
        s_pad(14); s_num(crops[k].seed_cost * rar_mult[rar], 3);
        lbl_set(n); map[n++] = k;
    }
    if (!n) { ui_dialog("No seeds this season, sorry!"); return; }
    while (1) {
        uint32_t afford;
        uint8_t hv = 0;
        refresh();
        ui_hover = &hv;
        while (1) {
            crop_panel_r(map[hv], rar);
            r = ui_menu(0, 0, 20, lp, n, hv);
            if (r != 0xFE) break;
        }
        ui_hover = 0;
        if (r == 0xFF) return;
        k = map[r];
        cost = crops[k].seed_cost * rar_mult[rar];
        afford = G.gold / cost;
        max = afford > 99 ? 99 : (uint8_t)afford;
        if (!max) { ui_dialog("You can't afford that right now."); continue; }
        q = ui_quantity(max, cost);
        if (!q) continue;
        G.gold -= (uint32_t)q * cost;
        G.seeds[k][rar] = (uint16_t)G.seeds[k][rar] + q > 255 ? 255 : G.seeds[k][rar] + q;
        hud_draw();
    }
}

static uint8_t animal_count(uint8_t kind, uint8_t *home) {
    uint8_t k, b, cap = kind == A_HEN ? 6 : BARN_CAP, n;   // cows and sheep share the barn
    for (b = 0; b < MAX_BLD; b++) {
        if (G.bld[b].type != (kind == A_HEN ? B_COOP : B_BARN) || (G.bld[b].flags & BF_RUIN)) continue;
        n = 0;
        for (k = 0; k < MAX_ANIMALS; k++) if (G.anim[k].kind != 0xFF && G.anim[k].home == b) n++;
        if (n < cap) { *home = b; return 1; }
    }
    return 0;
}

static const uint16_t breed_price[3][NUM_RARITY] = {{1500, 4500, 6500}, {400, 1200, 1700}, {1800, 5400, 7800}};
static const char *const breed_names[3][NUM_RARITY] = {{"Holstein", "Jersey", "Golden"}, {"White", "Brown", "Golden"}, {"Merino", "Cocoa", "Golden"}};
static const char *const breed_pet[3][NUM_RARITY] = {{"Daisy", "Mocha", "Goldy"}, {"Penny", "Hazel", "Sunny"}, {"Wooly", "Cocoa", "Midas"}};
static const char *const breed_tip[3][2] = {
    {"Jersey cows give two milk a day!", "A golden cow! Two gold-star milk a day, always top quality."},
    {"Brown hens lay two eggs a day!", "A golden hen! Two golden eggs a day, always top quality."},
    {"Cocoa sheep grow a double fleece!", "Golden fleece! Two gold-star wool every shearing."}};
static const char *const kind_word[3] = {"cow", "hen", "sheep"};

static void buy_animal(uint8_t kind) {
    uint8_t home, k, br, ki = kind;
    uint16_t price;
    for (k = 0; k < NUM_RARITY; k++) {
        s_clr(); s_add(breed_names[ki][k]); s_pad(10); s_num(breed_price[ki][k], 5);
        lbl_set(k);
    }
    refresh();
    br = ui_menu(0, 0, 20, lp, NUM_RARITY, 0);
    if (br == 0xFF) return;
    if (br == R_LEGEND && !legend_ok()) { legend_locked(); return; }
    if (br) ui_dialog(breed_tip[ki][br - 1]);
    price = breed_price[ki][br];
    if (!animal_count(kind, &home)) {
        ui_dialog(kind != A_HEN ? "You need a barn with room first. Fix up the old one or build a new barn!" :
                                  "You need a coop with room first. Fix up the old one or build a new coop!");
        return;
    }
    if (G.gold < price) { ui_dialog("You can't afford that yet."); return; }
    for (k = 0; k < MAX_ANIMALS; k++) if (G.anim[k].kind == 0xFF) break;
    if (k == MAX_ANIMALS) { ui_dialog("That's as many animals as you can care for!"); return; }
    G.gold -= price;
    G.anim[k].trait = (rnd() & TR_TEMPER); G.anim[k].gen = 0;
    G.anim[k].kind = kind; G.anim[k].home = home; G.anim[k].happy = 100; G.anim[k].age = ADULT_AGE; G.anim[k].flags = br << 6;
    strcpy(G.anim[k].name, breed_pet[ki][br]);
    herd_mark(k);
    ui_end();
    s_clr(); s_add("Name your "); s_add(kind_word[ki]);   // prompt via sbuf: ui_name lives in another bank
    ui_name(sbuf, G.anim[k].name, 5);
    ui_begin();
    s_clr(); s_add(G.anim[k].name); s_add(" will be waiting at your "); s_add(kind != A_HEN ? "barn" : "coop"); s_add(". Feed them hay every day!");
    ui_dialog(sbuf);
    hud_draw();
    goals_check();
}

static const char *const store_items[8] = {"Seeds", "Fertilizer", "Hay", "Cows", "Hens", "Sheep", "Sell", "Bye"};
// with a stable and no horse yet, Alyssa can find you one (horse.c)
static const char *const store_items_h[9] = {"Seeds", "Fertilizer", "Hay", "Cows", "Hens", "Sheep", "Horse", "Sell", "Bye"};
// a prosperous town (invest.c, level 2+) brings traders: Alyssa adds their goods before Sell
static const char *const store_items_g[9] = {"Seeds", "Fertilizer", "Hay", "Cows", "Hens", "Sheep", "Goods", "Sell", "Bye"};
static const char *const store_items_hg[10] = {"Seeds", "Fertilizer", "Hay", "Cows", "Hens", "Sheep", "Horse", "Goods", "Sell", "Bye"};
static void store(void) {
    uint8_t r, q;
    set_speaker("Alyssa");
    say("Welcome to Seeds & Needs! If it grows, I sell it. If you grew it, I'll buy it.");
    while (1) {
        uint8_t hs = !G.horse && bld_find(B_STABLE) != 0xFF, gd = prosperity() >= 2;
        ui_begin(); ui_money();
        r = ui_menu(0, 0, 13, gd ? (hs ? store_items_hg : store_items_g) : (hs ? store_items_h : store_items), 8 + hs + gd, 0);
        if (hs && r == 6) { horse_shop(); ui_end(); continue; }
        if (hs && r > 6 && r != 0xFF) r--;
        if (gd && r == 6) { invest_store(); ui_end(); continue; }
        if (gd && r > 6 && r != 0xFF) r--;
        switch (r) {
        case 0: buy_seeds(); break;
        case 1:
            q = ui_quantity(G.gold / 50 > 99 ? 99 : (uint8_t)(G.gold / 50), 50);
            if (q) { G.gold -= (uint32_t)q * 50; G.fert += q; hud_draw(); refresh(); ui_dialog("Fertilizer makes for better quality crops. Use it on tilled soil."); }
            break;
        case 2:
            q = ui_quantity(G.gold / 20 > 99 ? 99 : (uint8_t)(G.gold / 20), 20);
            if (q) { G.gold -= (uint32_t)q * 20; G.hay += q; hud_draw(); }
            break;
        case 3: buy_animal(A_COW); break;
        case 4: buy_animal(A_HEN); break;
        case 5: buy_animal(A_SHEEP); break;
        case 6: sell_filter = 0; sell_menu(); break;
        default: ui_dialog("Go on, get planting! Those seeds won't bury themselves."); ui_end(); ui_speaker = 0; return;
        }
        ui_end();
    }
}

// ---------------------------------------------------------------- blacksmith (Brick)
static const char *const tt_names[NUM_TT] = {"Hoe", "Water Can", "Axe", "Hammer", "Fishing Rod"};   // fit the price list
static const char *const tier_word[4] = {"Basic", "Copper", "Iron", "Gold"};
static const uint16_t tier_gold[3] = {3000, 10000, 25000};

static void smith(void) {
    static const char *const sm[3] = {"Upgrade", "Sell Ore", "Bye"};   // ore, coal and gems
    uint8_t r, k, t, up = 0xFF;
    set_speaker("Brick");
    say("The Grumpy Anvil. I hit things till they're better. Got ore? Got coin? Good.");
    while (1) {
        ui_begin(); ui_money();
        r = ui_menu(0, 0, 11, sm, 3, 0);
        if (r == 0) {
            for (k = 0; k < NUM_TT; k++) {
                t = G.tier[k];
                s_clr(); s_add(tt_names[k]); s_pad(12);   // prices right-aligned in the 17 columns, like every shop list
                if (t >= 3) s_add("  MAX");
                else s_num(tier_gold[t], 5);
                lbl_set(k);
            }
            refresh();
            r = ui_menu(0, 0, 20, lp, NUM_TT, 0);
            if (r != 0xFF) {
                refresh();   // clear the list: the speaker's name tab would sit on its bottom row
                t = G.tier[r];
                if (t >= 3) ui_dialog("That one's as good as it gets!");
                else {
                    uint8_t ore = I_COPPER + t;
                    s_clr(); s_add("Make it "); s_add(tier_word[t + 1]); s_add("?\nG"); s_num(tier_gold[t], 0); s_add(" + 5 "); s_add(items[ore].name);
                    if (ui_yesno(sbuf)) {
                        refresh();   // drop the YES/NO box before Brick answers
                        if (G.gold < tier_gold[t] || inv_count(ore) < 5) ui_dialog("You'll need more than that. Ore comes from the quarry, and the mine up the west road.");
                        else {
                            G.gold -= tier_gold[t]; inv_take(ore, 5); G.tier[r]++;
                            music_play(MUS_JINGLE_FANFARE);
                            up = r;
                            ui_dialog(r <= 1 ? "Done! It covers more ground now." : r == 4 ? "Done! Rarer fish will bite now." : "Done! It'll break tougher stuff now.");
                            music_for_scene();
                            hud_draw();
                        }
                    }
                }
            }
        } else if (r == 1) { sell_filter = 1; sell_menu(); }
        else {
            ui_dialog("Stay sharp."); ui_end(); ui_speaker = 0;
            if (up != 0xFF) {   // level-up toast once the shop closes
                s_clr(); s_ch((char)0xF0); s_ch(' '); s_add(tier_word[G.tier[up] & 3]); s_ch(' ');
                s_add(up == TT_CAN ? "Can" : up == TT_ROD ? "Rod" : tt_names[up]); s_add("! "); s_ch((char)0xF0);
                hud_flash(sbuf);
            }
            return;
        }
        ui_end();
    }
}

// ---------------------------------------------------------------- carpenter (Andy)
static const char *const style_names_[4] = {"Classic", "Modern", "Log Cabin", "Cottage"};
static const uint16_t style_gold[4] = {1500, 4000, 2500, 3000};

static void carpenter(void) {
    static const char *const cm[5] = {"House", "Styles", "Materials", "Advice", "Bye"};
    uint8_t r, h = bld_find(B_HOUSE), lv, k, q;
    const char *toast = 0;
    set_speaker("Andy");
    say("Knock on Wood! Measure twice, cut once, blame the wood. What are we building?");
    while (1) {
        ui_begin(); ui_money();
        r = ui_menu(0, 0, 12, cm, 5, 0);
        refresh();   // the prompts and replies below get a clean screen (the name tab sat on the menu)
        lv = (G.bld[h].flags & BF_LVL) >> 1;
        if (r == 0) {
            if (lv >= 2) ui_dialog("Your house is as big as it gets. It's a beauty!");
            else {
                uint32_t g = lv ? 8000 : 2500; uint16_t w = lv ? 120 : 60, s = lv ? 80 : 0;
                s_clr(); s_add(lv ? "Grow into a big two-storey home?\n" : "Rebuild the shack into a real house?\n"); cost_str(g, w, s);
                if (ui_yesno(sbuf) && (refresh(), pay(g, w, s))) {   // refresh: no YES/NO box left behind the reply
                    G.bld[h].flags = (G.bld[h].flags & ~BF_LVL) | ((lv + 1) << 1);
                    G.energy_max += 30;
                    music_play(MUS_JINGLE_FANFARE);
                    toast = lv ? "\xF0 Big House! \xF0" : "\xF0 New House! \xF0";
                    ui_dialog("It'll be done by the time you get home. More room means more energy every day!");
                    music_for_scene();
                    goals_check();
                }
            }
        } else if (r == 1) {
            if (!lv) { ui_dialog("Let's rebuild that old shack first. Then we can talk style!"); }
            else {
                for (k = 0; k < 4; k++) {
                    s_clr(); s_add(style_names_[k]); s_pad(12);
                    if (k == G.style) s_add("yours"); else { s_ch('G'); s_num(style_gold[k], 0); }
                    lbl_set(k);
                }
                refresh();
                r = ui_menu(0, 0, 20, lp, 4, G.style);
                if (r != 0xFF && r != G.style) {
                    static const char *const desc[4] = {
                        "Red roof, white siding, flower boxes, and a classic red barn.",
                        "White walls, black metal roof, big glass windows, and a sleek black barn.",
                        "Stacked logs, a green roof, a stone chimney, and a log barn.",
                        "A thatched storybook cottage with roses, and a stone barn."};
                    ui_dialog(desc[r]);
                    s_clr(); s_add("Renovate in "); s_add(style_names_[r]); s_add(" style for G"); s_num(style_gold[r], 0); s_ch('?');
                    if (ui_yesno(sbuf) && (refresh(), pay(style_gold[r], 0, 0))) {
                        G.style = r;
                        music_play(MUS_JINGLE_FANFARE);
                        toast = "\xF0 Fresh new look \xF0";
                        ui_dialog("Done! Your house and barns have a whole new look.");
                        music_for_scene();
                    }
                }
            }
        } else if (r == 2) {
            static const char *const mm[2] = {"Wood    10", "Stone   15"};
            refresh();
            r = ui_menu(0, 0, 13, mm, 2, 0);   // clear of the money box at x 13
            if (r != 0xFF) {
                uint16_t price = r ? 15 : 10;
                q = ui_quantity(G.gold / price > 99 ? 99 : (uint8_t)(G.gold / price), price);
                if (q) { G.gold -= (uint32_t)q * price; if (r) G.stone += q; else G.wood += q; hud_draw(); }
            }
        } else if (r == 3) {
            ui_dialog("Farm buildings, fences, paths, ponds, decor... you build all of it yourself from your START menu's BUILD page. Anywhere on land you own!");
            ui_dialog("And if a spot doesn't feel right, use MOVE to pick a building up and put it somewhere new.");
        } else {
            ui_dialog("Happy building! Mind your thumbs."); ui_end(); ui_speaker = 0;
            if (toast) hud_flash(toast);   // level-up toast once the shop closes
            return;
        }
        ui_end();
    }
}

// ---------------------------------------------------------------- cafe (Raza)
static void cafe(void) {
    static const char *const fm[4] = {"Coffee   150", "Lunch    350", "Feast    800", "Bye"};
    static const uint16_t price[3] = {150, 350, 800};
    static const uint8_t energy[3] = {20, 50, 255};
    uint8_t r;
    set_speaker("Raza");
    say("Welcome to Bean There! Sit, sit. You look like you've been pulling weeds all day.");
    while (1) {
        ui_begin(); ui_money();
        r = ui_menu(0, 4, 15, fm, 4, 0);   // 12-column labels + cursor + borders
        refresh();   // Raza's name tab would cover "Bye" during the answer
        if (r < 3) {
            if (G.gold < price[r]) ui_dialog("Maybe next time, hon.");
            else if (G.ate >= G.energy_max) ui_dialog("You've eaten plenty today, hon. Get some sleep!");   // one bar a day
            else {
                uint16_t e = (uint16_t)G.energy + energy[r];
                G.gold -= price[r];
                if (e > G.energy_max) e = G.energy_max;
                G.ate = (uint16_t)G.ate + (e - G.energy) > 255 ? 255 : G.ate + (uint8_t)(e - G.energy);
                G.energy = (uint8_t)e;
                sfx(SFX_COIN);
                hud_draw();
                ui_dialog(r == 0 ? "Fresh brewed. You feel more awake!" : r == 1 ? "A hearty lunch. Energy restored!" : "A full feast! You feel brand new!");
            }
        } else { ui_dialog("Come back hungry!"); ui_end(); ui_speaker = 0; return; }
        ui_end();
    }
}

// ---------------------------------------------------------------- land office
// Up to 10 plots for sale, those bordering your land first. Each row shows the plot's full name and, for a
// region spanning several plots, which part of it this is ("Granite Ridge NW" / "NE" / "SW"); the panel
// underneath gives the price and where it lies from your home field.
#define LAND_ROWS 10
static int8_t plot_dx(uint8_t p) { uint8_t x = p & 7; return x < 3 ? -(int8_t)(3 - x) : x > 4 ? (int8_t)(x - 4) : 0; }
static int8_t plot_dy(uint8_t p) { uint8_t y = p >> 3; return y < 3 ? -(int8_t)(3 - y) : y > 4 ? (int8_t)(y - 4) : 0; }

// a region spanning several plots: N/S, W/E (or both) for this plot's part of it
static void region_part(uint8_t p) {
    uint8_t q, r = plot_region(p), x0 = 7, x1 = 0, y0 = 7, y1 = 0, x = p & 7, y = p >> 3;
    for (q = 0; q < NUM_PLOTS; q++) {
        if (plot_region(q) != r) continue;
        if ((q & 7) < x0) x0 = q & 7;
        if ((q & 7) > x1) x1 = q & 7;
        if ((q >> 3) < y0) y0 = q >> 3;
        if ((q >> 3) > y1) y1 = q >> 3;
    }
    if (y1 > y0 && y == y0) s_ch('N');
    if (y1 > y0 && y == y1) s_ch('S');
    if (x1 > x0 && x == x0) s_ch('W');
    if (x1 > x0 && x == x1) s_ch('E');
}

static void land_panel(uint8_t p) {
    int8_t dx = plot_dx(p), dy = plot_dy(p);
    ui_panel(0, 12, 20, 5);
    s_clr(); s_add("Price G"); s_num(plot_price(p), 0);
    ui_text(1, 13, sbuf);
    s_clr();
    if (dx) { s_num(dx < 0 ? -dx : dx, 0); s_add(dx < 0 ? " west" : " east"); }
    if (dx && dy) s_add(", ");
    if (dy) { s_num(dy < 0 ? -dy : dy, 0); s_add(dy < 0 ? " north" : " south"); }
    s_add(" of home");
    ui_text(1, 15, sbuf);
}

static void realtor(void) {
    uint8_t p, n = 0, map[LAND_ROWS], r, pass, hv = 0;
    if (invest_realtor()) return;   // invest.c: Market Street (once the town has some prosperity)
    set_speaker("Joe");
    for (pass = 0; pass < 2; pass++)
        for (p = 0; p < NUM_PLOTS && n < LAND_ROWS; p++) {
            uint8_t x = (p & 7) << 3, y = (p >> 3) << 3, near;
            if (PLOT_OWNED(p)) continue;
            near = (x && owned(x - 8, y)) || (x < WW - 8 && owned(x + 8, y)) ||
                   (y && owned(x, y - 8)) || (y < WH - 8 && owned(x, y + 8));
            if (near != !pass) continue;
            s_clr(); plot_name_add(p); s_pad(15);   // full name (up to 15), then which part of the region
            region_part(p);
            lbl_set(n); map[n++] = p;
        }
    if (!n) { say("You own every acre around! What a farm."); ui_speaker = 0; return; }
    say("Welcome to Lots & Plots! Woods, a lake, a river, a quarry... dirt cheap. Pick a plot.");
    ui_begin();   // (the HUD shows your gold; the list needs the full width)
    ui_hover = &hv;
    while (1) {
        land_panel(map[hv < n ? hv : 0]);
        r = ui_menu(0, 0, 20, lp, n, hv);
        if (r != 0xFE) break;
    }
    ui_hover = 0;
    ui_end();
    if (r != 0xFF) buy_plot_prompt(map[r]);
    ui_speaker = 0;
}

// ---------------------------------------------------------------- townsfolk
static const char *const lines[NUM_NPC][3] = {
    {"Rain or shine, the seeds keep coming in. What are you planting this season?",
     "Your farm's the talk of the town! People say you've got a green thumb.",
     "You know, your grandpa used to buy his seeds right here. He'd be so proud."},
    {"Hmph. Ore from the quarry or the mine makes the best tools. Bring me some.",
     "That hammer of yours could use some love. Come by the Anvil.",
     "You're alright, farmer. Most folks don't work this hard."},
    {"Every good farm starts with a good fence. And a good plan!",
     "I saw your place from the road. Love what you're doing with it!",
     "If you ever want to renovate, I've got blueprints for days."},
    {"Busy day? Coffee's always on.",
     "My pies use your berries now, you know. Best in the valley!",
     "Sit for a minute. The farm will still be there."},
    {"Ah, the new farmer! Welcome, welcome. The town's glad you came.",
     "Festivals are held on the 14th of each season, and Star Night on Winter 25!",
     "Your grandpa and I were great friends. You remind me of him."},
    {"I caught a fish THIS big at the lake! Well... almost.",
     "Do you have a dog? I love dogs! Can I meet yours?",
     "When I grow up I'm gonna have a farm just like yours!"}};

static uint8_t loves(uint8_t npc, uint8_t it) {
    if (IS_DISH(it)) return 1;   // everyone loves a home-cooked dish
    switch (npc) {
    case NPC_ROSA: return it == I_SUNFLOWER || it == I_HERB || it == I_STRAWB || it == I_CORAL;
    case NPC_BRICK: return IS_MINERAL(it);
    case NPC_JUNE: return it == I_CIDER || it == I_APPLE || it == I_CORN || it == I_DRIFTWOOD;
    case NPC_ADA: return it == I_MILK || it == I_EGG || it == I_CHEESE || it == I_BERRIES;
    case NPC_HOLLIS: return it == I_PUMPKIN || it == I_HONEY || it == I_JAM;
    default: return IS_FISH(it) || it == I_SHELL || it == I_SEAGLASS;
    }
}


// town life: on their birthday they say so, and a gift counts double
static const char *const bday_lines[NUM_NPC] = {
    "It's my birthday today, dear! Don't tell anyone how old. I've stopped counting at 39.",
    "...Who told you it's my birthday? Hmph. Thanks.",
    "It's my BIRTHDAY! I'm building myself a cake. Out of wood. It's a metaphor!",
    "Birthday today, hon. Marcy made me breakfast. Mostly eggshell. Best one ever.",
    "Another year older today! Your grandpa always sent a pumpkin on my birthday.",
    "It's my BIRTHDAY! I'm EIGHT! Almost nine! Almost ten! Then I get a DOG!"};

void npc_talk(uint8_t npc) BANKED {
    uint8_t h = G.friend[npc] / 50, it, bday = tl_birthday(npc), first = 0;
    set_speaker(npc_names[npc]);
    if (!(G.talked & (1 << npc))) {
        first = 1;
        G.talked |= 1 << npc;
        if (G.friend[npc] < 253) G.friend[npc] += 2;
    }
    if (scene == MAP_TOWN && tl_heart_try(npc, 0)) { ui_speaker = 0; return; }   // a heart event is due: it plays instead
    set_speaker(npc_names[npc]);
    if (bday) say(bday_lines[npc]);
    else if (scene == MAP_BEACH) say("Weekends are for the beach! Someday I'm gonna catch a tuna off the end of the pier.");
    else if (!invest_line(npc)) say(lines[npc][h >= 3 ? 2 : h >= 1 ? 1 : (rnd() & 1) ? 0 : 1]);   // invest.c: a busy town
    cook_friend(npc);   // cooking.c: at 2 hearts they share a recipe
    if (wish_talk(npc, first)) { ui_speaker = 0; return; }   // friends2.c: this year's big wish
    set_speaker(npc_names[npc]);
    if (!(G.gifted & (1 << npc)) && ui_yesno(bday ? "Give a birthday gift?" : "Give a gift?")) {
        it = ui_pick_item(PICK_ANY);
        if (it != 0xFF) {
            uint8_t l = loves(npc, it), add = (l ? 12 : 4) << bday;
            inv_take(it, 1);
            G.gifted |= 1 << npc;
            G.friend[npc] = (uint16_t)G.friend[npc] + add > 250 ? 250 : G.friend[npc] + add;
            say(l ? "Oh! I LOVE this! Thank you so much!" : "Thank you, that's very kind.");
            goals_check();
        }
    }
    ui_speaker = 0;
}

void town_act(uint8_t act) BANKED {
    if (!tl_shop_open(act)) { tl_closed_sign(act); return; }   // town life: counters keep their owners' hours
    switch (act) {
    case ACT_HOUSE: tl_house(); break;
    case ACT_STORE: store(); break;
    case ACT_SMITH: smith(); break;
    case ACT_CARPENTER: carpenter(); break;
    case ACT_CAFE: cafe(); break;
    case ACT_REALTOR: realtor(); break;
    case ACT_PUB: pub_act(); break;
    case ACT_HALL: staff_hall(); break;
    case ACT_BOARD: board_act(); break;
    case ACT_BAKERY: biz_act(0); break;
    case ACT_JUICEBAR: biz_act(1); break;
    case ACT_CREAMERY: biz_act(2); break;
    case ACT_FLORIST: biz_act(3); break;
    case ACT_GATE: case ACT_LOT0: case ACT_LOT1: case ACT_LOT2: case ACT_LOT3:   // Market Street (invest.c)
    case ACT_PLAYGROUND: case ACT_GARDEN: case ACT_BANDSTAND: case ACT_LIBRARY: invest_act(act); break;
    case ACT_FOUNTAIN:
        if (G.gold >= 10 && ui_yesno("Toss a coin in the fountain and make a wish? (G10)")) {
            G.gold -= 10;
            hud_draw();
            if (G.season != SEASON_WINTER && rnd() < 120) { G.tomorrow = 0; say("The water sparkles... Tomorrow will be a sunny day!"); }
            else say("Plink! You make a wish.");
        }
        break;
    default: say("Nobody's home right now."); break;
    }
}
