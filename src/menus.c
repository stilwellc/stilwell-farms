// Stilwell Farms -- start menu and the build system (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "decor.h"

BANKREF(start_menu)



static void wait_button(void) {
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); return; }
        wait_vbl_done();
    }
}

// ================================================================ build catalogue
enum { BK_DECOR, BK_MACHINE, BK_TREE, BK_POND, BK_FILL, BK_BLDG, BK_MOVE, BK_REMOVE };
typedef struct { uint8_t kind, arg; } BItem;
static const char *const cat_names[6] = {"FARM", "DECOR", "PATHS", "WATER", "BUILDINGS", "MOVE"};
static const BItem cat_farm[] = {
    {BK_DECOR, DK_BIN}, {BK_DECOR, DK_WELL}, {BK_DECOR, DK_MAILBOX}, {BK_DECOR, DK_SCARECROW}, {BK_DECOR, DK_SPRINKLER},
    {BK_DECOR, DK_QSPRINKLER}, {BK_DECOR, DK_BEEHIVE}, {BK_DECOR, DK_ROD}, {BK_MACHINE, MK_KEG}, {BK_MACHINE, MK_PRESS},
    {BK_MACHINE, MK_JAR}, {BK_TREE, 0}, {BK_TREE, 1}, {BK_TREE, 2}, {BK_TREE, 3}, {BK_DECOR, DK_TROUGH}, {BK_DECOR, DK_HAYBALE}};
static const BItem cat_decor[] = {
    {BK_DECOR, DK_FLOWERBED}, {BK_DECOR, DK_FLOWERS_RED}, {BK_DECOR, DK_FLOWERS_BLUE}, {BK_DECOR, DK_FLOWERS_YELLOW},
    {BK_DECOR, DK_HEDGE}, {BK_DECOR, DK_STATUE}, {BK_DECOR, DK_FOUNTAIN}, {BK_DECOR, DK_ARCH}, {BK_DECOR, DK_PICNIC},
    {BK_DECOR, DK_STRING}, {BK_DECOR, DK_LAMP}, {BK_DECOR, DK_BENCH}, {BK_DECOR, DK_SIGN}};
static const BItem cat_paths[] = {
    {BK_DECOR, DK_PATH}, {BK_DECOR, DK_BRICK}, {BK_DECOR, DK_GRAVEL}, {BK_DECOR, DK_PLANK},
    {BK_DECOR, DK_FENCE}, {BK_DECOR, DK_PICKET}, {BK_DECOR, DK_STONEWALL}, {BK_DECOR, DK_MODERN}};
static const BItem cat_water[] = {{BK_POND, 0}, {BK_DECOR, DK_BRIDGE}, {BK_FILL, 0}};
static const BItem cat_bldg[] = {
    {BK_BLDG, B_BARN}, {BK_BLDG, B_COOP}, {BK_BLDG, B_GREENHOUSE}, {BK_BLDG, B_SILO}, {BK_BLDG, B_SHED},
    {BK_BLDG, B_STABLE}, {BK_BLDG, B_WINDMILL}, {BK_BLDG, B_STAND}, {BK_BLDG, B_GAZEBO}};
static const BItem cat_move[] = {{BK_MOVE, 0}, {BK_REMOVE, 0}};
static const BItem *const cats[6] = {cat_farm, cat_decor, cat_paths, cat_water, cat_bldg, cat_move};
static const uint8_t cat_n[6] = {17, 13, 8, 3, 9, 2};

static const char *const machine_names[3] = {"Keg", "Cheese Press", "Preserve Jar"};
static const char *const tree_names[4] = {"Apple Tree", "Peach Tree", "Orange Tree", "Cherry Tree"};
static const char *const bldg_names[NUM_BTYPES] = {"House", "Barn", "Coop", "Greenhouse", "Silo", "Tool Shed", "Stable", "Windmill", "Farm Stand", "Gazebo"};
static const uint16_t bldg_gold[NUM_BTYPES] = {0, 6000, 3000, 25000, 1000, 2000, 5000, 4000, 1500, 2500};
static const uint8_t bldg_wood[NUM_BTYPES] = {0, 150, 80, 100, 20, 80, 120, 60, 40, 60};
static const uint8_t bldg_stone[NUM_BTYPES] = {0, 50, 0, 150, 50, 0, 20, 40, 0, 10};

