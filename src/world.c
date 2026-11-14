// Stilwell Farms -- the farm world: generation, land plots, buildings, overnight (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "decor.h"

BANKREF(world_new)

// ---------------------------------------------------------------- land plots
static const char *const plot_names[16] = {
    "Pine Woods", "North Meadow", "Quarry Hill", "Old Orchard",
    "Birch Grove", "Home Field", "Home Field", "Wildflower Hill",
    "Lakeside", "Home Field", "Home Field", "River Bend",
    "Willow Marsh", "South Pasture", "Sunny Field", "Rocky Hollow"};
static const uint16_t plot_prices[16] = {
    4500, 2800, 10000, 7000,
    4000, 0, 0, 3200,
    9000, 0, 0, 8000,
    5000, 1500, 2200, 15000};

void plot_name_add(uint8_t p) BANKED { s_add(plot_names[p & 15]); }
uint16_t plot_price(uint8_t p) BANKED { return plot_prices[p & 15]; }

void buy_plot_prompt(uint8_t p) BANKED {
    s_clr(); s_add(plot_names[p]); s_add(" is for sale! Buy it for G"); s_num(plot_prices[p], 0); s_ch('?');
    if (!ui_yesno(sbuf)) return;
    if (!pay(plot_prices[p], 0, 0)) return;
    G.plots |= (uint16_t)1 << p;
    sfx(SFX_COIN);
    music_play(MUS_JINGLE_BUY);
    {
        uint8_t x0 = (p & 3) << 3, y0 = (p >> 2) << 3;
        redraw_rect(x0 ? x0 - 1 : 0, y0 ? y0 - 1 : 0, 10, 10);
    }
    s_clr(); s_add(plot_names[p]); s_add(" is yours! Your land keeps growing.");
    say(sbuf);
    music_for_scene();
    goals_check();
}

// ---------------------------------------------------------------- buildings
static const uint8_t bw[NUM_BTYPES] = {3, 3, 2, 3, 1, 2, 3, 2, 2, 2};
static const uint8_t bh[NUM_BTYPES] = {3, 3, 2, 3, 2, 2, 2, 3, 2, 2};
uint8_t bld_w(uint8_t type) BANKED { return bw[type]; }
uint8_t bld_h(uint8_t type) BANKED { return bh[type]; }

uint8_t bld_door(uint8_t slot) BANKED {  // bottom-middle (coop: bottom-left)
    uint8_t t = G.bld[slot].type, w = bw[t], h = bh[t];
    if (t == B_COOP) return (h - 1) * w;
    return (h - 1) * w + (w >> 1);
}

uint8_t bld_find(uint8_t type) BANKED {
    uint8_t k;
    for (k = 0; k < MAX_BLD; k++)
        if (G.bld[k].type == type) return k;
    return 0xFF;
}

static void stamp(uint8_t slot) {
    Building *b = &G.bld[slot];
    uint8_t x, y, p = 0;
    for (y = 0; y < bh[b->type]; y++)
        for (x = 0; x < bw[b->type]; x++) {
            uint16_t i = WIDX(b->x + x, b->y + y);
            G.cell[i] = C_STRUCT; G.a1[i] = slot; G.a2[i] = p++;
        }
}

uint8_t bld_new(uint8_t type, uint8_t x, uint8_t y, uint8_t flags) BANKED {
    uint8_t k;
    for (k = 0; k < MAX_BLD; k++)
        if (G.bld[k].type == 0xFF) {
            G.bld[k].type = type; G.bld[k].x = x; G.bld[k].y = y; G.bld[k].flags = flags;
            stamp(k);
            return k;
        }
    return 0xFF;
}

void bld_remove(uint8_t slot) BANKED {
    Building *b = &G.bld[slot];
    uint8_t x, y;
    for (y = 0; y < bh[b->type]; y++)
        for (x = 0; x < bw[b->type]; x++) {
            uint16_t i = WIDX(b->x + x, b->y + y);
            G.cell[i] = C_GRASS; G.a1[i] = 0; G.a2[i] = 0;
        }
    b->type = 0xFF;
}

// ---------------------------------------------------------------- autotiles
static uint8_t fence_kind(int8_t x, int8_t y) {
    uint16_t i;
    if (x < 0 || y < 0 || x >= WW || y >= WH) return 0;
    i = WIDX(x, y);
    if (G.cell[i] != C_DECOR) return 0;
    return decor[G.a1[i]].fence;
}

