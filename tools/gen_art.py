#!/usr/bin/env python3
"""Stilwell Farms art + map compiler.

Reads art/*.art (text pixel art) and art/maps/*.map and writes:
  src/art.h, src/art_tables.c (bank 0), src/art_gfx.c (banked)
  src/maps.h, src/maps.c (banked)
  build/art_preview.png

.art syntax (one directive per line, '#' comments):
  palette bg|obj KEY NAME c0 c1 c2 c3       hex RGB colours, KEY = one letter
  season NAME SPRING|SUMMER|FALL|WINTER c0 c1 c2 c3   seasonal override of bg palette NAME
  meta NAME [w=1] [h=1] pal=NAME [palmap=AB/CD] [set=SET] [style=STYLE]
        then h*16 rows of w*16 pixel chars ('.' or 0..3)
        set: COMMON (default, always loaded) or a scene tileset (FARM TOWN INTERIOR WILD ...)
        style: swappable variant inside the FARM set (only one style loaded at a time)
  sprite NAME pal=NAME [bank=0|1]   then 16 rows of 16 chars ('.' = transparent)
  glyph 'c' | glyph NAME             then 8 rows of 8; '.' paper(1), 'o' gold(0), 2 accent, 3 ink

VRAM model (CGB, BG tiles at 0x8800/0x9000 in both VRAM banks = 512 BG tiles):
  glyphs (bank1 from 0) + COMMON metas are always resident; one SET is loaded per scene on top;
  FARM additionally has a style slot.  Sprite bank 0 and 1 each hold 128 tiles at 0x8000.

.map syntax:
  map NAME set=SET [w=16] [h=16] [music=NAME]
  key C META [walk|water|block]     legend: char -> metatile + passability (default block)
  grid                              then h rows of w chars
  place META x y [act=ACT] [walk]   stamp a multi-cell meta (blocks unless 'walk')
  act x y ACT                       interaction id at a cell
  warp x y MAP dx dy [down|up|left|right]   stepping on (x,y) moves to MAP at (dx,dy)
"""
import glob, os, re, sys
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIX = {'.': 0, '0': 0, '1': 1, '2': 2, '3': 3, 'o': 4}
SEASONS = ['SPRING', 'SUMMER', 'FALL', 'WINTER']
EXPECTED = {n: (1, 1) for n in '''
WATER_0 WATER_1 WATER_2 WATER_3 WATER_4 WATER_5 WATER_6 WATER_7 WATER_8 WATER_9 WATER_10 WATER_11 WATER_12
WATER_13 WATER_14 WATER_15 WATER_ANIM LILYPAD REEDS BRIDGE_H BRIDGE_V ROPE_H ROPE_V ROPE_POST FOR_SALE PINE BIRCH
OAK_SAPLING BERRY_BUSH BUSH MUSHROOM WILD_FLOWERS ORE_COPPER ORE_IRON ORE_GOLD GEM_ROCK CAVE SAND CLIFF
BRICK GRAVEL PLANK PICKET_H PICKET_V PICKET_POST STONEWALL_H STONEWALL_V STONEWALL_POST MODERNFENCE_H
MODERNFENCE_V MODERNFENCE_POST HEDGE STATUE FOUNTAIN ARCH PICNIC STRINGLIGHTS FLOWERS_RED FLOWERS_BLUE
FLOWERS_YELLOW MAILBOX LIGHTNING_ROD QSPRINKLER KEG CHEESE_PRESS PRESERVE_JAR HAY_BALE TROUGH KEG_READY
PRESS_READY JAR_READY RIPE_CAULI RIPE_MELON RIPE_BLUEBERRY RIPE_EGGPLANT RIPE_CRANBERRY RIPE_SNOWPEA
RIPE_SUNFLOWER PEACH_TREE_RIPE ORANGE_TREE_RIPE CHERRY_TREE_RIPE CHERRY_TREE'''.split()}
EXPECTED.update({n: (2, 2) for n in 'PORTRAIT_GRANDPA PORTRAIT_ROSA PORTRAIT_BRICK PORTRAIT_JUNE PORTRAIT_ADA PORTRAIT_HOLLIS PORTRAIT_MILO PORTRAIT_PET'.split()})
EXPECTED.update({'MINIMAP_A': (1, 1), 'MINIMAP_B': (1, 1)})
EXPECTED.update({'GIANT_PUMPKIN': (2, 2), 'SILO': (1, 2), 'SHED': (2, 2), 'STABLE': (3, 2), 'WINDMILL': (2, 3),
                 'FARM_STAND': (2, 2), 'GAZEBO': (2, 2)})
