// Stilwell Farms -- battery saves: one slot, the farm world in cart SRAM, older saves carried forward (banked)
//
// SRAM map (MBC5, 4 banks of 8KB at 0xA000 = 32KB, all a ModRetro cartridge has. The first releases declared
// 128KB and used banks 0..6; on a 32KB cartridge banks 4..6 wrap onto 0..2, so a save overwrote its own
// header and never loaded again):
//   bank 0      the save's GameState at 0xA000 (under 4KB), the saved world's a2[4096] at 0xB000
//   bank 1      live world cell[4096] at 0xA000, a1[4096] at 0xB000  (WB_LIVE: mapped during play)
//   bank 2      live world a2[4096] at 0xA000                         (WB_A2); 0xB000 is free
//   bank 3      the saved world's cell[4096] at 0xA000, a1[4096] at 0xB000  (WB_SAVED)
// Live world + one saved world + the state = 28KB, so there is room for exactly one save.
// RAM stays enabled with WB_LIVE mapped; everything here that maps another bank puts WB_LIVE back.
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include <stddef.h>
#include "game.h"

BANKREF(save_write)

uint8_t save_slot;
uint8_t save_info[3];   // season, day, year of the slot save_valid() looked at last (title screen)
#define SLOT(s) ((uint8_t *)0xA000)
#define WB_SAVED 3
typedef char gamestate_fits_4k[(sizeof(GameState) <= 0x1000) ? 1 : -1];   // the saved a2 starts at 0xB000

// s = rotl1(s) + byte over the whole state. SDCC's C loop spent ~40 cycles a byte; this is 16.
// sdcccall(1): p in DE, n in BC, result in BC.
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

// ---------------------------------------------------------------- world copies (SRAM bank to bank)
// Both sides sit in the one 0xA000 window, so a world moves 256 bytes at a time through a stack buffer.
// Every byte that lands is folded into a two-byte running sum (lo += byte, hi += lo): the world's
// checksum, kept in the slot's GameState.
static uint16_t wsum;

// 256 bytes from an SRAM page (src, 256-aligned) into buf: de = buf, bc = src
static void page_in(uint8_t *buf, const uint8_t *src) __naked {
    (void)buf; (void)src;
    __asm
        ld   h, b
        ld   l, c
    20$:
        ld   a, (hl+)
        ld   (de), a
        inc  de
        ld   a, (hl+)
        ld   (de), a
        inc  de
        ld   a, (hl+)
        ld   (de), a
        inc  de
        ld   a, (hl+)
        ld   (de), a
        inc  de
        ld   a, (hl+)
        ld   (de), a
        inc  de
        ld   a, (hl+)
        ld   (de), a
        inc  de
        ld   a, (hl+)
        ld   (de), a
        inc  de
        ld   a, (hl+)
        ld   (de), a
        inc  de
        ld   a, l
        or   a, a
        jr   nz, 20$
        ret
    __endasm;
}

// buf -> an SRAM page (dst, 256-aligned), summing: de = dst, bc = buf
static void page_out(uint8_t *dst, const uint8_t *buf) __naked {
    (void)dst; (void)buf;
    __asm
        ld   h, b
        ld   l, c
        ld   a, (_wsum)
        ld   c, a
        ld   a, (_wsum + 1)
        ld   b, a
    30$:
        ld   a, (hl+)
        ld   (de), a
        inc  e
        add  a, c
        ld   c, a
        add  a, b
        ld   b, a
        ld   a, (hl+)
        ld   (de), a
        inc  e
        add  a, c
        ld   c, a
        add  a, b
        ld   b, a
        ld   a, (hl+)
        ld   (de), a
        inc  e
        add  a, c
        ld   c, a
        add  a, b
        ld   b, a
        ld   a, (hl+)
        ld   (de), a
        inc  e
        add  a, c
        ld   c, a
        add  a, b
        ld   b, a
        ld   a, e
        or   a, a
        jr   nz, 30$
        ld   hl, #_wsum
        ld   (hl), c
        inc  hl
        ld   (hl), b
        ret
    __endasm;
}

// the same sum over an SRAM page, nothing copied: de = page (256-aligned)
static void page_sum(const uint8_t *p) __naked {
    (void)p;
    __asm
        ld   h, d
        ld   l, e
        ld   a, (_wsum)
        ld   c, a
        ld   a, (_wsum + 1)
        ld   b, a
    40$:
        ld   a, (hl+)
        add  a, c
        ld   c, a
        add  a, b
        ld   b, a
        ld   a, (hl+)
        add  a, c
        ld   c, a
        add  a, b
        ld   b, a
        ld   a, l
        or   a, a
        jr   nz, 40$
        ld   hl, #_wsum
        ld   (hl), c
        inc  hl
        ld   (hl), b
        ret
    __endasm;
}

