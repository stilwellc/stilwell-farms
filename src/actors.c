// Stilwell Farms -- walking characters: townsfolk, farm animals, the pet (bank 0)
#pragma bank 255
#include <gb/gb.h>
#include "game.h"
#include "routes.h"
#include "designer.h"
BANKREF(actors_spawn)

#define MAX_ACT 8
enum { K_NONE, K_NPC, K_ANIMAL, K_PET, K_STAFF, K_HORSE };
uint8_t act_kind[MAX_ACT];
static uint8_t act_id[MAX_ACT], ax[MAX_ACT], ay[MAX_ACT], adir[MAX_ACT], amove[MAX_ACT], atimer[MAX_ACT];
static uint8_t anchor_x[MAX_ACT], anchor_y[MAX_ACT];
static int8_t aox[MAX_ACT], aoy[MAX_ACT];   // pixel offset while moving
static uint8_t nact;
// town life: townsfolk walk their daily routes (flow fields in routes.c) and show emote bubbles
static uint8_t atgt[MAX_ACT];      // SP_* spot a townsperson is heading for; | HOME_BIT = home: in at the door
static uint8_t astuck[MAX_ACT];    // blocked step attempts in a row
static uint16_t sched_t;           // G.time of the last schedule pass
static uint8_t greeted, arrive, greet_t;   // bit per townsperson greeted this visit; first town frame
static uint8_t resched_n;          // schedule pass in progress: one townsperson a frame
uint8_t emo_k, emo_kind, emo_t;    // emote bubble: actor slot, EMO_*, frames left (0 = stays up)
#define TOWN_SLOTS 7               // in town, slot 7's sprite carries the emote bubble
#define HOME_BIT 0x80

extern void put16(uint8_t oam, int16_t x, int16_t y, uint8_t tile, uint8_t pal, uint8_t flip);
extern void hide16(uint8_t oam);

static const uint8_t npc_down[NUM_NPC] = {SPR_ROSA_DOWN, SPR_BRICK_DOWN, SPR_JUNE_DOWN, SPR_ADA_DOWN, SPR_HOLLIS_DOWN, SPR_MILO_DOWN};
static const uint8_t npc_up[NUM_NPC] = {SPR_ROSA_UP, SPR_BRICK_UP, SPR_JUNE_UP, SPR_ADA_UP, SPR_HOLLIS_UP, SPR_MILO_UP};
static const uint8_t npc_side[NUM_NPC] = {SPR_ROSA_SIDE, SPR_BRICK_SIDE, SPR_JUNE_SIDE, SPR_ADA_SIDE, SPR_HOLLIS_SIDE, SPR_MILO_SIDE};
static const uint8_t npc_pal[NUM_NPC] = {SPR_ROSA_DOWN_PAL, SPR_BRICK_DOWN_PAL, SPR_JUNE_DOWN_PAL, SPR_ADA_DOWN_PAL, SPR_HOLLIS_DOWN_PAL, SPR_MILO_DOWN_PAL};
static const uint8_t home_spot[NUM_NPC] = {SP_STORE, SP_SMITH, SP_CARP, SP_HOME_A, SP_HOME_B, SP_HOME_A};   // front doors

uint8_t actor_at(uint8_t x, uint8_t y) BANKED {
    uint8_t k;
    for (k = 0; k < nact; k++)
        if (act_kind[k] && ax[k] == x && ay[k] == y) return k;
    return 0xFF;
}
// building never waits on the dog: the pet doesn't block a placement (or a blueprint ghost), it hops off
// whatever goes down on its cell (pet_unstick)
uint8_t actor_blocks(uint8_t x, uint8_t y) BANKED {
    uint8_t a = actor_at(x, y);
    return a != 0xFF && act_kind[a] != K_PET;
}
uint8_t actor_npc(uint8_t a) BANKED { return act_kind[a] == K_NPC ? act_id[a] : 0xFF; }
uint8_t actor_animal(uint8_t a) BANKED { return act_kind[a] == K_ANIMAL ? act_id[a] : 0xFF; }
uint8_t actor_is_pet(uint8_t a) BANKED { return act_kind[a] == K_PET; }
uint8_t actor_staff(uint8_t a) BANKED { return act_kind[a] == K_STAFF ? act_id[a] : 0xFF; }
uint8_t actor_is_horse(uint8_t a) BANKED { return act_kind[a] == K_HORSE; }

