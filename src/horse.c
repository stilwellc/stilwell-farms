// Stilwell Farms -- the horse: bought at Alyssa's once you have a stable, named, brushed and ridden (banked)
//
// The horse waits by the stable (actors.c spawns it on the farm). A on it: Ride, Brush, or a look at it.
// Riding moves twice as fast as walking; A or B steps you back down beside it, anywhere. Leaving the farm
// (or going indoors) leaves the horse at the stable. Friendship (G.horse_love, five hearts at 250) grows
// with a brush and a ride every day. Coats follow the breed rarity: White, Chestnut, Palomino (golden).
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"

BANKREF(horse_shop)

uint8_t riding;

extern void put16(uint8_t oam, int16_t x, int16_t y, uint8_t tile, uint8_t pal, uint8_t flip);
extern void hide16(uint8_t oam);

static const char *const coat_name[NUM_RARITY] = {"White", "Chestnut", "Palomino"};
static const char *const coat_pet[NUM_RARITY] = {"Clover", "Rusty", "Goldie"};
static const uint16_t coat_price[NUM_RARITY] = {4000, 9000, 20000};

static void love(uint8_t n) {
    G.horse_love = (uint16_t)G.horse_love + n > 250 ? 250 : G.horse_love + n;
}

// ---------------------------------------------------------------- buying (from the store menu)
void horse_shop(void) BANKED {
    uint8_t k, c;
    for (k = 0; k < NUM_RARITY; k++) {
        s_clr(); s_add(coat_name[k]); s_pad(10); s_num(coat_price[k], 5);
        lbl_set(k);
    }
    ui_end(); ui_begin(); ui_money();
    c = ui_menu(0, 0, 20, lp, NUM_RARITY, 0);
    if (c == 0xFF) return;
    if (c == R_LEGEND && !(G.year > 1 || G.earned >= 20000)) {
        ui_dialog("A palomino is for proven farmers. Earn G20000 or come back next year, and she's yours!");
        return;
    }
    if (bld_find(B_STABLE) == 0xFF) { ui_dialog("Build a stable on your farm first: START, then BUILD!"); return; }
    if (G.gold < coat_price[c]) { ui_dialog("You can't afford that yet."); return; }
    G.gold -= coat_price[c];
    G.horse = c + 1;
    G.horse_love = 20;
    G.horse_day = 0;
    strcpy(G.horse_name, coat_pet[c]);
    ui_end();
    s_clr(); s_add("Name your horse");   // prompt via sbuf: ui_name lives in another bank
    ui_name(sbuf, G.horse_name, 5);
    ui_begin();
    s_clr(); s_add(G.horse_name); s_add(" will be waiting by your stable. Brush and ride every day to make friends!");
    ui_dialog(sbuf);
    hud_draw();
    goals_check();
}

// ---------------------------------------------------------------- on the farm
void stable_act(void) BANKED {
    if (!G.horse) { say("A fine stable, ready for a horse. Alyssa at Seeds & Needs can find you one!"); return; }
    s_clr(); s_add(G.horse_name); s_add("'s stable. Fresh straw, a full water bucket and a brass name plate.");
    say(sbuf);
}

static void mount(uint8_t a) {
    uint8_t x, y;
    (void)a;
    if (!front_cell(&x, &y)) return;
    G.px = x; G.py = y;
    ppx = G.px * 16; ppy = G.py * 16;   // (a uint8 << 4 here lost its high byte)
    riding = 1;
    sfx(SFX_CONFIRM);
    if (!(G.horse_day & 2)) { G.horse_day |= 2; love(5); }
    if (!(G.barn_tips & 4)) {
        G.barn_tips |= 4;
        say("Giddy up! Riding is twice as fast as walking. Press A or B to climb down.");
    }
}

void horse_dismount(void) BANKED {
    static const int8_t ox[4] = {0, 0, -1, 1}, oy[4] = {1, -1, 0, 0};
    static const uint8_t order[4] = {D_LEFT, D_RIGHT, D_DOWN, D_UP};
    uint8_t k, d, hx = G.px, hy = G.py;
    for (k = 0; k < 4; k++) {
        int8_t x, y;
        d = order[k];
        x = (int8_t)hx + ox[d]; y = (int8_t)hy + oy[d];
        if (x < 0 || y < 0 || !walkable(x, y)) continue;
        riding = 0;
        horse_actor_move(hx, hy);
        G.px = x; G.py = y;
        ppx = G.px * 16; ppy = G.py * 16;
        sfx(SFX_STEP);
        return;
    }
    sfx(SFX_BUMP);
    hud_flash("No room to get off");
}

static const char *const talk_items[4] = {"Ride", "Brush", "Look", "Back"};

void horse_talk(uint8_t a) BANKED {
    uint8_t x, y, r;
    front_cell(&x, &y);
    fx_show(SPR_HEART, SPR_HEART_PAL, x, y);
    ui_begin();
    s_clr(); s_add(G.horse_name); s_add(" nickers.");
    ui_box(0, 12, 20, 6);
    ui_text(1, 13, sbuf);
    r = ui_menu(12, 3, 8, talk_items, 4, 0);
    ui_end();
    if (r == 0) mount(a);
    else if (r == 1) {
        if (G.horse_day & 1) { s_clr(); s_add(G.horse_name); s_add("'s coat is already shining."); hud_flash(sbuf); return; }
        G.horse_day |= 1;
        love(8);
        sfx(SFX_COIN);
        s_clr(); s_add(G.horse_name); s_add(" loves it!");
        hud_flash(sbuf);
        fx_show(SPR_HEART, SPR_HEART_PAL, x, y);
    } else if (r == 2) horse_panel();
}

// ---------------------------------------------------------------- the horse panel (also from the FRIENDS page)
static void wait_ab(void) {
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B)) { sfx(SFX_CURSOR); return; }
        wait_vbl_done();
    }
}

void horse_panel(void) BANKED {
    uint8_t c = (G.horse - 1) % NUM_RARITY, j, h = G.horse_love / 50;
    static const uint8_t pal[NUM_RARITY] = {SPAL_ANIMAL, SPAL_BREEDRARE, SPAL_BREEDGOLD};
    if (!G.horse) return;
    ui_begin();
    ui_box(0, 9, 20, 8);
    ui_title(1, 9, G.horse_name);
    portrait(SPR_HORSE_SIDE, (SPR_HORSE_SIDE_PAL & 8) | pal[c]);
    s_clr(); s_add(coat_name[c]); s_add(" horse");
    if (c) { s_ch(' '); s_ch(c == R_LEGEND ? (char)0xF1 : (char)0xF0); }
    ui_text(4, 10, sbuf);
    ui_text(4, 11, "Your horse");
    for (j = 0; j < 5; j++) ui_char(4 + j, 12, j < h ? GL_HEART : GL_HEART_EMPTY);
    s_clr(); s_add("Brushed today "); s_add((G.horse_day & 1) ? "yes" : "no");
    ui_text(1, 14, sbuf);
    s_clr(); s_add("Ridden today  "); s_add((G.horse_day & 2) ? "yes" : "no");
    ui_text(1, 15, sbuf);
    wait_ab();
    hide16(2);
    ui_end();
}
