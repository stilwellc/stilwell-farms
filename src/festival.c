// Stilwell Farms -- festival games: Spring egg hunt, Summer koi derby, Fall produce judging, Winter snowball fight
#pragma bank 255
#include <gb/gb.h>
#include "game.h"
#include "decor.h"

BANKREF(festival_hall)

extern void put16(uint8_t oam, int16_t x, int16_t y, uint8_t tile, uint8_t pal, uint8_t flip);
extern void hide16(uint8_t oam);

uint8_t fest_on;        // the egg hunt is running (you walk the town; A searches)
uint8_t fest_cue;       // what the game is showing right now: koi bite 1, polish needle 0-15, snowball fort 1-3
uint8_t egg_got;
uint16_t egg_tmr;
uint32_t egg_mask;      // bit per hiding place that still holds an egg
static uint8_t egg_beat;

static const char *const fest_names[4] = {"Flower Show", "Crop Fair", "Harvest Festival", "Star Night"};
static const uint8_t need[4] = {6, 120, 70, 8};             // eggs, koi cm, judges' points, snowball hits
static const uint16_t prize_gold[4] = {500, 800, 1200, 1000};
static const uint8_t next_crop[4] = {7, 10, 14, 3};          // rare seeds for next season: melon, pumpkin, snow pea, strawberry

static void wait_frames_spr(uint8_t n) { while (n--) { update_sprites(); wait_vbl_done(); } }

// ---------------------------------------------------------------- results + prizes
static void finish(uint8_t se, uint16_t score) {
    uint8_t bit = 1 << se, c = next_crop[se];
    uint32_t prize;
    if (score > G.fest_best[se]) G.fest_best[se] = score > 255 ? 255 : (uint8_t)score;
    set_speaker("Joe");
    if (score < need[se]) {
        s_clr(); s_add("A fine try! You needed "); s_num(need[se], 0); s_add(" to take first prize. Have another go!");
        say(sbuf);
        ui_speaker = 0;
        return;
    }
    music_play(MUS_JINGLE_FANFARE);
    say("We have a winner! First prize goes to our very own farmer!");
    prize = (uint32_t)prize_gold[se] * (G.year > 5 ? 5 : G.year ? G.year : 1);   // the prize grows with the fair, year by year
    G.gold += prize;
    G.seeds[c][R_RARE] = G.seeds[c][R_RARE] > 252 ? 255 : G.seeds[c][R_RARE] + 3;
    s_clr(); s_add("You win G"); s_num(prize, 0); s_add(" and 3 rare "); s_add(items[c - 1].name); s_add(" seeds!");
    ui_speaker = 0;
    say(sbuf);
    if (!(G.ribbons & bit)) {
        G.ribbons |= bit;
        s_clr(); s_add("And the "); s_add(fest_names[se]); s_add(" blue ribbon! It hangs in your GOODS log now.");
        say(sbuf);
    }
    G.fest_done |= bit;
    G.festival_won |= bit;
    hud_draw();
    music_for_scene();
    goals_check();
}

// ---------------------------------------------------------------- Spring: the egg hunt
#define EGGS 8
#define NHIDE 32
static const uint8_t hide_x[NHIDE] = {7, 13, 23, 30, 8, 3, 8, 25, 29, 23, 14, 17, 24, 8, 10, 13, 18, 21, 8, 2, 13, 18, 23, 11, 20, 7, 24, 29, 15, 4, 1, 30};
static const uint8_t hide_y[NHIDE] = {2, 2, 2, 2, 3, 6, 6, 6, 6, 8, 10, 10, 10, 11, 12, 12, 12, 12, 13, 16, 17, 17, 16, 18, 18, 19, 19, 16, 20, 9, 13, 10};
static uint32_t bit32(uint8_t i) { return 1UL << i; }

static void egg_start(void) {
    uint8_t n = 0, i;
    egg_mask = 0;
    while (n < EGGS) { i = rnd() & (NHIDE - 1); if (!(egg_mask & bit32(i))) { egg_mask |= bit32(i); n++; } }
    egg_got = 0; egg_tmr = 75 * 60; egg_beat = 0;
    say("Eight painted eggs are hidden around town: in flowerbeds, flowerpots, trees, benches...");
    say("Face a hiding spot and press A to search. Find 6 before time runs out! Ready... go!");
    ui_speaker = 0;
    fest_on = 1;
}

