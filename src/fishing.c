// Stilwell Farms -- fishing: cast, wait for a bite, reel it in (banked)
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(fish_start)

extern void put16(uint8_t oam, int16_t x, int16_t y, uint8_t tile, uint8_t pal, uint8_t flip);
extern void hide16(uint8_t oam);

// which fish: by water body, season, time, rod tier
static uint8_t pick_fish(uint8_t x, uint8_t y) {
    uint8_t p = PLOT_OF(x, y), r = rnd(), tier = G.tier[TT_ROD];
    if (r < 3 + tier * 3) return I_KOI;
    if (p == 11) return (r & 1) ? I_TROUT : I_MINNOW;                       // river
    if (p == 8) return G.time >= 1080 ? I_CATFISH : ((r & 1) ? I_BASS : I_CARP);  // lake
    if (p == 12) return (r & 1) ? I_CARP : I_CATFISH;                       // marsh
    if (G.season == SEASON_SUMMER && (r & 3) == 0) return I_BASS;
    return (r < 160) ? I_MINNOW : I_CARP;                                   // your own pond
}

static uint8_t difficulty(uint8_t f) {
    switch (f) {
    case I_KOI: return 4;
    case I_CATFISH: case I_TROUT: return 3;
    case I_BASS: return 2;
    case I_CARP: return 1;
    default: return 0;
    }
}

static void bar(uint8_t zone, uint8_t fish, uint8_t prog) {
    uint8_t i;
    for (i = 0; i < 16; i++) {
        uint8_t t = (i >= zone && i < zone + 4) ? GL_BAR_FULL : GL_BAR_EMPTY;
        ui_char(2 + i, 14, t);
        ui_char(2 + i, 13, i == fish ? GL_IT_FISH : GL_FILL);
    }
    for (i = 0; i < 16; i++) ui_char(2 + i, 16, i < (prog >> 4) ? GL_HEART : GL_FILL);
}

void fish_start(void) BANKED {
    uint8_t x, y, wait, t, f, d, zone = 6, prog = 60, fishpos = 8, target = 8, ok = 0;
    int8_t vel = 0;
    if (!front_cell(&x, &y)) return;
    if (!spend_energy(4)) return;
    player_pose(4, 255);
    sfx(SFX_SPLASH);
    hud_flash("Wait for a bite...");
    wait = 60 + (rnd() & 127);
    for (t = 0; t < wait; t++) {
        put16(2, (int16_t)(x * 16) - scx, (int16_t)(y * 16) - scy + ((t >> 4) & 1), SPR_BOBBER, SPR_BOBBER_PAL, 0);
        read_input();
        if (joy_press & J_B) { hide16(2); player_pose(0, 0); hud_flash("Reeled in early."); return; }
        wait_vbl_done();
    }
    // bite! react within half a second
    sfx(SFX_CONFIRM);
    put16(2, (int16_t)(G.px * 16) - scx, (int16_t)(G.py * 16) - scy - 18, SPR_EXCLAIM, SPR_EXCLAIM_PAL, 0);
    for (t = 0; t < 30; t++) {
        read_input();
        if (joy_press & J_A) { ok = 1; break; }
        wait_vbl_done();
    }
    hide16(2);
    if (!ok) { player_pose(0, 0); hud_flash("It got away..."); return; }
    f = pick_fish(x, y);
    d = difficulty(f);
    ui_begin();
    ui_box(0, 11, 20, 7);
    ui_text(2, 12, "Hold A to reel!");
    // reel: keep the zone under the fish to fill the meter
    while (prog > 0 && prog < 255) {
        read_input();
        if (joy & J_A) { if (vel < 3) vel++; } else { if (vel > -3) vel--; }
        if (!(sys_time & 3)) {
            if (vel > 0 && zone < 12) zone++;
            if (vel < 0 && zone > 0) zone--;
        }
        if ((rnd() & 63) < 3 + d * 3) target = rnd() & 15;
        if (!(sys_time & (d >= 3 ? 3 : 7))) { if (fishpos < target) fishpos++; else if (fishpos > target) fishpos--; }
        if (fishpos >= zone && fishpos < zone + 4) prog = prog > 252 - 3 ? 255 : prog + 3;
        else prog = prog < 2 + d ? 0 : prog - 2 - d;
        if (!(sys_time & 1)) bar(zone, fishpos, prog);
        wait_vbl_done();
    }
    ui_end();
    player_pose(0, 0);
    if (!prog) { sfx(SFX_CANCEL); hud_flash("It slipped away!"); return; }
    music_play(MUS_JINGLE_CATCH);
    inv_add(f, d >= 3 && (rnd() & 1) ? 1 : 0, 1);
    G.stat_fish++;
    player_pose(1, 40);
    s_clr(); s_add("You caught a "); s_add(items[f].name); s_ch('!');
    say(sbuf);
    music_for_scene();
    goals_check();
}