void fix_autotile(uint8_t x, uint8_t y) BANKED {
    int8_t dx, dy;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
            int8_t cx = x + dx, cy = y + dy;
            uint8_t f;
            if (cx < 0 || cy < 0 || cx >= WW || cy >= WH) continue;
            f = fence_kind(cx, cy);
            if (f) {
                uint8_t h = fence_kind(cx - 1, cy) == f || fence_kind(cx + 1, cy) == f;
                uint8_t v = fence_kind(cx, cy - 1) == f || fence_kind(cx, cy + 1) == f;
                G.a2[WIDX(cx, cy)] = h ? 1 : (v ? 2 : 0);
            }
            redraw_cell(cx, cy);  // water edges and fences around the change
        }
}

// ---------------------------------------------------------------- world generation
static void setc(uint8_t x, uint8_t y, uint8_t c, uint8_t a1, uint8_t a2) {
    uint16_t i = WIDX(x, y);
    G.cell[i] = c; G.a1[i] = a1; G.a2[i] = a2;
}

static void gen_plot(uint8_t p) {
    uint8_t x0 = (p & 3) << 3, y0 = (p >> 2) << 3, x, y, r, c, a;
    for (y = y0; y < y0 + 8; y++)
        for (x = x0; x < x0 + 8; x++) {
            r = rnd() % 100;
            a = 0;
            switch (p) {
            case 0:  // pine woods
                c = r < 55 ? C_PINE : r < 62 ? C_STUMP : r < 68 ? C_BUSH : r < 72 ? C_FORAGE : C_GRASS2;
                if (c == C_FORAGE) a = 1;
                break;
            case 4:  // birch grove
                c = r < 45 ? C_BIRCH : r < 52 ? C_SAPLING_WILD : r < 58 ? C_FORAGE : r < 62 ? C_STUMP : C_GRASS;
                if (c == C_FORAGE) a = 1;
                break;
            case 1: case 13: case 14:  // meadows
                c = r < 18 ? C_WILDFLOWER : r < 30 ? C_WEEDS : r < 34 ? C_ROCK : r < 50 ? C_GRASS2 : C_GRASS;
                break;
            case 7:  // wildflower hill
                c = r < 40 ? C_WILDFLOWER : r < 48 ? C_FORAGE : r < 54 ? C_BUSH : C_GRASS;
                if (c == C_FORAGE) a = 2;
                break;
            case 3:  // old orchard
                c = (((x - x0) % 3) == 1 && ((y - y0) % 3) == 1) ? C_TREE : (r < 20 ? C_WEEDS : C_GRASS);
                if (c == C_TREE) a = (uint8_t)((rnd() & 3) << 5);
                break;
            case 2: case 15:  // quarry / rocky hollow
                if (y == y0 && p == 2) c = C_CLIFF;
                else c = r < 30 ? C_ROCK : r < 40 ? C_ORE : r < 46 ? C_BOULDER : r < 70 ? C_SAND : C_GRASS;
                if (c == C_ORE) { r = rnd(); a = r < 140 ? 0 : r < 210 ? 1 : r < 245 ? 2 : 3; }
                break;
            case 12:  // willow marsh
                c = r < 25 ? C_WATER : r < 40 ? C_WEEDS : r < 48 ? C_FORAGE : r < 55 ? C_BUSH : C_GRASS2;
                if (c == C_FORAGE) a = 1;
                break;
            default:  // home field: lovingly overgrown
                c = r < 34 ? C_WEEDS : r < 44 ? C_ROCK : r < 51 ? C_STUMP : r < 61 ? C_GRASS2 : r < 65 ? C_WILDFLOWER : C_GRASS;
                break;
            }
            setc(x, y, c, a, c == C_TREE ? 30 : 0);
        }
    if (p == 8) {  // lakeside: a big round lake with a sandy shore
        for (y = y0; y < y0 + 8; y++)
            for (x = x0; x < x0 + 8; x++) {
                int8_t dx = (int8_t)x - (x0 + 3), dy = (int8_t)y - (y0 + 4);
                uint8_t d2 = dx * dx + dy * dy;
                if (d2 <= 8) setc(x, y, C_WATER, 0, 0);
                else if (d2 <= 12) setc(x, y, C_SAND, 0, 0);
                else setc(x, y, (rnd() & 3) ? C_GRASS : C_PINE, 0, 0);
            }
    }
    if (p == 11) {  // river bend
        for (y = y0; y < y0 + 8; y++) {
            uint8_t rx = x0 + 3 + ((y - y0) > 3 ? 1 : 0);
            setc(rx, y, C_WATER, 0, 0);
            setc(rx + 1, y, C_WATER, 0, 0);
            setc(rx - 1, y, C_GRASS, 0, 0);
            setc(rx + 2, y, C_GRASS, 0, 0);
        }
    }
}