EXPECTED_SPR = '''ROSA_DOWN ROSA_UP ROSA_SIDE BRICK_DOWN BRICK_UP BRICK_SIDE JUNE_DOWN JUNE_UP JUNE_SIDE ADA_DOWN ADA_UP
ADA_SIDE HOLLIS_DOWN HOLLIS_UP HOLLIS_SIDE MILO_DOWN MILO_UP MILO_SIDE DOG_DOWN DOG_SIDE DOG_SIT COW_SIDE COW_SIDE2
HEN_SIDE HEN_SIDE2 CROW BIRD SMOKE BOBBER FISH SPARKLE EXCLAIM HEART PL_HOE_DOWN PL_HOE_UP PL_HOE_LEFT PL_CAN_DOWN
PL_CAN_UP PL_CAN_LEFT PL_FISH_DOWN PL_FISH_UP PL_FISH_LEFT PL_HOLD'''.split()
EXPECTED_GL = '''IT_FISH IT_ORE IT_GEM IT_JUICE IT_CHEESE IT_JAM IT_FRUIT IT_FORAGE IT_FLOWER IT_MELON IT_STAR IT_STAR2
HEART_EMPTY KEY_DEL KEY_END ICON_ROD ICON_FERT HDR_L HDR_R CAL_FEST CAL_TODAY CAL_DOT MAP_YOU MAP_HOUSE
PANEL_TL PANEL_T PANEL_TR PANEL_L PANEL_R PANEL_BL PANEL_B PANEL_BR SEP ICON_SHOP ICON_CLOCK ICON_PIN ICON_LETTER'''.split()
# hot tiles kept permanently in VRAM bank 1 (after glyphs) instead of the cache
STATIC_METAS = ['GRASS', 'GRASS2', 'DIRT', 'FOREST', 'TILLED_DRY', 'TILLED_WET', 'WATER_15', 'WILDFLOWER',
                'PATH', 'SEED_DRY', 'SEED_WET', 'SPROUT_DRY', 'SPROUT_WET', 'YOUNG_DRY', 'YOUNG_WET', 'WEEDS']


def die(msg):
    print("gen_art: " + msg, file=sys.stderr)
    sys.exit(1)


class Art:
    def __init__(self):
        self.bgpal, self.objpal = [], []
        self.season = {}   # palname -> {season: [rgb]*4}
        self.metas = []    # dict(name,w,h,pm,px,set,style)
        self.sprites = []  # (name, pal, px, bank)
        self.glyphs = []   # (name, px)


def parse(art, path):
    lines = open(path).read().split('\n')
    i = 0

    def take(n, width, what):
        nonlocal i
        rows = []
        while len(rows) < n:
            if i >= len(lines):
                die(f"{path}: EOF inside {what}")
            raw = lines[i].rstrip()
            i += 1
            if raw.strip() == '' or raw.lstrip().startswith('#'):
                continue
            row = raw.strip()
            if len(row) != width:
                die(f"{path}:{i}: {what} row has {len(row)} chars, want {width}: '{row}'")
            try:
                rows.append([PIX[c] for c in row])
            except KeyError as e:
                die(f"{path}:{i}: bad pixel char {e} in {what}")
        return rows

    while i < len(lines):
        raw = lines[i].strip()
        i += 1
        if not raw or raw.startswith('#'):
            continue
        parts = raw.split()
        kw = parts[0]
        opts = dict(p.split('=', 1) for p in parts[2:] if '=' in p)
        if kw == 'palette':
            kind, key, name = parts[1], parts[2], parts[3]
            cols = [tuple(int(h[j:j + 2], 16) for j in (0, 2, 4)) for h in parts[4:8]]
            (art.bgpal if kind == 'bg' else art.objpal).append((key, name, cols))
        elif kw == 'season':
            name, se = parts[1], parts[2]
            if se not in SEASONS:
                die(f"{path}:{i}: bad season {se}")
            art.season.setdefault(name, {})[se] = [tuple(int(h[j:j + 2], 16) for j in (0, 2, 4)) for h in parts[3:7]]
        elif kw == 'meta':
            name = parts[1]
            w, h = int(opts.get('w', 1)), int(opts.get('h', 1))
            names = {n: k for k, (_, n, _) in enumerate(art.bgpal)}
            keys = {key: k for k, (key, _, _) in enumerate(art.bgpal)}
            if opts.get('pal') not in names:
                die(f"{path}:{i}: meta {name} needs pal=<bg palette name>")
            pm = [[names[opts['pal']]] * (w * 2) for _ in range(h * 2)]
            if 'palmap' in opts:
                rows = opts['palmap'].split('/')
                if len(rows) != h * 2 or any(len(r) != w * 2 for r in rows):
                    die(f"{path}:{i}: palmap for {name} must be {h*2} rows of {w*2}")
                pm = [[keys[c] if c in keys else die(f"{path}:{i}: palmap key {c}") for c in r] for r in rows]
            px = take(h * 16, w * 16, f"meta {name}")
            if any(4 in r for r in px):
                die(f"{path}: 'o' is only for glyphs ({name})")
            st = opts.get('style')
            art.metas.append(dict(name=name, w=w, h=h, pm=pm, px=px,
                                  set=opts.get('set', 'FARM' if st else 'COMMON'), style=st))
        elif kw == 'sprite':
            name = parts[1]
            names = {n: k for k, (_, n, _) in enumerate(art.objpal)}
            if opts.get('pal') not in names:
                die(f"{path}:{i}: sprite {name} needs pal=<obj palette name>")
            art.sprites.append((name, names[opts['pal']], take(16, 16, f"sprite {name}"), int(opts.get('bank', 0))))
        elif kw == 'glyph':
            m = re.match(r"glyph\s+'(.)'\s*$", raw)
            gname = m.group(1) if m else parts[1]
            px = take(8, 8, f"glyph {gname}")
            px = [[1 if v == 0 else (0 if v == 4 else v) for v in r] for r in px]
            art.glyphs.append((gname, px))
        else:
            die(f"{path}:{i}: unknown directive '{kw}'")