// A world w (WB_LIVE or WB_SAVED) = bank w: cell + a1 (0xA000..0xBFFF), plus a2: the live one in bank WB_A2
// at 0xA000, the saved one in bank 0 at 0xB000.
// world_xfer copies world src over world dst; with dst 0 it only sums src. Returns the sum.
#define A2_BANK(w) ((w) == WB_LIVE ? WB_A2 : 0)
#define A2_BASE(w) ((w) == WB_LIVE ? 0xA000 : 0xB000)
static uint16_t world_xfer(uint8_t dst, uint8_t src) {
    uint8_t buf[256];
    uint16_t a, sa = A2_BASE(src), da = A2_BASE(dst);
    wsum = 0;
    for (a = 0xA000; a != 0xC000; a += 256) {
        SWITCH_RAM(src);
        if (!dst) { page_sum((const uint8_t *)a); continue; }
        page_in(buf, (const uint8_t *)a);
        SWITCH_RAM(dst);
        page_out((uint8_t *)a, buf);
    }
    for (a = 0; a != 0x1000; a += 256) {
        SWITCH_RAM(A2_BANK(src));
        if (!dst) { page_sum((const uint8_t *)(sa + a)); continue; }
        page_in(buf, (const uint8_t *)(sa + a));
        SWITCH_RAM(A2_BANK(dst));
        page_out((uint8_t *)(da + a), buf);
    }
    SWITCH_RAM(WB_LIVE);
    return wsum;
}

// ---------------------------------------------------------------- saving
void save_write(void) BANKED {
    uint8_t *hdr = SLOT(save_slot);
    G.magic = SAVE_MAGIC;
    G.version = SAVE_VERSION;
    SWITCH_RAM(0);
    hdr[0] = 0;   // the slot reads as empty while its world is half copied (a torn save is never a mix)
    G.world_sum = world_xfer(WB_SAVED, WB_LIVE);
    G.checksum = sum((const uint8_t *)&G, sizeof(G) - 2);
    SWITCH_RAM(0);
    memcpy(hdr, &G, sizeof(G));
    SWITCH_RAM(WB_LIVE);
}

// ---------------------------------------------------------------- older saves
// v2 layout (the first release): one farm, fixed buildings, flat inventory
#define V2_SIZE 846
static uint8_t v2_ok(const uint8_t *p) {
    return p[0] == (SAVE_MAGIC & 0xFF) && p[1] == (SAVE_MAGIC >> 8) && p[2] == 2 &&
           (p[V2_SIZE - 2] | (p[V2_SIZE - 1] << 8)) == sum(p, V2_SIZE - 2);
}

// v3 and v4 kept the 32x32 world in the save: magic, version, cell[1024], a1[1024], a2[1024], a 16-bit plot
// mask, then the fields from bld on as GameState has them now (OLD() maps an offset before inv), except
// that inv held 40 items (v3: 39, no wool) and shipped 6 bytes; the items added since sit at the end of
// both, so old saves copy in three runs around them. v4 ended with seed_rar + checksum. v3 ended after
// rng_hi, with seeds without rarity.
#define V4_BLD (3 + 3072 + 2)
#define OLD(f) (offsetof(GameState, f) - offsetof(GameState, bld) + V4_BLD)
#define RUN_A (offsetof(GameState, inv) - offsetof(GameState, bld))        // bld .. tomorrow
#define V4_INV (40 * 3)
#define RUN_B (offsetof(GameState, shipped) - offsetof(GameState, seeds))  // seeds .. gifted
#define OLD_SHIP 6
#define RUN_C (offsetof(GameState, seed_rar) + 1 - offsetof(GameState, goals))   // goals .. seed_rar
#define V4_TAIL (RUN_A + V4_INV + RUN_B + OLD_SHIP + RUN_C)
#define V4_SIZE (V4_BLD + V4_TAIL + 2)
#define V3_INV (39 * 3)
#define V3_SEEDS (NUM_CROPS + 1)
#define V3_S1 (offsetof(GameState, shipped) - offsetof(GameState, saplings))     // saplings .. gifted
#define V3_S2 (offsetof(GameState, furn_init) - offsetof(GameState, goals))      // goals .. rng_hi
#define V3_TAIL (V3_S1 + OLD_SHIP + V3_S2)
#define V3_SIZE (OLD(inv) + V3_INV + V3_SEEDS + V3_TAIL + 2)
static uint8_t old_ok(const uint8_t *p, uint8_t v, uint16_t n) {
    return p[2] == v && (p[n - 2] | (p[n - 1] << 8)) == sum(p, n - 2);
}

