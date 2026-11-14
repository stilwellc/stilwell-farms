// Stilwell Farms -- what A does on the farm: tools, harvests, buildings, machines (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "decor.h"

BANKREF(farm_act)

static uint8_t grassy(uint8_t c) { return c == C_GRASS || c == C_GRASS2 || c == C_WILDFLOWER; }

static uint8_t green_ok(void) {   // a repaired (not ruined) greenhouse stands on the farm
    uint8_t k;
    for (k = 0; k < MAX_BLD; k++)
        if (G.bld[k].type == B_GREENHOUSE && !(G.bld[k].flags & BF_RUIN)) return 1;
    return 0;
}

static void set_cell(uint16_t i, uint8_t c) {
    G.cell[i] = c; G.a1[i] = 0; G.a2[i] = 0;
}

static void clear_to_grass(uint16_t i, uint8_t x, uint8_t y, uint8_t snd) {
    sfx(snd);
    set_cell(i, C_GRASS);
    redraw_cell(x, y);
    fx_show(SPR_DUST, SPR_DUST_PAL, x, y);
}

static uint8_t roll_quality(uint8_t fert) {
    uint8_t r = rnd();
    if (r < (fert ? 30 : 6)) return 2;
    if (r < (fert ? 100 : 30)) return 1;
    return 0;
}

// legendary crops roll much better: fert 2 = legendary bonus odds
static uint8_t roll_quality_r(uint8_t fert, uint8_t rar) {
    uint8_t r;
    if (!rar) return roll_quality(fert);
    r = rnd();
    if (rar == R_LEGEND) {
        if (r < (fert ? 160 : 110)) return 2;
        if (r < 220) return 1;
        return 0;
    }
    if (r < (fert ? 50 : 20)) return 2;
    if (r < (fert ? 130 : 70)) return 1;
    return 0;
}

static void gain_item(uint8_t it, uint8_t q, uint8_t n) {
    inv_add(it, q, n);
    s_clr(); s_ch('+'); s_num(n, 0); s_ch(' '); s_add(items[it].name);
    if (q) s_ch(q == 2 ? (char)0xF1 : (char)0xF0);
    sfx(SFX_COIN);
    hud_flash(sbuf);
    player_pose(1, 24);
}

// "+3 Wood": built here, not via flash_gain(), so the literal never crosses into another bank
static void gain_res(const char *what, uint8_t n) {
    sfx(SFX_COIN);
    s_clr(); s_ch('+'); s_num(n, 0); s_ch(' '); s_add(what);
    hud_flash(sbuf);
}

// ---------------------------------------------------------------- multi-hit obstacles
// Rocks, stumps, trees... take several strikes; hits so far live in the cell's a2 byte.
// Hitting a different cell resets the old one. Tier 2 tools need one hit fewer, gold breaks in one.
static uint16_t hit_cell = 0xFFFF;
static uint8_t obstacle(uint8_t c) {
    return c == C_ROCK || c == C_BOULDER || c == C_STUMP || c == C_LOG || c == C_PINE || c == C_BIRCH;
}
static uint8_t strike(uint16_t i, uint8_t x, uint8_t y, uint8_t need, uint8_t tier, uint8_t snd) {
    if (tier >= 3) need = 1;
    else if (tier == 2 && need > 1) need--;
    if (hit_cell != i) {
        if (hit_cell < 1024 && obstacle(G.cell[hit_cell])) G.a2[hit_cell] = 0;
        hit_cell = i;
        G.a2[i] = 0;
    }
    if (!spend_energy(2)) return 0;
    player_pose(2, 8);
    if (++G.a2[i] < need) {
        sfx(snd);
        fx_show(SPR_DUST, SPR_DUST_PAL, x, y);
        return 0;
    }
    G.a2[i] = 0;
    hit_cell = 0xFFFF;
    return 1;
}

// ---------------------------------------------------------------- tool areas
static uint8_t area_n(uint8_t tier) { return tier == 0 ? 1 : tier == 1 ? 3 : tier == 2 ? 5 : 9; }

