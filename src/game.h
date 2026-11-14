#ifndef GAME_H
#define GAME_H
#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include "art.h"
#include "fallback.h"
#include "maps.h"
#include "music.h"
#include "sfx.h"

// ---- the farm world --------------------------------------------------------
#define WW 32
#define WH 32
#define WIDX(x, y) ((uint16_t)(((uint16_t)(y) << 5) | (x)))
// land plots: 4x4 grid of 8x8 cells
#define PLOT_OF(x, y) ((uint8_t)((((y) >> 3) << 2) | ((x) >> 3)))
#define START_PLOTS 0x0660   // plots 5,6,9,10 (cells 8..23)
#define ROAD_X 15            // public road x = 15..16 from the farm's south edge to the world edge

enum {  // cell types
    C_GRASS, C_GRASS2, C_WILDFLOWER, C_WEEDS, C_ROCK, C_BOULDER, C_STUMP, C_LOG,
    C_FOREST, C_DIRT, C_SOIL, C_DECOR, C_TREE, C_STRUCT, C_WATER, C_ROAD,
    C_PINE, C_BIRCH, C_BUSH, C_FORAGE, C_ORE, C_SAND, C_CLIFF, C_MACHINE, C_GIANT, C_SAPLING_WILD
};
// C_SOIL: a1 = crop | WET | FERT, a2 = days grown
#define SOIL_WET 0x80
#define SOIL_FERT 0x40
#define SOIL_CROP 0x1F
#define CROP_WITHERED 31
#define SOIL_RAR 0xC0        // a2 top 2 bits: seed rarity (0 common, 1 rare, 2 legendary)
#define SOIL_DAYS 0x3F       // a2 low 6 bits: days grown
#define SOIL_GROWN(i) (G.a2[i] & SOIL_DAYS)
#define SOIL_RARITY(i) (G.a2[i] >> 6)
enum { R_COMMON, R_RARE, R_LEGEND, NUM_RARITY };
// C_DECOR: a1 = decor kind, a2 = autotile variant
// C_TREE: a1 = fruit kind << 5 | fruit-days (0..31), a2 = age
// C_STRUCT: a1 = building slot, a2 = part index
// C_BUSH: a1 = days since picked.  C_FORAGE: a1 = forage item.  C_ORE: a1 = ore kind 0..3
// C_MACHINE: a1 = machine kind, a2 = product item + 1 (0 empty); G.mdays[] not needed: days in high bits of a1
// C_GIANT: a1 = part (0..3)

// ---- buildings ---------------------------------------------------------------
enum { B_HOUSE, B_BARN, B_COOP, B_GREENHOUSE, B_SILO, B_SHED, B_STABLE, B_WINDMILL, B_STAND, B_GAZEBO, NUM_BTYPES };
#define BF_RUIN 0x01
#define BF_LVL 0x06         // house level in bits 1-2
#define MAX_BLD 20
typedef struct { uint8_t type, x, y, flags; } Building;   // type 0xFF = empty slot

// ---- items ---------------------------------------------------------------------
enum {
    I_RADISH, I_CARROT, I_STRAWB, I_CAULI, I_TOMATO, I_CORN, I_MELON, I_BLUEBERRY, I_SUNFLOWER,
    I_PUMPKIN, I_BEET, I_EGGPLANT, I_CRANBERRY, I_SNOWPEA,                      // 0..13 crops
    I_APPLE, I_PEACH, I_ORANGE, I_CHERRY,                                       // 14..17 fruit
    I_MILK, I_EGG, I_HONEY,                                                     // 18..20 animal
    I_BERRIES, I_MUSHROOM, I_HERB,                                              // 21..23 forage
    I_MINNOW, I_BASS, I_TROUT, I_CATFISH, I_CARP, I_KOI,                        // 24..29 fish
    I_COPPER, I_IRON, I_GOLD, I_GEM,                                            // 30..33 ore
    I_JUICE, I_CIDER, I_CHEESE, I_JAM, I_PICKLES,                               // 34..38 goods
    I_WOOL,                                                                     // 39 sheep (rarity)
    NUM_ITEMS
};
#define NUM_CROPS 14
typedef struct { const char *name; uint16_t price; } ItemDef;
extern const ItemDef items[NUM_ITEMS];

