// Stilwell Farms -- core loop, player, camera, sprites, palettes, HUD (bank 0)
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "game.h"
#include "decor.h"

GameState G;
uint8_t joy, joy_prev, joy_press;
uint16_t scx, scy;
uint8_t building;
uint8_t scene = SCENE_FARM;

// ---------------------------------------------------------------- input / rng
void read_input(void) {
    joy_prev = joy;
    joy = joypad();
    joy_press = joy & ~joy_prev;
}

void wait_frames(uint8_t n) {
    while (n--) wait_vbl_done();
}

uint8_t rnd(void) {
    uint16_t x = ((uint16_t)G.rng_hi << 8) | G.rng_lo;
    if (!x) x = 0xACE1;
    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    G.rng_lo = (uint8_t)x;
    G.rng_hi = (uint8_t)(x >> 8);
    return (uint8_t)(x ^ (x >> 8));
}

// ---------------------------------------------------------------- palettes
static uint16_t pbuf[32];
static uint8_t cur_tint = 0xFF, cur_fade, cur_season = 0xFF;

static void pal_build(const uint16_t *src, uint8_t tint, uint8_t fade, uint8_t skip_ui) {
    uint8_t i;
    for (i = 0; i < 32; i++) {
        uint16_t c = src[i];
        uint8_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        if (!(skip_ui && (i >> 2) == PAL_UI)) {
            if (tint == 1) { r = (r * 15) >> 4; g = (g * 12) >> 4; b = (b * 9) >> 4; }
            else if (tint == 2) { r = (r * 6) >> 4; g = (g * 7) >> 4; b = ((b * 10) >> 4) + 4; }
            else if (tint == 3) { r = (r * 12) >> 4; g = (g * 12) >> 4; b = ((b * 13) >> 4) + 1; }
            else if (tint == 4) { r = (r * 9) >> 4; g = (g * 9) >> 4; b = ((b * 11) >> 4) + 2; }
        }
        if (fade) { r = (r * (8 - fade)) >> 3; g = (g * (8 - fade)) >> 3; b = (b * (8 - fade)) >> 3; }
        if (b > 31) b = 31;
        pbuf[i] = r | (g << 5) | ((uint16_t)b << 10);
    }
}

static uint8_t want_tint(void) {
    if (IS_INTERIOR(scene)) return G.time >= 1050 || G.time < 300;   // lamplight indoors
    if (G.time >= 1200 || G.time < 300) return 2;
    if (G.time >= 1050) return 1;
    if (G.weather == 3) return 4;
    if (G.weather == 1) return 3;
    return 0;
}

static void pal_push(uint8_t tint, uint8_t fade) {
    pal_build(season_pal[G.season & 3], tint, fade, 1);
    set_bkg_palette(0, 8, pbuf);
    pal_build(obj_pal_base, tint, fade, 0);
    set_sprite_palette(0, 8, pbuf);
}

void apply_palettes(void) {
    uint8_t t = want_tint();
    if (t != cur_tint || cur_fade || cur_season != G.season) {
        cur_tint = t; cur_fade = 0; cur_season = G.season;
        pal_push(t, 0);
    }
}

void fade_out(void) {
    uint8_t f;
    for (f = 1; f <= 8; f++) { pal_push(cur_tint == 0xFF ? 0 : cur_tint, f); wait_frames(2); }
    cur_fade = 8;
}

void fade_in(void) {
    uint8_t f;
    cur_tint = want_tint();
    cur_season = G.season;
    for (f = 8; f; f--) { pal_push(cur_tint, f - 1); wait_frames(2); }
    cur_fade = 0;
}

static void flash(void) {  // lightning
    uint8_t i;
    for (i = 0; i < 32; i++) pbuf[i] = 0x7FFF;
    set_bkg_palette(0, 7, pbuf);
    wait_frames(3);
    cur_tint = 0xFF;
    apply_palettes();
}

// ---------------------------------------------------------------- music
void music_for_scene(void) {
    uint8_t want;
    if (scene == SCENE_FARM || IS_INTERIOR(scene)) {
        if (G.time >= 1200) want = MUS_NIGHT;
        else want = G.season == 0 ? MUS_SPRING : G.season == 1 ? MUS_SUMMER : G.season == 2 ? MUS_FALL : MUS_WINTER;
    } else {
        want = (G.day == 14 || (G.season == SEASON_WINTER && G.day == 25)) ? MUS_FESTIVAL : MUS_TOWN;
    }
    music_play(want);
}

