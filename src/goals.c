// Stilwell Farms -- milestones, Grandpa's letters, the request board, festivals, collection (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "decor.h"

BANKREF(goals_check)

#define NUM_GOALS 21
#define GOAL_REVIEW 31
static const char *const letters[NUM_GOALS] = {
    "Your first harvest! I knew you had it in you. Here's a little something for more seeds.",
    "1,000 gold earned. The farm is paying its way again. Keep it up!",
    "You've cleared 50 weeds. The old place is starting to breathe again.",
    "You bought more land! The farm I always dreamed of is taking shape.",
    "You fixed up one of my old buildings. It warms my heart to see it standing again.",
    "Animals on the farm again! Treat them well and they'll treat you well.",
    "Your first fish! I used to spend whole afternoons by the water.",
    "A real house at last. No more leaky roof, eh?",
    "A two-storey home! Now that's a farmhouse to be proud of.",
    "Half the valley is yours now. Imagine what you'll build!",
    "Every acre, from the pines to the lake. You did it, kid.",
    "10,000 gold! You're a real farmer now.",
    "100,000 gold! The whole valley knows the name on that sign.",
    "The townsfolk love you. Good neighbours are worth more than gold.",
    "You've sold 20 different goods. Variety is the spice of farm life!",
    "Look at all those buildings. Your farm is a little village of its own.",
    "A gold tool! Brick must be proud of that one.",
    "100 harvests! Your hands must be as green as the fields.",
    "A whole new look for the house. Make it yours, I always say.",
    "Five land plots. Room to breathe, room to grow.",
    "A GIANT pumpkin?! In all my years I never grew one. Amazing!"};
static const uint16_t rewards[NUM_GOALS] = {200, 300, 200, 300, 500, 300, 200, 500, 1000, 1500, 5000,
                                            1000, 5000, 1000, 800, 1000, 2000, 1500, 500, 800, 2000};


static uint8_t goal_met(uint8_t g) {
    uint8_t k, h = bld_find(B_HOUSE), lv = h != 0xFF ? (G.bld[h].flags & BF_LVL) >> 1 : 0, n;
    switch (g) {
    case 0: return G.stat_harvest >= 1;
    case 1: return G.earned >= 1000;
    case 2: return G.stat_weeds >= 50;
    case 3: return plots_owned() > 4;
    case 4:
        for (k = 0; k < MAX_BLD; k++)
            if ((G.bld[k].type == B_BARN || G.bld[k].type == B_COOP || G.bld[k].type == B_GREENHOUSE) && !(G.bld[k].flags & BF_RUIN)) return 1;
        return 0;
    case 5: for (k = 0; k < MAX_ANIMALS; k++) if (G.anim[k].kind != 0xFF) return 1; return 0;
    case 6: return G.stat_fish >= 1;
    case 7: return lv >= 1;
    case 8: return lv >= 2;
    case 9: return plots_owned() >= NUM_PLOTS / 2;
    case 10: return plots_owned() == NUM_PLOTS;
    case 11: return G.earned >= 10000;
    case 12: return G.earned >= 100000;
    case 13: for (k = 0; k < NUM_NPC; k++) if (G.friend[k] >= 150) return 1; return 0;
    case 14: n = 0; for (k = 0; k < NUM_ITEMS; k++) if (is_shipped(k)) n++; return n >= 20;
    case 15: n = 0; for (k = 0; k < MAX_BLD; k++) if (G.bld[k].type != 0xFF) n++; return n >= 8;
    case 16: for (k = 0; k < NUM_TT; k++) if (G.tier[k] >= 3) return 1; return 0;
    case 17: return G.stat_harvest >= 100;
    case 18: return G.style != 0;
    case 19: return plots_owned() >= 5;
    default: return 0;   // 20: giant pumpkin, set by the harvest itself
    }
}

void goals_check(void) BANKED {
    uint8_t g;
    for (g = 0; g < NUM_GOALS; g++) {
        uint32_t bit = 1UL << g;
        if ((G.goals & bit) && g != 20) continue;
        if (g == 20) {  // giant pumpkin bit is set by the harvest; mail once
            if ((G.goals & bit) && !(G.mail & (1UL << 30)) && !(G.goals & (1UL << 30))) { G.mail |= bit; G.goals |= 1UL << 30; }
            continue;
        }
        if (goal_met(g)) { G.goals |= bit; G.mail |= bit; }
    }
    if (G.year >= 2 && !(G.goals & (1UL << GOAL_REVIEW))) { G.goals |= 1UL << GOAL_REVIEW; G.mail |= 1UL << GOAL_REVIEW; }
}

