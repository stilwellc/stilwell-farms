// Stilwell Farms -- walking characters: townsfolk, farm animals, the pet (bank 0)
#pragma bank 255
#include <gb/gb.h>
#include "game.h"
BANKREF(actors_spawn)

#define MAX_ACT 8
enum { K_NONE, K_NPC, K_ANIMAL, K_PET, K_STAFF };
uint8_t act_kind[MAX_ACT];
static uint8_t act_id[MAX_ACT], ax[MAX_ACT], ay[MAX_ACT], adir[MAX_ACT], amove[MAX_ACT], atimer[MAX_ACT];
static uint8_t anchor_x[MAX_ACT], anchor_y[MAX_ACT];
static int8_t aox[MAX_ACT], aoy[MAX_ACT];   // pixel offset while moving
static uint8_t nact;

extern void put16(uint8_t oam, int16_t x, int16_t y, uint8_t tile, uint8_t pal, uint8_t flip);
extern void hide16(uint8_t oam);

static const uint8_t npc_down[NUM_NPC] = {SPR_ROSA_DOWN, SPR_BRICK_DOWN, SPR_JUNE_DOWN, SPR_ADA_DOWN, SPR_HOLLIS_DOWN, SPR_MILO_DOWN};
static const uint8_t npc_up[NUM_NPC] = {SPR_ROSA_UP, SPR_BRICK_UP, SPR_JUNE_UP, SPR_ADA_UP, SPR_HOLLIS_UP, SPR_MILO_UP};
static const uint8_t npc_side[NUM_NPC] = {SPR_ROSA_SIDE, SPR_BRICK_SIDE, SPR_JUNE_SIDE, SPR_ADA_SIDE, SPR_HOLLIS_SIDE, SPR_MILO_SIDE};
static const uint8_t npc_pal[NUM_NPC] = {SPR_ROSA_DOWN_PAL, SPR_BRICK_DOWN_PAL, SPR_JUNE_DOWN_PAL, SPR_ADA_DOWN_PAL, SPR_HOLLIS_DOWN_PAL, SPR_MILO_DOWN_PAL};
static const uint8_t npc_shop[NUM_NPC] = {ACT_STORE, ACT_SMITH, ACT_CARPENTER, ACT_CAFE, ACT_HALL, ACT_FOUNTAIN};

uint8_t actor_at(uint8_t x, uint8_t y) BANKED {
    uint8_t k;
    for (k = 0; k < nact; k++)
        if (act_kind[k] && ax[k] == x && ay[k] == y) return k;
    return 0xFF;
}
uint8_t actor_npc(uint8_t a) BANKED { return act_kind[a] == K_NPC ? act_id[a] : 0xFF; }
uint8_t actor_animal(uint8_t a) BANKED { return act_kind[a] == K_ANIMAL ? act_id[a] : 0xFF; }
uint8_t actor_is_pet(uint8_t a) BANKED { return act_kind[a] == K_PET; }
uint8_t actor_staff(uint8_t a) BANKED { return act_kind[a] == K_STAFF ? act_id[a] : 0xFF; }

static uint8_t free_cell(uint8_t x, uint8_t y) {
    return walkable(x, y) && !(x == G.px && y == G.py);
}

static void add(uint8_t kind, uint8_t id, uint8_t x, uint8_t y) {
    uint8_t k = nact, tries;
    if (nact >= MAX_ACT) return;
    // nudge to a free walkable cell nearby
    for (tries = 0; tries < 12 && !free_cell(x, y); tries++) {
        x += (rnd() % 3) - 1;
        y += (rnd() % 3) - 1;
    }
    if (!free_cell(x, y)) return;
    act_kind[k] = kind; act_id[k] = id;
    ax[k] = anchor_x[k] = x; ay[k] = anchor_y[k] = y;
    adir[k] = D_DOWN; amove[k] = 0; atimer[k] = rnd() & 63;
    aox[k] = aoy[k] = 0;
    nact++;
}

// find the walkable cell below a building's act cell (its door)
static uint8_t find_act_spot(uint8_t act, uint8_t *x, uint8_t *y) {
    uint8_t k, best = 0;
    for (k = 0; k < sc_nacts; k++)
        if (sc_acts[k].act == act) {
            if (!best || sc_acts[k].y >= *y) { *x = sc_acts[k].x; *y = sc_acts[k].y + 1; best = 1; }
        }
    return best;
}

