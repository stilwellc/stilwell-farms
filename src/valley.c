// Stilwell Farms -- the Valley Restoration: Mayor Hollis's board at town hall.  Six projects take
// donations of goods plus gold; each one finished changes the town for good (restored art drawn over
// the town map by valley_mt(), like biz_mt), gives a reward, and brings a letter from Grandpa.  The
// last one needs the other five and ends in a celebration (the game goes on afterwards).  (banked)
//
// Save: G.valley[p] = bit per donated item slot + VP_GOLD; G.valley_done = VD_* bits (game.h).
// Item slots of NO_ITEM are skipped: they are kept free for goods added later (see the table).
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(valley_board)

#define NO_ITEM 0xFF
typedef struct { uint8_t it[VP_SLOTS], n[VP_SLOTS]; uint16_t gold; } Proj;
static const Proj proj[NUM_PROJ] = {
    // Town Garden: one crop from each season
    {{I_RADISH, I_TOMATO, I_PUMPKIN, I_SNOWPEA, NO_ITEM}, {5, 3, 2, 2, 0}, 1500},
    // Old Orchard: all four fruits
    {{I_APPLE, I_PEACH, I_ORANGE, I_CHERRY, NO_ITEM}, {3, 3, 3, 3, 0}, 3000},
    // Clear Streams: river and lake fish, and a tuna from the sea
    {{I_BASS, I_TROUT, I_CATFISH, I_CARP, I_TUNA}, {2, 2, 2, 3, 1}, 4000},
    // Town Lamps: metal for the lamp posts, coal for the forge, a topaz for the square's beacon
    {{I_COPPER, I_IRON, I_GOLD, I_COAL, I_TOPAZ}, {9, 5, 3, 10, 1}, 10000},
    // Market Pantry: the farm's animal and artisan goods
    {{I_MILK, I_EGG, I_CHEESE, I_WOOL, I_JAM}, {5, 5, 3, 2, 2}, 8000},
    // Town Fountain: the very best of the valley, crowned with a diamond from the deep mine
    {{I_KOI, I_GEM, I_FEAST, I_CIDER, I_DIAMOND}, {1, 1, 1, 1, 1}, 60000},
};
static const char *const proj_name[NUM_PROJ] = {"Town Garden", "Old Orchard", "Clear Streams", "Town Lamps",
                                               "Market Pantry", "Town Fountain"};
static const char *const proj_reward[NUM_PROJ] = {"Garden+dish", "Fruit trees", "Rarer fish",   // 11 columns
                                                 "Lit lamps", "Feast dish", "The finale"};
static const char *const proj_pitch[NUM_PROJ] = {
    "The park beds used to feed the whole town. Bring a crop from every season to replant them.",
    "The old park trees bore fruit once. Fresh fruit and a little care will wake them up.",
    "The streams silted up years ago. River fish and a tuna to restock them, and gold for the dredging crew.",
    "The lamps on the square went dark years ago. Brick needs metal, coal, a topaz and gold to fix them.",
    "The market tents sit empty. Milk, eggs, cheese, wool and jam would fill them again.",
    "The heart of the valley: the fountain, rebuilt with the finest things the valley makes."};
static const char *const proj_done[NUM_PROJ] = {
    "The park beds are a vegetable garden again! Here's the town's favourite recipe, as thanks.",
    "Look at the park! The old trees are heavy with fruit again.",
    "The streams run clear! Rarer fish will bite all over the valley from now on.",
    "The lamps on the square are glowing! And Star Night prizes will be twice as big now.",
    "The market's full again! The cafe sent over a very special recipe for you.",
    "The fountain is finished... and the whole town is gathering in the square!"};
static const char *const letters[NUM_PROJ] = {
    "The town garden is growing again. I planted the first beds there with your grandma.",
    "The old orchard! We used to pick cherries there every summer. I can almost taste them.",
    "Clear water again. The fish will thank you, and so will every kid with a fishing rod.",
    "I hear the square is lit up again at night. Your grandma and I danced under those lamps.",
    "A full market! The valley feeds itself again, and it's thanks to your farm.",
    "You did it. The whole valley, brought back to life. I never could have. Love, Grandpa."};

static uint8_t slot_used(uint8_t p, uint8_t k) { return proj[p].it[k] != NO_ITEM; }

static uint8_t proj_done_bits(uint8_t p) {   // the bits a finished project has set
    uint8_t k, b = VP_GOLD;
    for (k = 0; k < VP_SLOTS; k++) if (slot_used(p, k)) b |= 1 << k;
    return b;
}