// v5 and v6 had today's fields in today's order, but 70 items (inv 9 bytes shorter; shipped and donated a byte
// shorter each) and 11-byte animals. v5 ends at the field before anim (its 12 animals sit in anim_v5); v6 goes on
// with 36 animals and the late-game block. Both load in runs around the arrays that grew.
#define O_ITEMS 70
#define O_BITS ((O_ITEMS + 7) / 8)
#define O_A (offsetof(GameState, inv) + O_ITEMS * 3)                                   // run A: magic .. inv
#define O_LB (offsetof(GameState, shipped) - offsetof(GameState, seeds) + O_BITS)      // run B: seeds .. shipped
#define O_LC (offsetof(GameState, anim) - offsetof(GameState, goals))                  // run C: goals .. before anim
#define O_ANIMS (O_A + O_LB + O_LC)
#define V5_SIZE (O_ANIMS + 2)
#define V6_ANIMALS 36
#define O_LD1 (offsetof(GameState, donated) - offsetof(GameState, review_year) + O_BITS)   // review_year .. donated
#define O_LD2 (offsetof(GameState, checksum) - offsetof(GameState, museum_sets))           // museum_sets .. spare
#define V6_SIZE (O_ANIMS + V6_ANIMALS * OLD_ANIMAL + O_LD1 + O_LD2 + 2)
#define O_WSUM (O_A + O_LB + offsetof(GameState, world_sum) - offsetof(GameState, goals))

// n old 11-byte animals into the list the game uses now; the rest of the list is empty
static void anims_in(const uint8_t *q, uint8_t n) {
    uint8_t k;
    memset(G.anim, 0, sizeof(G.anim));
    for (k = 0; k < MAX_ANIMALS; k++) {
        if (k < n) memcpy(&G.anim[k], q + (uint16_t)k * OLD_ANIMAL, OLD_ANIMAL);
        else G.anim[k].kind = 0xFF;
    }
}

// saves older than v6 keep their animals in anim_v5, and know nothing of the years after the valley
static void anim_carry(void) {
    anims_in(G.anim_v5, V5_ANIMALS);
    memset(&G.review_year, 0, offsetof(GameState, checksum) - offsetof(GameState, review_year));
    if (G.goals & (1UL << 31)) G.review_year = G.year;   // this year's review was already read
}

static void migrate_56(const uint8_t *p, uint8_t v) {
    memset(&G, 0, sizeof(G));
    memcpy(&G, p, O_A);
    memcpy(G.seeds, p + O_A, O_LB);
    memcpy(&G.goals, p + O_A + O_LB, O_LC);
    if (v == 5) anim_carry();
    else {
        const uint8_t *q = p + O_ANIMS + V6_ANIMALS * OLD_ANIMAL;
        anims_in(p + O_ANIMS, V6_ANIMALS);
        memcpy(&G.review_year, q, O_LD1);
        memcpy(&G.museum_sets, q + O_LD1, O_LD2);
        G.herd[0] = G.herd[1] = G.herd[2] = G.herd[3] = G.herd_done = 0;   // (were spare, and zero; be sure)
    }
}

// 0 none, 2 old v2 farm, 3 .. 6 a v3 .. v6 save (carried forward on load), 7 current. Leaves bank 0 mapped.
static uint8_t check(uint8_t slot) {
    const uint8_t *p = SLOT(slot);
    const GameState *s = (const GameState *)p;
    SWITCH_RAM(0);
    if (slot || s->magic != SAVE_MAGIC) return 0;   // one slot
    if (s->version == SAVE_VERSION && s->checksum == sum(p, sizeof(GameState) - 2)) {
        uint16_t ws = s->world_sum;
        ws = world_xfer(0, WB_SAVED) == ws;
        SWITCH_RAM(0);
        return ws ? 7 : 0;
    }
    if (old_ok(p, 6, V6_SIZE) || old_ok(p, 5, V5_SIZE)) {
        uint16_t ws = p[O_WSUM] | (p[O_WSUM + 1] << 8);
        uint8_t v = p[2];
        ws = world_xfer(0, WB_SAVED) == ws;
        SWITCH_RAM(0);
        return ws ? v : 0;
    }
    if (old_ok(p, 4, V4_SIZE)) return 4;
    if (old_ok(p, 3, V3_SIZE)) return 3;
    if (slot == 0 && v2_ok(p)) return 2;
    return 0;
}

uint8_t save_valid(uint8_t slot) BANKED {
    uint8_t v = check(slot);
    const uint8_t *p = SLOT(slot);
    if (v >= 5) { save_info[0] = p[offsetof(GameState, season)]; save_info[1] = p[offsetof(GameState, day)]; save_info[2] = p[offsetof(GameState, year)]; }
    else if (v >= 3) { save_info[0] = p[OLD(season)]; save_info[1] = p[OLD(day)]; save_info[2] = p[OLD(year)]; }
    SWITCH_RAM(WB_LIVE);
    return v;
}

