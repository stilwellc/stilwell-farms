// Stilwell Farms -- inventory and money helpers (banked)
#pragma bank 255
#include <gb/gb.h>
#include "game.h"
BANKREF(inv_count)

uint16_t inv_count(uint8_t it) BANKED {
    return (uint16_t)G.inv[it][0] + G.inv[it][1] + G.inv[it][2];
}

void inv_add(uint8_t it, uint8_t q, uint8_t n) BANKED {
    uint16_t v = (uint16_t)G.inv[it][q] + n;
    G.inv[it][q] = v > 255 ? 255 : (uint8_t)v;
}

uint8_t inv_take(uint8_t it, uint8_t n) BANKED {
    uint8_t got = 0, q;
    for (q = 0; q < 3 && got < n; q++) {
        while (G.inv[it][q] && got < n) { G.inv[it][q]--; got++; }
    }
    return got;
}

// processed goods are worth a multiple of their ingredient; base table entries of 0 use the average
uint16_t item_value(uint8_t it, uint8_t q) BANKED {
    uint16_t v = items[it].price;
    if (q == 1) v += v >> 2;
    else if (q == 2) v += v >> 1;
    return v;
}

void mark_shipped(uint8_t it) BANKED {
    G.shipped[it >> 3] |= 1 << (it & 7);
}

uint8_t spend_energy(uint8_t e) BANKED {
    if (G.energy < e) {
        say("You're too tired to work. Eat something or get some sleep!");
        return 0;
    }
    G.energy -= e;
    if (G.energy < 15 && G.energy + e >= 15) hud_flash("You're getting tired...");
    return 1;
}

uint8_t pay(uint32_t g, uint16_t w, uint16_t s) BANKED {
    if (G.gold < g || G.wood < w || G.stone < s) {
        say("You don't have enough for that yet.");
        return 0;
    }
    G.gold -= g; G.wood -= w; G.stone -= s;
    return 1;
}

void flash_gain(const char *what, uint8_t n) BANKED {
    sfx(SFX_COIN);
    s_clr(); s_ch('+'); s_num(n, 0); s_ch(' '); s_add(what);
    hud_flash(sbuf);
}

void cost_str(uint32_t g, uint16_t w, uint16_t s) BANKED {
    if (g) { s_ch('G'); s_num(g, 0); }
    if (w) { if (g) s_ch(' '); s_num(w, 0); s_ch('W'); }
    if (s) { if (g || w) s_ch(' '); s_num(s, 0); s_ch('S'); }
}
