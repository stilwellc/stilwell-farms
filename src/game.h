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
// 64x64 cells in cartridge SRAM (save.c has the bank map). cell[] and a1[] are plain arrays in the live
// bank, which stays mapped during play (RAM enabled, bank WB_LIVE): W_CELL[i] / W_A1[i] read and write
// like any array. a2[] sits in the next bank, reached through W_A2(i) / W_A2_SET(i, v) (bank 0 helpers
// that switch to it and back). Code that switches SRAM banks (save.c) must restore WB_LIVE.
#define WW 64
#define WH 64
#define WCELLS 4096
#define WIDX(x, y) ((uint16_t)(((uint16_t)(y) << 6) | (x)))
#define MIDX(x, y) ((uint16_t)(((uint16_t)(y) << 5) | (x)))   // static maps (town, house interiors): 32 wide
#define WIDX_X(i) ((uint8_t)(i) & 63)
#define WIDX_Y(i) ((uint8_t)((i) >> 6))
#define WB_LIVE 1                   // SRAM bank: live cell[] at 0xA000, a1[] at 0xB000
#define WB_A2 2                     // SRAM bank: live a2[] at 0xA000
#define W_CELL ((uint8_t *)0xA000)
#define W_A1 ((uint8_t *)0xB000)
uint8_t w_a2(uint16_t i);           // main.c (bank 0)
void w_a2_set(uint16_t i, uint8_t v);
#define W_A2(i) w_a2(i)
#define W_A2_SET(i, v) w_a2_set((i), (v))
// land plots: 8x8 grid of 8x8 cells; G.plots[plot row] bit (plot column) = owned
#define PLOT_OF(x, y) ((uint8_t)((((y) >> 3) << 3) | ((x) >> 3)))
#define NUM_PLOTS 64
#define PLOT_OWNED(p) owned(((p) & 7) << 3, ((p) >> 3) << 3)
#define START_ROW 0x18       // plots 27,28,35,36 (cells 24..39): bits 3-4 of plot rows 3 and 4
#define ROAD_X 31            // public road x = 31..32 from the farm's south edge to the world edge
#define TOWN_ROAD_X 15       // ... arriving in town at x = 15..16
#define FARM_OX 16           // saves before v5 had a 32x32 world: it sits at +16,+16 (the home valley)

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
#define SOIL_GROWN(i) (W_A2(i) & SOIL_DAYS)
#define SOIL_RARITY(i) (W_A2(i) >> 6)
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
    // mine + beach
    I_COAL, I_TOPAZ, I_EMERALD, I_RUBY, I_DIAMOND,                              // 40..44 mine (I_GEM is the amethyst)
    I_SARDINE, I_FLOUNDER, I_SNAPPER, I_TUNA, I_SQUID,                          // 45..49 ocean fish
    I_SHELL, I_CORAL, I_SEAGLASS, I_DRIFTWOOD,                                  // 50..53 beach finds
    // cooking (cooking.c): dishes, cooked from the fridge in the house; dish d is item I_DISH0 + d
    I_SALAD, I_OMELET, I_FISHFRY, I_FISHSTEW, I_CHOWDER, I_PUMPSOUP, I_BERRYPIE, I_APPLEPIE,
    I_CASSEROLE, I_RISOTTO, I_VEGSTEW, I_SORBET, I_MUFFIN, I_COBBLER, I_STIRFRY, I_FEAST,      // 54..69
    I_GOATMILK, I_DUCKEGG, I_TRUFFLE,                                           // 70..72 goats, ducks, pigs (save v7)
    NUM_ITEMS
};
#define I_DISH0 I_SALAD
#define NUM_DISHES 16
#define IS_DISH(it) ((uint8_t)((it) - I_DISH0) < NUM_DISHES)
// food buffs (G.buff bits): one dish's buff lasts the rest of the day
#define BUFF_SPEED 1        // brisk: walk at running pace
#define BUFF_HEARTY 2       // work costs half the energy
#define BUFF_LUCKY 4        // rarer fish bite, and sooner
#define BUFF_GREEN 8        // harvests roll quality as if fertilized
#define BUFF_ALL 15
// valley restoration (valley.c)
#define NUM_PROJ 6
#define VP_SLOTS 5          // item slots per project (G.valley bits 0-4); bit 5 = gold paid
#define VP_GOLD 0x20
#define VD_GARDEN 1         // G.valley_done bits: completed projects
#define VD_ORCHARD 2
#define VD_FISH 4
#define VD_LAMPS 8
#define VD_PANTRY 16
#define VD_MUSEUM 32
#define VD_ENDING 0x80      // the restoration celebration has played
#define MAIL_VALLEY 21      // G.mail bits 21..26: Grandpa's letter for each finished project
#define NUM_CROPS 14
#define I_AMETHYST I_GEM
#define IS_MINERAL(it) (((it) >= I_COPPER && (it) <= I_GEM) || ((it) >= I_COAL && (it) <= I_DIAMOND))
#define IS_FISH(it) (((it) >= I_MINNOW && (it) <= I_KOI) || ((it) >= I_SARDINE && (it) <= I_SQUID))
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
enum { A_COW, A_HEN, A_SHEEP, A_GOAT, A_DUCK, A_PIG, NUM_KINDS };   // kind % 3: 0 milker (barn), 1 layer (coop), 2 slow (barn): animals.c
#define MAX_ANIMALS 36      // six full barns / coops (was 12: a third barn or coop could never be filled)
#define V5_ANIMALS 12       // save v5 and older kept this many, earlier in GameState
typedef struct { uint8_t kind, home, happy, age, flags; char name[6]; uint8_t trait, gen; } Animal;  // kind 0xFF empty
#define OLD_ANIMAL 11        // an Animal up to save v6 (no trait, no gen)
#define AF_PAIR 0x20         // paired for breeding (the panel's menu): only pairs have babies
#define TR_SIZE 0x03         // trait: 0 normal, SZ_BIG, SZ_RUNT
#define SZ_BIG 1
#define SZ_RUNT 2
#define TR_TEMPER 0x0C       // 0 calm, 1 shy, 2 playful, 3 proud (<< 2)
#define TR_CARE 0x70         // a baby's days of care so far
#define TR_SPARKLE 0x80      // the rare one
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
#define NUM_BIZ 4               // town storefront businesses (biz_* fields)
#define NUM_BIZ_ALL 8           // ... plus the four built on Market Street lots (bz_* fields): biz.c
#define NUM_LOTS 4              // Market Street lots
#define BIZ_BREWERY 4
#define BIZ_WINERY 5
#define BIZ_RESTAURANT 6
#define BIZ_INN 7
#define BL_STYLE 0x03           // biz_look bits
#define BL_TIER 0x0C
#define BL_TIER1 0x04
#define BL_BUILDING 0x80
#define MK_OPEN 0x80            // mk_flags: Market Street bought
#define MK_GATE_Y 21            // town rows from here down are Market Street (closed until MK_OPEN)
#define NUM_TPROJ 6             // community projects (invest.c)
#define TP_PAVING 1
#define TP_PLAYGROUND 2
#define TP_BOARDWALK 4
#define TP_BANDSTAND 8
#define TP_GARDEN 16
#define TP_LIBRARY 32
#define PANTRY 8                // ingredient slots per business
#define SAVE_MAGIC 0x5F17
#define SAVE_VERSION 8
#define MAX_FURN 24             // house furniture pieces (interior.c)
#define NUM_ACCENT 3            // design tools: recolourable accent palette entries
#define BP_SLOTS 2              // design tools: blueprint slots, each up to BP_MAX x BP_MAX cells
#define BP_MAX 8
#define BP_PAL 15               // distinct pieces per blueprint
typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t plots[8];               // owned land plots: bit x of byte y = plot (x, y) of the 8x8 grid
    Building bld[MAX_BLD];
    // (these 132 bytes held the 12 animals of saves up to v5; zeroed and reused since v8)
    uint8_t region_taken[6];        // areas.c: like area_taken, for the six stagecoach regions
    uint8_t post_stock[11];         // regions.c: bought today at each trading post (2 bits a good)
    uint8_t rep[11];                // regions2.c: your standing at each place (gold sold there / 256)
    uint8_t glut[61];               // regions.c: how flooded each market is (a nibble per place x kind of good)
    uint8_t errand[3];              // regions2.c: 2 bits a stagecoach place (3 = mended); bit 22 the almanac prize
    uint8_t office[2], seen[2];     // ... a bit a place: your office there; visited
    uint8_t carry_week, carry_done; // ... this week's carrier job
    uint8_t more8[V5_ANIMALS * OLD_ANIMAL - 17 - 81];   // zero: room for what comes next
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
    uint8_t shipped[(NUM_ITEMS + 7) / 8];   // collection log: bit per item ever sold/shipped (is_shipped)
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
    // v5 world in SRAM: checksum of this slot's saved world (save.c)
    uint16_t world_sum;
    // design tools (designer.c): farm accent colours + blueprints
    uint16_t accent[NUM_ACCENT];    // RGB555 for the roof / wood / stone accent palette entries, 0 = the art's own colour
    uint8_t bp_wh[BP_SLOTS];        // blueprint size: w << 4 | h (1..BP_MAX each), 0 = empty slot
    uint8_t bp_pal[BP_SLOTS][BP_PAL];   // piece codes (see designer.h) used by the blueprint
    uint8_t bp_cell[BP_SLOTS][BP_MAX * BP_MAX / 2];   // one nibble per cell: 0 empty, else bp_pal index + 1
    // animals & stable (barn.c, horse.c): barn/coop interiors and the horse
    uint8_t bld_in;                 // building slot whose barn/coop interior you're in
    uint8_t barn_tips;              // one-time tips: 1 barn, 2 coop, 4 horse
    uint8_t horse;                  // 0 no horse, else coat + 1 (1 White, 2 Chestnut, 3 Palomino)
    uint8_t horse_love;             // friendship 0..250 (hearts = love / 50)
    uint8_t horse_day;              // today: 1 brushed, 2 ridden (cleared overnight)
    char horse_name[6];
    // ---- town life (townlife.c, festival.c): heart events, festivals (14 bytes, all valid at zero)
    uint8_t heart_seen[NUM_NPC];    // bit n: the townsperson's heart event at n+1 hearts has played
    uint8_t heart_day;              // date stamp (day | season << 5) of the last heart event: one a day
    uint8_t ribbons;                // festival blue ribbons ever won, bit per season's festival
    uint8_t fest_done;              // bit per season: this year's festival game won
    uint8_t fest_year;              // the year fest_done belongs to
    uint8_t fest_best[4];           // best festival score per season: eggs, koi cm, judges' points, snowball hits
    // mine + beach (areas.c)
    uint8_t mine_floor;             // mine floor you are on (0 = the entrance), so a save made down there resumes on it
    uint8_t mine_lift;              // deepest lift stop unlocked: 0, 5, 10, 15 or 20
    uint8_t mine_deep;              // deepest floor reached
    uint8_t beach_taken;            // bit per beach forage spot picked today
    uint8_t beach_stamp;            // the day beach_taken belongs to (0 = never visited)
    // cooking + valley restoration (cooking.c, valley.c); all valid when zero
    uint16_t recipes;               // bit per dish known
    uint8_t buff;                   // today's food buff (BUFF_* bits), cleared overnight
    uint8_t cook_flags;             // 1 = found Grandpa's recipe cards, 2 = heard of the restoration
    uint16_t stat_cooked;           // dishes cooked, lifetime
    uint8_t valley[NUM_PROJ];       // per project: bit per donated item slot, VP_GOLD = gold paid
    uint8_t valley_done;            // VD_* bits: finished projects; VD_ENDING = celebration seen
    // ---- town investment (biz.c, invest.c): storefront styles + facades, Market Street, community projects
    // (83 bytes, all valid at zero). Businesses 4..7 (Market Street) use biz_owned / biz_staffed bits 4..7.
    uint8_t biz_look[NUM_BIZ_ALL];  // per business: style 0..3 (BL_STYLE), facade tier 0..3 (BL_TIER), BL_BUILDING = opens tomorrow
    uint8_t mk_flags;               // MK_OPEN: Market Street bought; bit n (0..3): lot n bought
    uint8_t biz_lot[NUM_LOTS];      // business 4 + k: the lot it stands on + 1 (0 = not built)
    uint8_t bz_level[NUM_LOTS];     // business 4 + k: kitchen level 0..2 (like biz_level)
    uint8_t bz_pantry[NUM_LOTS][PANTRY];
    uint16_t bz_today[NUM_LOTS];
    uint16_t bz_made[NUM_LOTS];
    uint8_t bz_sold[NUM_LOTS];
    uint8_t rest_item[PANTRY];      // the restaurant's shelves: item + 1 on each (0 = free shelf)
    uint8_t town_proj;              // TP bit per community project built (invest.c)
    uint8_t inv_tips;               // one-time pitches: 1 Joe on investing, 2 the Land Office on Market Street
    uint32_t invested;              // gold put into the town, lifetime
    // ---- balance (cooking.c, town.c cafe): 1 byte, valid at zero
    uint8_t ate;                    // energy restored by food today (dishes + Raza's), at most energy_max; cleared overnight
    // ---- v6: room for more animals. Kept last so every older save's layout is a prefix of this one
    Animal anim[MAX_ANIMALS];
    // ---- v6: the years after the valley is restored (lategame.c, goals.c): all valid at zero
    uint8_t review_year;            // the year Grandpa last sent his spring review for
    uint16_t review_score;          // ... and what the farm scored
    uint8_t fund_lvl;               // Town Fund levels bought
    uint32_t fund_paid;             // gold given toward the next one
    uint8_t con_id;                 // contracts on the board are for year * 4 + season, + 1 (0 = none posted yet)
    uint8_t con_item[3], con_need[3], con_have[3];
    uint8_t con_stars;              // seasons with all three contracts filled
    uint8_t donated[(NUM_ITEMS + 7) / 8];   // museum.c: a bit per item given to the Library's collection
    uint16_t museum_sets;           // ... a bit per group already thanked for; bit 15 = the whole collection
    uint8_t daily, daily_arg, daily_flags;   // daily.c: today's surprise (D_*), its item / seed, what's been taken
    uint8_t npc_mail;               // friends2.c: a letter waiting from NPC n (bit n)
    uint8_t wish_year, wish_done;   // ... this year's wishes granted (bit per NPC)
    uint8_t area_stamp, area_taken[5];  // areas.c: what's been picked today in each place to visit (bit 7 = its daily find)
    uint8_t tame;                   // ... days of hay left in the High Pasture's trough (3 = one follows you home)
    uint8_t herd[4];                // animals.c, the Herd Book: bit kind * 4 + breed ever owned; bits 24-29 sparkles
    uint8_t herd_done;              // ... rows already rewarded (bit per kind), bit 7 the whole book
    uint8_t spare6[3];             // zero: room for what comes next without another save version
    uint16_t checksum;
} GameState;
extern GameState G;

