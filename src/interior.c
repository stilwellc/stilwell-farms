// Stilwell Farms -- house interiors: going in and out, furniture you buy and arrange,
// and the things in your house you can use (bed, TV, diary desk) (banked)
//
// Interior maps (art/maps/house1.map, house2.map) are static; furniture lives in G.furn as
// {kind, x, y} and is drawn over the map by interior_mt() (view.c cell_mt) and blocks walking
// through interior_walk() (main.c walkable).  Both houses share the same floor origin, so the
// small house's furniture stays put when the house is upgraded.
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "game.h"

BANKREF(interior_act)

enum { FK_NONE, FK_BED, FK_TV, FK_DESK, FK_FRIDGE, FK_RUG, FK_PLANT, FK_SHELF, FK_CHAIR, FK_TABLE, FK_LAMP, FK_FIRE,
       FK_PAINT, NUM_FK };
#define FF_WALK 1   // you can walk over it (rugs)
#define FF_WALL 2   // hangs on / stands in the back wall
typedef struct { const char *name; mt_t mt; uint8_t w, h, flags; uint16_t price; const char *blurb; } FurnDef;
static const FurnDef fdef[NUM_FK] = {
    {"", 0, 1, 1, 0, 0, ""},
    {"Bed", ST_BED, 1, 2, 0, 0, "Sleep: new day"},
    {"TV", MT_TV, 1, 1, 0, 800, "Weather, tips"},
    {"Diary Desk", MT_DESK, 1, 1, 0, 400, "Diary: saves"},
    {"Fridge", MT_FRIDGE, 1, 1, 0, 1000, "Kitchen: cook"},
    {"Rug", ST_RUG, 2, 2, FF_WALK, 300, "Soft and warm"},
    {"House Plant", MT_PLANT, 1, 1, 0, 150, "Fresh & green"},
    {"Bookshelf", MT_BOOKSHELF, 1, 1, 0, 500, "Old almanacs"},
    {"Armchair", MT_ARMCHAIR, 1, 1, 0, 450, "Feet up!"},
    {"Table", ST_TABLE, 2, 1, 0, 350, "Supper spot"},
    {"Floor Lamp", MT_LAMP_FLOOR, 1, 1, 0, 250, "Warm glow"},
    {"Fireplace", ST_FIREPLACE, 2, 1, FF_WALL, 2000, "Crackling!"},
    {"Painting", MT_PAINTING, 1, 1, FF_WALL, 600, "Farm sunset"},
};
#define NCAT 11
static const uint8_t cat[NCAT] = {FK_TV, FK_DESK, FK_FRIDGE, FK_RUG, FK_PLANT, FK_SHELF, FK_CHAIR, FK_TABLE, FK_LAMP,
                                  FK_FIRE, FK_PAINT};

static uint8_t f_mode, f_sel, carry = 0xFF;   // build mode: 0 buy, 1 move, 2 sell back

// ---------------------------------------------------------------- the furniture list
static uint8_t furn_at(uint8_t x, uint8_t y) {
    uint8_t k, kind;
    for (k = 0; k < MAX_FURN; k++) {
        kind = G.furn[k][0];
        if (!kind || kind >= NUM_FK) continue;
        if ((uint8_t)(x - G.furn[k][1]) < fdef[kind].w && (uint8_t)(y - G.furn[k][2]) < fdef[kind].h) return k;
    }
    return 0xFF;
}

static uint8_t furn_add(uint8_t kind, uint8_t x, uint8_t y) {
    uint8_t k;
    for (k = 0; k < MAX_FURN; k++)
        if (!G.furn[k][0]) { G.furn[k][0] = kind; G.furn[k][1] = x; G.furn[k][2] = y; return k; }
    return 0xFF;
}

static void furn_defaults(void) {   // Grandpa left a bed, an old TV and his diary desk
    if (G.furn_init & 1) return;
    G.furn_init |= 1;
    memset(G.furn, 0, sizeof(G.furn));
    furn_add(FK_BED, 1, 2);
    furn_add(FK_TV, 4, 2);
    furn_add(FK_DESK, 8, 2);
}

mt_t interior_mt(uint8_t x, uint8_t y) BANKED {
    uint8_t k, kind;
    if (IS_BARNIN(scene)) return barn_mt(x, y);   // barn.c
    k = furn_at(x, y);
    if (k == 0xFF) return map_mt(MIDX(x, y));
    kind = G.furn[k][0];
    return fdef[kind].mt + (uint8_t)(y - G.furn[k][2]) * fdef[kind].w + (uint8_t)(x - G.furn[k][1]);
}