typedef struct {
    uint8_t season;     // 0..3
    uint16_t seed_cost;
    uint8_t days, regrow;
    mt_t ripe_mt;
} CropDef;
extern const CropDef crops[NUM_CROPS + 1];   // index 1..14; crop c harvests item c-1

enum { SEASON_SPRING, SEASON_SUMMER, SEASON_FALL, SEASON_WINTER };

// ---- tools ---------------------------------------------------------------------
enum { T_HAND, T_HOE, T_CAN, T_SEEDS, T_SICKLE, T_AXE, T_HAMMER, T_ROD, T_FERT, NUM_TOOLS };
enum { TT_HOE, TT_CAN, TT_AXE, TT_HAMMER, TT_ROD, NUM_TT };

enum { D_DOWN, D_UP, D_LEFT, D_RIGHT };

// ---- people & animals ------------------------------------------------------------
enum { NPC_ROSA, NPC_BRICK, NPC_JUNE, NPC_ADA, NPC_HOLLIS, NPC_MILO, NUM_NPC };
enum { A_COW, A_HEN, A_SHEEP };   // sheep share the barn with cows
#define MAX_ANIMALS 12
typedef struct { uint8_t kind, home, happy, age, flags; char name[6]; } Animal;  // kind 0xFF empty
#define AF_FED 0x01
#define AF_PET 0x02
#define AF_READY 0x04
#define AF_BREED 0xC0       // breed rarity in the top 2 bits: 0 common, 1 rare (Jersey/brown), 2 legendary (golden)
#define BREED(a) ((a)->flags >> 6)
#define AF_WOOL 0x18        // sheep: fed days of fleece growth (bits 3-4); wool is ready every WOOL_DAYS
#define WOOL_DAYS 3
#define ADULT_AGE 7         // babies give no produce and can't breed until this many days old
#define BARN_CAP 6          // cows + sheep per barn

// ---- farmhands (staff.c): one hire per role; G.staff[role] = candidate name + 1, 0 = vacant
enum { ROLE_WATER, ROLE_HARVEST, ROLE_RANCH, ROLE_GROUNDS, ROLE_SHOP, NUM_ROLES };

