// Stilwell Farms -- title screen with the save slot (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"

BANKREF(title_screen)


static void peek(uint8_t slot) {
    uint8_t v = save_valid(slot);
    s_clr();
    s_add("Farm: ");
    if (!v) s_add("New");
    else if (v == 2) s_add("Old farm");
    else {
        static const char *const sn[4] = {"Spr", "Sum", "Fal", "Win"};
        s_add(sn[save_info[0] & 3]); s_num(save_info[1], 0); s_add(" Y"); s_num(save_info[2], 0);
    }
    lbl_set(slot);
}

uint8_t title_screen(void) BANKED {
    uint8_t t = 0, r, v;
    G.time = 600; G.weather = 0; G.season = 0;
    G.opt_music = G.opt_sfx = 1;
    enter_scene(MAP_TITLE, 4, 6, D_DOWN);
    hide_sprites();
    HIDE_WIN;
    music_play(MUS_TITLE);
    while (1) {
        read_input();
        if (joy_press & (J_START | J_A)) { sfx(SFX_CONFIRM); break; }
        // blink PRESS START in the window strip
        if ((++t & 31) == 0) { HIDE_WIN; }
        if ((t & 31) == 16) {
            uint8_t x;
            move_win(7, 128);
            for (x = 0; x < 20; x++) ui_char(x, 0, GL_FILL);
            ui_text(4, 0, "PRESS  START");
            SHOW_WIN;
        }
        wait_vbl_done();
    }
    G.rng_lo = DIV_REG | 1;
    G.rng_hi = t;
    while (1) {
        peek(0);
        s_clr(); s_add("Options"); lbl_set(1);
        ui_begin();
        r = ui_menu(1, 9, 18, lp, 2, 0);
        if (r == 1) { options_menu(); ui_end(); continue; }
        if (r == 0xFF) { ui_end(); continue; }
        v = save_valid(r);
        if (v) {
            static const char *const cm[3] = {"Continue", "Start Over", "Back"};
            uint8_t c = ui_menu(8, 4, 12, cm, 3, 0);
            if (c == 0) { ui_end(); fade_out(); return r + 1; }
            if (c == 1) {
                ui_end();
                if (ui_yesno("Erase this farm and start a new one?")) { fade_out(); return r + 3; }
                continue;
            }
            ui_end();
            continue;
        }
        ui_end();
        fade_out();
        return r + 3;
    }
}
