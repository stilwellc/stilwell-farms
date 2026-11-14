// Stilwell Farms -- options (banked)
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(options_menu)

static const char *const o_text[3] = {"Slow", "Normal", "Fast"};
static const char *const o_clock[3] = {"Relaxed", "Normal", "Quick"};
static const char *const o_onoff[2] = {"Off", "On"};
static const char *const o_run[2] = {"Hold B", "Always"};

void options_menu(void) BANKED {
    uint8_t sel = 0, k, t = 0;
    ui_box(0, 0, 20, 14);
    ui_title(2, 0, "OPTIONS");
    ui_text(2, 12, "A/<> change  B back");
    while (1) {
        static const char *const names[5] = {"Text", "Music", "Sound", "Clock", "Run"};
        for (k = 0; k < 5; k++) {
            const char *v = k == 0 ? o_text[G.opt_text] : k == 1 ? o_onoff[G.opt_music] : k == 2 ? o_onoff[G.opt_sfx] :
                            k == 3 ? o_clock[G.opt_clock] : o_run[G.opt_run];
            ui_clear(2, 2 + k * 2, 17, 1);
            ui_text(3, 2 + k * 2, names[k]);
            ui_text(11, 2 + k * 2, v);
            ui_char(2, 2 + k * 2, k == sel ? ((t & 16) ? GL_CURSOR2 : GL_CURSOR) : GL_FILL);
        }
        while (1) {
            read_input();
            if (joy_press) break;
            if ((++t & 15) == 0) ui_char(2, 2 + sel * 2, (t & 16) ? GL_CURSOR2 : GL_CURSOR);
            wait_vbl_done();
        }
        if (joy_press & J_B) { sfx(SFX_CANCEL); return; }
        if (joy_press & J_UP) { sel = sel ? sel - 1 : 4; sfx(SFX_CURSOR); }
        else if (joy_press & J_DOWN) { sel = sel == 4 ? 0 : sel + 1; sfx(SFX_CURSOR); }
        else if (joy_press & (J_A | J_LEFT | J_RIGHT)) {
            switch (sel) {
            case 0: G.opt_text = (G.opt_text + 1) % 3; break;
            case 1: G.opt_music ^= 1; if (G.opt_music) music_for_scene(); else music_stop(); break;
            case 2: G.opt_sfx ^= 1; break;
            case 3: G.opt_clock = (G.opt_clock + 1) % 3; break;
            default: G.opt_run ^= 1; break;
            }
            sfx(SFX_CONFIRM);
        }
    }
}
