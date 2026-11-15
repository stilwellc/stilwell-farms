// Stilwell Farms -- tiny sound effects on the GBC APU (banked)
#pragma bank 255
#include <gb/gb.h>
#include "sfx.h"
#include "game.h"
BANKREF(sfx)

void sfx_init(void) BANKED {
    NR52_REG = 0x80;
    NR50_REG = 0x77;
    NR51_REG = 0xFF;
}

static void sq1(uint8_t sweep, uint8_t env, uint16_t f) {
    NR10_REG = sweep; NR11_REG = 0x80 | 0x30; NR12_REG = env;
    NR13_REG = (uint8_t)f; NR14_REG = 0xC0 | (uint8_t)(f >> 8);
}
static void sq2(uint8_t env, uint16_t f, uint8_t len) {
    NR21_REG = 0x40 | len; NR22_REG = env;
    NR23_REG = (uint8_t)f; NR24_REG = 0xC0 | (uint8_t)(f >> 8);
}
static void noise(uint8_t env, uint8_t poly, uint8_t len) {
    NR41_REG = len; NR42_REG = env; NR43_REG = poly; NR44_REG = 0xC0;
}

void sfx(uint8_t id) BANKED {
    if (!G.opt_sfx) return;
    switch (id) {
    case SFX_CURSOR: case SFX_TEXT: case SFX_BUMP: music_hold(1, 10); break;
    case SFX_HIT: case SFX_CHOP: case SFX_WATER: case SFX_STEP:
    case SFX_STEP_WOOD: case SFX_STEP_GRAVEL: case SFX_STEP_SAND: music_hold(3, 12); break;
    case SFX_SIZZLE: music_hold(3, 24); break;
    default: music_hold(0, 20); break;
    }
    switch (id) {
    case SFX_CURSOR: sq2(0x52, 0x7A0, 0x38); break;
    case SFX_CONFIRM: sq1(0x15, 0x72, 0x6C0); break;
    case SFX_CANCEL: sq1(0x1D, 0x62, 0x700); break;
    case SFX_TEXT: sq2(0x21, 0x790, 0x3C); break;
    case SFX_HIT: noise(0xA2, 0x56, 0x30); break;
    case SFX_CHOP: noise(0xC2, 0x6C, 0x30); break;
    case SFX_WATER: noise(0x62, 0x22, 0x20); break;
    case SFX_COIN: sq1(0x16, 0xA3, 0x780); break;
    case SFX_BUMP: sq2(0x42, 0x200, 0x38); break;
    case SFX_SLEEP: sq1(0x7F, 0x87, 0x500); break;
    case SFX_STEP: noise(0x31, 0x40, 0x3C); break;
    case SFX_SPLASH: noise(0xA3, 0x34, 0x20); break;
    case SFX_STEP_WOOD: noise(0x31, 0x5E, 0x3C); break;     // hollow knock on planks
    case SFX_STEP_GRAVEL: noise(0x41, 0x23, 0x38); break;   // short crunch
    case SFX_STEP_SAND: noise(0x23, 0x14, 0x34); break;     // soft shushing splash
    case SFX_SIZZLE: noise(0x74, 0x12, 0x00); break;        // a long soft hiss in the pan
    }
}
