// Stilwell Farms -- RPG-style text boxes and menus on the window layer (bank 0)
#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"

char sbuf[96];
char lbl[14][19];
const char *lp[14];
void lbl_set(uint8_t k) { uint8_t i = 0; while (sbuf[i] && i < 18) { lbl[k][i] = sbuf[i]; i++; } lbl[k][i] = 0; lp[k] = lbl[k]; }
static char dbuf[96];
static uint8_t slen;
static uint8_t ui_depth;
const char *ui_speaker;
uint8_t *ui_hover;

void s_clr(void) { slen = 0; sbuf[0] = 0; }
uint8_t s_len(void) { return slen; }
void s_pad(uint8_t col) { while (slen < col) s_ch(' '); }
void s_ch(char c) { if (slen < 95) { sbuf[slen++] = c; sbuf[slen] = 0; } }
void s_add(const char *s) { while (*s) s_ch(*s++); }
// digits by subtracting powers of ten: the old v % 10, v /= 10 was two 32-bit library divides per
// digit (~a third of a frame for a 7-digit gold amount on every menu/HUD redraw)
static const uint32_t pow10[9] = {1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL, 10000UL, 1000UL, 100UL, 10UL};
void s_num(uint32_t v, uint8_t width) {
    char tmp[10];
    uint8_t n = 0, k;
    for (k = 0; k < 9; k++) {
        uint8_t d = 0;
        while (v >= pow10[k]) { v -= pow10[k]; d++; }
        if (d || n) tmp[n++] = '0' + d;
    }
    tmp[n++] = '0' + (uint8_t)v;
    while (width > n) { s_ch(' '); width--; }
    for (k = 0; k < n; k++) s_ch(tmp[k]);
}

// straight into the window map (0x9C00): attr + tile in one STAT wait (VRAM stays writable through
// mode 2, and the VBL interrupt returns inside VBlank) instead of two set_win_tile_xy calls
void ui_char(uint8_t x, uint8_t y, uint8_t tile) {
    volatile uint8_t *p = (volatile uint8_t *)(0x9C00 + ((uint16_t)y << 5) + x);
    while (STAT_REG & STATF_BUSY);
    VBK_REG = 1; *p = GL_ATTR;
    VBK_REG = 0; *p = tile;
}

void ui_text(uint8_t x, uint8_t y, const char *s) {
    while (*s && x < 20) {
        uint8_t c = (uint8_t)*s++;
        // 0x80+item = item icon, 0xF0/0xF1 = silver/gold quality star
        ui_char(x++, y, c >= 0xF0 ? (c == 0xF1 ? GL_IT_STAR2 : GL_IT_STAR) :
                        c >= 0x80 ? item_icon_tile[c - 0x80] : (c >= 32 ? font_map[c - 32] : font_map[0]));
    }
}

void ui_clear(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    uint8_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) ui_char(x + i, y + j, GL_FILL);
}

void ui_box(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    uint8_t i, j;
    ui_char(x, y, GL_BOX_TL);
    ui_char(x + w - 1, y, GL_BOX_TR);
    ui_char(x, y + h - 1, GL_BOX_BL);
    ui_char(x + w - 1, y + h - 1, GL_BOX_BR);
    for (i = 1; i < w - 1; i++) {
        ui_char(x + i, y, GL_BOX_T);
        ui_char(x + i, y + h - 1, GL_BOX_B);
    }
    for (j = 1; j < h - 1; j++) {
        ui_char(x, y + j, GL_BOX_L);
        ui_char(x + w - 1, y + j, GL_BOX_R);
        for (i = 1; i < w - 1; i++) ui_char(x + i, y + j, GL_FILL);
    }
}

// title tab in a box's top border:  ┤ TITLE ├  (bank 0: callers in any bank pass literals)
void ui_title(uint8_t x, uint8_t y, const char *t) {
    ui_char(x, y, GL_HDR_L);
    ui_char(x + 1, y, GL_FILL);
    while (*t) { ui_char(x + 2, y, font_map[(uint8_t)*t++ - 32]); x++; }
    ui_char(x + 2, y, GL_FILL);
    ui_char(x + 3, y, GL_HDR_R);
}

