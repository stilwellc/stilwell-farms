// Stilwell Farms -- start menu and the build system (banked)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "decor.h"
#include "designer.h"

BANKREF(start_menu)



static void wait_button(void) {
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); return; }
        wait_vbl_done();
    }
}

// ================================================================ build catalogue
static const char *const cat_names[8] = {"FARM", "DECOR", "PATHS", "WATER", "BUILDINGS", "MOVE", "BLUEPRINT", "COLOURS"};
typedef struct { uint8_t kind, arg; } BItem;
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
static const BItem cat_blue[] = {{BK_COPY, 0}, {BK_STAMP, 0}, {BK_STAMP, 1}};
static const BItem *const cats[8] = {cat_farm, cat_decor, cat_paths, cat_water, cat_bldg, cat_move, cat_blue, cat_blue};
static const uint8_t cat_n[8] = {17, 13, 8, 3, 9, 2, 3, 1};   // COLOURS opens the colour page, not a list

static const char *const machine_names[3] = {"Keg", "Cheese Press", "Preserve Jar"};
static const char *const tree_names[4] = {"Apple Tree", "Peach Tree", "Orange Tree", "Cherry Tree"};
static const char *const bldg_names[NUM_BTYPES] = {"House", "Barn", "Coop", "Greenhouse", "Silo", "Tool Shed", "Stable", "Windmill", "Farm Stand", "Gazebo"};
static const uint16_t bldg_gold[NUM_BTYPES] = {0, 6000, 3000, 40000, 1000, 2000, 5000, 4000, 1500, 2500};
static const uint8_t bldg_wood[NUM_BTYPES] = {0, 150, 80, 100, 20, 80, 120, 60, 40, 60};
static const uint8_t bldg_stone[NUM_BTYPES] = {0, 50, 0, 150, 50, 0, 20, 40, 0, 10};

void bk_cost(uint8_t kind, uint8_t arg, uint16_t *g, uint8_t *w, uint8_t *s) BANKED {
    *g = 0; *w = 0; *s = 0;
    switch (kind) {
    case BK_DECOR: *g = decor[arg].gold; *w = decor[arg].wood; *s = decor[arg].stone; break;
    case BK_MACHINE: *g = arg == MK_JAR ? 1200 : 1500; *w = 20; *s = arg == MK_PRESS ? 10 : 0; break;
    case BK_TREE: *g = 800 + arg * 150; break;
    case BK_POND: *g = 1000; break;
    case BK_FILL: *g = 100; *s = 2; break;
    case BK_BLDG: *g = bldg_gold[arg]; *w = bldg_wood[arg]; *s = bldg_stone[arg]; break;
    }
}
#define item_cost(b, g, w, s) bk_cost((b)->kind, (b)->arg, g, w, s)

static const char *item_name(const BItem *b) {
    switch (b->kind) {
    case BK_DECOR: return decor[b->arg].name;
    case BK_MACHINE: return machine_names[b->arg];
    case BK_TREE: return tree_names[b->arg];
    case BK_POND: return "Dig Pond";
    case BK_FILL: return "Fill Water";
    case BK_BLDG: return bldg_names[b->arg];
    case BK_MOVE: return "Move";
    case BK_COPY: return "Copy Area";
    case BK_STAMP: return b->arg ? "Blueprint 2" : "Blueprint 1";
    default: return "Remove";
    }
}

// carried object while moving
static uint8_t carry_kind = 0xFF, carry_a, carry_b, carry_slot, carry_x, carry_y;   // carry_kind = cell type, or C_STRUCT

static const BItem *cur_item(void) { return &cats[G.build_cat][G.build_sel]; }

static uint8_t clear_cell(uint8_t x, uint8_t y) {
    uint8_t c;
    if (x >= WW || y >= WH || !owned(x, y)) return 0;
    if (x == G.px && y == G.py) return 0;
    if (actor_blocks(x, y)) return 0;
    c = W_CELL[WIDX(x, y)];
    return c == C_GRASS || c == C_GRASS2 || c == C_WILDFLOWER || c == C_DIRT;
}