static uint8_t tool_area(uint8_t tool, uint8_t x, uint8_t y, uint8_t tier) {
    uint8_t n = area_n(tier), k, hit = 0;
    if (n == 9) {  // centre the 3x3 one step further so it doesn't include the player's own cell
        if (G.facing == D_DOWN) y++; else if (G.facing == D_UP) y--; else if (G.facing == D_LEFT) x--; else x++;
    }
    for (k = 0; k < n; k++) {
        int8_t cx = x, cy = y;
        if (n == 9) { cx += (int8_t)(k % 3) - 1; cy += (int8_t)(k / 3) - 1; }
        else {
            int8_t off = (int8_t)((k + 1) >> 1) * ((k & 1) ? -1 : 1);
            if (G.facing <= D_UP) cx += off; else cy += off;
        }
        if (cx < 0 || cy < 0 || cx >= WW || cy >= WH || !owned(cx, cy)) continue;
        {
            uint16_t i = WIDX(cx, cy);
            uint8_t c = G.cell[i];
            if (tool == T_HOE && grassy(c)) { set_cell(i, C_SOIL); hit++; redraw_cell(cx, cy); }
            else if (tool == T_CAN && c == C_SOIL && !(G.a1[i] & SOIL_WET)) { G.a1[i] |= SOIL_WET; hit++; redraw_cell(cx, cy); }
        }
    }
    return hit;
}

// ---------------------------------------------------------------- buildings
static void repair(uint8_t slot, const char *what, uint32_t g, uint16_t w, uint16_t s) {
    s_clr(); s_add("Repair "); s_add(what); s_add("?\n"); cost_str(g, w, s);
    if (!ui_yesno(sbuf)) return;
    if (!pay(g, w, s)) return;
    G.bld[slot].flags &= ~BF_RUIN;
    redraw_rect(G.bld[slot].x, G.bld[slot].y, bld_w(G.bld[slot].type), bld_h(G.bld[slot].type));
    music_play(MUS_JINGLE_FANFARE);
    say("Good as new! The farm is coming back to life.");
    music_for_scene();
    goals_check();
}

// rarity: collect one animal's produce (rare and golden breeds give two; golden is always gold quality)
static uint8_t collect_produce(Animal *a) {
    uint8_t br = BREED(a), n = br ? 2 : 1;
    uint8_t q = br == R_LEGEND ? 2 : a->happy > 200 ? 2 : a->happy > 120 ? 1 : 0;
    inv_add(a->kind == A_COW ? I_MILK : a->kind == A_HEN ? I_EGG : I_WOOL, q, n);
    a->flags &= ~AF_READY;
    return n;
}

static void animals_building(uint8_t slot, uint8_t kind) {
    uint8_t k, n = 0, fed = 0, nohay = 0, got = 0, wool = 0;
    for (k = 0; k < MAX_ANIMALS; k++) {
        Animal *a = &G.anim[k];
        if (a->home != slot || (a->kind != kind && !(kind == A_COW && a->kind == A_SHEEP))) continue;
        n++;
        if (a->flags & AF_READY) {
            if (a->kind == A_SHEEP) wool += collect_produce(a);
            else got += collect_produce(a);
        }
        if (!(a->flags & AF_FED)) {
            if (G.hay) { G.hay--; a->flags |= AF_FED; fed++; }
            else nohay = 1;
        }
    }
    if (!n) {
        say(kind == A_COW ? "An empty barn. Buy cows or sheep at the General Store in town!" : "An empty coop. Buy hens at the General Store in town!");
        return;
    }
    if (got || wool) {
        s_clr(); if (!got || !wool) s_add("Got ");   // "12 Milk, 3 Wool!" fits the toast; "Got " would not
        if (got) { s_num(got, 0); s_add(kind == A_COW ? " Milk" : " Eggs"); }
        if (wool) { if (got) s_add(", "); s_num(wool, 0); s_add(" Wool"); }
        s_ch('!');
        sfx(SFX_COIN); hud_flash(sbuf);
    }
    if (fed) {
        s_clr(); s_add("You fed "); s_num(fed, 0); s_add(fed == 1 ? " animal" : " animals"); s_add(" with hay.");
        say(sbuf);
    } else if (nohay) say("You're out of hay! Cut grass with the sickle (needs a silo) or buy hay in town.");
    else if (!got) say("Everyone's fed and happy. Come back tomorrow!");
}

