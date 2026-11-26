// Stilwell Farms -- core loop, player, camera, sprites, palettes, HUD (bank 0)
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "game.h"
#include "decor.h"
#include "designer.h"

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

static const uint8_t bit8[8] = {1, 2, 4, 8, 16, 32, 64, 128};
uint8_t owned(uint8_t x, uint8_t y) {   // byte + mask lookup (a variable shift is a slow loop)
    return (G.plots[y >> 3] & bit8[x >> 3]) ? 1 : 0;
}

// Cell scanner for the nightly passes (scan_types / scan_add in world.c pick the cell types): SDCC's
// loops over a span cost ~50 cycles a cell (every local on the stack); this one is 15. scan_tab is a
// 32-byte table, 32-aligned (so table + type never carries out of the low byte).
uint8_t *scan_tab;
// the first cell from p (n cells, n > 0 or not) whose type is wanted, else p + n: de = p, a = n
uint8_t *scan_cells(uint8_t *p, uint8_t n) __naked {
    (void)p; (void)n;
    __asm
        or   a, a
        jr   z, 53$
        ld   b, a
        ld   h, d
        ld   l, e
        ld   a, (_scan_tab + 1)
        ld   d, a
        ld   a, (_scan_tab)
        ld   c, a
    50$:
        ld   a, (hl+)
        and  a, #31
        or   a, c
        ld   e, a
        ld   a, (de)
        or   a, a
        jr   nz, 52$
        dec  b
        jr   nz, 50$
        ld   b, h
        ld   c, l
        ret
    52$:
        dec  hl
        ld   b, h
        ld   c, l
        ret
    53$:
        ld   b, d
        ld   c, e
        ret
    __endasm;
}

// a2 lives in its own SRAM bank: map it for one access, then put the live bank (cell/a1) back
uint8_t w_a2(uint16_t i) {
    uint8_t v;
    SWITCH_RAM(WB_A2);
    v = ((volatile uint8_t *)0xA000)[i];
    SWITCH_RAM(WB_LIVE);
    return v;
}
void w_a2_set(uint16_t i, uint8_t v) {
    SWITCH_RAM(WB_A2);
    ((volatile uint8_t *)0xA000)[i] = v;
    SWITCH_RAM(WB_LIVE);
}

uint8_t rnd(void) {   // xorshift16 (7, 9, 8) done bytewise: the 16-bit shifts compiled to slow loops
    uint8_t lo = G.rng_lo, hi = G.rng_hi, t;
    if (!(lo | hi)) { lo = 0xE1; hi = 0xAC; }
    t = (uint8_t)(hi << 7) | (lo >> 1);   // x ^= x << 7
    hi ^= t;
    lo ^= (uint8_t)(lo << 7);
    lo ^= hi >> 1;                        // x ^= x >> 9
    hi ^= lo;                             // x ^= x << 8
    G.rng_lo = lo;
    G.rng_hi = hi;
    return lo ^ hi;
}

// ---------------------------------------------------------------- palettes
uint16_t pbuf[32];
static uint8_t cur_tint = 0xFF, cur_fade, cur_season = 0xFF;

// Tint and fade scale each 5-bit channel by k/16. Per push that is 192 multiplies, which as C
// multiplies (__mulint calls) took longer than a frame and stretched every fade step to 3 frames.
// Instead build 32-entry ramps once per push (tint folded with fade) and look the channels up.
static uint8_t ramp_r[32], ramp_g[32], ramp_b[32], ramp_f[32];
static void ramp(uint8_t *t, uint8_t k) {   // t[v] = (v * k) >> 4, k <= 16: at most one carry per step
    uint8_t v, q = 0, rem = 0;
    for (v = 32; v; v--) {
        *t++ = q;
        rem += k;
        if (rem >= 16) { rem -= 16; q++; }
    }
}
//                                       r   g   b  +b   (tint 0 none, 1 dusk, 2 night, 3 rain, 4 storm, 5 mine lantern, 6 deep mine)
static const uint8_t tint_k[7][4] = {{16, 16, 16, 0}, {15, 12, 9, 0}, {6, 7, 10, 4}, {12, 12, 13, 1}, {9, 9, 11, 2},
                                     {15, 12, 9, 0}, {10, 10, 13, 2}};

