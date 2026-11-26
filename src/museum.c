// Stilwell Farms -- the Library's collection (banked)
//
// Once the Library is built (invest.c) its back room takes one of everything the valley makes, grows,
// catches and digs: all 70 goods. Donate from your bag; the EXHIBITS pages fill in like the goods log. Each
// finished group (crops, fruit, fish, gems, dishes ...) earns a gift from the librarian, and the whole
// collection a big one. Something to look for on every trip: the last koi, a diamond, the sixteenth dish.
//
// Save: G.donated[9] (a bit per item, like G.shipped), G.museum_sets (a bit per rewarded group, bit 15 = all).
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(museum_act)

#define NGROUP 11
static const char *const group_name[NGROUP] = {"crops", "fruit", "farm goods", "wild finds", "river fish", "ores",
                                              "gems", "preserves", "sea fish", "beach finds", "dishes"};

uint8_t item_group(uint8_t it) BANKED {
    if (it < I_APPLE) return 0;
    if (it < I_MILK) return 1;
    if (it <= I_HONEY || it == I_WOOL) return 2;
    if (it < I_MINNOW) return 3;
    if (it <= I_KOI) return 4;
    if (it <= I_GOLD || it == I_COAL) return 5;
    if (it == I_GEM || (it > I_COAL && it < I_COAL + 5)) return 6;
    if (it < I_WOOL) return 7;
    if (it >= I_GOATMILK) return 2;
    if (it >= I_DISH0) return 10;
    return it < I_COAL + 10 ? 8 : 9;
}

static uint8_t donated(uint8_t it) { return G.donated[it >> 3] & (1 << (it & 7)); }

static uint8_t count_all(void) {
    uint8_t k, n = 0;
    for (k = 0; k < NUM_ITEMS; k++) if (donated(k)) n++;
    return n;
}

static void group_check(uint8_t g) {   // the group just finished? thank the farmer once
    uint8_t k, n = 0, miss = 0;
    uint32_t gift;
    if (G.museum_sets & (1U << g)) return;
    for (k = 0; k < NUM_ITEMS; k++)
        if (item_group(k) == g) { n++; if (!donated(k)) miss = 1; }
    if (miss) return;
    G.museum_sets |= 1U << g;
    gift = 1500UL * n;
    G.gold += gift;
    music_play(MUS_JINGLE_FANFARE);
    s_clr(); s_add("That's every one of the "); s_add(group_name[g]); s_add("! The shelf is full. Here, G"); s_num(gift, 0);
    s_add(" from the town.");
    say(sbuf);
    music_for_scene();
    hud_draw();
    if (count_all() == NUM_ITEMS && !(G.museum_sets & 0x8000)) {
        G.museum_sets |= 0x8000;
        G.gold += 50000;
        music_play(MUS_JINGLE_FANFARE);
        say("The whole valley, under one roof! Every exhibit. Nobody's ever done it. G50000, with our thanks!");
        music_for_scene();
        hud_draw();
    }
}

// ---------------------------------------------------------------- donating
static void donate(void) {
    static uint8_t page;
    uint8_t list[9], n, k, it, skip, r;
    while (1) {
        n = 0; skip = page * 8;
        for (it = 0; it < NUM_ITEMS && n < 9; it++) {
            if (donated(it) || !inv_count(it)) continue;
            if (skip) { skip--; continue; }
            list[n++] = it;
        }
        if (!n && !page) { say("Nothing in your bag is new to the collection. Bring something we haven't seen!"); return; }
        if (!n) { page = 0; continue; }
        for (k = 0; k < n && k < 8; k++) {
            s_clr(); s_ch((char)(0x80 + list[k])); s_ch(' '); s_add(items[list[k]].name);
            lbl_set(k);
        }
        s_clr(); s_add(n > 8 ? "More..." : page ? "First..." : "Back"); lbl_set(k);
        ui_begin();
        r = ui_menu(0, 0, 20, lp, k + 1, 0);
        ui_end();
        if (r == 0xFF) { page = 0; return; }
        if (r == k) {
            if (n > 8) page++;
            else if (page) page = 0;
            else return;
            continue;
        }
        it = list[r];
        inv_take(it, 1);
        G.donated[it >> 3] |= 1 << (it & 7);
        sfx(SFX_CONFIRM);
        s_clr(); s_add("A fine "); s_add(items[it].name); s_add("! It goes on the shelf with your name on the card. ");
        s_num(count_all(), 0); s_add(" of "); s_num(NUM_ITEMS, 0); s_add(" now.");
        say(sbuf);
        group_check(item_group(it));
        page = 0;
    }
}

// ---------------------------------------------------------------- the exhibits (two pages, like the goods log)
static void exhibits_draw(uint8_t page) {
    uint8_t k, i, slot, last = page ? NUM_ITEMS - 40 : 40;
    ui_box(0, 0, 20, 17);
    ui_title(2, 0, page ? "EXHIBITS 2/2" : "EXHIBITS 1/2");
    for (i = 0; i < last; i++) {
        k = page ? 40 + i : i;
        slot = (page && k >= I_DISH0) ? i + 2 : i;   // dishes start on a fresh row
        ui_char(2 + (slot & 7) * 2, 2 + (slot >> 3) * 2, donated(k) ? item_icon_tile[k] : font_map['?' - 32]);
    }
    s_clr(); s_add("Donated "); s_num(count_all(), 0); s_ch('/'); s_num(NUM_ITEMS, 0);
    ui_text(2, 14, sbuf);
}

static void exhibits(void) {
    uint8_t page = 0;
    ui_begin();
    exhibits_draw(0);
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); break; }
        if (joy_press & (J_LEFT | J_RIGHT)) { page ^= 1; sfx(SFX_CURSOR); exhibits_draw(page); }
        wait_vbl_done();
    }
    ui_end();
}

// 1 = the farmer chose to read a book (invest.c says one)
uint8_t museum_act(void) BANKED {
    static const char *const mm[4] = {"Read a book", "Donate", "Exhibits", "Bye"};
    uint8_t r;
    if (!(G.inv_tips & 8)) {
        G.inv_tips |= 8;
        say("The back room is the valley's collection. Bring one of anything you grow, catch, dig or cook!");
    }
    while (1) {
        ui_begin();
        r = ui_menu(0, 0, 14, mm, 4, 0);
        ui_end();
        if (r == 0) return 1;
        if (r == 1) donate();
        else if (r == 2) exhibits();
        else return 0;
    }
}