static uint8_t free_cell(uint8_t x, uint8_t y) {
    return walkable(x, y) && !(x == G.px && y == G.py);
}

// something was built on the pet's cell: it hops to the nearest open cell (up to two away)
static void pet_unstick(uint8_t k) {
    uint8_t x = ax[k], y = ay[k], r;
    int8_t dx, dy;
    act_kind[k] = K_NONE;   // (walkable counts the pet itself as in the way)
    if (!walkable(x, y))
        for (r = 1; r <= 2; r++)
            for (dy = -(int8_t)r; dy <= (int8_t)r; dy++)
                for (dx = -(int8_t)r; dx <= (int8_t)r; dx++)
                    if (free_cell(x + dx, y + dy)) {
                        ax[k] = x + dx; ay[k] = y + dy; aox[k] = aoy[k] = 0;
                        act_kind[k] = K_PET;
                        return;
                    }
    act_kind[k] = K_PET;
}

// a step in or out of the blueprint ghost redraws both cells, so the ghost always shows what would go down
static void ghost_step(uint8_t ox, uint8_t oy, uint8_t nx, uint8_t ny) {
    if ((uint8_t)(ox - ghost_x) < ghost_w && (uint8_t)(oy - ghost_y) < ghost_h) redraw_cell(ox, oy);
    if ((uint8_t)(nx - ghost_x) < ghost_w && (uint8_t)(ny - ghost_y) < ghost_h) redraw_cell(nx, ny);
}

// take a slot (reusing one whose townsperson went indoors); 0xFF when full
static uint8_t place(uint8_t kind, uint8_t id, uint8_t x, uint8_t y) {
    uint8_t k, lim = scene == MAP_TOWN ? TOWN_SLOTS : MAX_ACT;
    for (k = 0; k < nact && act_kind[k]; k++) ;
    if (k >= lim) return 0xFF;
    act_kind[k] = kind; act_id[k] = id;
    ax[k] = anchor_x[k] = x; ay[k] = anchor_y[k] = y;
    adir[k] = D_DOWN; amove[k] = 0; atimer[k] = rnd() & 63;
    aox[k] = aoy[k] = 0;
    atgt[k] = 0; astuck[k] = 0;
    if (k == nact) nact++;
    return k;
}

static uint8_t add(uint8_t kind, uint8_t id, uint8_t x, uint8_t y) {
    uint8_t tries;
    // nudge to a free walkable cell nearby
    for (tries = 0; tries < 12 && !free_cell(x, y); tries++) {
        x += (rnd() % 3) - 1;
        y += (rnd() % 3) - 1;
    }
    return free_cell(x, y) ? place(kind, id, x, y) : 0xFF;
}

// ---------------------------------------------------------------- the horse (horse.c)
static uint8_t horse_k = 0xFF;   // its actor index on the farm
uint8_t horse_actor(void) BANKED { return horse_k; }
void horse_actor_move(uint8_t x, uint8_t y) BANKED {
    if (horse_k == 0xFF) return;
    ax[horse_k] = anchor_x[horse_k] = x; ay[horse_k] = anchor_y[horse_k] = y;
    aox[horse_k] = aoy[horse_k] = 0; amove[horse_k] = 0; adir[horse_k] = G.facing;
}

// inside a barn / coop: cows and sheep stand in their stalls, hens peck about the floor
static void spawn_inside(void) {
    uint8_t k, an;
    barn_scan();
    if (animals_out()) return;
    for (k = 0; k < 6; k++) {
        an = in_stall[k];
        if (an == 0xFF) continue;
        if (scene == MAP_BARN) place(K_ANIMAL, an, 1 + k, 3);
        else add(K_ANIMAL, an, 1 + (k % 3) * 2, 4 + (k / 3) * 2);
    }
}