def tile_bytes(px):
    out = []
    for r in px:
        lo = hi = 0
        for x, v in enumerate(r):
            lo |= (v & 1) << (7 - x)
            hi |= ((v >> 1) & 1) << (7 - x)
        out += [lo, hi]
    return tuple(out)


def sub(px, tx, ty):
    return [row[tx * 8:tx * 8 + 8] for row in px[ty * 8:ty * 8 + 8]]


def flips(t):
    hf = [r[::-1] for r in t]
    vf = t[::-1]
    return [(t, 0), (hf, 0x20), (vf, 0x40), (hf[::-1], 0x60)]


def rgb555(c):
    r, g, b = (v >> 3 for v in c)
    return r | (g << 5) | (b << 10)


def cid(s):
    return re.sub(r'[^A-Z0-9_]', '_', s.upper())


def cells(w, h):
    for my in range(h):
        for mx in range(w):
            for (dx, dy) in ((0, 0), (1, 0), (0, 1), (1, 1)):
                yield mx * 2 + dx, my * 2 + dy


class Space:
    """A run of free VRAM slots; groups allocate on top of a shared parent index."""
    def __init__(self, slots, parent_index):
        self.slots = slots          # list of (bank, idx)
        self.parent = parent_index  # bytes -> (bank, idx) already resident
        self.tiles = []             # tile bytes placed in this space, in slot order
        self.local = {}

    def place(self, t, what):
        for f, fl in flips(t):
            key = tile_bytes(f)
            if key in self.parent:
                return (*self.parent[key], fl)
            if key in self.local:
                return (*self.slots[self.local[key]], fl)
        key = tile_bytes(t)
        if len(self.tiles) >= len(self.slots):
            die(f"{what}: out of VRAM tiles ({len(self.slots)} free)")
        self.tiles.append(key)
        self.local[key] = len(self.tiles) - 1
        return (*self.slots[self.local[key]], 0)

    def index(self):
        d = dict(self.parent)
        for k, j in self.local.items():
            d[k] = self.slots[j]
        return d

    def remaining(self):
        return self.slots[len(self.tiles):]


def runs(space):
    """contiguous same-bank runs: (bank, first_idx, count, offset_in_tiles)"""
    out, j = [], 0
    while j < len(space.tiles):
        bank, idx = space.slots[j]
        n = 1
        while j + n < len(space.tiles) and space.slots[j + n] == (bank, idx + n):
            n += 1
        out.append((bank, idx, n, j))
        j += n
    return out


# ------------------------------------------------------------------ maps
def parse_maps(meta_names):
    maps = []
    for path in sorted(glob.glob(os.path.join(ROOT, 'art', 'maps', '*.map'))):
        lines = [l.rstrip() for l in open(path)]
        m = None
        i = 0
        while i < len(lines):
            raw = lines[i].strip()
            i += 1
            if not raw or raw.startswith('#'):
                continue
            p = raw.split()
            if p[0] == 'map':
                opts = dict(x.split('=', 1) for x in p[2:] if '=' in x)
                m = dict(name=p[1], set=opts.get('set', 'COMMON'), w=int(opts.get('w', 16)), h=int(opts.get('h', 16)),
                         music=opts.get('music', 'NONE'), keys={}, grid=[], places=[], acts=[], warps=[], path=path)
                maps.append(m)
            elif p[0] == 'key':
                ch = raw.split()[1]
                if ch == 'SPACE':
                    ch = ' '
                m['keys'][ch] = (p[2], p[3] if len(p) > 3 else 'block')
            elif p[0] == 'grid':
                while len(m['grid']) < m['h']:
                    row = lines[i]
                    i += 1
                    if row.startswith('# ') or row.strip() == '':
                        continue
                    row = row.rstrip('\n')
                    if len(row) < m['w']:
                        row = row + ' ' * (m['w'] - len(row))
                    m['grid'].append(row[:m['w']])
            elif p[0] == 'place':
                opts = dict(x.split('=', 1) for x in p[4:] if '=' in x)
                m['places'].append((p[1], int(p[2]), int(p[3]), opts.get('act'), 'walk' in p[4:]))
            elif p[0] == 'act':
                m['acts'].append((int(p[1]), int(p[2]), p[3]))
            elif p[0] == 'warp':
                d = p[6] if len(p) > 6 else 'down'
                m['warps'].append((int(p[1]), int(p[2]), p[3], int(p[4]), int(p[5]), d))
            else:
                die(f"{path}: unknown map directive {p[0]}")
    return maps