static void pal_ramps(uint8_t tint, uint8_t fade) {
    uint8_t v, off = tint_k[tint][3];
    ramp(ramp_f, (uint8_t)(8 - fade) << 1);   // (v * (8 - fade)) >> 3; fade 0 = identity
    ramp(ramp_r, tint_k[tint][0]);
    ramp(ramp_g, tint_k[tint][1]);
    ramp(ramp_b, tint_k[tint][2]);
    for (v = 0; v < 32; v++) {                // tinted value (b + off <= 26), then faded
        ramp_r[v] = ramp_f[ramp_r[v]];
        ramp_g[v] = ramp_f[ramp_g[v]];
        ramp_b[v] = ramp_f[(uint8_t)(ramp_b[v] + off)];
    }
}

static void pal_build(const uint16_t *src, uint8_t cnt, uint8_t ui_at) {   // entries from ui_at on (UI palette): fade only
    uint8_t n;
    const uint8_t *s = (const uint8_t *)src, *tr = ramp_r, *tg = ramp_g, *tb = ramp_b;
    uint8_t *d = (uint8_t *)pbuf;
    for (n = 0; n < cnt; n++) {
        uint8_t lo, hi, r, g, b;
        if (n == ui_at) tr = tg = tb = ramp_f;
        lo = *s++; hi = *s++;                  // 0bbbbbgg gggrrrrr
        r = tr[lo & 31];
        g = tg[(uint8_t)(lo >> 5) | (uint8_t)((hi & 3) << 3)];
        b = tb[(uint8_t)(hi >> 2)];
        *d++ = r | (uint8_t)(g << 5);
        *d++ = (uint8_t)(g >> 3) | (uint8_t)(b << 2);
    }
}

static uint8_t want_tint(void) {
    if (IS_MINE(scene)) return G.mine_floor >= 10 ? 6 : 5;            // lantern light; colder and darker deep down
    if (IS_INTERIOR(scene)) return G.time >= 1050 || G.time < 300;   // lamplight indoors
    if (G.time >= 1200 || G.time < 300) return 2;
    if (G.time >= 1050) return 1;
    if (G.weather == 3) return 4;
    if (G.weather == 1) return 3;
    return 0;
}

// no crops grow in the mine or on the beach, so those scenes recolour the crop palette:
// amethyst + emerald crystals underground, cream sand + palm fronds by the sea. The stone palette is
// matched to the ground too: its darkest colour to the dark cave floor, its lightest to the sand, so
// grey rocks, tide pools and sea glass sit on the ground without a box around them.
static const uint16_t area_pal[16] = {0x7EDB, 0x6972, 0x3B0A, 0x0865,    // mine: crop
                                      0x7FFF, 0x6717, 0x4DEE, 0x0865,    // mine: stone
                                      0x6FDF, 0x19B5, 0x22CB, 0x14E3,    // beach: crop
                                      0x6FDF, 0x6717, 0x4DEE, 0x1CA4};   // beach: stone

static void pal_push(uint8_t tint, uint8_t fade) {
    const uint16_t *src = season_pal[scene == MAP_PINECREST ? SEASON_WINTER : scene == MAP_AMBERWOOD ? SEASON_FALL : (G.season & 3)];   // two places keep one season
    pal_ramps(tint, fade);
    if (scene == SCENE_FARM) { memcpy(pbuf, src, 64); accent_patch(); src = pbuf; }   // farm colours (built in place)
    pal_build(src, 32, PAL_UI * 4);
    set_bkg_palette(0, 8, pbuf);
    if (IS_MINE(scene) || IS_SHORE(scene)) {   // (the other places wear the farm's own seasonal colours)
        pal_build(area_pal + (IS_SHORE(scene) ? 8 : 0), 8, 0xFF);
        set_bkg_palette(PAL_CROP, 1, pbuf);
        set_bkg_palette(PAL_STONE, 1, pbuf + 4);
    }
    pal_build(obj_pal_base, 32, 0xFF);
    set_sprite_palette(0, 8, pbuf);
}

void apply_palettes(void) {
    uint8_t t = want_tint();
    if (t != cur_tint || cur_fade || cur_season != G.season) {
        cur_tint = t; cur_fade = 0; cur_season = G.season;
        pal_push(t, 0);
    }
}

void pal_refresh(void) {   // push again now (the colour picker's live preview)
    cur_tint = 0xFF;
    apply_palettes();
}