#define SCENE_FARM 0xFE
#define IS_BARNIN(s) ((uint8_t)((s) - MAP_BARN) < 2)   // inside a barn or coop (barn.c; MAP_COOP = MAP_BARN + 1)
#define IS_INTERIOR(s) ((uint8_t)((s) - MAP_HOUSE1) < 2 || IS_BARNIN(s))   // inside a barn or coop (barn.c)
#define IS_MINE(s) ((s) >= MAP_MINE && (s) <= MAP_MINE_C)     // the entrance cave and the three floor layouts
#define IS_NEW(s) ((uint8_t)((s) - MAP_ISLAND) < 11)       // the eleven places to visit, Shell Island .. Stonegate (areas.c, regions.c)
#define IS_SHORE(s) ((s) == MAP_BEACH || (s) == MAP_ISLAND || (s) == MAP_HARBOUR || (s) == MAP_MESA)   // sand-coloured places
#define IS_OPEN(s) ((s) == MAP_BEACH || IS_NEW(s))         // open-air places to visit (areas.c)
#define IS_AREA(s) (IS_OPEN(s) || IS_MINE(s))
#define MINE_FLOORS 20

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
extern uint8_t music_cur;           // music.c: last song started

// view.c (banked: tile cache + streaming)
mt_t cell_mt(uint8_t x, uint8_t y) BANKED;
void view_reset(void) BANKED;
void view_update(void) BANKED;
void redraw_cell(uint8_t x, uint8_t y) BANKED;
void redraw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) BANKED;
void view_tick(void) BANKED;
uint8_t owned(uint8_t x, uint8_t y);  // main.c (bank 0)
uint16_t owned_run(uint16_t from) BANKED;   // world.c: first span of owned land at/after cell `from` (a plot edge), 0xFFFF at the end
extern uint8_t owned_len;             // ... and its length: whole plots side by side in one row (8..64 cells)
void scan_types(uint8_t t1, uint8_t t2) BANKED;   // world.c: the cell types scan_cells looks for (t2 may repeat t1)
void scan_add(uint8_t t) BANKED;           // ... one more
uint8_t *scan_cells(uint8_t *p, uint8_t n);   // main.c (bank 0): first wanted cell in p[0..n), else p + n (fast: ~15 cycles a cell)
// every span of owned land: r = its first cell, n = its length. Safe if the body calls owned_run itself.
#define FOR_OWNED(r, n) for (r = owned_run(0); r != 0xFFFF && ((n = owned_len), 1); r = owned_run(r + n))
// every cell of a wanted type (scan_types) on owned land: pc points at it (uint8_t *pc, *e; n uint8_t)
#define FOR_OWNED_CELLS(r, n, pc, e) \
    FOR_OWNED(r, n) for (pc = &W_CELL[r], e = pc + n; (pc = scan_cells(pc, (uint8_t)(e - pc))) != e; pc++)
