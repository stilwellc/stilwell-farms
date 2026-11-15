// Stilwell Farms -- the mine and the beach (banked)
//
// The Mine: up the west road from town. The entrance cave (MAP_MINE) holds the lift and the shaft ladder;
// floors 1..20 reuse three hand-drawn layouts (MAP_MINE_A/B/C), and every arrival scatters fresh rocks,
// ore and gem nodes over their open ground from rnd(). One rock on each floor hides the ladder down.
// Breaking rocks with the hammer costs energy; deeper floors hold iron, gold and rarer gems. Every 5th
// floor has a lift stop, and the lift remembers the deepest stop you've reached (G.mine_lift).
//
// The Beach: east along the south road. Shells, coral, sea glass and driftwood wash up each morning on a
// handful of spots (two of them in the tide pools); the sea has its own fish (fishing.c).
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"

BANKREF(area_enter)

static const uint8_t bit8[8] = {1, 2, 4, 8, 16, 32, 64, 128};

static uint8_t mix(uint8_t v) { v ^= v << 3; v ^= v >> 5; v += 0x9B; v ^= v << 2; return v; }

static void gain(uint8_t it, uint8_t q, uint8_t n) {   // "+2 Iron" (built here: no literal crosses banks)
    inv_add(it, q, n);
    s_clr(); s_ch('+'); s_num(n, 0); s_ch(' '); s_add(items[it].name);
    if (q) s_ch(q == 2 ? (char)0xF1 : (char)0xF0);
    sfx(SFX_COIN);
    hud_flash(sbuf);
    player_pose(1, 24);
}

static uint8_t told;   // first-visit lines shown this session: 1 mine, 2 beach

// ================================================================ the mine
// this visit's floor (not saved: a floor is laid out fresh each time you arrive)
uint8_t mn_rocks[32];            // a bit per cell of the 16x16 floor: mn_rocks[y*2 + x/8] bit x%8
uint8_t mn_seed;                 // decides each rock's kind
uint8_t mn_lad_x, mn_lad_y;         // the ladder down hides under this rock (0xFF: none on this floor)
static uint8_t hit_x, hit_y, hits;   // strikes so far on the rock being broken
#define ROCK_AT(x, y) (mn_rocks[((y) << 1) | ((x) >> 3)] & bit8[(x) & 7])
#define ON_FLOOR() (scene != MAP_MINE && scene != MAP_BEACH)

enum { RK_ROCK, RK_ROCK2, RK_COPPER, RK_IRON, RK_GOLD, RK_COAL, RK_GEM };
static const mt_t rock_mt[7] = {MT_MINE_ROCK, MT_MINE_ROCK2, MT_MINE_COPPER, MT_MINE_IRON, MT_MINE_GOLD, MT_MINE_COAL,
                                MT_MINE_GEMNODE};
static const uint8_t hardness[7] = {1, 1, 2, 3, 4, 2, 3};     // strikes with the basic hammer (each tier takes one off)
// cumulative odds out of 256, by depth (floors 1-5, 6-10, 11-15, 16-20): gem node, gold, iron, copper, coal; else stone
static const uint8_t odds[4][5] = {{4, 4, 14, 54, 74}, {5, 8, 38, 66, 90}, {4, 9, 39, 59, 87}, {4, 10, 38, 56, 86}};
static const uint8_t odds_kind[5] = {RK_GEM, RK_GOLD, RK_IRON, RK_COPPER, RK_COAL};
static const uint8_t gem_item[5] = {I_AMETHYST, I_TOPAZ, I_EMERALD, I_RUBY, I_DIAMOND};

static uint8_t band(void) {
    uint8_t f = G.mine_floor;
    return f <= 5 ? 0 : f <= 10 ? 1 : f <= 15 ? 2 : 3;
}

static uint8_t rock_kind(uint8_t x, uint8_t y) {
    uint8_t h = mix(mix((uint8_t)((x << 4) | y)) ^ mn_seed), k;
    const uint8_t *o = odds[band()];
    for (k = 0; k < 5; k++)
        if (h < o[k]) return odds_kind[k];
    return (h & 1) ? RK_ROCK2 : RK_ROCK;
}

static uint8_t layout(uint8_t f) {   // floors cycle B, C, A, B, ... (1, 2, 3, ...)
    uint8_t r = f % 3;
    return r == 1 ? MAP_MINE_B : r == 2 ? MAP_MINE_C : MAP_MINE_A;
}