// Copy what's on screen into the window so boxes can be drawn over it.
void ui_begin(void) {
    if (ui_depth++) return;
    bg_to_win();
    // HUD row moves from window row 0 to row 17
    if (scene != MAP_TITLE) hud_draw_row(17);
    move_win(7, 0);
    SHOW_WIN;
}

void ui_end(void) {
    if (!ui_depth || --ui_depth) return;
    ui_art_release();
    if (scene == MAP_TITLE) { HIDE_WIN; return; }
    move_win(7, 136);
    hud_draw_row(0);
}

static void wait_ab(uint8_t arrow) {
    uint8_t t = 0;
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B)) { sfx(SFX_CURSOR); break; }
        if (arrow) ui_char(18, 16, (++t & 16) ? GL_MORE : GL_FILL);
        wait_vbl_done();
    }
    if (arrow) ui_char(18, 16, GL_FILL);
}

// Typewriter text in the bottom box with word wrap. '\n' forces a new line.
static const char *const pnames[15] = {"Grandpa", "Alyssa", "Brick", "Andy", "Raza", "Joe", "Marcy", "Corey", "Mike",
                                        "Wren", "Captain", "Isla", "Dusty", "Nora", "Sage"};   // the stagecoach places' keepers
static const mt_t pmts[15] = {ST_PORTRAIT_GRANDPA, ST_PORTRAIT_ROSA, ST_PORTRAIT_BRICK, ST_PORTRAIT_JUNE,
                             ST_PORTRAIT_ADA, ST_PORTRAIT_HOLLIS, ST_PORTRAIT_MILO, ST_PORTRAIT_COREY, ST_PORTRAIT_MIKE,
                             ST_PORTRAIT_WREN, ST_PORTRAIT_CAPTAIN, ST_PORTRAIT_ISLA, ST_PORTRAIT_DUSTY, ST_PORTRAIT_NORA, ST_PORTRAIT_SAGE};

// ui_speaker is read by bank-0 dialog code while whatever bank called say() is mapped, so banked
// callers hand their name over in a RAM copy (a pointer into their own bank read as garbage / hung)
static char spk_buf[14];
void set_speaker(const char *s) {
    uint8_t i = 0;
    while (s[i] && i < 13) { spk_buf[i] = s[i]; i++; }
    spk_buf[i] = 0;
    ui_speaker = spk_buf;
}

static uint8_t streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void dialog_print(const char *s, uint8_t wait_end) {
    uint8_t line = 0, col = 0, len, i, ticks = 0, x0 = 1, tw = 17;   // col 18 stays free for the more-arrow
    const char *w;
    ui_box(0, 12, 20, 6);
    if (ui_speaker) {
        for (i = 0; i < 15; i++)
            if (streq(ui_speaker, pnames[i])) {
                ui_put_struct(1, 13, pmts[i], 2, 2);
                x0 = 6; tw = 12;
                break;
            }
    }
    if (ui_speaker) {  // name tab sitting on the box
        uint8_t n = 0, x;
        while (ui_speaker[n]) n++;
        ui_char(1, 10, GL_BOX_TL);
        for (x = 2; x < n + 4; x++) ui_char(x, 10, GL_BOX_T);
        ui_char(n + 4, 10, GL_BOX_TR);
        ui_char(1, 11, GL_BOX_L);
        ui_char(2, 11, GL_FILL);
        ui_text(3, 11, ui_speaker);
        ui_char(n + 3, 11, GL_FILL);
        ui_char(n + 4, 11, GL_BOX_R);
        ui_char(1, 12, GL_BOX_TJL);
        ui_char(n + 4, 12, GL_BOX_TJR);
    }
    while (*s) {
        if (*s == '\n') {
            s++;
            goto newline;
        }
        w = s;
        len = 0;
        while (w[len] && w[len] != ' ' && w[len] != '\n') len++;
        if (col && col + len > tw) goto newline;
        for (i = 0; i < len; i++) {
            if (col < tw) {
                uint8_t c = (uint8_t)s[i];
                ui_char(x0 + col, 14 + (line << 1), font_map[c - 32]);
                col++;
                if (!(++ticks & 1) && c != ' ') sfx(SFX_TEXT);
            }
            read_input();
            if (!(joy & (J_A | J_B)) && G.opt_text != 2) { wait_vbl_done(); if (!G.opt_text) wait_vbl_done(); }
        }
        s += len;
        if (*s == ' ') {
            s++;
            if (col) col++;
        }
        continue;
    newline:
        col = 0;
        if (++line == 2) {
            wait_ab(1);
            ui_clear(x0, 13, tw, 4);
            line = 0;
        }
    }
    if (wait_end) wait_ab(1);
}