static uint8_t popcount8(uint8_t v) { uint8_t n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

static uint8_t unlocked(uint8_t p) { return p < NUM_PROJ - 1 || (G.valley_done & 0x1F) == 0x1F; }

// ---------------------------------------------------------------- the town, restored
static const mt_t garden_mt[4] = {MT_RIPE_PUMPKIN, MT_RIPE_CORN, MT_RIPE_STRAWB, MT_RIPE_TOMATO};
static const mt_t orchard_mt[4] = {MT_FRUIT_TREE_RIPE, MT_PEACH_TREE_RIPE, MT_ORANGE_TREE_RIPE, MT_CHERRY_TREE_RIPE};

mt_t valley_mt(uint8_t x, uint8_t y) BANKED {
    mt_t m = map_mt(MIDX(x, y));
    uint8_t d = G.valley_done;
    if ((d & VD_LAMPS) && m == MT_TOWN_LAMP) return MT_TOWN_LAMP_LIT;
    if (y >= 16) {   // the park along the south street
        if ((d & VD_GARDEN) && m == MT_FLOWERBED) return garden_mt[(x + y) & 3];
        if ((d & VD_ORCHARD) && m == MT_TOWN_TREE) return orchard_mt[x & 3];
    }
    if ((d & VD_MUSEUM) && (uint16_t)(m - ST_TOWN_FOUNTAIN) < 4) return ST_VALLEY_FOUNTAIN + (m - ST_TOWN_FOUNTAIN);
    return 0xFFFF;
}

// ---------------------------------------------------------------- the finale
static void sparkle_wait(uint8_t frames) {
    uint8_t t;
    for (t = 0; t < frames; t++) {
        if (!(t & 15)) fx_show(SPR_SPARKLE, SPR_SPARKLE_PAL, 14 + (rnd() & 3), 10 + (rnd() & 3));
        update_sprites();
        wait_vbl_done();
    }
}

static void credit_line(uint8_t y, const char *s) {   // centred in the box, one at a time
    uint8_t n = 0;
    while (s[n]) n++;
    ui_text(10 - (n >> 1), y, s);
    sfx(SFX_TEXT);
    {
        uint8_t t;
        for (t = 0; t < 40; t++) wait_vbl_done();
    }
}

static void finale(void) {
    uint8_t k, n;
    enter_scene(MAP_TOWN, 15, 14, D_UP);   // everyone meets at the new fountain
    music_play(MUS_FESTIVAL);
    sparkle_wait(120);
    set_speaker("Hollis");
    say("Friends, neighbours! Look at our valley! The garden, the orchard, the lamps, the market...");
    say("None of it would be here without the farmer from the Stilwell place. Three cheers!");
    set_speaker("Rosa"); say("Hip hip... HOORAY! Your grandpa would be over the moon.");
    set_speaker("Brick"); say("Hmph. Not bad, farmer. Not bad at all.");
    set_speaker("June"); say("I'm putting your name on the fountain. In big letters!");
    set_speaker("Ada"); say("Supper's on me tonight. Every night, if you like!");
    set_speaker("Milo"); say("When I grow up I'm gonna fix up a whole valley too!");
    ui_speaker = 0;
    sparkle_wait(90);
    set_speaker("Grandpa");
    say("I always knew this valley could shine again. It just needed someone stubborn. Thank you.");
    ui_speaker = 0;
    // the credits card
    ui_begin();
    ui_box(0, 0, 20, 17);
    ui_title(3, 0, "THE VALLEY");
    credit_line(2, "Stilwell Valley");
    credit_line(3, "is restored!");
    for (k = 0; k < NUM_PROJ; k++) {
        s_clr(); s_ch((char)0xF1); s_ch(' '); s_add(proj_name[k]);
        ui_text(4, 5 + k, sbuf);
        sfx(SFX_COIN);
        for (n = 0; n < 20; n++) wait_vbl_done();
    }
    s_clr(); s_add("Year "); s_num(G.year, 0); s_ch(' '); s_add(season_name[G.season]); s_ch(' '); s_num(G.day, 0);
    credit_line(12, sbuf);
    for (n = 0, k = 0; k < NUM_DISHES; k++) n += (G.recipes >> k) & 1;
    s_clr(); s_add("Recipes "); s_num(n, 0); s_ch('/'); s_num(NUM_DISHES, 0);
    credit_line(13, sbuf);
    s_clr(); s_add("Dishes cooked "); s_num(G.stat_cooked, 0);
    credit_line(14, sbuf);
    credit_line(15, "The farm goes on!");
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); break; }
        wait_vbl_done();
    }
    ui_end();
    G.valley_done |= VD_ENDING;
    music_for_scene();
}