static void lay_floor(void) {
    uint8_t x, y, n = 0, k, density;
    mn_seed = rnd();
    density = 56 + (G.mine_floor << 1);   // ~22% of open ground on floor 1, ~37% on floor 20
    for (y = 1; y < 15; y++)
        for (x = 1; x < 15; x++) {
            mt_t m = map_mt(MIDX(x, y));
            if (m != MT_MINE_FLOOR && m != MT_MINE_FLOOR2) continue;
            if ((uint8_t)(x - G.px + 1) < 3 && (uint8_t)(y - G.py + 1) < 3) continue;   // keep the arrival clear
            if (rnd() < density) { mn_rocks[(y << 1) | (x >> 3)] |= bit8[x & 7]; n++; }
        }
    if (G.mine_floor >= MINE_FLOORS || !n) return;   // the bottom floor has no ladder
    k = rnd() % n;
    for (y = 1; y < 15; y++)
        for (x = 1; x < 15; x++)
            if (ROCK_AT(x, y) && !k--) { mn_lad_x = x; mn_lad_y = y; return; }
}

static uint8_t news;   // set by area_enter: 1 = a new lift stop, 2 = first time at the bottom

void area_enter(void) BANKED {
    memset(mn_rocks, 0, sizeof(mn_rocks));
    mn_lad_x = mn_lad_y = hit_x = 0xFF;
    hits = 0;
    news = 0;
    if (scene == MAP_BEACH) {   // a new day: everything washed up again
        uint8_t t = (uint8_t)(G.season * 28 + G.day) ^ (uint8_t)(G.year << 7);
        if (G.beach_stamp != t) { G.beach_stamp = t; G.beach_taken = 0; }
        return;
    }
    if (scene == MAP_MINE) { G.mine_floor = 0; return; }
    if (!G.mine_floor) G.mine_floor = 1;
    lay_floor();
    if (G.mine_floor > G.mine_deep) {
        if (G.mine_floor == MINE_FLOORS) news = 2;
        G.mine_deep = G.mine_floor;
    }
    if (G.mine_floor % 5 == 0 && G.mine_floor > G.mine_lift) { G.mine_lift = G.mine_floor; news |= 1; }
}

// go to floor f (0 = the entrance), arriving beside its lift or its ladder up
static void go_floor(uint8_t f, uint8_t by_lift) {
    uint8_t m = f ? layout(f) : MAP_MINE, x = 4, y = 5, k, want = by_lift ? ACT_LIFT : ACT_LADDERUP;
    map_select(m);   // the act list tells where the lift door / ladder is
    for (k = 0; k < sc_nacts; k++)
        if (sc_acts[k].act == want) { x = sc_acts[k].x; y = sc_acts[k].y + 1; }   // the lift's lower cell comes last
    G.mine_floor = f;
    enter_scene(m, x, y, D_DOWN);
    if (news & 2) say("The very bottom of the mine! The rock down here is thick with gold and gems.");
    if (!f) { hud_flash("Mine entrance"); return; }
    s_clr(); s_add("Floor "); s_num(f, 0);
    if (news & 1) s_add(": lift stop!");
    hud_flash(sbuf);
}

static void ride_lift(void) {
    uint8_t n = 1, k, r, cur = 0;
    if (scene != MAP_MINE && G.mine_floor % 5) {
        say("The cage is shuttered. The lift only stops on every 5th floor.");
        return;
    }
    if (!G.mine_lift) {
        say("The lift goes down to every 5th floor, once you've reached it on foot. Take the ladder for now!");
        return;
    }
    s_clr(); s_add("Entrance"); lbl_set(0);
    for (k = 5; k <= G.mine_lift && n < 5; k += 5) {
        if (k == G.mine_floor) cur = n;
        s_clr(); s_add("Floor "); s_num(k, 2); lbl_set(n++);
    }
    ui_begin();
    ui_title(1, 0, "LIFT");
    r = ui_menu(0, 1, 11, lp, n, cur);
    ui_end();
    if (r == 0xFF || r * 5 == G.mine_floor) return;
    sfx(SFX_CONFIRM);
    go_floor(r * 5, 1);
}