static void item_cost(const BItem *b, uint16_t *g, uint8_t *w, uint8_t *s) {
    *g = 0; *w = 0; *s = 0;
    switch (b->kind) {
    case BK_DECOR: *g = decor[b->arg].gold; *w = decor[b->arg].wood; *s = decor[b->arg].stone; break;
    case BK_MACHINE: *g = b->arg == MK_KEG ? 2000 : 1500; *w = 20; *s = b->arg == MK_PRESS ? 10 : 0; break;
    case BK_TREE: *g = 400 + b->arg * 100; break;
    case BK_POND: *g = 1000; break;
    case BK_FILL: *g = 100; *s = 2; break;
    case BK_BLDG: *g = bldg_gold[b->arg]; *w = bldg_wood[b->arg]; *s = bldg_stone[b->arg]; break;
    }
}

static const char *item_name(const BItem *b) {
    switch (b->kind) {
    case BK_DECOR: return decor[b->arg].name;
    case BK_MACHINE: return machine_names[b->arg];
    case BK_TREE: return tree_names[b->arg];
    case BK_POND: return "Dig Pond";
    case BK_FILL: return "Fill Water";
    case BK_BLDG: return bldg_names[b->arg];
    case BK_MOVE: return "Move";
    default: return "Remove";
    }
}

// carried object while moving
static uint8_t carry_kind = 0xFF, carry_a, carry_b, carry_slot;   // carry_kind = cell type, or C_STRUCT

static const BItem *cur_item(void) { return &cats[G.build_cat][G.build_sel]; }

static uint8_t clear_cell(uint8_t x, uint8_t y) {
    uint8_t c;
    if (x >= WW || y >= WH || !owned(x, y)) return 0;
    if (x == G.px && y == G.py) return 0;
    if (actor_at(x, y) != 0xFF) return 0;
    c = G.cell[WIDX(x, y)];
    return c == C_GRASS || c == C_GRASS2 || c == C_WILDFLOWER || c == C_DIRT;
}

// footprint of the current build action in front of the player; returns 1 if it can be placed
uint8_t build_cursor(uint8_t *x0, uint8_t *y0, uint8_t *w, uint8_t *h) BANKED {
    uint8_t fx, fy, bw = 1, bh = 1, i, j, ok = 1;
    const BItem *b = cur_item();
    if (IS_INTERIOR(scene)) return furn_cursor(x0, y0, w, h);
    if (!front_cell(&fx, &fy)) { *w = 0; return 0; }
    if (b->kind == BK_BLDG) { bw = bld_w(b->arg); bh = bld_h(b->arg); }
    if (b->kind == BK_MOVE && carry_kind == C_STRUCT) { bw = bld_w(G.bld[carry_slot].type); bh = bld_h(G.bld[carry_slot].type); }
    // anchor: the footprint's near edge sits on the front cell, centred
    {
        int8_t ax = fx, ay = fy;
        if (G.facing == D_UP) { ax -= bw >> 1; ay -= bh - 1; }
        else if (G.facing == D_DOWN) { ax -= bw >> 1; }
        else if (G.facing == D_LEFT) { ax -= bw - 1; ay -= bh >> 1; }
        else { ay -= bh >> 1; }
        if (ax < 0 || ay < 0) { *w = 0; return 0; }
        *x0 = ax; *y0 = ay;
    }
    *w = bw; *h = bh;
    if (b->kind == BK_REMOVE || (b->kind == BK_MOVE && carry_kind == 0xFF)) return 1;
    for (j = 0; j < bh; j++)
        for (i = 0; i < bw; i++) {
            uint8_t x = *x0 + i, y = *y0 + j, c;
            if (x >= WW || y >= WH) return 0;
            c = G.cell[WIDX(x, y)];
            if (b->kind == BK_DECOR && b->arg == DK_BRIDGE) ok &= c == C_WATER && owned(x, y);
            else if (b->kind == BK_FILL) ok &= c == C_WATER && owned(x, y);
            else if (b->kind == BK_DECOR && (b->arg >= DK_PATH && b->arg <= DK_PLANK)) ok &= clear_cell(x, y) || (c == C_SOIL && owned(x, y));
            else ok &= clear_cell(x, y);
        }
    return ok;
}