static void egg_status(void) {
    uint8_t i, best = 99, d;
    for (i = 0; i < NHIDE; i++) {
        if (!(egg_mask & bit32(i))) continue;
        d = (hide_x[i] > G.px ? hide_x[i] - G.px : G.px - hide_x[i]) + (hide_y[i] > G.py ? hide_y[i] - G.py : G.py - hide_y[i]);
        if (d < best) best = d;
    }
    s_clr(); s_add("Eggs "); s_num(egg_got, 0); s_add("/8 "); s_num(egg_tmr / 60, 0); s_add("s ");
    s_add(best <= 1 ? "Hot!" : best <= 3 ? "Warm" : "");
    hud_flash(sbuf);
}

void festival_tick(void) BANKED {
    if (!fest_on) return;
    if (egg_tmr) egg_tmr--;
    if (!egg_tmr || egg_got >= EGGS) {
        fest_on = 0;
        hud_draw();
        set_speaker("Joe");
        s_clr();
        if (egg_got >= EGGS) s_add("Every last egg! ");
        else s_add("Time's up! ");
        s_add("You found "); s_num(egg_got, 0); s_add(" eggs.");
        say(sbuf);
        finish(SEASON_SPRING, egg_got);
        return;
    }
    if (++egg_beat >= 30) { egg_beat = 0; egg_status(); }
}

uint8_t festival_poke(uint8_t x, uint8_t y) BANKED {
    uint8_t i;
    if (!fest_on) return 0;
    for (i = 0; i < NHIDE; i++) if (hide_x[i] == x && hide_y[i] == y) break;
    if (i == NHIDE) return 0;
    if (egg_mask & bit32(i)) {
        egg_mask &= ~bit32(i);
        egg_got++;
        fx_show(SPR_SPARKLE, SPR_SPARKLE_PAL, x, y);
        sfx(SFX_COIN);
        s_clr(); s_add("An egg! "); s_num(egg_got, 0); s_add(" of 8");
        hud_flash(sbuf);
    } else {
        sfx(SFX_BUMP);
        hud_flash("Just leaves...");
    }
    egg_beat = 0;   // keep the find on screen a moment before the timer comes back
    return 1;
}

// ---------------------------------------------------------------- Summer: the koi derby at the fountain
static void koi_derby(void) {
    uint8_t cast, t, wait, r, cm, early;
    uint16_t total = 0;
    say("The Koi Derby! Five casts into the fountain. Strike the moment a koi bites: quick hands land big fish.");
    say("Marcy's record is 120 cm, all five together. Catch and release, of course!");
    ui_speaker = 0;
    enter_scene(MAP_TOWN, 15, 13, D_UP);   // to the fountain's edge
    for (cast = 0; cast < 5; cast++) {
        player_pose(4, 255);
        sfx(SFX_SPLASH);
        s_clr(); s_add("Cast "); s_num(cast + 1, 0); s_add("/5  "); s_num(total, 0); s_add("cm");
        hud_flash(sbuf);
        wait = 50 + (rnd() & 127);
        early = 0;
        for (t = 0; t < wait; t++) {
            put16(2, 15 * 16 + 8 - (int16_t)scx, 12 * 16 - (int16_t)scy + ((t >> 4) & 1), SPR_BOBBER, SPR_BOBBER_PAL, 0);
            read_input();
            if (joy_press & J_A) { early = 1; break; }
            wait_vbl_done();
        }
        cm = 0;
        if (!early) {
            sfx(SFX_CONFIRM);
            fest_cue = 1;
            for (r = 0; r < 30; r++) {
                put16(2, (int16_t)(G.px * 16) - scx, (int16_t)(G.py * 16) - scy - 18, SPR_EXCLAIM, SPR_EXCLAIM_PAL, 0);
                read_input();
                if (joy_press & J_A) break;
                wait_vbl_done();
            }
            fest_cue = 0;
            if (r < 30) cm = 10 + (30 - r) + (rnd() & 7) + G.tier[TT_ROD] * 2;
        }
        hide16(2);
        player_pose(0, 0);
        if (cm) {
            total += cm;
            music_play(MUS_JINGLE_CATCH);
            player_pose(1, 40);
            s_clr(); s_add("A koi! "); s_num(cm, 0); s_add(" cm! Total "); s_num(total, 0); s_add(" cm.");
        } else {
            sfx(SFX_CANCEL);
            s_clr(); s_add(early ? "Too soon! The koi darted away." : "Too slow! It slipped the hook.");
        }
        say(sbuf);
        music_for_scene();
    }
    finish(SEASON_SUMMER, total);
}