static void struct_act(uint8_t slot) {
    Building *b = &G.bld[slot];
    uint8_t ruin = b->flags & BF_RUIN;
    switch (b->type) {
    case B_HOUSE: house_enter(); break;   // interior.c: sleep in the bed inside
    case B_BARN:
        if (ruin) repair(slot, "the barn", 3000, 80, 40);
        else animals_building(slot, A_COW);
        break;
    case B_COOP:
        if (ruin) repair(slot, "the coop", 1000, 30, 0);
        else animals_building(slot, A_HEN);
        break;
    case B_GREENHOUSE:
        if (ruin) repair(slot, "greenhouse", 15000, 60, 150);
        else say("With the greenhouse running, every crop grows in any season!");
        break;
    case B_SILO:
        s_clr(); s_add("The silo holds "); s_num(G.hay, 0); s_add(" hay. Cut grass with the sickle to fill it.");
        say(sbuf);
        break;
    case B_STAND: {
        uint32_t total = 0;
        uint8_t it, q;
        for (it = 0; it < NUM_ITEMS; it++)
            for (q = 0; q < 3; q++) total += (uint32_t)G.inv[it][q] * item_value(it, q);
        total = (total * 4) / 5;
        if (!total) { say("Your farm stand. Sell your goods here anytime for 80% of their value."); break; }
        s_clr(); s_add("Sell everything here now for G"); s_num(total, 0); s_ch('?');
        if (!ui_yesno(sbuf)) break;
        for (it = 0; it < NUM_ITEMS; it++) {
            if (inv_count(it)) mark_shipped(it);
            G.inv[it][0] = G.inv[it][1] = G.inv[it][2] = 0;
        }
        G.gold += total; G.earned += total;
        sfx(SFX_COIN); hud_flash("Sold!");
        goals_check();
        break;
    }
    case B_WINDMILL: say("The windmill creaks happily in the breeze."); break;
    case B_GAZEBO: say("A lovely spot to watch the sunset over your farm."); break;
    case B_STABLE: say("A fine stable. Maybe someday a horse will live here."); break;
    default: say("Your tool shed. Everything in its place."); break;
    }
}

// ---------------------------------------------------------------- machines

static void machine_act(uint16_t i, uint8_t x, uint8_t y) {
    uint8_t kind = G.a1[i] & 3, it, prod, days;
    static const char *const names[3] = {"keg", "cheese press", "preserves jar"};
    if (G.a2[i] & 0x80) {
        prod = (G.a2[i] & 0x7F) - 1;
        G.a2[i] = 0; G.a1[i] = kind;
        redraw_cell(x, y);
        gain_item(prod, 0, 1);
        return;
    }
    if (G.a2[i]) {
        s_clr(); s_add("It'll be ready in "); s_num(G.a1[i] >> 2, 0); s_add(" days.");
        say(sbuf);
        return;
    }
    s_clr(); s_add("Put something in the "); s_add(names[kind]); s_ch('?');
    if (!ui_yesno(sbuf)) return;
    it = ui_pick_item(kind == MK_PRESS ? PICK_MILK : PICK_PRODUCE);
    if (it == 0xFF) return;
    inv_take(it, 1);
    if (kind == MK_KEG) { prod = it >= I_APPLE ? I_CIDER : I_JUICE; days = 5; }
    else if (kind == MK_PRESS) { prod = I_CHEESE; days = 1; }
    else { prod = it >= I_APPLE ? I_JAM : I_PICKLES; days = 2; }
    G.a1[i] = kind | (days << 2);
    G.a2[i] = prod + 1;
    sfx(SFX_CONFIRM);
    hud_flash("Still working...");
}