static void place_decor_cell(uint8_t x, uint8_t y, uint8_t kind) {
    uint16_t i = WIDX(x, y);
    G.cell[i] = C_DECOR; G.a1[i] = kind; G.a2[i] = 0;
    if (kind == DK_BRIDGE) G.a2[i] = G.facing <= D_UP;
    fix_autotile(x, y);
}

static void remove_at(uint8_t x, uint8_t y) {
    uint16_t i = WIDX(x, y);
    uint8_t c = G.cell[i];
    if (c == C_STRUCT) {
        uint8_t slot = G.a1[i];
        Building *b = &G.bld[slot];
        if (b->type == B_HOUSE) { hud_flash("You can't remove your house."); return; }
        s_clr(); s_add("Demolish the "); s_add(bldg_names[b->type]); s_add("? It's gone for good.");
        if (!ui_yesno(sbuf)) return;
        {
            uint8_t bx = b->x, by = b->y, bw = bld_w(b->type), bh = bld_h(b->type), k;
            for (k = 0; k < MAX_ANIMALS; k++) if (G.anim[k].kind != 0xFF && G.anim[k].home == slot) G.anim[k].kind = 0xFF;
            bld_remove(slot);
            redraw_rect(bx, by, bw, bh);
        }
        return;
    }
    if (c == C_DECOR || c == C_MACHINE || c == C_TREE) {
        uint8_t was_fence = c == C_DECOR && decor[G.a1[i]].fence, was_bridge = c == C_DECOR && G.a1[i] == DK_BRIDGE;
        G.cell[i] = was_bridge ? C_WATER : C_GRASS; G.a1[i] = 0; G.a2[i] = 0;
        if (was_fence || was_bridge) fix_autotile(x, y); else redraw_cell(x, y);
        fx_show(SPR_DUST, SPR_DUST_PAL, x, y);
        sfx(SFX_HIT);
        hud_flash("Removed.");
        return;
    }
    hud_flash("Nothing to remove there.");
}

