// Stilwell Farms -- the wider country comes alive: standing, news, errands, offices, the almanac (banked)
//
// Standing   gold you have sold at a place's post, in 256s (G.rep[place], capped). Stars at about G4,000,
//            G16,000 and G41,000: each star is +5 % on that post's prices; two stars let you open an office.
// Glut       regions.c: a big load floods a market (a nibble per place and kind of good); it eases a notch a night.
// News       every day one place pays half as much again for one kind of good (a hash of the date: nothing saved).
//            The TV says where. Each place also has a fair day a season: +30 % on everything there.
// Carrier    each week one place wants a few of a good another place sells: deliver them for four times the price.
// Errands    the six stagecoach places each have a broken landmark. At one star the keeper asks for help: two loads
//            of goods and a sum of gold. Done, the landmark is mended for good (areas.c swaps the tiles) and the
//            post pays 10 % more for ever.
// Offices    at two stars, G40,000 opens your office there: from then on goods shipped from HOME sell at that
//            place's rate when it beats the plain price (daily.c sell_value). The late game's gold has a use.
// Almanac    one page: every place, seen or not, stars, errand, office. All of it done: G100,000.
//
// Save: G.rep[11], G.glut[61], G.errand[3] (2 bits a place; bit 22 = almanac prize), G.office[2], G.seen[2],
// G.carry_week, G.carry_done. All valid at zero.
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(region_stars)

#define NPLACE 11
static const char *const place_w[NPLACE] = {"Shell Island", "the Market", "the Pasture", "the Quarry", "the Old Woods", "Amberwood",
                                           "the Harbour", "Lakeshore", "the Mesa", "Pinecrest", "Stonegate"};
static const char *const group_w[11] = {"crops", "fruit", "farm goods", "wild finds", "river fish", "ore", "gems", "preserves",
                                       "sea fish", "beach finds", "cooked dishes"};
static uint8_t mix(uint8_t v) { v ^= v << 3; v ^= v >> 5; v += 0x9B; v ^= v << 2; return v; }
static uint8_t today(void) { return (uint8_t)(G.season * 28 + G.day) ^ (uint8_t)(G.year * 37); }

uint8_t region_stars(uint8_t a) BANKED { return G.rep[a] >= 160 ? 3 : G.rep[a] >= 64 ? 2 : G.rep[a] >= 16 ? 1 : 0; }

void region_sold(uint8_t a, uint32_t gold) BANKED {
    uint16_t add = (uint16_t)(gold >> 8);
    if (!add && gold >= 64) add = 1;
    G.rep[a] = (uint16_t)G.rep[a] + add > 255 ? 255 : G.rep[a] + add;
}

void region_seen(uint8_t a) BANKED { G.seen[a >> 3] |= 1 << (a & 7); }

// every night: gluts ease a notch
void regions_night(void) BANKED {
    uint8_t i, v;
    for (i = 0; i < sizeof(G.glut); i++) {
        v = G.glut[i];
        if (v & 0x0F) v--;
        if (v & 0xF0) v -= 0x10;
        G.glut[i] = v;
    }
}

// ---------------------------------------------------------------- news and fairs
uint8_t news_place(void) BANKED { return mix(today()) % NPLACE; }
uint8_t news_group(void) BANKED { return mix(mix(today()) ^ 0x5C) % 11; }

static const uint8_t fair_season[NPLACE] = {1, 0, 0, 2, 2, 2, 1, 1, 1, 3, 0};
static const uint8_t fair_day[NPLACE] = {9, 20, 6, 17, 3, 22, 20, 12, 26, 18, 11};
static const char *const fair_w[NPLACE] = {"the Shell Fair", "the Great Market", "Shearing Day", "the Stonecutters' Day", "the Mushroom Feast",
                                          "the Carvers' Fair", "the Regatta", "the Lantern Float", "the Sun Dance", "Lantern Night", "the Antiquaries' Day"};
uint8_t region_fair(uint8_t a) BANKED { return G.season == fair_season[a] && G.day == fair_day[a]; }

// the TV, after the weather: today's word from the country
void regions_tv(void) BANKED {
    uint8_t a;
    set_speaker("TV");
    s_clr(); s_add("Word from "); s_add(place_w[news_place()]); s_add(": they're paying half as much again for "); s_add(group_w[news_group()]); s_add(" today.");
    say(sbuf);
    for (a = 0; a < NPLACE; a++)
        if (region_fair(a)) {
            s_clr(); s_add("And it's "); s_add(fair_w[a]); s_add(" at "); s_add(place_w[a]); s_add("! Everything sells high there today.");
            say(sbuf);
        }
    ui_speaker = 0;
}

