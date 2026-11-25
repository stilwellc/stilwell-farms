// Stilwell Farms -- the status bar (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "decor.h"
BANKREF(hud_draw)
extern uint8_t hud_timer, hud_row;
void hud_put(uint8_t x, uint8_t t);

static const uint8_t tool_icon[NUM_TOOLS] = {GL_ICON_HAND, GL_ICON_HOE, GL_ICON_CAN, GL_ICON_SEED,
                                             GL_ICON_SICKLE, GL_ICON_AXE, GL_ICON_HAMMER, GL_ICON_ROD, GL_ICON_FERT};
static const uint8_t season_icon[4] = {GL_HUD_SPRING, GL_HUD_SUMMER, GL_HUD_FALL, GL_HUD_WINTER};
static const uint8_t wx_icon[4] = {GL_HUD_SUN, GL_HUD_RAIN, GL_HUD_SNOW, GL_HUD_RAIN};
static const uint8_t hud_bar[5] = {GL_HUD_BAR0, GL_HUD_BAR1, GL_HUD_BAR2, GL_HUD_BAR3, GL_HUD_BAR4};
#define HD(n) (GL_HUD_0 + (n))


// The bar is built in a buffer and written as one window row (win_row); digits come from subtraction
// loops. With 20 separate tile writes and ~12 library divides it cost ~40% of a frame every game
// 10 minutes, enough to drop a frame when it landed on a scrolling frame.
static uint8_t hb[20];

static void hud_2(uint8_t x, uint8_t v, uint8_t lead_blank) {
    uint8_t t = 0;
    while (v >= 10) { v -= 10; t++; }
    hb[x] = (!t && lead_blank) ? GL_HUD_BLANK : HD(t);
    hb[x + 1] = HD(v);
}

static uint16_t dig_v;
static uint8_t digit(uint16_t place) {   // peel the next decimal digit off dig_v
    uint8_t d = 0;
    while (dig_v >= place) { dig_v -= place; d++; }
    return d;
}

void hud_draw(void) BANKED {
    uint8_t h = 0, e = 0, m;
    uint16_t t;
    if (hud_timer) return;
    hb[0] = building ? GL_CURSOR : tool_icon[G.tool];
    hb[1] = season_icon[G.season];
    hud_2(2, G.day, 1);
    hb[4] = G.buff ? GL_HUD_BUFF : GL_HUD_BLANK;   // food buff (cooking.c), else a blank parts the date from the clock group; 6px HUD weather glyphs keep 10:00 clear
    hb[5] = G.time >= 1200 ? GL_HUD_MOON : wx_icon[G.weather & 3];
    for (t = G.time; t >= 60; t -= 60) h++;
    m = (uint8_t)t;
    while (h >= 24) h -= 24;
    hud_2(6, h == 0 ? 12 : (h > 12 ? h - 12 : h), 1);
    hb[8] = GL_HUD_COLON;
    hud_2(9, m, 0);
    hb[11] = h < 12 ? GL_HUD_AM : GL_HUD_PM;
    hb[12] = GL_HUD_BOLT;
    if (G.energy_max)   // segments = ceil(energy * 8 / max)
        for (t = (uint16_t)G.energy * 8 + G.energy_max - 1; t >= G.energy_max; t -= G.energy_max) e++;
    hb[13] = hud_bar[e > 4 ? 4 : e];
    hb[14] = hud_bar[e > 4 ? e - 4 : 0];
    hb[15] = GL_HUD_COIN;
    if (G.gold >= 10000) {
        uint32_t g = G.gold / 1000;   // the one 32-bit divide
        uint8_t d, any = 0;
        dig_v = g > 9999 ? 9999 : (uint16_t)g;
        if (dig_v >= 1000) { d = digit(1000); hb[15] = HD(d); any = 1; }   // a millionaire: 1000k.. over the coin
        d = digit(100); any |= d; hb[16] = any ? HD(d) : GL_HUD_BLANK;
        d = digit(10); any |= d; hb[17] = any ? HD(d) : GL_HUD_BLANK;
        hb[18] = HD((uint8_t)dig_v);
        hb[19] = GL_HUD_K;
    } else {
        uint8_t d, any;
        dig_v = (uint16_t)G.gold;
        d = digit(1000); any = d; hb[16] = any ? HD(d) : GL_HUD_BLANK;
        d = digit(100); any |= d; hb[17] = any ? HD(d) : GL_HUD_BLANK;
        d = digit(10); any |= d; hb[18] = any ? HD(d) : GL_HUD_BLANK;
        hb[19] = HD((uint8_t)dig_v);
    }
    win_row(hud_row, hb);
}