#define CELL_I(pc) ((uint16_t)(pc) - 0xA000)   // a W_CELL pointer's cell index
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
uint8_t is_shipped(uint8_t it) BANKED;          // in the collection log
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
uint8_t plot_region(uint8_t p) BANKED;  // the named region a plot belongs to (several plots can share one)
uint16_t plot_price(uint8_t p) BANKED;
uint8_t plot_kind(uint8_t p) BANKED;    // RK_* terrain of a plot
uint8_t plots_owned(void) BANKED;       // how many plots you own
enum { RK_HOME, RK_PINE, RK_BIRCH, RK_MEADOW, RK_FLOWER, RK_ORCHARD, RK_QUARRY, RK_ROCKY, RK_LAKE, RK_RIVER, RK_MARSH };
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
// barn.c (banked): barn and coop interiors, animal care inside
extern uint8_t in_stall[6];             // animal per stall / nest box of the building you're in (0xFF empty)
uint8_t animals_out(void) BANKED;       // daytime, sunny, not winter: animals graze outside
void barn_scan(void) BANKED;            // refresh in_stall + fed/ready bits for G.bld_in
void barn_enter(uint8_t slot) BANKED;
uint8_t door_bump(uint8_t x, uint8_t y) BANKED;   // walked into a door on the farm: 1 = went inside
mt_t barn_mt(uint8_t x, uint8_t y) BANKED;
void barn_act(void) BANKED;
void barn_leave(Warp *w) BANKED;
void animal_panel(uint8_t an, uint8_t got) BANKED;
void portrait(uint8_t tile, uint8_t pal) BANKED;   // a sprite on a stall tile in a UI panel (OAM 2)
// horse.c (banked): the horse from the stable
extern uint8_t riding;                  // in the saddle (farm only; any scene change dismounts)
extern uint16_t ppx, ppy;               // main.c: player pixel position
void horse_shop(void) BANKED;           // Alyssa's store: pick a coat, name it
void horse_talk(uint8_t a) BANKED;      // A on the horse: ride / brush / info
void horse_dismount(void) BANKED;
void stable_act(void) BANKED;
void horse_panel(void) BANKED;
// town.c (banked)
void town_act(uint8_t act) BANKED;
void npc_talk(uint8_t npc) BANKED;
// actors.c (banked)
void actors_spawn(void) BANKED;
void actors_tick(void) BANKED;
void actors_draw(void) BANKED;
uint8_t actor_at(uint8_t x, uint8_t y) BANKED;
uint8_t actor_blocks(uint8_t x, uint8_t y) BANKED;   // an actor in the way of building there (the pet never is)
uint8_t actor_npc(uint8_t a) BANKED;
uint8_t actor_animal(uint8_t a) BANKED;
uint8_t actor_is_pet(uint8_t a) BANKED;
uint8_t actor_staff(uint8_t a) BANKED;   // farmhand role or 0xFF
uint8_t actor_is_horse(uint8_t a) BANKED;
uint8_t horse_actor(void) BANKED;        // the horse's actor index, or 0xFF
void horse_actor_move(uint8_t x, uint8_t y) BANKED;   // set it down at a cell (dismounting)
void ride_draw(int16_t sx, int16_t sy) BANKED;        // main.c draw_player while riding
// goals.c (banked)
void goals_check(void) BANKED;
void read_mail(void) BANKED;
void board_act(void) BANKED;
void hall_act(void) BANKED;
void collection_page(void) BANKED;
// staff.c (banked): farmhands
void staff_overnight(void) BANKED;      // payroll, then the night's work (called from world_overnight)
void staff_report(void) BANKED;         // morning report lines
void staff_hall(void) BANKED;           // Joe's menu: festival, talk, valley board, hire, staff
void staff_page(void) BANKED;           // STAFF page: list, wages, fire
void staff_talk(uint8_t role) BANKED;   // A on a farmhand
uint16_t staff_spot(uint8_t role) BANKED;   // where they work today: x | y << 8, 0xFFFF = off the farm
uint8_t staff_pal(uint8_t role) BANKED;
uint8_t staff_hired(uint8_t role) BANKED;   // e.g. staff_hired(ROLE_SHOP) for town businesses
// fishing.c (banked)
void fish_start(void) BANKED;
// areas.c (banked): the mine and the beach
void area_enter(void) BANKED;                   // enter_scene hook: lay out the mine floor / today's beach finds
mt_t area_mt(uint8_t x, uint8_t y) BANKED;      // rocks, ladders, forage drawn over the static map
uint8_t area_walk(uint8_t x, uint8_t y) BANKED;
void area_act(void) BANKED;                     // A in the mine or on the beach
void area_step(void) BANKED;                    // after each step: footsteps, the ladder down
void area_map(void) BANKED;                     // START > map while in the mine or on the beach
uint8_t beach_finds(void) BANKED;               // finds still lying on the beach today
// save.c (banked)
uint8_t save_valid(uint8_t slot) BANKED;
void save_write(void) BANKED;
uint8_t save_load(uint8_t slot) BANKED;
extern uint8_t save_slot;            // always 0: a 32KB cartridge holds one save (see save.c)
extern uint8_t save_info[3];        // season, day, year of the slot save_valid() checked last
// save_valid: 0 empty, 2 a first-release farm, 3/4 a v3/v4 save (carried into the 64x64 valley on load), 5 current
// pub.c (banked)
void pub_act(void) BANKED;               // the Pub's bar: Corey, from 4pm
// title.c (banked)
uint8_t title_screen(void) BANKED;
// biz.c (banked): town businesses
mt_t biz_mt(uint8_t x, uint8_t y) BANKED;     // opened-shop art over an owned storefront, else 0xFFFF
void biz_act(uint8_t b) BANKED;
void biz_overnight(void) BANKED;
void biz_report(void) BANKED;
uint8_t biz_kind_at(uint8_t lot) BANKED;     // the business on a Market Street lot, 0xFF if none
void biz_build(uint8_t lot) BANKED;          // a bought, empty lot: pick a business + style, pay, it goes up tonight
uint16_t biz_income(uint8_t b) BANKED;       // last night's takings of business b (0 if not owned)
uint8_t biz_count(void) BANKED;              // businesses owned
uint8_t biz_tiers(void) BANKED;              // facade tiers summed over owned businesses
// invest.c (banked): Market Street, community projects, prosperity, the Investments view
#define MAIL_FRIEND 27                       // G.mail: a townsperson's letter waits (friends2.c)
void friends_overnight(void) BANKED;         // friends2.c: a letter for every new heart
void friends_letter(void) BANKED;            // ... read the next one
uint8_t wish_talk(uint8_t npc, uint8_t first_today) BANKED;   // ... this year's big wish; 1 = it was granted just now
uint8_t item_group(uint8_t it) BANKED;       // museum.c: 0 crops 1 fruit 2 farm goods 3 wild 4 river fish 5 ore 6 gems 7 preserves 8 sea fish 9 beach 10 dishes
void post_act(void) BANKED;                  // regions.c: this place's trading post
uint8_t region_rate(uint8_t place, uint8_t it) BANKED;
uint8_t region_stock(uint8_t place, uint8_t k) BANKED;
uint8_t office_rate(uint8_t it) BANKED;      // tenths your offices get for a good shipped from home (>= 10)
// regions2.c: standing, news, the carrier job, errands, offices, the almanac
uint8_t region_stars(uint8_t a) BANKED;
void region_sold(uint8_t a, uint32_t gold) BANKED;
void region_seen(uint8_t a) BANKED;
void regions_night(void) BANKED;
uint8_t news_place(void) BANKED;
uint8_t news_group(void) BANKED;
uint8_t region_fair(uint8_t a) BANKED;
void regions_tv(void) BANKED;
void region_news_line(void) BANKED;
uint8_t carry_here(uint8_t a) BANKED;
void carry_do(uint8_t a) BANKED;
void carry_tv(void) BANKED;
uint8_t region_errand(uint8_t a) BANKED;
uint8_t region_errand_open(uint8_t a) BANKED;
void region_errand_do(uint8_t a) BANKED;
void region_office(uint8_t a) BANKED;
void almanac_page(void) BANKED;
void coach_act(void) BANKED;                 // regions.c: the stagecoach at the Crossroads Market
// animals.c: kinds, breeds, traits, the Herd Book
uint8_t anim_item(uint8_t an) BANKED;        // what it gives
uint8_t anim_yield(uint8_t an) BANKED;       // (quality << 4) | count right now
uint8_t anim_collect(uint8_t an) BANKED;     // into the bag; sbuf = "+2 Milk"; -> count
uint16_t anim_worth(uint8_t an) BANKED;      // what a buyer pays
void anim_born(uint8_t c, uint8_t mom, uint8_t dad) BANKED;   // a newborn's traits
void anim_night(uint8_t an) BANKED;          // temperament and baby care, each night
void s_kind(uint8_t kind) BANKED;            // sbuf += "cow"
void s_produce(uint8_t kind) BANKED;         // sbuf += "Milk"
void s_breed(uint8_t an, uint8_t baby) BANKED;   // sbuf += "Jersey cow" / "Jersey calf"
void s_traits(uint8_t an) BANKED;            // sbuf += "Big playful G2"
void herd_mark(uint8_t an) BANKED;
void herd_all(void) BANKED;
void herd_page(void) BANKED;
enum { D_NONE, D_TRADER, D_BOTTLE, D_HOT, D_FISH };
#define BOTTLE_X 10                          // where the beach bottle lies
#define BOTTLE_Y 6
#define BOTTLE_HERE (G.daily == D_BOTTLE && !(G.daily_flags & 1))
void daily_roll(void) BANKED;                // daily.c: tomorrow's surprise (overnight)
void daily_report(void) BANKED;              // ... its line in the morning report
void daily_tv(void) BANKED;                  // ... and on the TV
uint8_t daily_fish(void) BANKED;             // the fish that is running today, once: 0xFF none
void daily_bottle(void) BANKED;              // open the bottle on the beach
void daily_trader(void) BANKED;              // the west market tent in the square
uint16_t sell_value(uint8_t it, uint8_t q) BANKED;   // item_value, doubled for today's hot item
uint8_t museum_act(void) BANKED;             // museum.c: the Library's collection; 1 = read a book
void fund_page(void) BANKED;                 // lategame.c: the Town Fund (Joe), past prosperity 5
uint8_t fund_pct(void) BANKED;               // ... % it adds to business takings and contract pay
void contracts_board(void) BANKED;           // lategame.c: the season's three bulk orders (year 2 on)
uint8_t prosperity(void) BANKED;             // Town Prosperity level 0..5
uint8_t prosperity_pts(void) BANKED;
void invest_menu(void) BANKED;               // Joe's INVEST: overview, community projects, Market Street
void invest_act(uint8_t act) BANKED;         // A on a Market Street act (gate, lots, project sites)
uint8_t invest_realtor(void) BANKED;         // Land Office: 1 if the town lots were handled (don't list farm land)
uint8_t invest_line(uint8_t npc) BANKED;     // a townsperson remarks on the town's prosperity: 1 if said
void invest_store(void) BANKED;              // Alyssa's traders' goods (prosperity 2+)
mt_t invest_mt(uint8_t x, uint8_t y) BANKED; // town upgrades drawn over the map (paving, lights, boardwalk, Market St)
uint8_t invest_spot(uint8_t npc, uint8_t s) BANKED;   // townlife: a stop moved to Market Street
// townlife.c (banked): townsfolk routines, greetings, heart events, birthdays
uint8_t tl_spot(uint8_t npc) BANKED;        // where the schedule puts them now: SP_* (routes.h), | 0x80 = home
uint8_t tl_shop_open(uint8_t act) BANKED;   // shop counters are staffed only while the owner is scheduled there
void tl_closed_sign(uint8_t act) BANKED;    // the posted hours
void tl_greet(uint8_t npc) BANKED;          // HUD greeting as you pass
uint8_t heart5_try(uint8_t npc, uint8_t a) BANKED;               // hearts5.c: the scene at five full hearts
uint8_t tl_heart_try(uint8_t npc, uint8_t walk_in) BANKED;   // play a due heart event; 1 if one played
void tl_arrive(void) BANKED;                // first frame in town: a due heart event walks up to you
uint8_t tl_birthday(uint8_t npc) BANKED;    // 1 on their birthday
uint8_t milo_beach(void) BANKED;            // Marcy is at the beach (weekend days), not in town
void tl_house(void) BANKED;                 // A on a townhouse door
void friends_rows(void) BANKED;             // FRIENDS page body
#define FESTIVAL_DAY() (G.season == SEASON_WINTER ? G.day == 25 : G.day == 14)
// actors.c town helpers (scripted scenes)
uint8_t actor_slot_npc(uint8_t npc) BANKED; // actor slot of a townsperson, 0xFF if not out
uint8_t actor_bring(uint8_t npc, uint8_t x, uint8_t y) BANKED;   // place (spawning if needed); slot
void actor_walk(uint8_t slot, uint8_t x, uint8_t y) BANKED;      // scripted walk (blocks, animates)
void actor_face(uint8_t slot, uint8_t dir) BANKED;
void emote(uint8_t slot, uint8_t kind, uint8_t frames) BANKED;   // bubble over an actor (frames 0 = until the next)
enum { EMO_NONE, EMO_EXCLAIM, EMO_HEART, EMO_NOTE, EMO_DOTS };
void actors_resched(void) BANKED;           // send everyone toward their scheduled spot now
// festival.c (banked)
void festival_hall(void) BANKED;            // Joe on a festival day: the season's game
void festival_tick(void) BANKED;            // every field frame in town while a festival game runs
uint8_t festival_poke(uint8_t x, uint8_t y) BANKED;   // A on a town cell with no act: 1 if used
extern uint8_t fest_on;                     // a walk-around festival game (egg hunt) is running
// cooking.c (banked): recipes, the kitchen, eating, food buffs
void kitchen_open(uint8_t fx, uint8_t fy) BANKED;   // A on the fridge at (fx, fy)
uint8_t dish_eat(uint8_t it) BANKED;     // bag: eat a dish; 1 if eaten
void dish_line(uint8_t it) BANKED;       // sbuf = "+60 energy Brisk"
uint8_t cook_tv(void) BANKED;            // the TV's Sunday cooking show; 1 if it aired
void cook_friend(uint8_t npc) BANKED;    // a good friend hands over their recipe
void cook_learn(uint8_t d) BANKED;       // learn dish d (with a toast)
// valley.c (banked): the Valley Restoration projects
void valley_board(void) BANKED;          // town hall: Joe's restoration board
void valley_page(void) BANKED;           // FARM > VALLEY progress page
mt_t valley_mt(uint8_t x, uint8_t y) BANKED;   // restored town art, else 0xFFFF
void valley_letter(uint8_t p) BANKED;    // Grandpa's letter for project p
// options.c (banked)
void options_menu(void) BANKED;
#endif