void fade_out(void) {
    uint8_t f;
    if (cur_fade == 8) return;   // already black (e.g. the overnight fade before new_morning's enter_scene)
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
    if (scene == SCENE_FARM || IS_INTERIOR(scene) || (IS_NEW(scene) && sc_music == MUS_SPRING)) {   // (a place whose map names no tune of its own keeps the farm's)
        if (G.time >= 1200) want = MUS_NIGHT;
        else want = G.season == 0 ? MUS_SPRING : G.season == 1 ? MUS_SUMMER : G.season == 2 ? MUS_FALL : MUS_WINTER;
    } else if (IS_AREA(scene)) {
        want = sc_music;   // the mine and the beach have their own tunes (map music=)
    } else {
        want = (G.day == 14 || (G.season == SEASON_WINTER && G.day == 25)) ? MUS_FESTIVAL : MUS_TOWN;
    }
    if (music_on && want == music_cur) return;   // same song (farm <-> house): keep playing, no restart
    music_play(want);
}

// ---------------------------------------------------------------- HUD (window row 0)
uint8_t hud_timer, hud_row;
void hud_put(uint8_t x, uint8_t t) { ui_char(x, hud_row, t); }

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
uint16_t ppx, ppy;   // pixel position (horse.c sets it when mounting / dismounting)
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
    if (riding) { ride_draw(sx, sy); return; }   // horse.c / actors.c: the horse and the rider
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
    uint8_t x0, y0, w, h, ok, i, j, n = 0, si = 1, sj = 1;
    ok = build_cursor(&x0, &y0, &w, &h);
    if ((sys_time & 16) || w == 0) { for (i = 22; i < 40; i += 2) hide16(i); return; }
    if (w > 3 || h > 3) { if (w > 1) si = w - 1; if (h > 1) sj = h - 1; }   // a big area (blueprints): corners only
    for (j = 0; j < h; j += sj)
        for (i = 0; i < w && n < 9; i += si, n++)
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
                uint8_t ph = (sys_time >> 2) & 31, l0 = !(G.bld[k].flags & BF_LVL);   // the shack: out of its roof hatch
                put16(4, (int16_t)(G.bld[k].x * 16 + (l0 ? 20 : 30)) - scx + (ph >> 3), (int16_t)(G.bld[k].y * 16 + (l0 ? 7 : -6)) - scy - ph,
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

// The scroll registers are written by the VBL interrupt from these copies, set once a frame's tiles
// and sprites are ready: a mid-frame move_bkg tore the screen and ran ahead of the shadow OAM.
static uint8_t vbl_sx, vbl_sy;
static void scroll_vbl(void) { SCX_REG = vbl_sx; SCY_REG = vbl_sy; }
static void scroll_latch(void) { CRITICAL { vbl_sx = (uint8_t)scx; vbl_sy = (uint8_t)scy; } }

static void camera(void) {
    clamp_cam((int16_t)ppx - 72, (int16_t)ppy - 64);
    view_update();
}

// The LCD is off while a scene loads, and with it the VBlank interrupt that ticks the music: the song
// froze for the whole load (~10 frames). The timer interrupt stands in meanwhile, at the frame rate
// (TAC 4096 Hz input, 8192 Hz at double speed; / 137 = 59.8 Hz).
static void load_music_on(void) {
    TMA_REG = TIMA_REG = 256 - 137;
    TAC_REG = 0x04;
    set_interrupts(VBL_IFLAG | TIM_IFLAG);
}
static void load_music_off(void) {
    set_interrupts(VBL_IFLAG);
    TAC_REG = 0;
}

void enter_scene(uint8_t sc, uint8_t x, uint8_t y, uint8_t facing) {
    fade_out();
    DISPLAY_OFF;
    load_music_on();
    hide_sprites();
    undo_clear();
    scene = sc;
    G.scene = sc;
    if (sc == SCENE_FARM) { view_w = WW; view_h = WH; }
    else { map_select(sc); view_w = sc_w; view_h = sc_h; }
    G.px = x; G.py = y; G.facing = facing;
    if (IS_AREA(sc)) area_enter();   // mine floor rocks / today's beach finds, before anything is drawn
    ppx = G.px * 16; ppy = G.py * 16;
    walk_frame = 0; pose_timer = 0;
    clamp_cam((int16_t)ppx - 72, (int16_t)ppy - 64);
    view_reset();
    scroll_latch();
    move_bkg(scx, scy);
    actors_spawn();
    move_win(7, 136);
    SHOW_BKG; SHOW_WIN; SHOW_SPRITES;
    hud_draw_row(0);
    update_sprites();
    load_music_off();
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
    if (x < 0 || y < 0 || (uint8_t)x >= view_w || (uint8_t)y >= view_h) return 0;
    *ox = x; *oy = y;
    return 1;
}

uint8_t walkable(uint8_t x, uint8_t y) {
    uint16_t i;
    uint8_t c;
    if (x >= view_w || y >= view_h) return 0;
    if (actor_at(x, y) != 0xFF) return 0;
    i = WIDX(x, y);
    if (scene != SCENE_FARM) {
        if (scene == MAP_TOWN && y >= MK_GATE_Y && !(G.mk_flags & MK_OPEN)) return 0;   // Market Street: not bought yet
        return IS_INTERIOR(scene) ? interior_walk(x, y) : IS_AREA(scene) ? area_walk(x, y) : (map_kind(MIDX(x, y)) & K_WALK);
    }
    c = W_CELL[i];
    if (c == C_ROAD) return 1;
    if (!owned(x, y)) return 0;
    if (c == C_DECOR) return decor[W_A1[i]].walk;
    return c == C_GRASS || c == C_GRASS2 || c == C_WILDFLOWER || c == C_DIRT || c == C_SOIL || c == C_SAND;
}

static void check_warp(void) {
    uint8_t k;
    if (scene == SCENE_FARM) {
        if (G.py == WH - 1 && G.facing == D_DOWN && (G.px == ROAD_X || G.px == ROAD_X + 1)) {
            enter_scene(MAP_TOWN, G.px - (ROAD_X - TOWN_ROAD_X), 1, D_DOWN);
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
    if (IS_AREA(scene)) { area_act(); return; }
    if (!front_cell(&x, &y)) return;
    a = actor_at(x, y);
    if (a != 0xFF && actor_npc(a) != 0xFF) { npc_talk(actor_npc(a)); return; }
    for (k = 0; k < sc_nacts; k++)
        if (sc_acts[k].x == x && sc_acts[k].y == y) { town_act(sc_acts[k].act); return; }
    if (scene == MAP_TOWN && y == 7 && (x == 10 || x == 11)) { daily_trader(); return; }   // the west market tent (daily.c)
    if (fest_on) festival_poke(x, y);   // town life: the egg hunt searches flowerbeds, trees, benches...
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
        if (paint_on && !(joy & J_A)) paint_end();
        if (pose_timer) pose_timer--;
        if (moving) {
            uint8_t run = G.opt_run ? !(joy & J_B) : (joy & J_B);
            speed = (run || riding || (G.buff & BUFF_SPEED)) ? 4 : 2;   // on horseback / Brisk dish: twice walking speed
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
                else if (paint_on) paint_step(G.px - dx, G.py - dy);   // build paint stroke: the cell just left
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
                if (building) { build_select(); hud_draw(); }
                else { G.tool = (G.tool + 1) % NUM_TOOLS; tool_toast(); }
            } else if (joy_press & J_A) {
                if (building) build_place();
                else act();
                hud_draw();
                continue;
            } else if ((joy_press & J_B) && building) {
                build_cancel();
                hud_draw();
            } else if ((joy_press & J_B) && riding) {
                horse_dismount();
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
                        if (paint_on) paint_bump();   // the eraser clears what is in the way
                        else if (scene == SCENE_FARM && !building && door_bump(tx, ty)) continue;   // barn.c: in the door
                        else if (joy_press & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) sfx(SFX_BUMP);
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
        scroll_latch();
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
    ENABLE_RAM;
    SWITCH_RAM(WB_LIVE);               // the live farm world stays mapped from here on (see game.h)
    CRITICAL { add_VBL(scroll_vbl); add_VBL(music_tick); add_TIM(music_tick); }
    set_interrupts(VBL_IFLAG);
    enable_interrupts();
    DISPLAY_ON;
    while (1) {
        uint8_t choice = title_screen();   // 1 = continue the saved farm, anything else = a new farm
        if (choice == 1 && save_load(0)) {
            enter_scene(G.scene, G.px, G.py, G.facing);
        } else {
            save_slot = 0;
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