// at a post: the keeper mentions it when the news is about here
void region_news_line(void) BANKED {
    uint8_t a = (uint8_t)(scene - MAP_ISLAND);
    if (region_fair(a)) { s_clr(); s_add("It's "); s_add(fair_w[a]); s_add(" today! Top prices for everything."); say(sbuf); }
    else if (news_place() == a) { s_clr(); s_add("We're short of "); s_add(group_w[news_group()]); s_add(" today. I'll pay half as much again."); say(sbuf); }
}

// ---------------------------------------------------------------- the weekly carrier job
static uint8_t week(void) { return (uint8_t)(G.year * 16 + G.season * 4 + (G.day - 1) / 7) + 1; }
static uint8_t carry_to(void) { return mix(week() ^ 0xA7) % NPLACE; }
static uint8_t carry_item(void) {
    uint8_t from = (carry_to() + 1 + mix(week()) % (NPLACE - 1)) % NPLACE;
    return region_stock(from, mix(week() ^ 0x3D) % 3);
}
static uint8_t carry_qty(void) { return 3 + region_stars(carry_to()); }

uint8_t carry_here(uint8_t a) BANKED {
    if (G.carry_week != week()) { G.carry_week = week(); G.carry_done = 0; }
    return !G.carry_done && carry_to() == a;
}

void carry_do(uint8_t a) BANKED {
    uint8_t it = carry_item(), n = carry_qty();
    uint32_t pay = (uint32_t)items[it].price * n * 4;
    s_clr(); s_add("This week we need "); s_num(n, 0); s_ch(' '); s_add(items[it].name); s_add(" brought in. I'll pay G"); s_num(pay, 0); s_add(" for the lot.");
    say(sbuf);
    if (inv_count(it) < n) { say("Some other post sells it, three a day. Or grow it, catch it, dig it: I don't ask."); return; }
    if (!ui_yesno("You have them! Hand them over?")) return;
    inv_take(it, n);
    G.gold += pay; G.earned += pay;
    G.carry_done = 1;
    region_sold(a, pay * 2);
    music_play(MUS_JINGLE_FANFARE);
    say("That's the job done, and done right. Word gets round about a carrier who delivers.");
    music_for_scene();
    hud_draw();
}

// what the carrier board says, anywhere (the TV reads it on the first day of each week)
void carry_tv(void) BANKED {
    if ((G.day - 1) % 7) return;
    set_speaker("TV");
    s_clr(); s_add("Carrier wanted! "); s_add(place_w[carry_to()]); s_add(" needs "); s_num(carry_qty(), 0); s_ch(' ');
    s_add(items[carry_item()].name); s_add(" this week. Good pay.");
    say(sbuf);
    ui_speaker = 0;
}

// ---------------------------------------------------------------- errands: the six stagecoach places (5..10)
typedef struct { uint8_t it1, n1, it2, n2; uint16_t gold; } Errand;
static const Errand errands[6] = {
    {I_IRON, 12, I_GEM, 3, 8000},          // Amberwood: tools and an amethyst eye for the great totem
    {I_COAL, 15, I_GOLD, 5, 12000},        // Harbour: coal and a gold lens-ring for the lighthouse
    {I_APPLE, 10, I_IRON, 8, 6000},        // Lakeshore: (apple-wood planks) and nails for the ferry
    {I_COPPER, 15, I_IRON, 10, 10000},     // Mesa: pipe and fittings for the pump
    {I_COAL, 20, I_CIDER, 6, 9000},        // Pinecrest: coal to thaw the well, cider for the crew
    {I_COPPER, 10, I_DIAMOND, 1, 15000}};  // Stonegate: the statue's head, and a diamond for its brow
static const char *const ask[6] = {
    "The great totem's stood bare forty years. I'd carve it yet, with good iron for the tools.",
    "The light's been dark since the storm. Coal for the lamp, and we could bring ships home safe.",
    "The old ferry's holed. With planks and nails I could cross the lake again. Folk miss it.",
    "The pump ran dry before I was born. Pipe and fittings, and this whole mesa could drink.",
    "The well froze solid three winters back. Enough coal and we could thaw it out.",
    "The statue lost its head long ago. We have the pieces. We lack the copper pins to set them."};
static const char *const done[6] = {
    "It's carved! Every face in Amberwood is on that totem now. Yours is near the top.",
    "She's LIT! Look at that beam. Every captain on this coast will know who did it.",
    "She floats! First crossing's yours, any time, for nothing, for ever.",
    "WATER! Cold, clean water! I never thought I'd hear that sound out here.",
    "The well runs! No more melting snow by the bucket. Pinecrest won't forget this.",
    "Whole again, after all these centuries. We still don't know who it is. It looks grateful."};

