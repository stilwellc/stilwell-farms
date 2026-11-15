// Stilwell Farms -- inside the barn and the coop: stalls, nest boxes, troughs, animal care (banked)
//
// Walk into a barn or coop door (or press A on it... A at the door still does the quick chores) and
// you're inside its interior map (art/maps/yard_barn.map, yard_coop.map). G.bld_in remembers which
// building, so several barns each show their own animals.
//   Barn: six stalls (x 1..6). The manger behind stall k (row 2) is full once that animal is fed; the
//         animal stands in its stall (row 3). A on the hay stack fills every manger from your hay.
//   Coop: six nest boxes (x 1..6, row 2), one per hen: her box shows an egg when she has laid. Hens peck
//         around the floor. A on the feed trough fills it from your hay.
// Where the animals are (same rule as actors.c on the farm): out grazing on sunny days before 6pm except
// in winter; otherwise (night, rain, snow, winter) inside, in their stalls and on the coop floor.
// The walls and floor follow the farm's building style: barn_mt swaps the CLASSIC tiles.
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"

BANKREF(barn_enter)

extern void put16(uint8_t oam, int16_t x, int16_t y, uint8_t tile, uint8_t pal, uint8_t flip);
extern void hide16(uint8_t oam);

uint8_t in_stall[6];
static uint8_t in_occ, in_fed, in_ready, scan_ok;   // bit per stall: occupied, fed, produce waiting
static const uint8_t bit6[6] = {1, 2, 4, 8, 16, 32};

uint8_t animals_out(void) BANKED {
    return G.time < 1080 && G.weather == 0 && G.season != SEASON_WINTER;
}

void barn_scan(void) BANKED {
    uint8_t k, n = 0;
    in_occ = in_fed = in_ready = 0;
    memset(in_stall, 0xFF, sizeof(in_stall));
    for (k = 0; k < MAX_ANIMALS && n < 6; k++) {
        Animal *a = &G.anim[k];
        if (a->kind == 0xFF || a->home != G.bld_in) continue;
        in_stall[n] = k;
        in_occ |= bit6[n];
        if (a->flags & AF_FED) in_fed |= bit6[n];
        if (a->flags & AF_READY) in_ready |= bit6[n];
        n++;
    }
    scan_ok = 1;
}

// ---------------------------------------------------------------- what the room looks like
static const mt_t style_wall[4] = {MT_BARN_WALL_CLASSIC, MT_BARN_WALL_MODERN, MT_BARN_WALL_CABIN, MT_BARN_WALL_COTTAGE};
static const mt_t style_floor[4] = {MT_BARN_FLOOR_CLASSIC, MT_BARN_FLOOR_MODERN, MT_BARN_FLOOR_CABIN, MT_BARN_FLOOR_COTTAGE};

mt_t barn_mt(uint8_t x, uint8_t y) BANKED {
    mt_t m = map_mt(MIDX(x, y));
    uint8_t k = x - 1;
    if (!scan_ok) barn_scan();   // e.g. a save made inside the barn, loaded straight in
    if (m == MT_BARN_WALL_CLASSIC) return style_wall[G.style & 3];
    if (m == MT_BARN_FLOOR_CLASSIC) return style_floor[G.style & 3];
    if (m == MT_MANGER_EMPTY) return (k < 6 && (in_fed & bit6[k])) ? MT_MANGER_FULL : m;
    if (m == MT_NEST_EMPTY) return (k < 6 && (in_ready & bit6[k])) ? MT_NEST_EGG : m;
    if ((m == ST_FEED_TROUGH || m == ST_FEED_TROUGH + 1) && in_occ && (in_fed & in_occ) == in_occ)
        return m + (ST_FEED_TROUGH_FULL - ST_FEED_TROUGH);
    return m;
}

// ---------------------------------------------------------------- in and out
void barn_enter(uint8_t slot) BANKED {
    uint8_t coop = G.bld[slot].type == B_COOP, m = coop ? MAP_COOP : MAP_BARN, tip = coop ? 2 : 1;
    G.bld_in = slot;
    barn_scan();
    map_select(m);   // the doorway: the first warp
    sfx(SFX_STEP);
    enter_scene(m, sc_warps[0].x, sc_warps[0].y - 1, D_UP);
    if (!(G.barn_tips & tip)) {
        G.barn_tips |= tip;
        say(coop ? "Inside the coop! Each hen has her own nest box. Check them for eggs every morning."
                 : "Inside the barn! Cows and sheep sleep in their stalls, and come in out of the rain and snow.");
        say(coop ? "Fill the trough from your hay to feed the hens. Press A on a hen to see how she's doing."
                 : "Press A on the hay stack to fill every manger. Press A on an animal to milk, shear and pet it.");
    } else if (in_occ && animals_out()) hud_flash(coop ? "The hens are out!" : "Everyone's grazing!");
}

