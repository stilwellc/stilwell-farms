// Stilwell Farms -- battery saves: two slots, plus carrying a v2 farm forward (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include <stddef.h>
#include "game.h"

BANKREF(save_write)

uint8_t save_slot;
#define SLOT(s) ((uint8_t *)(0xA000 + ((uint16_t)(s) << 12)))

// s = rotl1(s) + byte over the whole state. SDCC's C loop spent ~40 cycles a byte (4.4KB: ~5 frames
// at every save); this is 16. sdcccall(1): p in DE, n in BC, result in BC.
static uint16_t sum(const uint8_t *p, uint16_t n) __naked {
    (void)p; (void)n;
    __asm
        ld   hl, #0x1234
    10$:
        ld   a, c
        or   a, b
        jr   z, 13$
        dec  bc
        add  hl, hl             ; s << 1, old bit 15 -> carry
        jr   nc, 11$
        inc  l                  ; ... rotated into bit 0
    11$:
        ld   a, (de)
        inc  de
        add  a, l
        ld   l, a
        jr   nc, 10$
        inc  h
        jr   10$
    13$:
        ld   c, l
        ld   b, h
        ret
    __endasm;
}

void save_write(void) BANKED {
    G.magic = SAVE_MAGIC;
    G.version = SAVE_VERSION;
    G.checksum = sum((const uint8_t *)&G, sizeof(G) - 2);
    ENABLE_RAM;
    SWITCH_RAM(0);
    memcpy(SLOT(save_slot), &G, sizeof(G));
    DISABLE_RAM;
}

// v2 layout (the first release): one farm, fixed buildings, flat inventory
#define V2_SIZE 846
static uint8_t v2_ok(const uint8_t *p) {
    return p[0] == (SAVE_MAGIC & 0xFF) && p[1] == (SAVE_MAGIC >> 8) && p[2] == 2 &&
           (p[V2_SIZE - 2] | (p[V2_SIZE - 1] << 8)) == sum(p, V2_SIZE - 2);
}

// v3 = the current layout up to rng_hi, except inv had 39 items (no wool) and seeds had no rarity;
// everything from furn_init on was appended later. v3 ended with the checksum right after rng.
#define V3_INV (39 * 3)
#define V3_SEEDS (NUM_CROPS + 1)
#define V3_TAIL (offsetof(GameState, furn_init) - offsetof(GameState, saplings))
#define V3_SIZE (offsetof(GameState, inv) + V3_INV + V3_SEEDS + V3_TAIL + 2)
static uint8_t v3_ok(const uint8_t *p) {
    return p[2] == 3 && (p[V3_SIZE - 2] | (p[V3_SIZE - 1] << 8)) == sum(p, V3_SIZE - 2);
}

static uint8_t check(uint8_t slot) {  // 0 none, 2 old v2 farm, 3 current (or v3, carried forward on load)
    const uint8_t *p = SLOT(slot);
    const GameState *s = (const GameState *)p;
    if (s->magic != SAVE_MAGIC) return 0;
    if (s->version == SAVE_VERSION && s->checksum == sum(p, sizeof(GameState) - 2)) return 3;
    if (v3_ok(p)) return 3;
    if (slot == 0 && v2_ok(p)) return 2;
    return 0;
}

uint8_t save_valid(uint8_t slot) BANKED {
    uint8_t v;
    ENABLE_RAM; SWITCH_RAM(0);
    v = check(slot);
    DISABLE_RAM;
    return v;
}

static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

// rebuild a v2 farm's progress in the new world: money, goods, seeds, tools, house, animals
static void migrate_v2(const uint8_t *p) {
    static const uint8_t inv_map[11] = {I_RADISH, I_CARROT, I_STRAWB, I_TOMATO, I_CORN, I_PUMPKIN, I_BEET, I_MILK, I_EGG, I_HONEY, I_APPLE};
    static const uint8_t seed_map[8] = {0, 1, 2, 3, 5, 6, 10, 11};
    uint8_t k, cows = p[801], hens = p[802], up = p[796], b;
    world_new();
    G.gold = (uint32_t)rd16(p + 771) | ((uint32_t)rd16(p + 773) << 16);
    G.earned = (uint32_t)rd16(p + 775) | ((uint32_t)rd16(p + 777) << 16);
    G.ship_value = (uint32_t)rd16(p + 779) | ((uint32_t)rd16(p + 781) << 16);
    G.day = p[785]; G.season = p[786] & 3; G.year = p[787];
    G.energy_max = p[789]; G.energy = G.energy_max;
    for (k = 0; k < 11; k++) { uint16_t n = rd16(p + 807 + k * 2); G.inv[inv_map[k]][0] = n > 255 ? 255 : n; }
    for (k = 1; k < 8; k++) G.seeds[seed_map[k]][R_COMMON] = p[829 + k];
    G.wood = rd16(p + 837); G.stone = rd16(p + 839); G.style = p[841] & 3;
    if (up & 1) G.tier[TT_CAN] = 1;
    if (up & 2) G.tier[TT_HOE] = 1;
    if (up & 4) G.tier[TT_AXE] = 1;
    if (up & 8) G.tier[TT_HAMMER] = 1;
    b = bld_find(B_HOUSE);
    if (b != 0xFF) G.bld[b].flags = (p[797] & 3) << 1;
    if (p[798] && (b = bld_find(B_BARN)) != 0xFF) G.bld[b].flags = 0;
    if (p[799] && (b = bld_find(B_COOP)) != 0xFF) G.bld[b].flags = 0;
    if (p[800] && (b = bld_find(B_GREENHOUSE)) != 0xFF) G.bld[b].flags = 0;
    for (k = 0; k < cows && k < 4; k++) {
        G.anim[k].kind = A_COW; G.anim[k].home = bld_find(B_BARN); G.anim[k].happy = 150; strcpy(G.anim[k].name, "Daisy");
    }
    for (k = 0; k < hens && k < 6; k++) {
        Animal *a = &G.anim[4 + k];
        a->kind = A_HEN; a->home = bld_find(B_COOP); a->happy = 150; strcpy(a->name, "Penny");
    }
}

uint8_t save_load(uint8_t slot) BANKED {
    uint8_t v;
    ENABLE_RAM; SWITCH_RAM(0);
    v = check(slot);
    if (v == 3 && SLOT(slot)[2] == 3) {   // v3: carry it forward field block by field block
        const uint8_t *p = SLOT(slot) + offsetof(GameState, inv) + V3_INV;
        memset(&G, 0, sizeof(G));          // no wool, furniture, staff or businesses yet
        memcpy(&G, SLOT(slot), offsetof(GameState, inv) + V3_INV);
        for (v = 0; v < V3_SEEDS; v++) G.seeds[v][R_COMMON] = p[v];
        memcpy(G.saplings, p + V3_SEEDS, V3_TAIL);
        v = 3;
        G.version = SAVE_VERSION;
        if (G.scene != SCENE_FARM) G.scene = MAP_TOWN;   // the only v3 map you could save in
    } else if (v == 3) memcpy(&G, SLOT(slot), sizeof(G));
    else if (v == 2) migrate_v2(SLOT(slot));
    DISABLE_RAM;
    save_slot = slot;
    if (v == 2) G.scene = SCENE_FARM;
    return v != 0;
}