// can one cell of this item go at x, y? (paths may also cover tilled soil with no crop in it)
uint8_t bk_fits(uint8_t x, uint8_t y, uint8_t kind, uint8_t arg) BANKED {
    uint16_t i;
    uint8_t c;
    if (x >= WW || y >= WH) return 0;
    i = WIDX(x, y);
    c = W_CELL[i];
    if ((kind == BK_DECOR && arg == DK_BRIDGE) || kind == BK_FILL) return c == C_WATER && owned(x, y);
    if (kind == BK_DECOR && arg >= DK_PATH && arg <= DK_PLANK && c == C_SOIL && !(W_A1[i] & SOIL_CROP) && owned(x, y)) return 1;
    return clear_cell(x, y);
}

// footprint of the current build action in front of the player; returns 1 if it can be placed
uint8_t build_cursor(uint8_t *x0, uint8_t *y0, uint8_t *w, uint8_t *h) BANKED {
    uint8_t fx, fy, bw = 1, bh = 1, i, j, ok = 1;
    const BItem *b = cur_item();
    if (IS_INTERIOR(scene)) return furn_cursor(x0, y0, w, h);
    if (!front_cell(&fx, &fy)) { *w = 0; ghost_hide(); return 0; }
    if (b->kind == BK_COPY) return bp_copy_cursor(fx, fy, x0, y0, w, h);
    if (b->kind == BK_BLDG) { bw = bld_w(b->arg); bh = bld_h(b->arg); }
    if (b->kind == BK_STAMP && G.bp_wh[b->arg]) { bw = G.bp_wh[b->arg] >> 4; bh = G.bp_wh[b->arg] & 15; }
    if (b->kind == BK_MOVE && carry_kind == C_STRUCT) { bw = bld_w(G.bld[carry_slot].type); bh = bld_h(G.bld[carry_slot].type); }
    // anchor: the footprint's near edge sits on the front cell, centred
    {
        int8_t ax = fx, ay = fy;
        if (G.facing == D_UP) { ax -= bw >> 1; ay -= bh - 1; }
        else if (G.facing == D_DOWN) { ax -= bw >> 1; }
        else if (G.facing == D_LEFT) { ax -= bw - 1; ay -= bh >> 1; }
        else { ay -= bh >> 1; }
        if (ax < 0 || ay < 0) { *w = 0; ghost_hide(); return 0; }
        *x0 = ax; *y0 = ay;
    }
    *w = bw; *h = bh;
    if (b->kind == BK_STAMP) return G.bp_wh[b->arg] ? bp_ghost(b->arg, *x0, *y0) : 0;
    if (b->kind == BK_REMOVE || (b->kind == BK_MOVE && carry_kind == 0xFF)) return 1;
    for (j = 0; j < bh; j++)
        for (i = 0; i < bw; i++) ok &= bk_fits(*x0 + i, *y0 + j, b->kind, b->arg);
    return ok;
}

// a one-cell item onto the map (checks and cost are the caller's)
void bk_put(uint8_t x, uint8_t y, uint8_t kind, uint8_t arg) BANKED {
    uint16_t i = WIDX(x, y);
    switch (kind) {
    case BK_DECOR:
        W_CELL[i] = C_DECOR; W_A1[i] = arg; W_A2_SET(i, 0);
        if (arg == DK_BRIDGE) W_A2_SET(i, G.facing <= D_UP);
        fix_autotile(x, y);
        break;
    case BK_MACHINE: W_CELL[i] = C_MACHINE; W_A1[i] = arg; W_A2_SET(i, 0); redraw_cell(x, y); break;
    case BK_TREE: W_CELL[i] = C_TREE; W_A1[i] = arg << 5; W_A2_SET(i, 0); redraw_cell(x, y); break;
    case BK_POND: W_CELL[i] = C_WATER; W_A1[i] = 0; W_A2_SET(i, 0); fix_autotile(x, y); break;
    case BK_FILL: W_CELL[i] = C_GRASS; W_A1[i] = 0; W_A2_SET(i, 0); fix_autotile(x, y); break;
    }
}