void world_new(void) BANKED {
    uint8_t p, x, y;
    memset(&G, 0, sizeof(G));
    G.rng_lo = 0x34; G.rng_hi = 0x12;  // same lovingly overgrown land every new game
    for (p = 0; p < 16; p++) gen_plot(p);
    for (p = 0; p < MAX_BLD; p++) G.bld[p].type = 0xFF;
    for (p = 0; p < MAX_ANIMALS; p++) G.anim[p].kind = 0xFF;
    G.plots = START_PLOTS;
    // the public road to town
    for (y = 12; y < WH; y++) { setc(ROAD_X, y, y >= 24 ? C_ROAD : C_DIRT, 0, 0); setc(ROAD_X + 1, y, y >= 24 ? C_ROAD : C_DIRT, 0, 0); }
    for (x = 10; x < ROAD_X; x++) setc(x, 12, C_DIRT, 0, 0);
    // a friendly clearing by the house
    for (y = 12; y <= 14; y++)
        for (x = 8; x <= 13; x++) if (G.cell[WIDX(x, y)] != C_DIRT) setc(x, y, (x + y) & 3 ? C_GRASS : C_GRASS2, 0, 0);
    for (x = 8; x <= 14; x++) for (y = 8; y <= 11; y++) setc(x, y, C_GRASS, 0, 0);
    // big obstacles that need better tools
    setc(13, 17, C_BOULDER, 0, 0); setc(18, 15, C_BOULDER, 0, 0); setc(21, 22, C_BOULDER, 0, 0);
    setc(17, 19, C_LOG, 0, 0); setc(11, 17, C_LOG, 0, 0); setc(22, 13, C_LOG, 0, 0);
    // grandpa's buildings
    bld_new(B_HOUSE, 9, 9, 0);
    bld_new(B_COOP, 17, 9, BF_RUIN);
    bld_new(B_BARN, 20, 9, BF_RUIN);
    bld_new(B_GREENHOUSE, 20, 18, BF_RUIN);
    for (x = 17; x <= 22; x++) setc(x, 12, C_GRASS, 0, 0);
    setc(21, 21, C_GRASS, 0, 0);
    setc(12, 11, C_DECOR, DK_BIN, 0);
    setc(13, 10, C_DECOR, DK_WELL, 0);
    setc(14, 11, C_DECOR, DK_SIGN, 0);
    setc(12, 9, C_DECOR, DK_MAILBOX, 0);

    G.gold = 500;
    G.day = 1; G.season = SEASON_SPRING; G.year = 1;
    G.time = 360;
    G.energy = G.energy_max = 100;
    G.px = 10; G.py = 12; G.facing = D_DOWN;
    G.scene = SCENE_FARM;
    G.tool = T_HAND;
    G.seeds[1] = 6; G.seed_sel = 1;
    G.water = 30;
    G.tomorrow = 0;
    G.opt_music = G.opt_sfx = 1;
    G.opt_text = 1;
    G.opt_clock = 1;
    strcpy(G.farm_name, "Stilwell");
    strcpy(G.pet_name, "Biscuit");
    G.rng_lo = DIV_REG | 1; G.rng_hi = sys_time;
}

// ---------------------------------------------------------------- overnight
static uint8_t m_crows, m_season, m_passed, m_storm, m_fairy, m_giant, m_produce;
static uint32_t m_income;

static uint8_t near_decor(uint16_t i, uint8_t kind, uint8_t r) {
    int8_t x = i & 31, y = i >> 5, dx, dy;
    for (dy = -r; dy <= (int8_t)r; dy++)
        for (dx = -r; dx <= (int8_t)r; dx++) {
            int8_t cx = x + dx, cy = y + dy;
            uint16_t j;
            if (cx < 0 || cy < 0 || cx >= WW || cy >= WH) continue;
            j = WIDX(cx, cy);
            if (G.cell[j] == C_DECOR && G.a1[j] == kind) return 1;
        }
    return 0;
}

static void water_around(uint16_t i, uint8_t r) {
    int8_t x = i & 31, y = i >> 5, dx, dy;
    for (dy = -r; dy <= (int8_t)r; dy++)
        for (dx = -r; dx <= (int8_t)r; dx++) {
            int8_t cx = x + dx, cy = y + dy;
            uint16_t j;
            if (cx < 0 || cy < 0 || cx >= WW || cy >= WH) continue;
            j = WIDX(cx, cy);
            if (G.cell[j] == C_SOIL) G.a1[j] |= SOIL_WET;
        }
}