// ---- persistent state (one save slot) ----------------------------------------------
#define NUM_BIZ 4               // town businesses
#define PANTRY 8                // ingredient slots per business
#define SAVE_MAGIC 0x5F17
#define SAVE_VERSION 4
#define MAX_FURN 24             // house furniture pieces (interior.c)
typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t cell[1024], a1[1024], a2[1024];
    uint16_t plots;                 // owned land plots bitmask
    Building bld[MAX_BLD];
    Animal anim[MAX_ANIMALS];
    uint32_t gold, earned, ship_value;
    uint16_t time;                  // minutes since midnight (360..1560)
    uint8_t day, season, year;
    uint8_t energy, energy_max;
    uint8_t px, py, facing, scene;
    uint8_t tool, seed_sel, build_sel, build_cat;
    uint8_t tier[NUM_TT];
    uint8_t style;
    uint8_t water;
    uint8_t weather, tomorrow;      // 0 sun 1 rain 2 snow 3 storm
    uint8_t inv[NUM_ITEMS][3];      // count per quality (normal, silver, gold)
    uint8_t seeds[NUM_CROPS + 1][NUM_RARITY];   // count per seed rarity
    uint8_t saplings[4];            // apple, peach, orange, cherry
    uint8_t fert;
    uint16_t wood, stone, hay;
    uint8_t friend[NUM_NPC];
    uint8_t talked, gifted;         // bit per NPC, reset daily
    uint8_t shipped[6];             // collection log: bit per item ever sold/shipped
    uint32_t goals;                 // milestone bits reached
    uint32_t mail;                  // letters waiting
    uint8_t req_item, req_qty, req_npc, req_day;   // town board request (req_qty 0 = none)
    uint8_t pet_love, pet_flags;
    char pet_name[6], farm_name[9];
    uint8_t opt_text, opt_music, opt_sfx, opt_clock, opt_run;
    uint8_t tips;                   // one-time tips: 1 hoe 2 town 4 weeds 8 harvest 16 water 32 morning 64 land 128 gold (all used)
    uint16_t stat_weeds, stat_fish, stat_harvest;
    uint8_t festival_won;
    uint8_t rng_lo, rng_hi;
    // v4: house interior furniture {kind (0 empty), x, y}; furn_init 0 = Grandpa's furniture not placed yet
    uint8_t furn_init;
    uint8_t furn[MAX_FURN][3];
    // staff
    uint8_t staff[NUM_ROLES];       // hired farmhand per role (name index + 1), 0 = vacant
    uint8_t staff_unpaid;           // bit per role: worked unpaid last night (quits if unpaid again)
    uint16_t staff_wage[NUM_ROLES]; // daily wage, recalculated nightly from the farm's size
    // businesses (town storefronts, biz.c): index 0 Bakery, 1 Juice Bar, 2 Creamery, 3 Florist
    uint8_t biz_owned;              // bit per business
    uint8_t biz_staffed;            // bit per business: employees hook (+2 a night; see BIZ_NEEDS_STAFF)
    uint8_t biz_level[NUM_BIZ];     // upgrades 0..2
    uint8_t biz_pantry[NUM_BIZ][PANTRY];   // ingredient counts; slot -> item in biz.c pan_item
    uint16_t biz_today[NUM_BIZ];    // last night's earnings
    uint16_t biz_made[NUM_BIZ];     // products sold, lifetime
    uint8_t biz_sold[NUM_BIZ];      // products sold last night
    // rarity
    uint8_t seed_rar;               // rarity of the selected seed packet
    uint16_t checksum;
} GameState;
extern GameState G;

#define SCENE_FARM 0xFE
#define IS_INTERIOR(s) ((s) == MAP_HOUSE1 || (s) == MAP_HOUSE2)

// ---- engine globals ----------------------------------------------------------------
extern uint8_t joy, joy_prev, joy_press;
extern uint16_t scx, scy;
extern uint8_t building;            // build/placement mode active
extern uint8_t scene;               // current scene (SCENE_FARM or MAP_*)
extern uint8_t view_w, view_h;      // current map size in cells

// main.c
void read_input(void);
void wait_frames(uint8_t n);
uint8_t rnd(void);
void hud_draw(void) BANKED;
void tool_toast(void) BANKED;       // hud.c: SELECT flashes the new tool's info
void after_step(void) BANKED;       // hud.c: footstep sound + first-day tips after each step
void hud_flash(const char *s);
void hud_draw_row(uint8_t row);
void apply_palettes(void);
void fade_out(void);
void fade_in(void);
void fx_show(uint8_t tile, uint8_t pal, uint8_t x, uint8_t y);
void new_morning(void);
void hide_sprites(void);
void update_sprites(void);
void enter_scene(uint8_t sc, uint8_t x, uint8_t y, uint8_t facing);
uint8_t front_cell(uint8_t *ox, uint8_t *oy);
uint8_t walkable(uint8_t x, uint8_t y);
void player_pose(uint8_t pose, uint8_t frames);
void music_for_scene(void);

