// Stilwell Farms -- friends who write, and friends who ask (banked)
//
// Letters: the morning after a friendship reaches a new heart, that townsperson's letter is in the mailbox
//          with a small present from their trade. One letter a heart, five each.
// Wishes:  every year each townsperson (once they know you: one heart) has one big wish: a pile of something
//          they love, larger each year. Bring it and they pay double its worth and remember it.
//
// Save: G.npc_mail (a letter waiting from NPC n), bits 4-6 of G.heart_seen[n] (hearts already written about),
// G.wish_year + G.wish_done (a bit per NPC, cleared each year). Mail bit MAIL_FRIEND = some letter waits.
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(friends_overnight)

static const char *const names[NUM_NPC] = {"Alyssa", "Brick", "Andy", "Raza", "Joe", "Marcy"};
static const uint8_t gift_item[NUM_NPC] = {I_HERB, I_IRON, I_APPLE, I_MUFFIN, I_HONEY, I_SHELL};
static const char *const letter[NUM_NPC][2] = {
    {"Dear neighbour, the shop's brighter since you came. A little something from my own garden.",
     "Dear friend, I count on your visits now. You've done this valley good. From my garden, with love."},
    {"Farmer. You're all right. Here. Don't make a fuss.",
     "Farmer. Best customer I've had. Best friend too. Tell no one. Take these."},
    {"Hi! I had spare fruit and thought of you! Is that weird? Enjoy!",
     "Hi pal! I built you a crate and then filled the crate. You're the best neighbour on the whole road!"},
    {"Hon, you looked hungry last time. Baked a few extra. Eat!",
     "Hon, Marcy asks after you every day. So do I, truth be told. Baked your favourites."},
    {"From the mayor's desk: this town notices good neighbours. A jar from my own hives.",
     "Your grandpa would be so proud, and so am I. My best honey, saved for an occasion. This is one."},
    {"I FOUND THESE AT THE BEACH. THEY ARE FOR YOU. FROM MARCY. (Raza helped with spelling.)",
     "YOU ARE MY BEST GROWN-UP FRIEND. HERE ARE MY BEST SHELLS. DO NOT TELL JOE HE IS SECOND."}};

// called each night: a new heart since the last letter? write one (one letter a person a night)
void friends_overnight(void) BANKED {
    uint8_t n;
    for (n = 0; n < NUM_NPC; n++) {
        uint8_t sent = (G.heart_seen[n] >> 4) & 7, h = G.friend[n] / 50;
        if (h <= sent) continue;
        G.heart_seen[n] += 0x10;
        G.npc_mail |= 1 << n;
        G.mail |= 1UL << MAIL_FRIEND;
    }
}

// the mailbox: the next townsperson's letter (goals.c read_mail, on MAIL_FRIEND)
void friends_letter(void) BANKED {
    uint8_t n, h;
    for (n = 0; n < NUM_NPC && !(G.npc_mail & (1 << n)); n++) ;
    if (n == NUM_NPC) return;
    G.npc_mail &= ~(1 << n);
    if (G.npc_mail) G.mail |= 1UL << MAIL_FRIEND;   // more in the box
    h = (G.heart_seen[n] >> 4) & 7;
    set_speaker(names[n]);
    say(letter[n][h >= 3]);
    ui_speaker = 0;
    inv_add(gift_item[n], h >= 4 ? 2 : h >= 2 ? 1 : 0, h);
    s_clr(); s_add("Tucked inside: "); s_num(h, 0); s_ch(' '); s_add(items[gift_item[n]].name); s_ch('!');
    say(sbuf);
}

// ---------------------------------------------------------------- the yearly wish
static const uint8_t wish_pool[NUM_NPC][4] = {
    {I_SUNFLOWER, I_STRAWB, I_HERB, I_CORAL}, {I_COPPER, I_IRON, I_COAL, I_GOLD}, {I_CIDER, I_APPLE, I_CORN, I_DRIFTWOOD},
    {I_MILK, I_EGG, I_CHEESE, I_BERRIES}, {I_PUMPKIN, I_HONEY, I_JAM, I_PUMPKIN}, {I_SHELL, I_SEAGLASS, I_MINNOW, I_SARDINE}};

static uint8_t wish_item(uint8_t npc) { return wish_pool[npc][(uint8_t)(G.year + npc * 3) & 3]; }
static uint8_t wish_qty(void) { return G.year > 5 ? 30 : 5 + 5 * G.year; }

// talking to someone: 1 if the wish took the conversation (they asked, or you delivered)
uint8_t wish_talk(uint8_t npc, uint8_t first_today) BANKED {
    uint8_t it, n;
    uint32_t pay;
    if (G.wish_year != G.year) { G.wish_year = G.year; G.wish_done = 0; }
    if ((G.wish_done & (1 << npc)) || G.friend[npc] < 50) return 0;
    it = wish_item(npc); n = wish_qty();
    if (inv_count(it) < n) {
        if (!first_today) return 0;                 // they mention it once a day, not every time
        set_speaker(names[npc]);
        s_clr(); s_add("I've one big wish this year: "); s_num(n, 0); s_ch(' '); s_add(items[it].name);
        s_add(". If you ever have that many to spare!");
        say(sbuf);
        return 0;
    }
    s_clr(); s_add("Give "); s_add(names[npc]); s_add(" the "); s_num(n, 0); s_ch(' '); s_add(items[it].name); s_add(" they wished for?");
    if (!ui_yesno(sbuf)) return 0;
    inv_take(it, n);
    G.wish_done |= 1 << npc;
    G.friend[npc] = G.friend[npc] > 210 ? 250 : G.friend[npc] + 40;
    pay = (uint32_t)items[it].price * n * 2;
    G.gold += pay;
    music_play(MUS_JINGLE_FANFARE);
    set_speaker(names[npc]);
    say("You remembered! All of it! I'll not forget this. Not this year, not ever.");
    s_clr(); s_add("They press G"); s_num(pay, 0); s_add(" into your hands and won't hear no.");
    ui_speaker = 0;
    say(sbuf);
    music_for_scene();
    hud_draw();
    return 1;
}
