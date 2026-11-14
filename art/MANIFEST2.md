# Stilwell Farms — world-building expansion art contract

The game is becoming a designable world-building farm: a 32x32-cell scrolling farm made of 16 land
plots you buy, buildings you place anywhere, ponds you dig, and a town down the road.

Read first: `art/MANIFEST.md` (original contract and palette notes), `art/00_palettes.art`, and the
docstring of `tools/gen_art.py` (format, `set=`, `style=`, `bank=`, map syntax). Study existing art for
the quality bar: `art/10_terrain.art`, `art/20_structures.art`, `art/30_sprites.art`.
Preview your file alone: `.venv/bin/python tools/gen_art.py art/<yourfile>.art` → `build/preview_<file>.png`.
ONLY edit your own file(s). Original art only, no copying of any existing game.
Objects on grass: grass quadrants must be exact copies of GRASS/GRASS2 quadrants (GRASS palette, colour 1 base).

## Tile budgets (unique 8x8 tiles, as reported by the preview)
VRAM is shared: glyphs (~150) + COMMON terrain/objects + one scene set. Stay inside your budget.

## A) `art/11_water_wild.art` — COMMON (no set=), budget 110
- WATER_0 … WATER_15: water autotiles. The number is a 4-bit mask of which neighbours are ALSO water:
  bit0 = north, bit1 = east, bit2 = south, bit3 = west. WATER_15 = open water (all neighbours water);
  WATER_0 = a 1-cell pond. Where a neighbour is NOT water, draw a grassy shore/bank on that side
  (grass in GRASS palette, a sandy/earth lip, then water). Water: STONE palette (white sparkle, light
  blue-grey, slate) reads well as water; or pick what looks best. All 16 must tile together seamlessly.
  Corners: when two adjacent sides are shore, round the corner.
- WATER_ANIM: an alternate version of WATER_15 (shifted ripple highlights) — the engine flips between
  WATER_15's tiles and this every ~half second for shimmer. Keep the same palette layout as WATER_15.
- LILYPAD (water with a lily pad + flower), REEDS (water edge reeds, sits in open water).
- BRIDGE_H (wooden plank bridge over water, walkable, running left-right), BRIDGE_V (up-down).
- ROPE_H, ROPE_V, ROPE_POST: survey-rope boundary marking unowned land (thin posts with a sagging rope,
  on grass). ROPE_H runs left-right edge to edge, ROPE_V top-bottom.
- FOR_SALE: a small wooden sign reading "SALE" (or a coin symbol) on a post, on grass.
- PINE (tall pine tree filling the cell, blocks; gives wood), BIRCH (white-bark birch), OAK_SAPLING
  (tiny wild sapling that regrows into a tree).
- BERRY_BUSH (bush with berries, forage), BUSH (same bush without berries), MUSHROOM (forage on grass),
  WILD_FLOWERS (forage herb/flower clump).
- ORE_COPPER, ORE_IRON, ORE_GOLD: rocks with coloured mineral veins (orange-ish, grey-blue, gold).
  GEM_ROCK: rock with a sparkling gem.
- CAVE: a cave mouth in a rocky hillside (fills the cell; walkable entrance at the bottom-middle).
- SAND (beach sand, tiles seamlessly), CLIFF (rock face, tiles horizontally).

## B) `art/12_decor.art` — COMMON, budget 100
Paths (fill cell, tile seamlessly): BRICK, GRAVEL, PLANK.
Fences on grass, same conventions as the existing FENCE_* (H rails meet edges; V posts column; POST
lone): PICKET_H, PICKET_V, PICKET_POST (white picket), STONEWALL_H, STONEWALL_V, STONEWALL_POST,
MODERNFENCE_H, MODERNFENCE_V, MODERNFENCE_POST (black horizontal slats).
Decor (on grass): HEDGE (trimmed square hedge), STATUE (stone statue on plinth), FOUNTAIN (small stone
fountain with water), ARCH (rose garden arch, walkable look), PICNIC (picnic table), STRINGLIGHTS
(post with festoon bulbs), FLOWERS_RED, FLOWERS_BLUE, FLOWERS_YELLOW (flower beds), WINDCHIME? no.
Farm equipment: MAILBOX, LIGHTNING_ROD, QSPRINKLER (bigger copper sprinkler), KEG (wooden barrel),
CHEESE_PRESS, PRESERVE_JAR (big jar on a stand), HAY_BALE, TROUGH (feed trough), BEEHIVE2? no.
Machines also need a "ready" look: KEG_READY, PRESS_READY, JAR_READY (same object + a small sparkle
or full-lid indicator).