// view.c (banked: tile cache + streaming)
mt_t cell_mt(uint8_t x, uint8_t y) BANKED;
void view_reset(void) BANKED;
void view_update(void) BANKED;
void redraw_cell(uint8_t x, uint8_t y) BANKED;
void redraw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) BANKED;
void view_tick(void) BANKED;
uint8_t owned(uint8_t x, uint8_t y);  // main.c (bank 0)
void bg_to_win(void) BANKED;          // ui_begin: copy the visible BG into the window map
void win_row(uint8_t row, const uint8_t *tiles) BANKED;   // 20 tiles (UI attribute) into a window row
void ui_put_mt(uint8_t x, uint8_t y, mt_t m) BANKED;
void ui_put_quad(uint8_t x, uint8_t y, mt_t m, uint8_t k) BANKED;
void ui_put_struct(uint8_t x, uint8_t y, mt_t first, uint8_t w, uint8_t h) BANKED;
void ui_art_release(void) BANKED;

// ui.c
void ui_begin(void);
void ui_end(void);
void ui_box(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
void ui_text(uint8_t x, uint8_t y, const char *s);
void ui_char(uint8_t x, uint8_t y, uint8_t tile);
void ui_clear(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
void ui_dialog(const char *s);
void say(const char *s);
uint8_t ui_menu(uint8_t x, uint8_t y, uint8_t w, const char *const *items, uint8_t n, uint8_t start);
uint8_t ui_yesno(const char *q);
uint8_t ui_quantity(uint8_t max, uint32_t unit);
uint8_t ui_name(const char *prompt, char *buf, uint8_t maxlen) BANKED;   // prompt must be in RAM (e.g. sbuf)
uint8_t ui_pick_item(uint8_t filter) BANKED;   // PICK_*; returns item or 0xFF
enum { PICK_ANY, PICK_PRODUCE, PICK_MILK };
void ui_title(uint8_t x, uint8_t y, const char *s);   // bank 0: may be given literals from any bank
void ui_panel(uint8_t x, uint8_t y, uint8_t w, uint8_t h) BANKED;
void ui_money(void) BANKED;
void item_panel(uint8_t it, uint8_t q) BANKED;
void crop_panel(uint8_t crop) BANKED;
void crop_panel_r(uint8_t crop, uint8_t rar) BANKED;
void status_panel(void) BANKED;
void calendar_page(void) BANKED;
void map_page(void) BANKED;
extern uint8_t *ui_hover;        // when set, ui_menu returns 0xFE after the cursor moves (sel stored here)
extern const char *ui_speaker;
void set_speaker(const char *s);   // bank 0: copies the name to RAM (use from banked code)
extern char sbuf[96];
extern char lbl[14][19];          // shared menu label buffers
extern const char *lp[14];
void lbl_set(uint8_t k);
void s_clr(void);
void s_add(const char *s);
void s_num(uint32_t v, uint8_t width);
void s_ch(char c);
uint8_t s_len(void);
void s_pad(uint8_t col);

// items.c (bank 0)
uint16_t inv_count(uint8_t it) BANKED;
void inv_add(uint8_t it, uint8_t q, uint8_t n) BANKED;
uint8_t inv_take(uint8_t it, uint8_t n) BANKED;   // lowest quality first; returns how many taken
uint16_t item_value(uint8_t it, uint8_t q) BANKED;
void mark_shipped(uint8_t it) BANKED;
extern const uint8_t item_icon_tile[NUM_ITEMS];
uint8_t spend_energy(uint8_t e) BANKED;
uint8_t pay(uint32_t g, uint16_t w, uint16_t s) BANKED;
void flash_gain(const char *what, uint8_t n);   // bank 0
void cost_str(uint32_t g, uint16_t w, uint16_t s) BANKED;
extern const char *const season_name[4];

// world.c (banked)
void world_new(void) BANKED;
void world_overnight(uint8_t passed_out) BANKED;
void morning_report(void) BANKED;
uint8_t bld_new(uint8_t type, uint8_t x, uint8_t y, uint8_t flags) BANKED;
void bld_remove(uint8_t slot) BANKED;
uint8_t bld_w(uint8_t type) BANKED;
uint8_t bld_h(uint8_t type) BANKED;
uint8_t bld_door(uint8_t slot) BANKED;
uint8_t bld_find(uint8_t type) BANKED;
mt_t bld_mt(uint8_t slot, uint8_t part) BANKED;
mt_t world_cell_mt(uint8_t x, uint8_t y) BANKED;
#define LAMP_TIME 1140              // 7pm: lamps and string lights switch on
void lamps_refresh(void) BANKED;    // farmview.c
void fix_autotile(uint8_t x, uint8_t y) BANKED;
void plot_name_add(uint8_t p) BANKED;   // appends the plot's name to sbuf
uint16_t plot_price(uint8_t p) BANKED;
void buy_plot_prompt(uint8_t p) BANKED;
uint8_t clear_ground(uint8_t c);

// farm.c (banked)
void farm_act(void) BANKED;
// menus.c (banked)
void start_menu(void) BANKED;
void build_place(void) BANKED;
void build_cancel(void) BANKED;
uint8_t build_cursor(uint8_t *x0, uint8_t *y0, uint8_t *w, uint8_t *h) BANKED;
// interior.c (banked): house interiors + furniture
void house_enter(void) BANKED;
void interior_leave(Warp *w) BANKED;
void interior_act(void) BANKED;
mt_t interior_mt(uint8_t x, uint8_t y) BANKED;
uint8_t interior_walk(uint8_t x, uint8_t y) BANKED;
uint8_t furn_menu(void) BANKED;
uint8_t furn_cursor(uint8_t *x0, uint8_t *y0, uint8_t *w, uint8_t *h) BANKED;
void furn_place(void) BANKED;
// town.c (banked)
void town_act(uint8_t act) BANKED;
void npc_talk(uint8_t npc) BANKED;
// actors.c (banked)
void actors_spawn(void) BANKED;
void actors_tick(void) BANKED;
void actors_draw(void) BANKED;
uint8_t actor_at(uint8_t x, uint8_t y) BANKED;
uint8_t actor_npc(uint8_t a) BANKED;
uint8_t actor_animal(uint8_t a) BANKED;
uint8_t actor_is_pet(uint8_t a) BANKED;
uint8_t actor_staff(uint8_t a) BANKED;   // farmhand role or 0xFF
// goals.c (banked)
void goals_check(void) BANKED;
void read_mail(void) BANKED;
void board_act(void) BANKED;
void hall_act(void) BANKED;
void collection_page(void) BANKED;
// staff.c (banked): farmhands
void staff_overnight(void) BANKED;      // payroll, then the night's work (called from world_overnight)
void staff_report(void) BANKED;         // morning report lines
uint8_t staff_hall(void) BANKED;        // town hall menu; 1 = talk to Hollis as usual
void staff_page(void) BANKED;           // STAFF page: list, wages, fire
void staff_talk(uint8_t role) BANKED;   // A on a farmhand
uint16_t staff_spot(uint8_t role) BANKED;   // where they work today: x | y << 8, 0xFFFF = off the farm
uint8_t staff_pal(uint8_t role) BANKED;
uint8_t staff_hired(uint8_t role) BANKED;   // e.g. staff_hired(ROLE_SHOP) for town businesses
// fishing.c (banked)
void fish_start(void) BANKED;
// save.c (banked)
uint8_t save_valid(uint8_t slot) BANKED;
void save_write(void) BANKED;
uint8_t save_load(uint8_t slot) BANKED;
extern uint8_t save_slot;
// title.c (banked)
uint8_t title_screen(void) BANKED;
// biz.c (banked): town businesses
mt_t biz_mt(uint8_t x, uint8_t y) BANKED;     // opened-shop art over an owned storefront, else 0xFFFF
void biz_act(uint8_t b) BANKED;
void biz_overnight(void) BANKED;
void biz_report(void) BANKED;
// options.c (banked)
void options_menu(void) BANKED;
#endif