static void review(void) {
    uint16_t score = 0;
    uint8_t k, n = 0;
    score += plots_owned();   // the whole valley: 64, as much as the old 16 plots x 4
    for (k = 0; k < MAX_BLD; k++) if (G.bld[k].type != 0xFF) score += 4;
    for (k = 0; k < MAX_ANIMALS; k++) if (G.anim[k].kind != 0xFF) score += 3;
    for (k = 0; k < NUM_NPC; k++) score += G.friend[k] / 25;
    for (k = 0; k < NUM_ITEMS; k++) if (is_shipped(k)) n++;
    score += n * 2;
    score += (uint16_t)(G.earned / 2000 > 100 ? 100 : G.earned / 2000);
    set_speaker("Grandpa");
    say("A whole year has passed. I've been watching from up here, and I'd like to tell you what I see.");
    s_clr(); s_add("Your farm scores "); s_num(score, 0); s_add(" points.");
    say(sbuf);
    say(score >= 250 ? "It's the finest farm this valley has ever seen. I couldn't be prouder. Thank you." :
        score >= 150 ? "It's a wonderful, thriving farm. You've made the old place sing again." :
        score >= 70 ? "It's a good honest farm, and it's getting better every season." :
                      "It's a start, and every great farm starts somewhere. I believe in you.");
    say("The farm is yours to keep growing for as long as you like. Love, Grandpa.");
    ui_speaker = 0;
}

void read_mail(void) BANKED {
    uint8_t g;
    if (!G.mail) { say("The mailbox is empty."); return; }
    for (g = 0; g < 32; g++) {
        uint32_t bit = 1UL << g;
        if (!(G.mail & bit)) continue;
        G.mail &= ~bit;
        if (g >= MAIL_VALLEY && g < MAIL_VALLEY + NUM_PROJ) {   // valley.c: a restoration letter
            music_play(MUS_JINGLE_FANFARE);
            valley_letter(g - MAIL_VALLEY);
            music_for_scene();
            if (G.mail) say("There's another letter in there.");
            return;
        }
        if (g != GOAL_REVIEW && g >= NUM_GOALS) continue;   // no jingle left playing for a skipped bit
        music_play(MUS_JINGLE_FANFARE);
        if (g == GOAL_REVIEW) { review(); music_for_scene(); return; }
        if (g >= NUM_GOALS) continue;
        set_speaker("Grandpa");
        say(letters[g]);
        ui_speaker = 0;
        G.gold += rewards[g];
        s_clr(); s_add("There's G"); s_num(rewards[g], 0); s_add(" tucked in the envelope!");
        say(sbuf);
        hud_draw();
        music_for_scene();
        if (G.mail) say("There's another letter in there.");
        return;
    }
}

// ---------------------------------------------------------------- town request board
static const char *const npc_names[NUM_NPC] = {"Rosa", "Brick", "June", "Ada", "Hollis", "Milo"};

static void new_request(void) {
    static const uint8_t pool[4][6] = {
        {I_RADISH, I_CARROT, I_STRAWB, I_CAULI, I_MINNOW, I_MUSHROOM},
        {I_TOMATO, I_CORN, I_MELON, I_BLUEBERRY, I_BASS, I_BERRIES},
        {I_PUMPKIN, I_BEET, I_EGGPLANT, I_CRANBERRY, I_TROUT, I_APPLE},
        {I_SNOWPEA, I_MILK, I_EGG, I_COPPER, I_CARP, I_HONEY}};
    G.req_item = pool[G.season][rnd() % 6];
    G.req_qty = 2 + (rnd() % 6);
    G.req_npc = rnd() % NUM_NPC;
    G.req_day = 7;
}