void build_place(void) BANKED {
    uint8_t x0, y0, w, h, ok;
    uint16_t g; uint8_t wd, st;
    const BItem *b = cur_item();
    if (IS_INTERIOR(scene)) { furn_place(); return; }
    ok = build_cursor(&x0, &y0, &w, &h);
    if (!w) return;
    if (b->kind == BK_REMOVE) { remove_at(x0, y0); return; }
    if (b->kind == BK_MOVE) {
        uint16_t i = WIDX(x0, y0);
        if (carry_kind == 0xFF) {  // pick up
            uint8_t c = G.cell[i];
            if (!owned(x0, y0)) return;
            if (c == C_STRUCT) {
                Building *bb;
                carry_slot = G.a1[i];
                bb = &G.bld[carry_slot];
                carry_kind = C_STRUCT;
                {
                    uint8_t bx = bb->x, by = bb->y, bw = bld_w(bb->type), bh = bld_h(bb->type), xx, yy;
                    for (yy = 0; yy < bh; yy++)
                        for (xx = 0; xx < bw; xx++) {
                            uint16_t j = WIDX(bx + xx, by + yy);
                            G.cell[j] = C_GRASS; G.a1[j] = 0; G.a2[j] = 0;
                        }
                    redraw_rect(bx, by, bw, bh);
                }
                sfx(SFX_CONFIRM);
                hud_flash("Picked up! A to set it down.");
            } else if (c == C_DECOR || c == C_MACHINE || c == C_TREE) {
                uint8_t was_fence = c == C_DECOR && decor[G.a1[i]].fence;
                if (c == C_DECOR && G.a1[i] == DK_BRIDGE) return;
                carry_kind = c; carry_a = G.a1[i]; carry_b = G.a2[i];
                G.cell[i] = C_GRASS; G.a1[i] = 0; G.a2[i] = 0;
                if (was_fence) fix_autotile(x0, y0); else redraw_cell(x0, y0);
                sfx(SFX_CONFIRM);
                hud_flash("Picked up! A to set it down.");
            }
            return;
        }
        if (!ok) { sfx(SFX_BUMP); hud_flash("Can't put it there."); return; }
        if (carry_kind == C_STRUCT) {
            Building *bb = &G.bld[carry_slot];
            uint8_t xx, yy, p = 0;
            bb->x = x0; bb->y = y0;
            for (yy = 0; yy < h; yy++)
                for (xx = 0; xx < w; xx++) {
                    uint16_t j = WIDX(x0 + xx, y0 + yy);
                    G.cell[j] = C_STRUCT; G.a1[j] = carry_slot; G.a2[j] = p++;
                }
            redraw_rect(x0, y0, w, h);
        } else {
            G.cell[i] = carry_kind; G.a1[i] = carry_a; G.a2[i] = carry_b;
            if (carry_kind == C_DECOR) fix_autotile(x0, y0); else redraw_cell(x0, y0);
        }
        carry_kind = 0xFF;
        fx_show(SPR_DUST, SPR_DUST_PAL, x0, y0);
        sfx(SFX_HIT);
        return;
    }
    if (!ok) { sfx(SFX_BUMP); hud_flash("Can't build there."); return; }
    item_cost(b, &g, &wd, &st);
    if (G.gold < g || G.wood < wd || G.stone < st) { sfx(SFX_BUMP); hud_flash("Not enough materials."); return; }
    if (b->kind == BK_BLDG) {
        s_clr(); s_add("Build a "); s_add(bldg_names[b->arg]); s_add(" here?\n"); cost_str(g, wd, st);
        if (!ui_yesno(sbuf)) return;
        if (bld_new(b->arg, x0, y0, 0) == 0xFF) { say("You've built as much as the farm can hold!"); return; }
        redraw_rect(x0, y0, w, h);
        music_play(MUS_JINGLE_FANFARE);
        say("Built! Your farm is growing.");
        music_for_scene();
    } else {
        uint16_t i = WIDX(x0, y0);
        switch (b->kind) {
        case BK_DECOR: place_decor_cell(x0, y0, b->arg); break;
        case BK_MACHINE: G.cell[i] = C_MACHINE; G.a1[i] = b->arg; G.a2[i] = 0; redraw_cell(x0, y0); break;
        case BK_TREE: G.cell[i] = C_TREE; G.a1[i] = b->arg << 5; G.a2[i] = 0; redraw_cell(x0, y0); break;
        case BK_POND: G.cell[i] = C_WATER; G.a1[i] = 0; G.a2[i] = 0; fix_autotile(x0, y0); break;
        case BK_FILL: G.cell[i] = C_GRASS; G.a1[i] = 0; G.a2[i] = 0; fix_autotile(x0, y0); break;
        }
        sfx(SFX_HIT);
        fx_show(SPR_DUST, SPR_DUST_PAL, x0, y0);
    }
    G.gold -= g; G.wood -= wd; G.stone -= st;
    goals_check();
}

