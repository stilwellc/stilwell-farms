// Stilwell Farms -- what an animal is: six kinds, four breeds, a size, a temperament, and sometimes a sparkle (banked)
//
// Kinds: cow, hen, sheep (bought in town), goat, duck, pig (the Crossroads Market). kind % 3 is how it lives:
//   0 milker (cow, goat): gives every fed day, lives in the barn
//   1 layer  (hen, duck): gives every fed day, lives in the coop
//   2 slow   (sheep, pig): gives every WOOL_DAYS fed days (a fleece, a truffle), lives in the barn
// Traits (Animal.trait): size (bits 0-1: normal, big, runt), temperament (bits 2-3: calm, shy, playful, proud),
// a baby's care days (bits 4-6), sparkle (bit 7). Animal.gen counts generations born on the farm.
//   big gives one more, a runt one fewer (never none); a sparkle always gives gold quality and sells for five times
//   calm minds a missed meal less; shy needs petting or it frets; playful loves it twice as much; proud is content
//   whenever fed
// Babies: born only to a PAIRED couple of one kind in one building (the panel's menu), both adult and happy.
//   breed from the parents (as before), size and temperament usually from one of them, sometimes new, and one in
//   64 sparkles. Pet a baby most days while it grows (7 days) and it grows up big; ignore it and it's a runt.
// The Herd Book: every kind x breed you've ever owned, and each kind's sparkle. A row pays G5000, the book G50000.
//
// Save: Animal.trait / .gen, AF_PAIR in flags, G.herd[4] (bit kind * 4 + breed; bits 24-29 sparkles), G.herd_done.
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(anim_item)

static const uint8_t item_of[NUM_KINDS] = {I_MILK, I_EGG, I_WOOL, I_GOATMILK, I_DUCKEGG, I_TRUFFLE};
static const char *const kind_w[NUM_KINDS] = {"cow", "hen", "sheep", "goat", "duck", "pig"};
static const char *const baby_w[NUM_KINDS] = {"calf", "chick", "lamb", "kid", "duckling", "piglet"};
static const char *const breed_w[NUM_KINDS][4] = {
    {"Holstein", "Jersey", "Golden", "Highland"}, {"White", "Brown", "Golden", "Highland"},
    {"Merino", "Cocoa", "Golden", "Highland"}, {"Alpine", "Nubian", "Golden", "Highland"},
    {"Pekin", "Mallard", "Golden", "Highland"}, {"Pink", "Spotted", "Golden", "Highland"}};
static const uint16_t worth[NUM_KINDS][4] = {{750, 2250, 3250, 3500}, {200, 600, 850, 900}, {900, 2700, 3900, 4000},
                                            {1250, 3750, 5400, 5800}, {300, 900, 1300, 1400}, {1500, 4500, 6500, 7000}};
static const char *const size_w[3] = {"", "Big ", "Runt "};
static const char *const temper_w[4] = {"calm", "shy", "playful", "proud"};

uint8_t anim_item(uint8_t an) BANKED { return item_of[G.anim[an].kind % NUM_KINDS]; }

// what a collection gives: (quality << 4) | count
uint8_t anim_yield(uint8_t an) BANKED {
    Animal *a = &G.anim[an];
    uint8_t br = BREED(a), n = br ? 2 : 1, sz = a->trait & TR_SIZE;
    uint8_t q = (br == R_LEGEND || (a->trait & TR_SPARKLE)) ? 2 : a->happy > 200 ? 2 : a->happy > 120 ? 1 : 0;
    if (sz == SZ_BIG) n++;
    else if (sz == SZ_RUNT && n > 1) n--;
    return (q << 4) | n;
}

// collect into the bag; sbuf = "+2 Milk" with its star; -> count
uint8_t anim_collect(uint8_t an) BANKED {
    uint8_t y = anim_yield(an), it = anim_item(an), q = y >> 4, n = y & 15;
    inv_add(it, q, n);
    G.anim[an].flags &= ~AF_READY;
    s_clr(); s_ch('+'); s_num(n, 0); s_ch(' '); s_add(items[it].name);
    if (q) s_ch(q == 2 ? (char)0xF1 : (char)0xF0);
    return n;
}

void s_kind(uint8_t kind) BANKED { s_add(kind_w[kind % NUM_KINDS]); }
void s_produce(uint8_t kind) BANKED { s_add(items[item_of[kind % NUM_KINDS]].name); }

// "Jersey cow" / for a newborn "rare calf", into sbuf
void s_breed(uint8_t an, uint8_t baby) BANKED {
    Animal *a = &G.anim[an];
    uint8_t k = a->kind % NUM_KINDS;
    if (a->trait & TR_SPARKLE) s_add("SPARKLING ");
    s_add(breed_w[k][BREED(a)]); s_ch(' ');
    s_add(baby ? baby_w[k] : kind_w[k]);
}

// "Big playful G2", into sbuf
void s_traits(uint8_t an) BANKED {
    Animal *a = &G.anim[an];
    s_add(size_w[a->trait & TR_SIZE]);
    s_add(temper_w[(a->trait & TR_TEMPER) >> 2]);
    if (a->gen) { s_add(" G"); s_num(a->gen, 0); }
}