uint8_t actor_slot_npc(uint8_t npc) BANKED {
    uint8_t k;
    for (k = 0; k < nact; k++) if (act_kind[k] == K_NPC && act_id[k] == npc) return k;
    return 0xFF;
}

// everyone heads for where their schedule says; someone due out steps from their front door
static void resched_one(uint8_t n) {
    uint8_t s = tl_spot(n), a = actor_slot_npc(n);
    if (a == 0xFF) {
        if (s & HOME_BIT) return;
        a = add(K_NPC, n, spot_x(home_spot[n]), spot_y(home_spot[n]));
        if (a == 0xFF) return;
    }
    atgt[a] = s | 0x40; astuck[a] = 0;   // 0x40: on the move until they reach it
}

void actors_resched(void) BANKED {
    uint8_t n;
    for (n = 0; n < NUM_NPC; n++) resched_one(n);
}

static uint8_t spr_set = 0;   // art_load leaves the first set (TOWN) resident

void actors_spawn(void) BANKED {
    uint8_t k;
    nact = 0;
    horse_k = 0xFF;
    riding = 0;   // any scene change dismounts; the horse goes back to its stable
    for (k = 0; k < MAX_ACT; k++) act_kind[k] = K_NONE;
    emo_kind = 0;
    k = (scene == MAP_TOWN || IS_SHORE(scene) || scene == MAP_MARKET) ? SPRSET_TOWN : SPRSET_FARM;   // the LCD is off here: swap the sprite set (Marcy visits the beach)
    if (k != spr_set) { spr_set = k; spr_set_load(k); }
    if (IS_BARNIN(scene)) { spawn_inside(); return; }
    if (scene == SCENE_FARM) {
        uint8_t h = bld_find(B_HOUSE), n = 0;
        if (h != 0xFF) add(K_PET, 0, G.bld[h].x + 3, G.bld[h].y + 3);
        h = bld_find(B_STABLE);
        if (G.horse && h != 0xFF) {   // the horse waits by its stable
            horse_k = add(K_HORSE, 0, G.bld[h].x + 1, G.bld[h].y + 2);
        }
        if (G.time < 1200)   // farmhands work 6am-8pm, near their job
            for (k = 0; k < NUM_ROLES; k++) {
                uint16_t s = G.staff[k] ? staff_spot(k) : 0xFFFF;
                if (s != 0xFFFF) add(K_STAFF, k, (uint8_t)s, (uint8_t)(s >> 8));
            }
        if (animals_out())
            for (k = 0; k < MAX_ANIMALS && n < 6; k++) {
                Animal *a = &G.anim[k];
                Building *b;
                if (a->kind == 0xFF || a->home >= MAX_BLD) continue;
                b = &G.bld[a->home];
                add(K_ANIMAL, k, b->x + (rnd() % 3), b->y + bld_h(b->type) + 1 + (rnd() & 1));
                n++;
            }
    }
    if (fest_on) fest_on = 0;   // left town mid egg hunt: the game is called off
    if (scene == MAP_BEACH) { if (milo_beach()) add(K_NPC, NPC_MILO, 12, 8); return; }   // Marcy by the pier
    if (scene == MAP_TOWN) {
        // townsfolk start wherever their schedule has them right now (home = indoors)
        sched_t = G.time; greeted = 0; arrive = 1; resched_n = NUM_NPC;
        for (k = 0; k < NUM_NPC; k++) {
            uint8_t s = tl_spot(k), a;
            if (s & HOME_BIT) continue;
            a = add(K_NPC, k, spot_x(s), spot_y(s));
            if (a != 0xFF) atgt[a] = s;
        }
    }
}

// ---------------------------------------------------------------- scripted scenes (townlife.c)
uint8_t actor_bring(uint8_t npc, uint8_t x, uint8_t y) BANKED {
    uint8_t a = actor_slot_npc(npc);
    if (a == 0xFF) { a = add(K_NPC, npc, x, y); if (a == 0xFF) return 0xFF; }
    else { ax[a] = x; ay[a] = y; }
    amove[a] = 0; aox[a] = aoy[a] = 0;
    atgt[a] = tl_spot(npc) | 0x40;
    return a;
}