static void strike(uint8_t x, uint8_t y) {
    uint8_t kind = rock_kind(x, y), need = hardness[kind], t = G.tier[TT_HAMMER], n;
    if (G.tool != T_HAMMER) { hud_flash("Use your hammer"); return; }
    if (!spend_energy(2)) return;
    player_pose(2, 8);
    sfx(SFX_HIT);
    fx_show(SPR_DUST, SPR_DUST_PAL, x, y);
    if (x != hit_x || y != hit_y) { hit_x = x; hit_y = y; hits = 0; }
    need = need > t + 1 ? need - t : 1;
    if (++hits < need) return;
    hit_x = 0xFF; hits = 0;
    mn_rocks[(y << 1) | (x >> 3)] &= ~bit8[x & 7];
    redraw_cell(x, y);
    n = 1 + (rnd() & 1);
    switch (kind) {
    case RK_COPPER: gain(I_COPPER, 0, n); break;
    case RK_IRON: gain(I_IRON, 0, n); break;
    case RK_GOLD: gain(I_GOLD, 0, n); break;
    case RK_COAL: gain(I_COAL, 0, n); break;
    case RK_GEM: {
        uint8_t g = (G.mine_floor + 4) >> 2;          // rarest gem this deep: topaz from floor 4 ... diamond from 16
        if (g > 4) g = 4;
        while (g && rnd() < 210) g--;                   // and each rarer one is about a fifth as likely
        music_play(MUS_JINGLE_CATCH);
        gain(gem_item[g], rnd() < 40 ? 1 : 0, 1);
        music_for_scene();
        break;
    }
    default:
        G.stone += n;
        if (rnd() < 24) gain(I_COAL, 0, 1);
        else if (rnd() < 2 + band()) gain(I_AMETHYST, 0, 1);
        else { sfx(SFX_COIN); s_clr(); s_ch('+'); s_num(n, 0); s_add(" Stone"); hud_flash(sbuf); }
        break;
    }
    if (x == mn_lad_x && y == mn_lad_y) {
        sfx(SFX_CONFIRM);
        hud_flash("A ladder down!");
    }
}

static void mine_sign(void) {
    set_speaker("Notice");
    say("STILWELL MINE. Twenty floors deep. Break mn_rocks with your hammer: one on every floor hides the ladder down.");
    say("Copper near the top, iron and gold further down, and gems all the way. The lift stops every 5th floor.");
    ui_speaker = 0;
}

static void mine_step(void) {
    sfx(SFX_STEP_GRAVEL);
    if (scene == MAP_MINE) {
        if (!(told & 1)) {
            told |= 1;
            if (!G.mine_deep) say("The old Stilwell mine. Somewhere below, the rock is full of ore and gems.");
        }
        if (map_mt(MIDX(G.px, G.py)) == MT_MINE_LADDER) go_floor(1, 0);
        return;
    }
    if (G.px == mn_lad_x && G.py == mn_lad_y && G.mine_floor < MINE_FLOORS) go_floor(G.mine_floor + 1, 0);
}

// ================================================================ the beach
// spots where things wash up: six on the sand, then the two tide pools (map acts)
#define NSPOT 8
static const uint8_t spot_x[NSPOT] = {4, 8, 12, 17, 21, 3, 18, 20};
static const uint8_t spot_y[NSPOT] = {4, 7, 8, 4, 5, 8, 7, 8};

static uint8_t spot_item(uint8_t k) {   // what lies on spot k right now, 0xFF nothing
    uint8_t h;
    if (G.beach_taken & bit8[k]) return 0xFF;
    h = mix(mix((uint8_t)(G.beach_stamp + k * 37)) ^ (uint8_t)(k << 5));
    if (k < 6 && !(h & 3)) return 0xFF;            // about one sand spot in four is bare today
    h >>= 2;                                       // 0..63
    if (k >= 6) return h < 24 ? I_CORAL : h < 44 ? I_SHELL : I_SEAGLASS;
    return h < 26 ? I_SHELL : h < 40 ? I_SEAGLASS : h < 52 ? I_DRIFTWOOD : I_CORAL;
}

static uint8_t spot_at(uint8_t x, uint8_t y) {
    uint8_t k;
    if (y < 4 || y > 8) return 0xFF;
    for (k = 0; k < NSPOT; k++)
        if (spot_x[k] == x && spot_y[k] == y) return k;
    return 0xFF;
}