void ui_dialog(const char *s) { dialog_print(s, 1); }

static const char *hold(const char *s) {  // copy so HUD redraws can't clobber sbuf-built text
    uint8_t i = 0;
    while (s[i] && i < 95) { dbuf[i] = s[i]; i++; }
    dbuf[i] = 0;
    return dbuf;
}

void say(const char *s) {
    s = hold(s);
    ui_begin();
    dialog_print(s, 1);
    ui_end();
}

// Menu: returns index or 0xFF when cancelled with B.
uint8_t ui_menu(uint8_t x, uint8_t y, uint8_t w, const char *const *items, uint8_t n, uint8_t sel) {
    uint8_t i, step = (n * 2 + 1 + y <= 17) ? 2 : 1;
    uint8_t h = step == 2 ? n * 2 + 1 : n + 2;
    uint8_t first = step == 2 ? 1 : 1;
    ui_box(x, y, w, h);
    for (i = 0; i < n; i++) ui_text(x + 2, y + first + i * step, items[i]);
    if (sel >= n) sel = 0;
    while (1) {
        uint8_t t = 0;
        for (i = 0; i < n; i++) ui_char(x + 1, y + first + i * step, i == sel ? GL_CURSOR : GL_FILL);
        while (1) {
            read_input();
            if (joy_press) break;
            if ((++t & 15) == 0) ui_char(x + 1, y + first + sel * step, (t & 16) ? GL_CURSOR2 : GL_CURSOR);
            wait_vbl_done();
        }
        if (joy_press & J_UP) { sel = sel ? sel - 1 : n - 1; sfx(SFX_CURSOR); if (ui_hover) { *ui_hover = sel; return 0xFE; } }
        else if (joy_press & J_DOWN) { sel = (sel + 1 == n) ? 0 : sel + 1; sfx(SFX_CURSOR); if (ui_hover) { *ui_hover = sel; return 0xFE; } }
        else if (joy_press & J_A) { sfx(SFX_CONFIRM); return sel; }
        else if (joy_press & J_B) { sfx(SFX_CANCEL); return 0xFF; }
        wait_vbl_done();
    }
}

static const char *const yesno[2] = {"YES", "NO"};

uint8_t ui_yesno(const char *q) {
    uint8_t r;
    q = hold(q);
    ui_begin();
    dialog_print(q, 0);
    r = ui_menu(14, 5, 6, yesno, 2, 0);   // above the name tab (rows 10-12)
    ui_end();
    return r == 0;
}

// Quantity picker: x01  (coin) total. Returns 0 if cancelled.
uint8_t ui_quantity(uint8_t max, uint32_t unit) {
    uint8_t q = 1;
    if (!max) return 0;
    ui_box(7, 9, 13, 3);
    while (1) {
        ui_char(8, 10, GL_TIMES);
        s_clr(); s_num(q, 2);
        ui_text(9, 10, sbuf);
        ui_char(12, 10, GL_COIN);
        s_clr(); s_num(unit * q, 6);
        ui_text(13, 10, sbuf);
        while (1) {
            read_input();
            if (joy_press) break;
            wait_vbl_done();
        }
        if (joy_press & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) sfx(SFX_CURSOR);
        if (joy_press & J_UP) q = q >= max ? 1 : q + 1;
        else if (joy_press & J_DOWN) q = q <= 1 ? max : q - 1;
        else if (joy_press & J_RIGHT) q = (q + 10 > max) ? max : q + 10;
        else if (joy_press & J_LEFT) q = q <= 10 ? 1 : q - 10;
        else if (joy_press & J_A) { sfx(SFX_COIN); return q; }
        else if (joy_press & J_B) { sfx(SFX_CANCEL); return 0; }
    }
}