// ---------------------------------------------------------------- tools
static void use_tool(uint16_t i, uint8_t x, uint8_t y) {
    uint8_t c = G.cell[i], crop = G.a1[i] & SOIL_CROP;
    switch (G.tool) {
    case T_HAND:
    case T_SICKLE:
        if (c == C_WEEDS) {
            uint8_t rr;
            if (!spend_energy(G.tool == T_HAND ? 2 : 1)) return;
            clear_to_grass(i, x, y, SFX_HIT);
            G.stat_weeds++;
            if (rnd() < 24 && G.season != SEASON_WINTER) {
                uint8_t s = 1 + (rnd() % NUM_CROPS);
                if (crops[s].season == G.season) {
                    rr = rnd() < 16 ? R_RARE : R_COMMON;   // a lucky rare packet now and then
                    G.seeds[s][rr]++;
                    s_clr(); s_add(items[s - 1].name); s_add(rr ? "+ seed!" : " seed!");   // "+": a rare packet
                    hud_flash(sbuf);
                }
            }
        } else if (c == C_SOIL && crop == CROP_WITHERED) {
            G.a1[i] &= SOIL_WET | SOIL_FERT; G.a2[i] = 0;
            redraw_cell(x, y);
        } else if (G.tool == T_SICKLE && grassy(c)) {
            if (bld_find(B_SILO) == 0xFF) { hud_flash("Build a silo first"); return; }
            if (!spend_energy(1)) return;
            if (G.hay < 999) G.hay++;
            if (c == C_WILDFLOWER) { G.cell[i] = C_GRASS; redraw_cell(x, y); }
            sfx(SFX_HIT);
            hud_flash("+1 Hay");
        } else if (c == C_SOIL && crop && G.tool == T_HAND) {
            s_clr(); s_add(items[crop - 1].name);
            if (SOIL_RARITY(i)) s_ch(SOIL_RARITY(i) == R_LEGEND ? (char)0xF1 : '+');
            s_ch(' '); s_num(crops[crop].days - SOIL_GROWN(i), 0); s_ch('d');   // toast fits 18 chars
            if (!(G.a1[i] & SOIL_WET)) s_add(s_len() <= 9 ? ", thirsty" : " dry");
            else if (s_len() <= 13) s_add(" left");
            hud_flash(sbuf);
        }
        break;
    case T_HOE:
        if (c == C_WEEDS) { hud_flash("Clear weeds first."); return; }
        if (!grassy(c)) { if (c == C_DIRT) hud_flash("Too hard to till."); return; }
        if (!spend_energy(2)) return;
        player_pose(2, 12);
        tool_area(T_HOE, x, y, G.tier[TT_HOE]);
        sfx(SFX_HIT);
        fx_show(SPR_DUST, SPR_DUST_PAL, x, y);
        if (!(G.tips & 1)) { G.tips |= 1; say("Tip: switch to Seeds with SELECT to plant here, then water with the Can."); }
        break;
    case T_CAN:
        if (c == C_WATER || (c == C_DECOR && G.a1[i] == DK_WELL)) {
            G.water = 30 + G.tier[TT_CAN] * 20;
            sfx(SFX_WATER); hud_flash("Watering can full!");
            return;
        }
        if (c != C_SOIL) return;
        if (!G.water) { say("Your watering can is empty. Refill it at any water or a well."); return; }
        if (!spend_energy(1)) return;
        player_pose(3, 12);
        if (tool_area(T_CAN, x, y, G.tier[TT_CAN])) {
            G.water--; sfx(SFX_WATER); fx_show(SPR_DROP, SPR_DROP_PAL, x, y);
            if (!(G.tips & 0x10) && (G.a1[i] & SOIL_CROP)) {
                G.tips |= 0x10;
                say("Tip: watered soil turns dark. Only watered crops grow overnight, so water them every day!");
            }
        }
        break;
    case T_SEEDS: {
        uint8_t s = G.seed_sel, rr = G.seed_rar % NUM_RARITY;
        if (c != C_SOIL || crop) return;
        if (s && !G.seeds[s][rr])   // this packet ran out: fall back to another rarity of the same crop
            for (rr = 0; rr < NUM_RARITY - 1 && !G.seeds[s][rr]; rr++) ;
        G.seed_rar = rr;
        if (!s || !G.seeds[s][rr]) { say("You're out of those seeds. Buy more at the General Store in town."); return; }
        if (crops[s].season != G.season && !green_ok()) {
            s_clr(); s_add(items[s - 1].name); s_add(" won't grow in "); s_add(season_name[G.season]); s_ch('.');
            say(sbuf);
            return;
        }
        G.seeds[s][rr]--;
        G.a1[i] = (G.a1[i] & (SOIL_WET | SOIL_FERT)) | s; G.a2[i] = rr << 6;
        redraw_cell(x, y);
        if (rr == R_LEGEND) fx_show(SPR_SPARKLE, SPR_SPARKLE_PAL, x, y);
        break;
    }
    case T_FERT:
        if (c != C_SOIL || (G.a1[i] & SOIL_FERT)) return;
        if (!G.fert) { say("You're out of fertilizer. The General Store sells it."); return; }
        G.fert--;
        G.a1[i] |= SOIL_FERT;
        fx_show(SPR_SPARKLE, SPR_SPARKLE_PAL, x, y);
        hud_flash("Fertilized!");
        break;
    case T_AXE:
        if (c == C_STUMP || c == C_SAPLING_WILD) {
            if (c == C_STUMP ? !strike(i, x, y, 2, G.tier[TT_AXE], SFX_CHOP) : !spend_energy(4)) return;
            clear_to_grass(i, x, y, SFX_CHOP);
            G.wood += 3; gain_res("Wood", 3);
        } else if (c == C_PINE || c == C_BIRCH) {
            if (!strike(i, x, y, 3, G.tier[TT_AXE], SFX_CHOP)) return;
            sfx(SFX_CHOP);
            fx_show(SPR_DUST, SPR_DUST_PAL, x, y);
            G.cell[i] = C_SAPLING_WILD; G.a2[i] = 0;   // forests regrow
            redraw_cell(x, y);
            G.wood += 6; gain_res("Wood", 6);
        } else if (c == C_LOG) {
            if (!G.tier[TT_AXE]) { say("This log is too big for your rusty axe. The blacksmith in town can upgrade it."); return; }
            if (!strike(i, x, y, 4, G.tier[TT_AXE], SFX_CHOP)) return;
            clear_to_grass(i, x, y, SFX_CHOP);
            G.wood += 12; gain_res("Wood", 12);
        }
        break;
    case T_HAMMER:
        if (c == C_ROCK) {
            if (!strike(i, x, y, 2, G.tier[TT_HAMMER], SFX_HIT)) return;
            clear_to_grass(i, x, y, SFX_HIT);
            G.stone += 2; gain_res("Stone", 2);
        } else if (c == C_BOULDER) {
            if (!G.tier[TT_HAMMER]) { say("This boulder won't budge. The blacksmith in town can upgrade your hammer."); return; }
            if (!strike(i, x, y, 4, G.tier[TT_HAMMER], SFX_HIT)) return;
            clear_to_grass(i, x, y, SFX_HIT);
            G.stone += 10; gain_res("Stone", 10);
        } else if (c == C_ORE) {
            uint8_t k = G.a1[i] & 3, need = k == 0 ? 0 : k == 1 ? 1 : 2;
            if (G.tier[TT_HAMMER] < need) { say("This ore is too hard. Upgrade your hammer at the blacksmith."); return; }
            if (!spend_energy(5)) return;
            G.cell[i] = C_SAND; G.a1[i] = 0;
            redraw_cell(x, y);
            sfx(SFX_HIT);
            G.stone++;
            gain_item(I_COPPER + k, 0, k == 3 ? 1 : 1 + (rnd() & 1));
        } else if (c == C_SOIL && !crop) {
            if (!spend_energy(1)) return;
            clear_to_grass(i, x, y, SFX_HIT);
        }
        break;
    case T_ROD:
        if (c == C_WATER) fish_start();
        else hud_flash("Face water to fish");
        break;
    }
}