uint8_t beach_finds(void) BANKED {
    uint8_t k, n = 0;
    for (k = 0; k < NSPOT; k++) if (spot_item(k) != 0xFF) n++;
    return n;
}

static mt_t find_mt(uint8_t it) {
    switch (it) {
    case I_SHELL: return MT_BEACH_SHELL;
    case I_CORAL: return MT_BEACH_CORAL;
    case I_SEAGLASS: return MT_BEACH_GLASS;
    default: return MT_BEACH_DRIFTWOOD;
    }
}

static void pick(uint8_t k, uint8_t x, uint8_t y) {
    uint8_t it = spot_item(k);
    if (it == 0xFF) { if (k >= 6) hud_flash("Just little crabs today"); return; }
    G.beach_taken |= bit8[k];
    redraw_cell(x, y);
    gain(it, rnd() < 36 ? 1 : 0, 1);
}

static void tide_board(void) {
    static const char *const by_day[4] = {"Flounder bite by day.", "Flounder and snapper by day, and the tuna run!",
                                          "Snapper bite by day.", "Tuna run in the cold water!"};
    set_speaker("Tide Board");
    s_clr(); s_add("TODAY: "); s_add(by_day[G.season & 3]);
    s_add(" Sardines all day. Squid come out after 6pm.");
    say(sbuf);
    say("Fish from the shore or the end of the pier. Shells wash up on the sand every morning.");
    ui_speaker = 0;
}

static void beach_act(uint8_t x, uint8_t y) {
    uint8_t k = spot_at(x, y);
    if (k != 0xFF && k < 6 && spot_item(k) != 0xFF) { pick(k, x, y); return; }
    if (map_kind(MIDX(x, y)) & K_WATER) {
        if (G.tool == T_ROD) fish_start();
        else hud_flash("Face the sea to fish");
    }
}

// ================================================================ shared hooks
mt_t area_mt(uint8_t x, uint8_t y) BANKED {
    mt_t m;
    if (scene == MAP_BEACH) {
        uint8_t k = spot_at(x, y);
        if (k != 0xFF && spot_item(k) != 0xFF) return k < 6 ? find_mt(spot_item(k)) : MT_BEACH_TIDEPOOL_FULL;
        return map_mt(MIDX(x, y));
    }
    if (ON_FLOOR() && x < 16 && y < 16) {
        if (ROCK_AT(x, y)) return rock_mt[rock_kind(x, y)];
        if (x == mn_lad_x && y == mn_lad_y) return MT_MINE_LADDER;
    }
    m = map_mt(MIDX(x, y));
    if (m == ST_MINE_LIFT + 1 && ON_FLOOR() && G.mine_floor % 5) return MT_MINE_LIFT_OFF;
    return m;
}

uint8_t area_walk(uint8_t x, uint8_t y) BANKED {
    if (scene == MAP_BEACH) {
        uint8_t k = spot_at(x, y);
        if (k != 0xFF && k < 6 && spot_item(k) != 0xFF) return 0;
    } else if (ON_FLOOR() && x < 16 && y < 16 && ROCK_AT(x, y)) return 0;
    return map_kind(MIDX(x, y)) & K_WALK;
}

void area_act(void) BANKED {
    uint8_t x, y, k, a;
    if (!front_cell(&x, &y)) return;
    a = actor_at(x, y);
    if (a != 0xFF && actor_npc(a) != 0xFF) { npc_talk(actor_npc(a)); return; }
    for (k = 0; k < sc_nacts; k++)
        if (sc_acts[k].x == x && sc_acts[k].y == y) {
            switch (sc_acts[k].act) {
            case ACT_LIFT: ride_lift(); break;
            case ACT_MINESIGN: mine_sign(); break;
            case ACT_LADDERUP:
                if (ui_yesno("Climb back up to the mine entrance?")) go_floor(0, 0);
                break;
            case ACT_SHACK: tide_board(); break;
            case ACT_TIDEPOOL: pick(spot_at(x, y), x, y); break;
            }
            return;
        }
    if (scene == MAP_BEACH) { beach_act(x, y); return; }
    if (ON_FLOOR() && x < 16 && y < 16 && ROCK_AT(x, y)) { strike(x, y); return; }
    if (map_kind(MIDX(x, y)) & K_WATER) hud_flash("Cold, still water");
}

