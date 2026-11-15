#ifndef SFX_H
#define SFX_H
#include <stdint.h>
enum { SFX_CURSOR, SFX_CONFIRM, SFX_CANCEL, SFX_TEXT, SFX_HIT, SFX_CHOP, SFX_WATER, SFX_COIN, SFX_BUMP, SFX_SLEEP, SFX_STEP, SFX_SPLASH,
       SFX_STEP_WOOD, SFX_STEP_GRAVEL, SFX_STEP_SAND, SFX_SIZZLE };
#include <gb/gb.h>
void sfx_init(void) BANKED;
void sfx(uint8_t id) BANKED;
#endif