def compile_maps(maps, meta_index, meta_info):
    """Static maps live in ROM (up to 32x32 cells); the engine reads cells through map_mt/map_kind."""
    H = ["// generated by tools/gen_art.py -- do not edit", "#ifndef MAPS_H", "#define MAPS_H",
         "#include <gb/gb.h>", "#include <stdint.h>", '#include "art.h"', ""]
    acts = []
    for m in maps:
        for (_, _, _, a, _) in m['places']:
            if a and a not in acts:
                acts.append(a)
        for (_, _, a) in m['acts']:
            if a not in acts:
                acts.append(a)
    for k, m in enumerate(maps):
        H.append(f"#define MAP_{cid(m['name'])} {k}")
    H.append(f"#define NUM_MAPS {len(maps)}")
    for k, a in enumerate(acts):
        H.append(f"#define ACT_{cid(a)} {k + 1}")
    H += ["", "#define K_WALK 0x01", "#define K_WATER 0x02", "",
          "typedef struct { uint8_t x, y, map, dx, dy, dir; } Warp;",
          "typedef struct { uint8_t x, y, act; } Act;",
          "#define MAX_WARPS 12", "#define MAX_ACTS 80",
          "extern uint8_t sc_w, sc_h, sc_music, sc_nwarps, sc_nacts, sc_map;",
          "extern Warp sc_warps[MAX_WARPS];", "extern Act sc_acts[MAX_ACTS];",
          "void map_select(uint8_t id) BANKED;         // load metadata, acts, warps for a map",
          "mt_t map_mt(uint16_t i) BANKED;              // i = y*32 + x on the selected map",
          "uint8_t map_kind(uint16_t i) BANKED;", "", "#endif"]
    open(os.path.join(ROOT, 'src', 'maps.h'), 'w').write('\n'.join(H) + '\n')
    names = {m['name']: k for k, m in enumerate(maps)}
    dirs = {'down': 0, 'up': 1, 'left': 2, 'right': 3}
    C = ["// generated by tools/gen_art.py -- do not edit", "#pragma bank 255", "#include <gb/gb.h>",
         "#include <string.h>", '#include "art.h"', '#include "maps.h"', '#include "music.h"', "BANKREF(map_select)",
         "uint8_t sc_w, sc_h, sc_music, sc_nwarps, sc_nacts, sc_map;",
         "Warp sc_warps[MAX_WARPS];", "Act sc_acts[MAX_ACTS];",
         "static const uint16_t *cur_mt;", "static const uint8_t *cur_kind;"]
    table = []
    for k, m in enumerate(maps):
        w, h = m['w'], m['h']
        if w > 32 or h > 32:
            die(f"{m['path']}: max 32x32")
        mt = [meta_index.get('FOREST', 0)] * 1024
        kind = [0] * 1024
        for y in range(h):
            for x in range(w):
                ch = m['grid'][y][x]
                if ch not in m['keys']:
                    die(f"{m['path']}: no key for '{ch}' at {x},{y}")
                name, pas = m['keys'][ch]
                if name not in meta_index:
                    die(f"{m['path']}: unknown meta {name}")
                mt[y * 32 + x] = meta_index[name]
                kind[y * 32 + x] = {'walk': 1, 'water': 2, 'block': 0}[pas]
        al = []
        for (name, px0, py0, act, walk) in m['places']:
            if name not in meta_index:
                die(f"{m['path']}: unknown meta {name}")
            mw, mh, _ = meta_info[name]
            for yy in range(mh):
                for xx in range(mw):
                    j = (py0 + yy) * 32 + px0 + xx
                    mt[j] = meta_index[name] + yy * mw + xx
                    kind[j] = 1 if walk else 0
                    if act:
                        al.append((px0 + xx, py0 + yy, act))
        al += m['acts']
        if len(al) > 80 or len(m['warps']) > 12:
            die(f"{m['path']}: too many acts ({len(al)}) or warps")
        C.append(f"static const uint16_t m{k}_mt[1024] = {{{','.join(map(str, mt))}}};")
        C.append(f"static const uint8_t m{k}_kind[1024] = {{{','.join(map(str, kind))}}};")
        C.append(f"static const Act m{k}_acts[] = {{" + ",".join(f"{{{x},{y},ACT_{cid(a)}}}" for x, y, a in al) + ("" if al else "{0,0,0}") + "};")
        wl = []
        for (x, y, dest, dx, dy, d) in m['warps']:
            if dest not in names:
                die(f"{m['path']}: warp to unknown map {dest}")
            wl.append(f"{{{x},{y},{names[dest]},{dx},{dy},{dirs[d]}}}")
        C.append(f"static const Warp m{k}_warps[] = {{" + ",".join(wl) + ("" if wl else "{0,0,0,0,0,0}") + "};")
        table.append((k, w, h, f"MUS_{cid(m['music'])}", len(al), len(wl)))
    C += ["void map_select(uint8_t id) BANKED {", "  sc_map = id;", "  switch (id) {"]
    for (k, w, h, mus, na, nw) in table:
        C.append(f"  case {k}: cur_mt = m{k}_mt; cur_kind = m{k}_kind;"
                 f" memcpy(sc_acts, m{k}_acts, sizeof(Act) * {max(na, 1)}); memcpy(sc_warps, m{k}_warps, sizeof(Warp) * {max(nw, 1)});"
                 f" sc_w = {w}; sc_h = {h}; sc_music = {mus}; sc_nacts = {na}; sc_nwarps = {nw}; break;")
    C += ["  }", "}",
          "mt_t map_mt(uint16_t i) BANKED { return cur_mt[i]; }",
          "uint8_t map_kind(uint16_t i) BANKED { return cur_kind[i]; }"]
    open(os.path.join(ROOT, 'src', 'maps.c'), 'w').write('\n'.join(C) + '\n')
    return len(maps)