static void remove_at(uint8_t x, uint8_t y) {
    uint16_t i = WIDX(x, y);
    uint8_t c = W_CELL[i];
    if (c == C_STRUCT) {
        uint8_t slot = W_A1[i];
        Building *b = &G.bld[slot];
        if (b->type == B_HOUSE) { hud_flash("Can't remove house"); return; }
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
    paint_begin(BK_REMOVE, 0);          // the eraser: hold A and walk to clear a line
    if (erase_at(x, y)) hud_flash("Removed.");
    else hud_flash("Nothing to remove.");
}

// leave build mode; anything still in hand goes back where it was picked up
void build_cancel(void) BANKED {
    if (paint_on) paint_end();
    if (carry_kind == C_STRUCT) {
        Building *bb = &G.bld[carry_slot];
        uint8_t xx, yy, p = 0, w = bld_w(bb->type), h = bld_h(bb->type);
        for (yy = 0; yy < h; yy++)
            for (xx = 0; xx < w; xx++) {
                uint16_t j = WIDX(bb->x + xx, bb->y + yy);
                W_CELL[j] = C_STRUCT; W_A1[j] = carry_slot; W_A2_SET(j, p++);
            }
        redraw_rect(bb->x, bb->y, w, h);
    } else if (carry_kind != 0xFF) {
        uint16_t i = WIDX(carry_x, carry_y);
        W_CELL[i] = carry_kind; W_A1[i] = carry_a; W_A2_SET(i, carry_b);
        if (carry_kind == C_DECOR) fix_autotile(carry_x, carry_y); else redraw_cell(carry_x, carry_y);
    }
    carry_kind = 0xFF;
    ghost_hide();
    bp_copy_reset();
    building = 0;
}

void build_place(void) BANKED {
    uint8_t x0, y0, w, h, ok;
    uint16_t g; uint8_t wd, st;
    const BItem *b = cur_item();
    if (IS_INTERIOR(scene)) { furn_place(); return; }
    ok = build_cursor(&x0, &y0, &w, &h);
    if (!w) return;
    if (b->kind == BK_REMOVE) { remove_at(x0, y0); return; }
    if (b->kind == BK_COPY) { bp_copy_click(); return; }
    if (b->kind == BK_STAMP) {
        if (!G.bp_wh[b->arg]) { sfx(SFX_BUMP); hud_flash("Blueprint is empty"); return; }
        bp_stamp(b->arg, x0, y0);
        return;
    }
    if (b->kind == BK_MOVE) {
        uint16_t i = WIDX(x0, y0);
        if (carry_kind == 0xFF) {  // pick up
            uint8_t c = W_CELL[i];
            if (!owned(x0, y0)) return;
            if (c == C_STRUCT) {
                Building *bb;
                carry_slot = W_A1[i];
                bb = &G.bld[carry_slot];
                carry_kind = C_STRUCT;
                {
                    uint8_t bx = bb->x, by = bb->y, bw = bld_w(bb->type), bh = bld_h(bb->type), xx, yy;
                    for (yy = 0; yy < bh; yy++)
                        for (xx = 0; xx < bw; xx++) {
                            uint16_t j = WIDX(bx + xx, by + yy);
                            W_CELL[j] = C_GRASS; W_A1[j] = 0; W_A2_SET(j, 0);
                        }
                    redraw_rect(bx, by, bw, bh);
                }
                sfx(SFX_CONFIRM);
                hud_flash("Picked up! A=place");
            } else if (c == C_DECOR || c == C_MACHINE || c == C_TREE) {
                uint8_t was_fence = c == C_DECOR && decor[W_A1[i]].fence;
                if (c == C_DECOR && W_A1[i] == DK_BRIDGE) return;
                carry_kind = c; carry_a = W_A1[i]; carry_b = W_A2(i); carry_x = x0; carry_y = y0;
                W_CELL[i] = C_GRASS; W_A1[i] = 0; W_A2_SET(i, 0);
                if (was_fence) fix_autotile(x0, y0); else redraw_cell(x0, y0);
                sfx(SFX_CONFIRM);
                hud_flash("Picked up! A=place");
            }
            return;
        }
        if (!ok) { sfx(SFX_BUMP); hud_flash("Can't put it here."); return; }
        if (carry_kind == C_STRUCT) {
            Building *bb = &G.bld[carry_slot];
            uint8_t xx, yy, p = 0;
            undo_moveb(carry_slot, bb->x, bb->y);
            bb->x = x0; bb->y = y0;
            for (yy = 0; yy < h; yy++)
                for (xx = 0; xx < w; xx++) {
                    uint16_t j = WIDX(x0 + xx, y0 + yy);
                    W_CELL[j] = C_STRUCT; W_A1[j] = carry_slot; W_A2_SET(j, p++);
                }
            redraw_rect(x0, y0, w, h);
        } else {
            undo_move(WIDX(carry_x, carry_y), i);
            W_CELL[i] = carry_kind; W_A1[i] = carry_a; W_A2_SET(i, carry_b);
            if (carry_kind == C_DECOR) fix_autotile(x0, y0); else redraw_cell(x0, y0);
        }
        carry_kind = 0xFF;
        fx_show(SPR_DUST, SPR_DUST_PAL, x0, y0);
        sfx(SFX_HIT);
        return;
    }
    if (!ok) { sfx(SFX_BUMP); hud_flash("Can't build there."); return; }
    if (b->kind == BK_BLDG) {
        uint8_t slot;
        item_cost(b, &g, &wd, &st);
        if (G.gold < g || G.wood < wd || G.stone < st) { sfx(SFX_BUMP); hud_flash("Need materials."); return; }
        s_clr(); s_add("Build a "); s_add(bldg_names[b->arg]); s_add(" here?\n"); cost_str(g, wd, st);
        if (!ui_yesno(sbuf)) return;
        if ((slot = bld_new(b->arg, x0, y0, 0)) == 0xFF) { say("You've built as much as the farm can hold!"); return; }
        G.gold -= g; G.wood -= wd; G.stone -= st;
        undo_bld(slot);
        redraw_rect(x0, y0, w, h);
        music_play(MUS_JINGLE_FANFARE);
        say("Built! Your farm is growing.");
        music_for_scene();
        goals_check();
        return;
    }
    // one-cell items: a paint stroke. A tap places it in front when A is let go; walking with A held
    // lays a line behind you instead (designer.c)
    paint_begin(b->kind, b->arg);
}

// SELECT in build mode swaps between the eraser and the item in hand (while carrying something,
// or indoors, it still puts things back and leaves build mode)
static uint8_t prev_cat, prev_sel;
void build_select(void) BANKED {
    if (IS_INTERIOR(scene) || carry_kind != 0xFF) { build_cancel(); return; }
    if (paint_on) paint_end();
    ghost_hide();
    bp_copy_reset();
    s_clr();
    if (G.build_cat == CAT_MOVE && G.build_sel == 1) {
        if (prev_cat == CAT_MOVE && prev_sel == 1) prev_sel = 0;
        G.build_cat = prev_cat; G.build_sel = prev_sel;
        s_add(item_name(cur_item()));
    } else {
        prev_cat = G.build_cat; prev_sel = G.build_sel;
        G.build_cat = CAT_MOVE; G.build_sel = 1;
        s_add("Eraser");
    }
    sfx(SFX_CONFIRM);
    hud_flash(sbuf);
}

// The lists hold names only (a name plus its full cost never fit 17 columns); the hovered entry's cost
// shows in a panel on top, and the list sits under it at rows 4..16: up to 11 entries, else 10 + "More...".
#define BPAGE 10
static void build_panel(const BItem *b) {
    uint16_t g; uint8_t w, s;
    ui_panel(0, 0, 20, 4);
    if (!b) { ui_text(2, 1, "More choices"); return; }
    if (b->kind == BK_COPY) { ui_text(2, 1, "Save an area as a"); ui_text(2, 2, "blueprint, 8x8 max"); return; }
    if (b->kind == BK_STAMP) {
        uint8_t wh = G.bp_wh[b->arg], k, n = 0, v;
        if (!wh) { ui_text(2, 1, "Empty: use Copy"); ui_text(2, 2, "Area to fill it"); return; }
        for (k = 0; k < BP_MAX * BP_MAX / 2; k++) { v = G.bp_cell[b->arg][k]; n += ((v & 15) != 0) + ((v >> 4) != 0); }
        s_clr(); s_num(wh >> 4, 0); s_ch('x'); s_num(wh & 15, 0); s_add(" area, "); s_num(n, 0); s_add(" pcs");
        ui_text(2, 1, sbuf);
        ui_text(2, 2, "Stamp it anywhere");
        return;
    }
    item_cost(b, &g, &w, &s);
    s_clr();
    if (g || w || s) cost_str(g, w, s); else s_add("Free");
    if ((g || w || s) && s_len() <= 12) { s_clr(); s_add("Cost "); cost_str(g, w, s); }
    ui_text(2, 1, sbuf);
    s_clr(); s_add("Have "); s_num(G.wood, 0); s_add("W "); s_num(G.stone, 0); s_ch('S');
    ui_text(2, 2, sbuf);
}

static uint8_t build_list(uint8_t c, uint8_t first, uint8_t cnt, uint8_t more, uint8_t sel) {
    uint8_t k, r, hv = sel < cnt + more ? sel : 0;
    for (k = 0; k < cnt; k++) {
        const BItem *b = &cats[c][first + k];
        s_clr(); s_add(item_name(b));
        if (b->kind == BK_STAMP && !G.bp_wh[b->arg]) s_add(" -");
        lbl_set(k);
    }
    if (more) { s_clr(); s_add("More..."); lbl_set(cnt); }
    ui_hover = &hv;
    while (1) {
        build_panel(hv < cnt ? &cats[c][first + hv] : 0);
        r = ui_menu(0, 4, 20, lp, cnt + more, hv);
        if (r != 0xFE) break;
    }
    ui_hover = 0;
    return r;
}

static uint8_t build_menu(void) {
    uint8_t c, r, n, sel, k;
    c = ui_menu(0, 0, 12, cat_names, 8, G.build_cat);
    if (c == 0xFF) return 0;
    ui_end(); ui_begin();
    if (c == CAT_COLOURS) { colour_menu(); return 1; }
    n = cat_n[c];
    sel = G.build_cat == c ? G.build_sel : 0;
    if (n > BPAGE + 1) {
        r = build_list(c, 0, BPAGE, 1, sel > BPAGE ? BPAGE : sel);
        if (r == BPAGE) {
            ui_end(); ui_begin();
            r = build_list(c, BPAGE, n - BPAGE, 0, sel >= BPAGE ? sel - BPAGE : 0);
            if (r != 0xFF) r += BPAGE;
        }
    } else r = build_list(c, 0, n, 0, sel);
    if (r == 0xFF) return 0;
    build_cancel();
    G.build_cat = c; G.build_sel = r;
    building = 1;
    k = cats[c][r].kind;
    ui_dialog(k == BK_MOVE ? "Face something you built and press A to pick it up, then A again to set it down."
            : k == BK_REMOVE ? "Face something you built and press A to remove it. Hold A and walk to clear a line."
            : k == BK_COPY ? "Face one corner and press A. Walk to the far corner (8x8 at most) and press A to save it."
            : k == BK_STAMP ? "The ghost shows what fits. A stamps it down: only what fits and what you can pay for is built."
            : "A places it in front of you. Hold A and walk to lay a line. SELECT: eraser. START: undo. B: done.");
    return 1;
}

// START while building on the farm: the quick build tools. Returns 1 to go on to the main menu.
static const char *const quick_help[2] = {"Hold A+walk: paint", "SELECT: eraser"};
uint8_t build_quick(void) BANKED {
    uint8_t r, k, er = G.build_cat == CAT_MOVE && G.build_sel == 1;
    ui_begin();
    s_clr(); s_add("UNDO"); k = undo_count(); if (k) { s_add(" ("); s_num(k, 0); s_ch(')'); } lbl_set(0);
    s_clr(); s_add("ITEMS"); lbl_set(1);
    s_clr(); s_add(er ? "PLACE" : "ERASER"); lbl_set(2);
    s_clr(); s_add("COLOURS"); lbl_set(3);
    s_clr(); s_add("MAIN MENU"); lbl_set(4);
    s_clr(); s_add("DONE"); lbl_set(5);
    ui_box(0, 13, 20, 4);
    ui_title(1, 13, "BUILD");
    for (k = 0; k < 2; k++) ui_text(1, 14 + k, quick_help[k]);
    r = ui_menu(8, 0, 12, lp, 6, 0);
    if (r == 1) { ui_end(); ui_begin(); build_menu(); }
    else if (r == 3) { ui_end(); ui_begin(); colour_menu(); }
    ui_end();
    if (r == 0) undo_last();
    else if (r == 2) build_select();
    else if (r == 4) return 1;
    else if (r == 5) build_cancel();
    return 0;
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
        if (k == T_CAN) { s_pad(14); s_num(G.water, 3); }   // 3 wide: 17 columns with 3-digit fertilizer
        if (k == T_FERT) { s_pad(14); s_num(G.fert, 3); }
        lbl_set(k);
    }
    r = ui_menu(0, 0, 20, lp, NUM_TOOLS, G.tool);
    if (r == 0xFF) return 0;
    if (r == T_SEEDS && !G.seed_sel) G.seed_sel = 1;
    G.tool = r;
    build_cancel();
    ui_dialog(tool_help[r]);
    (void)tier_names;
    return 1;
}