static uint8_t build_menu(void) {
    uint8_t c, k, r, n;
    c = ui_menu(0, 0, 11, cat_names, 6, G.build_cat);
    if (c == 0xFF) return 0;
    n = cat_n[c];
    while (1) {
        uint8_t first = 0, show;
        ui_end(); ui_begin();
        // page through long lists 12 at a time
        show = n;
        if (n > 12) show = 12;
        for (k = 0; k < show; k++) {
            const BItem *b = &cats[c][k + first];
            uint16_t g; uint8_t w, s;
            uint8_t len;
            item_cost(b, &g, &w, &s);
            s_clr(); cost_str(g, w, s);
            len = s_len();
            strcpy(lbl[13], sbuf);
            s_clr(); s_add(item_name(b)); s_pad(17 - len); s_add(lbl[13]);
            lbl_set(k);
        }
        if (n > 12) {
            // second page via the last entry
            s_clr(); s_add("More..."); lbl_set(12);
            r = ui_menu(0, 0, 20, lp, 13, G.build_cat == c ? G.build_sel : 0);
            if (r == 12) {
                ui_end(); ui_begin();
                for (k = 12; k < n; k++) {
                    const BItem *b = &cats[c][k];
                    uint16_t g; uint8_t w, s, len;
                    item_cost(b, &g, &w, &s);
                    s_clr(); cost_str(g, w, s); len = s_len(); strcpy(lbl[13], sbuf);
                    s_clr(); s_add(item_name(b)); s_pad(17 - len); s_add(lbl[13]);
                    lbl_set(k - 12);
                }
                r = ui_menu(0, 0, 20, lp, n - 12, 0);
                if (r != 0xFF) r += 12;
            }
        } else r = ui_menu(0, 0, 20, lp, n, G.build_cat == c ? G.build_sel : 0);
        if (r == 0xFF) return 0;
        G.build_cat = c; G.build_sel = r;
        carry_kind = 0xFF;
        building = 1;
        ui_dialog(c == 5 ? (r == 0 ? "Face something you built and press A to pick it up, then A again to set it down."
                                   : "Face something you built and press A to remove it.")
                         : "A places it in front of you. B or SELECT when you're done.");
        return 1;
    }
}

// ================================================================ start menu pages
static const char *const tool_names[NUM_TOOLS] = {"Hands", "Hoe", "Watering Can", "Seeds", "Sickle", "Axe", "Hammer", "Fishing Rod", "Fertilizer"};
static const char *const tool_help[NUM_TOOLS] = {
    "Pull weeds, harvest, and check how crops are doing.",
    "Till grass into soil for planting.",
    "Water soil so crops grow overnight. Refill at any water.",
    "Plant seeds in tilled soil.",
    "Cut weeds, clear dead crops, and cut grass into hay.",
    "Chop stumps and trees into wood. Trees grow back.",
    "Smash rocks into stone and ore into metal.",
    "Face any water and press A to fish.",
    "Fertilize soil for better quality crops."};
static const char *const tier_names[4] = {"", " (Copper)", " (Iron)", " (Gold)"};

static uint8_t tools_page(void) {
    uint8_t k, r, t;
    for (k = 0; k < NUM_TOOLS; k++) {
        s_clr(); s_add(tool_names[k]);
        t = k == T_HOE ? G.tier[TT_HOE] : k == T_CAN ? G.tier[TT_CAN] : k == T_AXE ? G.tier[TT_AXE] : k == T_HAMMER ? G.tier[TT_HAMMER] : k == T_ROD ? G.tier[TT_ROD] : 0;
        if (t) { s_ch(' '); s_ch(t == 1 ? 'C' : t == 2 ? 'I' : 'G'); }
        if (k == T_CAN) { s_pad(15); s_num(G.water, 2); }
        if (k == T_FERT) { s_pad(15); s_num(G.fert, 2); }
        lbl_set(k);
    }
    r = ui_menu(0, 0, 20, lp, NUM_TOOLS, G.tool);
    if (r == 0xFF) return 0;
    if (r == T_SEEDS && !G.seed_sel) G.seed_sel = 1;
    G.tool = r;
    building = 0;
    ui_dialog(tool_help[r]);
    (void)tier_names;
    return 1;
}