# ------------------------------------------------------------------ main
def main():
    art = Art()
    files = sorted(glob.glob(os.path.join(os.environ.get('ART_DIR', os.path.join(ROOT, 'art')), '*.art')))
    only = sys.argv[1:]
    if only:
        files = [f for f in files if 'palette' in os.path.basename(f)] + [os.path.abspath(o) for o in only]
    files.sort(key=lambda p: (0 if 'palette' in os.path.basename(p) else 1, p))
    for f in files:
        parse(art, f)
    if len(art.bgpal) > 8 or len(art.objpal) > 8:
        die("max 8 palettes per kind")
    if not any(n == 'UI' for (_, n, _) in art.bgpal):
        die("need a bg palette named UI")

    # ---------------- VRAM plan (tile cache) ----------------
    # bank1: glyphs from 0, then STATIC hot tiles; bank0 0..254: runtime cache of every other tile.
    glyph_tiles = {}
    glyph_bytes = []
    for gname, px in art.glyphs:
        if gname in glyph_tiles:
            die(f"duplicate glyph {gname}")
        glyph_bytes.append(tile_bytes(px))
        glyph_tiles[gname] = len(glyph_bytes) - 1
    static_slots = [(1, i) for i in range(len(glyph_bytes), 256)]
    static = Space(static_slots, {})
    pool = {}        # tile bytes -> cache id
    pool_bytes = []
    placed = {}
    by_name = {m['name']: k for k, m in enumerate(art.metas)}
    for nm in STATIC_METAS:
        if nm in by_name:
            m = art.metas[by_name[nm]]
            for tx, ty in cells(m['w'], m['h']):
                static.place(sub(m['px'], tx, ty), 'static')
    sindex = static.index()

    def quad(t):
        for f, fl in flips(t):
            key = tile_bytes(f)
            if key in sindex:
                return 0x8000 | sindex[key][1], fl
            if key in pool:
                return pool[key], fl
        key = tile_bytes(t)
        pool[key] = len(pool_bytes)
        pool_bytes.append(key)
        return pool[key], 0

    mt_q, mt_pf, mt_ids, structs = [], [], [], []
    meta_index, meta_info = {}, {}
    for k, m in enumerate(art.metas):
        first = len(mt_ids)
        meta_index[m['name']] = first
        meta_info[m['name']] = (m['w'], m['h'], m['set'])
        for j, (tx, ty) in enumerate(cells(m['w'], m['h'])):
            q, fl = quad(sub(m['px'], tx, ty))
            mt_q.append(q)
            mt_pf.append(m['pm'][ty][tx] | fl)
            if j % 4 == 3:
                mt_ids.append(m['name'] if m['w'] * m['h'] == 1 else f"{m['name']}_{j // 4}")
        if m['w'] * m['h'] > 1:
            structs.append((m['name'], first, m['w'], m['h']))

    spr = {0: [], 1: []}
    spr_ids = []
    for name, pal, px, bank in art.sprites:
        spr_ids.append((name, len(spr[bank]), pal, bank))
        for tx, ty in ((0, 0), (0, 1), (1, 0), (1, 1)):
            spr[bank].append(tile_bytes(sub(px, tx, ty)))
    for b in (0, 1):
        if len(spr[b]) > 128:
            die(f"too many sprite tiles in bank {b} ({len(spr[b])} > 128)")

    report = (f"glyphs {len(glyph_bytes)}, static {len(static.tiles)}/{len(static_slots)}, cache pool {len(pool_bytes)} tiles, "
              f"metatiles {len(mt_ids)}, sprites b0 {len(spr[0])}/128 b1 {len(spr[1])}/128")
    if only:
        tag = os.path.splitext(os.path.basename(only[0]))[0]
        preview(art, f'preview_{tag}.png')
        print("gen_art (preview): " + report + f" -> build/preview_{tag}.png")
        return

    src = os.path.join(ROOT, 'src')
    H = ["// generated by tools/gen_art.py -- do not edit", "#ifndef ART_H", "#define ART_H",
         "#include <gb/gb.h>", "#include <stdint.h>", ""]
    for k, (_, n, _) in enumerate(art.bgpal):
        H.append(f"#define PAL_{cid(n)} {k}")
    for k, (_, n, _) in enumerate(art.objpal):
        H.append(f"#define SPAL_{cid(n)} {k}")
    H.append("typedef uint16_t mt_t;")
    for k, n in enumerate(mt_ids):
        H.append(f"#define MT_{cid(n)} {k}")
    H.append(f"#define MT_COUNT {len(mt_ids)}")
    for n, first, w, h in structs:
        H += [f"#define ST_{cid(n)} {first}", f"#define ST_{cid(n)}_W {w}", f"#define ST_{cid(n)}_H {h}"]
    for n, t, p, b in spr_ids:
        H += [f"#define SPR_{cid(n)} {t}", f"#define SPR_{cid(n)}_PAL ({p} | {b << 3})"]
    for g, t in glyph_tiles.items():
        if len(g) > 1:
            H.append(f"#define GL_{cid(g)} {t}")
    H += [f"#define NUM_CTILES {len(pool_bytes)}",
          "#define GL_ATTR (0x80 | 0x08 | PAL_UI)", "",
          "extern const uint8_t font_map[96];",
          "extern const uint16_t bg_pal_base[32];", "extern const uint16_t obj_pal_base[32];",
          "extern const uint16_t season_pal[4][32];",
          "void art_load(void) BANKED;",
          "void mt_get(mt_t mt, uint16_t *q, uint8_t *pf) BANKED;  // 4 quads: bit15 = static bank1 tile",
          "void ctile_upload(uint16_t id, uint8_t slot);            // copy cache tile into bank0 slot",
          "void art_water(uint8_t phase) BANKED;                     // water shimmer frame",
          "", "#endif"]
    open(os.path.join(src, 'art.h'), 'w').write('\n'.join(H) + '\n')
    # placeholders for art that hasn't been drawn yet, so the engine always compiles
    have = set(mt_ids) | {n for n, _, _, _ in structs}
    F = ["// generated by tools/gen_art.py: fallbacks for metas not drawn yet", "#ifndef FALLBACK_H", "#define FALLBACK_H"]
    missing = []
    for nm, (w, h) in EXPECTED.items():
        if nm in have:
            continue
        missing.append(nm)
        if w * h == 1:
            F.append(f"#define MT_{cid(nm)} MT_GRASS")
        else:
            F += [f"#define ST_{cid(nm)} ST_COOP", f"#define ST_{cid(nm)}_W {w}", f"#define ST_{cid(nm)}_H {h}"]
    for nm in EXPECTED_SPR:
        if nm not in {n for n, _, _, _ in spr_ids}:
            missing.append(nm)
            F += [f"#define SPR_{cid(nm)} SPR_DUST", f"#define SPR_{cid(nm)}_PAL SPR_DUST_PAL"]
    for nm in EXPECTED_GL:
        if nm not in glyph_tiles:
            missing.append(nm)
            F.append(f"#define GL_{cid(nm)} GL_FILL")
    F.append("#endif")
    open(os.path.join(src, 'fallback.h'), 'w').write('\n'.join(F) + '\n')
    if missing:
        print(f"gen_art: {len(missing)} placeholder(s): " + ' '.join(missing[:40]) + (' ...' if len(missing) > 40 else ''))

    def carr(name, vals, typ='uint8_t', per=16, static_=False):
        s_ = [f"{'static ' if static_ else ''}const {typ} {name}[] = {{"]
        for j in range(0, len(vals), per):
            s_.append("  " + ", ".join(str(v) for v in vals[j:j + per]) + ",")
        s_.append("};")
        return s_

    blank = glyph_tiles.get(' ', 0)
    fmap = [glyph_tiles.get(chr(c), blank) for c in range(32, 128)]
    bgp = [rgb555(c) for (_, _, cs) in art.bgpal for c in cs] + [0] * (32 - 4 * len(art.bgpal))
    obp = [rgb555(c) for (_, _, cs) in art.objpal for c in cs] + [0] * (32 - 4 * len(art.objpal))
    sp_rows = []
    for se in SEASONS:
        row = []
        for (_, n, cs) in art.bgpal:
            row += [rgb555(c) for c in art.season.get(n, {}).get(se, cs)]
        row += [0] * (32 - len(row))
        sp_rows.append("  {" + ", ".join(map(str, row)) + "},")
    T = ["// generated by tools/gen_art.py -- do not edit", '#include "art.h"']
    T += carr('font_map', fmap)
    T += carr('bg_pal_base', bgp, 'uint16_t', 8) + carr('obj_pal_base', obp, 'uint16_t', 8)
    T += ["const uint16_t season_pal[4][32] = {"] + sp_rows + ["};"]
    open(os.path.join(src, 'art_tables.c'), 'w').write('\n'.join(T) + '\n')

    flat = lambda ts: [b for t in ts for b in t] or [0]
    # metatile quads: a header included by src/view.c only, so the renderer's bank holds them
    # (mt_get, for other banks, is defined there)
    M = ["// generated by tools/gen_art.py -- do not edit (included by view.c only)"]
    M += carr('mt_q', mt_q, 'uint16_t', 12, True) + carr('mt_pf', mt_pf, 'uint8_t', 16, True)
    # ready-to-write BG map bytes: static quads final (bank 1 tile + attr | 0x08); mt_cf bit k = quad k
    # is a cache tile the renderer fills in (0 = the cell is written straight from these tables)
    mt_tile = [q & 0xFF if q & 0x8000 else 0 for q in mt_q]
    mt_attr = [(pf | 0x08) if q & 0x8000 else pf for q, pf in zip(mt_q, mt_pf)]
    mt_cf = [sum(1 << k for k in range(4) if not mt_q[4 * i + k] & 0x8000) for i in range(len(mt_q) // 4)]
    M += carr('mt_tile', mt_tile, 'uint8_t', 16, True) + carr('mt_attr', mt_attr, 'uint8_t', 16, True)
    M += carr('mt_cf', mt_cf, 'uint8_t', 16, True)
    open(os.path.join(src, 'art_mt.h'), 'w').write('\n'.join(M) + '\n')
    if os.path.exists(os.path.join(src, 'art_mt.c')):
        os.remove(os.path.join(src, 'art_mt.c'))
    # cache tile pool, split into chunks of 896 tiles (14 KB) per bank
    CH = 896
    nch = (len(pool_bytes) + CH - 1) // CH
    U = ["// generated by tools/gen_art.py -- do not edit", '#include <gb/gb.h>', '#include "art.h"']
    for c in range(nch):
        U.append(f"void ctile_up{c}(uint16_t id, uint8_t slot) BANKED;")
        X = ["// generated by tools/gen_art.py -- do not edit", "#pragma bank 255", "#include <gb/gb.h>",
             '#include "art.h"', f"BANKREF(ctile_up{c})"]
        X += carr(f'ct{c}', flat(pool_bytes[c * CH:(c + 1) * CH]), static_=True)
        # straight to VRAM (bank 0, BG tiles in 8800 mode: slot s at 0x8800 + ((s + 128) & 255) * 16), 2 bytes
        # per STAT wait in asm: VRAM stays writable through mode 2 (80 dots = 40 cycles at double speed) and
        # the second write lands ~13 cycles after the STAT read. set_bkg_data cost ~4x this per tile, and a
        # C loop doing 4 bytes per wait ran into mode 3 and lost bytes. cpy16: de = dst, bc = src (sdcccall 1)
        X += ["static void cpy16(uint8_t *d, const uint8_t *s) __naked {",
              "  (void)d; (void)s;",
              "  __asm",
              "    ld   h, b",
              "    ld   l, c",
              "    ld   c, #8",
              "  1$:",
              "    ldh  a, (_STAT_REG + 0)",
              "    and  a, #2",
              "    jr   nz, 1$",
              "    ld   a, (hl+)",
              "    ld   (de), a",
              "    inc  de",
              "    ld   a, (hl+)",
              "    ld   (de), a",
              "    inc  de",
              "    dec  c",
              "    jr   nz, 1$",
              "    ret",
              "  __endasm;",
              "}",
              f"void ctile_up{c}(uint16_t id, uint8_t slot) BANKED {{",
              f"  cpy16((uint8_t *)(0x8800 + ((uint16_t)(uint8_t)(slot + 128) << 4)), ct{c} + ((id - {c * CH}) << 4));",
              "}"]
        open(os.path.join(src, f'art_ct{c}.c'), 'w').write('\n'.join(X) + '\n')
    for old in glob.glob(os.path.join(src, 'art_ct*.c')):
        k = int(re.search(r'art_ct(\d+)\.c', old).group(1))
        if k >= nch:
            os.remove(old)
    U += ["void ctile_upload(uint16_t id, uint8_t slot) {"]
    for c in range(nch):
        U.append(f"  if (id < {(c + 1) * CH}) {{ ctile_up{c}(id, slot); return; }}")
    U += ["}"]
    open(os.path.join(src, 'art_upload.c'), 'w').write('\n'.join(U) + '\n')
    G = ["// generated by tools/gen_art.py -- do not edit", "#pragma bank 255",
         "#include <gb/gb.h>", "#include <gb/cgb.h>", '#include "art.h"', "BANKREF(art_load)"]
    G += carr('glyphs', flat(glyph_bytes), static_=True) + carr('stat', flat(static.tiles), static_=True)
    G += carr('spr0', flat(spr[0]), static_=True) + carr('spr1', flat(spr[1]), static_=True)
    # water shimmer: static WATER_15 quads alternate with WATER_ANIM pixels
    wa = []
    if 'WATER_15' in by_name and 'WATER_ANIM' in by_name:
        m15, man = art.metas[by_name['WATER_15']], art.metas[by_name['WATER_ANIM']]
        for tx, ty in cells(1, 1):
            t15 = sub(m15['px'], tx, ty)
            k15 = tile_bytes(t15)
            if k15 in sindex:
                wa.append((sindex[k15][1], k15, tile_bytes(sub(man['px'], tx, ty))))
    G += carr('wa0', flat([w[1] for w in wa]), static_=True) + carr('wa1', flat([w[2] for w in wa]), static_=True)
    G += ["void art_water(uint8_t phase) BANKED {", "  const uint8_t *src = phase ? wa1 : wa0;", "  VBK_REG = 1;"]
    for k, (slot, _, _) in enumerate(wa):
        G.append(f"  set_bkg_data({slot}, 1, src + {k * 16});")
    G += ["  VBK_REG = 0;", "}"]
    G += ["void art_load(void) BANKED {",
          f"  VBK_REG = 1; set_bkg_data(0, {len(glyph_bytes)}, glyphs);",
          f"  set_bkg_data({len(glyph_bytes)}, {len(static.tiles)}, stat);" if static.tiles else "",
          "  VBK_REG = 0;",
          f"  set_sprite_data(0, {len(spr[0])}, spr0);" if spr[0] else "",
          f"  VBK_REG = 1; set_sprite_data(0, {len(spr[1])}, spr1); VBK_REG = 0;" if spr[1] else "",
          "  set_bkg_palette(0, 8, bg_pal_base);", "  set_sprite_palette(0, 8, obj_pal_base);", "}"]
    open(os.path.join(src, 'art_gfx.c'), 'w').write('\n'.join(G) + '\n')
    nmaps = compile_maps(parse_maps(meta_index), meta_index, meta_info)
    preview(art, 'art_preview.png')
    print("gen_art: " + report + f"; maps {nmaps}")


def preview(art, out):
    S = 3
    items = []
    for m in art.metas:
        w, h, pm, px = m['w'], m['h'], m['pm'], m['px']
        im = Image.new('RGB', (w * 16, h * 16))
        for y in range(h * 16):
            for x in range(w * 16):
                im.putpixel((x, y), art.bgpal[pm[y // 8][x // 8]][2][px[y][x]])
        items.append((m['name'], im))
    for name, pal, px, _ in art.sprites:
        im = Image.new('RGB', (16, 16), (255, 0, 255))
        for y in range(16):
            for x in range(16):
                if px[y][x]:
                    im.putpixel((x, y), art.objpal[pal][2][px[y][x]])
        items.append((name, im))
    ui = next(cs for (_, n, cs) in art.bgpal if n == 'UI')
    for g, px in art.glyphs:
        im = Image.new('RGB', (8, 8))
        for y in range(8):
            for x in range(8):
                im.putpixel((x, y), ui[px[y][x]])
        items.append((g, im))
    x, y, rowh, W = 4, 4, 0, 1400
    placed = []
    for name, im in items:
        iw, ih = im.width * S, im.height * S
        cw = max(iw, 8 * min(len(name), 14)) + 8
        if x + cw > W:
            x, y, rowh = 4, y + rowh + 16, 0
        placed.append((name, im, x, y))
        x += cw
        rowh = max(rowh, ih)
    sheet = Image.new('RGB', (W, y + rowh + 20), (40, 40, 48))
    d = ImageDraw.Draw(sheet)
    for name, im, px_, py in placed:
        sheet.paste(im.resize((im.width * S, im.height * S), Image.NEAREST), (px_, py + 12))
        d.text((px_, py), name[:14], fill=(230, 230, 230))
    os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
    sheet.save(os.path.join(ROOT, 'build', out))


if __name__ == '__main__':
    main()