// ---------------------------------------------------------------- SELECT: what the new tool is
static const char *const tool_name[NUM_TOOLS] = {"Hands: pick & pull", "Hoe", "Watering Can", "Seeds", "Sickle: weeds, hay",
                                                 "Axe", "Hammer", "Fishing Rod", "Fertilizer"};
static const uint8_t tool_tt[NUM_TOOLS] = {0xFF, TT_HOE, TT_CAN, 0xFF, 0xFF, TT_AXE, TT_HAMMER, TT_ROD, 0xFF};
static const char *const tier_name[4] = {"Basic", "Copper", "Iron", "Gold"};

void tool_toast(void) BANKED {
    uint8_t t = G.tool, s = G.seed_sel, tt = tool_tt[t];
    s_clr();
    if (t == T_SEEDS) {
        if (!s) { s_add("Seeds: none"); }
        else {
            uint8_t q = G.seeds[s][G.seed_rar];
            if (strlen(items[s - 1].name) + (q >= 100 ? 3 : q >= 10 ? 2 : 1) <= 9) s_add("Seeds: ");   // fits 18
            s_add(items[s - 1].name); s_add(" x"); s_num(G.seeds[s][G.seed_rar], 0);
        }
    } else if (t == T_FERT) {
        s_add(tool_name[t]); s_add(" x"); s_num(G.fert, 0);
    } else {
        s_add(tool_name[t]);
        if (t == T_CAN) { s_ch(' '); s_num(G.water, 0); s_ch('/'); s_num(30 + G.tier[TT_CAN] * 20, 0); }
        else if (tt != 0xFF) {
            const char *w = tier_name[G.tier[tt] & 3];
            if (s_len() + strlen(w) + 3 <= 18) { s_add(" ("); s_add(w); s_ch(')'); }
            else { s_ch(' '); s_add(w); }
        }
    }
    hud_flash(sbuf);
}

// ---------------------------------------------------------------- after each step: footsteps + first-day tips
#define TIP_WEEDS 0x04
#define TIP_LAND 0x40
#define TIP_GOLD 0x80

void after_step(void) BANKED {
    uint8_t x, y, c, k;
    uint16_t i;
    if (IS_AREA(scene)) { area_step(); return; }   // the mine and the beach: footsteps + ladders (areas.c)
    if (scene != SCENE_FARM) { sfx(SFX_STEP); }
    else if (riding) { if ((G.px ^ G.py) & 1) sfx(SFX_STEP_WOOD); }   // hoofbeats
    else {
        i = WIDX(G.px, G.py);
        c = W_CELL[i]; k = W_A1[i];
        if (c == C_SAND) sfx(SFX_STEP_SAND);
        else if (c == C_DECOR)
            sfx(k == DK_GRAVEL ? SFX_STEP_GRAVEL : (k == DK_PLANK || k == DK_BRIDGE) ? SFX_STEP_WOOD : SFX_STEP);
    }
    if (G.gold >= 1000 && !(G.tips & TIP_GOLD)) {
        G.tips |= TIP_GOLD;
        k = bld_find(B_HOUSE);
        if (k != 0xFF && !(G.bld[k].flags & BF_LVL))
            say("Tip: 1000G saved! Knock on Wood in town can rebuild your shack into a real house. More energy!");
        return;
    }
    if (scene != SCENE_FARM || !front_cell(&x, &y)) return;
    i = WIDX(x, y);
    c = W_CELL[i];
    if (!owned(x, y)) {
        if (c != C_ROAD && !(G.tips & TIP_LAND)) {
            G.tips |= TIP_LAND;
            say("Tip: past the rope is land for sale. Face it and press A to see the price. Each plot has its own terrain!");
        }
    } else if (c == C_WEEDS && !(G.tips & TIP_WEEDS)) {
        G.tips |= TIP_WEEDS;
        say("Tip: weeds! Press A with your Hands or the Sickle to pull them. Sometimes you'll find seeds.");
    }
}