void board_act(void) BANKED {
    uint32_t reward;
    if (!G.req_qty) new_request();
    reward = (uint32_t)items[G.req_item].price * G.req_qty * 2;
    s_clr(); s_add(npc_names[G.req_npc]); s_add(" needs "); s_num(G.req_qty, 0); s_ch(' '); s_add(items[G.req_item].name);
    s_add(" within "); s_num(G.req_day, 0); s_add(" days. Reward: G"); s_num(reward, 0); s_ch('.');
    say(sbuf);
    if (inv_count(G.req_item) >= G.req_qty) {
        if (ui_yesno("You have enough! Deliver them now?")) {
            inv_take(G.req_item, G.req_qty);
            G.gold += reward;
            G.friend[G.req_npc] = G.friend[G.req_npc] + 20 > 250 ? 250 : G.friend[G.req_npc] + 20;
            G.req_qty = 0;
            music_play(MUS_JINGLE_FANFARE);
            say("Delivered! They'll be so happy. A new request will be posted soon.");
            music_for_scene();
            hud_draw();
        }
    }
}

// ---------------------------------------------------------------- town hall: festivals and rating
static uint8_t festival_today(void) {
    if (G.season == SEASON_WINTER) return G.day == 25;
    return G.day == 14;
}

void hall_act(void) BANKED {
    set_speaker("Hollis");
    if (festival_today()) say("Festival day! The whole town's out by the fountain. Have you joined this year's game?");
    else {
        uint8_t plots = plots_owned(), d = G.season == SEASON_WINTER ? 25 : 14;
        say("Welcome to town hall! I keep the calendar and cheer on our farmers.");
        s_clr();
        if (G.day < d) { s_add("The next festival is on "); s_add(season_name[G.season]); s_ch(' '); s_num(d, 0); s_add(". Don't miss it!"); }
        else s_add("This season's festival has passed. See you next season!");
        say(sbuf);
        s_clr(); s_add("Your farm covers "); s_num(plots, 0); s_add(" of 64 plots. ");
        s_add(plots >= 48 ? "A true estate!" : plots >= 32 ? "Quite the spread!" : "Room to grow!");
        say(sbuf);
    }
    if (!(G.cook_flags & 2)) valley_board();   // valley.c: Hollis tells you about the restoration board
    ui_speaker = 0;
}

// ---------------------------------------------------------------- collection log
// Two pages of 8 a row (LEFT/RIGHT flips): 1/2 the farm's goods (items 0..39); 2/2 the mine and the sea
// (40..53, rows 2-4) and the kitchen's dishes (rows 6-8). Ribbons and the count sit under both.
static void goods_draw(uint8_t page) {
    uint8_t k, i, slot, last = page ? NUM_ITEMS - 40 : 40;
    ui_box(0, 0, 20, 17);
    ui_title(2, 0, page ? "GOODS 2/2" : "GOODS 1/2");
    for (i = 0; i < last; i++) {
        k = page ? 40 + i : i;
        slot = (page && k >= I_DISH0) ? i + 2 : i;   // dishes start on a fresh row
        ui_char(2 + (slot & 7) * 2, 2 + (slot >> 3) * 2, is_shipped(k) ? item_icon_tile[k] : font_map['?' - 32]);
    }
    {   // town life: festival blue ribbons (flower, koi, produce, star) with a gold star once won
        static const uint8_t rib_icon[4] = {GL_IT_FLOWER, GL_IT_FISH, GL_IT_MELON, GL_IT_STAR};
        ui_text(2, 12, "Ribbons");
        for (k = 0; k < 4; k++) {
            ui_char(10 + k * 2, 12, rib_icon[k]);
            ui_char(11 + k * 2, 12, (G.ribbons & (1 << k)) ? GL_IT_STAR2 : font_map['-' - 32]);
        }
    }
    for (k = 0, i = 0; k < NUM_ITEMS; k++) if (is_shipped(k)) i++;
    s_clr(); s_add("Sold "); s_num(i, 0); s_ch('/'); s_num(NUM_ITEMS, 0); s_add(" goods");   // fits inside the box
    ui_text(2, 14, sbuf);
}

void collection_page(void) BANKED {
    uint8_t page = 0;
    goods_draw(0);
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); return; }
        if (joy_press & (J_LEFT | J_RIGHT)) { page ^= 1; sfx(SFX_CURSOR); goods_draw(page); }
        wait_vbl_done();
    }
}