void actors_spawn(void) BANKED {
    uint8_t k;
    nact = 0;
    for (k = 0; k < MAX_ACT; k++) act_kind[k] = K_NONE;
    if (scene == SCENE_FARM) {
        uint8_t h = bld_find(B_HOUSE), n = 0;
        if (h != 0xFF) add(K_PET, 0, G.bld[h].x + 3, G.bld[h].y + 3);
        if (G.time < 1200)   // farmhands work 6am-8pm, near their job
            for (k = 0; k < NUM_ROLES; k++) {
                uint16_t s = G.staff[k] ? staff_spot(k) : 0xFFFF;
                if (s != 0xFFFF) add(K_STAFF, k, (uint8_t)s, (uint8_t)(s >> 8));
            }
        if (G.time < 1080 && G.weather == 0 && G.season != SEASON_WINTER)
            for (k = 0; k < MAX_ANIMALS && n < 6; k++) {
                Animal *a = &G.anim[k];
                Building *b;
                if (a->kind == 0xFF || a->home >= MAX_BLD) continue;
                b = &G.bld[a->home];
                add(K_ANIMAL, k, b->x + (rnd() % 3), b->y + bld_h(b->type) + 1 + (rnd() & 1));
                n++;
            }
    } else {
        uint8_t day = G.time >= 480 && G.time < 1080;
        for (k = 0; k < NUM_NPC; k++) {
            uint8_t x = 15, y = 10;
            uint8_t act = (day || k == NPC_MILO) ? npc_shop[k] : ACT_FOUNTAIN;
            if (find_act_spot(act, &x, &y)) add(K_NPC, k, x + (k & 1), y + (act == ACT_FOUNTAIN ? 1 + (k % 3) : 0));
        }
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
    case K_PET:
        *pal = SPR_DOG_DOWN_PAL;
        if (!amove[k] && atimer[k] > 100) return SPR_DOG_SIT;
        return d <= D_UP ? SPR_DOG_DOWN : SPR_DOG_SIDE;
    default: {
        Animal *a = &G.anim[act_id[k]];
        uint8_t kd = a->kind % 3, br = BREED(a);
        static const uint8_t breed_pal[NUM_RARITY] = {SPAL_ANIMAL, SPAL_BREEDRARE, SPAL_BREEDGOLD};
        static const uint8_t front[3] = {SPR_COW, SPR_CHICKEN, SPR_SHEEP};
        static const uint8_t front_bank[3] = {SPR_COW_PAL & 8, SPR_CHICKEN_PAL & 8, SPR_SHEEP_PAL & 8};
        static const uint8_t side[3][2] = {{SPR_COW_SIDE, SPR_COW_SIDE2}, {SPR_HEN_SIDE, SPR_HEN_SIDE2}, {SPR_SHEEP_SIDE, SPR_SHEEP_SIDE2}};
        static const uint8_t side_bank[3] = {SPR_COW_SIDE_PAL & 8, SPR_HEN_SIDE_PAL & 8, SPR_SHEEP_SIDE_PAL & 8};
        // rarity: breed picks the palette; each sprite keeps its own VRAM bank bit
        if (d <= D_UP) { *flip = 0; *pal = front_bank[kd] | breed_pal[br % NUM_RARITY]; return front[kd]; }
        *pal = side_bank[kd] | breed_pal[br % NUM_RARITY];
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
        if (G.cell[i] != C_SOIL || SOIL_RARITY(i) != R_LEGEND) continue;
        c = G.a1[i] & SOIL_CROP;
        if (c && c != CROP_WITHERED && SOIL_GROWN(i) >= crops[c].days) { fx_show(SPR_SPARKLE, SPR_SPARKLE_PAL, x, y); return; }
    }
}

void actors_tick(void) BANKED {
    uint8_t k;
    if (scene == SCENE_FARM) legend_sparkle();
    for (k = 0; k < nact; k++) {
        if (!act_kind[k]) continue;
        if (amove[k]) {
            amove[k]--;
            if (aox[k] > 0) aox[k]--; else if (aox[k] < 0) aox[k]++;
            if (aoy[k] > 0) aoy[k]--; else if (aoy[k] < 0) aoy[k]++;
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
            ax[k] = nx; ay[k] = ny;
            aox[k] = d == D_LEFT ? 16 : d == D_RIGHT ? -16 : 0;
            aoy[k] = d == D_UP ? 16 : d == D_DOWN ? -16 : 0;
            amove[k] = 16;
        }
    }
}

void actors_draw(void) BANKED {
    uint8_t k, flip, pal, t;
    for (k = 0; k < MAX_ACT; k++) {
        if (k >= nact || !act_kind[k]) { hide16(6 + k * 2); continue; }
        t = tile_for(k, &flip, &pal);
        put16(6 + k * 2, (int16_t)(ax[k] * 16) + aox[k] - scx, (int16_t)(ay[k] * 16) + aoy[k] - scy - 2, t, pal, flip);
    }
}