// walking into a door on the farm: barns and coops (and the house) take you inside
uint8_t door_bump(uint8_t x, uint8_t y) BANKED {
    uint16_t i = WIDX(x, y);
    uint8_t slot, t;
    if (x >= WW || y >= WH || W_CELL[i] != C_STRUCT || G.facing != D_UP) return 0;
    slot = W_A1[i];
    t = G.bld[slot].type;
    if (W_A2(i) != bld_door(slot) || (G.bld[slot].flags & BF_RUIN)) return 0;
    if (t == B_HOUSE) { house_enter(); return 1; }
    if (t != B_BARN && t != B_COOP) return 0;
    barn_enter(slot);
    return 1;
}

// leaving: come out one step below the door of the building you went into
void barn_leave(Warp *w) BANKED {
    uint8_t s = G.bld_in, t = G.bld[s].type;
    if (s < MAX_BLD && (t == B_BARN || t == B_COOP)) {
        uint8_t bw = bld_w(t), bh = bld_h(t), d = bld_door(s);
        w->dx = G.bld[s].x + (uint8_t)(d - (bh - 1) * bw);
        w->dy = G.bld[s].y + bh;
    } else { w->dx = 10; w->dy = 12; }
    if (w->dy >= WH) w->dy = WH - 1;
    w->dir = D_DOWN;
    scan_ok = 0;
}

// ---------------------------------------------------------------- caring for the animals
static const uint8_t produce_item[3] = {I_MILK, I_EGG, I_WOOL};
static const char *const kind_name[3] = {"cow", "hen", "sheep"};
static const char *const breed_name[3][NUM_RARITY] = {{"Holstein", "Jersey", "Golden"}, {"White", "Brown", "Golden"},
                                                      {"Merino", "Cocoa", "Golden"}};
static const char *const produce_name[3] = {"Milk", "Egg", "Wool"};

static uint8_t collect(Animal *a) {   // rare and golden breeds give two; golden is always gold quality
    uint8_t br = BREED(a), n = br ? 2 : 1;
    uint8_t q = br == R_LEGEND ? 2 : a->happy > 200 ? 2 : a->happy > 120 ? 1 : 0;
    inv_add(produce_item[a->kind % 3], q, n);
    a->flags &= ~AF_READY;
    s_clr(); s_ch('+'); s_num(n, 0); s_ch(' '); s_add(items[produce_item[a->kind % 3]].name);
    if (q) s_ch(q == 2 ? (char)0xF1 : (char)0xF0);
    sfx(SFX_COIN);
    hud_flash(sbuf);
    player_pose(1, 24);
    return n;
}

static void refresh_room(void) {
    barn_scan();
    redraw_rect(1, 2, sc_w - 2, 4);   // mangers, nests, the trough
}

static void fill_trough(void) {
    uint8_t k, fed = 0, hungry = 0;
    if (!in_occ) {
        say(scene == MAP_COOP ? "No hens live here yet. Buy some at the General Store in town!"
                              : "No animals live here yet. Buy cows or sheep at the General Store in town!");
        return;
    }
    for (k = 0; k < 6; k++) {
        Animal *a;
        if (in_stall[k] == 0xFF) continue;
        a = &G.anim[in_stall[k]];
        if (a->flags & AF_FED) continue;
        if (G.hay) { G.hay--; a->flags |= AF_FED; fed++; }
        else hungry++;
    }
    if (fed) {
        refresh_room();
        sfx(SFX_CONFIRM);
        s_clr(); s_add("Fed "); s_num(fed, 0); s_add("! Hay left "); s_num(G.hay, 0);
        hud_flash(sbuf);
    }
    if (hungry) say("You're out of hay! Cut grass with the sickle (needs a silo) or buy hay in town.");
    else if (!fed) say(scene == MAP_COOP ? "The trough is full. The hens have had their breakfast."
                                         : "Every manger is full. Everyone's been fed today.");
}

static void care(uint8_t an, uint8_t x, uint8_t y) {
    Animal *m = &G.anim[an];
    uint8_t got = 0;
    m->flags |= AF_PET;
    fx_show(SPR_HEART, SPR_HEART_PAL, x, y);
    if (m->flags & AF_READY) { got = collect(m); refresh_room(); }
    else sfx(SFX_CONFIRM);
    animal_panel(an, got);
}