static uint8_t seeds_page(void) {
    uint8_t k, n = 0, map[10], rmap[10], r, rr;
    for (k = 1; k <= NUM_CROPS && n < 10; k++)
        for (rr = 0; rr < NUM_RARITY && n < 10; rr++) {
            if (!G.seeds[k][rr]) continue;
            s_clr(); s_ch((char)(0x80 + k - 1)); s_ch(' '); s_add(items[k - 1].name);
            if (rr) s_ch(rr == R_RARE ? '+' : (char)0xF1);   // rare +, legendary gold star
            s_pad(13); s_ch('x'); s_num(G.seeds[k][rr], 2);
            lbl_set(n);
            rmap[n] = rr;
            map[n++] = k;
        }
    if (!n) { ui_dialog("You have no seeds. Seeds & Needs in town sells them by season."); return 0; }
    {
        uint8_t hv = 0;
        ui_hover = &hv;
        while (1) {
            crop_panel_r(map[hv], rmap[hv]);
            r = ui_menu(0, 0, 20, lp, n, hv);
            if (r != 0xFE) break;
        }
        ui_hover = 0;
    }
    if (r == 0xFF) return 0;
    k = map[r];
    G.seed_sel = k;
    G.seed_rar = rmap[r];
    G.tool = T_SEEDS;
    build_cancel();
    s_clr(); if (G.seed_rar) s_add(G.seed_rar == R_RARE ? "Rare " : "Legendary "); s_add(items[k - 1].name); s_add(" ("); s_add(season_name[crops[k].season]); s_add("): ripe in ");
    s_num(crops[k].days, 0); s_add(" days");
    if (crops[k].regrow) { s_add(", regrows every "); s_num(crops[k].regrow, 0); }
    s_add(". Sells for G"); s_num(items[k - 1].price, 0);
    if (G.seed_rar) s_add(G.seed_rar == R_RARE ? ", x2 a harvest" : crops[k].regrow ? ", x3-4 then x2" : ", x3-4 a harvest");
    s_ch('.');
    ui_dialog(sbuf);
    return 1;
}

