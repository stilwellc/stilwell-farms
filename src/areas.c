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
#define ON_FLOOR() (scene != MAP_MINE && !IS_OPEN(scene))

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
    if (IS_NEW(scene)) {   // a new day: everything has grown back, everywhere
        uint8_t t = (uint8_t)(G.season * 28 + G.day) ^ (uint8_t)(G.year << 6) ^ 0x5A;
        if (G.area_stamp != t) { G.area_stamp = t; memset(G.area_taken, 0, sizeof(G.area_taken)); }
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
    if (x == BOTTLE_X && y == BOTTLE_Y && BOTTLE_HERE) { daily_bottle(); redraw_cell(x, y); return; }
    if (k != 0xFF && k < 6 && spot_item(k) != 0xFF) { pick(k, x, y); return; }
    if (map_kind(MIDX(x, y)) & K_WATER) {
        if (G.tool == T_ROD) fish_start();
        else if (G.px == 15 && G.py == 15) {   // the end of the pier: the rowing boat
            if (ui_yesno("A rowing boat is tied up here. Row out to Shell Island?")) enter_scene(MAP_ISLAND, 11, 13, D_UP);
        } else hud_flash("Face the sea to fish");
    }
}

// ================================================================ places to visit
// Shell Island, the Crossroads Market, the High Pasture, the Granite Quarry, the Old Woods: static maps with up
// to seven places where something comes back each morning, and one daily find each (bit 7 of area_taken).
#define AI(s) ((uint8_t)((s) - MAP_ISLAND))     // 0 island, 1 market, 2 pasture, 3 quarry, 4 woods
#define TAKEN G.area_taken[AI(scene)]
static const uint8_t sp_n[5] = {7, 0, 6, 7, 7};
static const uint8_t sp_x[5][7] = {{5, 9, 14, 17, 7, 12, 19}, {0}, {4, 9, 16, 20, 3, 18}, {3, 8, 12, 5, 13, 4, 9}, {4, 8, 15, 20, 3, 12, 7}};
static const uint8_t sp_y[5][7] = {{5, 4, 7, 9, 9, 6, 7}, {0}, {2, 7, 8, 2, 9, 11}, {2, 3, 2, 5, 6, 8, 6}, {3, 2, 2, 10, 12, 13, 15}};
static const uint8_t wd_x[7] = {4, 8, 15, 20, 3, 12, 7};      // (the woods' row again: the test suite reads it)
static const uint8_t wd_y[7] = {3, 2, 2, 10, 12, 13, 15};

static uint8_t place_item(uint8_t k) {   // what is at place k of this map today, 0xFF nothing
    uint8_t h, a = AI(scene), bare = 1;
    if (TAKEN & bit8[k]) return 0xFF;
    h = mix(mix((uint8_t)(G.area_stamp + k * 53 + a * 91)) ^ (uint8_t)(k << 4));
    if (scene == MAP_QUARRY && k == 6) { h >>= 2; return h < 40 ? I_GEM : h < 56 ? I_TOPAZ : I_EMERALD; }   // the gem seam
    if (G.season == SEASON_WINTER && (scene == MAP_WOODS || scene == MAP_PASTURE)) bare = 2;                 // half under snow
    if ((h & 3) < bare) return 0xFF;
    h >>= 2;                                       // 0..63
    switch (scene) {
    case MAP_ISLAND: return h < 22 ? I_SHELL : h < 38 ? I_SEAGLASS : h < 50 ? I_DRIFTWOOD : I_CORAL;
    case MAP_QUARRY: return h < 26 ? I_COPPER : h < 44 ? I_IRON : h < 56 ? I_COAL : I_GOLD;
    case MAP_PASTURE: return h < 34 ? I_HERB : h < 50 ? I_MUSHROOM : I_BERRIES;
    }
    if (G.season == SEASON_FALL) return h < 36 ? I_MUSHROOM : h < 52 ? I_BERRIES : I_HERB;
    if (G.season == SEASON_WINTER) return h < 40 ? I_MUSHROOM : I_HERB;
    return h < 26 ? I_BERRIES : h < 46 ? I_HERB : I_MUSHROOM;
}

static uint8_t place_at(uint8_t x, uint8_t y) {
    uint8_t k, a = AI(scene);
    for (k = 0; k < sp_n[a]; k++)
        if (sp_x[a][k] == x && sp_y[a][k] == y) return k;
    return 0xFF;
}