// ---------------------------------------------------------------- finishing a project
static void complete(uint8_t p) {
    G.valley_done |= 1 << p;
    G.mail |= 1UL << (MAIL_VALLEY + p);
    redraw_rect(0, 0, 32, 24);   // the town under the menus changes now
    ui_end();
    music_play(MUS_JINGLE_FANFARE);
    set_speaker("Hollis");
    say(proj_done[p]);
    ui_speaker = 0;
    music_for_scene();
    if (p == 0) cook_learn(I_SORBET - I_DISH0);
    if (p == 4) cook_learn(I_FEAST - I_DISH0);
    say("There'll be a letter in your mailbox, I'd wager.");
    ui_begin();
    if (p == NUM_PROJ - 1) { ui_end(); finale(); ui_begin(); }
}

// ---------------------------------------------------------------- one project: donate
static void proj_panel(uint8_t p, uint8_t k) {   // k = slot under the cursor, VP_SLOTS = gold
    uint8_t it;
    ui_panel(0, 13, 20, 4);
    if (k >= VP_SLOTS) {
        s_clr();
        if (G.valley[p] & VP_GOLD) s_add("Paid. Thank you!");
        else { s_add("You have G"); s_num(G.gold, 0); }
    } else {
        it = proj[p].it[k];
        s_clr();
        if (G.valley[p] & (1 << k)) s_add("Donated. Thanks!");
        else { s_add("You have "); s_num(inv_count(it), 0); s_add(" of "); s_num(proj[p].n[k], 0); }
    }
    ui_text(2, 14, sbuf);
    s_clr(); s_add("Gift: "); s_add(proj_reward[p]);
    ui_text(2, 15, sbuf);
}

static void project(uint8_t p) {
    uint8_t n, k, r, map[VP_SLOTS + 1], hv = 0, it, q;
    set_speaker("Hollis");
    say(proj_pitch[p]);
    ui_speaker = 0;
    while (!(G.valley_done & (1 << p))) {
        n = 0;
        for (k = 0; k < VP_SLOTS; k++) {
            if (!slot_used(p, k)) continue;
            it = proj[p].it[k];
            s_clr(); s_ch((char)(0x80 + it)); s_add(items[it].name); s_pad(11); s_ch('x'); s_num(proj[p].n[k], 0);
            s_pad(15);
            if (G.valley[p] & (1 << k)) s_ch((char)0xF1);
            else if (inv_count(it) >= proj[p].n[k]) s_add("OK");
            lbl_set(n); map[n++] = k;
        }
        s_clr(); s_ch('G'); s_num(proj[p].gold, 0); s_add(" gold"); s_pad(15);
        if (G.valley[p] & VP_GOLD) s_ch((char)0xF1);
        else if (G.gold >= proj[p].gold) s_add("OK");
        lbl_set(n); map[n++] = VP_SLOTS;
        ui_begin();
        ui_hover = &hv;
        while (1) {
            proj_panel(p, map[hv < n ? hv : 0]);
            r = ui_menu(0, 0, 20, lp, n, hv);
            if (r != 0xFE) break;
        }
        ui_hover = 0;
        if (r == 0xFF) { ui_end(); return; }
        hv = r; k = map[r];
        if (k == VP_SLOTS) {
            if (G.valley[p] & VP_GOLD) ui_dialog("That part's paid for. Thank you!");
            else {
                s_clr(); s_add("Put G"); s_num(proj[p].gold, 0); s_add(" toward the "); s_add(proj_name[p]); s_ch('?');
                if (ui_yesno(sbuf) && pay(proj[p].gold, 0, 0)) {
                    G.valley[p] |= VP_GOLD;
                    sfx(SFX_COIN);
                    hud_draw();
                }
            }
        } else {
            it = proj[p].it[k]; q = proj[p].n[k];
            if (G.valley[p] & (1 << k)) ui_dialog("You've already brought those. Thank you!");
            else if (inv_count(it) < q) {
                s_clr(); s_add("This needs "); s_num(q, 0); s_ch(' '); s_add(items[it].name); s_add(". Bring them when you have them!");
                ui_dialog(sbuf);
            } else {
                s_clr(); s_add("Donate "); s_num(q, 0); s_ch(' '); s_add(items[it].name); s_add("?");
                if (ui_yesno(sbuf)) {
                    inv_take(it, q);
                    G.valley[p] |= 1 << k;
                    sfx(SFX_COIN);
                }
            }
        }
        if ((G.valley[p] & proj_done_bits(p)) == proj_done_bits(p)) { complete(p); ui_end(); return; }
        ui_end();
    }
}