void area_step(void) BANKED {
    if (scene != MAP_BEACH) { mine_step(); return; }
    {
        mt_t m = map_mt(MIDX(G.px, G.py));
        sfx(m == MT_BEACH_PIER || m == MT_BEACH_PIER_END ? SFX_STEP_WOOD : SFX_STEP_SAND);
    }
    if (!(told & 2)) {
        told |= 2;
        say("Salt air and soft sand! Things wash up here every morning, and the sea has fish of its own.");
    }
}

// ---------------------------------------------------------------- START > map, in the mine or on the beach
// the minimap tiles: 0 grass 1 water 2 trees 3 crops 4 building 5 rock 6 path; 8 = blank (cave rock, off the map)
static uint8_t cell_class(uint8_t x, uint8_t y) {
    mt_t m;
    uint8_t kd;
    if (x >= sc_w || y >= sc_h) return 8;
    m = area_mt(x, y);
    kd = map_kind(MIDX(x, y));
    if (scene == MAP_BEACH) {
        if (m == MT_FOREST || m == ST_BEACH_PALM || m == ST_BEACH_PALM + 1) return 2;
        if (kd & K_WATER) return 1;
        if (m == MT_BEACH_DUNE) return 0;
        if (m == MT_BEACH_PIER || m == MT_BEACH_PIER_END || m == MT_BEACH_UMBRELLA || (m >= ST_BEACH_SHACK && m < ST_BEACH_SHACK + 4)) return 4;
        if (kd & K_WALK) return 6;
        return 5;
    }
    if (m == MT_MINE_LADDER || m == MT_MINE_LADDER_UP || m == ST_MINE_LIFT || m == ST_MINE_LIFT + 1 || m == MT_MINE_LIFT_OFF) return 4;
    if (kd & K_WATER) return 1;
    if (ON_FLOOR() && x < 16 && y < 16 && ROCK_AT(x, y)) return 5;
    return (kd & K_WALK) ? 6 : 8;
}

static void mini(uint8_t tx, uint8_t ty, uint8_t k) {
    if (k == 8) { ui_char(tx, ty, GL_FILL); return; }
    ui_put_quad(tx, ty, k < 4 ? MT_MINIMAP_A : MT_MINIMAP_B, k & 3);
}

void area_map(void) BANKED {
    static const uint8_t prio[8] = {0, 5, 3, 4, 6, 2, 1, 7};
    uint8_t x, y, sh = sc_w > 18 ? 1 : 0, tw, th, ox, oy, t = 0, i, j, best, c, yx, yy;
    hide_sprites();
    for (y = 0; y < 17; y++)
        for (x = 0; x < 20; x++) ui_char(x, y, GL_FILL);
    tw = (sc_w + sh) >> sh; th = (sc_h + sh) >> sh;
    ox = (20 - tw) >> 1; oy = (16 - th) >> 1;
    for (y = 0; y < th; y++)
        for (x = 0; x < tw; x++) {
            best = cell_class(x << sh, y << sh);
            if (sh)
                for (j = 0; j < 2; j++)
                    for (i = 0; i < 2; i++) {
                        c = cell_class((x << 1) + i, (y << 1) + j);
                        if (c != 8 && (best == 8 || prio[c] > prio[best])) best = c;
                    }
            mini(ox + x, oy + y, best);
        }
    s_clr();
    if (scene == MAP_BEACH) { s_add("The Beach: "); s_num(beach_finds(), 0); s_add(" finds"); }
    else if (scene == MAP_MINE) { s_add("Mine entrance  Lift "); s_num(G.mine_lift, 0); }
    else { s_add("Mine floor "); s_num(G.mine_floor, 0); s_add(" of "); s_num(MINE_FLOORS, 0); }
    ui_text(1, 16, sbuf);
    yx = ox + (G.px >> sh); yy = oy + (G.py >> sh);
    while (1) {
        if (t & 16) ui_char(yx, yy, GL_MAP_YOU);
        else {
            best = cell_class(G.px & (sh ? 0xFE : 0xFF), G.py & (sh ? 0xFE : 0xFF));
            mini(yx, yy, best);
        }
        read_input();
        t++;
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CANCEL); return; }
        wait_vbl_done();
    }
}
