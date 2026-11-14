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
static const uint8_t wx_icon[4] = {GL_ICON_SUN, GL_ICON_RAIN, GL_ICON_SNOW, GL_ICON_RAIN};
static const uint8_t hud_bar[5] = {GL_HUD_BAR0, GL_HUD_BAR1, GL_HUD_BAR2, GL_HUD_BAR3, GL_HUD_BAR4};
#define HD(n) (GL_HUD_0 + (n))


static void hud_2(uint8_t x, uint8_t v, uint8_t lead_blank) {
    hud_put(x, (v < 10 && lead_blank) ? GL_HUD_BLANK : HD(v / 10));
    hud_put(x + 1, HD(v % 10));
}

void hud_draw(void) BANKED {
    uint8_t h, e;
    uint32_t g;
    if (hud_timer) return;
    hud_put(0, building ? GL_CURSOR : tool_icon[G.tool]);
    hud_put(1, season_icon[G.season]);
    hud_2(2, G.day, 1);
    hud_put(4, G.time >= 1200 ? GL_ICON_MOON : wx_icon[G.weather & 3]);
    h = (G.time / 60) % 24;
    hud_2(5, h == 0 ? 12 : (h > 12 ? h - 12 : h), 1);
    hud_put(7, GL_HUD_COLON);
    hud_2(8, (uint8_t)(G.time % 60), 0);
    hud_put(10, h < 12 ? GL_HUD_AM : GL_HUD_PM);
    hud_put(11, GL_HUD_BLANK);
    hud_put(12, GL_HUD_BOLT);
    e = (uint8_t)(((uint16_t)G.energy * 8 + G.energy_max - 1) / G.energy_max);
    hud_put(13, hud_bar[e > 4 ? 4 : e]);
    hud_put(14, hud_bar[e > 4 ? e - 4 : 0]);
    hud_put(15, GL_HUD_COIN);
    g = G.gold;
    if (g >= 10000) {
        g /= 1000;
        if (g > 999) g = 999;
        hud_put(16, g >= 100 ? HD(g / 100) : GL_HUD_BLANK);
        hud_put(17, g >= 10 ? HD((g / 10) % 10) : GL_HUD_BLANK);
        hud_put(18, HD(g % 10));
        hud_put(19, GL_HUD_K);
    } else {
        uint16_t v = (uint16_t)g;
        hud_put(16, v >= 1000 ? HD(v / 1000) : GL_HUD_BLANK);
        hud_put(17, v >= 100 ? HD((v / 100) % 10) : GL_HUD_BLANK);
        hud_put(18, v >= 10 ? HD((v / 10) % 10) : GL_HUD_BLANK);
        hud_put(19, HD(v % 10));
    }
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
            if (strlen(items[s - 1].name) <= 7) s_add("Seeds: ");
            s_add(items[s - 1].name); s_add(" x"); s_num(G.seeds[s], 0);
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
    if (scene != SCENE_FARM) { sfx(SFX_STEP); }
    else {
        i = WIDX(G.px, G.py);
        c = G.cell[i]; k = G.a1[i];
        if (c == C_SAND) sfx(SFX_STEP_SAND);
        else if (c == C_DECOR)
            sfx(k == DK_GRAVEL ? SFX_STEP_GRAVEL : (k == DK_PLANK || k == DK_BRIDGE) ? SFX_STEP_WOOD : SFX_STEP);
    }
    if (G.gold >= 1000 && !(G.tips & TIP_GOLD)) {
        G.tips |= TIP_GOLD;
        k = bld_find(B_HOUSE);
        if (k != 0xFF && !(G.bld[k].flags & BF_LVL))
            say("Tip: 1000G saved! June's Carpentry in town can rebuild your shack into a real house. More room, more energy!");
        return;
    }
    if (scene != SCENE_FARM || !front_cell(&x, &y)) return;
    i = WIDX(x, y);
    c = G.cell[i];
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