void actor_face(uint8_t a, uint8_t d) BANKED { adir[a] = d; }

static void anim_step(uint8_t a, uint8_t d) {
    uint8_t f;
    adir[a] = d;
    if (d == D_LEFT) ax[a]--; else if (d == D_RIGHT) ax[a]++; else if (d == D_UP) ay[a]--; else ay[a]++;
    aox[a] = d == D_LEFT ? 16 : d == D_RIGHT ? -16 : 0;
    aoy[a] = d == D_UP ? 16 : d == D_DOWN ? -16 : 0;
    for (f = 16; f; f--) {
        amove[a] = f;
        if (aox[a] > 0) aox[a]--; else if (aox[a] < 0) aox[a]++;
        if (aoy[a] > 0) aoy[a]--; else if (aoy[a] < 0) aoy[a]++;
        update_sprites();
        wait_vbl_done();
    }
    amove[a] = 0;
}

// a scripted walk: across, then up or down (scenes keep their paths on open street)
void actor_walk(uint8_t a, uint8_t x, uint8_t y) BANKED {
    uint8_t n;
    for (n = 0; n < 40 && (ax[a] != x || ay[a] != y); n++)
        anim_step(a, ax[a] < x ? D_RIGHT : ax[a] > x ? D_LEFT : ay[a] < y ? D_DOWN : D_UP);
}

void emote(uint8_t a, uint8_t kind, uint8_t frames) BANKED {
    emo_k = a; emo_kind = kind; emo_t = frames;
    if (kind) sfx(kind == EMO_HEART || kind == EMO_NOTE ? SFX_CONFIRM : SFX_CURSOR);
}

// ---------------------------------------------------------------- daily routes
// one step along the flow field toward the scheduled spot; 0 once there (then they idle)
static uint8_t npc_route(uint8_t k) {
    uint8_t s = atgt[k] & 0x3F, tx, ty, d, nx, ny;
    if (ax[k] == anchor_x[k] && ay[k] == anchor_y[k] && !(atgt[k] & 0x40)) return 0;   // settled
    tx = spot_x(s); ty = spot_y(s);
    if (ax[k] == tx && ay[k] == ty) {
        if (atgt[k] & HOME_BIT) { act_kind[k] = K_NONE; if (emo_k == k) emo_kind = 0; return 1; }   // in the door
        anchor_x[k] = tx; anchor_y[k] = ty; atgt[k] &= ~0x40;
        return 0;
    }
    if (++atimer[k] < 3) return 1;   // a beat between steps: a stroll, not a march
    atimer[k] = 0;
    d = astuck[k] >= 12 ? rnd() & 3 : route_dir(s, ax[k], ay[k]);   // boxed in a while: sidestep
    nx = ax[k]; ny = ay[k];
    if (d == D_LEFT) nx--; else if (d == D_RIGHT) nx++; else if (d == D_UP) ny--; else ny++;
    adir[k] = d;
    if (!free_cell(nx, ny)) {
        // somebody is standing on their spot: right beside it is close enough
        if (nx == tx && ny == ty && !(atgt[k] & HOME_BIT)) { anchor_x[k] = ax[k]; anchor_y[k] = ay[k]; atgt[k] &= ~0x40; return 0; }
        astuck[k]++;
        return 1;
    }
    if (astuck[k] >= 12) astuck[k] = 0;
    ax[k] = nx; ay[k] = ny;
    aox[k] = d == D_LEFT ? 16 : d == D_RIGHT ? -16 : 0;
    aoy[k] = d == D_UP ? 16 : d == D_DOWN ? -16 : 0;
    amove[k] = 16;
    return 1;
}

static uint8_t near_player(uint8_t k, uint8_t r) {
    uint8_t dx = ax[k] > G.px ? ax[k] - G.px : G.px - ax[k], dy = ay[k] > G.py ? ay[k] - G.py : G.py - ay[k];
    return dx + dy <= r;
}

static void face_player(uint8_t k) {
    int8_t dx = (int8_t)G.px - ax[k], dy = (int8_t)G.py - ay[k];
    adir[k] = (dx > 1 || dx < -1 || !dy) ? (dx > 0 ? D_RIGHT : D_LEFT) : (dy > 0 ? D_DOWN : D_UP);
}