static uint8_t seeds_page(void) {
    uint8_t k, n = 0, map[NUM_CROPS], r;
    for (k = 1; k <= NUM_CROPS; k++) {
        if (!G.seeds[k]) continue;
        s_clr(); s_ch((char)(0x80 + k - 1)); s_ch(' '); s_add(items[k - 1].name); s_pad(13); s_ch('x'); s_num(G.seeds[k], 2);
        lbl_set(n);
        map[n++] = k;
        if (n == 10) break;
    }
    if (!n) { ui_dialog("You have no seeds. The General Store in town sells them by season."); return 0; }
    {
        uint8_t hv = 0;
        ui_hover = &hv;
        while (1) {
            crop_panel(map[hv]);
            r = ui_menu(0, 0, 20, lp, n, hv);
            if (r != 0xFE) break;
        }
        ui_hover = 0;
    }
    if (r == 0xFF) return 0;
    k = map[r];
    G.seed_sel = k;
    G.tool = T_SEEDS;
    building = 0;
    s_clr(); s_add(items[k - 1].name); s_add(" ("); s_add(season_name[crops[k].season]); s_add("): ripe in ");
    s_num(crops[k].days, 0); s_add(" days");
    if (crops[k].regrow) { s_add(", regrows every "); s_num(crops[k].regrow, 0); }
    s_add(". Sells for G"); s_num(items[k - 1].price, 0); s_ch('.');
    ui_dialog(sbuf);
    return 1;
}

static void bag_page(void) {
    uint8_t it, q, n, sel = 0, top = 0, k, t = 0;
    uint8_t mi[48], mq[48];
    n = 0;
    for (it = 0; it < NUM_ITEMS; it++)
        for (q = 0; q < 3; q++)
            if (G.inv[it][q] && n < 48) { mi[n] = it; mq[n] = q; n++; }
    while (1) {
        ui_box(0, 0, 20, 12);
        ui_title(2, 0, "BAG");
        if (!n) ui_text(2, 2, "Nothing yet!");
        for (k = 0; k < 9 && top + k < n; k++) {
            it = mi[top + k]; q = mq[top + k];
            s_clr(); s_ch((char)(0x80 + it)); s_ch(' '); s_add(items[it].name);
            if (q) s_ch(q == 2 ? (char)0xF1 : (char)0xF0);
            s_pad(14); s_ch('x'); s_num(G.inv[it][q], 3);
            ui_text(2, 1 + k, sbuf);
        }
        if (top + 9 < n) ui_char(18, 10, GL_MORE);
        s_clr(); s_add("W"); s_num(G.wood, 0); s_add(" S"); s_num(G.stone, 0); s_add(" Hay"); s_num(G.hay, 0);
        ui_text(2, 10, sbuf);
        if (n) item_panel(mi[sel], mq[sel]);
        else { ui_panel(0, 12, 20, 5); s_clr(); s_add("Fert "); s_num(G.fert, 0); s_add("  Water "); s_num(G.water, 0); ui_text(2, 14, sbuf); }
        while (1) {
            if (n) ui_char(1, 1 + sel - top, (t & 16) ? GL_CURSOR2 : GL_CURSOR);
            read_input();
            t++;
            if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); return; }
            if (n && (joy_press & (J_UP | J_DOWN))) {
                ui_char(1, 1 + sel - top, GL_FILL);
                if (joy_press & J_DOWN) sel = sel + 1 < n ? sel + 1 : 0; else sel = sel ? sel - 1 : n - 1;
                if (sel < top) top = sel;
                if (sel >= top + 9) top = sel - 8;
                sfx(SFX_CURSOR);
                break;
            }
            wait_vbl_done();
        }
    }
}

static const char *const npc_names[NUM_NPC] = {"Rosa", "Brick", "June", "Ada", "Mayor Hollis", "Milo"};

static void friends_page(void) {
    uint8_t k, h, j;
    ui_box(0, 0, 20, 17);
    ui_title(2, 0, "FRIENDS");
    for (k = 0; k < NUM_NPC; k++) {
        ui_text(2, 2 + k * 2, npc_names[k]);
        h = G.friend[k] / 50;
        for (j = 0; j < 5; j++) ui_char(13 + j, 2 + k * 2, j < h ? GL_HEART : GL_HEART_EMPTY);
    }
    s_clr(); s_add(G.pet_name); ui_text(2, 14, sbuf);
    h = G.pet_love / 50;
    for (j = 0; j < 5; j++) ui_char(13 + j, 14, j < h ? GL_HEART : GL_HEART_EMPTY);
    wait_button();
}