static uint8_t ripe_pumpkin(uint16_t i) {
    return G.cell[i] == C_SOIL && (G.a1[i] & SOIL_CROP) == 10 && G.a2[i] >= crops[10].days;
}

void world_overnight(uint8_t passed_out) BANKED {
    uint16_t i;
    uint8_t c, crop, n = 0, k;
    uint8_t has_rod = 0, green = 0;
    m_crows = m_season = m_storm = m_fairy = m_giant = m_produce = 0;
    m_passed = passed_out;
    for (k = 0; k < MAX_BLD; k++)
        if (G.bld[k].type == B_GREENHOUSE && !(G.bld[k].flags & BF_RUIN)) green = 1;
    m_fairy = rnd() < 6;
    // 1. growth and nature
    for (i = 0; i < 1024; i++) {
        c = G.cell[i];
        if (c == C_SOIL) {
            crop = G.a1[i] & SOIL_CROP;
            if (crop && crop != CROP_WITHERED) {
                n++;
                if (((G.a1[i] & SOIL_WET) || m_fairy) && G.a2[i] < crops[crop].days) G.a2[i]++;
            }
            G.a1[i] &= ~SOIL_WET;
        } else if (c == C_TREE) {
            if (G.a2[i] < 250) G.a2[i]++;
            if (G.a2[i] >= 10 && (G.a1[i] & 31) < 31 && season_ok_tree(G.a1[i] >> 5)) G.a1[i]++;
        } else if (c == C_BUSH) {
            if (G.a1[i] < 9) G.a1[i]++;
        } else if (c == C_SAPLING_WILD) {
            if (++G.a2[i] >= 8) { G.cell[i] = PLOT_OF(i & 31, i >> 5) == 4 ? C_BIRCH : C_PINE; G.a2[i] = 0; }
        } else if (c == C_DECOR) {
            if (G.a1[i] == DK_BEEHIVE && G.a2[i] < 3) G.a2[i]++;
            if (G.a1[i] == DK_ROD) has_rod = 1;
        } else if (c == C_MACHINE) {
            if (G.a2[i] && G.a2[i] < 0x80) {
                if ((G.a1[i] >> 2) <= 1) { G.a1[i] &= 3; G.a2[i] |= 0x80; }
                else G.a1[i] -= 4;
            }
        } else if ((c == C_GRASS || c == C_GRASS2) && owned(i & 31, i >> 5)) {
            if (rnd() < 2) G.cell[i] = C_WEEDS;
        } else if ((c == C_GRASS || c == C_SAND) && !owned(i & 31, i >> 5)) {
            uint8_t p = PLOT_OF(i & 31, i >> 5);
            if ((p == 0 || p == 4 || p == 7 || p == 12) && rnd() < 3) { G.cell[i] = C_FORAGE; G.a1[i] = rnd() % 3; }
            if ((p == 2 || p == 15) && rnd() < 3) { G.cell[i] = C_ORE; G.a1[i] = rnd() < 200 ? 0 : 1; }
        }
    }
    // 2. giant pumpkins: a ripe 2x2 block may fuse
    for (i = 0; i < 1024 - 33; i++) {
        if ((i & 31) == 31) continue;
        if (ripe_pumpkin(i) && ripe_pumpkin(i + 1) && ripe_pumpkin(i + 32) && ripe_pumpkin(i + 33) && rnd() < 64) {
            G.cell[i] = G.cell[i + 1] = G.cell[i + 32] = G.cell[i + 33] = C_GIANT;
            G.a1[i] = 0; G.a1[i + 1] = 1; G.a1[i + 32] = 2; G.a1[i + 33] = 3;
            m_giant = 1;
        }
    }
    // 3. crows visit bigger farms without scarecrows
    if (n >= 6 && rnd() < 90) {
        uint16_t start = ((uint16_t)rnd() << 2), j;
        for (j = 0; j < 1024; j++) {
            i = (start + j) & 1023;
            crop = G.a1[i] & SOIL_CROP;
            if (G.cell[i] == C_SOIL && crop && crop != CROP_WITHERED && !near_decor(i, DK_SCARECROW, 4)) {
                G.a1[i] &= SOIL_WET | SOIL_FERT; G.a2[i] = 0;
                m_crows = 1;
                break;
            }
        }
    }
    // 4. storms: lightning finds a crop unless a lightning rod stands anywhere on the farm
    if (G.weather == 3 && !has_rod && n && rnd() < 160) {
        uint16_t start = ((uint16_t)rnd() << 2), j;
        for (j = 0; j < 1024; j++) {
            i = (start + j) & 1023;
            if (G.cell[i] == C_SOIL && (G.a1[i] & SOIL_CROP)) {
                G.a1[i] = (G.a1[i] & SOIL_FERT) | CROP_WITHERED;
                m_storm = 1;
                break;
            }
        }
    }
    // 5. calendar
    if (++G.day > 28) {
        G.day = 1;
        G.season = (G.season + 1) & 3;
        if (G.season == SEASON_SPRING) G.year++;
        m_season = 1;
        if (!green)
            for (i = 0; i < 1024; i++) {
                crop = G.a1[i] & SOIL_CROP;
                if (G.cell[i] == C_SOIL && crop && crop != CROP_WITHERED && crops[crop].season != G.season)
                    G.a1[i] = (G.a1[i] & (SOIL_WET | SOIL_FERT)) | CROP_WITHERED;
            }
    }
    // 6. weather: today is yesterday's forecast; roll tomorrow
    G.weather = G.tomorrow;
    if (G.season == SEASON_WINTER) G.tomorrow = rnd() < 70 ? 2 : 0;
    else {
        uint8_t r = rnd();
        G.tomorrow = r < 50 ? 1 : (G.season == SEASON_SUMMER && r < 70 ? 3 : 0);
    }
    for (i = 0; i < 1024; i++) {
        c = G.cell[i];
        if (c == C_SOIL && (G.weather == 1 || G.weather == 3)) G.a1[i] |= SOIL_WET;
        if (c == C_DECOR) {
            if (G.a1[i] == DK_SPRINKLER) water_around(i, 1);
            else if (G.a1[i] == DK_QSPRINKLER) water_around(i, 2);
        }
    }
    // 7. animals: fed ones give produce (quality from happiness); happiness drifts
    for (k = 0; k < MAX_ANIMALS; k++) {
        Animal *a = &G.anim[k];
        if (a->kind == 0xFF) continue;
        if (a->flags & AF_FED) { if (a->happy < 235) a->happy += 20; a->flags |= AF_READY; m_produce = 1; }
        else if (a->happy > 30) a->happy -= 30;
        else a->happy = 0;
        if (a->flags & AF_PET) { if (a->happy < 245) a->happy += 10; }
        a->flags &= AF_READY;
        if (a->age < 255) a->age++;
    }
    G.pet_flags = 0;
    G.talked = G.gifted = 0;
    // 8. money, rest, reset
    m_income = G.ship_value;
    G.gold += G.ship_value;
    G.earned += G.ship_value;
    G.ship_value = 0;
    if (G.req_qty && G.req_day && --G.req_day == 0) G.req_qty = 0;
    G.energy = passed_out ? G.energy_max >> 1 : G.energy_max;
    G.time = 360;
    {
        uint8_t h = bld_find(B_HOUSE);
        if (h != 0xFF) { G.px = G.bld[h].x + 1; G.py = G.bld[h].y + bld_h(B_HOUSE); }
    }
    G.facing = D_DOWN;
    G.scene = SCENE_FARM;
    building = 0;
    goals_check();
    save_write();
}