## C) `art/13_crops2.art` — COMMON, budget 80
New ripe crops (same conventions as the existing RIPE_*: RED or ORANGE or CROP palette, colour 0 = dry
soil background): RIPE_CAULI (cauliflower, spring), RIPE_MELON (summer), RIPE_BLUEBERRY (summer),
RIPE_EGGPLANT (fall), RIPE_CRANBERRY (fall), RIPE_SNOWPEA (greenhouse/winter), RIPE_SUNFLOWER (summer,
tall flower). Fruit trees (fill cell, like FRUIT_TREE/_RIPE): PEACH_TREE_RIPE, ORANGE_TREE_RIPE,
CHERRY_TREE_RIPE, CHERRY_TREE (blossoming, not ripe). The existing FRUIT_TREE is used as the generic
unripe crown for apple/peach/orange.
GIANT_PUMPKIN w=2 h=2 (a huge pumpkin spanning 2x2 cells of soil).

## D) `art/21_buildings2.art` — FARM set (put `set=FARM` on each meta), budget 90
New placeable farm buildings, 3/4 view, door/opening at the bottom edge:
SILO w=1 h=2 (tall grain silo with a domed cap), SHED w=2 h=2 (tool shed), STABLE w=3 h=2 (horse stable),
WINDMILL w=2 h=3 (decorative windmill with sails), FARM_STAND w=2 h=2 (small roadside produce stand you
can place), GAZEBO w=2 h=2 (white garden gazebo).
Also coop variants for the styles — style variants go in the style files later; skip them here.

## E) `art/50_town.art` + `art/maps/town.map` — TOWN set (`set=TOWN` on every meta), budget 220
A charming small-town tileset and the town map (32 wide x 24 tall, `map TOWN set=TOWN w=32 h=24
music=TOWN`). Town ground: COBBLE (plaza), TOWN_GRASS can reuse COMMON GRASS, DIRT road (COMMON DIRT),
TOWN_TREE (round shade tree), FLOWERPOT, TOWN_LAMP, TOWN_BENCH, NOTICE_BOARD (requests board, 1x1),
TOWN_FOUNTAIN w=2 h=2 (plaza centrepiece), BUNTING (festival flags strung, walkable overlay look — put
it on cobble), MARKET_TENT w=2 h=2 (festival tent).
Buildings (fill footprint, door bottom-middle touching the bottom edge, sign above the door):
GENERAL_STORE w=3 h=3 (seeds & goods), BLACKSMITH w=3 h=3 (anvil, chimney smoke stack), CARPENTER w=3
h=3 (lumber, saw sign), CAFE w=3 h=3 (awning, tables), TOWN_HALL w=4 h=3 (clock tower, mayor),
REALTOR w=2 h=2 (land office, "LAND" sign), TOWN_HOUSE_A w=2 h=2, TOWN_HOUSE_B w=2 h=2.
Map file: the farm road enters from the TOP edge at x=15..16 (`warp 15 0 FARM 15 30 up` and `warp 16 0
FARM 16 30 up`). Lay out a cozy town: plaza with fountain in the middle, shops around it, houses,
trees, flowers, lamps, a notice board. Put `act=` on buildings: GENERAL_STORE act=STORE, BLACKSMITH
act=SMITH, CARPENTER act=CARPENTER, CAFE act=CAFE, TOWN_HALL act=HALL, REALTOR act=REALTOR, houses
act=HOUSE; `act x y BOARD` on the notice board; `act x y FOUNTAIN` on the fountain. Leave walkable
streets (key chars with `walk`) connecting everything; borders = trees. Every building door cell must
have a walkable cell directly below it.

## F) `art/31_sprites2.art` — sprites, budget: bank 0 up to 76 more tiles (19 sprites), bank 1 up to 128 tiles (32 sprites)
Bank 1 (`bank=1`), townsfolk (16x16, chibi like the player; palettes PLAYER/MERCHANT/ANIMAL/FX/CURSOR
only — pick per character): for each of ROSA (store owner, apron), BRICK (blacksmith, burly, dark),
JUNE (carpenter, overalls + cap), ADA (café owner, bun hair), HOLLIS (old mayor, top hat), MILO (kid
fisher, bucket hat): <NAME>_DOWN, <NAME>_UP, <NAME>_SIDE (facing left) = 18 sprites. Also DOG_DOWN,
DOG_SIDE, DOG_SIT (pet dog), COW_SIDE, COW_SIDE2 (walk frame), HEN_SIDE, HEN_SIDE2, CROW, BIRD,
SMOKE (chimney puff), BOBBER (fishing bobber on water ripple), FISH (fish silhouette), SPARKLE, EXCLAIM
("!" bubble), HEART (love bubble).
Bank 0: player action frames (PLAYER palette, same character as PL_*): PL_HOE_DOWN, PL_HOE_UP,
PL_HOE_LEFT (swinging a hoe forward), PL_CAN_DOWN, PL_CAN_UP, PL_CAN_LEFT (pouring a watering can),
PL_FISH_DOWN, PL_FISH_UP, PL_FISH_LEFT (holding a rod out), PL_HOLD (facing down holding both arms
up over the head, for showing an item).

## G) `art/music/*.mml` — music (see art/music/FORMAT.md)
