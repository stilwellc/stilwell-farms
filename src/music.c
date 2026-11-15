// Stilwell Farms -- tiny 4-channel music driver, ticked from the VBL interrupt (bank 0)
#include <gb/gb.h>
#include "game.h"
BANKREF_EXTERN(music_get)

extern const uint16_t sq_freq[84];   // in the music data bank (mapped while notes play)
extern const uint16_t wv_freq[84];

uint8_t music_on;
uint8_t music_cur = 0xFF;          // last song started (main.c: music_for_scene keeps a song that is already playing)
static SongDef song;
static uint8_t song_bank, fc;
static const uint8_t *ptr[4];
static uint8_t left[4], vol[4], hold[4];

void music_hold(uint8_t ch, uint8_t frames) { hold[ch] = frames; }

void music_stop(void) {
    music_on = 0;
    NR12_REG = 0; NR14_REG = 0x80;
    NR22_REG = 0; NR24_REG = 0x80;
    NR32_REG = 0;
    NR42_REG = 0; NR44_REG = 0x80;
}

void music_play(uint8_t id) {
    uint8_t c, save;
    music_stop();
    music_cur = id;
    if (id == MUS_NONE || !G.opt_music) return;
    music_get(id, &song);
    song_bank = BANK(music_get);
    save = _current_bank;
    music_wave(song.wave);
    SWITCH_ROM(save);
    for (c = 0; c < 4; c++) { ptr[c] = song.ch[c]; left[c] = 0; }
    vol[0] = song.env1 >> 4; vol[1] = song.env2 >> 4;
    fc = 0xFE;
    music_on = 1;
}

static void note(uint8_t c, uint8_t n) {
    if (hold[c]) return;
    switch (c) {
    case 0:
        if (!n) { NR12_REG = 0; NR14_REG = 0x80; break; }
        NR10_REG = 0; NR11_REG = song.duty1 << 6; NR12_REG = (vol[0] << 4) | (song.env1 & 7);
        NR13_REG = (uint8_t)sq_freq[n - 1]; NR14_REG = 0x80 | (uint8_t)(sq_freq[n - 1] >> 8);
        break;
    case 1:
        if (!n) { NR22_REG = 0; NR24_REG = 0x80; break; }
        NR21_REG = song.duty2 << 6; NR22_REG = (vol[1] << 4) | (song.env2 & 7);
        NR23_REG = (uint8_t)sq_freq[n - 1]; NR24_REG = 0x80 | (uint8_t)(sq_freq[n - 1] >> 8);
        break;
    case 2:
        if (!n) { NR32_REG = 0; break; }
        NR30_REG = 0x80; NR32_REG = song.wvol << 5;
        NR33_REG = (uint8_t)wv_freq[n - 1]; NR34_REG = 0x80 | (uint8_t)(wv_freq[n - 1] >> 8);
        break;
    default:
        if (!n) break;
        NR41_REG = 0x30;
        if (n == 1) { NR42_REG = 0xA1; NR43_REG = 0x71; }
        else if (n == 2) { NR42_REG = 0x92; NR43_REG = 0x52; }
        else if (n == 3) { NR42_REG = 0x41; NR43_REG = 0x11; NR41_REG = 0x3A; }
        else { NR42_REG = 0x52; NR43_REG = 0x12; }
        NR44_REG = 0xC0;
        break;
    }
}

void music_tick(void) {
    uint8_t c, save, done = 0;
    for (c = 0; c < 4; c++) if (hold[c]) hold[c]--;
    if (!music_on) return;
    if (++fc < song.tempo && fc != 0xFF) return;
    fc = 0;
    save = _current_bank;
    SWITCH_ROM(song_bank);
    for (c = 0; c < 4; c++) {
        if (!ptr[c]) continue;
        if (left[c]) { left[c]--; if (left[c]) continue; }
        while (1) {
            uint8_t b = *ptr[c];
            if (b == 0xFE) { vol[c] = ptr[c][1]; ptr[c] += 2; continue; }
            if (b == 0xFF) {
                if (!song.loop || ptr[c] == song.ch[c]) { done = !song.loop; ptr[c] = song.loop ? ptr[c] : 0; break; }
                ptr[c] = song.ch[c];
                continue;
            }
            note(c, b);
            left[c] = ptr[c][1];
            ptr[c] += 2;
            break;
        }
    }
    SWITCH_ROM(save);
    if (done) music_stop();
}
