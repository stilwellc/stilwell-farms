// Stilwell Farms -- town: shops, land office, townsfolk (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"

BANKREF(town_act)


static const char *const npc_names[NUM_NPC] = {"Rosa", "Brick", "June", "Ada", "Hollis", "Milo"};

static void refresh(void) { ui_end(); ui_begin(); ui_money(); }

// ---------------------------------------------------------------- selling (any shop)
static uint8_t sell_filter;   // 0 all, 1 ore/gems only
static void sell_menu(void) {
    uint8_t it, q, n, r, k, qty;
    uint8_t mi[13], mq[13];
    while (1) {
        n = 0;
        for (it = 0; it < NUM_ITEMS && n < 10; it++) {
            if (sell_filter && !(it >= I_COPPER && it <= I_GEM)) continue;
            for (q = 0; q < 3 && n < 10; q++) {
                if (!G.inv[it][q]) continue;
                s_clr(); s_ch((char)(0x80 + it)); s_add(items[it].name);
                if (q) s_ch(q == 2 ? (char)0xF1 : (char)0xF0);
                s_pad(11); s_ch('x'); s_num(G.inv[it][q], 2); s_pad(14); s_num(item_value(it, q), 3);
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
        qty = ui_quantity(G.inv[it][q] > 99 ? 99 : G.inv[it][q], item_value(it, q));
        if (!qty) continue;
        G.inv[it][q] -= qty;
        G.gold += (uint32_t)qty * item_value(it, q);
        G.earned += (uint32_t)qty * item_value(it, q);
        mark_shipped(it);
        hud_draw();
        goals_check();
    }
}

// ---------------------------------------------------------------- general store (Rosa)
static void buy_seeds(void) {
    uint8_t k, n = 0, map[NUM_CROPS], r, q, max, green = bld_find(B_GREENHOUSE) != 0xFF;
    for (k = 1; k <= NUM_CROPS && n < 10; k++) {
        if (crops[k].season != G.season && !green) continue;
        s_clr(); s_ch((char)(0x80 + k - 1)); s_add(items[k - 1].name); s_add(" seed"); s_pad(14); s_num(crops[k].seed_cost, 3);
        lbl_set(n); map[n++] = k;
    }
    if (!n) { ui_dialog("No seeds this season, sorry!"); return; }
    while (1) {
        uint32_t afford;
        uint8_t hv = 0;
        refresh();
        ui_hover = &hv;
        while (1) {
            crop_panel(map[hv]);
            r = ui_menu(0, 0, 20, lp, n, hv);
            if (r != 0xFE) break;
        }
        ui_hover = 0;
        if (r == 0xFF) return;
        k = map[r];
        afford = G.gold / crops[k].seed_cost;
        max = afford > 99 ? 99 : (uint8_t)afford;
        if (!max) { ui_dialog("You can't afford that right now."); continue; }
        q = ui_quantity(max, crops[k].seed_cost);
        if (!q) continue;
        G.gold -= (uint32_t)q * crops[k].seed_cost;
        G.seeds[k] += q;
        hud_draw();
    }
}

static uint8_t animal_count(uint8_t kind, uint8_t *home) {
    uint8_t k, b, cap = kind == A_COW ? 4 : 6, n;
    for (b = 0; b < MAX_BLD; b++) {
        if (G.bld[b].type != (kind == A_COW ? B_BARN : B_COOP) || (G.bld[b].flags & BF_RUIN)) continue;
        n = 0;
        for (k = 0; k < MAX_ANIMALS; k++) if (G.anim[k].kind == kind && G.anim[k].home == b) n++;
        if (n < cap) { *home = b; return 1; }
    }
    return 0;
}

static void buy_animal(uint8_t kind) {
    uint8_t home, k;
    uint16_t price = kind == A_COW ? 1500 : 400;
    if (!animal_count(kind, &home)) {
        ui_dialog(kind == A_COW ? "You need a barn with room first. Fix up the old one or build a new barn!" :
                                  "You need a coop with room first. Fix up the old one or build a new coop!");
        return;
    }
    if (G.gold < price) { ui_dialog("You can't afford that yet."); return; }
    for (k = 0; k < MAX_ANIMALS; k++) if (G.anim[k].kind == 0xFF) break;
    if (k == MAX_ANIMALS) { ui_dialog("That's as many animals as you can care for!"); return; }
    G.gold -= price;
    G.anim[k].kind = kind; G.anim[k].home = home; G.anim[k].happy = 100; G.anim[k].age = 0; G.anim[k].flags = 0;
    strcpy(G.anim[k].name, kind == A_COW ? "Daisy" : "Penny");
    ui_end();
    s_clr(); s_add(kind == A_COW ? "Name your cow" : "Name your hen");
    ui_name(sbuf, G.anim[k].name, 5);
    ui_begin();
    s_clr(); s_add(G.anim[k].name); s_add(" will be waiting at your "); s_add(kind == A_COW ? "barn" : "coop"); s_add(". Feed them hay every day!");
    ui_dialog(sbuf);
    hud_draw();
    goals_check();
}

static const char *const store_items[7] = {"Seeds", "Fertilizer", "Hay", "Cow  1500", "Hen   400", "Sell", "Bye"};
static void store(void) {
    uint8_t r, q;
    ui_speaker = "Rosa";
    say("Welcome to the General Store! Seeds for every season, and I'll buy whatever you grow.");
    while (1) {
        ui_begin(); ui_money();
        r = ui_menu(0, 0, 11, store_items, 7, 0);
        switch (r) {
        case 0: buy_seeds(); break;
        case 1:
            q = ui_quantity(G.gold / 50 > 99 ? 99 : (uint8_t)(G.gold / 50), 50);
            if (q) { G.gold -= (uint32_t)q * 50; G.fert += q; hud_draw(); ui_dialog("Fertilizer makes for better quality crops. Use it on tilled soil."); }
            break;
        case 2:
            q = ui_quantity(G.gold / 20 > 99 ? 99 : (uint8_t)(G.gold / 20), 20);
            if (q) { G.gold -= (uint32_t)q * 20; G.hay += q; hud_draw(); }
            break;
        case 3: buy_animal(A_COW); break;
        case 4: buy_animal(A_HEN); break;
        case 5: sell_filter = 0; sell_menu(); break;
        default: ui_dialog("Come back soon!"); ui_end(); ui_speaker = 0; return;
        }
        ui_end();
    }
}

// ---------------------------------------------------------------- blacksmith (Brick)
static const char *const tt_names[NUM_TT] = {"Hoe", "Watering Can", "Axe", "Hammer", "Fishing Rod"};
static const char *const tier_word[4] = {"Basic", "Copper", "Iron", "Gold"};
static const uint16_t tier_gold[3] = {2000, 5000, 12000};

static void smith(void) {
    static const char *const sm[3] = {"Upgrade", "Sell Ore", "Bye"};
    uint8_t r, k, t, up = 0xFF;
    ui_speaker = "Brick";
    say("Brick's Forge. Bring me ore and coin and I'll make your tools sing.");
    while (1) {
        ui_begin(); ui_money();
        r = ui_menu(0, 0, 11, sm, 3, 0);
        if (r == 0) {
            for (k = 0; k < NUM_TT; k++) {
                t = G.tier[k];
                s_clr(); s_add(tt_names[k]); s_pad(12);
                if (t >= 3) s_add("MAX");
                else { s_ch('G'); s_num(tier_gold[t], 0); }
                lbl_set(k);
            }
            refresh();
            r = ui_menu(0, 0, 20, lp, NUM_TT, 0);
            if (r != 0xFF) {
                t = G.tier[r];
                if (t >= 3) ui_dialog("That one's as good as it gets!");
                else {
                    uint8_t ore = I_COPPER + t;
                    s_clr(); s_add("Make it "); s_add(tier_word[t + 1]); s_add("?\nG"); s_num(tier_gold[t], 0); s_add(" + 5 "); s_add(items[ore].name);
                    if (ui_yesno(sbuf)) {
                        if (G.gold < tier_gold[t] || inv_count(ore) < 5) ui_dialog("You'll need more than that. Ore comes from the quarry and the hollow.");
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

// ---------------------------------------------------------------- carpenter (June)
static const char *const style_names_[4] = {"Classic", "Modern", "Log Cabin", "Cottage"};
static const uint16_t style_gold[4] = {1500, 4000, 2500, 3000};

static void carpenter(void) {
    static const char *const cm[5] = {"House", "Styles", "Materials", "Advice", "Bye"};
    uint8_t r, h = bld_find(B_HOUSE), lv, k, q;
    const char *toast = 0;
    ui_speaker = "June";
    say("June's Carpentry! Houses, renovations, lumber. What are we building?");
    while (1) {
        ui_begin(); ui_money();
        r = ui_menu(0, 0, 12, cm, 5, 0);
        lv = (G.bld[h].flags & BF_LVL) >> 1;
        if (r == 0) {
            if (lv >= 2) ui_dialog("Your house is as big as it gets. It's a beauty!");
            else {
                uint32_t g = lv ? 8000 : 2500; uint16_t w = lv ? 120 : 60, s = lv ? 80 : 0;
                s_clr(); s_add(lv ? "Grow into a big two-storey home?\n" : "Rebuild the shack into a real house?\n"); cost_str(g, w, s);
                if (ui_yesno(sbuf) && pay(g, w, s)) {
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
                    if (ui_yesno(sbuf) && pay(style_gold[r], 0, 0)) {
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
            r = ui_menu(0, 0, 14, mm, 2, 0);
            if (r != 0xFF) {
                uint16_t price = r ? 15 : 10;
                q = ui_quantity(G.gold / price > 99 ? 99 : (uint8_t)(G.gold / price), price);
                if (q) { G.gold -= (uint32_t)q * price; if (r) G.stone += q; else G.wood += q; hud_draw(); }
            }
        } else if (r == 3) {
            ui_dialog("Farm buildings, fences, paths, ponds, decor... you build all of it yourself from your START menu's BUILD page. Anywhere on land you own!");
            ui_dialog("And if a spot doesn't feel right, use MOVE to pick a building up and put it somewhere new.");
        } else {
            ui_dialog("Happy building!"); ui_end(); ui_speaker = 0;
            if (toast) hud_flash(toast);   // level-up toast once the shop closes
            return;
        }
        ui_end();
    }
}

// ---------------------------------------------------------------- cafe (Ada)
static void cafe(void) {
    static const char *const fm[4] = {"Coffee   150", "Lunch    350", "Feast    800", "Bye"};
    static const uint16_t price[3] = {150, 350, 800};
    static const uint8_t energy[3] = {20, 50, 255};
    uint8_t r;
    ui_speaker = "Ada";
    say("Welcome to Ada's! Sit down, you look like you've been pulling weeds all day.");
    while (1) {
        ui_begin(); ui_money();
        r = ui_menu(0, 4, 14, fm, 4, 0);
        if (r < 3) {
            if (G.gold < price[r]) ui_dialog("Maybe next time, hon.");
            else {
                uint16_t e = (uint16_t)G.energy + energy[r];
                G.gold -= price[r];
                G.energy = e > G.energy_max ? G.energy_max : (uint8_t)e;
                sfx(SFX_COIN);
                hud_draw();
                ui_dialog(r == 0 ? "Fresh brewed. You feel more awake!" : r == 1 ? "A hearty lunch. Energy restored!" : "A full feast! You feel brand new!");
            }
        } else { ui_dialog("Come back hungry!"); ui_end(); ui_speaker = 0; return; }
        ui_end();
    }
}

// ---------------------------------------------------------------- land office
static void realtor(void) {
    uint8_t p, n = 0, map[16], r;
    ui_speaker = "Land Office";
    for (p = 0; p < 16 && n < 12; p++) {
        if (G.plots & ((uint16_t)1 << p)) continue;
        s_clr(); plot_name_add(p); s_pad(13); s_num(plot_price(p), 5);
        lbl_set(n); map[n++] = p;
    }
    if (!n) { say("You own every acre around! What a farm."); ui_speaker = 0; return; }
    say("Land for sale all around your farm! Woods, a lake, a river, a quarry... pick a plot.");
    ui_begin(); ui_money();
    r = ui_menu(0, 3, 20, lp, n, 0);
    ui_end();
    if (r != 0xFF) buy_plot_prompt(map[r]);
    ui_speaker = 0;
}

// ---------------------------------------------------------------- townsfolk
static const char *const lines[NUM_NPC][3] = {
    {"Rain or shine, the seeds keep coming in. What are you planting this season?",
     "Your farm's the talk of the town! People say you've got a green thumb.",
     "You know, your grandpa used to buy his seeds right here. He'd be so proud."},
    {"Hmph. Ore from the quarry makes the best tools. Bring me some.",
     "That hammer of yours could use some love. Come by the forge.",
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
    switch (npc) {
    case NPC_ROSA: return it == I_SUNFLOWER || it == I_HERB || it == I_STRAWB;
    case NPC_BRICK: return it >= I_COPPER && it <= I_GEM;
    case NPC_JUNE: return it == I_CIDER || it == I_APPLE || it == I_CORN;
    case NPC_ADA: return it == I_MILK || it == I_EGG || it == I_CHEESE || it == I_BERRIES;
    case NPC_HOLLIS: return it == I_PUMPKIN || it == I_HONEY || it == I_JAM;
    default: return it >= I_MINNOW && it <= I_KOI;
    }
}


void npc_talk(uint8_t npc) BANKED {
    uint8_t h = G.friend[npc] / 50, it;
    ui_speaker = npc_names[npc];
    if (!(G.talked & (1 << npc))) {
        G.talked |= 1 << npc;
        if (G.friend[npc] < 253) G.friend[npc] += 2;
    }
    say(lines[npc][h >= 3 ? 2 : h >= 1 ? 1 : (rnd() & 1) ? 0 : 1]);
    if (!(G.gifted & (1 << npc)) && ui_yesno("Give a gift?")) {
        it = ui_pick_item(PICK_ANY);
        if (it != 0xFF) {
            uint8_t l = loves(npc, it), add = l ? 12 : 4;
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
    switch (act) {
    case ACT_STORE: store(); break;
    case ACT_SMITH: smith(); break;
    case ACT_CARPENTER: carpenter(); break;
    case ACT_CAFE: cafe(); break;
    case ACT_REALTOR: realtor(); break;
    case ACT_HALL: hall_act(); break;
    case ACT_BOARD: board_act(); break;
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