static mt_t place_mt(uint8_t it) {
    switch (it) {
    case I_BERRIES: return MT_BERRY_BUSH;
    case I_MUSHROOM: return MT_MUSHROOM;
    case I_HERB: return MT_WILD_FLOWERS;
    case I_COPPER: return MT_ORE_COPPER;
    case I_IRON: return MT_ORE_IRON;
    case I_GOLD: return MT_ORE_GOLD;
    case I_COAL: return MT_ROCK;
    case I_GEM: case I_TOPAZ: case I_EMERALD: return MT_GEM_ROCK;
    }
    return find_mt(it);
}

uint8_t woods_finds(void) BANKED {   // (any of the five: how many places still hold something)
    uint8_t k, n = 0;
    for (k = 0; k < sp_n[AI(scene)]; k++) if (place_item(k) != 0xFF) n++;
    return n;
}

static void rare_seeds(uint8_t h) {   // 3 rare seeds of a crop of this season
    static const uint8_t here[4][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {10, 11, 12, 13}, {14, 14, 3, 7}};
    uint8_t c = here[G.season & 3][h & 3];
    G.seeds[c][R_RARE] = G.seeds[c][R_RARE] > 252 ? 255 : G.seeds[c][R_RARE] + 3;
    sfx(SFX_COIN);
    s_clr(); s_add("A little cache of seeds! 3 rare "); s_add(items[c - 1].name); s_add(" seeds.");
    say(sbuf);
}

static void hollow_log(void) {
    uint8_t h = mix(G.area_stamp ^ 0xC3);
    if (TAKEN & 0x80) { say("Nothing more in the hollow log today. Something new turns up most mornings."); return; }
    TAKEN |= 0x80;
    say("You reach into the hollow log...");
    if ((h & 7) == 0) { say("Just beetles today. They seem busy."); return; }
    if (h & 0x40) { rare_seeds(h); return; }
    gain((h & 0x30) == 0 ? I_GEM : (h & 0x10) ? I_HONEY : I_HERB, (h & 8) ? 1 : 0, 1);
}

static void island_dig(void) {
    uint8_t h = mix(G.area_stamp ^ 0x6D);
    if (TAKEN & 0x80) { say("The sand under the umbrella is dug over. The tide brings more tomorrow."); return; }
    TAKEN |= 0x80;
    say("Somebody's old umbrella. The sand under it looks freshly turned...");
    if ((h & 3) == 0) { rare_seeds(h >> 2); return; }
    gain((h & 0x30) == 0 ? I_RUBY : (h & 0x10) ? I_CORAL : I_TOPAZ, 0, 1);
}

// ---------------------------------------------------------------- animals that come home from away
static uint8_t take_home(uint8_t kind, uint8_t breed) {   // an animal from away, if a barn / coop has room: 1 = it is yours
    static const char *const nm[3] = {"Heath", "Fern", "Moss"};
    uint8_t b, k, n, slot;
    for (slot = 0; slot < MAX_ANIMALS && G.anim[slot].kind != 0xFF; slot++) ;
    if (slot == MAX_ANIMALS) return 0;
    for (b = 0; b < MAX_BLD; b++) {
        if (G.bld[b].type != ((kind % 3) == A_HEN ? B_COOP : B_BARN) || (G.bld[b].flags & BF_RUIN)) continue;
        for (n = 0, k = 0; k < MAX_ANIMALS; k++) if (G.anim[k].kind != 0xFF && G.anim[k].home == b) n++;
        if (n >= 6) continue;
        G.anim[slot].kind = kind; G.anim[slot].home = b; G.anim[slot].happy = 140; G.anim[slot].age = ADULT_AGE;
        G.anim[slot].flags = breed << 6;
        G.anim[slot].trait = rnd() & TR_TEMPER; G.anim[slot].gen = 0;
        strcpy(G.anim[slot].name, nm[kind % 3]);
        herd_mark(slot);
        return 1;
    }
    return 0;
}

static void trough(void) {   // the High Pasture: three days of hay and one of the herd follows you home
    static const char *const what[3] = {"cow", "hen", "sheep"};
    uint8_t kind = mix(G.area_stamp ^ 0x31) % 3;
    if (G.tame < 3) {
        if (TAKEN & 0x80) { say("The trough is full, and something has been at it. Come back tomorrow."); return; }
        if (!G.hay) { say("An old trough. The wild herd watches from the trees. Hay might bring them closer."); return; }
        if (!ui_yesno("Leave some hay in the trough?")) return;
        G.hay--; G.tame++; TAKEN |= 0x80;
        sfx(SFX_CONFIRM);
        if (G.tame < 3) { say(G.tame == 1 ? "Eyes in the trees. Nothing comes out... yet." : "They ate while you watched! One came almost close enough to touch."); return; }
    }
    s_clr(); s_add("A shaggy Highland "); s_add(what[kind]); s_add(" walks right up and nudges your pocket.");
    say(sbuf);
    if (!take_home(kind, 3)) { say(kind == A_HEN ? "But there's no room in a coop at home. It will wait for you." : "But there's no room in a barn at home. It will wait for you."); return; }
    G.tame = 0;
    music_play(MUS_JINGLE_FANFARE);
    say("It follows you all the way to the cart. It's yours! Highland stock gives double, and it's never been tamed before.");
    music_for_scene();
}