uint16_t anim_worth(uint8_t an) BANKED {
    Animal *a = &G.anim[an];
    uint16_t v = worth[a->kind % NUM_KINDS][BREED(a)];
    if (a->happy >= 200) v += v >> 2;
    if ((a->trait & TR_SIZE) == SZ_BIG) v += v >> 2;
    if (a->age < ADULT_AGE) v >>= 1;
    if (a->trait & TR_SPARKLE) v = v > 13000 ? 65000 : v * 5;
    return v;
}

// ---------------------------------------------------------------- the Herd Book
void herd_mark(uint8_t an) BANKED {
    Animal *a = &G.anim[an];
    uint8_t k = a->kind % NUM_KINDS, b = k * 4 + BREED(a);
    if (a->kind == 0xFF) return;
    G.herd[b >> 3] |= 1 << (b & 7);
    if (a->trait & TR_SPARKLE) G.herd[3] |= 1 << k;
}

void herd_all(void) BANKED {
    uint8_t k;
    for (k = 0; k < MAX_ANIMALS; k++) herd_mark(k);
}

static uint8_t herd_has(uint8_t b) { return G.herd[b >> 3] & (1 << (b & 7)); }

void herd_page(void) BANKED {
    uint8_t k, b, n = 0, row;
    herd_all();
    ui_begin();
    ui_box(0, 0, 20, 17);
    ui_title(2, 0, "HERD BOOK");
    ui_text(8, 2, "1 2 3 4  \xF1");
    for (k = 0; k < NUM_KINDS; k++) {
        ui_text(1, 4 + k * 2, kind_w[k]);
        row = 1;
        for (b = 0; b < 4; b++) {
            if (herd_has(k * 4 + b)) { ui_char(8 + b * 2, 4 + k * 2, GL_IT_STAR); n++; }
            else { ui_char(8 + b * 2, 4 + k * 2, font_map['.' - 32]); row = 0; }
        }
        if (G.herd[3] & (1 << k)) { ui_char(17, 4 + k * 2, GL_IT_STAR2); n++; }
        else ui_char(17, 4 + k * 2, font_map['.' - 32]);
        if (row && !(G.herd_done & (1 << k))) {   // all four breeds of a kind: the breeders' prize
            G.herd_done |= 1 << k;
            G.gold += 5000;
            s_clr(); s_add("Every breed of "); s_add(kind_w[k]); s_add("! The breeders' prize: G5000.");
            ui_end();
            music_play(MUS_JINGLE_FANFARE); say(sbuf); music_for_scene(); hud_draw();
            herd_page();
            return;
        }
    }
    s_clr(); s_num(n, 0); s_add(" of 30"); ui_text(1, 15, sbuf);
    if (n == 30 && !(G.herd_done & 0x80)) {
        G.herd_done |= 0x80;
        G.gold += 50000;
        ui_end();
        music_play(MUS_JINGLE_FANFARE);
        say("The Herd Book is full! Every breed, every sparkle. The county's never seen the like: G50000!");
        music_for_scene(); hud_draw();
        return;
    }
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); break; }
        wait_vbl_done();
    }
    ui_end();
}

// ---------------------------------------------------------------- babies
// a newborn in slot c from mom and dad: its traits (breed was set by the caller)
void anim_born(uint8_t c, uint8_t mom, uint8_t dad) BANKED {
    Animal *a = &G.anim[c], *m = &G.anim[mom], *d = &G.anim[dad];
    uint8_t r = rnd(), t;
    t = ((r & 1) ? m->trait : d->trait) & TR_TEMPER;        // temperament from one parent...
    if ((r & 0x0E) == 0) t = rnd() & TR_TEMPER;             // ...or, one time in eight, its own
    if ((m->trait & TR_SPARKLE) || (d->trait & TR_SPARKLE)) { if (rnd() < 32) t |= TR_SPARKLE; }   // one in eight from a sparkling parent
    else if (rnd() < 4) t |= TR_SPARKLE;                    // one in 64 out of nowhere
    a->trait = t;                                           // size is decided by how it is raised
    a->gen = (m->gen > d->gen ? m->gen : d->gen) + 1;
    if (!a->gen) a->gen = 255;
    herd_mark(c);
}

// one animal's night, before its daily flags are cleared: temperament, and a baby's care
void anim_night(uint8_t an) BANKED {
    Animal *a = &G.anim[an];
    uint8_t t = (a->trait & TR_TEMPER) >> 2, fed = a->flags & AF_FED, pet = a->flags & AF_PET;
    if (t == 0 && !fed && a->happy < 240) a->happy += 15;                // calm: a missed meal costs half
    else if (t == 1 && !pet) a->happy = a->happy > 5 ? a->happy - 5 : 0;    // shy: frets when ignored
    else if (t == 2 && pet && a->happy < 245) a->happy += 10;            // playful: petting counts double
    else if (t == 3 && fed && a->happy < 250) a->happy += 5;             // proud: content when fed
    if (a->age < ADULT_AGE && a->gen) {
        if (pet && fed && (a->trait & TR_CARE) != TR_CARE) a->trait += 0x10;
        if (a->age == ADULT_AGE - 1) {                                    // grown tomorrow: how was it raised?
            uint8_t care = (a->trait & TR_CARE) >> 4;
            a->trait = (a->trait & ~(TR_SIZE | TR_CARE)) | (care >= 5 ? SZ_BIG : care <= 1 ? SZ_RUNT : 0);
        }
    }
}