void morning_report(void) BANKED {
    static const char *const wx[4] = {"Sunny", "Rainy: crops are watered", "Snowy", "Stormy! Watch for lightning"};
    static const char *const fc[4] = {"sun", "rain", "snow", "a thunderstorm"};
    s_clr();
    s_add(season_name[G.season]); s_ch(' '); s_num(G.day, 0); s_add(", Year "); s_num(G.year, 0); s_add(". ");
    s_add(wx[G.weather]); s_add(". Tomorrow: "); s_add(fc[G.tomorrow]); s_ch('.');
    say(sbuf);
    if (m_passed) say("You overslept and feel worn out.");
    if (!(G.tips & 0x20)) {   // first morning
        G.tips |= 0x20;
        say("Tip: the game saves every morning. Get to bed before 2am, or you'll wake up worn out.");
    }
    if (m_season) {
        s_clr(); s_add("It's "); s_add(season_name[G.season]); s_add(" now!");
        say(sbuf);
    }
    if (m_income) {
        s_clr(); s_add("Shipping paid G"); s_num(m_income, 0); s_add(" overnight!");
        say(sbuf);
    }
    if (m_fairy) say("A crop fairy visited in the night... everything grew a little!");
    if (m_giant) say("Whoa! Your pumpkins grew into a GIANT pumpkin!");
    if (m_crows) say("Crows pecked at a crop overnight! A scarecrow would help.");
    if (m_storm) say("Lightning struck a crop in the storm! A lightning rod protects the whole farm.");
    if (m_produce) say("Your animals have produce waiting for you.");
    if (G.mail) say("There's a letter in your mailbox!");
}