uint8_t interior_walk(uint8_t x, uint8_t y) BANKED {
    uint8_t k;
    if (!(map_kind(MIDX(x, y)) & K_WALK)) return 0;
    if (IS_BARNIN(scene)) return 1;   // no furniture in the barn
    k = furn_at(x, y);
    return k == 0xFF || (fdef[G.furn[k][0]].flags & FF_WALK);
}

// ---------------------------------------------------------------- in and out
static uint8_t house_map(void) {
    uint8_t h = bld_find(B_HOUSE);
    return (h != 0xFF && ((G.bld[h].flags & BF_LVL) >> 1) >= 2) ? MAP_HOUSE2 : MAP_HOUSE1;
}

void house_enter(void) BANKED {
    uint8_t m = house_map();
    furn_defaults();
    map_select(m);   // read the doorway position (the first warp is the mat)
    sfx(SFX_STEP);
    enter_scene(m, sc_warps[0].x, sc_warps[0].y - 1, D_UP);
    if (!(G.furn_init & 2)) {   // first visit (G.tips is full: bit 0x80 is the 1000G tip)
        G.furn_init |= 2;
        say("Home sweet home! Open START, then BUILD, to buy and arrange furniture.");
        say("Sleep in the bed to end the day. The TV has tomorrow's weather.");
    }
}

// leaving: always come out one step below the house door, wherever the house was moved to
void interior_leave(Warp *w) BANKED {
    uint8_t h = bld_find(B_HOUSE);
    if (IS_BARNIN(scene)) { barn_leave(w); return; }
    if (h != 0xFF) { w->dx = G.bld[h].x + 1; w->dy = G.bld[h].y + 3; }
    else { w->dx = FARM_OX + 10; w->dy = FARM_OX + 12; }
    if (w->dy >= WH) w->dy = WH - 1;
    w->dir = D_DOWN;
    building = 0;
    carry = 0xFF;
}

// ---------------------------------------------------------------- using things
static const char *const tips[10] = {
    "Farm tip: crops only grow on nights they're watered. Rainy days water everything for you!",
    "Farm tip: fertilized soil grows silver and gold quality crops that sell for more.",
    "Farm tip: a scarecrow keeps crows off every crop within 4 steps.",
    "Farm tip: feed and pet your animals every day. Happy animals give better milk and eggs.",
    "Farm tip: once it's repaired, the greenhouse grows any crop in any season.",
    "Farm tip: kegs turn fruit into juice and cider worth far more than the fruit.",
    "Farm tip: walk up to a For Sale sign at the edge of your land to buy more of it.",
    "Farm tip: say hello to the townsfolk every day, and bring gifts, to make friends.",
    "Farm tip: sprinklers water the soil around them every morning. No more watering cans!",
    "Farm tip: upgraded tools from the Grumpy Anvil work on more ground with each swing."};

static void go_to_bed(uint8_t k) {
    uint8_t bx = G.furn[k][1], by = G.furn[k][2], sc = scene, i;
    static const int8_t ox[6] = {1, -1, 0, 1, -1, 0}, oy[6] = {1, 1, 2, 0, 0, -1};
    music_play(MUS_JINGLE_SLEEP);
    fade_out();
    world_overnight(0);
    // wake up beside the bed (or at the doorway if the bed is boxed in)
    map_select(sc);
    G.px = sc_warps[0].x; G.py = sc_warps[0].y - 1;
    for (i = 0; i < 6; i++) {
        int8_t x = (int8_t)bx + ox[i], y = (int8_t)by + oy[i];
        if (x >= 0 && y >= 0 && walkable(x, y)) { G.px = x; G.py = y; break; }
    }
    enter_scene(sc, G.px, G.py, D_DOWN);
    save_write();
    morning_report();
    hud_draw();
}

