# Stilwell Farms — art manifest (the engine/art contract)

Game: an original Game Boy Color farming game. You inherit your grandpa's run-down farm and rebuild it.
Look: top-down, crisp GBC pixel art, 3/4 view like classic 8-bit RPGs (fronts of buildings visible),
clear dark outlines (colour 3), strong readable silhouettes. ORIGINAL art only: do not copy any existing game's sprites.

Format and compiler: see the docstring of `tools/gen_art.py`. Run `.venv/bin/python tools/gen_art.py`
and inspect `build/art_preview.png` (Read the PNG) to check your work. Palettes are fixed in
`art/00_palettes.art` (do not edit them; tell me if a palette blocks you).

Pixel values: `.`=0 (lightest) `1` `2` `3`=darkest/outline, in the palette named by `pal=`.
One metatile = 16x16 px = four 8x8 tiles; each 8x8 tile can use a different palette via `palmap`.
Every 8x8 tile shows only the 4 colours of ITS palette — plan palette splits on the 8px grid.

Tiles sit next to GRASS on the map, so any metatile that is "an object on grass" should draw grass
around the object using the GRASS palette (palmap) or fill its whole 16x16 cell.
The GRASS base must tile seamlessly with itself and with GRASS2.

## BG palettes
GRASS(G): pale-green, green, dark-green, outline     SOIL(S): light-dirt, tilled-dry, tilled-wet, outline
WOOD(W): cream, orange-brown, dark-brown, outline     STONE(T): white, light-grey, slate-blue, outline
CROP(C): dry-soil, wet-soil, leaf-green, outline      RED(R): dry-soil, leaf-green, red, outline
ORANGE(O): dry-soil, leaf-green, orange, outline      UI(U): white, white, grey, ink(black)

## Terrain + objects (file: art/10_terrain.art) — all 1x1 metatiles
GRASS, GRASS2 (variant w/ tufts), WILDFLOWER (grass with a few tiny flowers),
WEEDS (tall scrubby weed clump, blocks walking), ROCK (small grey rock), BOULDER (big rock filling cell),
STUMP (tree stump), LOG (big fallen log filling the cell), FOREST (dense tree canopy; tiles seamlessly
in every direction — border of the farm), DIRT (packed dirt road, light; tiles seamlessly),
TILLED_DRY (furrowed soil, SOIL pal, colour 1 dominant), TILLED_WET (same furrows, colour 2 dominant),
SEED_DRY / SEED_WET (tilled soil with a few seed dots; CROP pal: soil colour 0 for dry, colour 1 for wet),
SPROUT_DRY / SPROUT_WET (tiny two-leaf sprout), YOUNG_DRY / YOUNG_WET (leafy half-grown plant),
RIPE_RADISH (red), RIPE_CARROT (orange), RIPE_STRAWB (red berries), RIPE_TOMATO (red, staked),
RIPE_CORN (tall stalk, orange/yellow cob), RIPE_PUMPKIN (big orange pumpkin), RIPE_BEET (red),
  — ripe crops: RED or ORANGE pal (colour 0 = dry soil background); leaves green colour 1.
WITHERED (dead brown plant on soil, SOIL pal),
FENCE_POST, FENCE_H (horizontal rail joining L/R neighbours edge-to-edge), FENCE_V (vertical rail),
  — wood fence on grass; FENCE_H rails must meet the cell edges so a row reads as continuous.
PATH (cobblestone path, STONE pal, fills cell, tiles seamlessly), FLOWERBED (bed of colourful flowers),
SAPLING (small young tree on grass), FRUIT_TREE (round tree, fills cell), FRUIT_TREE_RIPE (same with red apples),
SCARECROW, SPRINKLER (small metal sprinkler on grass), BEEHIVE (wooden box hive), LAMP (lamp post),
BENCH (wooden bench), SHIP_BIN (wooden shipping crate with lid), WELL (stone well, 1x1),
SIGN (wooden sign post).

## Structures (file: art/20_structures.art) — multi-metatile, fill their whole footprint
HOUSE_L0 w=3 h=3 — Grandpa's old shack: weathered, crooked boards, patched roof. Door bottom-middle cell.
HOUSE_L1 w=3 h=3 — Cottage: tidy, red roof, window boxes. Door bottom-middle.
HOUSE_L2 w=3 h=3 — Farmhouse: big, bright, porch, chimney. Door bottom-middle.
BARN_RUIN w=3 h=3 — collapsed old barn (broken roof, missing boards). Door bottom-middle.
BARN w=3 h=3 — classic red barn with white X doors. Door bottom-middle.
COOP_RUIN w=2 h=2 — broken chicken coop.   COOP w=2 h=2 — tidy coop. Door bottom-left cell.
GREENHOUSE_RUIN w=3 h=3 — glass house with smashed panes, overgrown.
GREENHOUSE w=3 h=3 — gleaming glass house (STONE pal panes, WOOD frame). Door bottom-middle.
MARKET w=2 h=2 — a market stall with a striped awning and crates of produce (merchant stands in front).
LOGO w=8 h=2 — the title "STILWELL FARMS" as a chunky two-line wordmark (128x32 px), on GRASS-coloured
  background (use G palette pale green as the ground, or W for wood-plank letters). Title screen.

## Sprites (file: art/30_sprites.art) — 16x16, '.' transparent
Player (a young farmer: straw hat, blue overalls; PLAYER pal):
  PL_DOWN_0 (standing facing down), PL_DOWN_1 (walking step facing down; engine mirrors it for the other step),
  PL_UP_0, PL_UP_1, PL_LEFT_0, PL_LEFT_1 (engine mirrors left for right).
MERCHANT (shopkeeper facing down, apron, MERCHANT pal).
CURSOR — one 16x16 bracket frame marking the target tile (CURSOR pal, just corner brackets, centre empty).
DROP (small water droplet splash, FX pal), DUST (small dust puff, FX pal),
RAIN (a couple of slanted raindrops in a 16x16, FX pal),
COW (small cow facing down, ANIMAL pal), CHICKEN (small hen, ANIMAL pal).

## Glyphs (file: art/40_font.art) — 8x8, UI pal: '.' paper (white), 2 grey, 3 ink
A chunky, very legible pixel font in the style of classic 8-bit RPG text boxes (original drawing):
'A'..'Z', 'a'..'z', '0'..'9', and: ' ' '.' ',' '!' '?' ''' '-' ':' '/' '(' ')' '&' '+' '%' '#' '$' '='
Specials (named): CURSOR (solid right-pointing arrow ▶), MORE (down-pointing ▼), HEART, COIN,
TIMES (×), BOX_TL BOX_T BOX_TR BOX_L BOX_R BOX_BL BOX_B BOX_BR (text-box border: a bold double-line
frame like classic RPG dialog boxes; BOX_T/B/L/R repeat), FILL (solid paper), BAR_FULL / BAR_EMPTY
(energy bar segments), and 8x8 tool icons: ICON_HAND ICON_HOE ICON_CAN ICON_SEED ICON_AXE ICON_HAMMER
ICON_SICKLE, plus ICON_SUN ICON_RAIN ICON_SNOW (weather) and ICON_MOON.
Glyph letters should sit on a baseline at row 6 (row 7 blank) except descenders (g j p q y).