// ---------------------------------------------------------------- HUD (window row 0)
uint8_t hud_timer, hud_row;
void hud_put(uint8_t x, uint8_t t) {
    VBK_REG = 1; set_win_tile_xy(x, hud_row, GL_ATTR);
    VBK_REG = 0; set_win_tile_xy(x, hud_row, t);
}

void hud_draw_row(uint8_t row) {
    hud_row = row;
    hud_timer = 0;
    hud_draw();
}

void hud_flash(const char *s) {
    uint8_t x;
    hud_timer = 0;
    hud_put(0, GL_HUD_SLOT_L);
    for (x = 1; x < 19 && *s; x++) {
        uint8_t c = (uint8_t)*s++;
        hud_put(x, c >= 0xF0 ? (c == 0xF1 ? GL_IT_STAR2 : GL_IT_STAR) : font_map[c - 32]);
    }
    for (; x < 19; x++) hud_put(x, GL_FILL);
    hud_put(19, GL_HUD_SLOT_R);
    hud_timer = 90;
}

// ---------------------------------------------------------------- sprites
// OAM: 0-1 player, 2-3 fx, 4-5 smoke, 6-21 actors, 22-39 build cursor / weather
static uint16_t ppx, ppy;
static uint8_t walk_frame, step_count, pose, pose_timer;
static uint8_t fx_timer, fx_tile, fx_pal, fx_x, fx_y;
static uint8_t wx[6], wy[6];

void put16(uint8_t oam, int16_t x, int16_t y, uint8_t tile, uint8_t pal, uint8_t flip) {
    if (x < -16 || x > 168 || y < -16 || y > 152) {
        move_sprite(oam, 0, 0); move_sprite(oam + 1, 0, 0);
        return;
    }
    if (flip) {
        set_sprite_tile(oam, tile + 2); set_sprite_prop(oam, pal | S_FLIPX);
        set_sprite_tile(oam + 1, tile); set_sprite_prop(oam + 1, pal | S_FLIPX);
    } else {
        set_sprite_tile(oam, tile); set_sprite_prop(oam, pal);
        set_sprite_tile(oam + 1, tile + 2); set_sprite_prop(oam + 1, pal);
    }
    move_sprite(oam, x + 8, y + 16);
    move_sprite(oam + 1, x + 16, y + 16);
}

void hide16(uint8_t oam) { move_sprite(oam, 0, 0); move_sprite(oam + 1, 0, 0); }

void hide_sprites(void) {
    uint8_t i;
    for (i = 0; i < 40; i++) move_sprite(i, 0, 0);
}

void fx_show(uint8_t tile, uint8_t pal, uint8_t x, uint8_t y) {
    fx_tile = tile; fx_pal = pal; fx_x = x; fx_y = y; fx_timer = 16;
}

void player_pose(uint8_t p, uint8_t frames) {
    pose = p; pose_timer = frames;
}

static const uint8_t pose_tiles[3][3] = {
    {SPR_PL_HOE_DOWN, SPR_PL_HOE_UP, SPR_PL_HOE_LEFT},
    {SPR_PL_CAN_DOWN, SPR_PL_CAN_UP, SPR_PL_CAN_LEFT},
    {SPR_PL_FISH_DOWN, SPR_PL_FISH_UP, SPR_PL_FISH_LEFT}};

