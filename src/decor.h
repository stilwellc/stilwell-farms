// Stilwell Farms -- placeable things catalogue
#ifndef DECOR_H
#define DECOR_H
#include "game.h"

enum {
    DK_NONE, DK_BRIDGE, DK_FENCE, DK_PICKET, DK_STONEWALL, DK_MODERN, DK_PATH, DK_BRICK, DK_GRAVEL, DK_PLANK,
    DK_FLOWERBED, DK_FLOWERS_RED, DK_FLOWERS_BLUE, DK_FLOWERS_YELLOW, DK_HEDGE, DK_STATUE, DK_FOUNTAIN, DK_ARCH,
    DK_PICNIC, DK_STRING, DK_LAMP, DK_BENCH, DK_SCARECROW, DK_SPRINKLER, DK_QSPRINKLER, DK_BEEHIVE, DK_ROD,
    DK_HAYBALE, DK_TROUGH, DK_MAILBOX, DK_BIN, DK_WELL, DK_SIGN, NUM_DECOR
};
typedef struct {
    const char *name;
    mt_t mt;
    uint8_t walk, fence;      // fence: 0 none, else fence family 1..4 (autotiles)
    uint16_t gold;
    uint8_t wood, stone;
} DecorDef;
extern const DecorDef decor[NUM_DECOR];

// fruit tree kinds: 0 apple (fall), 1 peach (summer), 2 orange (summer), 3 cherry (spring)
#define season_ok_tree(k) ((k) == 0 ? G.season == SEASON_FALL : (k) == 3 ? G.season == SEASON_SPRING : G.season == SEASON_SUMMER)

enum { MK_KEG, MK_PRESS, MK_JAR };
#endif