void farm_act(void) BANKED {
    uint8_t x, y, c, a;
    uint16_t i;
    if (!front_cell(&x, &y)) return;
    a = actor_at(x, y);
    if (a != 0xFF) {
        uint8_t an = actor_animal(a);
        if (actor_is_pet(a)) {
            if (!(G.pet_flags & 1)) { G.pet_flags |= 1; if (G.pet_love < 250) G.pet_love += 5; }
            fx_show(SPR_HEART, SPR_HEART_PAL, x, y);
            s_clr(); s_add(G.pet_name); s_add(" wags happily");
            hud_flash(sbuf);
            return;
        }
        if (actor_staff(a) != 0xFF) { staff_talk(actor_staff(a)); return; }
        if (an != 0xFF) {
            Animal *m = &G.anim[an];
            m->flags |= AF_PET;
            fx_show(SPR_HEART, SPR_HEART_PAL, x, y);
            if (m->kind == A_SHEEP && (m->flags & AF_READY)) {   // shear right here in the field
                uint8_t q = BREED(m) == R_LEGEND ? 2 : m->happy > 200 ? 2 : m->happy > 120 ? 1 : 0;
                m->flags &= ~AF_READY;
                gain_item(I_WOOL, q, BREED(m) ? 2 : 1);
                return;
            }
            s_clr(); s_add(m->name);
            s_add(m->age < ADULT_AGE ? " is a baby!" : m->happy > 200 ? " adores you!" : m->happy > 100 ? " is content." : " looks hungry");
            hud_flash(sbuf);
            return;
        }
    }
    i = WIDX(x, y);
    if (!owned(x, y) && G.cell[i] != C_ROAD) {
        buy_plot_prompt(PLOT_OF(x, y));
        return;
    }
    c = G.cell[i];
    switch (c) {
    case C_STRUCT: struct_act(G.a1[i]); return;
    case C_MACHINE: machine_act(i, x, y); return;
    case C_DECOR:
        switch (G.a1[i]) {
        case DK_BIN: {
            uint32_t total = 0;
            uint8_t it, q;
            for (it = 0; it < NUM_ITEMS; it++)
                for (q = 0; q < 3; q++) total += (uint32_t)G.inv[it][q] * item_value(it, q);
            if (!total) { say("The shipping bin. Goods put in here are sold overnight."); return; }
            s_clr(); s_add("Ship all your goods for G"); s_num(total, 0); s_ch('?');
            if (!ui_yesno(sbuf)) return;
            for (it = 0; it < NUM_ITEMS; it++) {
                if (inv_count(it)) mark_shipped(it);
                G.inv[it][0] = G.inv[it][1] = G.inv[it][2] = 0;
            }
            G.ship_value += total;
            sfx(SFX_COIN);
            hud_flash("Shipped!");
            return;
        }
        case DK_WELL:
            G.water = 30 + G.tier[TT_CAN] * 20;
            sfx(SFX_WATER); hud_flash("Watering can full!");
            return;
        case DK_MAILBOX: read_mail(); return;
        case DK_SIGN:
            s_clr(); s_add(G.farm_name); s_add(" Farms\nEst. by Grandpa, rebuilt by you.");
            say(sbuf);
            return;
        case DK_BEEHIVE:
            if (G.a2[i] >= 3) { G.a2[i] = 0; gain_item(I_HONEY, 0, 1); }
            else say("The bees are busy making honey.");
            return;
        case DK_SCARECROW: say("It keeps crows off crops within 4 steps."); return;
        case DK_SPRINKLER: say("It waters the 8 tiles around it every morning."); return;
        case DK_QSPRINKLER: say("It waters every tile within 2 steps each morning."); return;
        case DK_ROD: say("It protects the whole farm from lightning."); return;
        case DK_BENCH: case DK_PICNIC: say("A nice spot to take in the view."); return;
        }
        break;
    case C_TREE: {
        uint8_t kind = G.a1[i] >> 5;
        static const uint8_t fruit[4] = {I_APPLE, I_PEACH, I_ORANGE, I_CHERRY};
        if (G.a2[i] < 10) { say("A young fruit tree. Give it time to grow."); return; }
        if ((G.a1[i] & 31) >= TREE_RIPE_DAYS && season_ok_tree(kind)) {
            uint8_t home = tree_home_season(kind), n = tree_yield(G.a2[i]) + (home ? 2 : 0);
            G.a1[i] &= 0xE0;
            redraw_cell(x, y);
            gain_item(fruit[kind], roll_quality(home), n);
            return;
        }
        if (!season_ok_tree(kind)) say("The tree is resting for winter. It will fruit again in spring.");
        else {
            s_clr(); s_add("The fruit isn't ripe yet. Ready in "); s_num(TREE_RIPE_DAYS - (G.a1[i] & 31), 0);
            s_add(" days. Fruit trees yield more as they age, and most in their own season.");
            say(sbuf);
        }
        return;
    }
    case C_BUSH:
        if (G.a1[i] >= 5) { G.a1[i] = 0; redraw_cell(x, y); gain_item(I_BERRIES, 0, 2 + (rnd() & 1)); return; }
        break;
    case C_FORAGE: {
        static const uint8_t f[3] = {I_BERRIES, I_MUSHROOM, I_HERB};
        uint8_t it = f[G.a1[i] % 3];
        G.cell[i] = C_GRASS; G.a1[i] = 0;
        redraw_cell(x, y);
        gain_item(it, roll_quality(0), 1);
        return;
    }
    case C_GIANT: {
        uint8_t part = G.a1[i] & 3, gx = x - (part & 1), gy = y - (part >> 1);
        uint16_t j = WIDX(gx, gy);
        G.cell[j] = G.cell[j + 1] = G.cell[j + 32] = G.cell[j + 33] = C_SOIL;
        G.a1[j] = G.a1[j + 1] = G.a1[j + 32] = G.a1[j + 33] = 0;
        G.a2[j] = G.a2[j + 1] = G.a2[j + 32] = G.a2[j + 33] = 0;
        redraw_rect(gx, gy, 2, 2);
        gain_item(I_PUMPKIN, 2, 12);
        G.goals |= 1UL << 20;
        return;
    }
    case C_SOIL: {
        uint8_t crop = G.a1[i] & SOIL_CROP;
        if (crop && crop != CROP_WITHERED && SOIL_GROWN(i) >= crops[crop].days) {
            uint8_t rr = SOIL_RARITY(i), q = roll_quality_r(G.a1[i] & SOIL_FERT, rr);
            uint8_t n = rr == R_LEGEND ? 3 + (rnd() & 1) : rr + 1;
            if (crops[crop].regrow) G.a2[i] = (G.a2[i] & SOIL_RAR) | (crops[crop].days - crops[crop].regrow);
            else { G.a1[i] &= SOIL_WET | SOIL_FERT; G.a2[i] = 0; }
            redraw_cell(x, y);
            G.stat_harvest++;
            gain_item(crop - 1, q, n);
            if (rr == R_LEGEND) fx_show(SPR_SPARKLE, SPR_SPARKLE_PAL, x, y);
            if (!(G.tips & 0x08)) {
                G.tips |= 0x08;
                say("Your first harvest! Drop crops in the shipping bin by the house: they sell overnight.");
            }
            goals_check();
            return;
        }
        break;
    }
    }
    use_tool(i, x, y);
}