static void draw_player(void) {
    uint8_t t, flip = 0, d = G.facing;
    int16_t sx = (int16_t)ppx - scx, sy = (int16_t)ppy - scy - 2;
    if (pose_timer && pose) {
        if (pose == 1) { put16(0, sx, sy, SPR_PL_HOLD, SPR_PL_HOLD_PAL, 0); return; }
        t = pose_tiles[pose - 2][d == D_RIGHT ? 2 : d];
        put16(0, sx, sy, t, SPR_PL_DOWN_0_PAL, d == D_RIGHT);
        return;
    }
    switch (d) {
    case D_DOWN: t = walk_frame ? SPR_PL_DOWN_1 : SPR_PL_DOWN_0; flip = (walk_frame == 2); break;
    case D_UP: t = walk_frame ? SPR_PL_UP_1 : SPR_PL_UP_0; flip = (walk_frame == 2); break;
    case D_LEFT: t = walk_frame ? SPR_PL_LEFT_1 : SPR_PL_LEFT_0; break;
    default: t = walk_frame ? SPR_PL_LEFT_1 : SPR_PL_LEFT_0; flip = 1; break;
    }
    put16(0, sx, sy, t, SPR_PL_DOWN_0_PAL, flip);
}

static void draw_cursor(void) {
    uint8_t x0, y0, w, h, ok, i, j, n = 0;
    ok = build_cursor(&x0, &y0, &w, &h);
    if ((sys_time & 16) || w == 0) { for (i = 22; i < 40; i += 2) hide16(i); return; }
    for (j = 0; j < h; j++)
        for (i = 0; i < w && n < 9; i++, n++)
            put16(22 + n * 2, (int16_t)((x0 + i) * 16) - scx, (int16_t)((y0 + j) * 16) - scy, SPR_CURSOR,
                  ok ? SPR_CURSOR_PAL : SPR_DROP_PAL, 0);
    for (; n < 9; n++) hide16(22 + n * 2);
}

static void draw_weather(void) {
    uint8_t i;
    for (i = 0; i < 6; i++) {
        if (scene != SCENE_FARM || G.weather == 0 || G.time >= 1200) { hide16(22 + i * 2); continue; }
        if (G.weather == 1 || G.weather == 3) { wy[i] += 5; wx[i] -= 1; }
        else { wy[i] += 1; if ((sys_time + i) & 8) wx[i]++; else wx[i]--; }
        if (wy[i] > 150) { wy[i] = 0; wx[i] = rnd() % 168; }
        put16(22 + i * 2, wx[i], (int16_t)wy[i] - 8, G.weather == 2 ? SPR_DUST : SPR_RAIN,
              G.weather == 2 ? SPR_DUST_PAL : SPR_RAIN_PAL, 0);
    }
    for (i = 34; i < 40; i += 2) hide16(i);
}

void update_sprites(void) {
    draw_player();
    if (fx_timer) {
        fx_timer--;
        put16(2, (int16_t)(fx_x * 16) - scx, (int16_t)(fx_y * 16) - scy - (16 - fx_timer) / 3, fx_tile, fx_pal, fx_timer & 4);
    } else hide16(2);
    if (scene == SCENE_FARM && G.time < 1260) {  // chimney smoke
        uint8_t k;
        for (k = 0; k < MAX_BLD; k++)
            if (G.bld[k].type == B_HOUSE) {
                uint8_t ph = (sys_time >> 2) & 31;
                put16(4, (int16_t)(G.bld[k].x * 16 + 30) - scx + (ph >> 3), (int16_t)(G.bld[k].y * 16) - scy - 6 - ph,
                      SPR_SMOKE, SPR_SMOKE_PAL, 0);
                break;
            }
    } else hide16(4);
    actors_draw();
    if (building) draw_cursor();
    else draw_weather();
}

// ---------------------------------------------------------------- camera & scenes
static void clamp_cam(int16_t x, int16_t y) {
    int16_t mx = (int16_t)view_w * 16 - 160, my = (int16_t)view_h * 16 - 136;
    if (x > mx) x = mx;
    if (y > my) y = my;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    scx = x; scy = y;
}

static void camera(void) {
    clamp_cam((int16_t)ppx - 72, (int16_t)ppy - 64);
    view_update();
    move_bkg(scx, scy);
}

void enter_scene(uint8_t sc, uint8_t x, uint8_t y, uint8_t facing) {
    fade_out();
    DISPLAY_OFF;
    hide_sprites();
    scene = sc;
    G.scene = sc;
    if (sc == SCENE_FARM) { view_w = WW; view_h = WH; }
    else { map_select(sc); view_w = sc_w; view_h = sc_h; }
    G.px = x; G.py = y; G.facing = facing;
    ppx = G.px * 16; ppy = G.py * 16;
    walk_frame = 0; pose_timer = 0;
    clamp_cam((int16_t)ppx - 72, (int16_t)ppy - 64);
    view_reset();
    move_bkg(scx, scy);
    actors_spawn();
    move_win(7, 136);
    SHOW_BKG; SHOW_WIN; SHOW_SPRITES;
    hud_draw_row(0);
    update_sprites();
    DISPLAY_ON;
    music_for_scene();
    fade_in();
}