void interior_act(void) BANKED {
    uint8_t x, y, k;
    if (IS_BARNIN(scene)) { barn_act(); return; }
    if (!front_cell(&x, &y)) return;
    k = furn_at(x, y);
    if (k == 0xFF) {
        if (map_mt(MIDX(x, y)) == ST_WALL_WINDOW + 1) {
            if (G.time >= 1200) say("It's dark out. The farm is sleeping under the stars.");
            else if (G.weather == 1) say("Rain is pattering on the window. The crops are getting a drink.");
            else if (G.weather == 2) say("Snow is drifting past the window.");
            else if (G.weather == 3) say("A storm is rattling the window!");
            else say("Sunshine is pouring in. A fine day for farming!");
        }
        return;
    }
    switch (G.furn[k][0]) {
    case FK_BED:
        if (ui_yesno("Go to bed and end the day?")) go_to_bed(k);
        break;
    case FK_TV: {
        static const char *const fc[4] = {"sunny skies", "rain showers", "snow", "thunderstorms"};
        set_speaker("TV");
        s_clr(); s_add("And now the weather. Tomorrow: "); s_add(fc[G.tomorrow & 3]); s_ch('!');
        say(sbuf);
        ui_speaker = 0;
        daily_tv();
        if (!cook_tv()) { set_speaker("TV"); say(tips[(uint8_t)(G.day + G.season * 7 + G.year) % 10]); ui_speaker = 0; }
        break;
    }
    case FK_DESK:
        if (ui_yesno("Write about today in your diary? (This saves the game.)")) {
            save_write();
            sfx(SFX_CONFIRM);
            say("Dear diary... What a day on the farm. (Game saved.)");
        }
        break;
    case FK_FRIDGE: kitchen_open(x, y); break;   // cooking.c
    case FK_SHELF: say("Grandpa's old farming almanacs. Every page is full of his notes."); break;
    case FK_CHAIR: say("You sink into the armchair. Ahh... just for a minute."); break;
    case FK_FIRE: say("The fire crackles and pops. Toasty!"); break;
    case FK_PAINT: say("A painting of the sun setting over a farm. Maybe this one, someday."); break;
    case FK_PLANT: say("A happy little house plant."); break;
    case FK_LAMP: say("The lamp fills the room with a warm glow."); break;
    case FK_TABLE: say("A sturdy table. Supper for one... for now."); break;
    default: say("A cozy rug. Soft under your boots."); break;
    }
}

// ---------------------------------------------------------------- furniture build mode
// placement rules: floor pieces on free floor (never the doorway or the step inside it);
// wall pieces only on plain back wall
static uint8_t near_door(uint8_t x, uint8_t y) {
    uint8_t k;
    for (k = 0; k < sc_nwarps; k++)
        if (sc_warps[k].x == x && (sc_warps[k].y == y || sc_warps[k].y == y + 1)) return 1;
    return 0;
}

static uint8_t cell_ok(uint8_t x, uint8_t y, uint8_t kind) {
    uint16_t i;
    uint8_t k;
    if (x >= sc_w || y >= sc_h) return 0;
    k = furn_at(x, y);
    if (k != 0xFF && k != carry) return 0;
    i = MIDX(x, y);
    if (fdef[kind].flags & FF_WALL) return map_mt(i) == MT_WALL;
    if (!(map_kind(i) & K_WALK) || near_door(x, y)) return 0;
    return !(x == G.px && y == G.py);
}

uint8_t furn_cursor(uint8_t *x0, uint8_t *y0, uint8_t *w, uint8_t *h) BANKED {
    uint8_t fx, fy, kind, bw, bh, i, j, k;
    int8_t ax, ay;
    if (!front_cell(&fx, &fy)) { *w = 0; return 0; }
    kind = f_mode == 0 ? cat[f_sel] : (carry != 0xFF ? G.furn[carry][0] : 0);
    if (!kind) {   // picking up / selling: frame whatever is in front
        k = furn_at(fx, fy);
        if (k == 0xFF) { *x0 = fx; *y0 = fy; *w = *h = 1; return 0; }
        *x0 = G.furn[k][1]; *y0 = G.furn[k][2];
        *w = fdef[G.furn[k][0]].w; *h = fdef[G.furn[k][0]].h;
        return 1;
    }
    bw = fdef[kind].w; bh = fdef[kind].h;
    ax = fx; ay = fy;
    if (G.facing == D_UP) { ax -= bw >> 1; ay -= bh - 1; }
    else if (G.facing == D_DOWN) ax -= bw >> 1;
    else if (G.facing == D_LEFT) { ax -= bw - 1; ay -= bh >> 1; }
    else ay -= bh >> 1;
    if (ax < 0 || ay < 0) { *w = 0; return 0; }
    *x0 = ax; *y0 = ay; *w = bw; *h = bh;
    for (j = 0; j < bh; j++)
        for (i = 0; i < bw; i++)
            if (!cell_ok(ax + i, ay + j, kind)) return 0;
    return 1;
}