static void farm_page(void) {
    uint8_t k, segs, plots = 0, blds = 0, anim = 0;
    for (k = 0; k < 16; k++) if (G.plots & (1 << k)) plots++;
    for (k = 0; k < MAX_BLD; k++) if (G.bld[k].type != 0xFF) blds++;
    for (k = 0; k < MAX_ANIMALS; k++) if (G.anim[k].kind != 0xFF) anim++;
    ui_box(0, 0, 20, 17);
    s_clr(); s_add(G.farm_name); s_add(" Farms");
    ui_title(1, 0, sbuf);
    s_clr(); s_add(season_name[G.season]); s_ch(' '); s_num(G.day, 0); s_add(", Year "); s_num(G.year, 0);
    ui_text(2, 2, sbuf);
    s_clr(); s_add("Gold"); s_pad(8); s_num(G.gold, 8); ui_text(2, 4, sbuf);
    ui_text(2, 5, "Energy");
    segs = (uint8_t)(((uint16_t)G.energy * 10 + G.energy_max - 1) / G.energy_max);
    for (k = 0; k < 10; k++) ui_char(8 + k, 5, k < segs ? GL_BAR_FULL : GL_BAR_EMPTY);
    s_clr(); s_add("Land"); s_pad(8); s_num(plots, 2); s_add(" of 16"); ui_text(2, 7, sbuf);
    s_clr(); s_add("Buildings"); s_pad(13); s_num(blds, 2); ui_text(2, 8, sbuf);
    s_clr(); s_add("Animals"); s_pad(13); s_num(anim, 2); ui_text(2, 9, sbuf);
    s_clr(); s_add("Wood "); s_num(G.wood, 0); s_add(" Stone "); s_num(G.stone, 0); ui_text(2, 11, sbuf);
    s_clr(); s_add("Harvests"); s_pad(10); s_num(G.stat_harvest, 5); ui_text(2, 13, sbuf);
    s_clr(); s_add("Earned"); s_pad(8); s_num(G.earned, 8); ui_text(2, 15, sbuf);
    wait_button();
}

static const char *const farm_items[6] = {"STATUS", "MAP", "CALENDAR", "FRIENDS", "GOODS", "BACK"};
static void farm_menu(void) {
    uint8_t r = ui_menu(8, 2, 12, farm_items, 6, 0);
    ui_end(); ui_begin();
    if (r == 0) farm_page();
    else if (r == 1) map_page();
    else if (r == 2) calendar_page();
    else if (r == 3) friends_page();
    else if (r == 4) collection_page();
}

static const char *const sm_items[8] = {"TOOLS", "SEEDS", "BAG", "BUILD", "FARM", "OPTIONS", "SAVE", "EXIT"};

void start_menu(void) BANKED {
    static uint8_t sel;
    uint8_t r, done = 0;
    while (!done) {
        ui_begin();
        status_panel();
        r = ui_menu(11, 0, 9, sm_items, 8, sel);
        if (r != 0xFF) sel = r;
        if (r < 6) { ui_end(); ui_begin(); }
        switch (r) {
        case 0: done = tools_page(); break;
        case 1: done = seeds_page(); break;
        case 2: bag_page(); break;
        case 3:
            if (IS_INTERIOR(scene)) done = furn_menu();
            else if (scene != SCENE_FARM) ui_dialog("You can only build on your farm.");
            else done = build_menu();
            break;
        case 4: farm_menu(); break;
        case 5: options_menu(); break;
        case 6:
            ui_end();
            if (ui_yesno("Save your farm?")) { save_write(); say("Your farm has been saved."); }
            ui_begin();
            break;
        default: done = 1; break;
        }
        ui_end();
    }
}