// ---------------------------------------------------------------- the board (town hall, via Hollis)
static void board_panel(uint8_t p) {
    uint8_t k, x = 2, it;
    ui_panel(0, 13, 20, 4);
    if (!unlocked(p)) { ui_text(2, 14, "Finish the other"); ui_text(2, 15, "five projects!"); return; }
    for (k = 0; k < VP_SLOTS; k++) {   // icon + count, or icon + star once donated
        if (!slot_used(p, k)) continue;
        it = proj[p].it[k];
        ui_char(x, 14, item_icon_tile[it]);
        if (G.valley[p] & (1 << k)) ui_char(x + 1, 14, GL_IT_STAR2);
        else { s_clr(); s_num(proj[p].n[k], 0); ui_text(x + 1, 14, sbuf); }
        x += proj[p].n[k] >= 10 ? 4 : 3;   // (10 coal: two digits)
    }
    s_clr(); s_ch('G'); s_num(proj[p].gold, 0);
    if (G.valley[p] & VP_GOLD) s_ch((char)0xF1);
    ui_text(2, 15, sbuf);
    if (G.valley_done & (1 << p)) ui_text(11, 15, "Restored");
}

void valley_board(void) BANKED {
    uint8_t p, r, hv = 0;
    set_speaker("Hollis");
    if (!(G.cook_flags & 2)) {
        G.cook_flags |= 2;
        say("Say, farmer... this valley was the pride of the county once. The garden, the orchard...");
        say("I've put up a Valley Restoration board. Bring what each project needs, and we'll rebuild!");
    }
    ui_speaker = 0;
    while (1) {
        for (p = 0; p < NUM_PROJ; p++) {
            s_clr(); s_add(proj_name[p]); s_pad(14);
            if (G.valley_done & (1 << p)) s_ch((char)0xF1);
            else if (!unlocked(p)) s_ch('-');
            else { s_num(popcount8(G.valley[p]), 0); s_ch('/'); s_num(popcount8(proj_done_bits(p)), 0); }
            lbl_set(p);
        }
        ui_begin();
        ui_hover = &hv;
        while (1) {
            board_panel(hv);
            r = ui_menu(0, 0, 20, lp, NUM_PROJ, hv);
            if (r != 0xFE) break;
        }
        ui_hover = 0;
        ui_end();
        if (r == 0xFF) return;
        hv = r;
        if (G.valley_done & (1 << r)) {
            set_speaker("Hollis"); say(proj_done[r]); ui_speaker = 0;
        } else if (!unlocked(r)) {
            set_speaker("Hollis"); say("That's the big one. Let's finish the other five projects first!"); ui_speaker = 0;
        } else project(r);
    }
}

// ---------------------------------------------------------------- FARM > VALLEY
void valley_page(void) BANKED {   // name + status, then a 10-segment bar and n/m under it
    uint8_t p, k, segs, have, all, y;
    ui_box(0, 0, 20, 17);
    ui_title(2, 0, "VALLEY");
    for (p = 0; p < NUM_PROJ; p++) {
        y = 2 + p * 2;
        ui_text(2, y, proj_name[p]);
        have = popcount8(G.valley[p] & proj_done_bits(p)); all = popcount8(proj_done_bits(p));
        if (G.valley_done & (1 << p)) { ui_char(17, y, GL_IT_STAR2); have = all; }
        else if (!unlocked(p)) { ui_text(3, y + 1, "Finish the rest"); continue; }
        segs = (uint8_t)(((uint16_t)have * 10 + all - 1) / all);
        for (k = 0; k < 10; k++) ui_char(3 + k, y + 1, k < segs ? GL_BAR_FULL : GL_BAR_EMPTY);
        s_clr(); s_num(have, 0); s_ch('/'); s_num(all, 0);
        ui_text(14, y + 1, sbuf);
    }
    ui_text(2, 15, (G.valley_done & 0x3F) == 0x3F ? "Valley restored!" : "Board: town hall");
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); return; }
        wait_vbl_done();
    }
}

void valley_letter(uint8_t p) BANKED {
    set_speaker("Grandpa");
    say(letters[p]);
    ui_speaker = 0;
}