void barn_act(void) BANKED {
    uint8_t x, y, a, k, an;
    mt_t m;
    if (!front_cell(&x, &y)) return;
    a = actor_at(x, y);
    if (a != 0xFF && actor_animal(a) != 0xFF) { care(actor_animal(a), x, y); return; }
    m = map_mt(MIDX(x, y));
    k = x - 1;
    an = k < 6 ? in_stall[k] : 0xFF;
    if (m == MT_NEST_EMPTY) {
        if (an == 0xFF) { say("An empty nest box, lined with fresh straw. Room for another hen!"); return; }
        if (G.anim[an].flags & AF_READY) { collect(&G.anim[an]); refresh_room(); return; }
        s_clr(); s_add(G.anim[an].name); s_add("'s nest box. ");
        s_add(G.anim[an].age < ADULT_AGE ? "She's too young to lay yet." : (G.anim[an].flags & AF_FED) ?
              "She'll lay tomorrow morning." : "No egg: she needs feeding!");
        say(sbuf);
        return;
    }
    if (m == MT_STALL || m == MT_MANGER_EMPTY) {
        if (an == 0xFF) { say("An empty stall, freshly bedded. Room for another cow or sheep!"); return; }
        s_clr(); s_add(G.anim[an].name); s_add("'s stall. ");
        s_add(animals_out() ? "Out grazing in the sunshine." : "Nobody's home.");
        say(sbuf);
        return;
    }
    if ((m >= ST_HAY_STACK && m < ST_HAY_STACK + 4) || m == ST_FEED_TROUGH || m == ST_FEED_TROUGH + 1 || m == MT_BARN_BALE) {
        fill_trough();
        return;
    }
    if (m == MT_PERCH) {
        s_clr(); s_add("The hens' roost. They sleep up here at night. ");
        s_add("Hay in store: "); s_num(G.hay, 0); s_ch('.');
        say(sbuf);
        return;
    }
    if (m == ST_MILK_CANS || m == ST_MILK_CANS + 1) {
        s_clr(); s_add("Milk cans, scrubbed and ready. You have "); s_num(inv_count(I_MILK), 0); s_add(" milk.");
        say(sbuf);
        return;
    }
    if (m == ST_BARN_TOOLS || m == ST_BARN_TOOLS + 1) say("A pitchfork, a rake and Grandpa's old milking pail.");
}

// ---------------------------------------------------------------- the animal panel
static const uint8_t front_spr[3] = {SPR_COW, SPR_CHICKEN, SPR_SHEEP};
static const uint8_t front_bank[3] = {SPR_COW_PAL & 8, SPR_CHICKEN_PAL & 8, SPR_SHEEP_PAL & 8};
static const uint8_t breed_pal[NUM_RARITY] = {SPAL_ANIMAL, SPAL_BREEDRARE, SPAL_BREEDGOLD};

static void wait_ab(void) {
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B)) { sfx(SFX_CURSOR); return; }
        wait_vbl_done();
    }
}

static void hearts(uint8_t x, uint8_t y, uint8_t v) {
    uint8_t j, h = v / 50;
    for (j = 0; j < 5; j++) ui_char(x + j, y, j < h ? GL_HEART : GL_HEART_EMPTY);
}

// portrait: the real sprite (its own breed colours) standing on a stall tile; UI tiles have BG priority
// over sprites, so the four stall cells get it cleared
void portrait(uint8_t tile, uint8_t pal) BANKED {
    uint8_t k;
    hide_sprites();
    ui_put_mt(1, 11, MT_STALL);
    for (k = 0; k < 4; k++) {
        volatile uint8_t *p = (volatile uint8_t *)(0x9C00 + ((uint16_t)(11 + (k >> 1)) << 5) + 1 + (k & 1));
        uint8_t a;
        while (STAT_REG & STATF_BUSY);
        VBK_REG = 1; a = *p; *p = a & 0x7F;
        VBK_REG = 0;
    }
    put16(2, 8, 11 * 8, tile, pal, 0);
}

void animal_panel(uint8_t an, uint8_t got) BANKED {
    Animal *a = &G.anim[an];
    uint8_t kd = a->kind % 3, br = BREED(a) % NUM_RARITY;
    ui_begin();
    ui_box(0, 9, 20, 8);
    ui_title(1, 9, a->name);
    portrait(front_spr[kd], front_bank[kd] | breed_pal[br]);
    s_clr(); s_add(breed_name[kd][br]); s_ch(' '); s_add(kind_name[kd]);
    if (br) { s_ch(' '); s_ch(br == R_LEGEND ? (char)0xF1 : (char)0xF0); }
    ui_text(4, 10, sbuf);
    s_clr();
    if (a->age < ADULT_AGE) { s_add("Baby, "); s_num(a->age, 0); s_add(a->age == 1 ? " day" : " days"); }
    else { s_add("Age "); s_num(a->age, 0); s_add(" days"); }
    ui_text(4, 11, sbuf);
    hearts(4, 12, a->happy);
    s_clr(); s_add("Today: ");
    if (got) { s_ch('+'); s_num(got, 0); s_ch(' '); s_add(produce_name[kd]); s_ch('!'); }
    else if (a->flags & AF_READY) { s_add(produce_name[kd]); s_add(" ready"); }
    else if (a->age < ADULT_AGE) { s_add("grows in "); s_num(ADULT_AGE - a->age, 0); s_ch('d'); }
    else if (kd == A_SHEEP) {
        uint8_t left = WOOL_DAYS - ((a->flags & AF_WOOL) >> 3);
        s_add("wool in "); s_num(left, 0); s_ch('d');
    } else if (a->flags & AF_FED) { s_add(produce_name[kd]); s_add(" tmrw"); }
    else s_add("hungry");
    ui_text(1, 14, sbuf);
    s_clr(); s_add("Fed: "); s_add((a->flags & AF_FED) ? "yes" : "no "); s_add("  Pet: yes");
    ui_text(1, 15, sbuf);
    wait_ab();
    hide16(2);
    ui_end();
}