// "W 12  S 3  Hay 40": the widest spacing that fits the bag's 17 columns
static void res_line(uint8_t gap) {
    uint8_t k;
    s_clr(); s_add("W "); s_num(G.wood, 0);
    for (k = 0; k < gap; k++) s_ch(' ');
    s_add("S "); s_num(G.stone, 0);
    for (k = 0; k < gap; k++) s_ch(' ');
    s_add("Hay "); s_num(G.hay, 0);
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
            s_pad(13); s_ch('x'); s_num(G.inv[it][q], 3);   // 17 columns: clear of the right border
            ui_text(2, 1 + k, sbuf);
        }
        if (top + 9 < n) ui_char(18, 10, GL_MORE);
        res_line(2);
        if (s_len() > 17) res_line(1);
        if (s_len() > 17) { s_clr(); s_ch('W'); s_num(G.wood, 0); s_add(" S"); s_num(G.stone, 0); s_add(" Hay"); s_num(G.hay, 0); }
        ui_text(2, 10, sbuf);
        if (n) item_panel(mi[sel], mq[sel]);
        else { ui_panel(0, 12, 20, 5); s_clr(); s_add("Fert "); s_num(G.fert, 0); s_add("  Water "); s_num(G.water, 0); ui_text(2, 14, sbuf); }
        while (1) {
            if (n) ui_char(1, 1 + sel - top, (t & 16) ? GL_CURSOR2 : GL_CURSOR);
            read_input();
            t++;
            if (n && (joy_press & J_A) && IS_DISH(mi[sel])) { sfx(SFX_CURSOR); dish_eat(mi[sel]); return; }   // cooking.c
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


static void friends_page(void) {
    uint8_t h, j;
    ui_box(0, 0, 20, 17);
    ui_title(2, 0, "FRIENDS");
    friends_rows();   // town life: hearts, heart events seen and birthdays (townlife.c)
    s_clr(); s_add(G.pet_name); ui_text(2, 14, sbuf);
    h = G.pet_love / 50;
    for (j = 0; j < 5; j++) ui_char(14 + j, 14, j < h ? GL_HEART : GL_HEART_EMPTY);
    if (G.horse) {   // horse.c
        s_clr(); s_add(G.horse_name); ui_text(2, 15, sbuf);
        h = G.horse_love / 50;
        for (j = 0; j < 5; j++) ui_char(14 + j, 15, j < h ? GL_HEART : GL_HEART_EMPTY);
    }
    wait_button();
}

static void farm_page(void) {
    uint8_t k, segs, plots = plots_owned(), blds = 0, anim = 0;
    for (k = 0; k < MAX_BLD; k++) if (G.bld[k].type != 0xFF) blds++;
    for (k = 0; k < MAX_ANIMALS; k++) if (G.anim[k].kind != 0xFF) anim++;
    ui_box(0, 0, 20, 17);
    s_clr(); s_add(G.farm_name); s_add(" Farms");
    ui_title(1, 0, sbuf);
    s_clr(); s_add(season_name[G.season]); s_ch(' '); s_num(G.day, 0); s_add(", Year "); s_num(G.year, 0);
    ui_text(2, 2, sbuf);
    s_clr(); s_add("Gold"); s_pad(8); s_num(G.gold, 8); ui_text(2, 4, sbuf);
    ui_text(2, 5, "Energy");
    segs = (uint8_t)(((uint16_t)G.energy * 9 + G.energy_max - 1) / G.energy_max);   // 9 segments: a gap after the label
    for (k = 0; k < 9; k++) ui_char(9 + k, 5, k < segs ? GL_BAR_FULL : GL_BAR_EMPTY);
    s_clr(); s_add("Land"); s_pad(8); s_num(plots, 2); s_add(" of 64"); ui_text(2, 7, sbuf);
    s_clr(); s_add("Buildings"); s_pad(13); s_num(blds, 2); ui_text(2, 8, sbuf);
    s_clr(); s_add("Animals"); s_pad(13); s_num(anim, 2); ui_text(2, 9, sbuf);
    if (G.horse) {   // horse.c: name + friendship
        s_clr(); s_add("Horse"); s_pad(6); s_add(G.horse_name); ui_text(2, 10, sbuf);
        segs = G.horse_love / 50;
        for (k = 0; k < 5; k++) ui_char(14 + k, 10, k < segs ? GL_HEART : GL_HEART_EMPTY);
    }
    s_clr(); s_add("Wood "); s_num(G.wood, 0); s_add(" Stone "); s_num(G.stone, 0); ui_text(2, 11, sbuf);
    s_clr(); s_add("Harvests"); s_pad(10); s_num(G.stat_harvest, 5); ui_text(2, 13, sbuf);
    s_clr(); s_add("Earned"); s_pad(8); s_num(G.earned, 8); ui_text(2, 15, sbuf);
    wait_button();
}

static const char *const farm_items[8] = {"STATUS", "MAP", "CALENDAR", "FRIENDS", "GOODS", "STAFF", "VALLEY", "BACK"};
static void farm_menu(void) {
    uint8_t r = ui_menu(8, 2, 12, farm_items, 8, 0);
    ui_end(); ui_begin();
    if (r == 0) farm_page();
    else if (r == 1) map_page();
    else if (r == 2) calendar_page();
    else if (r == 3) friends_page();
    else if (r == 4) collection_page();
    else if (r == 5) staff_page();
    else if (r == 6) valley_page();   // valley.c
}

static const char *const sm_items[8] = {"TOOLS", "SEEDS", "BAG", "BUILD", "FARM", "OPTION", "SAVE", "EXIT"};   // 6 cols beside the panel

void start_menu(void) BANKED {
    static uint8_t sel;
    uint8_t r, done = 0;
    if (building && scene == SCENE_FARM && !build_quick()) return;
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