void new_morning(void) {
    enter_scene(SCENE_FARM, G.px, G.py, G.facing);
    morning_report();
    hud_draw();
}

uint8_t front_cell(uint8_t *ox, uint8_t *oy) {
    int8_t x = G.px, y = G.py;
    if (G.facing == D_DOWN) y++;
    else if (G.facing == D_UP) y--;
    else if (G.facing == D_LEFT) x--;
    else x++;
    if (x < 0 || y < 0 || x >= view_w || y >= view_h) return 0;
    *ox = x; *oy = y;
    return 1;
}

uint8_t walkable(uint8_t x, uint8_t y) {
    uint16_t i;
    uint8_t c;
    if (x >= view_w || y >= view_h) return 0;
    if (actor_at(x, y) != 0xFF) return 0;
    i = WIDX(x, y);
    if (scene != SCENE_FARM) return IS_INTERIOR(scene) ? interior_walk(x, y) : (map_kind(i) & K_WALK);
    c = G.cell[i];
    if (c == C_ROAD) return 1;
    if (!owned(x, y)) return 0;
    if (c == C_DECOR) return decor[G.a1[i]].walk;
    return c == C_GRASS || c == C_GRASS2 || c == C_WILDFLOWER || c == C_DIRT || c == C_SOIL || c == C_SAND;
}

static void check_warp(void) {
    uint8_t k;
    if (scene == SCENE_FARM) {
        if (G.py == WH - 1 && G.facing == D_DOWN && (G.px == ROAD_X || G.px == ROAD_X + 1)) {
            enter_scene(MAP_TOWN, G.px, 1, D_DOWN);
            if (!(G.tips & 2)) { G.tips |= 2; say("Welcome to town! Walk up to a shop door and press A."); }
        }
        return;
    }
    for (k = 0; k < sc_nwarps; k++)
        if (sc_warps[k].x == G.px && sc_warps[k].y == G.py) {
            Warp w = sc_warps[k];
            if (IS_INTERIOR(scene)) interior_leave(&w);
            enter_scene(w.map == MAP_FARM ? SCENE_FARM : w.map, w.dx, w.dy, w.dir);
            return;
        }
}

static void act(void) {
    uint8_t x, y, k, a;
    if (scene == SCENE_FARM) { farm_act(); return; }
    if (IS_INTERIOR(scene)) { interior_act(); return; }
    if (!front_cell(&x, &y)) return;
    a = actor_at(x, y);
    if (a != 0xFF && actor_npc(a) != 0xFF) { npc_talk(actor_npc(a)); return; }
    for (k = 0; k < sc_nacts; k++)
        if (sc_acts[k].x == x && sc_acts[k].y == y) { town_act(sc_acts[k].act); return; }
}

// ---------------------------------------------------------------- clock
static uint16_t clock_frames;
static uint8_t last_night;

static void tick_clock(void) {
    uint16_t per = G.opt_clock == 0 ? 600 : (G.opt_clock == 1 ? 420 : 260);
    if (++clock_frames < per) return;
    clock_frames = 0;
    G.time += 10;
    apply_palettes();
    hud_draw();
    if ((G.time >= 1200) != last_night) { last_night = G.time >= 1200; music_for_scene(); }
    if (G.time == LAMP_TIME && scene == SCENE_FARM) lamps_refresh();
    if (G.weather == 3 && scene == SCENE_FARM && rnd() < 40) { flash(); sfx(SFX_HIT); }
}