uint8_t region_errand(uint8_t a) BANKED {      // 0..3 (3 = done); places without one are always 0
    uint8_t i;
    if (a < 5) return 0;
    i = (a - 5) * 2;
    return (G.errand[i >> 3] >> (i & 7)) & 3;
}
static void errand_set(uint8_t a, uint8_t v) {
    uint8_t i = (a - 5) * 2;
    G.errand[i >> 3] = (G.errand[i >> 3] & ~(3 << (i & 7))) | (v << (i & 7));
}
uint8_t region_errand_open(uint8_t a) BANKED { return a >= 5 && region_stars(a) >= 1 && region_errand(a) < 3; }

void region_errand_do(uint8_t a) BANKED {
    const Errand *e = &errands[a - 5];
    uint8_t st = region_errand(a), it, n;
    if (st == 0) { say(ask[a - 5]); }
    if (st < 2) {
        it = st ? e->it2 : e->it1; n = st ? e->n2 : e->n1;
        s_clr(); s_add(st ? "Next we need " : "First we need "); s_num(n, 0); s_ch(' '); s_add(items[it].name); s_ch('.');
        say(sbuf);
        if (inv_count(it) < n) return;
        if (!ui_yesno("You have enough! Give them?")) return;
        inv_take(it, n);
        errand_set(a, st + 1);
        sfx(SFX_CONFIRM);
        say(st ? "That's the materials! Now it only wants paying for." : "A fine start. I'll put the word out for hands.");
        return;
    }
    s_clr(); s_add("The work will cost G"); s_num(e->gold, 0); s_add(". The town has scraped half. Will you stand the rest?");
    if (G.gold < e->gold) { say(sbuf); return; }
    if (!ui_yesno(sbuf)) return;
    G.gold -= e->gold; G.invested += e->gold;
    errand_set(a, 3);
    region_sold(a, 16384);
    music_play(MUS_JINGLE_FANFARE);
    say(done[a - 5]);
    say("From now on this post pays you a tenth more for everything. It's the least we can do.");
    music_for_scene();
    hud_draw();
    enter_scene(scene, G.px, G.py, G.facing);      // draw the place again, mended
}

// ---------------------------------------------------------------- offices
void region_office(uint8_t a) BANKED {
    say("You've standing enough to open an office here. Your goods shipped from HOME would sell at our rates.");
    if (G.gold < 40000) { say("It costs G40000 to set up. Come back when you have it."); return; }
    if (!ui_yesno("Open your office here for G40000?")) return;
    G.gold -= 40000; G.invested += 40000;
    G.office[a >> 3] |= 1 << (a & 7);
    music_play(MUS_JINGLE_FANFARE);
    say("Done! Your name's on the door. Ship from your bin at home and my best prices follow you there.");
    music_for_scene();
    hud_draw();
}

// ---------------------------------------------------------------- the almanac
void almanac_page(void) BANKED {
    static const char *const short_w[NPLACE] = {"Shell Is.", "Market", "Pasture", "Quarry", "Old Woods", "Amberwood", "Harbour", "Lakeshore",
                                                "Mesa", "Pinecrest", "Stonegate"};
    uint8_t a, k, all = 1, st;
    ui_begin();
    ui_box(0, 0, 20, 17);
    ui_title(2, 0, "ALMANAC");
    ui_text(11, 1, "\xF1\xF1\xF1 E O");
    for (a = 0; a < NPLACE; a++) {
        if (!(G.seen[a >> 3] & (1 << (a & 7)))) { ui_text(1, 2 + a, "?????????"); all = 0; continue; }
        ui_text(1, 2 + a, short_w[a]);
        st = region_stars(a);
        for (k = 0; k < 3; k++) ui_char(11 + k, 2 + a, k < st ? GL_IT_STAR2 : font_map['.' - 32]);
        if (a >= 5) ui_char(15, 2 + a, region_errand(a) == 3 ? GL_IT_STAR : font_map['.' - 32]);
        ui_char(17, 2 + a, (G.office[a >> 3] & (1 << (a & 7))) ? GL_IT_STAR : font_map['.' - 32]);
        if (st < 3 || (a >= 5 && region_errand(a) != 3) || !(G.office[a >> 3] & (1 << (a & 7)))) all = 0;
    }
    ui_text(1, 14, "\xF1 standing");
    ui_text(1, 15, "E errand  O office");
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); break; }
        wait_vbl_done();
    }
    ui_end();
    if (all && !(G.errand[2] & 0x40)) {
        G.errand[2] |= 0x40;
        G.gold += 100000;
        music_play(MUS_JINGLE_FANFARE);
        say("The almanac is complete: every place known, mended and trading under your name. G100000 from a grateful country!");
        music_for_scene();
        hud_draw();
    }
}