static void town_tick(void) {
    uint8_t k;
    if (arrive) { arrive = 0; tl_arrive(); }       // a due heart event walks up to you
    if (G.time != sched_t) { sched_t = G.time; resched_n = 0; }   // every ten minutes: anyone due elsewhere sets off,
    else if (resched_n < NUM_NPC) resched_one(resched_n++);       // one a frame (keeps the frame budget flat)
    if (fest_on) festival_tick();
    if (emo_t && !--emo_t) emo_kind = 0;
    if (++greet_t & 7) return;
    for (k = 0; k < nact; k++) {                   // a hello as you pass (once a visit)
        uint8_t n = act_id[k], bit = 1 << n;
        if (act_kind[k] != K_NPC || amove[k] || (greeted & bit) || !near_player(k, 2)) continue;
        greeted |= bit;
        face_player(k);
        emote(k, G.friend[n] >= 150 ? EMO_HEART : n == NPC_BRICK ? EMO_DOTS : n == NPC_MILO ? EMO_EXCLAIM : EMO_NOTE, 70);
        tl_greet(n);
        return;
    }
}

static uint8_t tile_for(uint8_t k, uint8_t *flip, uint8_t *pal) {
    uint8_t d = adir[k], walk = amove[k] && (amove[k] & 8);
    *flip = d == D_RIGHT;
    switch (act_kind[k]) {
    case K_NPC:
        *pal = npc_pal[act_id[k]];
        if (d == D_DOWN) { *flip = walk; return npc_down[act_id[k]]; }
        if (d == D_UP) { *flip = walk; return npc_up[act_id[k]]; }
        return npc_side[act_id[k]];
    case K_STAFF:
        *pal = staff_pal(act_id[k]);
        if (d == D_DOWN) { *flip = walk; return SPR_FARMHAND_DOWN; }
        if (d == D_UP) { *flip = walk; return SPR_FARMHAND_UP; }
        return SPR_FARMHAND_SIDE;
    case K_HORSE: {
        static const uint8_t coat_pal[NUM_RARITY] = {SPAL_ANIMAL, SPAL_BREEDRARE, SPAL_BREEDGOLD};
        *pal = (SPR_HORSE_SIDE_PAL & 8) | coat_pal[(G.horse - 1) % NUM_RARITY];
        if (d == D_DOWN) { *flip = 0; return SPR_HORSE_DOWN; }
        if (d == D_UP) { *flip = 0; return SPR_HORSE_UP; }
        return walk ? SPR_HORSE_SIDE2 : SPR_HORSE_SIDE;
    }
    case K_PET:
        *pal = SPR_DOG_DOWN_PAL;
        if (!amove[k] && atimer[k] > 100) return SPR_DOG_SIT;
        return d <= D_UP ? SPR_DOG_DOWN : SPR_DOG_SIDE;
    default: {
        Animal *a = &G.anim[act_id[k]];
        uint8_t kd = a->kind % NUM_KINDS, br = BREED(a);
        static const uint8_t breed_pal[4] = {SPAL_ANIMAL, SPAL_BREEDRARE, SPAL_BREEDGOLD, SPAL_BREEDRARE};   // 3 = Highland
        static const uint8_t front[NUM_KINDS] = {SPR_COW, SPR_CHICKEN, SPR_SHEEP, SPR_GOAT, SPR_DUCK, SPR_PIG};
        static const uint8_t front_bank[NUM_KINDS] = {SPR_COW_PAL & 8, SPR_CHICKEN_PAL & 8, SPR_SHEEP_PAL & 8, SPR_GOAT_PAL & 8, SPR_DUCK_PAL & 8, SPR_PIG_PAL & 8};
        static const uint8_t side[NUM_KINDS][2] = {{SPR_COW_SIDE, SPR_COW_SIDE2}, {SPR_HEN_SIDE, SPR_HEN_SIDE2}, {SPR_SHEEP_SIDE, SPR_SHEEP_SIDE2},
                                                   {SPR_GOAT_SIDE, SPR_GOAT_SIDE2}, {SPR_DUCK_SIDE, SPR_DUCK_SIDE2}, {SPR_PIG_SIDE, SPR_PIG_SIDE2}};
        static const uint8_t side_bank[NUM_KINDS] = {SPR_COW_SIDE_PAL & 8, SPR_HEN_SIDE_PAL & 8, SPR_SHEEP_SIDE_PAL & 8, SPR_GOAT_SIDE_PAL & 8, SPR_DUCK_SIDE_PAL & 8, SPR_PIG_SIDE_PAL & 8};
        // rarity: breed picks the palette; each sprite keeps its own VRAM bank bit
        if (d <= D_UP) { *flip = 0; *pal = front_bank[kd] | breed_pal[br & 3]; return front[kd]; }
        *pal = side_bank[kd] | breed_pal[br & 3];
        return side[kd][walk ? 1 : 0];
    }
    }
}