static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

// rebuild a v2 farm's progress in the new world: money, goods, seeds, tools, house, animals
static void migrate_v2(const uint8_t *p) {
    static const uint8_t inv_map[11] = {I_RADISH, I_CARROT, I_STRAWB, I_TOMATO, I_CORN, I_PUMPKIN, I_BEET, I_MILK, I_EGG, I_HONEY, I_APPLE};
    static const uint8_t seed_map[8] = {0, 1, 2, 3, 5, 6, 10, 11};
    uint8_t k, cows, hens, up, b;
    world_new();
    SWITCH_RAM(0);   // (world_new left the live world mapped)
    cows = p[801]; hens = p[802]; up = p[796];
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
    SWITCH_RAM(WB_LIVE);
}

// a v3/v4 farm in the 64x64 valley: a fresh valley first, then the old state on top, and the old 32x32
// world's owned plots cell for cell into the middle (old plot (px, py) is new plot (px + 2, py + 2)).
// Unowned old land is left as the new valley draws it. Farm coordinates shift by FARM_OX.
static void migrate_old(const uint8_t *p, uint8_t v) {
    uint8_t x, y, k, row[96];
    uint16_t op;
    world_new();
    SWITCH_RAM(0);
    memcpy(&G.bld, p + V4_BLD, RUN_A);
    anim_carry();
    if (v == 4) {
        const uint8_t *q = p + V4_BLD + RUN_A;
        memcpy(G.inv, q, V4_INV); q += V4_INV;
        memcpy(G.seeds, q, RUN_B); q += RUN_B;
        memcpy(G.shipped, q, OLD_SHIP); q += OLD_SHIP;
        memcpy(&G.goals, q, RUN_C);
    } else {
        const uint8_t *q = p + OLD(inv) + V3_INV;
        memcpy(G.inv, p + OLD(inv), V3_INV);
        for (k = 0; k < V3_SEEDS; k++) G.seeds[k][R_COMMON] = q[k];
        q += V3_SEEDS;
        memcpy(G.saplings, q, V3_S1); q += V3_S1;
        memcpy(G.shipped, q, OLD_SHIP); q += OLD_SHIP;
        memcpy(&G.goals, q, V3_S2);
        if (G.scene != SCENE_FARM) G.scene = MAP_TOWN;   // the only v3 map you could save in
    }
    if (G.scene != SCENE_FARM && G.scene > MAP_TOWN) G.scene = MAP_TOWN;   // (old map ids are 0..4)
    op = rd16(p + V4_BLD - 2);
    for (k = 0; k < 16; k++)
        if (op & ((uint16_t)1 << k)) G.plots[(k >> 2) + 2] |= (uint8_t)(1 << ((k & 3) + 2));
    for (y = 0; y < 32; y++) {
        SWITCH_RAM(0);
        memcpy(row, p + 3 + (y << 5), 32);
        memcpy(row + 32, p + 3 + 1024 + (y << 5), 32);
        memcpy(row + 64, p + 3 + 2048 + (y << 5), 32);
        SWITCH_RAM(WB_LIVE);
        for (x = 0; x < 32; x++) {
            uint16_t i = WIDX(x + FARM_OX, y + FARM_OX);
            if (!owned(x + FARM_OX, y + FARM_OX)) continue;
            W_CELL[i] = row[x]; W_A1[i] = row[32 + x]; W_A2_SET(i, row[64 + x]);
        }
    }
    for (k = 0; k < MAX_BLD; k++)
        if (G.bld[k].type != 0xFF) { G.bld[k].x += FARM_OX; G.bld[k].y += FARM_OX; }
    if (G.scene == SCENE_FARM) { G.px += FARM_OX; G.py += FARM_OX; }
    G.magic = SAVE_MAGIC;
    G.version = SAVE_VERSION;
}

uint8_t save_load(uint8_t slot) BANKED {
    uint8_t v = check(slot);
    if (v == 7) {
        memcpy(&G, SLOT(slot), sizeof(G));
        world_xfer(WB_LIVE, WB_SAVED);
    } else if (v >= 5) {
        migrate_56(SLOT(slot), v);
        G.version = SAVE_VERSION;
        world_xfer(WB_LIVE, WB_SAVED);
    } else if (v == 4 || v == 3) migrate_old(SLOT(slot), v);
    else if (v == 2) migrate_v2(SLOT(slot));
    SWITCH_RAM(WB_LIVE);
    save_slot = 0;
    if (v == 2) G.scene = SCENE_FARM;
    if (v) herd_all();   // the Herd Book knows whoever is in the barns
    return v != 0;
}