// ---------------------------------------------------------------- Fall: produce judging
static uint8_t polish(void) {   // a needle sweeps the bar; stop it in the middle to shine your entry
    uint8_t pos = 0, t = 0, i, d;
    int8_t dir = 1;
    ui_begin();
    ui_box(0, 4, 20, 6);
    ui_text(2, 5, "Polish it! Tap A");
    ui_text(2, 6, "in the middle.");
    for (i = 0; i < 16; i++) ui_char(2 + i, 8, (i == 7 || i == 8) ? GL_BAR_FULL : GL_BAR_EMPTY);
    while (1) {
        fest_cue = pos;
        for (i = 0; i < 16; i++) ui_char(2 + i, 7, i == pos ? GL_IT_STAR2 : GL_FILL);
        for (i = 0; i < 4; i++) {
            read_input();
            if (joy_press & J_A) goto done;
            wait_vbl_done();
        }
        if (++t > 120) break;   // dawdled: the needle stops wherever it is
        pos += dir;
        if (pos == 15 || pos == 0) dir = -dir;
    }
done:
    sfx(SFX_CONFIRM);
    ui_end();
    d = pos < 7 ? 7 - pos : pos > 8 ? pos - 8 : 0;
    return d >= 8 ? 0 : 25 - d * 3 - (d ? 1 : 0);
}

static void judging(void) {
    uint8_t it, q, n = 0, r, mi[10], mq[10], pts, giant = 0;
    uint16_t i;
    for (q = 3; q-- && n < 10;)   // best quality first
        for (it = 0; it <= I_CHERRY && n < 10; it++) {
            if (!G.inv[it][q]) continue;
            s_clr(); s_ch((char)(0x80 + it)); s_add(items[it].name);
            if (q) s_ch(q == 2 ? (char)0xF1 : (char)0xF0);
            lbl_set(n); mi[n] = it; mq[n] = q; n++;
        }
    if (!n) { say("You'll need something you grew to enter. Bring a crop or some fruit!"); ui_speaker = 0; return; }
    say("The Giant Produce Judging! Enter the best thing you've grown. Quality and size both count.");
    ui_begin();
    r = ui_menu(0, 0, 20, lp, n, 0);
    ui_end();
    if (r == 0xFF) { say("Take your time. The judges aren't going anywhere!"); ui_speaker = 0; return; }
    it = mi[r]; q = mq[r];
    pts = q == 2 ? 35 : q == 1 ? 22 : 10;
    pts += items[it].price >= 300 ? 30 : (uint8_t)(items[it].price / 10);
    for (i = 0; i < WCELLS; i++) if (W_CELL[i] == C_GIANT) { giant = 1; break; }
    if (it == I_PUMPKIN && giant) pts += 10;
    ui_speaker = 0;
    pts += polish();
    set_speaker("Raza");
    say(q == 2 ? "Oh my. Look at the shine on it! Gold star quality, hon." : q ? "A fine, healthy one. Nicely grown!" : "Hmm. Honest and wholesome.");
    set_speaker("Alyssa");
    say(items[it].price >= 200 ? "And the SIZE of it! I've never sold seeds that grew this big." : "Small, but you can taste the care in it.");
    if (it == I_PUMPKIN && giant) { set_speaker("Brick"); say("Heard you grew a GIANT one. Hmph. Extra point for nerve."); }
    set_speaker("Joe");
    s_clr(); s_add("The judges award... "); s_num(pts, 0); s_add(" points! Andy's sweet corn has 69. Brick's lumpy beet, 41.");
    say(sbuf);
    finish(SEASON_FALL, pts);
}

// ---------------------------------------------------------------- Winter: snowball fight with Marcy
static void fort_draw(uint8_t k, uint8_t up) {
    uint8_t x = 1 + k * 6, i, j;
    if (up) ui_put_struct(x + 1, 4, ST_PORTRAIT_MILO, 2, 2);
    else for (j = 0; j < 4; j++) for (i = 0; i < 4; i++) ui_char(x + 1 + i, 4 + j, GL_FILL);
}