// ---------------------------------------------------------------- the Crossroads Market (weekends)
static void market(uint8_t tent) {
    uint8_t r, k, q, page = 0, c;
    uint32_t cost;
    if (G.day % 7 != 0 && G.day % 7 != 6) { say("The stall is shuttered. A card reads: MARKET DAYS, the 6th and 7th of every week."); return; }
    set_speaker("Trader");
    if (tent == 0) {            // seeds for any season, at twice the shop price
        say("Seeds from every corner of the country! Any season, if you've glass to grow them under.");
        while (1) {
            for (k = 0; k < 7; k++) {
                c = 1 + page * 7 + k;
                s_clr(); s_add(items[c - 1].name); s_pad(12); s_num((uint32_t)crops[c].seed_cost * 2, 5); lbl_set(k);
            }
            s_clr(); s_add(page ? "First..." : "More..."); lbl_set(7);
            ui_begin();
            r = ui_menu(0, 0, 20, lp, 8, 0);
            if (r == 7) { ui_end(); page ^= 1; continue; }
            if (r > 7) { ui_end(); break; }
            c = 1 + page * 7 + r; cost = (uint32_t)crops[c].seed_cost * 2;
            q = G.gold / cost > 99 ? 99 : (uint8_t)(G.gold / cost);
            if (!q) { ui_end(); say("Not with that purse, friend."); continue; }
            q = ui_quantity(q, cost);
            ui_end();
            if (!q) continue;
            G.gold -= cost * q;
            G.seeds[c][R_COMMON] = (uint16_t)G.seeds[c][R_COMMON] + q > 255 ? 255 : G.seeds[c][R_COMMON] + q;
            sfx(SFX_COIN); hud_draw();
        }
    } else if (tent == 1) {     // feed by the cartload
        static const char *const fm[3] = {"20 hay      G300", "10 fertilizer 400", "Bye"};
        say("Feed and fertilizer by the cartload. Cheaper than town, and I don't deliver!");
        while (1) {
            ui_begin();
            r = ui_menu(0, 0, 20, fm, 3, 0);
            ui_end();
            if (r > 1) break;
            cost = r ? 400 : 300;
            if (G.gold < cost) { say("Not with that purse, friend."); continue; }
            G.gold -= cost;
            if (r) G.fert = G.fert > 245 ? 255 : G.fert + 10; else G.hay = G.hay > 65000 ? G.hay : G.hay + 20;
            sfx(SFX_COIN); hud_draw();
        }
    } else {                    // Highland stock
        static const char *const lm[7] = {"Highland cow  7000", "Highland hen  1800", "Highland sheep 8000",
                                          "Goat          2500", "Duck           600", "Pig           3000", "Bye"};
        static const uint16_t lp_[6] = {7000, 1800, 8000, 2500, 600, 3000};
        static const uint8_t lk[6] = {A_COW, A_HEN, A_SHEEP, A_GOAT, A_DUCK, A_PIG};
        say("Highland stock off the hills, and goats, ducks and pigs! You won't find any of them in town.");
        while (1) {
            ui_begin();
            r = ui_menu(0, 0, 20, lm, 7, 0);
            ui_end();
            if (r > 5) break;
            if (G.gold < lp_[r]) { say("Not with that purse, friend."); continue; }
            if (!take_home(lk[r], r < 3 ? 3 : 0)) { say("You've no room for it at home! Build a barn or a coop with space first."); continue; }
            G.gold -= lp_[r];
            sfx(SFX_COIN); hud_draw();
            say("Sold! I'll have it walked down to your farm by nightfall. Feed it well.");
        }
    }
    ui_speaker = 0;
}

static void cart_ride(void) {   // the minecart at the mine entrance: out to three places and back
    static const char *const cm[4] = {"Granite Quarry", "High Pasture", "Crossroads Mkt", "Stay here"};
    static const uint8_t to[3] = {MAP_QUARRY, MAP_PASTURE, MAP_MARKET}, tx[3] = {7, 10, 7}, ty[3] = {10, 12, 10};
    uint8_t r;
    ui_begin();
    ui_title(1, 0, "MINECART");
    r = ui_menu(0, 1, 16, cm, 4, 0);
    ui_end();
    if (r > 2) return;
    sfx(SFX_STEP);
    enter_scene(to[r], tx[r], ty[r], D_UP);
}