// rarity: now and then a ripe legendary crop near you twinkles
static uint8_t sparkle_t;
static void legend_sparkle(void) {
    uint8_t n, x, y, c;
    uint16_t i;
    if (++sparkle_t < 90) return;
    sparkle_t = rnd() & 31;
    for (n = 0; n < 40; n++) {   // offsets -5..5 / -4..4 without % (80 byte divides overran the frame)
        x = rnd() & 15; if (x > 10) x -= 5;
        y = rnd() & 15; if (y > 8) y -= 7;
        x = G.px + x - 5; y = G.py + y - 4;
        if (x >= WW || y >= WH) continue;
        i = WIDX(x, y);
        if (W_CELL[i] != C_SOIL || SOIL_RARITY(i) != R_LEGEND) continue;
        c = W_A1[i] & SOIL_CROP;
        if (c && c != CROP_WITHERED && SOIL_GROWN(i) >= crops[c].days) { fx_show(SPR_SPARKLE, SPR_SPARKLE_PAL, x, y); return; }
    }
}

void actors_tick(void) BANKED {
    uint8_t k, town = scene == MAP_TOWN;
    if (scene == SCENE_FARM) legend_sparkle();
    else if (town) town_tick();
    for (k = 0; k < nact; k++) {
        if (!act_kind[k]) continue;
        if (k == horse_k && riding) { ax[k] = G.px; ay[k] = G.py; amove[k] = 0; continue; }   // under you
        if (scene == MAP_BARN) {   // stalled: just look around now and then
            if (++atimer[k] >= 150) { atimer[k] = rnd() & 63; adir[k] = rnd() < 150 ? D_DOWN : (rnd() & 1) ? D_LEFT : D_RIGHT; }
            continue;
        }
        if (amove[k]) {
            amove[k]--;
            if (aox[k] > 0) aox[k]--; else if (aox[k] < 0) aox[k]++;
            if (aoy[k] > 0) aoy[k]--; else if (aoy[k] < 0) aoy[k]++;
            continue;
        }
        if (act_kind[k] == K_PET && building) pet_unstick(k);
        if (town && act_kind[k] == K_NPC) {
            if (npc_route(k)) continue;
            // at their spot: turn to you when you're close, else look about now and then
            if (near_player(k, 2)) { face_player(k); continue; }
            if (++atimer[k] < 150) continue;
            atimer[k] = rnd() & 63;
            adir[k] = rnd() & 3;
            continue;
        }
        if (++atimer[k] < 120) continue;
        {
            uint8_t d = rnd() & 3, nx = ax[k], ny = ay[k];
            // the pet trots toward you when far; others stay near their anchor
            if (act_kind[k] == K_PET) {
                int8_t ddx = (int8_t)G.px - ax[k], ddy = (int8_t)G.py - ay[k];
                if (ddx > 2 || ddx < -2 || ddy > 2 || ddy < -2)
                    d = (ddx > 1 || ddx < -1) ? (ddx > 0 ? D_RIGHT : D_LEFT) : (ddy > 0 ? D_DOWN : D_UP);
                atimer[k] = 80 + (rnd() & 15);
            } else {
                atimer[k] = rnd() & 63;
                if (rnd() < 90) continue;   // idle a while
            }
            if (d == D_LEFT) nx--; else if (d == D_RIGHT) nx++; else if (d == D_UP) ny--; else ny++;
            adir[k] = d;
            if (act_kind[k] != K_PET) {
                int8_t ox = (int8_t)nx - anchor_x[k], oy = (int8_t)ny - anchor_y[k];
                if (ox > 3 || ox < -3 || oy > 2 || oy < -2) continue;
            }
            if (!free_cell(nx, ny)) continue;
            if (ghost_w && act_kind[k] != K_PET) ghost_step(ax[k], ay[k], nx, ny);
            ax[k] = nx; ay[k] = ny;
            aox[k] = d == D_LEFT ? 16 : d == D_RIGHT ? -16 : 0;
            aoy[k] = d == D_UP ? 16 : d == D_DOWN ? -16 : 0;
            amove[k] = 16;
        }
    }
}

