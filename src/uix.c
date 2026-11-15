// Stilwell Farms -- name entry keyboard and item picker (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"

BANKREF(ui_name)

static const char keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -'!";
#define NKEYS 66
#define KCOLS 9

static void draw_name(const char *buf, uint8_t maxlen) {
    uint8_t i, end = 0;
    for (i = 0; i < maxlen; i++) {   // nothing past the terminator: tmp[] beyond it is stale stack
        if (!buf[i]) end = 1;
        ui_char(5 + i, 3, end ? font_map['-' - 32] : font_map[buf[i] - 32]);
    }
}

uint8_t ui_name(const char *prompt, char *buf, uint8_t maxlen) BANKED {
    uint8_t sel = 0, len = strlen(buf), k, t = 0;
    char tmp[10];
    strcpy(tmp, buf);
    ui_begin();
    ui_box(0, 0, 20, 18);   // full height: covers the HUD row (17) while the keyboard is up
    ui_text(2, 1, prompt);
    for (k = 0; k < NKEYS + 2; k++) {
        uint8_t x = 2 + (k % KCOLS) * 2, y = 6 + k / KCOLS;
        if (k < NKEYS) ui_char(x, y, font_map[keys[k] - 32]);
        else ui_char(x, y, k == NKEYS ? GL_KEY_DEL : GL_KEY_END);
    }
    while (1) {
        draw_name(tmp, maxlen);
        for (k = 0; k < NKEYS + 2; k++) ui_char(1 + (k % KCOLS) * 2, 6 + k / KCOLS, k == sel ? ((t & 16) ? GL_CURSOR2 : GL_CURSOR) : GL_FILL);
        read_input();
        t++;
        if (joy_press & J_RIGHT) { sel = (sel + 1) % (NKEYS + 2); sfx(SFX_CURSOR); }
        else if (joy_press & J_LEFT) { sel = sel ? sel - 1 : NKEYS + 1; sfx(SFX_CURSOR); }
        else if (joy_press & J_DOWN) { sel = (sel + KCOLS < NKEYS + 2) ? sel + KCOLS : sel % KCOLS; sfx(SFX_CURSOR); }
        else if (joy_press & J_UP) { sel = sel >= KCOLS ? sel - KCOLS : sel; sfx(SFX_CURSOR); }
        else if (joy_press & J_B) { if (len) { tmp[--len] = 0; sfx(SFX_CANCEL); } }
        else if (joy_press & (J_A | J_START)) {
            if (sel == NKEYS + 1 || (joy_press & J_START)) {
                if (len) { strcpy(buf, tmp); sfx(SFX_CONFIRM); break; }
            } else if (sel == NKEYS) { if (len) { tmp[--len] = 0; sfx(SFX_CANCEL); } }
            else if (len < maxlen) { tmp[len++] = keys[sel]; tmp[len] = 0; sfx(SFX_CURSOR); if (len == maxlen) sel = NKEYS + 1; }
        }
        wait_vbl_done();
    }
    ui_end();
    return 1;
}


static uint8_t pick_ok(uint8_t filter, uint8_t it) {
    if (filter == PICK_PRODUCE) return it <= I_CHERRY;
    if (filter == PICK_MILK) return it == I_MILK;
    return 1;
}

uint8_t ui_pick_item(uint8_t filter) BANKED {
    uint8_t it, n = 0, map[12], r;
    for (it = 0; it < NUM_ITEMS && n < 12; it++) {
        if (!inv_count(it) || !pick_ok(filter, it)) continue;
        s_clr(); s_ch((char)(0x80 + it)); s_ch(' '); s_add(items[it].name); s_pad(14); s_ch('x'); s_num(inv_count(it), 2);
        lbl_set(n);
        map[n++] = it;
    }
    if (!n) { say("You don't have anything that fits."); return 0xFF; }
    ui_begin();
    r = ui_menu(0, 0, 20, lp, n, 0);
    ui_end();
    return r == 0xFF ? 0xFF : map[r];
}

// lighter sub-panel frame
void ui_panel(uint8_t x, uint8_t y, uint8_t w, uint8_t h) BANKED {
    uint8_t i, j;
    ui_char(x, y, GL_PANEL_TL); ui_char(x + w - 1, y, GL_PANEL_TR);
    ui_char(x, y + h - 1, GL_PANEL_BL); ui_char(x + w - 1, y + h - 1, GL_PANEL_BR);
    for (i = 1; i < w - 1; i++) { ui_char(x + i, y, GL_PANEL_T); ui_char(x + i, y + h - 1, GL_PANEL_B); }
    for (j = 1; j < h - 1; j++) {
        ui_char(x, y + j, GL_PANEL_L); ui_char(x + w - 1, y + j, GL_PANEL_R);
        for (i = 1; i < w - 1; i++) ui_char(x + i, y + j, GL_FILL);
    }
}

// the shop money box, top right: coin + 4 columns inside the border, like the HUD (9999, then 10k..999k, and
// for a millionaire 1000k..9999k over the coin); x 13..19 leaves 13 columns for the shop menus beside it
void ui_money(void) BANKED {
    uint32_t g = G.gold;
    ui_box(13, 0, 7, 3);
    ui_char(14, 1, GL_COIN);
    s_clr();
    if (g < 10000) s_num(g, 4);
    else { g /= 1000; s_num(g > 9999 ? 9999 : g, 3); s_ch('k'); }
    ui_text(s_len() > 4 ? 14 : 15, 1, sbuf);
}