// ---------------------------------------------------------------- field loop
static void field_loop(void) {
    uint8_t moving = 0, turn_wait = 0, speed;
    int8_t dx = 0, dy = 0;
    while (1) {
        read_input();
        if (pose_timer) pose_timer--;
        if (moving) {
            uint8_t run = G.opt_run ? !(joy & J_B) : (joy & J_B);
            speed = run ? 4 : 2;
            if (speed > moving) speed = moving;
            ppx += dx * speed; ppy += dy * speed;
            moving -= speed;
            if (moving <= 8) walk_frame = 0;
            if (!moving) {
                G.px = ppx >> 4; G.py = ppy >> 4;
                walk_frame = 0;
                after_step();
                check_warp();
                if ((joy & J_A) && scene == SCENE_FARM && !building &&
                    (G.tool == T_HOE || G.tool == T_CAN || G.tool == T_SEEDS || G.tool == T_FERT)) farm_act();
            }
        } else if (!pose_timer) {
            uint8_t d = 0xFF;
            if (joy & J_DOWN) d = D_DOWN;
            else if (joy & J_UP) d = D_UP;
            else if (joy & J_LEFT) d = D_LEFT;
            else if (joy & J_RIGHT) d = D_RIGHT;
            if (G.time >= 1560) {
                say("It's way past midnight... You collapse from exhaustion.");
                fade_out();
                world_overnight(1);
                new_morning();
                continue;
            }
            if (joy_press & J_START) {
                start_menu();
                hud_draw();
                continue;
            } else if (joy_press & J_SELECT) {
                sfx(SFX_CURSOR);
                if (building) { building = 0; hud_draw(); }
                else { G.tool = (G.tool + 1) % NUM_TOOLS; tool_toast(); }
            } else if (joy_press & J_A) {
                if (building) build_place();
                else act();
                hud_draw();
                continue;
            } else if ((joy_press & J_B) && building) {
                building = 0;
                hud_draw();
            } else if (d != 0xFF) {
                if (d != G.facing) {
                    G.facing = d;
                    turn_wait = 4;
                } else if (turn_wait) {
                    turn_wait--;
                } else {
                    int8_t tx = G.px, ty = G.py;
                    dx = dy = 0;
                    if (d == D_DOWN) dy = 1;
                    else if (d == D_UP) dy = -1;
                    else if (d == D_LEFT) dx = -1;
                    else dx = 1;
                    tx += dx; ty += dy;
                    if (scene == SCENE_FARM && d == D_DOWN && G.py == WH - 1) { check_warp(); continue; }
                    if (!(tx >= 0 && ty >= 0 && walkable(tx, ty))) {
                        if (joy_press & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) sfx(SFX_BUMP);
                    } else {
                        moving = 16;
                        step_count++;
                        walk_frame = (step_count & 1) ? 1 : 2;
                    }
                }
            } else {
                turn_wait = 0;
            }
        }
        if (hud_timer && !--hud_timer) hud_draw();
        tick_clock();
        actors_tick();
        camera();
        view_tick();
        update_sprites();
        wait_vbl_done();
    }
}

void main(void) {
    DISPLAY_OFF;
    cpu_fast();
    sfx_init();
    SPRITES_8x16;
    art_load();
    HIDE_WIN;
    hide_sprites();
    G.opt_music = G.opt_sfx = 1;
    CRITICAL { add_VBL(music_tick); }
    set_interrupts(VBL_IFLAG);
    enable_interrupts();
    DISPLAY_ON;
    while (1) {
        uint8_t choice = title_screen();   // 0 = new game in slot 0, 1/2 = continue slot 1/2, 3/4 = new in slot 1/2
        if (choice >= 1 && choice <= 2 && save_load(choice - 1)) {
            enter_scene(G.scene, G.px, G.py, G.facing);
        } else {
            save_slot = choice >= 3 ? choice - 3 : 0;
            world_new();
            enter_scene(SCENE_FARM, G.px, G.py, G.facing);
            ui_speaker = "Grandpa";
            say("The old farm is yours now. It's overgrown, but the land is good... and there's plenty more of it for sale.");
            say("Clear it, plant it, build it your way. Make it something special!");
            ui_speaker = 0;
            if (ui_yesno("Would you like to name your farm?")) { s_clr(); s_add("Name your farm"); ui_name(sbuf, G.farm_name, 8); }
            say("START opens your menu, SELECT swaps tools, A uses them. The road south leads to town.");
        }
        last_night = G.time >= 1200;
        field_loop();
    }
}