static void snowball(void) {
    uint8_t round, hits = 0, k, t, up, got, i;
    say("Star Night snowball fight! Marcy pops up behind the snow forts. Throw with LEFT, UP or RIGHT!");
    say("Land 8 hits out of 12 to win. Marcy's quick, so stay sharp!");
    ui_speaker = 0;
    ui_begin();
    ui_box(0, 0, 20, 12);
    ui_title(2, 0, "SNOWBALLS");
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 4; i++) ui_char(2 + k * 6 + i, 8, GL_BOX_T);   // a snow wall in front of each fort
        fort_draw(k, 0);
    }
    ui_text(2, 9, "LEFT"); ui_text(9, 9, "UP"); ui_text(14, 9, "RIGHT");
    for (round = 0; round < 12; round++) {
        s_clr(); s_add("Hits "); s_num(hits, 2); s_add("  Round "); s_num(round + 1, 2);
        ui_text(2, 10, sbuf);
        for (t = 20 + (rnd() & 31); t; t--) wait_vbl_done();
        k = rnd() % 3;
        up = 44 - round * 2;
        fort_draw(k, 1);
        fest_cue = k + 1;
        got = 0xFF;
        for (t = 0; t < up && got == 0xFF; t++) {
            read_input();
            if (joy_press & J_LEFT) got = 0;
            else if (joy_press & J_UP) got = 1;
            else if (joy_press & J_RIGHT) got = 2;
            wait_vbl_done();
        }
        fest_cue = 0;
        fort_draw(k, 0);
        if (got == k) { hits++; sfx(SFX_HIT); ui_text(2 + k * 6, 6, "SPLAT!"); }
        else { sfx(SFX_BUMP); ui_text(2 + k * 6, 6, got == 0xFF ? "Ha ha!" : "Missed"); }
        for (t = 0; t < 24; t++) wait_vbl_done();
        ui_text(2 + k * 6, 6, "      ");
    }
    ui_end();
    set_speaker("Marcy");
    s_clr(); s_num(hits, 0); s_add(hits >= 8 ? " hits?! No fair! You're like a snowball MACHINE." : " hits! Ha! I'm the snowball champion of the valley!");
    say(sbuf);
    set_speaker("Joe");
    say("And now, everyone, light your lanterns... and let them go. Merry Star Night!");
    finish(SEASON_WINTER, hits);
}

// ---------------------------------------------------------------- Joe runs the festival from the hall steps
void festival_hall(void) BANKED {
    uint8_t se = G.season & 3, bit = 1 << se;
    if (G.fest_year != G.year) { G.fest_year = G.year; G.fest_done = 0; }
    set_speaker("Joe");
    s_clr(); s_add("Welcome to the "); s_add(fest_names[se]); s_ch('!');
    say(sbuf);
    if (se == SEASON_SPRING && !(G.fest_done & 0x10)) {   // the old Flower Show garden tour still runs every spring
        uint8_t flowers = 0;
        uint16_t i;
        for (i = 0; i < WCELLS; i++)
            if (W_CELL[i] == C_DECOR && W_A1[i] >= DK_FLOWERBED && W_A1[i] <= DK_FLOWERS_YELLOW && flowers < 255) flowers++;
        say("The judges toured every garden in the valley this morning...");
        if (flowers >= 8) { say("...and yours was the loveliest! Best Garden: G500!"); G.gold += 500; G.fest_done |= 0x10; hud_draw(); }
        else say("...plant 8 flower beds for a shot at Best Garden next time!");
    }
    if (se == SEASON_WINTER && !(G.fest_done & 0x80)) {
        uint16_t f = 0;
        uint8_t k;
        for (k = 0; k < NUM_NPC; k++) f += G.friend[k];
        if (f >= 400) {
            say("The whole town chipped in to thank you for being such a good friend this year. G1000!");
            G.gold += 1000; G.fest_done |= 0x80;
            if (G.valley_done & VD_LAMPS) { G.gold += 1000; say("And with the square's lamps restored, the prize is doubled!"); }   // valley.c
            hud_draw();
        }
    }
    if (G.fest_done & bit) {
        s_clr(); s_add("You already took first prize this year! Your best: "); s_num(G.fest_best[se], 0); s_add(". Enjoy the festival!");
        say(sbuf);
        ui_speaker = 0;
        return;
    }
    if (!ui_yesno(se == SEASON_SPRING ? "Join the Egg Hunt?" : se == SEASON_SUMMER ? "Join the Koi Derby?" :
                  se == SEASON_FALL ? "Enter the Produce Judging?" : "Join the snowball fight?")) {
        say("Come back when you're ready! The festival runs until six.");
        ui_speaker = 0;
        return;
    }
    set_speaker("Joe");
    if (se == SEASON_SPRING) egg_start();
    else if (se == SEASON_SUMMER) koi_derby();
    else if (se == SEASON_FALL) judging();
    else snowball();
    ui_speaker = 0;
}