static void dust(uint8_t x, uint8_t y) {
    fx_show(SPR_DUST, SPR_DUST_PAL, x, y);
    sfx(SFX_HIT);
}

void furn_place(void) BANKED {
    uint8_t x0, y0, w, h, ok, k, kind;
    ok = furn_cursor(&x0, &y0, &w, &h);
    if (!w) return;
    if (f_mode == 2) {   // sell back
        uint16_t back;
        if (!ok) { hud_flash("Nothing to sell."); return; }
        k = furn_at(x0, y0);
        kind = G.furn[k][0];
        if (kind == FK_BED) { hud_flash("Keep your bed!"); return; }
        back = fdef[kind].price >> 1;
        s_clr(); s_add("Sell the "); s_add(fdef[kind].name); s_add(" back for G"); s_num(back, 0); s_ch('?');
        if (!ui_yesno(sbuf)) return;
        G.furn[k][0] = 0;
        G.gold += back;
        redraw_rect(x0, y0, w, h);
        dust(x0, y0);
        hud_draw();
        return;
    }
    if (f_mode == 1) {   // move: pick up, then set down
        if (carry == 0xFF) {
            if (!ok) { hud_flash("Nothing to move."); return; }
            carry = furn_at(x0, y0);
            sfx(SFX_CONFIRM);
            hud_flash("Picked up! A=place");
            return;
        }
        if (!ok) { sfx(SFX_BUMP); hud_flash("Can't put it here."); return; }
        {
            uint8_t ox = G.furn[carry][1], oy = G.furn[carry][2];
            G.furn[carry][1] = x0; G.furn[carry][2] = y0;
            redraw_rect(ox, oy, w, h);
        }
        redraw_rect(x0, y0, w, h);
        carry = 0xFF;
        dust(x0, y0);
        return;
    }
    kind = cat[f_sel];
    if (!ok) {
        sfx(SFX_BUMP);
        hud_flash((fdef[kind].flags & FF_WALL) ? "Face a bare wall." : "Can't put it here.");
        return;
    }
    if (G.gold < fdef[kind].price) { sfx(SFX_BUMP); hud_flash("Not enough gold."); return; }
    if (furn_add(kind, x0, y0) == 0xFF) { sfx(SFX_BUMP); hud_flash("House is full!"); return; }
    G.gold -= fdef[kind].price;
    redraw_rect(x0, y0, w, h);
    dust(x0, y0);
    hud_draw();
}

static void furn_panel(uint8_t kind) {
    const FurnDef *f = &fdef[kind];
    ui_art_release();
    ui_panel(0, 13, 20, 4);
    ui_put_mt(1, 14, f->mt);
    if (f->w > 1) ui_put_mt(3, 14, f->mt + 1);
    ui_text(6, 14, f->blurb);
    ui_text(6, 15, (f->flags & FF_WALL) ? "Wall piece" : (f->flags & FF_WALK) ? "Walk on it" : "Floor piece");
}

static const char *const fm_names[3] = {"FURNITURE", "MOVE", "SELL BACK"};

uint8_t furn_menu(void) BANKED {
    uint8_t c, r, k, len;
    if (IS_BARNIN(scene)) { ui_dialog("Furniture is for the house. Your animals like it just the way it is!"); return 0; }
    c = ui_menu(0, 0, 12, fm_names, 3, f_mode);
    if (c == 0xFF) return 0;
    if (c == 0) {
        uint8_t hv = f_sel;
        ui_end(); ui_begin();
        for (k = 0; k < NCAT; k++) {
            s_clr(); s_ch('G'); s_num(fdef[cat[k]].price, 0);
            len = s_len();
            strcpy(lbl[13], sbuf);
            s_clr(); s_add(fdef[cat[k]].name); s_pad(17 - len); s_add(lbl[13]);
            lbl_set(k);
        }
        ui_hover = &hv;
        while (1) {
            furn_panel(cat[hv]);
            r = ui_menu(0, 0, 20, lp, NCAT, hv);
            if (r != 0xFE) break;
        }
        ui_hover = 0;
        if (r == 0xFF) return 0;
        f_sel = r;
    }
    f_mode = c;
    carry = 0xFF;
    building = 1;
    ui_dialog(c == 0 ? "A places it in front of you. B or SELECT when you're done."
            : c == 1 ? "Face a piece of furniture and press A to pick it up, then A again to set it down."
                     : "Face a piece of furniture and press A to sell it back for half price.");
    return 1;
}