static const uint8_t emo_tile[5] = {0, SPR_EXCLAIM, SPR_HEART, SPR_EMOTE_NOTE, SPR_EMOTE_DOTS};
static const uint8_t emo_pal[5] = {0, SPR_EXCLAIM_PAL, SPR_HEART_PAL, SPR_EMOTE_NOTE_PAL, SPR_EMOTE_DOTS_PAL};

void actors_draw(void) BANKED {
    uint8_t k, flip, pal, t;
    for (k = 0; k < MAX_ACT; k++) {
        if (k == horse_k && riding) continue;   // ride_draw owns its sprites
        if (k == TOWN_SLOTS && scene == MAP_TOWN) {   // the emote bubble rides above its actor
            uint8_t e = emo_k;
            if (emo_kind && e < nact && act_kind[e])
                put16(6 + k * 2, (int16_t)(ax[e] * 16) + aox[e] - scx, (int16_t)(ay[e] * 16) + aoy[e] - scy - 17,
                      emo_tile[emo_kind], emo_pal[emo_kind], 0);
            else hide16(6 + k * 2);
            continue;
        }
        if (k >= nact || !act_kind[k]) { hide16(6 + k * 2); continue; }
        t = tile_for(k, &flip, &pal);
        put16(6 + k * 2, (int16_t)(ax[k] * 16) + aox[k] - scx, (int16_t)(ay[k] * 16) + aoy[k] - scy - 2, t, pal, flip);
    }
}

// in the saddle (main.c draw_player): the rider sits above the horse. Facing down the horse's head is in
// front of the rider, so the horse takes the player's (top priority) sprites and the rider the actor's.
void ride_draw(int16_t sx, int16_t sy) BANKED {
    uint8_t d = G.facing, flip = d == D_RIGHT, step = ((uint8_t)ppx ^ (uint8_t)ppy) & 8, hs, rs, ho, ro, pal;
    int8_t rdx = 0, rdy;
    static const uint8_t coat_pal[NUM_RARITY] = {SPAL_ANIMAL, SPAL_BREEDRARE, SPAL_BREEDGOLD};
    pal = (SPR_HORSE_SIDE_PAL & 8) | coat_pal[(G.horse - 1) % NUM_RARITY];
    ho = horse_k == 0xFF ? 4 : 6 + horse_k * 2;   // (4: the chimney smoke's pair, never without a horse)
    ro = 0;
    if (d == D_DOWN) { hs = SPR_HORSE_DOWN; rs = SPR_RIDER_DOWN; rdy = -10; ho = 0; ro = horse_k == 0xFF ? 4 : 6 + horse_k * 2; flip = 0; }
    else if (d == D_UP) { hs = SPR_HORSE_UP; rs = SPR_RIDER_UP; rdy = -6; flip = 0; }
    else { hs = step ? SPR_HORSE_SIDE2 : SPR_HORSE_SIDE; rs = SPR_RIDER_SIDE; rdy = -5; rdx = flip ? -3 : 3; }
    put16(ho, sx, sy, hs, pal, flip);
    put16(ro, sx + rdx, sy + rdy, rs, SPR_RIDER_SIDE_PAL, flip);
}