static void new_act(uint8_t x, uint8_t y) {
    uint8_t k = place_at(x, y), it;
    if (scene == MAP_WOODS && x == 17 && y == 14) { hollow_log(); return; }
    if (scene == MAP_ISLAND && x == 12 && y == 8) { island_dig(); return; }
    if (scene == MAP_PASTURE && x == 10 && y == 4) { trough(); return; }
    if (scene == MAP_MARKET && y == 3 && x >= 2 && x <= 11 && (x & 3) >= 2) { market((x - 2) >> 2); return; }
    if (k != 0xFF && (it = place_item(k)) != 0xFF) {
        if (scene == MAP_QUARRY) {
            if (G.tool != T_HAMMER) { hud_flash("Needs your hammer"); return; }
            if (!spend_energy(3)) return;
            sfx(SFX_HIT);
        }
        TAKEN |= bit8[k];
        redraw_cell(x, y);
        gain(it, rnd() < 48 ? 1 : 0, 1 + (rnd() < 80));
        return;
    }
    if (map_kind(MIDX(x, y)) & K_WATER) {
        if (G.tool == T_ROD) fish_start();
        else hud_flash(scene == MAP_ISLAND ? "Deep blue water" : "A still, green pond");
    }
}

// ================================================================ shared hooks
mt_t area_mt(uint8_t x, uint8_t y) BANKED {
    mt_t m;
    if (IS_NEW(scene)) {
        uint8_t k = place_at(x, y), it;
        if (k != 0xFF && (it = place_item(k)) != 0xFF) return place_mt(it);
        return map_mt(MIDX(x, y));
    }
    if (scene == MAP_BEACH) {
        uint8_t k = spot_at(x, y);
        if (k != 0xFF && spot_item(k) != 0xFF) return k < 6 ? find_mt(spot_item(k)) : MT_BEACH_TIDEPOOL_FULL;
        if (x == BOTTLE_X && y == BOTTLE_Y && BOTTLE_HERE) return MT_BEACH_GLASS;
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
    if (IS_NEW(scene)) {
        uint8_t k = place_at(x, y);
        if (k != 0xFF && place_item(k) != 0xFF) return 0;
    }
    if (scene == MAP_BEACH) {
        uint8_t k = spot_at(x, y);
        if (k != 0xFF && k < 6 && spot_item(k) != 0xFF) return 0;
        if (x == BOTTLE_X && y == BOTTLE_Y && BOTTLE_HERE) return 0;
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
    if (IS_NEW(scene)) { new_act(x, y); return; }
    if (scene == MAP_MINE && x == 11 && y == 7) { cart_ride(); return; }
    if (ON_FLOOR() && x < 16 && y < 16 && ROCK_AT(x, y)) { strike(x, y); return; }
    if (map_kind(MIDX(x, y)) & K_WATER) hud_flash("Cold, still water");
}

void area_step(void) BANKED {
    if (IS_NEW(scene)) {
        static const char *const hello[5] = {
            "Shell Island! Nobody comes out here. Shells everywhere, and deep water on every side.",
            "The Crossroads Market. Traders set up here at the end of every week: the 6th and 7th days.",
            "The High Pasture. Wild herbs underfoot, and a herd that keeps to the trees. They look hungry.",
            "The Granite Quarry. Ore shows in the rock fresh each morning. Bring your hammer!",
            "The Old Woods. Berries, mushrooms and herbs come up fresh each morning, and the pond is full of fish."};
        uint8_t bit = 4 << AI(scene);
        sfx(scene == MAP_ISLAND ? SFX_STEP_SAND : SFX_STEP);
        if (!(told & bit)) { told |= bit; say(hello[AI(scene)]); }
        return;
    }
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
    if (IS_NEW(scene)) return (kd & K_WATER) ? 1 : (m == MT_DIRT || m == MT_PATH || m == MT_COBBLE || m == MT_BEACH_PIER) ? 6 : (kd & K_WALK) ? 0 : (m == MT_ROCK || m == MT_CLIFF) ? 5 : 2;
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
    else if (IS_NEW(scene)) {
        static const char *const an[5] = {"Shell Island", "The Market", "High Pasture", "The Quarry", "Old Woods"};
        s_add(an[AI(scene)]);
        if (scene != MAP_MARKET) { s_add(": "); s_num(woods_finds(), 0); }
    }
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
