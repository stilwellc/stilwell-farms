#!/usr/bin/env python3
"""Stilwell Farms QA suite: named scripted scenarios that play the ROM headless in PyBoy,
assert on GameState (read from WRAM at the _G symbol) and save screenshots.

Usage:
  .venv/bin/python tools/test_suite.py [SCENARIO ...] [--list] [--rom build/stilwell_farms.gbc]

Screenshots go to build/qa/<scenario>/NN_name.png (3x).  Every frame the CPU's PC is sampled; if the
game goes HANG_FRAMES frames without ever being inside wait_vbl_done (where every idle loop of the
game parks), the scenario fails with a hang report naming the hottest PC.

Offsets into GameState are computed from the field list below, which mirrors src/game.h (packed,
SDCC has no padding); the total is checked against the distance from _G to the next symbol.
"""
import argparse, io, os, re, shutil, sys, time, traceback
from collections import deque, Counter

os.environ.setdefault('PYSDL2_DLL_PATH', '')
import warnings
warnings.filterwarnings('ignore')
from pyboy import PyBoy

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ------------------------------------------------------------------ constants + GameState layout, read from the sources
# Nothing here is hand-copied: enums/#defines come from src/game.h, decor.h, interior.c, the generated src/art.h and
# src/maps.h; the GameState layout is computed from game.h's struct (SDCC packs structs: no padding); prices and
# catalogue tables are parsed from items.c, decor.c, menus.c, world.c.  The total is checked against the .map file.
def _src(name):
    return open(os.path.join(ROOT, 'src', name)).read()


def _nocomment(s):
    s = re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)
    return re.sub(r'//[^\n]*', '', s)


CONST = {}


def _cval(expr):
    e = re.sub(r'\b(0x[0-9A-Fa-f]+|\d+)[uUlL]+\b', r'\1', expr.strip())
    e = re.sub(r'\((?:u?int(?:8|16|32)_t|char|mt_t)\)', '', e)
    return int(eval(e, {'__builtins__': {}}, CONST))


def _parse_defines(text):
    for name, val in re.findall(r'^\s*#define\s+([A-Za-z_]\w*)[ \t]+([^\n]+)$', _nocomment(text), re.M):
        try:
            CONST.setdefault(name, _cval(val))
        except Exception:
            pass


def _parse_enums(text):
    for body in re.findall(r'\benum\s*\w*\s*\{([^}]*)\}', _nocomment(text)):
        v = -1
        for item in body.split(','):
            item = item.strip()
            if not item:
                continue
            if '=' in item:
                name, expr = item.split('=', 1)
                v = _cval(expr)
            else:
                name, v = item, v + 1
            CONST[name.strip()] = v


for _f in ('art.h', 'maps.h', 'game.h', 'decor.h', 'designer.h', 'interior.c', 'menus.c', 'actors.c'):
    _t = _src(_f)
    _parse_defines(_t)
    _parse_enums(_t)

_TSIZE = {'uint8_t': 1, 'int8_t': 1, 'char': 1, 'uint16_t': 2, 'int16_t': 2, 'mt_t': 2, 'uint32_t': 4, 'int32_t': 4}
STRUCTS = {}    # name -> (size, {field: (offset, elem_size, dims)})


def _parse_structs(text):
    for body, name in re.findall(r'typedef\s+struct\s*\{(.*?)\}\s*(\w+)\s*;', _nocomment(text), re.S):
        off, fields = 0, {}
        for decl in body.split(';'):
            decl = ' '.join(decl.split())
            if not decl:
                continue
            m = re.match(r'(?:const\s+)?(\w+)\s+(.*)', decl)
            typ, rest = m.groups()
            for var in rest.split(','):
                var = var.strip()
                ptr = var.startswith('*')
                var = var.lstrip('*').strip()
                fname = re.match(r'\w+', var).group(0)
                dims = [_cval(d) for d in re.findall(r'\[([^\]]+)\]', var)]
                es = 2 if ptr else (_TSIZE[typ] if typ in _TSIZE else STRUCTS[typ][0])
                n = 1
                for d in dims:
                    n *= d
                fields[fname] = (off, es, dims)
                off += es * n
        STRUCTS[name] = (off, fields)
        _TSIZE.setdefault(name, off)


_parse_structs(_src('maps.h'))
_parse_structs(_src('game.h'))
GSIZE, _GF = STRUCTS['GameState']
OFF = {n: f[0] for n, f in _GF.items()}
DIMS = {n: f[2] for n, f in _GF.items()}
ANIM_SIZE = STRUCTS['Animal'][0]
ANIM_OFF = {n: f[0] for n, f in STRUCTS['Animal'][1].items()}
BLD_SIZE = STRUCTS['Building'][0]


def _table(src, name):
    """the brace body of `name[...] = { ... };` in a source file"""
    m = re.search(r'(?<!\w)' + re.escape(name) + r'\s*(?:\[[^\]]*\])+\s*=\s*\{(.*?)\};', _nocomment(src), re.S)
    return m.group(1)


def _ints(body):
    return [_cval(v) for v in body.split(',') if v.strip()]


globals().update({k: v for k, v in CONST.items() if re.match(
    r'(C|T|TT|B|DK|I|A|R|ROLE|NPC|D|SEASON|MAP|GL|SPR|SPAL|ACT|K|AF|SOIL|BF|FK|MK|BK|SAVE|NUM|MAX|START|WOOL|ADULT|BARN|PANTRY|TREE|CROP)_\w+$', k)})
NUM_ITEMS, NUM_CROPS, NUM_NPC, NUM_TT, NUM_RARITY = CONST['NUM_ITEMS'], CONST['NUM_CROPS'], CONST['NUM_NPC'], CONST['NUM_TT'], CONST['NUM_RARITY']
MAX_BLD, MAX_ANIMALS, MAX_FURN, NUM_BIZ = CONST['MAX_BLD'], CONST['MAX_ANIMALS'], CONST['MAX_FURN'], CONST['NUM_BIZ']
_items_c = _src('items.c')
ITEMS = re.findall(r'\{"([^"]*)",\s*(\d+)\}', _table(_items_c, 'items'))
ITEM_NAME = [n for n, _ in ITEMS]
ITEM_PRICE = [int(p) for _, p in ITEMS]
assert len(ITEM_PRICE) == NUM_ITEMS, 'items.c table does not match NUM_ITEMS'
CROPS = [_ints(r) for r in re.findall(r'\{([^{}]*)\}', _table(_items_c, 'crops'))]   # season, seed_cost, days, regrow, mt
SEED_COST = [r[1] for r in CROPS]
CROP_DAYS = [r[2] for r in CROPS]
CROP_REGROW = [r[3] for r in CROPS]
DECOR = re.findall(r'\{"([^"]*)",\s*\w+,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}', _table(_src('decor.c'), 'decor'))
DECOR_GOLD = [int(d[3]) for d in DECOR]
DECOR_WALK = {k for k, d in enumerate(DECOR) if d[1] == '1'}
_world_c = _src('world.c')
BW = _ints(_table(_world_c, 'bw'))
BH = _ints(_table(_world_c, 'bh'))
PLOT_PRICE = _ints(_table(_world_c, 'plot_prices'))     # 64 plots (8x8 grid)


def OLD_PLOT(p):
    """plot p of the old 4x4 grid (the home valley) as a plot of the 8x8 grid"""
    return ((p >> 2) + 2) * 8 + (p & 3) + 2
_menus_c = _src('menus.c')
CAT_N = _ints(_table(_menus_c, 'cat_n'))
BLDG_GOLD = _ints(_table(_menus_c, 'bldg_gold'))
_interior_c = _src('interior.c')
FURN = re.findall(r'\{"([^"]*)",\s*\w+,\s*(\d+),\s*(\d+),\s*([\w|]+),\s*(\d+),', _table(_interior_c, 'fdef'))
FURN_PRICE = [int(f[4]) for f in FURN]
FURN_WH = [(int(f[1]), int(f[2])) for f in FURN]
FURN_CAT = [CONST[n.strip()] for n in _table(_interior_c, 'cat').split(',') if n.strip()]
_biz_c = _src('biz.c')
BIZ_PRICE = _ints(_table(_biz_c, 'biz_price'))
BIZ_UP_PRICE = _ints(_table(_biz_c, 'up_price'))
BIZ_CAP = _ints(_table(_biz_c, 'cap_tab'))
BIZ_SPOT = list(zip(_ints(_table(_biz_c, 'spot_x')), _ints(_table(_biz_c, 'spot_y'))))
BIZ_MENU_N = len(re.findall(r'"[^"]*"', _table(_biz_c, 'bm')))           # Stock Status Kitchen Facade Staff Bye
BIZ_TIER_PRICE = _ints(_table(_biz_c, 'tier_price'))
BIZ_BQ = _ints(_table(_biz_c, 'bq_base'))             # basic product: ingredient x (bq + kitchen level) / 4
BIZ_PQ = _ints(_table(_biz_c, 'pq_base'))             # premium: ingredients' total x (pq + level) / 4
_bdef = dict(re.findall(r'#define (BIZ_PCT_\w+|INN_RATE) (\d+)', _biz_c))
BIZ_PCT_TIER, BIZ_PCT_PROSP, BIZ_PCT_MGR, BIZ_PCT_INN, INN_RATE = (int(_bdef[k]) for k in (
    'BIZ_PCT_TIER', 'BIZ_PCT_PROSP', 'BIZ_PCT_MGR', 'BIZ_PCT_INN', 'INN_RATE'))
MACHINE_DAYS = [int(d) for d in re.findall(r'days = (\d+);', _nocomment(_src('farm.c')))]   # keg, press, jar
LOT_X = _ints(_table(_biz_c, 'lot_x'))
_invest_c = _src('invest.c')
TPROJ = [tuple(_ints(row)) for row in re.findall(r'\{([\d, ]+)\}', _table(_invest_c, 'tproj'))]   # gold wood stone need_p market
BIZ_PAN = [[CONST.get(n.strip(), 0xFF) for n in row.split(',')] for row in re.findall(r'\{([^{}]*)\}', _table(_biz_c, 'pan_item'))]
_staff_c = _src('staff.c')
WAGE_BASE = _ints(_table(_staff_c, 'wage_base'))
WAGE_PLOT = _ints(_table(_staff_c, 'wage_plot'))
_town_c = _src('town.c')
# the mine + beach (areas.c): rock kinds and beach finds are hashes, mirrored here to plan routes and pick targets
_areas_c = _src('areas.c')
MINE_ODDS = [_ints(r) for r in re.findall(r'\{([^{}]*)\}', _table(_areas_c, 'odds'))]
MINE_ODDS_KIND = [CONST.get(n.strip(), None) for n in _table(_areas_c, 'odds_kind').split(',') if n.strip()]
BEACH_SPOTS = list(zip(_ints(_table(_areas_c, 'spot_x')), _ints(_table(_areas_c, 'spot_y'))))
MINE_HOLE = (4, 4)                      # mine.map: the shaft ladder (H) down to floor 1
GEM_ITEMS = [CONST['I_GEM' if n.strip() == 'I_AMETHYST' else n.strip()] for n in _table(_areas_c, 'gem_item').split(',') if n.strip()]


def mix8(v):
    v ^= (v << 3) & 255
    v ^= v >> 5
    v = (v + 0x9B) & 255
    v ^= (v << 2) & 255
    return v


def mine_band(floor):
    return 0 if floor <= 5 else 1 if floor <= 10 else 2 if floor <= 15 else 3


def rock_kind(x, y, seed, floor):
    """areas.c rock_kind: 0 rock 1 rock2 2 copper 3 iron 4 gold 5 coal 6 gem node"""
    h = mix8(mix8((x << 4) | y) ^ seed)
    for k, o in enumerate(MINE_ODDS[mine_band(floor)]):
        if h < o:
            return [6, 4, 3, 2, 5][k]
    return 1 if h & 1 else 0


def beach_item(k, stamp, taken):
    """areas.c spot_item: what lies on beach spot k (None: nothing)"""
    if taken & (1 << k):
        return None
    h = mix8(mix8((stamp + k * 37) & 255) ^ ((k << 5) & 255))
    if k < 6 and not (h & 3):
        return None
    h >>= 2
    if k >= 6:
        return I_CORAL if h < 24 else I_SHELL if h < 44 else I_SEAGLASS
    return I_SHELL if h < 26 else I_SEAGLASS if h < 40 else I_DRIFTWOOD if h < 52 else I_CORAL
STORE_N = len(re.findall(r'"[^"]*"', _table(_town_c, 'store_items')))
HALL_N = len(re.findall(r'"[^"]*"', _table(_src('staff.c'), 'hall_items'))) - 1   # Joe's menu; + 1 on a festival day
FARM_N = len(re.findall(r'"[^"]*"', _table(_src('menus.c'), 'farm_items')))        # START > FARM
TIER_GOLD = _ints(_table(_town_c, 'tier_gold'))
CAFE_PRICE = _ints(_table(_town_c, 'price'))
BREED_PRICE = [_ints(r) for r in re.findall(r'\{([^{}]*)\}', _table(_town_c, 'breed_price'))]
RAR_MULT = _ints(_table(_town_c, 'rar_mult'))
LEGEND_EARNED = int(re.search(r'#define LEGEND_EARNED (\d+)', _town_c).group(1))   # Alyssa's legendary stock unlocks
SCENE_FARM = CONST['SCENE_FARM']
# The farm world is 64x64 cells in cart SRAM (bank WB_LIVE: cell at 0xA000, a1 at 0xB000; bank WB_A2: a2 at
# 0xA000). Scenarios address the farm in the old 32x32 home-valley frame: farm coordinates passed to and returned
# by the driver (cell/a1/a2/setcell/owned/walkable/go/pos/bld/actors) are real cells minus FO, so the home valley
# is 0..31 as it always was and the outer valley is -16..-1 and 32..47. Real coordinates: wcell/wa1/wa2 and
# real_pos. u8/w8 of 'cell'/'a1'/'a2' with an index y*32+x and u16('plots') keep reading the home valley the old way.
WW, WH, FO = CONST['WW'], CONST['WH'], CONST['FARM_OX']
WB_LIVE, WB_A2 = CONST['WB_LIVE'], CONST['WB_A2']
RAM_SIZE = 4 * 0x2000                  # 32KB battery file (4 banks)
DIRS = {'down': (0, 1, D_DOWN), 'up': (0, -1, D_UP), 'left': (-1, 0, D_LEFT), 'right': (1, 0, D_RIGHT)}
DIRNAME = {v[2]: k for k, v in DIRS.items()}
HANG_FRAMES = 420


def ITEM_VALUE(it, q=0):
    v = ITEM_PRICE[it]
    return v + (v >> 2 if q == 1 else v >> 1 if q == 2 else 0)


YN_Y = 5                # ui_yesno box row


class Fail(Exception):
    pass


class Hang(Exception):
    pass


def parse_symbols(mapfile):
    txt = open(mapfile).read()
    return {n: int(a, 16) for a, n in re.findall(r'([0-9A-F]{8})\s+([_.A-Za-z][\w.]*)', txt)}


def map_warps(map_id):
    """[(x, y, map, dx, dy, dir)] of a static map, from src/maps.c (m<id>_warps)"""
    src = open(os.path.join(ROOT, 'src/maps.c')).read() + open(os.path.join(ROOT, 'src/maps2.c')).read()
    m = re.search(r'm%d_warps\[\] = \{(.*?)\};' % map_id, src)
    return [tuple(int(v) for v in w.split(',')) for w in re.findall(r'\{([\d,]+)\}', m.group(1))]


def map_kind(map_id):
    """walkability (K_* bits) of a static map, from the generated src/maps.c (m<id>_kind)"""
    src = open(os.path.join(ROOT, 'src/maps.c')).read() + open(os.path.join(ROOT, 'src/maps2.c')).read()
    w, h, stride = (int(v) for v in re.search(r'// m%d: \w+ w=(\d+) h=(\d+) stride=(\d+)' % map_id, src).groups())
    m = re.search(r'm%d_kind\[\d+\] = \{([^}]*)\}' % map_id, src)
    rows = [int(v) for v in m.group(1).split(',')]
    out = [0] * 1024
    for y in range(h):
        for x in range(w):
            out[y * 32 + x] = rows[y * stride + x]
    return out


# ------------------------------------------------------------------ the driver
class Game:
    def __init__(self, rom, outdir):
        self.rom = rom
        self.sym = parse_symbols(rom.replace('.gbc', '.map'))
        self.G = self.sym['_G']
        nxt = min(a for n, a in self.sym.items() if a > self.G and n.startswith('_') and not n.startswith('__'))
        assert nxt - self.G == GSIZE, f'GameState layout mismatch: _G is {nxt - self.G} bytes, game.h says {GSIZE}'
        self.noi = {}                   # full symbol list (bank << 16 | addr), for hooks on any function
        if os.path.exists(rom.replace('.gbc', '.noi')):
            for line in open(rom.replace('.gbc', '.noi')):
                m = re.match(r'DEF _(\w+) 0x([0-9A-Fa-f]+)', line)
                if m:
                    self.noi[m.group(1)] = int(m.group(2), 16)
        self.WAIT_LO = self.sym['_wait_vbl']
        self.WAIT_HI = self.WAIT_LO + 0x30
        self.kinds = {m: map_kind(m) for m in (MAP_TOWN, MAP_HOUSE1, MAP_HOUSE2, MAP_BARN, MAP_COOP, MAP_MINE, MAP_MINE_A,
                                               MAP_MINE_B, MAP_MINE_C, MAP_BEACH, CONST['MAP_WOODS'], CONST['MAP_ISLAND'],
                                               CONST['MAP_MARKET'], CONST['MAP_PASTURE'], CONST['MAP_QUARRY'])}
        self.tk = self.kinds[MAP_TOWN]
        self.outdir = outdir
        self.pb = None
        self.boot(fresh=True)

    # ---- emulator lifetime
    def boot(self, fresh=False):
        if self.pb:
            self.pb.stop(save=True)
        ram = self.rom + '.ram'
        if fresh and os.path.exists(ram):
            os.remove(ram)
        self.pb = PyBoy(self.rom, window='null', cgb=True, sound_emulated=False)
        self.pb.set_emulation_speed(0)
        self.pcs = deque(maxlen=HANG_FRAMES)
        self.since_wait = 0
        self.frames = 0
        self.vbl_calls = 0
        self.morning_shot = None
        self.morning_find = None
        self.pb.hook_register(0, self.WAIT_LO, self._on_wait, None)

    def _on_wait(self, _ctx):
        self.vbl_calls += 1
        self.since_wait = 0

    def stop(self):
        if self.pb:
            self.pb.stop(save=True)
            self.pb = None

    def snapshot(self):
        f = io.BytesIO()
        self.pb.save_state(f)
        return f.getvalue()

    def restore(self, data):
        self.pb.load_state(io.BytesIO(data))
        self.pcs.clear()
        self.since_wait = 0

    # ---- frames, hang detection
    def tick(self, n=1):
        for _ in range(n):
            self.pb.tick()
            self.frames += 1
            pc = self.pb.register_file.PC
            self.since_wait += 1
            if not (self.WAIT_LO <= pc < self.WAIT_HI):
                self.pcs.append(pc)
                if self.since_wait >= HANG_FRAMES:
                    hot, cnt = Counter(self.pcs).most_common(1)[0]
                    self.shot('HANG')
                    raise Hang(f'no wait_vbl_done for {self.since_wait} frames; hottest PC {hot:04X} '
                               f'({self.symbolize(hot)}) x{cnt}')

    def symbolize(self, pc):
        best = None
        for n, a in self.sym.items():
            if a <= pc and (best is None or a > best[1]) and (pc < 0x4000) == (a < 0x4000):
                best = (n, a)
        return f'{best[0]}+{pc - best[1]:X}' if best else '?'

    def press(self, btn, hold=4, after=6):
        self.pb.button_press(btn)
        self.tick(hold)
        self.pb.button_release(btn)
        self.tick(after)

    def hold(self, btn, n):
        self.pb.button_press(btn)
        self.tick(n)
        self.pb.button_release(btn)
        self.tick(2)

    # ---- memory
    def addr(self, name, i=0):
        return self.G + OFF[name] + i

    def u8(self, name, i=0):
        if name in ('cell', 'a1', 'a2'):           # old 32x32 index into the home valley
            return getattr(self, name)(i % 32, i // 32)
        return self.pb.memory[self.addr(name, i)]

    def u16(self, name, i=0):
        if name == 'plots':                         # the home valley's 16 old plots as the old bitmask
            return sum(1 << p for p in range(16) if self.plot_owned(((p >> 2) + 2) * 8 + (p & 3) + 2))
        a = self.addr(name, i)
        return self.pb.memory[a] | (self.pb.memory[a + 1] << 8)

    def u32(self, name, i=0):
        a = self.addr(name, i)
        return sum(self.pb.memory[a + k] << (8 * k) for k in range(4))

    def w8(self, name, v, i=0):
        if name in ('cell', 'a1', 'a2'):
            x, y = i % 32 + FO, i // 32 + FO
            self.pb.memory[WB_A2 if name == 'a2' else WB_LIVE, (0xB000 if name == 'a1' else 0xA000) + y * WW + x] = v & 0xFF
            return
        self.pb.memory[self.addr(name, i)] = v & 0xFF

    def w16(self, name, v, i=0):
        a = self.addr(name, i)
        self.pb.memory[a] = v & 0xFF
        self.pb.memory[a + 1] = (v >> 8) & 0xFF

    def w32(self, name, v, i=0):
        a = self.addr(name, i)
        for k in range(4):
            self.pb.memory[a + k] = (v >> (8 * k)) & 0xFF

    def sbuf(self):
        """the UI text buffer (ui.c sbuf) as a string"""
        b = bytes(self.pb.memory[self.sym['_sbuf'] + k] for k in range(96))
        return b.split(b'\0')[0].decode('latin1')

    def sym8(self, name, i=0):
        return self.pb.memory[self.sym[name] + i]

    def symw8(self, name, v, i=0):
        self.pb.memory[self.sym[name] + i] = v

    # ---- the farm world (SRAM). w*: real cells 0..63; the rest: the home-valley frame (real - FO)
    def wcell(self, x, y):
        return self.pb.memory[WB_LIVE, 0xA000 + y * WW + x]

    def wa1(self, x, y):
        return self.pb.memory[WB_LIVE, 0xB000 + y * WW + x]

    def wa2(self, x, y):
        return self.pb.memory[WB_A2, 0xA000 + y * WW + x]

    def wset(self, x, y, c, a1=0, a2=0):
        self.pb.memory[WB_LIVE, 0xA000 + y * WW + x] = c
        self.pb.memory[WB_LIVE, 0xB000 + y * WW + x] = a1
        self.pb.memory[WB_A2, 0xA000 + y * WW + x] = a2

    def world(self):
        """the whole live world: cell + a1 (bank WB_LIVE) and a2 (bank WB_A2) as bytes"""
        m = self.pb.memory
        return bytes(m[WB_LIVE, 0xA000:0xBFFF]) + bytes([m[WB_LIVE, 0xBFFF]]) + bytes(m[WB_A2, 0xA000:0xB000])

    def cell(self, x, y):
        return self.wcell(x + FO, y + FO)

    def a1(self, x, y):
        return self.wa1(x + FO, y + FO)

    def a2(self, x, y):
        return self.wa2(x + FO, y + FO)

    def setcell(self, x, y, c, a1=0, a2=0):
        self.wset(x + FO, y + FO, c, a1, a2)

    def seta2(self, x, y, v):
        self.pb.memory[WB_A2, 0xA000 + (y + FO) * WW + x + FO] = v

    def plot_owned(self, p):
        """new plot index 0..63 (8x8 grid of 8x8 cells)"""
        return (self.u8('plots', p >> 3) >> (p & 7)) & 1

    def plots_n(self):
        return sum(bin(self.u8('plots', k)).count('1') for k in range(8))

    def inv(self, it, q=None):
        if q is None:
            return sum(self.u8('inv', it * 3 + k) for k in range(3))
        return self.u8('inv', it * 3 + q)

    def bld(self, k):
        b = [self.u8('bld', k * BLD_SIZE + j) for j in range(4)]
        if b[0] != 0xFF:
            b[1] -= FO; b[2] -= FO
        return tuple(b)

    def bld_find(self, t):
        for k in range(MAX_BLD):
            if self.bld(k)[0] == t:
                return k
        return None

    def anim(self, k):
        b = [self.u8('anim', k * ANIM_SIZE + j) for j in range(ANIM_SIZE)]
        o = ANIM_OFF
        return dict(kind=b[o['kind']], home=b[o['home']], happy=b[o['happy']], age=b[o['age']], flags=b[o['flags']],
                    name=bytes(b[o['name']:o['name'] + 6]).split(b'\0')[0].decode('latin1'))

    def set_anim(self, k, kind, home, happy=200, age=30, flags=0, name='Test'):
        base = k * ANIM_SIZE
        o = ANIM_OFF
        for f, v in (('kind', kind), ('home', home), ('happy', happy), ('age', age), ('flags', flags)):
            self.w8('anim', v, base + o[f])
        nm = name.encode()[:5] + b'\0'
        for j in range(6):
            self.w8('anim', nm[j] if j < len(nm) else 0, base + o['name'] + j)

    def animals(self):
        return [(k, a) for k in range(MAX_ANIMALS) for a in [self.anim(k)] if a['kind'] != 0xFF]

    def seeds(self, crop, rar=0):
        return self.u8('seeds', crop * NUM_RARITY + rar)

    def set_seeds(self, crop, n, rar=0):
        self.w8('seeds', n, crop * NUM_RARITY + rar)

    def furn(self):
        """[(slot, kind, x, y)] of placed furniture"""
        return [(k, self.u8('furn', k * 3), self.u8('furn', k * 3 + 1), self.u8('furn', k * 3 + 2))
                for k in range(MAX_FURN) if self.u8('furn', k * 3)]

    def furn_at(self, x, y):
        for k, kind, fx, fy in self.furn():
            w, h = FURN_WH[kind]
            if fx <= x < fx + w and fy <= y < fy + h:
                return (k, kind, fx, fy)
        return None

    def real_pos(self):
        return self.u8('px'), self.u8('py')

    def pos(self):
        """position; on the farm in the home-valley frame"""
        o = FO if self.u8('scene') == SCENE_FARM else 0
        return self.u8('px') - o, self.u8('py') - o

    def owned(self, x, y):
        x, y = x + FO, y + FO
        return (self.u8('plots', y >> 3) >> (x >> 3)) & 1 if 0 <= x < WW and 0 <= y < WH else 0

    def rich(self):
        self.w32('gold', 900000)
        self.w16('wood', 900)
        self.w16('stone', 900)

    # ---- screen
    def ui_open(self):
        return self.pb.memory[0xFF4A] < 100    # WY: 0 while a menu/dialog is up, 136 in the field

    def wtile(self, x, y):
        """window tile id at (x, y), read from VRAM bank 0 directly: the tilemap API follows VBK, and the
        game leaves VBK=1 for a moment in every ui_char, so a frame can end while attributes are mapped"""
        base = 0x9C00 if self.pb.memory[0xFF40] & 0x40 else 0x9800
        return self.pb.memory[0, base + y * 32 + x]

    def cursor_at(self, x, y):
        return self.wtile(x, y) in (GL_CURSOR, GL_CURSOR2)

    def screen_text_rows(self):
        return None

    def shot(self, name):
        os.makedirs(self.outdir, exist_ok=True)
        n = len([f for f in os.listdir(self.outdir) if f.endswith('.png')])
        img = self.pb.screen.image.copy()
        img = img.resize((img.width * 3, img.height * 3), 0)
        p = os.path.join(self.outdir, f'{n:02d}_{name}.png')
        img.save(p)
        return p

    # ---- UI helpers
    def settle(self, n=8):
        self.tick(n)

    def name_entry_open(self):
        return self.ui_open() and self.wtile(10, 13) == GL_KEY_END and self.wtile(8, 13) == GL_KEY_DEL

    def dismiss(self, maxn=80, btn='b'):
        """close every dialog/menu with B (START too if B alone doesn't do it)"""
        n = 0
        self.tick(3)
        while self.ui_open() and n < maxn:
            # the name keyboard (a new baby animal...): B would erase the name, START keeps it
            self.press('start' if n % 10 == 9 or self.name_entry_open() else btn)
            n += 1
            if not self.ui_open():
                self.tick(6)
        if self.ui_open():
            raise Fail('UI did not close after %d presses' % n)
        return n

    def wait_field(self, maxf=900):
        for _ in range(maxf):
            if not self.ui_open():
                self.tick(4)
                if not self.ui_open():
                    return
            self.tick(1)
        raise Fail('never returned to the field')

    def menu_rows(self, x, y, n):
        step = 2 if n * 2 + 1 + y <= 17 else 1
        return [(x + 1, y + 1 + i * step) for i in range(n)]

    def menu_sel(self, x, y, n):
        for i, (cx, cy) in enumerate(self.menu_rows(x, y, n)):
            if self.cursor_at(cx, cy):
                return i
        return None

    def menu_live(self, x, y, n, frames=36):
        """index of a menu that is actually waiting for input: its cursor blinks (GL_CURSOR <-> GL_CURSOR2
        every 16 frames).  A stale cursor left on screen under a dialog never changes."""
        seen = {}
        f = 0
        while f < frames or (seen and f < frames + 48):    # a cursor showed up late: watch it blink
            f += 1
            for i, (cx, cy) in enumerate(self.menu_rows(x, y, n)):
                t = self.wtile(cx, cy)
                if t in (GL_CURSOR, GL_CURSOR2):
                    seen.setdefault(i, set()).add(t)
            for i, ts in seen.items():
                if len(ts) == 2:
                    return i
            self.tick(1)
        return None

    def wait_menu(self, x, y, n, advance='a', maxp=60):
        """advance dialogs (pressing `advance`) until the menu at x,y with n items is live"""
        for _ in range(maxp):
            if self.menu_live(x, y, n) is not None:
                return
            if self.ui_open():
                self.press(advance)
        self.shot('menu_timeout')
        raise Fail(f'menu at {x},{y} n={n} never appeared')

    def choose(self, x, y, n, idx, advance='a'):
        self.wait_menu(x, y, n, advance)
        for _ in range(n + 2):
            s = self.menu_sel(x, y, n)
            if s == idx:
                break
            if s is None:
                self.tick(4)
                continue
            self.press('down' if (idx - s) % n <= n // 2 else 'up', 2, 4)
        if self.menu_sel(x, y, n) != idx:
            self.shot('choose_fail')
            raise Fail(f'could not move cursor to {idx} in menu at {x},{y}')
        self.press('a')

    def yesno(self, yes=True, advance=None):
        self.choose(14, YN_Y, 2, 0 if yes else 1, advance or ('a' if yes else 'b'))

    def quantity(self, q):
        """ui_quantity: starts at 1; UP adds one"""
        self.tick(4)
        for _ in range(q - 1):
            self.press('up', 2, 3)
        self.press('a')

    # ---- walking
    def actors(self):
        """[(kind, id, x, y)] from actors.c: act_kind is global, act_id/ax/ay are the statics after it"""
        b = self.sym['_act_kind']
        m = self.pb.memory
        n = MAX_ACT
        o = FO if self.u8('scene') == SCENE_FARM else 0
        return [(m[b + k], m[b + n + k], m[b + 2 * n + k] - o, m[b + 3 * n + k] - o) for k in range(n) if m[b + k]]

    def shoo_pet(self):
        """test-only: put the pet on the player's own cell so it stops blocking a corridor"""
        b, n = self.sym['_act_kind'], MAX_ACT
        for k in range(n):
            if self.pb.memory[b + k] == K_PET:
                self.pb.memory[b + 2 * n + k], self.pb.memory[b + 3 * n + k] = self.real_pos()

    def park_pet(self):
        """test-only: take the pet off the field until the next scene change (it follows you into build spots)"""
        b = self.sym['_act_kind']
        for k in range(MAX_ACT):
            if self.pb.memory[b + k] == K_PET:
                self.pb.memory[b + k] = 0

    def actor_at(self, x, y):
        for a in self.actors():
            if a[2:] == (x, y):
                return a
        return None

    def walkable(self, x, y, scene=None):
        scene = self.u8('scene') if scene is None else scene
        if scene == SCENE_FARM:
            if not (-FO <= x < WW - FO and -FO <= y < WH - FO):
                return False
        elif not (0 <= x < 32 and 0 <= y < 32):
            return False
        if self.actor_at(x, y):
            return False
        if scene in (MAP_BARN, MAP_COOP):               # barn.c: no furniture in barns
            return bool(self.kinds[scene][y * 32 + x] & K_WALK)
        if scene in (MAP_HOUSE1, MAP_HOUSE2):
            if not self.kinds[scene][y * 32 + x] & K_WALK:
                return False
            f = self.furn_at(x, y)
            return f is None or f[1] == FK_RUG
        if scene != SCENE_FARM:
            if not self.kinds.get(scene, self.tk)[y * 32 + x] & K_WALK:
                return False
            if scene == MAP_TOWN and y >= MK_GATE_Y and not self.u8('mk_flags') & MK_OPEN:
                return False                     # Market Street: closed until bought (main.c walkable)
            if scene in (MAP_MINE_A, MAP_MINE_B, MAP_MINE_C) and (self.rock_at(x, y) or (x, y) == self.ladder()):
                return False                     # (the open ladder is walkable, but stepping on it goes down)
            if scene == MAP_MINE and (x, y) == MINE_HOLE:
                return False                     # the shaft ladder down to floor 1
            if scene == CONST['MAP_WOODS']:
                for k, (sx, sy) in enumerate(zip(_ints(_table(_areas_c, 'wd_x')), _ints(_table(_areas_c, 'wd_y')))):
                    if (sx, sy) == (x, y) and wood_item(k, self.u8('area_stamp'), self.u8('area_taken', 4), self.u8('season')) is not None:
                        return False
            if scene == MAP_BEACH:
                for k, (sx, sy) in enumerate(BEACH_SPOTS[:6]):
                    if (sx, sy) == (x, y) and beach_item(k, self.u8('beach_stamp'), self.u8('beach_taken')) is not None:
                        return False
            return True
        c = self.cell(x, y)
        if c == C_ROAD:
            return True
        if not self.owned(x, y):
            return False
        if c == C_DECOR:
            return self.a1(x, y) in DECOR_WALK
        return c in (C_GRASS, C_GRASS2, C_WILDFLOWER, C_DIRT, C_SOIL, C_SAND)

    # ---- the mine (areas.c globals)
    def rock_at(self, x, y):
        if x >= 16 or y >= 16:
            return False
        return bool(self.sym8('_mn_rocks', (y << 1) | (x >> 3)) & (1 << (x & 7)))

    def rocks(self):
        return [(x, y) for y in range(16) for x in range(16) if self.rock_at(x, y)]

    def ladder(self):
        return self.sym8('_mn_lad_x'), self.sym8('_mn_lad_y')

    def face(self, d):
        for _ in range(8):
            if self.u8('facing') == DIRS[d][2]:
                return
            if self.ui_open():
                self.dismiss()
            self.press(d, 2, 4)
            self.tick(4)
        raise Fail(f'could not face {d}')

    def step(self, d):
        x, y = self.pos()
        self.face(d)
        self.pb.button_press(d)
        self.tick(2)
        self.pb.button_release(d)
        for _ in range(24):
            self.tick(1)
            if self.pos() != (x, y):
                break
        self.tick(4)
        return self.pos() != (x, y)

    def go(self, tx, ty, blocked_ok=False):
        blocked = set()
        retries = 0
        for _ in range(400):
            if self.ui_open():
                self.dismiss()
            x, y = self.pos()
            if (x, y) == (tx, ty):
                return True
            prev = {(x, y): None}
            q = deque([(x, y)])
            while q:
                c = q.popleft()
                if c == (tx, ty):
                    break
                for d, (dx, dy, _) in DIRS.items():
                    n = (c[0] + dx, c[1] + dy)
                    if n in prev or n in blocked or not self.walkable(*n):
                        continue
                    prev[n] = (c, d)
                    q.append(n)
            if (tx, ty) not in prev:
                retries += 1
                if retries < 16:                 # maybe an actor is in the way; let them wander off
                    blocked.clear()
                    if retries >= 3:             # the pet loves to sit in one-tile corridors: lift it
                        self.shoo_pet()
                    if retries >= 6:             # still there (it follows you back in): off the field for now
                        self.park_pet()
                    self.tick(90)
                    continue
                self.shot('no_path')
                raise Fail(f'go: no path from {(x, y)} to {(tx, ty)}')
            c = (tx, ty)
            while prev[c][0] != (x, y):
                c = prev[c][0]
            d = prev[c][1]
            if not self.step(d):
                blocked.add(c)      # an actor is standing there
        self.shot('go_stuck')
        raise Fail(f'go: gave up reaching {(tx, ty)} from {self.pos()}')

    def go_face(self, tx, ty, d):
        """stand on (tx,ty) facing d"""
        self.go(tx, ty)
        self.face(d)

    def act(self):
        self.press('a')

    def use_on(self, tx, ty, tool=None):
        """stand next to cell (tx,ty), face it, press A"""
        for d, (dx, dy, _) in DIRS.items():
            sx, sy = tx - dx, ty - dy
            if self.walkable(sx, sy):
                break
        else:
            raise Fail(f'no free side next to {(tx, ty)}')
        self.go(sx, sy)
        self.face(d)
        if tool is not None:
            self.w8('tool', tool)
        self.act()

    # ---- the house (interiors: A at the door goes inside; the bed, TV and diary are in there)
    def wait_scene(self, want, maxf=900):
        for _ in range(maxf // 10):
            if self.u8('scene') in want:
                break
            self.tick(10)
        self.tick(30)
        for _ in range(90):                 # a first-visit line shows up after the fade-in
            if self.ui_open():
                break
            self.tick(1)
        self.dismiss()
        self.wait_field()
        if self.u8('scene') not in want:
            raise Fail(f'expected scene {want}, still in {self.u8("scene")}')

    def in_house(self):
        return self.u8('scene') in (MAP_HOUSE1, MAP_HOUSE2)

    def enter_house(self):
        if self.in_house():
            return
        if self.u8('scene') != SCENE_FARM:
            raise Fail('enter_house: not on the farm')
        _, hx, hy, _ = self.bld(self.bld_find(B_HOUSE))
        self.go_face(hx + 1, hy + 3, 'up')
        self.act()
        self.wait_scene((MAP_HOUSE1, MAP_HOUSE2))

    def leave_house(self):
        wx, wy = map_warps(self.u8('scene'))[0][:2]
        self.go_face(wx, wy - 1, 'down')
        self.press('down', 4, 10)
        self.wait_scene((SCENE_FARM,))

    def furn_spot(self, kind):
        """(x, y, dir) of a free floor cell next to the first piece of furniture of `kind`, facing it"""
        for k, kd, fx, fy in self.furn():
            if kd != kind:
                continue
            w, h = FURN_WH[kd]
            cells = [(fx + i, fy + j) for i in range(w) for j in range(h)]
            for (cx, cy) in cells:
                for d, (dx, dy, _) in DIRS.items():
                    sx, sy = cx - dx, cy - dy
                    if (sx, sy) not in cells and self.walkable(sx, sy):
                        return sx, sy, d
        raise Fail(f'no free spot next to furniture kind {kind}')

    def sleep(self, stay=False):
        """go inside, sleep in the bed; returns after the morning report (back out on the farm unless stay)"""
        self.enter_house()
        x, y, d = self.furn_spot(FK_BED)
        self.go_face(x, y, d)
        day = self.u8('day')
        self.act()
        self.yesno(True)
        for _ in range(60):
            self.tick(20)
            if self.u8('day') != day:
                break
        self.after_night()
        if not stay:
            self.leave_house()

    def after_night(self):
        """the night has run: wait for the morning report, read it, back to the field"""
        for _ in range(400):
            if self.ui_open():
                break
            self.tick(1)
        self.tick(30)
        if self.morning_shot:
            self.shot(self.morning_shot)
            self.morning_shot = None
        if self.morning_find:            # (text, shot): page through the report to the line containing text
            want, name = self.morning_find
            self.morning_find = None
            for _ in range(60):
                if want in self.sbuf():
                    self.tick(40)
                    self.shot(name)
                    break
                if not self.ui_open():
                    break
                self.press('a'); self.tick(4)
        self.dismiss()
        self.wait_field()


# ------------------------------------------------------------------ scenarios
SCEN = {}


def scenario(name, base='farm'):
    def deco(fn):
        SCEN[name] = (fn, base)
        return fn
    return deco


class Ctx:
    def __init__(self, g, name):
        self.g, self.name, self.fails, self.notes = g, name, [], []

    def check(self, cond, msg):
        if not cond:
            self.fails.append(msg)
            self.g.shot('FAIL')
        return cond

    def eq(self, got, want, msg):
        return self.check(got == want, f'{msg}: got {got!r}, want {want!r}')

    def require(self, cond, msg):
        if not cond:
            self.g.shot('FAIL')
            raise Fail(msg)

    def note(self, msg):
        self.notes.append(msg)


def title_start(g):
    g.tick(200)
    for _ in range(40):
        g.press('start')
        g.tick(30)
        if g.menu_sel(1, 9, 2) is not None:
            # the game seeds its RNG from the DIV register at PRESS START, so any change in boot timing (new art,
            # moved code) used to reshuffle the farm's debris and break path-walking scenarios. Pin the seed.
            g.w8('rng_lo', 117); g.w8('rng_hi', 85)
            return
    raise Fail('PRESS START never reached the slot menu')


def new_game(g, slot_row=0, name_farm=False):
    """title -> PRESS START -> pick the farm (row 0) -> intro"""
    title_start(g)
    g.choose(1, 9, 2, slot_row)
    g.tick(30)
    if g.u16('magic') == 0x5F17 and g.ui_open() and g.menu_sel(8, 4, 3) is not None:
        g.choose(8, 4, 3, 1)           # Start Over
        g.yesno(True)
    # Grandpa's two lines, then the farm-name question
    g.yesno(name_farm, advance='a')
    if name_farm:
        g.tick(30)
        g.press('start')
    g.tick(20)
    g.dismiss()
    g.wait_field()
    g.w8('rng_lo', 149); g.w8('rng_hi', 209)   # world_new reseeds from DIV + the frame counter: pin that too


@scenario('intro', base=None)
def s_intro(c, g):
    g.tick(240)
    g.shot('title')
    title_start(g)
    g.wait_menu(1, 9, 2)
    g.shot('slots')
    g.choose(1, 9, 2, 0)
    g.tick(140)                         # world_new draws the whole 64x64 valley first (~40 frames)
    g.shot('grandpa')
    c.eq(g.u8('scene'), SCENE_FARM, 'scene after new game')
    g.yesno(True, advance='a')          # name the farm
    g.tick(40)
    g.shot('name_entry')
    g.press('start')                    # keep the default name "Stilwell"
    g.tick(30)
    g.shot('after_name')
    g.dismiss()
    g.wait_field()
    g.shot('field')
    c.eq(g.pos(), (10, 12), 'start position')
    c.eq(g.u32('gold'), 500, 'start gold')
    c.eq(g.u8('day'), 1, 'day')
    c.eq(g.u16('time'), 360, 'time (6am, clock not running during intro)')
    c.eq(g.u16('plots'), 0x0660, 'plots')
    c.eq(bytes(g.u8('farm_name', i) for i in range(8)).split(b'\0')[0], b'Stilwell', 'farm name')
    c.eq(g.seeds(1), 6, 'starting radish seeds')
    for k, (t, x, y) in enumerate([(B_HOUSE, 9, 9), (B_COOP, 17, 9), (B_BARN, 20, 9), (B_GREENHOUSE, 20, 18)]):
        c.eq(g.bld(k)[:3], (t, x, y), f'building {k}')
    c.eq(g.cell(12, 11), C_DECOR, 'shipping bin cell')
    # the HUD clock starts running once in the field
    g.tick(800)
    c.check(g.u16('time') > 360, 'clock runs in the field')




# ------------------------------------------------------------------ shared helpers
def realtor_list(g):
    """the land office's plots in menu order: up to 10 for sale, those bordering your land first"""
    def near(p):
        x, y = p & 7, p >> 3
        return any(0 <= x + dx < 8 and 0 <= y + dy < 8 and g.plot_owned((y + dy) * 8 + x + dx)
                   for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)))
    free = [p for p in range(64) if not g.plot_owned(p)]
    return ([p for p in free if near(p)] + [p for p in free if not near(p)])[:10]


def to_town(g):
    g.go(15, WH - FO - 2)
    g.step('down')                      # arriving on the farm's last row (y=63) facing down warps
    if g.u8('scene') != MAP_TOWN:
        g.face('down')
        g.press('down', 4, 10)
    for _ in range(40):
        g.tick(10)
        if g.u8('scene') == MAP_TOWN:
            break
    g.tick(40)
    g.dismiss()
    g.wait_field()
    if g.u8('scene') != MAP_TOWN:
        raise Fail('did not reach town')


def to_farm(g):
    g.go(15, 1)
    g.step('up')
    for _ in range(40):
        g.tick(10)
        if g.u8('scene') == SCENE_FARM:
            break
    g.tick(40)
    g.dismiss()
    g.wait_field()
    if g.u8('scene') != SCENE_FARM:
        raise Fail('did not get back to the farm')


def open_start(g, idx):
    g.press('start')
    g.choose(11, 0, 8, idx)


def build_pick(g, cat, item):
    """START > BUILD > category > item, through the real menus"""
    open_start(g, 3)
    g.choose(0, 0, 6, cat)
    n = CAT_N[cat]
    if n > 11:                          # lists sit under the cost panel (y=4): 10 items + "More..."
        g.choose(0, 4, 11, min(item, 10))
        if item >= 10:
            g.choose(0, 4, n - 10, item - 10)
    else:
        g.choose(0, 4, n, item)
    g.tick(10)
    g.dismiss(btn='a')
    return g.sym8('_building')


def build_poke(g, cat, sel):
    g.w8('build_cat', cat)
    g.w8('build_sel', sel)
    g.symw8('_building', 1)
    g.tick(2)


def clear_area(g, x0, y0, x1, y1, c=C_GRASS, force=False):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if force or g.cell(x, y) not in (C_STRUCT, C_DECOR):
                g.setcell(x, y, c)


def make_cleared(g):
    """farm base + two cleared work areas, redrawn by a trip to town"""
    clear_area(g, 8, 15, 14, 23)
    clear_area(g, 17, 13, 23, 17)
    clear_area(g, 17, 11, 19, 12)
    clear_area(g, 20, 12, 23, 12)
    to_town(g)
    to_farm(g)
    g.go(12, 14)


def till_plant_water(g, x, y, crop=1):
    """stand above (x,y) facing down; hoe, seed, water it"""
    g.go_face(x, y - 1, 'down')
    g.w8('tool', T_HOE); g.act(); g.tick(10); g.dismiss()
    g.w8('tool', T_SEEDS); g.w8('seed_sel', crop); g.act(); g.tick(10); g.dismiss()
    g.w8('tool', T_CAN); g.act(); g.tick(10); g.dismiss()


# ------------------------------------------------------------------ scenarios
@scenario('crops')
def s_crops(c, g):
    g.go_face(10, 13, 'down')
    g.w8('tool', T_HOE)
    e0 = g.u8('energy')
    g.act(); g.tick(20)
    g.shot('tilled_tip')
    g.dismiss()
    c.eq(g.cell(10, 14), C_SOIL, 'hoe tills grass')
    c.eq(g.u8('energy'), e0 - 2, 'hoe costs 2 energy')
    g.w8('tool', T_SEEDS); g.w8('seed_sel', 1)
    g.act(); g.tick(10); g.dismiss()
    c.eq(g.a1(10, 14) & 0x1F, 1, 'radish planted')
    c.eq(g.seeds(1), 5, 'seed used')
    g.w8('tool', T_CAN)
    w0 = g.u8('water')
    g.act(); g.tick(10); g.dismiss()
    c.check(g.a1(10, 14) & 0x80, 'soil watered')
    c.eq(g.u8('water'), w0 - 1, 'can water used')
    g.shot('planted_watered')
    # out-of-season seed must be refused (the greenhouse is still a ruin)
    g.go_face(11, 13, 'down')
    g.w8('tool', T_HOE); g.act(); g.tick(10); g.dismiss()
    g.set_seeds(5, 3)                   # corn = summer
    g.w8('tool', T_SEEDS); g.w8('seed_sel', 5)
    g.act(); g.tick(20)
    g.shot('corn_in_spring')
    g.dismiss()
    c.eq(g.a1(11, 14) & 0x1F, 0, 'summer corn cannot be planted in spring without a working greenhouse')
    # hand: check crop status
    g.go_face(10, 13, 'down')
    g.w8('tool', T_HAND); g.act(); g.tick(6)
    g.shot('crop_status')
    g.tick(100)
    g.sleep()
    g.shot('day2')
    c.eq(g.u8('day'), 2, 'slept to day 2')
    c.check(360 <= g.u16('time') < 400, f"morning time ({g.u16('time')}; the walk out of the house may tick the clock)")
    c.eq(g.a2(10, 14), 1, 'watered radish grew one day')
    c.eq(g.a1(10, 14) & 0x80, 0 if g.u8('weather') not in (1, 3) else 0x80, 'soil dries overnight (unless rain)')
    # fast-forward growth: 3 of 4 days grown, water, sleep -> ripe
    g.w8('a2', 3, 14 * 32 + 10)
    g.go_face(10, 13, 'down')
    g.w8('tool', T_CAN); g.act(); g.tick(10); g.dismiss()
    g.sleep()
    c.eq(g.a2(10, 14), 4, 'radish ripe after 4 days')
    g.go_face(10, 13, 'down')
    g.shot('ripe')
    g.w8('tool', T_HAND); g.act(); g.tick(10)
    g.shot('harvest')
    g.dismiss()
    c.eq(g.inv(I_RADISH), 1, 'harvested a radish')
    c.eq(g.a1(10, 14) & 0x1F, 0, 'soil empty after harvest')
    c.eq(g.u16('stat_harvest'), 1, 'harvest stat')
    c.check(g.u32('goals') & 1, 'first-harvest goal reached')
    c.check(g.u32('mail') & 1, 'first-harvest letter waiting')
    q = max(k for k in range(3) if g.inv(I_RADISH, k)) if g.inv(I_RADISH) else 0
    val = ITEM_VALUE(I_RADISH, q)
    # ship it
    g.go_face(12, 12, 'up')
    g.act()
    g.yesno(True)
    g.tick(10); g.dismiss()
    c.eq(g.u32('ship_value'), val, 'shipping bin value')
    c.eq(g.inv(I_RADISH), 0, 'bin took the goods')
    gold = g.u32('gold')
    g.sleep()
    c.eq(g.u32('gold'), gold + val, 'shipping paid overnight')
    c.eq(g.u32('ship_value'), 0, 'ship value reset')
    g.shot('paid')


@scenario('tool_tiers', base='cleared')
def s_tool_tiers(c, g):
    want = {0: 1, 1: 3, 2: 5, 3: 9}
    for t, (x, y) in zip(range(4), [(9, 16), (13, 16), (10, 19), (11, 21)]):
        g.w8('tier', t, TT_HOE)
        before = sum(g.cell(i % 32, i // 32) == C_SOIL for i in range(1024))
        g.go_face(x, y - 1, 'down')
        g.w8('tool', T_HOE)
        g.act(); g.tick(20); g.dismiss()
        after = sum(g.cell(i % 32, i // 32) == C_SOIL for i in range(1024))
        c.eq(after - before, want[t], f'hoe tier {t} area')
        c.check(g.cell(*g.pos()) != C_SOIL or t < 3, f'tier {t} hoe should not till under the player')
        g.shot(f'hoe_tier{t}')
    # can: tier 3 on the 3x3 patch
    g.w8('tier', 3, TT_CAN)
    g.w8('water', 50)
    g.go_face(11, 20, 'down')
    g.w8('tool', T_CAN); g.act(); g.tick(20); g.dismiss()
    wet = sum(1 for yy in range(21, 24) for xx in range(10, 13) if g.cell(xx, yy) == C_SOIL and g.a1(xx, yy) & 0x80)
    c.eq(wet, 9, 'gold can waters the 3x3 patch in front')
    g.shot('can_tier3')


@scenario('buy_plot', base='cleared')
def s_buy_plot(c, g):
    g.w32('gold', 5000)
    # plot 13 South Pasture (x 8..15, y 24..31) is south of plot 9; the rope sign is at x=12, y=24
    g.go_face(12, 23, 'down')
    g.shot('rope_sign')
    g.act()
    g.wait_menu(14, YN_Y, 2)
    g.shot('buy_prompt')
    g.yesno(True)
    g.tick(30)
    g.shot('bought')
    g.dismiss()
    c.check(g.u16('plots') & (1 << 13), 'plot 13 owned')
    c.eq(g.u32('gold'), 5000 - PLOT_PRICE[OLD_PLOT(13)], 'paid the South Pasture price')
    c.check(g.u32('goals') & 8, 'more-land goal')
    # walk into the new land
    for (x, y) in [(12, 24), (11, 26), (10, 28)]:
        if g.walkable(x, y):
            g.go(x, y)
            break
    c.check(g.pos()[1] >= 24, f'walked into plot 13 ({g.pos()})')
    g.shot('in_plot')
    # an unowned plot with no money: refused
    g.w32('gold', 10)
    g.go_face(8, 16, 'left') if g.walkable(8, 16) else None
    g.act(); g.tick(10)
    if g.ui_open() and g.menu_sel(14, YN_Y, 2) is not None:
        g.yesno(True)
    g.tick(10)
    g.dismiss()
    c.check(not (g.u16('plots') & (1 << 4)), 'cannot buy a plot without the gold')
    c.eq(g.u32('gold'), 10, 'gold unchanged when the purchase fails')


@scenario('build', base='cleared')
def s_build(c, g):
    g.rich()
    # real menu: FARM > Scarecrow
    g.go_face(9, 16, 'down')
    c.eq(build_pick(g, 0, 3), 1, 'build mode on after picking from the menu')
    g.shot('build_cursor')
    gold = g.u32('gold')
    g.act(); g.tick(10)
    c.eq((g.cell(9, 17), g.a1(9, 17)), (C_DECOR, DK_SCARECROW), 'scarecrow placed')
    c.eq(g.u32('gold'), gold - DECOR_GOLD[DK_SCARECROW], 'scarecrow cost')
    g.act(); g.tick(10)
    c.eq(g.u32('gold'), gold - DECOR_GOLD[DK_SCARECROW], 'no charge for a blocked spot')
    g.press('b'); g.tick(6)
    c.eq(g.sym8('_building'), 0, 'B leaves build mode')
    # fence autotile: a horizontal row of three
    build_poke(g, 2, 4)                 # PATHS > Wood Fence
    for x in (10, 11, 12):
        g.go_face(x, 19, 'up'); g.act(); g.tick(6)
    c.eq([g.a1(x, 18) for x in (10, 11, 12)], [DK_FENCE] * 3, 'fences placed')
    c.eq([g.a2(x, 18) for x in (10, 11, 12)], [1, 1, 1], 'fence row autotiles horizontal')
    g.shot('fences')
    # paths
    build_poke(g, 2, 0)
    g.go_face(13, 16, 'down'); g.act(); g.tick(6)
    c.eq(g.a1(13, 17), DK_PATH, 'stone path')
    c.check(g.walkable(13, 17), 'path walkable')
    # pond + bridge + fill
    build_poke(g, 3, 0)
    for x in (9, 10, 11):
        g.go_face(x, 21, 'down'); g.act(); g.tick(6)
    c.eq([g.cell(x, 22) for x in (9, 10, 11)], [C_WATER] * 3, 'pond dug')
    build_poke(g, 3, 1)
    g.go_face(10, 21, 'down'); g.act(); g.tick(6)
    c.eq((g.cell(10, 22), g.a1(10, 22)), (C_DECOR, DK_BRIDGE), 'bridge on water')
    g.go_face(12, 21, 'down'); g.act(); g.tick(6)
    c.check(g.cell(12, 22) != C_DECOR, 'bridge refused on land')
    g.shot('pond_bridge')
    build_poke(g, 3, 2)
    g.go_face(11, 21, 'down'); g.act(); g.tick(6)
    c.eq(g.cell(11, 22), C_GRASS, 'fill water back to grass')
    g.press('b'); g.tick(4)
    # buildings: silo 1x2 in the east work area
    build_poke(g, 4, 3)
    g.go_face(18, 16, 'up'); g.act()
    g.yesno(True); g.tick(20); g.dismiss()
    k = g.bld_find(B_SILO)
    c.check(k is not None, 'silo built')
    if k is not None:
        _, bx, by, fl = g.bld(k)
        c.eq((bx, by, fl), (18, 14, 0), 'silo footprint anchored on the front cell')
        c.eq([g.cell(18, 14), g.cell(18, 15)], [C_STRUCT, C_STRUCT], 'silo cells')
    g.shot('silo')
    # overlapping footprint refused
    build_poke(g, 4, 0)                 # barn 3x3 overlapping the silo
    gold = g.u32('gold')
    g.go_face(19, 17, 'up'); g.act(); g.tick(10)
    if g.ui_open():
        g.dismiss()
    c.eq(sum(1 for k in range(MAX_BLD) if g.bld(k)[0] == B_BARN), 1, 'barn overlapping the silo is refused')
    c.eq(g.u32('gold'), gold, 'nothing charged')
    g.press('b'); g.tick(4)
    # a 3x3 building on the west area
    clear_area(g, 8, 15, 14, 23, force=True)
    build_poke(g, 4, 2)                 # greenhouse
    g.go_face(11, 19, 'up'); g.act()
    g.yesno(True); g.tick(20); g.dismiss()
    ks = [k for k in range(MAX_BLD) if g.bld(k)[0] == B_GREENHOUSE]
    c.eq(len(ks), 2, 'second greenhouse built')
    if len(ks) == 2:
        _, bx, by, fl = g.bld(ks[1])
        c.eq((bx, by), (10, 16), 'greenhouse footprint (3x3 centred, facing up)')
        c.eq(fl & 1, 0, 'new building is not a ruin')
    g.press('b'); g.tick(4)
    g.go(11, 20)
    g.shot('greenhouse')
    # every other building type in turn, each on a freshly cleared spot, then demolished
    for t in (B_BARN, B_COOP, B_SHED, B_STABLE, B_WINDMILL, B_STAND, B_GAZEBO):
        sel = [B_BARN, B_COOP, B_GREENHOUSE, B_SILO, B_SHED, B_STABLE, B_WINDMILL, B_STAND, B_GAZEBO].index(t)
        clear_area(g, 17, 13, 23, 17, force=True)
        n0 = sum(1 for k in range(MAX_BLD) if g.bld(k)[0] == t)
        build_poke(g, 4, sel)
        g.go_face(20, 17, 'up'); g.shoo_pet(); g.act()     # the dog likes to sit in the footprint
        g.yesno(True); g.tick(20); g.dismiss()
        ks = [k for k in range(MAX_BLD) if g.bld(k)[0] == t]
        c.eq(len(ks), n0 + 1, f'building type {t} built')
        if len(ks) > n0:
            _, bx, by, _ = g.bld(ks[-1])
            cells = {g.cell(bx + i, by + j) for i in range(BW[t]) for j in range(BH[t])}
            c.eq(cells, {C_STRUCT}, f'building type {t} footprint stamped')
            g.press('b'); g.tick(4)
            g.go(20, 18) if g.walkable(20, 18) else None
            g.shot(f'bld_{t}')
            build_poke(g, 5, 1)
            g.go_face(20, 17, 'up'); g.act()
            g.yesno(True); g.tick(10); g.dismiss()
        g.press('b'); g.tick(4)
    # the house can't be removed
    build_poke(g, 5, 1)
    g.go_face(10, 12, 'up'); g.act(); g.tick(20)
    g.dismiss()
    c.eq(g.bld(0)[0], B_HOUSE, 'house survives REMOVE')
    g.press('b'); g.tick(4)


@scenario('build_all', base='cleared')
def s_build_all(c, g):
    """place every decor/machine/tree catalogue entry once, check cell + cost"""
    g.rich()
    clear_area(g, 17, 12, 23, 17)
    spots = [(x, y) for y in (16, 18, 20, 22) for x in range(8, 15)] + [(x, y) for y in (13, 15, 17) for x in range(17, 24)]
    si = 0
    for cat, n in [(0, 17), (1, 13), (2, 8)]:
        for sel in range(n):
            x, y = spots[si]; si += 1
            build_poke(g, cat, sel)
            g.go_face(x, y - 1, 'down')
            before = g.u32('gold'), g.u16('wood'), g.u16('stone')
            g.park_pet()                          # the dog likes to sit in the spot
            g.act(); g.tick(8)
            if g.ui_open():
                g.dismiss()
            c.check(g.cell(x, y) in (C_DECOR, C_MACHINE, C_TREE), f'cat {cat} item {sel} placed at {x},{y} (cell {g.cell(x, y)})')
            c.check((g.u32('gold'), g.u16('wood'), g.u16('stone')) != before, f'cat {cat} item {sel} charged something')
        g.press('b'); g.tick(4)
    to_town(g); to_farm(g)
    g.go(11, 17)
    g.shot('west')
    g.go(20, 14)
    g.shot('east')


@scenario('move_remove', base='cleared')
def s_move_remove(c, g):
    g.rich()
    build_poke(g, 1, 5)                 # statue
    g.go_face(9, 16, 'down'); g.act(); g.tick(6)
    c.eq(g.a1(9, 17), DK_STATUE, 'statue placed')
    build_poke(g, 5, 0)                 # MOVE
    g.act(); g.tick(6)
    c.eq(g.cell(9, 17), C_GRASS, 'statue picked up')
    g.go_face(11, 16, 'down'); g.act(); g.tick(6)
    c.eq((g.cell(11, 17), g.a1(11, 17)), (C_DECOR, DK_STATUE), 'statue set down')
    # move the coop (2x2 at 17,9) to the east area
    g.go_face(17, 11, 'up'); g.act(); g.tick(6)
    c.eq(g.cell(17, 9), C_GRASS, 'coop lifted')
    g.go_face(20, 16, 'up'); g.act(); g.tick(10)
    _, bx, by, _ = g.bld(1)
    c.eq((bx, by), (19, 14), 'coop moved')
    c.eq([g.cell(x, y) for x in (19, 20) for y in (14, 15)], [C_STRUCT] * 4, 'coop restamped')
    g.shot('coop_moved')
    # cancelling build mode while carrying must not lose the object
    g.go_face(11, 16, 'down'); g.act(); g.tick(6)
    c.eq(g.cell(11, 17), C_GRASS, 'statue in hand')
    g.press('b'); g.tick(6)
    statues = sum(1 for i in range(1024) if g.u8('cell', i) == C_DECOR and g.u8('a1', i) == DK_STATUE)
    c.eq(statues, 1, 'statue comes back when build mode is cancelled while carrying it')
    build_poke(g, 5, 0)
    g.go_face(20, 16, 'up'); g.act(); g.tick(6)      # lift the coop again...
    c.eq(g.cell(20, 15), C_GRASS, 'coop in hand')
    g.press('select'); g.tick(6)                     # ...and leave build mode with SELECT
    c.eq([g.cell(x, y) for x in (19, 20) for y in (14, 15)], [C_STRUCT] * 4, 'coop comes back after cancel')
    # REMOVE the statue and the coop
    build_poke(g, 5, 1)
    st = [(i % 32, i // 32) for i in range(1024) if g.u8('cell', i) == C_DECOR and g.u8('a1', i) == DK_STATUE]
    if st:
        sx, sy = st[0]
        g.go_face(sx, sy - 1, 'down'); g.act(); g.tick(6)
        c.eq(g.cell(sx, sy), C_GRASS, 'statue removed')
    g.go_face(20, 16, 'up'); g.act()
    g.yesno(True); g.tick(10); g.dismiss()
    c.eq(g.bld(1)[0], 0xFF, 'coop demolished')
    c.eq(g.cell(19, 14), C_GRASS, 'coop cells cleared')
    g.press('b'); g.tick(4)
    g.shot('after_remove')


@scenario('music_no_hitch')
def s_music_hitch(c, g):
    """the music keeps time through a scene load (the LCD is off then: the timer interrupt ticks it)"""
    ticks = []
    a = g.noi['music_tick']
    g.pb.hook_register(0, a, lambda _: ticks.append(g.pb._cycles()), None)
    try:
        g.tick(30)
        c0 = len(ticks)
        to_town(g)
        F = 70224 * 2
        gaps = [(b - a) / F for a, b in zip(ticks[c0:], ticks[c0 + 1:])]
        c.check(len(gaps) > 100, f'music ticked through the trip ({len(gaps)} ticks)')
        c.check(max(gaps) < 2.5, f'longest pause between music ticks {max(gaps):.2f} frames (was ~11 while loading)')
        c.note(f'longest music gap farm->town: {max(gaps):.2f} frames')
    finally:
        g.pb.hook_deregister(0, a)


@scenario('town_trip')
def s_town_trip(c, g):
    to_town(g)
    c.eq(g.u8('scene'), MAP_TOWN, 'in town')
    c.eq(g.pos(), (15, 1), 'town arrival spot')
    g.shot('arrive')
    c.eq(len([a for a in g.actors() if a[0] == 1]), 6, 'six townsfolk spawned')
    g.go(12, 15)
    g.shot('plaza')
    g.go(5, 19)
    g.shot('southwest')
    g.go(28, 19)
    g.shot('southeast')
    open_start(g, 3)
    g.tick(30)
    g.shot('no_build_in_town')
    g.dismiss()
    c.eq(g.sym8('_building'), 0, 'no build mode in town')
    to_farm(g)
    c.eq(g.pos(), (15, WH - FO - 2), 'back on the farm road')
    g.shot('home')


def make_town(g):
    g.w16('time', 600)                  # 10am: everyone at their shop
    to_town(g)


@scenario('store', base='town')
def s_store(c, g):
    g.w32('gold', 5000)
    g.go_face(11, 4, 'up')
    g.act()
    g.wait_menu(0, 0, STORE_N)
    g.shot('store_menu')
    g.choose(0, 0, STORE_N, 0)
    g.wait_menu(0, 0, 3)
    g.shot('seed_tiers')
    g.choose(0, 0, 3, R_COMMON)
    g.tick(20)
    g.shot('seed_list')
    c.check(g.wtile(2, 2) == GL_FILL, 'store offers only the 4 spring seeds while the greenhouse is a ruin')
    rows = 10 if g.wtile(2, 2) != GL_FILL else 4     # step-1 list when row 2 has text
    g.choose(0, 0, rows, 1)             # carrot
    g.tick(10)
    g.shot('qty')
    g.quantity(5)
    g.tick(10)
    c.eq(g.seeds(2), 5, 'bought 5 carrot seeds')
    c.eq(g.u32('gold'), 5000 - 5 * SEED_COST[2], 'paid 5x the carrot seed price')
    g.wait_menu(0, 0, rows)
    g.press('b'); g.tick(10)
    gold = g.u32('gold')
    g.choose(0, 0, STORE_N, 1)          # fertilizer x3
    g.quantity(3); g.tick(10)
    c.eq(g.u8('fert'), 3, 'bought 3 fertilizer')
    c.eq(g.u32('gold'), gold - 150, 'paid 150')
    gold = g.u32('gold')
    g.choose(0, 0, STORE_N, 2)          # hay x4
    g.quantity(4); g.tick(10)
    c.eq(g.u16('hay'), 4, 'bought 4 hay')
    c.eq(g.u32('gold'), gold - 80, 'paid 80')
    gold = g.u32('gold')
    g.choose(0, 0, STORE_N, 4)          # hen without a working coop
    g.choose(0, 0, 3, R_COMMON)         # breed: White
    g.tick(30); g.shot('no_coop')
    g.wait_menu(0, 0, STORE_N)
    c.check(all(g.anim(k)['kind'] == 0xFF for k in range(MAX_ANIMALS)), 'no hen without a coop')
    c.eq(g.u32('gold'), gold, 'no charge')
    g.w8('inv', 4, I_EGG * 3); g.w8('inv', 2, I_MILK * 3 + 2)
    gold = g.u32('gold')
    g.choose(0, 0, STORE_N, 6)          # sell
    g.wait_menu(0, 0, 2)
    g.shot('sell_list')
    g.choose(0, 0, 2, 0)                # gold-star milk (items list in item order)
    g.quantity(2); g.tick(10)
    c.eq(g.inv(I_MILK), 0, 'sold both gold milk')
    c.eq(g.u32('gold'), gold + 2 * ITEM_VALUE(I_MILK, 2), 'gold milk sells for 1.5x')
    c.check(g.u8('shipped', I_MILK >> 3) & (1 << (I_MILK & 7)), 'milk in the collection log')
    g.dismiss()
    c.eq(g.u8('scene'), MAP_TOWN, 'still in town')


@scenario('smith', base='town')
def s_smith(c, g):
    g.w32('gold', 20000)
    g.w8('inv', 5, I_COPPER * 3)
    g.go_face(2, 4, 'up')
    g.act()
    g.choose(0, 0, 3, 0)
    g.wait_menu(0, 0, 5)
    g.shot('upgrades')
    g.choose(0, 0, 5, 0)                # hoe
    g.yesno(True)
    g.tick(30); g.shot('done')
    g.dismiss()
    c.eq(g.u8('tier', TT_HOE), 1, 'hoe upgraded to copper')
    c.eq(g.u32('gold'), 20000 - TIER_GOLD[0], 'paid for the copper hoe')
    c.eq(g.inv(I_COPPER), 0, 'used 5 copper')
    g.go_face(2, 4, 'up'); g.act()
    g.choose(0, 0, 3, 0); g.choose(0, 0, 5, 0); g.yesno(True); g.tick(20); g.dismiss()
    c.eq(g.u8('tier', TT_HOE), 1, 'no iron upgrade without iron ore')
    c.eq(g.u32('gold'), 20000 - TIER_GOLD[0], 'no charge')
    # sell ore
    g.w8('inv', 3, I_IRON * 3)
    g.go_face(2, 4, 'up'); g.act()
    g.choose(0, 0, 3, 1)
    g.choose(0, 0, 1, 0); g.quantity(3); g.tick(10); g.dismiss()
    c.eq(g.u32('gold'), 20000 - TIER_GOLD[0] + 3 * ITEM_PRICE[I_IRON], 'sold 3 iron')


@scenario('carpenter', base='town')
def s_carpenter(c, g):
    g.rich()
    em = g.u8('energy_max')
    g.go_face(27, 4, 'up')
    g.act()
    g.choose(0, 0, 5, 1)                # styles before the shack is rebuilt: refused
    g.choose(0, 0, 5, 0)                # house upgrade
    g.yesno(True); g.tick(30)
    g.wait_menu(0, 0, 5)
    c.eq((g.bld(0)[3] >> 1) & 3, 1, 'house level 1')
    c.eq(g.u8('energy_max'), em + 30, 'more max energy')
    g.choose(0, 0, 5, 1)
    g.wait_menu(0, 0, 4)
    g.shot('styles')
    g.choose(0, 0, 4, 2)                # log cabin
    g.yesno(True); g.tick(30)
    g.dismiss()
    c.eq(g.u8('style'), 2, 'cabin style')
    wood = g.u16('wood')
    g.go_face(27, 4, 'up'); g.act()
    g.choose(0, 0, 5, 2)                # materials: wood x3
    g.choose(0, 0, 2, 0); g.quantity(3); g.tick(10); g.dismiss()
    c.eq(g.u16('wood'), wood + 3, 'bought 3 wood')
    to_farm(g)
    g.go(10, 13)
    g.shot('cabin_house')
    c.check(g.u32('goals') & (1 << 7), 'house goal')
    g.sleep()
    c.check(g.u32('goals') & (1 << 18), 'style goal (by the next morning)')
    c.eq(g.u8('energy'), em + 30, 'morning energy uses the new max')


@scenario('pub', base='town')
def s_pub(c, g):
    """the Pub (west of the park): shut before 4pm, then Corey serves; a root beer is 160G for 20 energy"""
    g.w32('gold', 5000)
    g.w8('energy', 40)
    g.w16('time', 600)                  # 10am: the sign, no menu
    g.go_face(9, 19, 'up')
    g.act()
    g.tick(30); g.shot('pub_closed')
    g.dismiss(); g.tick(20); g.dismiss(); g.wait_field()
    c.eq(g.u32('gold'), 5000, 'nothing sold while shut')
    g.w16('time', 1020)                 # 5pm
    g.act()
    g.choose(0, 3, 5, 0)                # root beer
    g.tick(20); g.shot('pub_rootbeer')
    g.dismiss()
    c.eq(g.u8('energy'), 60, 'root beer +20 energy')
    c.eq(g.u32('gold'), 5000 - 160, 'root beer price')


@scenario('cafe_realtor', base='town')
def s_cafe_realtor(c, g):
    g.w32('gold', 5000)
    g.w8('energy', 40)
    g.go_face(19, 4, 'up')
    g.act()
    g.choose(0, 4, 4, 0)                # coffee
    g.tick(20); g.shot('coffee')
    g.dismiss()
    c.eq(g.u8('energy'), 60, 'coffee +20 energy')
    c.eq(g.u32('gold'), 5000 - CAFE_PRICE[0], 'coffee price')
    g.go_face(24, 14, 'up')
    g.act()
    lst = realtor_list(g)
    g.wait_menu(0, 0, len(lst))
    g.shot('realtor')
    names = [bytes(g.pb.memory[g.sym['_lbl'] + k * 19 + j] for j in range(18)).split(b'\0')[0].decode('latin1') for k in range(len(lst))]
    c.check(any(n.startswith('South Pasture') for n in names), f'full plot names in the land list: {names}')
    c.eq(len(set(names)), len(names), f'every row of the land list reads differently: {names}')
    g.choose(0, 0, len(lst), lst.index(OLD_PLOT(13)))   # South Pasture
    g.wait_menu(14, YN_Y, 2)
    g.shot('buy_prompt')
    g.yesno(True); g.tick(20); g.dismiss()
    c.check(g.u16('plots') & (1 << 13), 'bought South Pasture at the land office')
    c.eq(g.u32('gold'), 5000 - CAFE_PRICE[0] - PLOT_PRICE[OLD_PLOT(13)], 'paid the South Pasture price')


@scenario('board_hall_fountain', base='town')
def s_board(c, g):
    g.w32('gold', 5000)
    g.go_face(13, 9, 'up')
    g.act(); g.tick(20); g.shot('board'); g.dismiss()
    it, qty, npc = g.u8('req_item'), g.u8('req_qty'), g.u8('req_npc')
    c.check(qty >= 2, f'a request is posted (item {it} x{qty})')
    g.w8('inv', qty, it * 3)
    g.go_face(13, 9, 'up'); g.act()
    g.yesno(True); g.tick(20); g.dismiss()
    c.eq(g.u8('req_qty'), 0, 'request delivered')
    c.eq(g.u32('gold'), 5000 + ITEM_PRICE[it] * qty * 2, 'request reward')
    c.eq(g.u8('friend', npc), 20, 'requester likes you more')
    # hall, ordinary day
    g.go_face(16, 9, 'up'); g.act(); g.choose(0, 0, HALL_N, 0); g.tick(20); g.shot('hall'); g.dismiss()
    # spring festival with 8 flower beds
    g.w8('day', 14)
    for k in range(8):
        g.setcell(8 + k, 15, C_DECOR, DK_FLOWERBED)
    gold = g.u32('gold')
    g.go_face(16, 9, 'up'); g.act(); g.choose(0, 0, HALL_N + 1, 0); g.tick(20); g.shot('festival'); g.dismiss()
    c.eq(g.u32('gold'), gold + 500, 'flower show best-garden prize')
    c.check(g.u8('fest_done') & 0x10, 'best garden award (the egg hunt itself is fest_egg_hunt)')
    # fountain
    gold = g.u32('gold')
    g.go_face(15, 13, 'up'); g.act()
    g.yesno(True); g.tick(20); g.dismiss()
    c.eq(g.u32('gold'), gold - 10, 'fountain wish costs 10')


@scenario('npc_gift', base='town')
def s_npc(c, g):
    g.w8('inv', 2, I_STRAWB * 3)
    g.w8('inv', 1, I_MINNOW * 3)
    talked = False
    for _ in range(10):
        rosa = [a for a in g.actors() if a[0] == 1 and a[1] == 0][0]
        _, _, rx, ry = rosa
        for d, (dx, dy, _) in DIRS.items():
            sx, sy = rx - dx, ry - dy
            if g.walkable(sx, sy):
                break
        else:
            g.tick(60)
            continue
        g.go(sx, sy)
        g.face(d)
        if g.actor_at(rx, ry):
            g.act()
            talked = True
            break
    c.require(talked, 'could not reach Alyssa')
    g.tick(30)
    g.shot('rosa')
    g.yesno(True)                       # give a gift?
    g.wait_menu(0, 0, 2)
    g.shot('gift_pick')
    g.choose(0, 0, 2, 0)                # strawberry (she loves it)
    g.tick(20); g.shot('gift')
    g.dismiss()
    c.eq(g.u8('friend', 0), 14, 'talk (+2) + loved gift (+12)')
    c.check(g.u8('gifted') & 1, 'gifted today')
    c.eq(g.inv(I_STRAWB), 1, 'gift taken')
    # second talk the same day: no gift prompt, no more friendship
    rosa = [a for a in g.actors() if a[0] == 1 and a[1] == 0][0]
    if abs(rosa[2] - g.pos()[0]) + abs(rosa[3] - g.pos()[1]) == 1:
        g.act(); g.tick(40)
        c.check(g.menu_sel(14, YN_Y, 2) is None, 'no second gift prompt today')
        g.dismiss()
    c.eq(g.u8('friend', 0), 14, 'no extra friendship from talking twice')


@scenario('animals', base='cleared')
def s_animals(c, g):
    g.rich()
    g.w16('hay', 5)
    clear_area(g, 10, 11, 22, 13)       # the walk to the coop and barn doors, whatever the valley grew there
    g.go_face(17, 11, 'up')             # coop door
    g.act()
    g.yesno(True); g.tick(30); g.dismiss()
    c.eq(g.bld(1)[3] & 1, 0, 'coop repaired')
    g.go_face(21, 12, 'up')             # barn door
    g.act()
    g.yesno(True); g.tick(30); g.dismiss()
    c.eq(g.bld(2)[3] & 1, 0, 'barn repaired')
    g.w16('time', 600)
    to_town(g)
    g.go_face(11, 4, 'up'); g.act()
    g.choose(0, 0, STORE_N, 4)          # hen
    g.choose(0, 0, 3, R_COMMON)
    g.tick(40); g.shot('name_hen'); g.press('start'); g.tick(20)
    g.wait_menu(0, 0, STORE_N)
    g.choose(0, 0, STORE_N, 3)          # cow
    g.choose(0, 0, 3, R_COMMON)
    g.tick(40); g.press('start'); g.tick(20)
    g.wait_menu(0, 0, STORE_N)
    g.dismiss()
    kinds = sorted(g.anim(k)['kind'] for k in range(MAX_ANIMALS) if g.anim(k)['kind'] != 0xFF)
    c.eq(kinds, [0, 1], 'bought a cow and a hen')
    to_farm(g)
    animals = [a for a in g.actors() if a[0] == 2]
    c.eq(len(animals), 2, 'both animals walk around the farm')
    g.go(19, 13)
    g.shot('animals_out')
    # feed both
    g.go_face(17, 11, 'up'); g.act(); g.tick(20); g.dismiss()
    g.go_face(21, 12, 'up'); g.act(); g.tick(20); g.dismiss()
    c.eq(g.u16('hay'), 3, 'one hay each')
    c.check(all(g.anim(k)['flags'] & 1 for k in range(MAX_ANIMALS) if g.anim(k)['kind'] != 0xFF), 'both fed')
    g.sleep()
    c.check(all(g.anim(k)['flags'] & 4 for k in range(MAX_ANIMALS) if g.anim(k)['kind'] != 0xFF), 'produce ready next day')
    g.go_face(17, 11, 'up'); g.act(); g.tick(20); g.dismiss()
    g.go_face(21, 12, 'up'); g.act(); g.tick(20); g.dismiss()
    c.eq((g.inv(I_EGG), g.inv(I_MILK)), (1, 1), 'collected an egg and milk')
    # morning animals: right after waking (6am, sunny) they should be outside too
    g.w8('weather', 0); g.w8('tomorrow', 0)
    g.sleep()
    c.check(len([a for a in g.actors() if a[0] == 2]) > 0 or g.u8('weather') != 0, 'animals are out on a sunny morning')
    g.go(19, 13)
    g.shot('morning_animals')


@scenario('fishing', base='cleared')
def s_fishing(c, g):
    g.rich()
    build_poke(g, 3, 0)
    for x in (9, 10, 11):
        g.go_face(x, 21, 'down'); g.act(); g.tick(6)
    g.press('b'); g.tick(4)
    caught = 0
    for attempt in range(4):
        g.go_face(10, 21, 'down')
        g.w8('tool', T_ROD)
        n0 = g.u16('stat_fish')
        g.act()
        g.tick(10)
        g.shot(f'cast{attempt}')
        bite = False
        for _ in range(260):
            g.tick(1)
            if g.pb.get_sprite(2).tile_identifier == SPR_EXCLAIM:
                bite = True
                break
        if not bite:
            c.note(f'cast {attempt}: no bite seen')
            g.dismiss()
            continue
        g.press('a', 2, 2)
        g.tick(10)
        g.shot(f'reel{attempt}')
        held = False
        for f in range(3000):
            if not g.ui_open():
                break
            fish = zone = None
            for i in range(16):
                if g.wtile(2 + i, 13) == GL_IT_FISH:
                    fish = i
                if zone is None and g.wtile(2 + i, 14) == GL_BAR_FULL:
                    zone = i
            if fish is not None and zone is not None:
                want = fish > zone + 1
                if want != held:
                    (g.pb.button_press if want else g.pb.button_release)('a')
                    held = want
            elif fish is None and zone is None and f > 30:
                break
            g.tick(1)
        if held:
            g.pb.button_release('a')
        for _ in range(240):                # the catch jingle + pose play before the dialog
            g.tick(1)
            if g.ui_open() or g.u16("stat_fish") > n0:
                break
        g.tick(30)
        g.shot(f'result{attempt}')
        g.dismiss()
        if g.u16('stat_fish') > n0:
            caught += 1
            break
    c.check(caught, 'caught a fish')
    fish_items = sum(g.inv(i) for i in range(I_MINNOW, I_KOI + 1))
    c.eq(fish_items, caught, 'fish in the bag')


@scenario('machines', base='cleared')
def s_machines(c, g):
    g.rich()
    for sel, x in ((8, 9), (9, 10), (10, 11)):
        build_poke(g, 0, sel)
        g.go_face(x, 16, 'down'); g.act(); g.tick(6)
    g.press('b'); g.tick(4)
    c.eq([g.cell(x, 17) for x in (9, 10, 11)], [C_MACHINE] * 3, 'keg, press, jar placed')
    g.w8('inv', 2, I_APPLE * 3); g.w8('inv', 1, I_MILK * 3); g.w8('inv', 1, I_RADISH * 3)
    g.go_face(9, 16, 'down'); g.act(); g.yesno(True); g.choose(0, 0, 2, 1); g.tick(10)   # keg <- apple
    g.go_face(10, 16, 'down'); g.act(); g.yesno(True); g.choose(0, 0, 1, 0); g.tick(10)  # press <- milk
    g.go_face(11, 16, 'down'); g.act(); g.yesno(True); g.choose(0, 0, 2, 0); g.tick(10)  # jar <- radish
    g.dismiss()
    c.eq([g.a2(x, 17) for x in (9, 10, 11)], [I_CIDER + 1, I_CHEESE + 1, I_PICKLES + 1], 'machines loaded')
    c.eq((g.inv(I_APPLE), g.inv(I_MILK), g.inv(I_RADISH)), (1, 0, 0), 'inputs taken')
    g.shot('loaded')
    ready = []
    for day in range(5):
        g.sleep()
        ready.append([bool(g.a2(x, 17) & 0x80) for x in (9, 10, 11)])
    want = [[d + 1 >= MACHINE_DAYS[k] for k in range(3)] for d in range(5)]
    c.eq(ready, want, f'keg {MACHINE_DAYS[0]} days, press {MACHINE_DAYS[1]}, jar {MACHINE_DAYS[2]}')
    g.go(10, 16)
    g.shot('ready')
    for x in (9, 10, 11):
        g.go_face(x, 16, 'down'); g.act(); g.tick(10); g.dismiss()
    c.eq((g.inv(I_CIDER), g.inv(I_CHEESE), g.inv(I_PICKLES)), (1, 1, 1), 'collected cider, cheese, pickles')
    c.eq([g.a2(x, 17) for x in (9, 10, 11)], [0, 0, 0], 'machines empty again')


@scenario('fruit_trees', base='cleared')
def s_trees(c, g):
    """trees live forever: fruit every TREE_RIPE_DAYS days except winter, 2..5 by age, +2 in their home season"""
    g.rich()
    build_poke(g, 0, 14)                # cherry (home season spring)
    g.go_face(9, 16, 'down'); g.act(); g.tick(6)
    build_poke(g, 0, 11)                # apple (home season fall)
    g.go_face(11, 16, 'down'); g.act(); g.tick(6)
    g.press('b'); g.tick(4)
    c.eq((g.cell(9, 17), g.a1(9, 17) >> 5), (C_TREE, 3), 'cherry tree planted')
    c.eq((g.cell(11, 17), g.a1(11, 17) >> 5), (C_TREE, 0), 'apple tree planted')
    g.go_face(9, 16, 'down'); g.act(); g.tick(20); g.shot('sapling'); g.dismiss()
    c.eq(g.inv(I_CHERRY), 0, 'saplings give no fruit')
    # a young (age 10) cherry and a mature (age 30) apple, one day from ripe
    g.w8('a2', 10, 17 * 32 + 9); g.w8('a1', (3 << 5) | (TREE_RIPE_DAYS - 1), 17 * 32 + 9)
    g.w8('a2', 30, 17 * 32 + 11); g.w8('a1', (0 << 5) | (TREE_RIPE_DAYS - 1), 17 * 32 + 11)
    g.sleep()
    c.eq(g.a1(9, 17) & 31, TREE_RIPE_DAYS, 'cherry ripe')
    c.eq(g.a1(11, 17) & 31, TREE_RIPE_DAYS, 'apple ripe too: trees fruit in any season but winter')
    g.go(10, 16)
    g.shot('ripe_trees')
    g.go_face(9, 16, 'down'); g.act(); g.tick(10); g.dismiss()
    c.eq(g.inv(I_CHERRY), 2 + 2, 'young cherry in its home season: 2 + 2 fruit')
    c.eq(g.a1(9, 17) & 31, 0, 'picked tree starts over')
    g.go_face(11, 16, 'down'); g.act(); g.tick(10); g.dismiss()
    c.eq(g.inv(I_APPLE), 5, 'mature apple out of season: 5 fruit')
    g.go_face(11, 16, 'down'); g.act(); g.tick(20); g.shot('not_ripe'); g.dismiss()
    c.eq(g.inv(I_APPLE), 5, 'nothing more until it ripens again')
    # winter: resting, does not ripen
    g.w8('season', SEASON_WINTER)
    g.w8('a1', (3 << 5) | (TREE_RIPE_DAYS - 1), 17 * 32 + 9)
    g.sleep()
    c.eq(g.a1(9, 17) & 31, TREE_RIPE_DAYS - 1, 'no ripening in winter')
    g.go_face(9, 16, 'down'); g.act(); g.tick(20); g.shot('winter_rest'); g.dismiss()
    c.eq(g.inv(I_CHERRY), 4, 'no winter fruit')


@scenario('mail')
def s_mail(c, g):
    g.setcell(10, 14, C_SOIL, 1, 4)     # a ripe radish
    g.go_face(10, 13, 'down')
    g.w8('tool', T_HAND); g.act(); g.tick(10); g.dismiss()
    c.check(g.u32('mail') & 1, 'letter waiting after the first harvest')
    g.sleep_shots = True
    g.sleep()
    clear_area(g, 8, 8, 14, 8)          # overnight growth can wall in the mailbox nook
    gold = g.u32('gold')
    g.go_face(13, 9, 'left')
    g.act()
    g.tick(60); g.shot('letter')
    g.dismiss()
    c.eq(g.u32('gold'), gold + 200, 'letter had 200g')
    c.eq(g.u32('mail'), 0, 'mailbox empty')
    g.go_face(13, 9, 'left'); g.act(); g.tick(30); g.shot('empty'); g.dismiss()


@scenario('options')
def s_options(c, g):
    open_start(g, 5)
    g.tick(20)
    g.shot('options')
    g.press('a')                        # text speed Normal -> Fast
    for _ in range(3):
        g.press('down', 2, 4)
    g.press('right')                    # clock Normal -> Quick
    g.tick(10)
    g.shot('changed')
    g.dismiss()
    c.eq(g.u8('opt_text'), 2, 'text speed changed')
    c.eq(g.u8('opt_clock'), 2, 'clock speed changed')
    t0 = g.u16('time')
    g.tick(262 * 3)
    c.check(g.u16('time') - t0 >= 30, 'quick clock ticks faster')


@scenario('menus')
def s_menus(c, g):
    g.w8('inv', 3, I_RADISH * 3); g.w8('inv', 1, I_EGG * 3 + 2)
    for idx, name in ((0, 'tools'), (1, 'seeds'), (2, 'bag')):
        open_start(g, idx)
        g.tick(30)
        g.shot(name)
        g.dismiss()
    for k, name in enumerate(('status', 'map', 'calendar', 'friends', 'goods', 'staff', 'valley')):
        open_start(g, 4)
        if name == 'calendar':
            g.w8('req_qty', 3); g.w8('req_day', 5)
        g.choose(8, 2, FARM_N, k)
        g.tick(40)
        g.shot(name)
        if name == 'calendar':
            marks = [g.wtile(x, y) for y in range(3, 10) for x in range(4, 19)]
            c.eq(marks.count(GL_CAL_FEST), 1, 'one festival flag (day 14)')
            c.eq(marks.count(GL_CAL_DOT), 1, 'one request-due dot (day 6)')
        g.dismiss()
    c.check(not g.ui_open(), 'menus closed')


def vm_kind(g, x, y, own):
    """uipages.c vm_kind: what the valley map shows for the 4x4 cells at real (x, y)"""
    n = {}
    rows = []
    for j in range(4):
        wr = 0
        for i in range(4):
            c, a = g.wcell(x + i, y + j), g.wa1(x + i, y + j)
            k = {C_WILDFLOWER: 'flower', C_ROCK: 'rock', C_BOULDER: 'rock', C_ORE: 'rock', C_FOREST: 'woods',
                 C_PINE: 'woods', C_BIRCH: 'woods', C_BUSH: 'woods', C_SAPLING_WILD: 'woods', C_DIRT: 'path',
                 C_SOIL: 'soil', C_GIANT: 'soil', C_TREE: 'tree', C_STRUCT: 'bldg', C_MACHINE: 'bldg',
                 C_WATER: 'water', C_ROAD: 'road', C_SAND: 'sand', C_CLIFF: 'cliff'}.get(c)
            if c == C_DECOR:
                k = 'road' if a == DK_BRIDGE else 'path' if a in DECOR_WALK else None
            if k == 'water':
                wr |= 1 << i
            n[k] = n.get(k, 0) + 1
        rows.append(wr)
    wcol = rows[0] & rows[1] & rows[2] & rows[3]
    if n.get('bldg'): return 'bldg'
    if n.get('road'): return 'path'
    if n.get('water', 0) >= 8 or 15 in rows or wcol: return 'water'
    if n.get('water', 0) >= 2: return 'sand' if n.get('sand') else 'marsh'
    if n.get('cliff', 0) >= 2: return 'cliff'
    if n.get('soil', 0) >= 2: return 'crops'
    if n.get('tree', 0) >= 2 or (n.get('tree') and own): return 'orchard'
    if n.get('woods', 0) >= 6: return 'woods'
    if n.get('rock', 0) >= 5: return 'rocks'
    if n.get('sand', 0) >= 6: return 'sand'
    if n.get('path', 0) >= 3: return 'path'
    if n.get('flower', 0) >= 5: return 'flowers'
    return 'meadow'


def open_map(g):
    open_start(g, 4)
    g.choose(8, 2, FARM_N, 1)
    g.tick(8)


@scenario('valley_map')
def s_valley_map(c, g):
    """START > FARM > MAP: the whole valley, one tile per 4x4 cells, each showing its ground, shaded where it's
    for sale; a bracket sprite moves plot by plot and A offers to buy"""
    open_start(g, 4)
    g.choose(8, 2, FARM_N, 1)
    for f in range(60):                 # fast to open: the map is up within a second
        g.tick(1)
        if g.pb.memory[0xFE00 + 22 * 4] != 0:
            break
    c.check(f < 50, f'map drawn and bracket up after {f} frames')
    g.tick(40)
    g.shot('map')
    hx, hy = g.bld(0)[1] + FO, g.bld(0)[2] + FO
    skip = {((hx + 1) >> 2, (hy + 1) >> 2), (g.real_pos()[0] >> 2, g.real_pos()[1] >> 2)}   # house + you markers
    seen, kinds = {}, set()
    ok = True
    for ty in range(16):
        for tx in range(16):
            if (tx, ty) in skip:
                continue
            own = g.plot_owned((ty >> 1) * 8 + (tx >> 1))
            k = (vm_kind(g, tx * 4, ty * 4, own), own)
            kinds.add(k[0])
            t = g.wtile(2 + tx, ty)
            if seen.setdefault(k, t) != t:
                ok = False
    c.check(ok, 'every tile shows its kind (same kind, same tile)')
    c.eq(len(set(seen.values())), len(seen), f'each kind has its own tile ({len(seen)} kinds shown)')
    for want in ('woods', 'orchard', 'water', 'marsh', 'rocks', 'cliff', 'path', 'bldg', 'meadow'):
        c.check(want in kinds, f'{want} on the new valley map')
    c.check(seen.get(('meadow', True)) != seen.get(('meadow', False)), 'owned meadow is drawn apart from meadow for sale')
    # the bracket starts on your plot and moves plot by plot
    px, py = g.real_pos()
    oam = lambda: (g.pb.memory[0xFE00 + 22 * 4 + 1] - 8, g.pb.memory[0xFE00 + 22 * 4] - 16)
    def bracket():
        for _ in range(20):
            if g.pb.memory[0xFE00 + 22 * 4]:
                return oam()
            g.tick(1)
        return None
    c.eq(bracket(), (16 + (px >> 3) * 16, (py >> 3) * 16), 'bracket on your plot')
    g.press('right'); g.tick(2)
    c.eq(bracket(), (16 + ((px >> 3) + 1) * 16, (py >> 3) * 16), 'one plot right')
    g.press('down'); g.tick(2)
    c.eq(bracket(), (16 + ((px >> 3) + 1) * 16, ((py >> 3) + 1) * 16), 'one plot down')
    g.shot('moved')
    c.check(g.plot_owned(((py >> 3) + 1) * 8 + (px >> 3) + 1), 'still in the home field: A does nothing')
    g.press('a'); g.tick(10)
    c.eq(bracket(), (16 + ((px >> 3) + 1) * 16, ((py >> 3) + 1) * 16), 'map still up')
    g.press('right'); g.tick(2)
    # right of the home field: for sale -> A opens the buy prompt
    p = ((py >> 3) + 1) * 8 + (px >> 3) + 2
    c.check(not g.plot_owned(p), 'that plot is for sale')
    g.tick(16)
    g.shot('for_sale')
    g.press('a')
    g.wait_menu(14, YN_Y, 2)
    g.shot('buy_from_map')
    g.yesno(False)
    g.dismiss()
    c.check(not g.ui_open(), 'map closed')
    # back in the field: the sprites the map borrowed are the field's again
    g.tick(20)
    c.check(g.pb.memory[0xFE00] != 0, 'player sprite back')


def boot_title(g):
    g.boot()
    title_start(g)


@scenario('save_continue')
def s_save(c, g):
    g.w32('gold', 12345)
    open_start(g, 6)
    g.yesno(True)
    g.tick(20)
    g.dismiss()
    boot_title(g)
    g.shot('title_slots')
    g.choose(1, 9, 2, 0)
    g.choose(8, 4, 3, 0)                # Continue
    g.tick(60)
    g.dismiss(); g.wait_field()
    g.shot('continued')
    c.eq(g.u32('gold'), 12345, 'slot 1 gold restored')
    c.eq(g.u8('scene'), SCENE_FARM, 'on the farm')
    # the one slot is reused: sleep (the morning auto-save) writes over it, and that is what loads next
    g.w32('gold', 777)
    g.sleep()
    boot_title(g)
    g.shot('one_slot')
    g.choose(1, 9, 2, 0)
    g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    c.eq((g.u32('gold'), g.u8('day')), (777, 2), 'restored after the morning auto-save')
    # Start Over gives a fresh farm in the same slot
    boot_title(g)
    g.choose(1, 9, 2, 0)
    g.choose(8, 4, 3, 1)
    g.yesno(True)
    g.tick(60)
    g.yesno(False, advance='a')
    g.tick(20); g.dismiss(); g.wait_field()
    c.eq(g.u32('gold'), 500, 'Start Over is a new farm')


@scenario('pass_out')
def s_pass_out(c, g):
    g.go(14, 20) if g.walkable(14, 20) else g.go(12, 14)
    g.w16('time', 1550)
    for _ in range(200):
        g.tick(10)
        if g.ui_open():
            break
    g.tick(30)
    g.shot('collapse')
    g.dismiss()
    for _ in range(100):
        g.tick(10)
        if g.u8('day') == 2:
            break
    g.morning_shot = 'morning_report'
    g.after_night()
    g.shot('morning')
    c.eq(g.u8('day'), 2, 'next day')
    c.eq(g.u16('time'), 360, '6am')
    c.eq(g.u8('energy'), g.u8('energy_max') // 2, 'half energy after passing out')
    c.eq(g.pos(), (10, 12), 'woke at the house door')


@scenario('season_change')
def s_season(c, g):
    till_plant_water(g, 10, 14, 1)
    c.eq(g.a1(10, 14) & 0x1F, 1, 'radish planted')
    g.w8('day', 28)
    g.morning_shot = 'summer_report'
    g.sleep()
    c.eq((g.u8('season'), g.u8('day')), (1, 1), 'summer 1')
    c.eq(g.a1(10, 14) & 0x1F, 31, 'spring radish withered in summer')
    g.go(10, 13)
    g.shot('summer')
    g.w8('day', 28); g.w8('season', 2)
    g.sleep()
    c.eq((g.u8('season'), g.u8('day')), (3, 1), 'winter 1')
    g.shot('winter')
    g.w8('day', 28)
    g.sleep()
    c.eq((g.u8('season'), g.u8('year')), (0, 2), 'year 2 spring')
    c.check(g.u32('mail') & (1 << 31), "Grandpa's review letter in year 2")


@scenario('clearing', base='cleared')
def s_clearing(c, g):
    """hands/sickle/axe/hammer on every kind of obstacle; rocks, stumps, logs and trees take several hits"""
    row = [(C_WEEDS, 0), (C_STUMP, 0), (C_ROCK, 0), (C_BOULDER, 0), (C_LOG, 0), (C_ORE, 1), (C_PINE, 0)]
    for k, (cell, a1) in enumerate(row):
        g.setcell(8 + k, 20, cell, a1)
    g.setcell(8, 22, C_FORAGE, 1); g.setcell(9, 22, C_BUSH, 6)
    to_town(g); to_farm(g)               # redraw
    g.go(10, 19)
    g.shot('obstacles')

    def hit(x, tool, n=1):
        g.go_face(x, 19, 'down'); g.w8('tool', tool)
        for _ in range(n):
            g.act(); g.tick(10)
            if g.ui_open():
                g.dismiss()

    w0 = g.u16('stat_weeds')
    hit(8, T_HAND)
    c.eq(g.cell(8, 20), C_GRASS, 'weeds pulled by hand')
    c.eq(g.u16('stat_weeds'), w0 + 1, 'weed stat')
    e0 = g.u8('energy')
    hit(9, T_AXE)
    c.eq(g.cell(9, 20), C_STUMP, 'one axe swing does not clear a stump')
    c.eq(g.u8('energy'), e0 - 2, 'each swing costs energy')
    hit(9, T_AXE)
    g.shot('wood_flash')
    c.eq((g.cell(9, 20), g.u16('wood')), (C_GRASS, 3), 'stump: 2 swings -> 3 wood')
    hit(10, T_HAMMER, 2); g.shot('stone_flash')
    c.eq((g.cell(10, 20), g.u16('stone')), (C_GRASS, 2), 'rock: 2 hits -> 2 stone')
    hit(11, T_HAMMER, 1); g.tick(20)
    c.eq(g.cell(11, 20), C_BOULDER, 'boulder needs a better hammer')
    g.w8('tier', 1, TT_HAMMER)
    hit(11, T_HAMMER, 3)
    c.eq(g.cell(11, 20), C_BOULDER, 'copper hammer: boulder holds after 3 hits')
    hit(11, T_HAMMER, 1)
    c.eq((g.cell(11, 20), g.u16('stone')), (C_GRASS, 12), 'copper hammer breaks the boulder on hit 4 (+10)')
    hit(12, T_AXE); g.tick(20)
    c.eq(g.cell(12, 20), C_LOG, 'log needs a better axe')
    g.w8('tier', 2, TT_AXE)
    hit(12, T_AXE, 3)
    c.eq((g.cell(12, 20), g.u16('wood')), (C_GRASS, 15), 'iron axe chops the log in 3 (+12)')
    # switching targets resets the hit count
    g.w8('tier', 0, TT_AXE)
    g.setcell(10, 20, C_STUMP)
    hit(10, T_AXE, 1)
    hit(14, T_AXE, 1)
    hit(10, T_AXE, 1)
    c.eq(g.cell(10, 20), C_STUMP, 'hitting another obstacle resets the first one')
    hit(10, T_AXE, 1)
    c.eq(g.cell(10, 20), C_GRASS, 'stump cleared after 2 hits in a row')
    hit(13, T_HAMMER); g.tick(10)
    c.eq(g.cell(13, 20), C_SAND, 'iron ore mined with a copper hammer')
    c.check(g.inv(I_IRON) >= 1, 'got iron')
    wood = g.u16('wood')
    hit(14, T_AXE, 3)
    c.eq(g.cell(14, 20), C_SAPLING_WILD, 'pine: 3 swings leave a sapling')
    c.eq(g.u16('wood'), wood + 6, 'pine gives 6 wood')
    g.go_face(8, 21, 'down'); g.w8('tool', T_HAND); g.act(); g.tick(4); g.dismiss()
    c.eq((g.cell(8, 22), g.inv(I_MUSHROOM)), (C_GRASS, 1), 'picked a mushroom')
    g.go_face(9, 21, 'down'); g.act(); g.tick(4); g.dismiss()
    c.check(g.inv(I_BERRIES) >= 2 and g.a1(9, 22) == 0, 'picked berries from the bush')
    # sickle: grass -> hay needs a silo
    g.go_face(10, 21, 'down'); g.w8('tool', T_SICKLE); g.act(); g.tick(4)
    g.shot('no_silo'); g.dismiss()
    c.eq(g.u16('hay'), 0, 'no hay without a silo')
    g.go(11, 19)
    g.shot('cleared')
    # energy runs out
    g.w8('energy', 1)
    g.setcell(10, 22, C_STUMP)
    g.go_face(10, 21, 'down'); g.w8('tool', T_AXE); g.act(); g.tick(30)
    g.shot('too_tired'); g.dismiss()
    c.eq(g.cell(10, 22), C_STUMP, 'too tired to chop')


@scenario('broke', base='town')
def s_broke(c, g):
    """every 'not enough' path: dialogs from other banks with a shopkeeper speaking (used to hang)"""
    g.w32('gold', 0)
    g.go_face(27, 4, 'up'); g.act()
    g.choose(0, 0, 5, 0); g.yesno(True); g.tick(60)
    g.shot('june_no_money')
    g.dismiss()
    c.eq((g.bld(0)[3] >> 1) & 3, 0, 'no house upgrade without money')
    g.go_face(24, 14, 'up'); g.act()
    g.choose(0, 0, len(realtor_list(g)), 0); g.yesno(True); g.tick(60)
    g.shot('realtor_no_money')
    g.dismiss()
    c.eq(g.u16('plots'), 0x0660, 'no plot without money')
    g.go_face(11, 4, 'up'); g.act()
    g.choose(0, 0, STORE_N, 6); g.tick(60)
    g.shot('nothing_to_sell')
    g.dismiss()
    g.go_face(19, 4, 'up'); g.act()
    g.choose(0, 4, 4, 2); g.tick(40)
    g.shot('ada_no_money')
    g.dismiss()
    # a gift with an empty bag
    for _ in range(10):
        rosa = [a for a in g.actors() if a[0] == 1 and a[1] == 0][0]
        sides = [(d, rosa[2] - dx, rosa[3] - dy) for d, (dx, dy, _) in DIRS.items() if g.walkable(rosa[2] - dx, rosa[3] - dy)]
        if sides:
            d, sx, sy = sides[0]
            g.go(sx, sy); g.face(d)
            if g.actor_at(rosa[2], rosa[3]):
                break
        g.tick(60)
    g.act()
    g.yesno(True); g.tick(60)
    g.shot('no_gift_items')
    g.dismiss()
    c.check(g.u8('friend', 0) <= 2, 'no gift given')


@scenario('overnight', base='cleared')
def s_overnight(c, g):
    """sprinklers, beehive, silo + sickle hay, farm stand, giant pumpkin, a working greenhouse"""
    g.rich()
    for x in range(8, 15):                      # a soil strip around a sprinkler at (11,17)
        for y in (16, 18):
            g.setcell(x, y, C_SOIL)
    g.setcell(10, 17, C_SOIL); g.setcell(12, 17, C_SOIL)
    build_poke(g, 0, 4)                          # sprinkler
    g.go_face(11, 16, 'down') if False else None
    g.setcell(11, 17, C_DECOR, DK_SPRINKLER)
    g.setcell(9, 21, C_DECOR, DK_BEEHIVE)
    g.symw8('_building', 0)
    # silo for hay
    build_poke(g, 4, 3)
    g.go_face(18, 16, 'up'); g.act(); g.yesno(True); g.tick(20); g.dismiss()
    g.press('b'); g.tick(4)
    g.go_face(20, 16, 'down'); g.w8('tool', T_SICKLE); g.act(); g.tick(6); g.dismiss()
    c.eq(g.u16('hay'), 1, 'sickle cuts hay once a silo stands')
    # farm stand
    build_poke(g, 4, 7)
    g.go_face(21, 14, 'up'); g.act(); g.yesno(True); g.tick(20); g.dismiss()
    g.press('b'); g.tick(4)
    k = g.bld_find(B_STAND)
    c.check(k is not None, 'farm stand built')
    gold = g.u32('gold')
    g.w8('inv', 10, I_EGG * 3)
    if k is not None:
        _, bx, by, _ = g.bld(k)
        g.go_face(bx, by + 2, 'up'); g.act(); g.yesno(True); g.tick(20); g.dismiss()
        c.eq(g.u32('gold'), gold + 10 * ITEM_PRICE[I_EGG] * 4 // 5, 'farm stand pays 80%')
    # repair the greenhouse and plant an out-of-season crop
    clear_area(g, 17, 18, 19, 22); clear_area(g, 17, 21, 23, 22)
    g.go_face(21, 21, 'up'); g.act(); g.yesno(True); g.tick(30); g.dismiss()
    c.eq(g.bld(3)[3] & 1, 0, 'greenhouse repaired')
    g.setcell(13, 22, C_SOIL)
    g.go_face(13, 21, 'down'); g.set_seeds(6, 2); g.w8('seed_sel', 6); g.w8('tool', T_SEEDS); g.act(); g.tick(6); g.dismiss()
    c.eq(g.a1(13, 22) & 0x1F, 6, 'melon planted in spring with a working greenhouse')
    g.go(11, 20)
    g.shot('setup')
    g.sleep()
    wet = sum(1 for x in range(10, 13) for y in range(16, 19) if g.cell(x, y) == C_SOIL and g.a1(x, y) & 0x80)
    c.eq(wet, 8, 'sprinkler watered its 8 neighbours')
    c.check(not (g.a1(8, 16) & 0x80) or g.u8('weather') in (1, 3), 'outside the sprinkler stays dry')
    g.sleep(); g.sleep()
    if g.cell(9, 20) == C_WEEDS:                 # weeds may have sprung up where we stand to reach the hive
        g.setcell(9, 20, C_GRASS)
    g.go_face(9, 20, 'down'); g.act(); g.tick(6); g.dismiss()
    c.eq(g.inv(I_HONEY), 1, 'beehive honey after 3 days')
    # giant pumpkin: fall, a 2x2 of ripe pumpkins
    g.w8('season', 2); g.w8('day', 5)
    for (x, y) in ((8, 18), (9, 18), (8, 19), (9, 19)):
        g.setcell(x, y, C_SOIL, 10, 10)
    for _ in range(16):
        g.sleep()
        if g.cell(8, 18) == C_GIANT:
            break
        for (x, y) in ((8, 18), (9, 18), (8, 19), (9, 19)):   # crows may peck one; keep the block ripe
            g.setcell(x, y, C_SOIL, 10, 10)
    c.eq([g.cell(x, y) for (x, y) in ((8, 18), (9, 18), (8, 19), (9, 19))], [C_GIANT] * 4, 'ripe 2x2 pumpkins fuse into a giant')
    g.go(10, 17)
    g.shot('giant')
    g.go_face(10, 18, 'left'); g.act(); g.tick(10); g.dismiss()
    c.eq(g.inv(I_PUMPKIN, 2), 12, 'giant pumpkin gives 12 gold-star pumpkins')
    c.eq(g.cell(8, 18), C_SOIL, 'giant pumpkin cleared')
    g.sleep()
    c.check(g.u32('mail') & (1 << 20), 'giant pumpkin letter')
    c.eq(g.a1(13, 22) & 0x1F, 6, 'greenhouse keeps the summer melon alive through season changes')



# ------------------------------------------------------------------ merged features: interiors, farmhands, businesses, rarity
def bg_tiles(g):
    """the whole 32x32 BG map from VRAM bank 0"""
    base = 0x9C00 if g.pb.memory[0xFF40] & 0x08 else 0x9800
    return [g.pb.memory[0, base + i] for i in range(1024)]


def oam_attr(g, i):
    return g.pb.memory[0xFE00 + i * 4 + 3]


def actor_slots(g, kind):
    """OAM base sprite index for each actor of `kind` (actors.c draws actor k at OAM 6 + 2k)"""
    b, n = g.sym['_act_kind'], MAX_ACT
    return [(k, 6 + 2 * k) for k in range(n) if g.pb.memory[b + k] == kind]


def furn_build(g, mode, item=None):
    """inside: START > BUILD > FURNITURE/MOVE/SELL BACK (> item); leaves build mode on"""
    open_start(g, 3)
    g.choose(0, 0, 3, mode)
    if mode == 0:
        g.choose(0, 0, len(FURN_CAT), FURN_CAT.index(item))
    g.tick(10)
    g.dismiss(btn='a')
    return g.sym8('_building')


@scenario('interiors')
def s_interiors(c, g):
    """A at the house door goes inside; Grandpa's bed, TV and diary desk; furniture buy/move/sell; out again"""
    _, hx, hy, _ = g.bld(g.bld_find(B_HOUSE))
    g.go_face(hx + 1, hy + 3, 'up')
    g.act()
    for _ in range(60):
        g.tick(10)
        if g.in_house():
            break
    g.tick(30)
    g.shot('first_visit_tip')
    g.dismiss(); g.wait_field()
    c.eq(g.u8('scene'), MAP_HOUSE1, 'A at the door enters the small house')
    w = map_warps(MAP_HOUSE1)[0]
    c.eq(g.pos(), (w[0], w[1] - 1), 'standing just inside the doorway')
    kinds = sorted(k for _, k, _, _ in g.furn())
    c.eq(kinds, sorted([FK_BED, FK_TV, FK_DESK]), "Grandpa's bed, TV and diary desk")
    c.eq(g.u8('furn_init') & 1, 1, 'furniture placed once')
    g.go(4, 4)
    g.shot('inside')
    # TV: tomorrow's forecast + a tip
    x, y, d = g.furn_spot(FK_TV)
    g.go_face(x, y, d)
    g.w8('tomorrow', 1)
    g.act(); g.tick(80)
    g.shot('tv')
    g.dismiss()
    # window (back wall): weather line
    # buy a house plant and set it down in front of you
    g.w32('gold', 5000)
    c.eq(furn_build(g, 0, FK_PLANT), 1, 'furniture build mode on')
    g.go_face(6, 5, 'up')
    g.shot('furn_cursor')
    g.act(); g.tick(10)
    c.check(g.furn_at(6, 4) and g.furn_at(6, 4)[1] == FK_PLANT, 'plant placed at 6,4')
    c.eq(g.u32('gold'), 5000 - FURN_PRICE[FK_PLANT], 'plant price')
    g.act(); g.tick(10)
    c.eq(sum(1 for f in g.furn() if f[1] == FK_PLANT), 1, 'no second plant on an occupied spot')
    c.eq(g.u32('gold'), 5000 - FURN_PRICE[FK_PLANT], 'no charge for a blocked spot')
    # the doorway stays clear
    g.go_face(w[0], w[1] - 2, 'down')
    g.act(); g.tick(10)
    c.check(g.furn_at(w[0], w[1] - 1) is None, 'nothing placed on the step inside the door')
    # a wall piece only goes on bare back wall
    g.press('b'); g.tick(6)
    c.eq(g.sym8('_building'), 0, 'B leaves build mode')
    furn_build(g, 0, FK_PAINT)
    g.go_face(6, 2, 'up')
    g.act(); g.tick(10)
    c.check(g.furn_at(6, 1) and g.furn_at(6, 1)[1] == FK_PAINT, 'painting hung on the back wall')
    g.go_face(6, 5, 'down')
    g.act(); g.tick(10)
    c.check(g.furn_at(6, 6) is None, 'a painting refuses the floor')
    g.press('b'); g.tick(6)
    # move the plant
    furn_build(g, 1)
    g.go_face(6, 5, 'up'); g.act(); g.tick(10)
    g.go_face(7, 5, 'up'); g.act(); g.tick(10)
    c.check(g.furn_at(7, 4) and g.furn_at(7, 4)[1] == FK_PLANT, 'plant moved to 7,4')
    c.check(g.furn_at(6, 4) is None, 'old spot free')
    g.press('b'); g.tick(6)
    g.go(6, 5)
    g.shot('moved')
    # sell it back for half
    furn_build(g, 2)
    gold = g.u32('gold')
    g.go_face(7, 5, 'up'); g.act()
    g.yesno(True); g.tick(10)
    c.check(g.furn_at(7, 4) is None, 'plant sold')
    c.eq(g.u32('gold'), gold + FURN_PRICE[FK_PLANT] // 2, 'sold back for half')
    x, y, d = g.furn_spot(FK_BED)
    g.go_face(x, y, d); g.act(); g.tick(10)
    c.check(any(f[1] == FK_BED for f in g.furn()), 'the bed cannot be sold')
    g.press('b'); g.tick(6)
    g.dismiss()
    # diary desk saves (in the house), then the bed ends the day inside
    g.w32('gold', 4242)
    x, y, d = g.furn_spot(FK_DESK)
    g.go_face(x, y, d); g.act()
    g.yesno(True); g.tick(30)
    g.shot('diary')
    g.dismiss()
    day = g.u8('day')
    g.sleep(stay=True)
    g.shot('morning_inside')
    c.eq(g.u8('day'), day + 1, 'slept in the bed')
    c.check(g.in_house(), 'woke up inside')
    bx, by = [(f[2], f[3]) for f in g.furn() if f[1] == FK_BED][0]
    px, py = g.pos()
    c.check(min(abs(px - bx) + abs(py - y) for y in (by, by + 1)) <= 2, f'woke beside the bed ({g.pos()})')
    c.eq(g.u16('time'), 360, '6am')
    g.leave_house()
    c.eq(g.pos(), (hx + 1, hy + 3), 'out the door, one step below it')
    g.shot('outside')
    # the diary save: reload slot 1 -> back in the house with the saved gold
    boot_title(g)
    g.choose(1, 9, 2, 0)
    g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    c.check(g.in_house(), 'the morning save put us back in the house')
    c.eq(g.u8('day'), day + 1, 'morning save day')
    # a big house: the bigger interior, furniture kept
    g.w8('bld', (2 << 1), g.bld_find(B_HOUSE) * BLD_SIZE + 3)
    g.leave_house()
    g.enter_house()
    c.eq(g.u8('scene'), MAP_HOUSE2, 'the two-storey house has the big interior')
    c.eq(sorted(k for _, k, _, _ in g.furn()), sorted([FK_BED, FK_TV, FK_DESK, FK_PAINT]), 'furniture moved in')
    g.go(6, 6)
    g.shot('big_house')
    g.sleep(stay=True)
    c.eq(g.u8('scene'), MAP_HOUSE2, 'sleeping in the big house')


@scenario('interior_passout')
def s_interior_passout(c, g):
    """staying up past 2am inside: you collapse and wake on the farm at the door, half energy"""
    g.enter_house()
    g.w16('time', 1550)
    for _ in range(200):
        g.tick(10)
        if g.ui_open():
            break
    g.dismiss()
    for _ in range(100):
        g.tick(10)
        if g.u8('day') == 2:
            break
    g.after_night()
    c.eq(g.u8('day'), 2, 'next day')
    c.eq(g.u8('scene'), SCENE_FARM, 'woke on the farm')
    c.eq(g.u8('energy'), g.u8('energy_max') // 2, 'half energy')
    g.shot('morning')


def hall_hire(g, role, expect=True):
    g.go_face(16, 9, 'up'); g.act()
    g.choose(0, 0, HALL_N, 2)                    # HIRE HELP
    g.choose(0, 0, 4, role)
    g.shot(f'hire_{role}')
    if expect:
        g.yesno(True)
    g.tick(20)
    g.dismiss()


def wage(g, role):
    plots = g.plots_n()
    w = WAGE_BASE[role] + WAGE_PLOT[role] * plots
    soil = [i for i in range(1024) if g.u8('cell', i) == C_SOIL]
    if role == ROLE_WATER:
        w += len(soil) // 2
    elif role == ROLE_HARVEST:
        w += 2 * sum(1 for i in soil if g.u8('a1', i) & SOIL_CROP not in (0, CROP_WITHERED))
    elif role == ROLE_RANCH:
        w += 12 * len(g.animals())
    return w


@scenario('farmhands', base='cleared')
def s_farmhands(c, g):
    """hire at town hall, overnight work, wages, unpaid -> quit, fire from the STAFF page"""
    g.w32('gold', 20000)
    for x in range(8, 14):                       # a soil strip: one ripe rare radish, one unripe rare carrot
        g.setcell(x, 22, C_SOIL)
    g.setcell(8, 22, C_SOIL, 1, (R_RARE << 6) | CROP_DAYS[1])
    g.setcell(9, 22, C_SOIL, 2, (R_RARE << 6) | 1)
    g.setcell(10, 22, C_SOIL, 1, (R_LEGEND << 6) | CROP_DAYS[1])
    make_town(g)
    hall_hire(g, ROLE_WATER)
    c.check(g.u8('staff', ROLE_WATER) != 0, 'waterer hired')
    hall_hire(g, ROLE_HARVEST)
    c.check(g.u8('staff', ROLE_HARVEST) != 0, 'harvester hired')
    hall_hire(g, ROLE_WATER, expect=False)       # already hired: just a line, no double hire
    c.eq(sum(1 for r in range(NUM_ROLES) if g.u8('staff', r)), 2, 'two farmhands')
    to_farm(g)
    gold = g.u32('gold')
    wages = wage(g, ROLE_WATER) + wage(g, ROLE_HARVEST)
    g.w8('weather', 0); g.w8('tomorrow', 0)
    g.morning_shot = 'staff_report'
    g.sleep()
    wet = sum(1 for i in range(1024) if g.u8('cell', i) == C_SOIL and g.u8('a1', i) & SOIL_WET)
    soil = sum(1 for i in range(1024) if g.u8('cell', i) == C_SOIL)
    c.eq(wet, soil, 'waterer watered every tilled tile')
    c.eq(g.a1(8, 22) & SOIL_CROP, 0, 'harvester picked the ripe rare radish')
    c.eq(g.a1(9, 22) & SOIL_CROP, 2, 'an unripe RARE crop is left alone (rarity bits are not days)')
    c.eq(g.a2(9, 22) >> 6, R_RARE, 'the unripe crop keeps its rarity')
    shipped = 2 * ITEM_PRICE[I_RADISH]           # rare: 2, legendary: 3, quality rolls on top
    c.check(g.u32('gold') - (gold - wages) >= shipped + 3 * ITEM_PRICE[I_RADISH],
            f'rare + legendary harvest shipped 5+ radishes (net {g.u32("gold") - (gold - wages)})')
    # farmhands are out on the farm during the day
    staff = [a for a in g.actors() if a[0] == K_STAFF]
    c.eq(len(staff), 2, 'both farmhands visible on the farm')
    if staff:
        _, role, sx, sy = staff[0]
        for d, (dx, dy, _) in DIRS.items():
            if g.walkable(sx - dx, sy - dy):
                g.go_face(sx - dx, sy - dy, d)
                break
        if g.actor_at(sx, sy):
            g.act(); g.tick(60)
            g.shot('farmhand_talk')
            g.dismiss()
    # broke: one unpaid night, then they quit
    g.w32('gold', 0)
    g.sleep()
    c.eq(g.u8('staff_unpaid') & 3, 3, 'both worked unpaid')
    c.check(g.u8('staff', ROLE_WATER) and g.u8('staff', ROLE_HARVEST), 'still employed after one unpaid night')
    g.w32('gold', 0)
    g.morning_shot = 'quit_notes'
    g.sleep()
    c.eq((g.u8('staff', ROLE_WATER), g.u8('staff', ROLE_HARVEST)), (0, 0), 'they quit after a second unpaid night')
    c.eq(g.u8('staff_unpaid'), 0, 'unpaid bits cleared')
    # rehire, pay back pay, then fire from the STAFF page
    g.w32('gold', 20000)
    g.w16('time', 600)
    to_town(g)
    hall_hire(g, ROLE_GROUNDS)
    to_farm(g)
    g.sleep()
    g.w8('staff_unpaid', 1 << ROLE_GROUNDS)       # owes a night: back pay doubles tonight's wage
    gold = g.u32('gold')
    w = wage(g, ROLE_GROUNDS)
    g.sleep()
    c.eq(gold - g.u32('gold'), 2 * w, 'back pay: two nights of wages')
    open_start(g, 4)
    g.choose(8, 2, FARM_N, 5)                         # FARM > STAFF
    g.tick(30)
    g.shot('staff_page')
    g.press('a')
    g.yesno(True); g.tick(20)
    g.dismiss()
    c.eq(g.u8('staff', ROLE_GROUNDS), 0, 'fired from the STAFF page')


@scenario('rancher_sheep', base='cleared')
def s_rancher(c, g):
    """a rancher ships wool from sheep (not eggs), two from a rare breed"""
    g.w32('gold', 50000); g.w16('hay', 20)
    barn = g.bld_find(B_BARN)
    g.w8('bld', 0, barn * BLD_SIZE + 3)          # barn repaired
    # unfed overnight they lose 30 happiness before the rancher collects: 200 -> 170 is still silver quality
    g.set_anim(0, A_SHEEP, barn, happy=200, flags=AF_READY)
    g.set_anim(1, A_SHEEP, barn, happy=200, flags=AF_READY | (R_RARE << 6))
    g.set_anim(2, A_COW, barn, happy=200, flags=AF_READY)
    g.w8('staff', 1, ROLE_RANCH)
    gold = g.u32('gold')
    w = wage(g, ROLE_RANCH)
    g.sleep()
    c.eq(g.inv(I_EGG), 0, 'no eggs')
    paid = g.u32('gold') - (gold - w)
    want = 3 * ITEM_VALUE(I_WOOL, 1) + ITEM_VALUE(I_MILK, 1)
    c.eq(paid, want, 'rancher shipped 1 + 2 wool and a milk')
    c.check(all(a['flags'] & AF_FED for _, a in g.animals()), 'rancher fed everyone')


@scenario('businesses', base='town')
def s_businesses(c, g):
    """buy the bakery, stock it, overnight income, upgrade; the opened storefront art"""
    bx, by = BIZ_SPOT[0]
    door = (bx + 1, by + 3)
    g.go_face(*door, 'up')
    g.shot('vacant')
    before = bg_tiles(g)
    g.w32('gold', 1000)
    g.act()
    g.yesno(True)
    g.tick(20); g.dismiss()
    c.eq(g.u8('biz_owned'), 0, 'cannot buy without the gold')
    g.w32('gold', 30000)
    g.act()
    g.yesno(True)
    g.tick(30)
    g.shot('bought_dialog')
    g.dismiss()
    c.eq(g.u8('biz_owned') & 1, 1, 'bakery owned')
    c.eq(g.u32('gold'), 30000 - BIZ_PRICE[0], 'bakery price')
    g.tick(10)
    g.shot('opened')
    after = bg_tiles(g)
    c.check(before != after, 'the storefront was redrawn as an open shop')
    # stock 6 strawberries (basic: tarts), 2 eggs + 2 milk + 2 apples (premium: pies)
    g.w8('inv', 6, I_STRAWB * 3)
    g.act()
    g.choose(0, 0, BIZ_MENU_N, 0)                         # Stock
    g.choose(0, 0, 1, 0)
    g.quantity(6)
    g.tick(10)
    g.dismiss()
    pan = BIZ_PAN[0]
    c.eq(g.u8('biz_pantry', pan.index(I_STRAWB)), 6, '6 strawberries on the shelf')
    c.eq(g.inv(I_STRAWB), 0, 'taken from the bag')
    g.act()
    g.choose(0, 0, BIZ_MENU_N, 1)                         # Status
    g.tick(20); g.shot('status'); g.dismiss()
    to_farm(g)
    gold = g.u32('gold')
    g.morning_shot = 'biz_report'
    g.sleep()
    want = BIZ_CAP[0] * (ITEM_PRICE[I_STRAWB] * BIZ_BQ[0] // 4)
    c.eq(g.u16('biz_today', 0), want, f'six tarts at {BIZ_BQ[0] / 4}x')
    c.eq(g.u32('gold') - gold, want, 'income paid overnight')
    c.eq(g.u8('biz_sold', 0), 6, 'sold 6')
    c.eq(g.u8('biz_pantry', pan.index(I_STRAWB)), 0, 'pantry used up')
    # premium: one of each base (egg, milk) + a main
    for it, n in ((I_EGG, 1), (I_MILK, 1), (I_APPLE, 1)):
        g.w8('biz_pantry', n, pan.index(it))
    g.sleep()
    prem = (ITEM_PRICE[I_EGG] + ITEM_PRICE[I_MILK] + ITEM_PRICE[I_APPLE]) * BIZ_PQ[0] // 4
    c.eq(g.u16('biz_today', 0), prem, f'a pie: (egg + milk + apple) x {BIZ_PQ[0] / 4}')
    # upgrade
    g.w16('time', 600)
    to_town(g)
    gold = g.u32('gold')
    g.go_face(*door, 'up'); g.act()
    g.choose(0, 0, BIZ_MENU_N, 2)
    g.yesno(True); g.tick(20); g.dismiss()
    c.eq(g.u8('biz_level', 0), 1, 'bakery level 2')
    c.eq(gold - g.u32('gold'), BIZ_UP_PRICE[0], 'upgrade price')
    g.go(door[0], door[1] + 1)
    g.shot('bakery_lv2')
    # every other storefront opens too
    for b in (1, 2, 3):
        g.w32('gold', 100000)
        x, y = BIZ_SPOT[b]
        g.go_face(x + 1, y + 3, 'up'); g.act()
        g.yesno(True); g.tick(20); g.dismiss()
        c.check(g.u8('biz_owned') & (1 << b), f'business {b} bought')
        g.shot(f'biz_{b}')
    # an empty pantry is reported in the morning; nothing earned
    to_farm(g)
    g.sleep()
    c.eq(g.u16('biz_today', 1), 0, 'empty juice bar earns nothing')


@scenario('rarity_seeds', base='town')
def s_rarity_seeds(c, g):
    """rare packets cost 3x and yield 2; legendary is locked until LEGEND_EARNED is earned (or year 2), yields 3-4"""
    g.w32('gold', 50000)
    g.go_face(11, 4, 'up'); g.act()
    g.choose(0, 0, STORE_N, 0)
    g.choose(0, 0, 3, R_RARE)
    g.choose(0, 0, 4, 0)                          # radish+
    g.quantity(2); g.tick(10)
    c.eq(g.seeds(1, R_RARE), 2, 'bought 2 rare radish seeds')
    c.eq(g.u32('gold'), 50000 - 2 * SEED_COST[1] * RAR_MULT[R_RARE], 'rare costs 3x')
    g.dismiss()
    g.go_face(11, 4, 'up'); g.act()
    g.choose(0, 0, STORE_N, 0)
    g.choose(0, 0, 3, R_LEGEND)
    g.tick(40); g.shot('legend_locked')
    g.dismiss()
    c.eq(g.seeds(1, R_LEGEND), 0, 'legendary locked for a new farmer')
    g.w32('earned', LEGEND_EARNED)
    g.go_face(11, 4, 'up'); g.act()
    g.choose(0, 0, STORE_N, 0)
    g.choose(0, 0, 3, R_LEGEND)
    g.choose(0, 0, 4, 0)
    g.quantity(1); g.tick(10)
    g.dismiss()
    c.eq(g.seeds(1, R_LEGEND), 1, f'legendary radish seed after G{LEGEND_EARNED} earned')
    to_farm(g)
    # plant: the selected packet's rarity goes into the soil
    for x, rar in ((9, R_RARE), (10, R_LEGEND)):
        g.setcell(x, 14, C_SOIL)
        g.go_face(x, 13, 'down')
        g.w8('tool', T_SEEDS); g.w8('seed_sel', 1); g.w8('seed_rar', rar)
        g.act(); g.tick(10); g.dismiss()
        c.eq((g.a1(x, 14) & SOIL_CROP, g.a2(x, 14) >> 6), (1, rar), f'rarity {rar} radish planted')
    # rare packet empty -> falls back to commons
    g.setcell(11, 14, C_SOIL)
    g.go_face(11, 13, 'down'); g.w8('seed_rar', R_LEGEND); g.act(); g.tick(10); g.dismiss()
    c.eq(g.a2(11, 14) >> 6, R_COMMON, 'out of legendary: falls back to the commons')
    g.w8('a2', (R_RARE << 6) | (CROP_DAYS[1] - 1), 14 * 32 + 9)
    g.w8('a2', (R_LEGEND << 6) | (CROP_DAYS[1] - 1), 14 * 32 + 10)
    g.w8('a2', (R_COMMON << 6) | (CROP_DAYS[1] - 1), 14 * 32 + 11)
    for x in (9, 10, 11):
        g.w8('a1', g.a1(x, 14) | SOIL_WET, 14 * 32 + x)
    g.sleep()
    c.eq([g.a2(x, 14) & SOIL_DAYS for x in (9, 10)], [CROP_DAYS[1]] * 2, 'grew to ripe (days in the low bits)')
    clear_area(g, 9, 13, 11, 13)                  # overnight may have grown a bush on the path
    g.go(10, 13)
    g.shot('ripe_rare_legend')
    g.go_face(9, 13, 'down'); g.w8('tool', T_HAND); g.act(); g.tick(10); g.dismiss()
    c.eq(g.inv(I_RADISH), 2, 'rare radish yields 2')
    g.go_face(10, 13, 'down'); g.act(); g.tick(10); g.dismiss()
    c.check(g.inv(I_RADISH) - 2 in (3, 4), f'legendary yields 3-4 (got {g.inv(I_RADISH) - 2})')
    g.go_face(11, 13, 'down'); g.act(); g.tick(10); g.dismiss()
    c.eq(g.a1(11, 14) & SOIL_CROP, 0, 'harvested')


@scenario('breeds', base='cleared')
def s_breeds(c, g):
    """rare and golden breeds: 2 produce, golden always gold quality, palettes per breed"""
    g.w16('hay', 20)
    for b in (B_BARN, B_COOP):
        g.w8('bld', 0, g.bld_find(b) * BLD_SIZE + 3)
    barn, coop = g.bld_find(B_BARN), g.bld_find(B_COOP)
    g.set_anim(0, A_COW, barn, happy=100, flags=AF_READY, name='Moo')
    g.set_anim(1, A_COW, barn, happy=100, flags=AF_READY | (R_RARE << 6), name='Mocha')
    g.set_anim(2, A_HEN, coop, happy=100, flags=AF_READY | (R_LEGEND << 6), name='Sunny')
    g.go_face(21, 12, 'up'); g.act(); g.tick(20); g.dismiss()
    g.go_face(17, 11, 'up'); g.act(); g.tick(20); g.dismiss()
    c.eq(g.inv(I_MILK), 3, 'common cow 1 milk + Jersey 2')
    c.eq(g.inv(I_EGG, 2), 2, 'golden hen: 2 gold-star eggs')
    # palettes: walk out into a sunny morning with the animals about
    g.w8('weather', 0); g.w8('tomorrow', 0)
    g.sleep()
    g.go(20, 14)
    g.tick(30)
    pals = {}
    for k, oam in actor_slots(g, K_ANIMAL):
        aid = g.pb.memory[g.sym['_act_kind'] + MAX_ACT + k]
        pals[aid] = oam_attr(g, oam) & 7
    g.shot('breed_palettes')
    want = {0: SPAL_ANIMAL, 1: SPAL_BREEDRARE, 2: SPAL_BREEDGOLD}
    for aid, p in pals.items():
        c.eq(p, want[aid], f'animal {aid} palette')
    c.check(len(pals) >= 2, f'animals drawn ({len(pals)})')
    # store: breed menu + prices
    g.w32('gold', 100000)
    g.w16('time', 600)
    to_town(g)
    g.go_face(11, 4, 'up'); g.act()
    g.choose(0, 0, STORE_N, 3)
    g.wait_menu(0, 0, 3)
    g.shot('cow_breeds')
    g.choose(0, 0, 3, R_RARE)
    g.tick(40)
    g.dismiss()                                  # breed tip, name entry (START), welcome line, store
    jersey = [a for _, a in g.animals() if a['kind'] == A_COW and a['flags'] >> 6 == R_RARE]
    c.eq(len(jersey), 2, 'bought a Jersey')
    c.eq(g.u32('gold'), 100000 - BREED_PRICE[A_COW][R_RARE], 'Jersey price')
    g.go_face(11, 4, 'up'); g.act()
    g.choose(0, 0, STORE_N, 3)
    g.choose(0, 0, 3, R_LEGEND)
    g.tick(40); g.shot('golden_locked'); g.dismiss()
    c.eq(sum(1 for _, a in g.animals() if a['kind'] == A_COW), 3, 'golden cow locked for a new farmer')


@scenario('sheep_wool', base='cleared')
def s_sheep(c, g):
    """buy a sheep, fed every day it grows wool every WOOL_DAYS days; shear it in the field or at the barn"""
    g.w32('gold', 50000); g.w16('hay', 30)
    g.w8('bld', 0, g.bld_find(B_BARN) * BLD_SIZE + 3)   # the barn is repaired
    g.w16('time', 600)
    to_town(g)
    g.go_face(11, 4, 'up'); g.act()
    g.choose(0, 0, STORE_N, 5)
    g.choose(0, 0, 3, R_COMMON)
    g.tick(40); g.shot('name_sheep')
    g.dismiss()
    sheep = [k for k, a in g.animals() if a['kind'] == A_SHEEP]
    c.eq(len(sheep), 1, 'bought a sheep')
    c.eq(g.u32('gold'), 50000 - BREED_PRICE[A_SHEEP][R_COMMON], 'sheep price')
    to_farm(g)
    k = sheep[0]
    ready_day = None
    for day in range(1, WOOL_DAYS + 2):
        g.go_face(21, 12, 'up'); g.act(); g.tick(20); g.dismiss()   # feed (and collect)
        if g.inv(I_WOOL):
            break
        g.w8('weather', 1); g.w8('tomorrow', 1)   # rain keeps the animals in (no wandering)
        g.sleep()
        if g.anim(k)['flags'] & AF_READY and ready_day is None:
            ready_day = day
    c.eq(ready_day, WOOL_DAYS, f'wool ready after {WOOL_DAYS} fed days')
    c.eq(g.inv(I_WOOL), 1, 'collected the wool at the barn')
    # shear in the field
    g.w8('anim', g.anim(k)['flags'] | AF_READY, k * ANIM_SIZE + ANIM_OFF['flags'])
    g.w8('weather', 0)
    to_town(g); to_farm(g)
    for _ in range(10):
        s = [a for a in g.actors() if a[0] == K_ANIMAL]
        if not s:
            g.tick(30)
            continue
        _, _, sx, sy = s[0]
        for d, (dx, dy, _) in DIRS.items():
            if g.walkable(sx - dx, sy - dy):
                g.go(sx - dx, sy - dy); g.face(d)
                break
        if g.actor_at(sx, sy):
            g.act(); g.tick(10)
            g.shot('sheared')
            g.dismiss()
            break
    c.eq(g.inv(I_WOOL), 2, 'sheared in the field')
    c.check(not g.anim(k)['flags'] & AF_READY, 'fleece gone')


@scenario('breeding', base='cleared')
def s_breeding(c, g):
    """two happy adult cows in the barn have a calf overnight (forced by re-poking until it happens)"""
    g.w16('hay', 50)
    barn = g.bld_find(B_BARN)
    g.w8('bld', 0, barn * BLD_SIZE + 3)
    born = None
    PAIR = CONST['AF_PAIR']
    for night in range(4):                       # happy, adult, together, but never paired: nothing happens
        g.set_anim(0, A_COW, barn, happy=230, age=30, flags=AF_FED | (R_RARE << 6), name='Ma')
        g.set_anim(1, A_COW, barn, happy=230, age=30, flags=AF_FED | (R_LEGEND << 6), name='Pa')
        g.sleep(stay=True)
    c.eq(len(g.animals()), 2, 'no baby without pairing')
    for night in range(40):
        g.set_anim(0, A_COW, barn, happy=230, age=30, flags=AF_FED | PAIR | (R_RARE << 6), name='Ma')
        g.set_anim(1, A_COW, barn, happy=230, age=30, flags=AF_FED | PAIR | (R_LEGEND << 6), name='Pa')
        for k in range(2, MAX_ANIMALS):
            g.w8('anim', 0xFF, k * ANIM_SIZE)
        g.morning_shot = 'baby' if night == 0 else None
        g.sleep(stay=True)
        kids = [(k, a) for k, a in g.animals() if k >= 2]
        if kids:
            born = kids[0]
            break
    c.require(born is not None, 'no calf in 40 nights')
    k, a = born
    c.note(f'calf after {night + 1} nights, breed {a["flags"] >> 6}')
    c.eq((a['kind'], a['home'], a['age']), (A_COW, barn, 0), 'a newborn calf in the barn')
    c.check(a['flags'] >> 6 in (R_RARE, R_LEGEND), 'calf takes a parent breed (or better)')
    c.check(a['name'] != '', f'calf named ({a["name"]!r})')
    c.eq(g.u8('anim', k * ANIM_SIZE + ANIM_OFF['gen']), 1, 'first generation born here')
    c.check(g.anim(0)['flags'] & PAIR, 'the pair stays paired')
    # babies give nothing until grown
    g.w8('anim', AF_FED | (a['flags'] & 0xC0), k * ANIM_SIZE + ANIM_OFF['flags'])
    g.sleep(stay=True)
    c.check(not g.anim(k)['flags'] & AF_READY, 'a calf gives no milk')
    g.w8('anim', ADULT_AGE, k * ANIM_SIZE + ANIM_OFF['age'])
    g.w8('anim', AF_FED | (a['flags'] & 0xC0), k * ANIM_SIZE + ANIM_OFF['flags'])
    g.sleep()
    c.check(g.anim(k)['flags'] & AF_READY, 'grown up: milk')


# ------------------------------------------------------------------ animals & stable: barn/coop interiors, the horse
def fix_up(g, t):
    """repair a building of type t (test shortcut) and return its slot"""
    k = g.bld_find(t)
    g.w8('bld', g.bld(k)[3] & ~1, k * BLD_SIZE + 3)
    return k


def door_cell(g, k):
    """the farm cell of a building's door (bottom-middle; coop bottom-left)"""
    t, x, y, _ = g.bld(k)
    return (x if t == B_COOP else x + BW[t] // 2), y + BH[t] - 1


def walk_in(g, k, want):
    """stand below a building's door and walk into it"""
    dx, dy = door_cell(g, k)
    g.go_face(dx, dy + 1, 'up')
    g.hold('up', 12)
    g.wait_scene((want,))


def walk_out(g):
    wx, wy = map_warps(g.u8('scene'))[0][:2]
    g.go_face(wx, wy - 1, 'down')
    g.press('down', 4, 10)
    g.wait_scene((SCENE_FARM,))


def riding(g):
    return g.sym8('_riding')


@scenario('barn_inside', base='cleared')
def s_barn_inside(c, g):
    barn = fix_up(g, B_BARN)
    g.set_anim(0, A_COW, barn, happy=230, flags=AF_READY, name='Daisy')
    g.set_anim(1, A_SHEEP, barn, happy=150, flags=AF_READY | (R_RARE << 6), name='Wooly')
    g.set_anim(2, A_COW, barn, happy=90, age=3, flags=R_LEGEND << 6, name='Baby')
    g.w16('hay', 10)
    g.w8('weather', 1)                         # rain: everyone's inside
    to_town(g); to_farm(g)
    walk_in(g, barn, MAP_BARN)
    c.eq(g.u8('scene'), MAP_BARN, 'walked into the barn door')
    c.eq(g.u8('bld_in'), barn, 'remembers which barn')
    stalls = sorted((a[2], a[3]) for a in g.actors() if a[0] == K_ANIMAL)
    c.eq(stalls, [(1, 3), (2, 3), (3, 3)], 'three animals in the first three stalls')
    g.shot('barn_rain')
    # milk the cow in stall 1
    g.go_face(1, 4, 'up'); g.act(); g.tick(30)
    g.shot('cow_panel')
    c.check(g.ui_open(), 'animal panel open')
    g.dismiss()
    c.eq(g.inv(I_MILK, 2), 1, 'milked the cow (gold-star: happy)')
    c.check(g.anim(0)['flags'] & AF_PET, 'petted')
    # shear the rare sheep
    g.go_face(2, 4, 'up'); g.act(); g.tick(30); g.dismiss()
    c.eq(g.inv(I_WOOL), 2, 'sheared two wool from a rare sheep')
    g.go_face(3, 4, 'up'); g.act(); g.tick(30)
    g.shot('calf_panel')
    g.dismiss()
    c.eq(g.inv(I_MILK), 1, 'a calf gives no milk')
    # fill the mangers from the hay stack
    g.go_face(8, 4, 'up'); g.act(); g.tick(20); g.dismiss()
    c.eq(g.u16('hay'), 7, 'three hay into the mangers')
    c.check(all(g.anim(k)['flags'] & AF_FED for k in range(3)), 'everyone fed')
    g.go(4, 5)
    g.shot('mangers_full')
    g.go_face(8, 4, 'up'); g.act(); g.tick(20); g.dismiss()
    c.eq(g.u16('hay'), 7, 'no hay used twice')
    walk_out(g)
    dx, dy = door_cell(g, barn)
    c.eq(g.pos(), (dx, dy + 1), 'back outside the barn door')
    # every building style has its own walls and floor; at noon in the sun the animals are out
    g.w8('weather', 0); g.w16('time', 720)
    for st in range(4):
        g.w8('style', st)
        walk_in(g, barn, MAP_BARN)
        if st == 0:
            c.eq([a for a in g.actors() if a[0] == K_ANIMAL], [], 'sunny noon: the barn is empty, everyone is out')
        g.go(5, 5)
        g.shot(f'style_{st}')
        walk_out(g)
    g.w8('style', 0)
    # A at the door still does the quick chores from outside
    g.w8('anim', 0, ANIM_OFF['flags'])
    g.go_face(dx, dy + 1, 'up'); g.act(); g.tick(20); g.dismiss()
    c.check(g.anim(0)['flags'] & AF_FED, 'door chores still feed')


@scenario('coop_inside', base='cleared')
def s_coop_inside(c, g):
    coop = fix_up(g, B_COOP)
    g.set_anim(0, A_HEN, coop, happy=100, flags=AF_READY, name='Penny')
    g.set_anim(1, A_HEN, coop, happy=210, flags=R_LEGEND << 6, name='Sunny')
    g.w16('hay', 5)
    g.w16('time', 1140)                        # evening: the hens are in
    to_town(g); to_farm(g)
    walk_in(g, coop, MAP_COOP)
    c.eq(g.u8('scene'), MAP_COOP, 'walked into the coop')
    hens = [a for a in g.actors() if a[0] == K_ANIMAL]
    c.eq(len(hens), 2, 'both hens inside in the evening')
    g.shot('coop_evening')
    # Penny's nest (box 1) has an egg: collect it from the box
    g.go_face(1, 3, 'up'); g.act(); g.tick(20)
    g.shot('egg_collected')
    g.dismiss()
    c.eq(g.inv(I_EGG, 0), 1, 'collected an egg from the nest box')
    c.check(not g.anim(0)['flags'] & AF_READY, 'nest empty now')
    # fill the trough (cells 6..7, row 5)
    g.go_face(6, 4, 'down'); g.act(); g.tick(20); g.dismiss()
    c.eq(g.u16('hay'), 3, 'two hay into the trough')
    c.check(all(g.anim(k)['flags'] & AF_FED for k in range(2)), 'both hens fed')
    g.go(3, 6)
    g.shot('trough_full')
    # a hen's panel
    for _ in range(10):
        h = [a for a in g.actors() if a[0] == K_ANIMAL and a[1] == 1]
        if not h:
            break
        _, _, hx, hy = h[0]
        for d, (ddx, ddy, _) in DIRS.items():
            if g.walkable(hx - ddx, hy - ddy):
                g.go(hx - ddx, hy - ddy); g.face(d)
                break
        if g.actor_at(hx, hy):
            g.act(); g.tick(30)
            break
    g.shot('hen_panel')
    c.check(g.ui_open(), 'hen panel open')
    g.dismiss()
    c.check(g.anim(1)['flags'] & AF_PET, 'petted Sunny')
    walk_out(g)
    dx, dy = door_cell(g, coop)
    c.eq(g.pos(), (dx, dy + 1), 'back out at the coop door')
    # a save made inside the coop loads back inside
    walk_in(g, coop, MAP_COOP)
    c.eq(g.u8('bld_in'), coop, 'bld_in set for the save')


@scenario('horse', base='cleared')
def s_horse(c, g):
    g.rich()
    g.w16('time', 600)
    build_poke(g, 4, 5)                        # BUILDINGS > Stable
    g.go_face(20, 12, 'down'); g.act()
    g.yesno(True); g.tick(20); g.dismiss()
    g.press('b'); g.tick(4)
    st = g.bld_find(B_STABLE)
    c.require(st is not None, 'stable built')
    g.go_face(20, 12, 'down'); g.act(); g.tick(10)
    g.shot('stable_empty'); g.dismiss()
    to_town(g)
    g.go_face(11, 4, 'up'); g.act()
    g.wait_menu(0, 0, STORE_N + 1)
    g.shot('store_horse')
    gold = g.u32('gold')
    g.choose(0, 0, STORE_N + 1, 6)             # Horse
    g.choose(0, 0, 3, 1)                       # Chestnut
    for _ in range(60):
        if g.name_entry_open():
            break
        g.tick(5)
    g.shot('name_horse')
    g.press('start'); g.tick(20)
    g.wait_menu(0, 0, STORE_N, advance='a')
    g.dismiss()
    c.eq(g.u8('horse'), 2, 'bought a chestnut horse')
    c.eq(g.u32('gold'), gold - 9000, 'paid G9000')
    name = bytes(g.u8('horse_name', i) for i in range(6)).split(b'\0')[0]
    c.eq(name, b'Rusty', 'default name kept')
    to_farm(g)
    hs = [a for a in g.actors() if a[0] == K_HORSE]
    c.require(len(hs) == 1, 'the horse waits by the stable')
    _, _, hx, hy = hs[0]
    _, sx, sy, _ = g.bld(st)
    c.check(abs(hx - (sx + 1)) <= 3 and abs(hy - (sy + 2)) <= 3, f'horse near the stable ({hx},{hy})')
    # brush, then ride
    def face_horse():
        for _ in range(12):                    # it wanders a little: chase it
            _, _, hx, hy = [a for a in g.actors() if a[0] == K_HORSE][0]
            for d, (ddx, ddy, _) in DIRS.items():
                if g.walkable(hx - ddx, hy - ddy):
                    g.go(hx - ddx, hy - ddy); g.face(d)
                    break
            h = g.actor_at(*{'up': (g.pos()[0], g.pos()[1] - 1), 'down': (g.pos()[0], g.pos()[1] + 1),
                             'left': (g.pos()[0] - 1, g.pos()[1]), 'right': (g.pos()[0] + 1, g.pos()[1])}[DIRNAME[g.u8('facing')]])
            if h and h[0] == K_HORSE:
                return h[2], h[3]
            g.tick(20)
        raise Fail('cannot reach the horse')
    love = g.u8('horse_love')
    face_horse(); g.act()
    g.choose(12, 3, 4, 1); g.tick(10)
    c.check(g.u8('horse_day') & 1, 'brushed today')
    c.eq(g.u8('horse_love'), love + 8, 'brushing: +8 friendship')
    g.dismiss()
    hx, hy = face_horse(); g.act()
    g.shot('horse_menu')
    g.choose(12, 3, 4, 0); g.tick(10)
    g.shot('giddy_up')
    g.dismiss()
    c.eq(riding(g), 1, 'in the saddle')
    c.eq(g.pos(), (hx, hy), 'mounted onto the horse')
    c.check(g.u8('horse_day') & 2, 'ridden today')
    # speed: hold a direction 32 frames riding vs walking (open ground south of the stable: rows 15-17)
    g.symw8('_riding', 0)
    g.go(17, 16)
    g.symw8('_riding', 1)
    g.face('right')
    g.shot('ride_right')
    x0 = g.pos()[0]
    g.hold('right', 16); g.tick(10)
    ride = g.pos()[0] - x0
    g.shot('ride_moved')
    g.face('down'); g.tick(4); g.shot('ride_down')
    g.face('up'); g.tick(4); g.shot('ride_up')
    g.face('left'); g.tick(4)
    g.go(17, 16)
    g.press('b'); g.tick(10)
    c.eq(riding(g), 0, 'B climbs down')
    c.check(any(a[0] == K_HORSE and abs(a[2] - 17) + abs(a[3] - 16) <= 1 for a in g.actors()),
            'horse left where you got off')     # (it may already have taken a step)
    g.go(17, 17); g.face('right')
    x0 = g.pos()[0]
    g.hold('right', 16); g.tick(10)
    walk = g.pos()[0] - x0
    c.note(f'16 frames: riding {ride} cells, walking {walk} cells')
    c.check(ride >= 2 * walk and ride >= 4, f'riding is twice walking speed ({ride} vs {walk})')
    # A dismounts too
    hx, hy = face_horse(); g.act(); g.choose(12, 3, 4, 0); g.tick(10); g.dismiss()
    c.eq(riding(g), 1, 'mounted again')
    g.act(); g.tick(10)
    c.eq(riding(g), 0, 'A climbs down')
    # friends + farm pages
    open_start(g, 4); g.choose(8, 2, FARM_N, 3); g.tick(20)
    g.shot('friends_page'); g.dismiss()
    open_start(g, 4); g.choose(8, 2, FARM_N, 0); g.tick(20)
    g.shot('farm_page'); g.dismiss()
    # off to town: the horse waits at the stable when you come back
    hx, hy = face_horse(); g.act(); g.choose(12, 3, 4, 0); g.tick(10); g.dismiss()
    to_town(g)
    c.eq(riding(g), 0, 'leaving the farm dismounts')
    to_farm(g)
    _, _, hx, hy = [a for a in g.actors() if a[0] == K_HORSE][0]
    c.check(abs(hx - (sx + 1)) <= 3 and abs(hy - (sy + 2)) <= 3, 'back by the stable')
    # friendship grows day by day
    g.w8('horse_day', 3)
    g.sleep()
    c.eq(g.u8('horse_day'), 0, 'a new day: brush and ride again')


def legacy_bytes(g, version):
    """the current game as a v3/v4 save: magic, version, the home valley as the old 32x32 world (cell, a1,
    a2), the old 16-bit plot mask, then GameState from bld on (v4: up to seed_rar; v3: 39 items, seeds
    without rarity, up to rng_hi), with farm coordinates back in the old frame, and the checksum"""
    raw = bytearray(g.pb.memory[g.G + i] for i in range(GSIZE))
    for k in range(MAX_BLD):
        b = OFF['bld'] + k * BLD_SIZE
        if raw[b] != 0xFF:
            raw[b + 1] -= FO; raw[b + 2] -= FO
    if raw[OFF['scene']] == SCENE_FARM:
        raw[OFF['px']] -= FO; raw[OFF['py']] -= FO
    for k in range(12):                              # where old saves kept them, 11 bytes each
        raw[OFF['anim_v5'] + k * 11:OFF['anim_v5'] + k * 11 + 11] = raw[OFF['anim'] + k * ANIM_SIZE:OFF['anim'] + k * ANIM_SIZE + 11]
    world = bytes(g.cell(i % 32, i // 32) for i in range(1024)) + bytes(g.a1(i % 32, i // 32) for i in range(1024)) + \
        bytes(g.a2(i % 32, i // 32) for i in range(1024))
    plots = g.u16('plots')
    old = bytearray([SAVE_MAGIC & 0xFF, SAVE_MAGIC >> 8, version]) + world + bytes([plots & 0xFF, plots >> 8])
    bld = OFF['bld']
    inv, ship = OFF['inv'], OFF['shipped']         # old saves: 40 items (v3: 39) and a 6-byte collection log
    if version == 4:
        old += raw[bld:inv + 40 * 3] + raw[OFF['seeds']:ship + 6] + raw[OFF['goals']:OFF['seed_rar'] + 1]
    else:
        old += raw[bld:inv + 39 * 3]
        old += bytes(raw[OFF['seeds'] + k * NUM_RARITY] for k in range(NUM_CROPS + 1))
        old += raw[OFF['saplings']:ship + 6] + raw[OFF['goals']:OFF['furn_init']]
    s = 0x1234
    for b in old:
        s = (((s << 1) | (s >> 15)) + b) & 0xFFFF
    return bytes(old) + bytes([s & 0xFF, s >> 8])


def v3_bytes(g):
    return legacy_bytes(g, 3)


def put_save(g, data):
    """write a raw save image into the slot header (SRAM bank 0) of the battery file, then boot"""
    g.stop()
    ram = g.rom + '.ram'
    sav = bytearray(open(ram, 'rb').read()) if os.path.exists(ram) else bytearray(RAM_SIZE)
    sav += bytes(RAM_SIZE - len(sav))
    sav[0:len(data)] = data
    open(ram, 'wb').write(bytes(sav))
    g.boot()


@scenario('save_v4_migrate')
def s_save_v4(c, g):
    """a v4 save (32x32 world inside GameState) carried into the 64x64 valley: the old farm is the middle"""
    g.w32('gold', 4242)
    g.w16('stone', 55)
    g.w8('friend', 77, NPC_ROSA)
    g.w8('inv', 9, I_WOOL * 3)
    g.setcell(10, 14, C_SOIL, 3 | SOIL_WET, (R_RARE << 6) | 2)       # a watered rare strawberry, 2 days in
    g.setcell(11, 14, C_DECOR, DK_SCARECROW, 0)
    g.setcell(9, 25, C_TREE, 2 << 5, 40)                              # in old plot 13 (bought below)
    g.setcell(2, 2, C_DECOR, DK_STATUE, 0)                            # in old plot 0 (not owned: dropped)
    old_plots = 0x0660 | (1 << 13)
    data = bytearray(legacy_bytes(g, 4))
    data[3 + 3072] = old_plots & 0xFF; data[3 + 3073] = old_plots >> 8
    s = 0x1234
    for b in data[:-2]:
        s = (((s << 1) | (s >> 15)) + b) & 0xFFFF
    data[-2], data[-1] = s & 0xFF, s >> 8
    house = g.bld(g.bld_find(B_HOUSE))
    pos = g.pos()
    put_save(g, bytes(data))
    title_start(g)
    g.choose(1, 9, 2, 0)
    g.shot('slot_menu')
    g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    g.shot('migrated')
    c.eq(g.u8('version'), SAVE_VERSION, 'upgraded to the current version')
    c.eq(g.u32('gold'), 4242, 'gold')
    c.eq(g.u16('stone'), 55, 'stone')
    c.eq(g.u8('friend', NPC_ROSA), 77, 'friendship')
    c.eq(g.inv(I_WOOL), 9, 'wool (the last item)')
    c.eq(g.u16('plots'), old_plots, 'old plots map onto the middle of the 8x8 grid')
    c.eq(g.plots_n(), 5, 'five plots owned in all')
    c.check(g.plot_owned(OLD_PLOT(13)) and g.plot_owned(OLD_PLOT(5)), 'South Pasture and the home field owned')
    c.eq((g.cell(10, 14), g.a1(10, 14), g.a2(10, 14)), (C_SOIL, 3 | SOIL_WET, (R_RARE << 6) | 2), 'the crop moved with its farm')
    c.eq((g.cell(11, 14), g.a1(11, 14)), (C_DECOR, DK_SCARECROW), 'scarecrow')
    c.eq((g.cell(9, 25), g.a1(9, 25), g.a2(9, 25)), (C_TREE, 2 << 5, 40), 'fruit tree on the bought plot')
    c.check(g.cell(2, 2) != C_DECOR, 'an unowned old plot is the new valley, not the old cells')
    c.eq(g.bld(g.bld_find(B_HOUSE)), house, 'house where it was (in the home-valley frame)')
    c.eq(g.wcell(*[v + FO for v in (house[1], house[2])]), C_STRUCT, 'house cells at the shifted spot')
    c.eq(g.pos(), pos, 'player where they saved')
    c.eq(g.wcell(46, 0), C_WATER, 'the river runs out of the top of the valley')
    g.go(10, 13)
    g.shot('walked')
    # the next save is a v5 one, and it loads back with the farm intact
    open_start(g, 6)
    g.yesno(True); g.tick(20); g.dismiss()
    w = g.world()
    boot_title(g)
    g.choose(1, 9, 2, 0)
    g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    c.eq(g.world(), w, 'the migrated world saved and loaded back byte for byte')
    c.eq(g.u32('gold'), 4242, 'gold after the round trip')


@scenario('save_v5_migrate')
def s_save_v5(c, g):
    """a real v5 battery file (written by the last v5 build: 12 animals, year 3) loads into the v6 game, which
    keeps its animals in a longer list at the end of the save"""
    g.stop()
    shutil.copy(os.path.join(ROOT, 'tools/fixtures/v5_twelve_animals.ram'), g.rom + '.ram')
    g.boot()
    title_start(g)
    g.shot('title_v5')
    g.choose(1, 9, 2, 0)
    g.choose(8, 4, 3, 0)                # Continue
    g.tick(60); g.dismiss(); g.wait_field()
    c.eq(g.u8('version'), SAVE_VERSION, 'upgraded to the current version')
    c.eq((g.u32('gold'), g.u8('year')), (54321, 3), 'gold and year carried over')
    names = ['Daisy', 'Mocha', 'Goldy', 'Wooly', 'Cocoa', 'Midas', 'Penny', 'Hazel', 'Sunny', 'Hen10', 'Hen11', 'Hen12']
    got = g.animals()
    c.eq([a['name'] for _, a in got], names, 'all twelve animals, in order')
    c.eq([(a['kind'], a['home'], a['happy'], a['age'], a['flags'] >> 6) for _, a in got],
         [(0 if k < 3 else 2 if k < 6 else 1, 1 if k < 6 else 2, 100 + k, 20 + k, k % 3) for k in range(12)], 'kind, home, mood, age, breed')
    c.eq(len(got), 12, 'and the new slots are empty')
    w = g.world()
    open_start(g, 6); g.yesno(True); g.tick(20); g.dismiss()      # the next save is a v6 one
    boot_title(g)
    g.choose(1, 9, 2, 0); g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    c.eq([a['name'] for _, a in g.animals()], names, 'the v6 save loads back with the same animals')
    c.eq(g.world(), w, 'and the same world')


@scenario('animals_more_homes')
def s_more_homes(c, g):
    """a second coop means six more hens: the 13th animal used to be refused however many barns you owned"""
    for b in (B_BARN, B_COOP):
        g.w8('bld', 0, g.bld_find(b) * BLD_SIZE + 3)             # Grandpa's barn and coop, repaired
    barn, coop = g.bld_find(B_BARN), g.bld_find(B_COOP)
    for k in range(12):
        g.set_anim(k, 0 if k < 6 else 1, barn if k < 6 else coop, name='A%d' % k)
    free = next(k for k in range(MAX_BLD) if g.bld(k)[0] == 0xFF)
    for j, v in enumerate((B_COOP, 30, 30, 0)):
        g.w8('bld', v, free * BLD_SIZE + j)                      # a new coop (the shop only looks at the list)
    g.w32('gold', 50000); g.w16('time', 600)
    to_town(g)
    g.go_face(11, 4, 'up'); g.act()
    for n in range(6):
        g.choose(0, 0, STORE_N, 4)          # hen
        g.choose(0, 0, 3, R_COMMON)
        g.tick(40); g.press('start'); g.tick(20)
        g.wait_menu(0, 0, STORE_N)
    hens = [a for _, a in g.animals() if a['home'] == free]
    c.eq((len(g.animals()), len(hens)), (18, 6), 'six more hens, all in the new coop')
    g.shot('eighteen')
    g.choose(0, 0, STORE_N, 4); g.choose(0, 0, 3, R_COMMON)       # a 7th: that coop is full
    g.tick(30); g.shot('coop_full'); g.dismiss()
    c.eq(len(g.animals()), 18, 'a full coop still refuses')


@scenario('late_contracts', base='town')
def s_contracts(c, g):
    """year 2 on: the request board posts three bulk orders a season; they fill in parts and pay 3x"""
    g.go_face(13, 9, 'up')
    g.act(); g.tick(20); g.dismiss()
    c.eq(g.u8('con_id'), 0, 'no contracts in year 1')
    g.w8('year', 3); g.w8('season', 1); g.w32('gold', 0); g.w32('earned', 0)
    g.go_face(13, 9, 'up'); g.act()
    g.wait_menu(0, 0, 4)
    g.shot('contracts')
    items, need = [g.u8('con_item', k) for k in range(3)], [g.u8('con_need', k) for k in range(3)]
    c.eq(g.u8('con_id'), 3 * 4 + 1 + 1, 'posted for summer of year 3')
    c.eq(need, [45, 15, 9], 'year 3 sizes')
    c.check(items[0] in (I_TOMATO, I_CORN, I_MELON), f'a summer crop ({items[0]})')
    # part of the first order now, the rest later
    g.w8('inv', 20, items[0] * 3)
    g.choose(0, 0, 4, 0); g.yesno(True)
    g.wait_menu(0, 0, 4)
    c.eq((g.u8('con_have', 0), g.u8('inv', items[0] * 3), g.u32('gold')), (20, 0, 0), '20 handed over, nothing paid yet')
    g.shot('part_filled')
    pay = [ITEM_PRICE[items[k]] * need[k] * 3 for k in range(3)]
    for k in range(3):
        g.w8('inv', 200, items[k] * 3)
        g.choose(0, 0, 4, k); g.yesno(True)
        g.wait_menu(0, 0, 4)
        c.eq(g.u8('con_have', k), need[k], f'order {k} full')
    c.eq(g.u8('inv', items[0] * 3), 200 - 25, 'only what was still wanted is taken')
    c.eq(g.u8('con_stars'), 1, 'all three: a contract star')
    c.eq(g.u32('gold'), sum(pay) + 5000 * 2, 'three orders at 3x the price + the star bonus')
    g.shot('all_filled')
    g.dismiss()
    # a new season posts a new, larger set (a star and a year both raise the tier)
    g.w8('season', 2); g.w8('year', 4); g.w8('con_stars', 2)
    g.go_face(13, 9, 'up'); g.act()
    g.wait_menu(0, 0, 4)
    c.eq([g.u8('con_need', k) for k in range(3)], [30 + 15 * 3, 10 + 5 * 3, 6 + 3 * 3], 'bigger orders')
    c.eq([g.u8('con_have', k) for k in range(3)], [0, 0, 0], 'fresh')
    g.dismiss()


@scenario('late_town_fund', base='town')
def s_fund(c, g):
    """past prosperity 5 Joe's Town Fund takes gold for ever-dearer levels, each adding to shop takings"""
    g.w32('gold', 120000)
    g.go_face(16, 9, 'up'); g.act()
    g.choose(0, 0, HALL_N, HALL_N - 2)                # INVEST
    g.choose(0, 0, 5, 3)                               # Town Fund: closed below five stars
    g.wait_menu(0, 0, 5)
    c.eq((g.u8('fund_lvl'), g.u32('gold')), (0, 120000), 'closed before prosperity 5')
    g.w8('biz_owned', 0xFF); g.w8('mk_flags', MK_OPEN | 0x0F); g.w8('town_proj', 0x3F)
    for b in range(8):
        g.w8('biz_look', g.u8('biz_look', b) | 0x0C, b)
    c.eq(prosperity_of(g), 5, 'five stars')
    g.choose(0, 0, 5, 3)
    g.yesno(True)                                       # G100,000: level 1
    g.tick(20); g.shot('fund_level_1')
    g.yesno(True)                                       # the last G20,000 toward level 2
    g.wait_menu(0, 0, 5)
    c.eq((g.u8('fund_lvl'), g.u32('fund_paid'), g.u32('gold')), (1, 20000, 0), 'level 1, and 20,000 toward the next')
    c.eq(g.u32('invested'), 120000, 'counted as invested')
    g.choose(0, 0, 5, 0); g.tick(20); g.shot('overview_fund'); g.press('a'); g.tick(10)
    g.dismiss()


@scenario('late_review')
def s_late_review(c, g):
    """Grandpa's review comes every spring, compares with last year's, and its gift grows with the years"""
    g.w8('year', 3); g.w8('season', 3); g.w8('day', 28)
    g.w8('review_year', 3); g.w16('review_score', 5); g.w8('fund_lvl', 2); g.w8('con_stars', 4)
    g.sleep()
    c.eq((g.u8('year'), g.u8('review_year')), (4, 4), 'spring of year 4')
    c.check(g.u8('daily') <= 4 and g.u8('daily_flags') == 0, 'the night rolled a daily surprise (or none)')
    c.check(g.u32('mail') & (1 << 31), 'the review is in the mailbox again')
    g.w32('mail', 1 << 31)
    clear_area(g, 8, 8, 14, 8)
    gold = g.u32('gold')
    g.go_face(13, 9, 'left'); g.act()
    g.tick(60); g.shot('review')
    g.dismiss()
    score = g.u16('review_score')
    c.check(score >= 2 * 5 + 4 * 3, f'score {score} counts the fund and the stars')
    c.eq(g.u32('gold'), gold + score * 20 * 3, 'gift = score x 20 x (year - 1)')
    c.eq(g.u32('mail'), 0, 'read')
    g.sleep()
    c.check(not g.u32('mail') & (1 << 31), 'once a year')


@scenario('museum', base='town')
def s_museum(c, g):
    """the Library's collection: donate one of each good, watch the exhibits fill, a gift per finished group"""
    g.w8('mk_flags', MK_OPEN); g.w8('town_proj', 0x3F); g.w8('inv_tips', g.u8('inv_tips') | 4)   # library built, gift had
    re_enter_town(g)
    to_market(g, 13, 27)
    g.w8('inv', 2, I_RADISH * 3); g.w8('inv', 1, I_KOI * 3 + 2)
    g.w32('gold', 0)
    g.go(21, 31); g.go_face(21, 31, 'up'); g.act()
    g.wait_menu(0, 0, 4)
    g.shot('library_menu')
    g.choose(0, 0, 4, 1)                              # Donate: radish, koi, Back
    g.wait_menu(0, 0, 3)
    g.shot('donate_list')
    g.choose(0, 0, 3, 0)
    g.wait_menu(0, 0, 2)                              # the radish is in; only the koi is new now
    c.eq((g.u8('donated', 0) & 1, g.inv(I_RADISH)), (1, 1), 'one radish donated, one kept')
    g.choose(0, 0, 2, 0)
    g.wait_menu(0, 0, 4)                              # nothing left to give: back at the desk
    c.check(g.u8('donated', I_KOI >> 3) & (1 << (I_KOI & 7)), 'the koi too')
    c.eq(g.u32('gold'), 0, 'no gift yet')
    # the last crop finishes the group
    for it in range(1, 13):
        g.w8('donated', g.u8('donated', it >> 3) | (1 << (it & 7)), it >> 3)
    g.w8('inv', 1, I_SNOWPEA * 3)
    g.choose(0, 0, 4, 1); g.choose(0, 0, 2, 0)
    g.wait_menu(0, 0, 4)
    c.eq(g.u32('gold'), 1500 * 14, 'every crop: the group gift')
    c.eq(g.u16('museum_sets'), 1, 'thanked once')
    g.choose(0, 0, 4, 2); g.tick(20); g.shot('exhibits'); g.press('right'); g.tick(10); g.shot('exhibits_2'); g.press('a'); g.tick(10)
    g.choose(0, 0, 4, 3)
    g.dismiss()


@scenario('daily_surprise', base='town')
def s_daily(c, g):
    """one small surprise most mornings: Mike's wagon under the west tent, a bottle on the beach"""
    g.go_face(10, 8, 'up'); g.act(); g.tick(20); g.shot('empty_tent'); g.dismiss()
    g.w8('daily', CONST['D_TRADER']); g.w8('daily_arg', 0x15); g.w8('daily_flags', 0); g.w32('gold', 90000)
    g.go_face(10, 8, 'up'); g.act()
    g.wait_menu(0, 0, 4)
    g.shot('trader')
    held = sum(g.inv(it) for it in range(NUM_ITEMS))
    g.choose(0, 0, 4, 1)
    g.wait_menu(0, 0, 4)
    paid = 90000 - g.u32('gold')
    got = [it for it in range(NUM_ITEMS) if g.inv(it)]
    c.eq(sum(g.inv(it) for it in range(NUM_ITEMS)), held + 1, 'one far-off good')
    c.check(got and paid == ITEM_PRICE[got[-1]] * 4, f'at four times its price ({paid})')
    g.choose(0, 0, 4, 1)                              # one of each
    g.wait_menu(0, 0, 4)
    c.eq(90000 - g.u32('gold'), paid, 'the same thing is not sold twice')
    seeds = sum(g.u8('seeds', k) for k in range((NUM_CROPS + 1) * NUM_RARITY))
    g.choose(0, 0, 4, 0)
    g.wait_menu(0, 0, 4)
    c.eq(sum(g.u8('seeds', k) for k in range((NUM_CROPS + 1) * NUM_RARITY)), seeds + 5, 'five rare seeds')
    g.choose(0, 0, 4, 3); g.dismiss()
    # a bottle on the beach
    g.w8('daily', CONST['D_BOTTLE']); g.w8('daily_arg', 0x10); g.w8('daily_flags', 0)
    gold = g.u32('gold')
    beach_to(g)
    g.go(10, 7); g.tick(10); g.shot('bottle')
    g.go_face(10, 7, 'up'); g.act(); g.tick(30); g.dismiss()
    c.eq((g.u32('gold'), g.u8('daily_flags') & 1), (gold + 300 + 0x10 * 40, 1), 'old coins in the bottle')
    g.go(10, 6)
    c.eq(g.pos(), (10, 6), 'and the sand is clear again')


def wood_item(k, stamp, taken, season):
    """areas.c wood_item, mirrored"""
    def mix(v):
        v ^= (v << 3) & 0xFF; v ^= v >> 5; v = (v + 0x9B) & 0xFF; v ^= (v << 2) & 0xFF
        return v
    if taken & (1 << k):
        return None
    h = mix(mix((stamp + k * 53 + 4 * 91) & 0xFF) ^ ((k << 4) & 0xFF))
    if (h & 3) < (2 if season == 3 else 1):
        return None
    h >>= 2
    if season == 2:
        return I_MUSHROOM if h < 36 else I_BERRIES if h < 52 else I_HERB
    if season == 3:
        return I_MUSHROOM if h < 40 else I_HERB
    return I_BERRIES if h < 26 else I_HERB if h < 46 else I_MUSHROOM


@scenario('old_woods', base='town')
def s_woods(c, g):
    """east out of town along Main Street: the Old Woods. Forage that grows back each morning, the lily pond,
    the hollow log; and the way home"""
    MAP_WOODS = CONST['MAP_WOODS']
    spots = list(zip(_ints(_table(_areas_c, 'wd_x')), _ints(_table(_areas_c, 'wd_y'))))
    g.w8('energy', 200)
    g.go(30, 4); g.step('right')
    g.wait_scene((MAP_WOODS,))
    g.tick(20); g.dismiss()
    c.eq(g.pos(), (1, 8), 'arrive on the woods path')
    g.shot('arrive')
    stamp = g.u8('area_stamp')
    finds = [(k, wood_item(k, stamp, 0, 0)) for k in range(7) if wood_item(k, stamp, 0, 0) is not None]
    c.check(len(finds) >= 3, f'things to pick ({len(finds)})')
    for k, it in finds:
        x, y = spots[k]
        n0 = g.inv(it)
        g.go_face(x, y + 1, 'up'); g.act(); g.tick(20); g.dismiss()
        c.check(g.inv(it) > n0, f'picked item {it} at {x},{y}')
    c.eq(g.u8('area_taken', 4) & 0x7F, sum(1 << k for k, _ in finds), 'all marked taken')
    g.go(12, 10); g.tick(10); g.shot('pond')
    # the hollow log: once a day
    held = sum(g.inv(it) for it in range(NUM_ITEMS)) + sum(g.u8('seeds', k) for k in range((NUM_CROPS + 1) * NUM_RARITY))
    g.go_face(17, 13, 'down'); g.act(); g.tick(30); g.shot('log'); g.dismiss()
    c.check(g.u8('area_taken', 4) & 0x80, 'the log is searched')
    after = sum(g.inv(it) for it in range(NUM_ITEMS)) + sum(g.u8('seeds', k) for k in range((NUM_CROPS + 1) * NUM_RARITY))
    g.go_face(17, 13, 'down'); g.act(); g.tick(30); g.dismiss()
    c.eq(sum(g.inv(it) for it in range(NUM_ITEMS)) + sum(g.u8('seeds', k) for k in range((NUM_CROPS + 1) * NUM_RARITY)), after, 'nothing more today')
    c.notes.append(f'log gave {after - held}')
    # START > map, then home
    open_start(g, 4); g.choose(8, 2, FARM_N, 1); g.tick(20); g.shot('woods_map'); g.press('b'); g.tick(6); g.dismiss()
    g.go(1, 9); g.step('left')
    g.wait_scene((MAP_TOWN,))
    c.eq(g.pos(), (30, 5), 'back on Main Street')
    # tomorrow it has all grown back
    g.w8('day', g.u8('day') + 1)
    g.go(30, 4); g.step('right'); g.wait_scene((MAP_WOODS,))
    c.eq(g.u8('area_taken', 4), 0, 'a new day in the woods')


def cart_to(g, idx, scene):
    """the minecart at the mine entrance -> place idx of its menu"""
    g.go(1, 4); g.step('left'); g.wait_scene((MAP_MINE,))
    g.go_face(11, 6, 'down'); g.act()
    g.choose(0, 1, 4, idx)
    g.wait_scene((scene,))
    g.tick(20); g.dismiss()


@scenario('places_by_cart', base='town')
def s_cart(c, g):
    """the minecart runs to the Granite Quarry, the High Pasture and the Crossroads Market, and home again"""
    Q, P, M = CONST['MAP_QUARRY'], CONST['MAP_PASTURE'], CONST['MAP_MARKET']
    g.w8('energy', 200)
    # the quarry: seams need the hammer
    cart_to(g, 0, Q)
    c.eq(g.pos(), (7, 10), 'arrive in the quarry')
    g.shot('quarry')
    g.w8('tool', T_HAND)
    held = lambda: sum(g.inv(it) for it in range(NUM_ITEMS))
    n0 = held()
    g.go_face(9, 7, 'up'); g.act(); g.tick(20); g.dismiss()          # the gem seam at (9, 6)
    c.eq((held(), g.u8('area_taken', 3)), (n0, 0), 'bare hands do nothing')
    g.w8('tool', T_HAMMER)
    g.go_face(9, 7, 'up'); g.act(); g.tick(20); g.dismiss()
    c.check(held() > n0 and g.u8('area_taken', 3) == 1 << 6, 'the gem seam gives to the hammer')
    c.check(any(g.inv(it) for it in GEM_ITEMS), 'a gem')
    g.go(7, 10); g.step('down'); g.wait_scene((MAP_MINE,))
    c.eq(g.pos(), (11, 6), 'the track brings you back beside the cart')
    g.go(14, 3); g.step('right'); g.wait_scene((MAP_TOWN,))
    # the pasture: three days of hay and a Highland animal comes home
    for b in (B_BARN, B_COOP):
        g.w8('bld', 0, g.bld_find(b) * BLD_SIZE + 3)
    g.w16('hay', 5)
    cart_to(g, 1, P)
    g.shot('pasture')
    for day in range(3):
        g.w8('area_taken', 0, 2)
        g.go_face(10, 5, 'up'); g.act(); g.yesno(True); g.tick(30)
        if day == 0:
            g.dismiss()
            g.go_face(10, 5, 'up'); g.act(); g.tick(20); g.dismiss()
            c.eq((g.u16('hay'), g.u8('tame')), (4, 1), 'one hay a day')
    g.dismiss()
    c.eq((g.u16('hay'), g.u8('tame')), (2, 0), 'three days of hay')
    got = g.animals()
    c.check(len(got) == 1 and got[0][1]['flags'] >> 6 == 3, f'a Highland animal came home ({got})')
    g.go(10, 12); g.step('down'); g.wait_scene((MAP_MINE,))
    g.go(14, 3); g.step('right'); g.wait_scene((MAP_TOWN,))
    # the market: shut on weekdays, open on the 6th and 7th
    g.w8('day', 3)
    cart_to(g, 2, M)
    g.shot('market')
    g.w32('gold', 20000)
    g.go_face(10, 4, 'up'); g.act(); g.tick(20); g.dismiss()
    c.eq(g.u32('gold'), 20000, 'shuttered midweek')
    g.w8('day', 6)
    g.go_face(10, 4, 'up'); g.act()
    g.choose(0, 0, 7, 1)                                # a Highland hen
    g.wait_menu(0, 0, 7)
    c.eq((g.u32('gold'), len(g.animals())), (20000 - 1800, 2), 'bought a Highland hen')
    g.choose(0, 0, 7, 3)                                # and a goat: it goes in the barn, and into the Herd Book
    g.wait_menu(0, 0, 7)
    goat = [a for _, a in g.animals() if a['kind'] == 3]
    c.check(len(goat) == 1 and g.bld(goat[0]['home'])[0] == B_BARN, 'a goat in the barn')
    c.check(g.u8('herd', 1) & (1 << 4), 'goat (kind 3, breed 0) in the Herd Book')
    g.w32('gold', 20000 - 1800)
    g.choose(0, 0, 7, 6); g.dismiss()
    g.go_face(6, 4, 'up'); g.act()
    g.choose(0, 0, 3, 0); g.wait_menu(0, 0, 3)
    c.eq((g.u16('hay'), g.u32('gold')), (22, 20000 - 1800 - 300), 'a cartload of hay')
    g.choose(0, 0, 3, 2); g.dismiss()
    g.go_face(2, 4, 'up'); g.act()
    g.choose(0, 0, 8, 7)                                # More...
    g.choose(0, 0, 8, 6)                                # snow pea, in spring
    g.quantity(3); g.tick(10)
    c.eq(g.seeds(14), 3, 'seeds out of season')
    g.dismiss()


@scenario('shell_island', base='town')
def s_island(c, g):
    """row out from the end of the pier: shells, the umbrella, deep water; the pier brings you back"""
    I = CONST['MAP_ISLAND']
    g.w8('energy', 200); g.w8('tool', T_HAND)
    beach_to(g)
    g.go_face(15, 15, 'down'); g.act(); g.yesno(True)
    g.wait_scene((I,))
    g.tick(20); g.dismiss()
    c.eq(g.pos(), (11, 13), 'on the island jetty')
    g.go(11, 9); g.tick(10); g.shot('island')
    held = lambda: sum(g.inv(it) for it in range(NUM_ITEMS)) + sum(g.u8('seeds', k) for k in range((NUM_CROPS + 1) * NUM_RARITY))
    n0 = held()
    g.go_face(12, 9, 'up'); g.act(); g.tick(30); g.dismiss()
    c.check(held() > n0 and g.u8('area_taken', 0) & 0x80, 'something buried under the umbrella')
    n1 = held()
    g.go_face(12, 9, 'up'); g.act(); g.tick(30); g.dismiss()
    c.eq(held(), n1, 'once a day')
    g.go(11, 14); g.step('down')
    g.wait_scene((MAP_BEACH,))
    c.eq(g.pos(), (15, 14), 'back on the beach pier')


@scenario('fifth_heart', base='town')
def s_fifth_heart(c, g):
    """at five full hearts, with the four scenes seen, one last scene plays the next time you talk in town"""
    g.w16('time', 660)
    for npc in range(NUM_NPC):
        g.w8('friend', 250, npc); g.w8('heart_seen', 0x0F, npc)
    gold = g.u32('gold')
    done = None
    for _ in range(6):
        for npc, (x, y) in sorted(npc_pos(g).items()):
            for dx, dy, d in ((0, 1, 'up'), (0, -1, 'down'), (-1, 0, 'right'), (1, 0, 'left')):
                if g.walkable(x + dx, y + dy):
                    try:
                        g.go_face(x + dx, y + dy, d)
                    except Fail:
                        continue
                    if npc_pos(g).get(npc) != (x, y):
                        continue
                    g.act(); g.tick(40); g.shot('fifth')
                    for _ in range(40):                 # the scene pauses between lines for its bubbles
                        g.dismiss(); g.tick(30)
                        if g.u8('heart_seen', npc) & 0x80 and not g.ui_open():
                            break
                    done = npc
                    break
            if done is not None:
                break
        if done is not None:
            break
        g.tick(120)
    c.require(done is not None, 'someone to talk to in town')
    c.check(g.u8('heart_seen', done) & 0x80, 'the fifth scene is remembered')
    c.eq(g.u32('gold'), gold + 5000, 'and its gift given')
    c.eq(g.u8('heart_day'), g.u8('day') | g.u8('season') << 5, 'one scene a day')


@scenario('sell_animal', base='cleared')
def s_sell_animal(c, g):
    """inside the barn: A on an animal for its panel, SELECT there to sell it"""
    barn = fix_up(g, B_BARN)
    g.set_anim(0, 0, barn, happy=220, age=30, flags=1 << 6, name='Mocha')       # a happy Jersey cow
    g.set_anim(1, 2, barn, happy=100, age=2, name='Lamb')                       # a baby sheep
    g.w8('weather', 1)                                                           # rain: everyone's inside
    to_town(g); to_farm(g)
    walk_in(g, barn, MAP_BARN)
    gold = g.u32('gold')
    for k, want in ((0, 2250 + 2250 // 4), (1, 900 // 2)):
        g.go_face(1, 4, 'up'); g.act(); g.tick(30); g.shot('panel')              # whoever is in the first stall
        g.press('select'); g.tick(10)
        g.choose(0, 0, 4, 1)                                                     # Sell
        g.yesno(True); g.tick(20); g.dismiss()
        c.eq(g.anim(k)['kind'], 0xFF, f'animal {k} is gone')
        c.eq(g.u32('gold'), gold + want, 'paid its worth')
        gold = g.u32('gold')
    c.eq(len(g.animals()), 0, 'the barn is empty')


@scenario('save_v6_migrate')
def s_save_v6(c, g):
    """a real v6 battery file (70 items, 11-byte animals, the late-game block) loads into the v7 game"""
    g.stop()
    shutil.copy(os.path.join(ROOT, 'tools/fixtures/v6_late_game.ram'), g.rom + '.ram')
    g.boot()
    title_start(g)
    g.choose(1, 9, 2, 0); g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    c.eq(g.u8('version'), SAVE_VERSION, 'upgraded to the current version')
    c.eq((g.u32('gold'), g.u8('year'), g.u8('season'), g.u8('day')), (765432, 4, 2, 9), 'gold and date')
    got = g.animals()
    c.eq([(k, a['name'], a['kind'], a['home'], a['happy'], a['age'], a['flags'] >> 6) for k, a in got],
         [(k, 'An%02d' % k, k % 3, 1 if k % 3 == 1 else 2, 60 + k * 9, 3 + k, k % 4) for k in range(20)], 'all twenty animals')
    c.check(all(g.u8('anim', k * ANIM_SIZE + ANIM_OFF[f]) == 0 for k in range(20) for f in ('trait', 'gen')), 'new traits start plain')
    c.eq((g.u8('fund_lvl'), g.u32('fund_paid'), g.u8('con_stars'), g.u16('review_score'), g.u8('review_year')), (3, 12345, 5, 321, 4), 'late game')
    c.eq([g.u8('donated', i) for i in range(10)], [0xFF, 0x3F, 0x01, 0, 0x80, 0, 0x11, 0, 0x20, 0], 'the collection')
    c.eq((g.u16('museum_sets'), g.u8('daily'), g.u8('daily_arg'), g.u8('tame'), g.u8('npc_mail'), g.u8('wish_done'), g.u8('wish_year')),
         (1, 3, 5, 2, 2, 5, 4), 'the rest of the block')
    c.eq([(g.u8('inv', it * 3), g.u8('inv', it * 3 + 2)) for it in range(70)], [((it * 7 + 3) % 200, it % 5) for it in range(70)], 'the bag')
    c.eq([g.inv(it) for it in range(70, NUM_ITEMS)], [0] * (NUM_ITEMS - 70), 'the new goods start at none')
    c.eq([g.u8('shipped', i) for i in range(10)], [(i * 37 + 5) & 0xFF for i in range(8)] + [0x3F, 0], 'the goods log')
    c.eq(g.u8('herd', 0), 0xFF, 'the Herd Book already knows the cows and hens in the barns')
    w = g.world()
    open_start(g, 6); g.yesno(True); g.tick(20); g.dismiss()
    boot_title(g)
    g.choose(1, 9, 2, 0); g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    c.eq((len(g.animals()), g.u32('gold'), g.u8('fund_lvl')), (20, 765432, 3), 'the v7 save loads back')
    c.eq(g.world(), w, 'with the same world')


@scenario('animal_traits', base='cleared')
def s_traits(c, g):
    """size comes from how a baby is raised; big gives more; goats, ducks and pigs give their own goods"""
    barn, coop = fix_up(g, B_BARN), fix_up(g, B_COOP)
    T = lambda k, f: g.u8('anim', k * ANIM_SIZE + ANIM_OFF[f])
    def W(k, f, v): g.w8('anim', v, k * ANIM_SIZE + ANIM_OFF[f])
    g.w16('hay', 50)
    g.set_anim(0, A_COW, barn, happy=150, age=ADULT_AGE - 1, flags=AF_FED, name='Loved'); W(0, 'gen', 1); W(0, 'trait', 5 << 4)
    g.set_anim(1, A_COW, barn, happy=150, age=ADULT_AGE - 1, flags=AF_FED, name='Lonely'); W(1, 'gen', 1); W(1, 'trait', 0)
    g.set_anim(2, 3, barn, happy=230, age=30, flags=AF_FED, name='Goat')
    g.set_anim(3, 4, coop, happy=100, age=30, flags=AF_FED, name='Duck')
    g.set_anim(4, 5, barn, happy=100, age=30, flags=AF_FED | (2 << 3), name='Pig')        # third fed day: a truffle
    g.sleep(stay=True)
    c.eq((T(0, 'trait') & 3, T(1, 'trait') & 3), (1, 2), 'the petted calf grew up big, the ignored one a runt')
    c.check(all(g.anim(k)['flags'] & AF_READY for k in (2, 3, 4)), 'goat, duck and pig all have something ready')


@scenario('animal_goods', base='cleared')
def s_animal_goods(c, g):
    """in the barn: a big animal gives one more, a runt one fewer; goats give goat milk, pigs truffles"""
    barn = fix_up(g, B_BARN)
    def W(k, f, v): g.w8('anim', v, k * ANIM_SIZE + ANIM_OFF[f])
    rare_ready = AF_READY | (R_RARE << 6)
    g.set_anim(0, A_COW, barn, happy=150, flags=rare_ready, name='Big'); W(0, 'trait', 1)
    g.set_anim(1, A_COW, barn, happy=150, flags=rare_ready, name='Runt'); W(1, 'trait', 2)
    g.set_anim(2, 3, barn, happy=230, flags=rare_ready, name='Goat')
    g.set_anim(3, 5, barn, happy=100, flags=rare_ready, name='Pig'); W(3, 'trait', 0x80)      # a sparkling pig
    g.w8('weather', 1)
    to_town(g); to_farm(g)
    walk_in(g, barn, MAP_BARN)
    g.shot('barn_goat_pig')
    for stall in (1, 2, 3, 4):
        g.go_face(stall, 4, 'up'); g.act(); g.tick(30)
        g.shot('panel_%d' % stall)
        g.dismiss()
    c.eq(g.inv(I_MILK), 3 + 1, 'big rare cow 3, runt rare cow 1')
    c.eq(g.inv(CONST['I_GOATMILK'], 2), 2, 'a happy rare goat: two gold-star goat milk')
    c.eq(g.inv(CONST['I_TRUFFLE'], 2), 2, 'a sparkling pig: gold-star truffles whatever its mood')
    # SELECT > Pair to breed, then the Herd Book
    g.go_face(3, 4, 'up'); g.act(); g.tick(30); g.press('select'); g.tick(10)
    g.choose(0, 0, 4, 0); g.tick(20); g.dismiss()
    c.check(g.anim(2)['flags'] & CONST['AF_PAIR'], 'the goat is paired')
    g.go_face(3, 4, 'up'); g.act(); g.tick(30); g.press('select'); g.tick(10)
    g.choose(0, 0, 4, 2); g.tick(30); g.shot('herd_book'); g.press('a'); g.tick(10); g.dismiss()
    c.check(g.u8('herd', 3) & (1 << 5), "the pig's sparkle is in the Herd Book")


@scenario('friends_letters')
def s_friends(c, g):
    """a letter with a present the morning after each new heart, and one big wish a year"""
    g.w8('friend', 120, NPC_BRICK)                    # two hearts, never written about
    g.sleep()
    c.check(g.u32('mail') & (1 << 27), 'a letter from town is waiting')
    c.eq(g.u8('npc_mail'), 1 << NPC_BRICK, "Brick's")
    g.w32('mail', 1 << 27)
    clear_area(g, 8, 8, 14, 8)
    g.go_face(13, 9, 'left'); g.act(); g.tick(60); g.shot('letter'); g.dismiss()
    c.eq((g.inv(I_IRON), g.u8('npc_mail')), (1, 0), 'one iron for the first heart')
    c.check(g.u32('mail') & (1 << 27) == 0, 'read')
    g.sleep()
    c.eq(g.u8('npc_mail'), 1 << NPC_BRICK, 'the second heart, the next night')
    g.w32('mail', 1 << 27)
    g.go_face(13, 9, 'left'); g.act(); g.tick(60); g.dismiss()
    c.eq(g.inv(I_IRON), 3, 'two more for the second')
    g.sleep()
    c.eq(g.u8('npc_mail'), 0, 'and no third letter at two hearts')


@scenario('world_save_roundtrip')
def s_world_roundtrip(c, g):
    """the 12KB world saves into its slot and loads back exactly; the other slot keeps its own world"""
    g.wset(1, 1, C_SOIL, 5 | SOIL_FERT, 7)               # far corners, outer valley
    g.wset(62, 62, C_DECOR, DK_LAMP, 1)
    g.wset(0, 63, C_TREE, 3 << 5, 99)
    g.setcell(12, 15, C_SOIL, 1, 3)
    w = g.world()
    sums = (sum(w[:4096]), sum(w[4096:8192]), sum(w[8192:]))
    open_start(g, 6)
    g.yesno(True); g.tick(20); g.dismiss()
    # scribble on the live world, then load the slot: the save must win
    for x in range(64):
        g.wset(x, 5, C_WATER, 0, 0)
    boot_title(g)
    g.choose(1, 9, 2, 0)
    g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    w2 = g.world()
    c.eq((sum(w2[:4096]), sum(w2[4096:8192]), sum(w2[8192:])), sums, 'world sums')
    c.check(w2 == w, 'world byte for byte')
    c.eq((g.wcell(1, 1), g.wa1(1, 1), g.wa2(1, 1)), (C_SOIL, 5 | SOIL_FERT, 7), 'corner cell (a2 from the other bank)')
    c.eq((g.wcell(62, 62), g.wa1(62, 62), g.wa2(62, 62)), (C_DECOR, DK_LAMP, 1), 'far corner')
    # the saved a2 lives in SRAM bank 0 at 0xB000, beside the state: a second save must leave both readable
    g.sleep()
    boot_title(g)
    g.choose(1, 9, 2, 0)
    g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    c.eq((g.wcell(1, 1), g.wa1(1, 1), g.wa2(1, 1)), (C_SOIL, 5 | SOIL_FERT, 7), 'corner cell after a second save')
    c.eq((g.wcell(62, 62), g.wa1(62, 62), g.wa2(62, 62)), (C_DECOR, DK_LAMP, 1), 'far corner after a second save')
    # a corrupted world fails its checksum: the slot reads as empty instead of loading garbage
    g.stop()
    ram = g.rom + '.ram'
    sav = bytearray(open(ram, 'rb').read())
    sav[3 * 0x2000 + 100] ^= 0xFF                         # slot 0 world (SRAM bank 3)
    open(ram, 'wb').write(bytes(sav))
    g.boot()
    title_start(g)
    g.wait_menu(1, 9, 2)
    g.shot('corrupt_slot')
    g.choose(1, 9, 2, 0)
    g.tick(60)
    c.check(not (g.ui_open() and g.menu_sel(8, 4, 3) is not None), 'a corrupted slot offers no Continue')


@scenario('far_corners', base='cleared')
def s_far_corners(c, g):
    """own the whole valley and walk the 64x64 world to all four corners: camera, streaming, minimap"""
    for k in range(8):
        g.w8('plots', 0xFF, k)
    # a walkable lattice through the woods, lake and quarries (every 4th row and column; the river stays)
    for y in range(WH):
        for x in range(WW):
            if (x % 4 == 0 or y % 4 == 0 or y in (10, 54)) and g.wcell(x, y) not in (C_WATER, C_STRUCT, C_ROAD) and \
                    not (g.wcell(x, y) == C_DECOR and g.wa1(x, y) == DK_BRIDGE):
                g.wset(x, y, C_GRASS)
    for (x, y) in ((0, 0), (WW - 1, 0), (0, WH - 1), (WW - 1, WH - 1)):
        g.wset(x, y, C_GRASS)
    to_town(g); to_farm(g)
    for name, (x, y) in (('nw', (0, 0)), ('ne', (WW - 1, 0)), ('se', (WW - 1, WH - 1)), ('sw', (0, WH - 1))):
        g.go(x - FO, y - FO)
        g.tick(10)
        c.eq(g.real_pos(), (x, y), f'reached the {name} corner')
        scx, scy = g.sym8('_scx') | (g.sym8('_scx', 1) << 8), g.sym8('_scy') | (g.sym8('_scy', 1) << 8)
        c.eq(scx, 0 if x == 0 else WW * 16 - 160, f'{name}: camera x clamped to the world edge')
        c.eq(scy, 0 if y == 0 else WH * 16 - 136, f'{name}: camera y clamped to the world edge')
        g.shot(name)
    open_start(g, 4)
    g.choose(8, 2, FARM_N, 1)                                   # MAP
    g.tick(60)
    g.shot('map_all_owned')
    g.dismiss()


@scenario('save_v3_migrate')
def s_save_v3(c, g):
    """a v3 save (before wool / seed rarity / furniture / staff / businesses) still loads"""
    g.w32('gold', 31337)
    g.set_seeds(2, 7)
    g.w16('wood', 77)
    g.w8('friend', 33, NPC_ADA)
    g.w8('inv', 5, I_PICKLES * 3)
    data = v3_bytes(g)
    g.stop()
    ram = g.rom + '.ram'
    sav = bytearray(open(ram, 'rb').read()) if os.path.exists(ram) else bytearray(RAM_SIZE)
    sav[0:len(data)] = data
    open(ram, 'wb').write(bytes(sav))
    g.boot()
    title_start(g)
    g.choose(1, 9, 2, 0)
    g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    g.shot('migrated')
    c.eq(g.u32('gold'), 31337, 'gold')
    c.eq(g.seeds(2), 7, 'carrot seeds (now common packets)')
    c.eq(g.u16('wood'), 77, 'wood (after the old seeds block)')
    c.eq(g.u8('friend', NPC_ADA), 33, 'friendship')
    c.eq(g.inv(I_PICKLES), 5, 'pickles (last old item)')
    c.eq(g.inv(I_WOOL), 0, 'no wool')
    c.eq(bytes(g.u8('farm_name', i) for i in range(8)).split(b'\0')[0], b'Stilwell', 'farm name')
    c.eq(g.u8('furn_init'), 0, 'furniture not placed yet')
    c.eq(g.u8('version'), SAVE_VERSION, 'upgraded to the current version')
    g.enter_house()
    c.eq(len(g.furn()), 3, "Grandpa's furniture appears on the first visit")


# ------------------------------------------------------------------ design tools (designer.c)
DECOR_WOOD = [int(d[4]) for d in DECOR]
DECOR_STONE = [int(d[5]) for d in DECOR]
SWATCH = [[int(h, 16) for h in re.findall(r'C5\(0x([0-9A-Fa-f]+)\)', row)]
          for row in re.findall(r'\{([^{}]*)\}', _table(_src('designer.c'), 'swatch'))]
ACC_SLOT = [CONST['PAL_RED'] * 4 + 2, CONST['PAL_WOOD'] * 4 + 1, CONST['PAL_STONE'] * 4 + 1]


def c5(rgb):
    return ((rgb >> 19) & 31) | (((rgb >> 11) & 31) << 5) | (((rgb >> 3) & 31) << 10)


def bg_pal(g, entry):
    """a colour from CGB background palette RAM (BCPS/BCPD), entry = palette * 4 + colour"""
    m = g.pb.memory
    keep = m[0xFF68]
    m[0xFF68] = entry * 2
    lo = m[0xFF69]
    m[0xFF68] = entry * 2 + 1
    hi = m[0xFF69]
    m[0xFF68] = keep
    return lo | (hi << 8)


def money(g):
    return g.u32('gold'), g.u16('wood'), g.u16('stone')


def paint_walk(g, d, steps, shot_at=None, shot=None):
    """face d, hold A, walk `steps` cells with A held, let go; optional screenshot mid-stroke"""
    g.park_pet()                        # (a shooed pet stands on your cell: the stroke would skip it)
    g.face(d)
    g.pb.button_press('a')
    g.tick(6)
    g.pb.button_press(d)
    x, y = g.pos()
    done = 0
    for _ in range(steps * 40):
        g.tick(1)
        if g.pos() != (x, y):
            x, y = g.pos()
            done += 1
            if done == shot_at:
                g.tick(2)
                g.shot(shot)
            if done >= steps:
                break
    g.pb.button_release(d)
    g.tick(10)
    g.pb.button_release('a')
    g.tick(8)
    return done


def quick_undo(g):
    """START while building -> the BUILD quick menu -> UNDO"""
    g.press('start')
    g.choose(8, 0, 6, 0)
    g.tick(10)


@scenario('paint', base='cleared')
def s_paint(c, g):
    g.rich()
    # a brick path: hold A and walk right; the line starts where you stood
    build_poke(g, 2, 1)
    g.go_face(9, 16, 'right')
    before = money(g)
    paint_walk(g, 'right', 4, shot_at=3, shot='painting')
    bricks = [x for x in range(8, 15) if g.cell(x, 16) == C_DECOR and g.a1(x, 16) == DK_BRICK]
    c.eq(bricks, [9, 10, 11, 12], 'brick line laid along the walk')
    c.eq(before[0] - g.u32('gold'), 4 * DECOR_GOLD[DK_BRICK], 'gold charged per cell')
    c.eq(before[2] - g.u16('stone'), 4 * DECOR_STONE[DK_BRICK], 'stone charged per cell')
    c.eq(g.sym8('_paint_on'), 0, 'stroke closes when A is let go')
    g.shot('brick_line')
    # a fence row: fences block walking, but each lands behind you
    build_poke(g, 2, 4)
    g.go_face(9, 22, 'right')
    before = money(g)
    paint_walk(g, 'right', 5)
    fences = [x for x in range(8, 15) if g.cell(x, 22) == C_DECOR and g.a1(x, 22) == DK_FENCE]
    c.eq(fences, [9, 10, 11, 12, 13], 'fence row laid behind the player')
    c.eq([g.a2(x, 22) for x in (10, 11, 12)], [1, 1, 1], 'fence rails autotile horizontally')
    c.eq(before[1] - g.u16('wood'), 5 * DECOR_WOOD[DK_FENCE], 'wood charged per fence')
    g.shot('fence_row')
    # a tap still places in front, as before
    build_poke(g, 1, 4)                 # hedge
    g.go_face(11, 19, 'up')
    g.act(); g.tick(6)
    c.eq((g.cell(11, 18), g.a1(11, 18)), (C_DECOR, DK_HEDGE), 'tap places in front')
    # running out of gold stops the stroke cleanly
    build_poke(g, 1, 0)                 # flower bed
    g.go_face(17, 13, 'right')
    g.w32('gold', DECOR_GOLD[DK_FLOWERBED] * 2 + 5)
    paint_walk(g, 'right', 5, shot_at=4, shot='out_of_gold')
    beds = [x for x in range(17, 24) if g.cell(x, 13) == C_DECOR and g.a1(x, 13) == DK_FLOWERBED]
    c.eq(beds, [17, 18], 'only what the gold could pay for')
    c.eq(g.u32('gold'), 5, 'never goes below zero')
    # a blocked cell on the way is skipped, the rest of the line still goes down
    g.rich()
    build_poke(g, 2, 2)                 # gravel
    g.setcell(19, 15, C_DECOR, DK_PATH)
    g.go_face(17, 15, 'right')
    before = money(g)
    paint_walk(g, 'right', 4)
    row = [(g.a1(x, 15) if g.cell(x, 15) == C_DECOR else None) for x in range(17, 21)]
    c.eq(row, [DK_GRAVEL, DK_GRAVEL, DK_PATH, DK_GRAVEL], 'gravel line goes round the existing path cell')
    c.eq(before[0] - g.u32('gold'), 3 * DECOR_GOLD[DK_GRAVEL], 'skipped cell not charged')
    # the eraser: SELECT swaps to it; hold A and walk into the fence row to clear it
    build_poke(g, 2, 2)
    g.go_face(8, 22, 'right')
    g.press('select'); g.tick(4)
    c.eq((g.u8('build_cat'), g.u8('build_sel')), (5, 1), 'SELECT picks the eraser')
    c.eq(g.sym8('_building'), 1, 'still building')
    paint_walk(g, 'right', 3, shot_at=2, shot='erasing')
    gone = [x for x in range(9, 14) if g.cell(x, 22) == C_GRASS]
    c.check(len(gone) >= 3 and gone[0] == 9, f'eraser cleared the fence row as it walked (cleared {gone})')
    g.press('select'); g.tick(4)
    c.eq((g.u8('build_cat'), g.u8('build_sel')), (2, 2), 'SELECT again goes back to the item in hand')
    g.press('b'); g.tick(4)
    c.eq(g.sym8('_building'), 0, 'B still leaves build mode')


@scenario('undo', base='cleared')
def s_undo(c, g):
    g.rich()
    start = money(g)
    # 1. a statue (gold + stone)
    build_poke(g, 1, 5)
    g.go_face(9, 16, 'down'); g.act(); g.tick(6)
    c.eq(g.a1(9, 17), DK_STATUE, 'statue placed')
    after_statue = money(g)
    # 2. a brick stroke over tilled soil (the soil comes back on undo)
    for x in range(10, 14):
        g.setcell(x, 19, C_SOIL, SOIL_WET)
    build_poke(g, 2, 1)
    g.go_face(10, 19, 'right')
    paint_walk(g, 'right', 4)
    c.eq([g.a1(x, 19) for x in range(10, 14)], [DK_BRICK] * 4, 'brick stroke over soil')
    after_stroke = money(g)
    # 3. erase the statue
    build_poke(g, 5, 1)
    g.go_face(9, 16, 'down'); g.act(); g.tick(6)
    c.eq(g.cell(9, 17), C_GRASS, 'statue erased')
    # 4. a picnic table, then move it
    build_poke(g, 1, 8)
    g.go_face(12, 16, 'down'); g.act(); g.tick(6)
    after_picnic = money(g)
    build_poke(g, 5, 0)
    g.act(); g.tick(6)
    g.go_face(13, 16, 'down'); g.act(); g.tick(6)
    c.eq((g.cell(12, 17), g.a1(13, 17)), (C_GRASS, DK_PICNIC), 'picnic table moved')
    g.shot('before_undo')
    # undo, newest first
    build_poke(g, 1, 0)
    g.press('start')
    g.wait_menu(8, 0, 6)
    g.shot('quick_menu')
    g.choose(8, 0, 6, 0)
    g.tick(10)
    c.eq((g.cell(13, 17), g.a1(12, 17)), (C_GRASS, DK_PICNIC), 'undo move: table back where it was')
    c.eq(money(g), after_picnic, 'a move costs and refunds nothing')
    quick_undo(g)
    c.eq(g.cell(12, 17), C_GRASS, 'undo place: table gone')
    c.eq(money(g), after_stroke, 'picnic table refunded exactly')
    quick_undo(g)
    c.eq((g.cell(9, 17), g.a1(9, 17)), (C_DECOR, DK_STATUE), 'undo erase: statue back (free)')
    c.eq(money(g), after_stroke, 'restoring an erased piece is free')
    g.shot('undo_erase')
    quick_undo(g)
    c.eq([(g.cell(x, 19), g.a1(x, 19)) for x in range(10, 14)], [(C_SOIL, SOIL_WET)] * 4, 'undo stroke: the whole line, wet soil back')
    c.eq(money(g), after_statue, 'stroke refunded exactly')
    quick_undo(g)
    c.eq(g.cell(9, 17), C_GRASS, 'undo statue')
    c.eq(money(g), start, 'back to the starting gold, wood and stone')
    quick_undo(g)
    c.eq(money(g), start, 'nothing left to undo')
    # a building and its move
    clear_area(g, 17, 13, 23, 17, force=True)
    build_poke(g, 4, 3)                 # silo
    g.go_face(18, 16, 'up'); g.act()
    g.yesno(True); g.tick(20); g.dismiss()
    k = g.bld_find(B_SILO)
    c.require(k is not None, 'silo built')
    build_poke(g, 5, 0)
    g.go_face(18, 16, 'up'); g.act(); g.tick(6)
    g.go_face(21, 16, 'up'); g.act(); g.tick(6)
    c.eq(g.bld(k)[1:3], (21, 14), 'silo moved')
    quick_undo(g)
    c.eq(g.bld(k)[1:3], (18, 14), 'undo building move')
    c.eq([g.cell(18, 14), g.cell(18, 15), g.cell(21, 14)], [C_STRUCT, C_STRUCT, C_GRASS], 'silo cells restamped')
    quick_undo(g)
    c.eq(g.bld(k)[0], 0xFF, 'undo building')
    c.eq(money(g), start, 'silo refunded exactly')
    g.shot('after_undos')
    # the history is cleared by leaving the farm
    build_poke(g, 1, 0)
    g.go_face(11, 16, 'down'); g.act(); g.tick(6)
    c.eq(g.sym8('_undo_n'), 1, 'one action recorded')
    g.press('b'); g.tick(4)
    to_town(g); to_farm(g)
    c.eq(g.sym8('_undo_n'), 0, 'history cleared after a trip to town')


@scenario('recolour', base='cleared')
def s_recolour(c, g):
    c.eq(bg_pal(g, ACC_SLOT[0]), c5(SWATCH[0][0]), 'roof red is the art colour to begin with')
    open_start(g, 3)
    g.choose(0, 0, 8, 7)                # BUILD > COLOURS
    g.tick(20)
    g.press('right'); g.press('right')  # roofs: Terracotta
    g.tick(4)
    c.eq(g.u16('accent', 0), c5(SWATCH[0][2]), 'roof accent set')
    c.eq(bg_pal(g, ACC_SLOT[0]), c5(SWATCH[0][2]), 'palette RAM written live')
    g.press('down'); g.press('left')    # wood: wraps round to the last swatch
    g.press('down'); g.press('right'); g.press('right'); g.press('right')   # stone: the fourth swatch
    g.tick(20)
    g.shot('colour_menu')
    g.press('a'); g.tick(10)
    g.dismiss(); g.wait_field()
    want = [c5(SWATCH[0][2]), c5(SWATCH[1][4]), c5(SWATCH[2][3])]
    c.eq([g.u16('accent', k * 2) for k in range(3)], want, 'accents kept')
    c.eq([bg_pal(g, s) for s in ACC_SLOT], want, 'all three in palette RAM')
    g.go(19, 12)
    g.shot('recoloured_farm')
    # B in the menu puts the colours back
    open_start(g, 3)
    g.choose(0, 0, 8, 7)
    g.tick(10)
    g.press('right'); g.tick(4)
    c.check(g.u16('accent', 0) != want[0], 'preview changes it')
    g.press('b'); g.tick(10)
    g.dismiss(); g.wait_field()
    c.eq(g.u16('accent', 0), want[0], 'B undoes the preview')
    c.eq(bg_pal(g, ACC_SLOT[0]), want[0], 'and puts the palette back')
    # night: the custom colour goes through the same tint ramps as the rest
    g.w16('time', 1300)
    g.tick(700)                         # the next clock tick re-applies the palettes

    def tint(rgb):
        r, gg, b = rgb & 31, (rgb >> 5) & 31, rgb >> 10
        return (r * 6 // 16) | ((gg * 7 // 16) << 5) | ((b * 10 // 16 + 4) << 10)
    c.eq(bg_pal(g, ACC_SLOT[0]), tint(want[0]), 'night tint composes with the custom roof colour')
    g.shot('recoloured_night')
    # the town keeps its own colours
    g.w16('time', 600)
    to_town(g)
    c.eq(bg_pal(g, ACC_SLOT[0]), c5(SWATCH[0][0]), 'town uses the art colours')
    to_farm(g)
    c.eq(bg_pal(g, ACC_SLOT[0]), want[0], 'back on the farm the roofs are recoloured again')
    # kept in the save
    open_start(g, 6)
    g.yesno(True); g.tick(20); g.dismiss()
    boot_title(g)
    g.choose(1, 9, 2, 0)
    g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    c.eq([g.u16('accent', k * 2) for k in range(3)], want, 'accents saved and loaded')
    c.eq(bg_pal(g, ACC_SLOT[1]), want[1], 'and applied after loading')


def garden(g):
    """a little garden at 8..12 x 15..19: picket ring, brick cross path, red flowers and a lamp"""
    for x in range(8, 13):
        for y in (15, 19):
            g.setcell(x, y, C_DECOR, DK_PICKET)
    for y in range(16, 19):
        g.setcell(8, y, C_DECOR, DK_PICKET)
        g.setcell(12, y, C_DECOR, DK_PICKET)
    for x in range(9, 12):
        g.setcell(x, 17, C_DECOR, DK_BRICK)
    g.setcell(10, 16, C_DECOR, DK_FLOWERS_RED)
    g.setcell(10, 18, C_DECOR, DK_LAMP)


@scenario('blueprint', base='cleared')
def s_blueprint(c, g):
    g.rich()
    clear_area(g, 8, 14, 14, 14)
    garden(g)
    to_town(g); to_farm(g)
    build_poke(g, 6, 0)                 # BLUEPRINT > Copy Area
    g.go_face(8, 14, 'down'); g.act(); g.tick(6)    # first corner (8,15)
    c.eq(g.sym8('_copy_on'), 1, 'first corner set')
    g.go_face(13, 19, 'left')           # far corner (12,19)
    g.tick(20)
    g.shot('copy_area')
    g.act()
    g.wait_menu(0, 0, CONST['BP_SLOTS'])
    g.shot('save_slot')
    g.choose(0, 0, CONST['BP_SLOTS'], 0)         # save as Blueprint 1
    g.tick(20); g.dismiss()
    c.eq(g.u8('bp_wh', 0), (5 << 4) | 5, 'blueprint 1 is 5x5')
    pieces = 16 + 3 + 2
    c.eq((g.u8('build_cat'), g.u8('build_sel')), (6, 1), 'goes straight on to stamping it')
    # stamp it in the east area, with the ghost showing first
    clear_area(g, 17, 12, 23, 17, force=True)
    to_town(g); to_farm(g)
    g.symw8('_building', 1)
    g.go_face(20, 12, 'down')           # 5 wide, centred under the front cell: x 18..22, y 13..17
    g.shoo_pet()
    g.tick(30)
    c.eq((g.sym8('_ghost_x') - FO, g.sym8('_ghost_y') - FO, g.sym8('_ghost_w')), (18, 13, 5), 'ghost over the stamp area')
    g.shot('stamp_ghost')
    before = money(g)
    g.park_pet()
    g.act()
    g.wait_menu(14, YN_Y, 2)
    g.shot('stamp_cost')
    g.yesno(True); g.tick(20); g.dismiss()
    got = sum(1 for x in range(18, 23) for y in range(13, 18) if g.cell(x, y) == C_DECOR)
    c.eq(got, pieces, 'every piece stamped')
    c.eq([g.a1(x, 13) for x in range(18, 23)], [DK_PICKET] * 5, 'fence top row')
    c.eq((g.a1(20, 14), g.a1(20, 16), g.a1(19, 15)), (DK_FLOWERS_RED, DK_LAMP, DK_BRICK), 'inside pieces')
    cost = [0, 0, 0]
    for k, n in ((DK_PICKET, 16), (DK_BRICK, 3), (DK_FLOWERS_RED, 1), (DK_LAMP, 1)):
        cost[0] += DECOR_GOLD[k] * n
        cost[1] += DECOR_WOOD[k] * n
        cost[2] += DECOR_STONE[k] * n
    c.eq(tuple(b - a for a, b in zip(money(g), before)), tuple(cost), 'stamp charged the sum of its pieces')
    c.eq([g.a2(x, 13) for x in (19, 20, 21)], [1, 1, 1], 'stamped fences autotile')
    g.press('b'); g.tick(4)
    g.go(17, 12)
    g.shot('stamped')
    # undo lifts the whole stamp as one action
    build_poke(g, 6, 1)
    quick_undo(g)
    c.eq(sum(1 for x in range(18, 23) for y in range(13, 18) if g.cell(x, y) == C_DECOR), 0, 'undo lifts the whole stamp')
    c.eq(money(g), before, 'stamp refunded exactly')
    # partial stamp: a blocked cell and too little gold
    g.setcell(18, 13, C_ROCK)
    g.w32('gold', DECOR_GOLD[DK_PICKET] * 3 + 1)
    g.go_face(20, 12, 'down'); g.tick(20)
    g.act()
    g.yesno(True); g.tick(20); g.dismiss()
    placed = sum(1 for x in range(18, 23) for y in range(13, 18) if g.cell(x, y) == C_DECOR)
    c.check(0 < placed < pieces, f'partial stamp places only what fits and is paid for ({placed})')
    c.eq(g.cell(18, 13), C_ROCK, 'blocked cell left alone')
    c.check(g.u32('gold') < DECOR_GOLD[DK_PICKET], 'spent down to what there was')
    g.press('b'); g.tick(4)
    # a building inside the copied area comes along
    g.rich()
    clear_area(g, 8, 15, 14, 23, force=True)
    clear_area(g, 16, 8, 16, 11)
    to_town(g); to_farm(g)
    build_poke(g, 6, 0)
    g.go_face(16, 9, 'right'); g.act(); g.tick(6)    # the coop at 17,9 (2x2): first corner (17,9)
    g.go_face(19, 11, 'up'); g.act()                 # far corner (19,10)
    g.choose(0, 0, CONST['BP_SLOTS'], 1)
    g.tick(20); g.dismiss()
    c.eq(g.u8('bp_wh', 1), (3 << 4) | 2, 'blueprint 2 is 3x2')
    ncoop = sum(1 for k in range(MAX_BLD) if g.bld(k)[0] == B_COOP)
    g.go_face(11, 21, 'up'); g.tick(20)              # 3x2 above the player: x 10..12, y 19..20
    g.shot('ghost_coop')
    g.act()
    g.yesno(True); g.tick(20); g.dismiss()
    c.eq(sum(1 for k in range(MAX_BLD) if g.bld(k)[0] == B_COOP), ncoop + 1, 'a coop stamped from the blueprint')
    g.press('b'); g.tick(4)
    g.go(11, 22)
    g.shot('coop_stamped')


def pet_oam(g):
    """the pet's sprite pair: (y, x) of its first OAM entry, (0, 0) when hidden"""
    b = g.sym['_act_kind']
    for k in range(MAX_ACT):
        if g.pb.memory[b + k] == K_PET:
            o = 0xFE00 + (6 + k * 2) * 4
            return g.pb.memory[o], g.pb.memory[o + 1]
    return None


def put_pet(g, x, y):
    """test-only: stand the pet on a real cell"""
    b, n = g.sym['_act_kind'], MAX_ACT
    for k in range(n):
        if g.pb.memory[b + k] == K_PET:
            g.pb.memory[b + 2 * n + k], g.pb.memory[b + 3 * n + k] = x, y


@scenario('ghost_pet', base='cleared')
def s_ghost_pet(c, g):
    """the pet never blocks building: a blueprint ghost draws its piece under the pet (the pet stays drawn on
    top), the stamp puts every piece down and the pet hops off onto open ground"""
    g.rich()
    clear_area(g, 8, 14, 14, 14)
    garden(g)
    to_town(g); to_farm(g)
    build_poke(g, 6, 0)
    g.go_face(8, 14, 'down'); g.act(); g.tick(6)
    g.go_face(13, 19, 'left'); g.tick(4); g.act()
    g.choose(0, 0, CONST['BP_SLOTS'], 0)
    g.tick(20); g.dismiss()
    clear_area(g, 17, 12, 23, 17, force=True)
    to_town(g); to_farm(g)
    g.go(17, 12)
    g.symw8('_building', 1)
    g.go_face(20, 12, 'down')
    g.tick(4)
    put_pet(g, 19 + FO, 13 + FO)        # the pet sits where the ghost's fence goes
    g.tick(2)
    c.eq(g.sym8('_ghost_w'), 5, 'ghost showing')
    c.eq(g.actor_at(19, 13)[0], K_PET, 'pet inside the ghost')
    bg = lambda x, y: g.pb.memory[0, 0x9800 + (((y + FO) * 2) & 31) * 32 + (((x + FO) * 2) & 31)]
    c.eq(bg(19, 13), bg(20, 13), 'the ghost fence is drawn under the pet too')
    y, x = pet_oam(g)
    c.check(y != 0 and x != 0, f'pet sprite drawn ({x},{y})')
    g.shot('pet_in_ghost')
    pieces = 16 + 3 + 2
    g.act()
    g.wait_menu(14, YN_Y, 2)
    g.yesno(True); g.tick(20); g.dismiss()
    got = sum(1 for x in range(18, 23) for y in range(13, 18) if g.cell(x, y) == C_DECOR)
    c.eq(got, pieces, "every piece stamped, the pet's cell too")
    g.tick(10)
    pet = [a for a in g.actors() if a[0] == K_PET][0]
    c.check(pet[2:] != (19, 13), f'pet hopped off the fence (now at {pet[2:]})')
    under = g.cell(*pet[2:])
    c.check(under != C_DECOR or g.a1(*pet[2:]) in (DK_BRICK,), f'pet stands on open ground ({under})')
    y, x = pet_oam(g)
    c.check(y != 0 and x != 0, f'pet still drawn ({x},{y})')
    g.shot('pet_hopped')


# ------------------------------------------------------------------ town life: routines, heart events, festivals
_routes_py = open(os.path.join(ROOT, 'tools/gen_routes.py')).read()
SPOT = {n: (int(x), int(y)) for n, x, y in re.findall(r"\('(\w+)', (\d+), (\d+)\)", _routes_py)}
_fest_c = _src('festival.c')
HIDE = list(zip(_ints(_table(_fest_c, 'hide_x')), _ints(_table(_fest_c, 'hide_y'))))
FEST_NEED = _ints(_table(_fest_c, 'need'))
FEST_GOLD = _ints(_table(_fest_c, 'prize_gold'))
FEST_CROP = _ints(_table(_fest_c, 'next_crop'))


def npc_pos(g):
    """{npc: (x, y)} of the townsfolk out in town"""
    return {a[1]: a[2:] for a in g.actors() if a[0] == K_NPC}


def wait_npcs(g, want, maxf=2400):
    """tick until every npc in want ({npc: (x, y) or None for indoors}) is there"""
    for _ in range(maxf // 20):
        p = npc_pos(g)
        if all(p.get(n) == xy for n, xy in want.items()):
            return True
        g.tick(20)
    return False


def next_tick(g, t):
    """set the clock just before t and wait for the 10-minute tick to land on t"""
    g.w16('time', t - 10)
    for _ in range(80):
        g.tick(20)
        if g.u16('time') >= t:
            return
    raise Fail('the clock never ticked')


def sym16(g, name):
    return g.sym8(name) | g.sym8(name, 1) << 8


def sym32(g, name):
    return sum(g.sym8(name, k) << (8 * k) for k in range(4))


def come_to_town(g):
    """walk down the farm road into town WITHOUT dismissing anything (a heart event may be waiting)"""
    g.go(15, WH - FO - 2)
    g.step('down')
    if g.u8('scene') != MAP_TOWN:       # (to_town's way: the last row warps when you face down on it)
        g.face('down')
        g.press('down', 4, 10)
    for _ in range(60):
        g.tick(10)
        if g.u8('scene') == MAP_TOWN:
            break
    if g.u8('scene') != MAP_TOWN:
        raise Fail('did not reach town')


def re_enter_town(g):
    to_farm(g)
    to_town(g)


@scenario('town_routines', base='town')
def s_town_routines(c, g):
    """townsfolk keep a schedule: shops staffed in opening hours, lunch at the cafe, home at night, Sundays off"""
    c.eq(g.u8('day'), 1, 'a Monday')
    want = {NPC_ROSA: SPOT['STORE'], NPC_BRICK: SPOT['SMITH'], NPC_JUNE: SPOT['CARP'], NPC_ADA: SPOT['CAFE'],
            NPC_HOLLIS: SPOT['HALL'], NPC_MILO: SPOT['FOUNT_E']}
    c.check(wait_npcs(g, want, 200), f'10am: everyone at work {npc_pos(g)}')
    g.shot('ten_am')
    # noon: the mayor and Marcy walk to the cafe terrace for lunch
    next_tick(g, 720)
    c.check(npc_pos(g).get(NPC_HOLLIS) != SPOT['CAFE_T1'], 'Joe walks there (not a teleport)')
    c.check(wait_npcs(g, {NPC_HOLLIS: SPOT['CAFE_T1'], NPC_MILO: SPOT['CAFE_T2']}), f'lunch at the cafe {npc_pos(g)}')
    g.shot('lunch')
    # the hall is shut while Joe is at lunch: the sign, not the hiring menu
    g.go_face(16, 9, 'up'); g.act(); g.tick(40)
    g.shot('hall_lunch_sign')
    c.check(g.menu_live(0, 0, HALL_N) is None, 'no hall menu at lunch')
    g.dismiss()
    # 9pm: shops shut, everyone walks home and goes in
    next_tick(g, 1260)
    g.tick(60)
    g.shot('going_home')
    c.check(wait_npcs(g, {n: None for n in (NPC_ROSA, NPC_ADA, NPC_HOLLIS, NPC_MILO)}, 3000), f'home by 9pm {npc_pos(g)}')
    g.go_face(11, 4, 'up'); g.act(); g.tick(40)
    g.shot('store_closed_sign')
    c.check(g.menu_live(0, 0, STORE_N) is None, 'store closed at night')
    g.dismiss()
    g.w16('time', 1340)
    c.check(wait_npcs(g, {n: None for n in range(NUM_NPC)}, 3000), f'town asleep by 10:20pm {npc_pos(g)}')
    g.shot('night')
    # Sunday: the forge is closed and Brick spends the morning in the park
    g.w8('day', 7); g.w16('time', 600)
    re_enter_town(g)
    c.check(wait_npcs(g, {NPC_BRICK: SPOT['PARK_E'], NPC_ROSA: SPOT['STORE']}, 200), f'Sunday 10am {npc_pos(g)}')
    g.shot('sunday')
    g.go_face(2, 4, 'up'); g.act(); g.tick(40)
    c.check(g.menu_live(0, 0, 3) is None, 'forge closed on Sundays')
    g.dismiss()
    # rain: Marcy is indoors after lunch, Brick goes back to the forge on a rainy weekday
    g.w8('day', 2); g.w8('weather', 1); g.w16('time', 840)
    re_enter_town(g)
    p = npc_pos(g)
    c.check(NPC_MILO not in p and p.get(NPC_BRICK) == SPOT['SMITH'], f'rainy Tuesday 2pm {p}')
    g.shot('rain')


def heart_scene(g, choice=0, maxp=80):
    """advance a heart event: A through the lines, pick `choice` at the question, then play it out
    (lines are separate dialogs with emote pauses between them: wait for the field to stay quiet)"""
    g.wait_menu(0, 5, 2, advance='a', maxp=maxp)
    g.shot('choice')
    g.choose(0, 5, 2, choice)
    quiet = 0
    while quiet < 90:
        g.tick(1)
        if g.ui_open():
            quiet = 0
            g.press('a')
        else:
            quiet += 1


@scenario('heart_events', base='town')
def s_heart_events(c, g):
    """at 1 heart Alyssa's first scene plays on arrival (walk-in, emote, choice), once; the next waits for 2 hearts"""
    g.w8('friend', 50, NPC_ROSA)
    to_farm(g)
    come_to_town(g)
    for _ in range(200):                     # she walks up from the street
        g.tick(4)
        if g.ui_open():
            break
    g.shot('walk_in')
    rosa = npc_pos(g).get(NPC_ROSA)
    px, py = g.pos()
    c.eq(rosa, (px + 1, py), 'Alyssa walked up to your side')
    c.check(g.sym8('_emo_kind') != 0, 'an emote bubble is up')
    heart_scene(g, 0)
    c.check(g.u8('heart_seen', NPC_ROSA) & 1, 'event recorded')
    c.eq(g.u8('friend', NPC_ROSA), 50 + 12, 'kind answer +12')
    c.eq(g.u8('heart_day'), g.u8('day') | g.u8('season') << 5, 'one a day')
    g.shot('after')
    # it never plays twice
    to_farm(g)
    come_to_town(g)
    g.tick(120)
    c.check(not g.ui_open(), 'no repeat on the next arrival')
    g.dismiss()
    # 2 hearts, another (dry) evening: the second scene
    g.w8('friend', 100, NPC_ROSA); g.w8('heart_day', 0); g.w8('weather', 0); g.w16('time', 1150)
    to_farm(g)
    g.w16('time', 1150)
    come_to_town(g)
    for _ in range(300):
        g.tick(4)
        if g.ui_open():
            break
    heart_scene(g, 1)                        # 7pm, her 2-heart window: she walks up again; the other answer
    c.check(g.u8('heart_seen', NPC_ROSA) & 2, 'second event recorded')
    c.eq(g.u8('friend', NPC_ROSA), 100 + 4, 'other answer +4')
    # npc_talk path: Marcy at 1 heart, talked to on the spot
    g.w8('friend', 49, NPC_MILO); g.w8('heart_day', 0); g.w16('time', 600)
    re_enter_town(g)
    talked = False
    for _ in range(10):
        p = npc_pos(g).get(NPC_MILO)
        if p:
            for d, (dx, dy, _) in DIRS.items():
                if g.walkable(p[0] - dx, p[1] - dy):
                    g.go(p[0] - dx, p[1] - dy); g.face(d)
                    if g.actor_at(*p):
                        g.act(); talked = True
                    break
        if talked:
            break
        g.tick(60)
    c.require(talked, 'could not reach Marcy')
    heart_scene(g, 0)                        # talking (+2) tips him over 50: his first scene plays
    c.check(g.u8('heart_seen', NPC_MILO) & 1, 'Marcy event recorded from talking')
    # the FRIENDS page shows hearts and the seen events
    open_start(g, 4)
    g.choose(8, 2, FARM_N, 3)
    g.tick(30)
    g.shot('friends_page')
    c.eq(g.wtile(14, 3), GL_IT_STAR2, 'Alyssa event 1 marked seen')
    c.eq(g.wtile(15, 3), GL_IT_STAR2, 'Alyssa event 2 marked seen')
    g.dismiss()


def fest_setup(g, season, day):
    g.w8('season', season); g.w8('day', day); g.w8('weather', 0); g.w16('time', 600)
    re_enter_town(g)


def hall_festival(g):
    g.go_face(16, 9, 'up'); g.act()
    g.choose(0, 0, HALL_N + 1, 0)            # FESTIVAL
    g.wait_menu(14, YN_Y, 2)
    g.shot('join')
    g.yesno(True)


def fest_check(c, g, se, gold0, seeds0):
    c.check(g.u8('ribbons') & (1 << se), 'blue ribbon won')
    c.check(g.u8('fest_done') & (1 << se), 'prize taken this year')
    c.check(g.u32('gold') >= gold0 + FEST_GOLD[se], f'prize gold (G{g.u32("gold") - gold0})')
    c.eq(g.seeds(FEST_CROP[se], R_RARE), seeds0 + 3, '3 rare seeds for next season')
    c.check(g.u8('fest_best', se) >= FEST_NEED[se], f'best score {g.u8("fest_best", se)}')


@scenario('fest_egg_hunt', base='town')
def s_fest_egg(c, g):
    fest_setup(g, SEASON_SPRING, 14)
    gold, seeds = g.u32('gold'), g.seeds(FEST_CROP[0], R_RARE)
    p = npc_pos(g)
    c.check(all(v in (SPOT['PLAZA_1'], SPOT['PLAZA_2'], SPOT['PLAZA_3'], SPOT['PLAZA_4'], SPOT['PLAZA_5'], SPOT['HALL'])
                for v in p.values()), f'the town gathers at the plaza {p}')
    g.go_face(11, 4, 'up'); g.act(); g.tick(30)
    c.check(g.menu_live(0, 0, STORE_N) is None, 'the store is closed for the festival')
    g.dismiss()
    hall_festival(g)
    g.tick(20); g.dismiss(btn='a'); g.wait_field()
    c.eq(g.sym8('_fest_on'), 1, 'egg hunt running')
    g.symw8('_egg_tmr', 0xFF); g.symw8('_egg_tmr', 0x7F, 1)   # plenty of time for the walking harness
    mask = sym32(g, '_egg_mask')
    eggs = [HIDE[i] for i in range(len(HIDE)) if mask >> i & 1]
    c.eq(len(eggs), 8, 'eight eggs hidden')
    empty = next(HIDE[i] for i in range(len(HIDE)) if not mask >> i & 1)
    g.use_on(*empty); g.tick(10)
    c.eq(g.sym8('_egg_got'), 0, 'an empty hiding spot')
    found = 0
    for (x, y) in eggs[:6]:
        g.use_on(x, y); g.tick(10)
        found += 1
        if found == 1:
            g.shot('found_egg')
        c.eq(g.sym8('_egg_got'), found, f'egg at {(x, y)}')
    g.shot('six_eggs')
    g.symw8('_egg_tmr', 3); g.symw8('_egg_tmr', 0, 1)   # time's up
    g.tick(30)
    g.shot('hunt_over')
    g.dismiss(); g.wait_field()
    c.eq(g.sym8('_fest_on'), 0, 'hunt over')
    fest_check(c, g, SEASON_SPRING, gold, seeds)
    c.eq(g.u8('fest_best', 0), 6, 'best: 6 eggs')
    open_start(g, 4); g.choose(8, 2, FARM_N, 4); g.tick(30)
    g.shot('ribbon_in_goods')
    c.eq(g.wtile(11, 12), GL_IT_STAR2, 'flower show ribbon in the GOODS log')
    g.dismiss()


@scenario('fest_koi_derby', base='town')
def s_fest_koi(c, g):
    fest_setup(g, SEASON_SUMMER, 14)
    gold, seeds = g.u32('gold'), g.seeds(FEST_CROP[1], R_RARE)
    hall_festival(g)
    caught = 0
    for cast in range(5):
        for _ in range(900):                 # wait for the bite (pressing early spooks the koi); A only closes lines
            if g.sym8('_fest_cue') == 1:
                break
            if g.ui_open():
                g.press('a')
            else:
                g.tick(1)
        c.require(g.sym8('_fest_cue') == 1, f'cast {cast + 1}: no bite')
        if cast == 0:
            g.shot('bite')
        g.press('a', 2, 2)
        g.tick(24)
        if cast == 0:
            g.shot('caught')
        caught += 1
    g.tick(30)
    g.dismiss(); g.wait_field()
    c.eq(g.pos(), (15, 13), 'fished at the fountain')
    fest_check(c, g, SEASON_SUMMER, gold, seeds)


@scenario('fest_judging', base='town')
def s_fest_judging(c, g):
    fest_setup(g, SEASON_FALL, 14)
    g.w8('inv', 1, I_PUMPKIN * 3 + 2)        # a gold-star pumpkin
    g.w8('inv', 2, I_RADISH * 3)
    gold, seeds = g.u32('gold'), g.seeds(FEST_CROP[2], R_RARE)
    hall_festival(g)
    g.wait_menu(0, 0, 2)
    g.shot('entry')
    g.choose(0, 0, 2, 0)                     # best quality first: the pumpkin
    for _ in range(600):                     # stop the polish needle in the middle
        if g.sym8('_fest_cue') in (7, 8) and g.ui_open() and g.wtile(2, 5) != GL_FILL:
            break
        g.tick(1)
    g.shot('polish')
    g.press('a', 2, 4)
    g.tick(20)
    g.shot('judges')
    g.dismiss(); g.wait_field()
    fest_check(c, g, SEASON_FALL, gold, seeds)
    c.eq(g.inv(I_PUMPKIN, 2), 1, 'the entry comes home with you')


@scenario('fest_snowball', base='town')
def s_fest_snowball(c, g):
    fest_setup(g, SEASON_WINTER, 25)
    gold, seeds = g.u32('gold'), g.seeds(FEST_CROP[3], R_RARE)
    hall_festival(g)
    keys = {1: 'left', 2: 'up', 3: 'right'}
    hits = 0
    for rnd_ in range(12):
        for _ in range(600):
            cue = g.sym8('_fest_cue')
            if cue:
                break
            if g.ui_open() and g.wtile(2, 9) != font_tile('L'):
                g.press('a')                 # the rules
            else:
                g.tick(1)
        if rnd_ == 2:
            g.shot('milo_pops_up')
        g.press(keys[cue], 2, 2)
        g.tick(30)
    g.tick(40)
    g.shot('snowball_done')
    g.dismiss(); g.wait_field()
    fest_check(c, g, SEASON_WINTER, gold, seeds)


def font_tile(ch):
    m = re.search(r'font_map\[\] = \{([^}]*)\}', open(os.path.join(ROOT, 'src/art_tables.c')).read())
    return _ints(m.group(1))[ord(ch) - 32]


# ------------------------------------------------------------------ the mine + the beach
MINE_LAYOUTS = (MAP_MINE_A, MAP_MINE_B, MAP_MINE_C)


def map_acts(map_id):
    """[(x, y, act)] of a static map, from src/maps.c (m<id>_acts)"""
    src = open(os.path.join(ROOT, 'src/maps.c')).read() + open(os.path.join(ROOT, 'src/maps2.c')).read()
    m = re.search(r'm%d_acts\[\] = \{(.*?)\};' % map_id, src)
    return [tuple(CONST[v] if v in CONST else int(v) for v in a.split(',')) for a in re.findall(r'\{([\w,]+)\}', m.group(1))]


def mine_layout(f):
    return (MAP_MINE_A, MAP_MINE_B, MAP_MINE_C)[f % 3]


def to_mine(g):
    """town -> the west road -> the mine entrance"""
    g.go(1, 4)
    g.step('left')
    g.wait_scene((MAP_MINE,))


def reach_from(g, start):
    """cells reachable on foot from start in the current scene"""
    seen, q = {start}, deque([start])
    while q:
        c = q.popleft()
        for dx, dy, _ in DIRS.values():
            n = (c[0] + dx, c[1] + dy)
            if n not in seen and g.walkable(*n):
                seen.add(n)
                q.append(n)
    return seen


def break_rock(c, g, x, y, maxhits=8):
    """walk up to the rock at (x,y) and hammer it until it breaks; returns hits"""
    g.w8('energy', 200)
    for d, (dx, dy, _) in DIRS.items():
        if g.walkable(x - dx, y - dy) and (x - dx, y - dy) in reach_from(g, g.pos()):
            break
    else:
        raise Fail(f'no way to stand next to the rock at {(x, y)}')
    g.go_face(x - dx, y - dy, d)
    g.w8('tool', T_HAMMER)
    for n in range(1, maxhits + 1):
        g.act()
        g.tick(14)
        if not g.rock_at(x, y):
            return n
    raise Fail(f'rock at {(x, y)} did not break')


def dig_to(c, g, tx, ty):
    """break rocks until the rock at (tx,ty) is broken (clearing a way to it first)"""
    for _ in range(60):
        if not g.rock_at(tx, ty):
            return
        here = reach_from(g, g.pos())
        # rocks touching the reachable area, the target first, then the one nearest the target
        cand = sorted({(x, y) for (x, y) in g.rocks() for dx, dy, _ in DIRS.values() if (x - dx, y - dy) in here},
                      key=lambda r: (r != (tx, ty), abs(r[0] - tx) + abs(r[1] - ty)))
        if not cand:
            raise Fail('no rock within reach')
        break_rock(c, g, *cand[0])
    raise Fail(f'could not dig to {(tx, ty)}')


def take_ladder(c, g):
    """find this floor's ladder (break its rock), step onto it, wait for the next floor"""
    f = g.u8('mine_floor')
    lx, ly = g.ladder()
    c.require(lx != 0xFF, f'floor {f} has a ladder')
    dig_to(c, g, lx, ly)
    g.tick(10)
    here = reach_from(g, g.pos())
    for d, (dx, dy, _) in DIRS.items():
        if (lx - dx, ly - dy) in here:
            break
    g.go(lx - dx, ly - dy)
    g.face(d)
    g.press(d, 4, 10)
    for _ in range(60):
        g.tick(10)
        if g.u8('mine_floor') == f + 1 and g.u8('scene') == mine_layout(f + 1):
            break
    g.tick(30)
    g.dismiss()
    g.wait_field()
    c.eq(g.u8('mine_floor'), f + 1, 'went down the ladder')


def use_lift(c, g, stop_idx, nstops):
    """stand in front of the lift (the act cell below the pulley), A, pick a stop (0 entrance, k floor 5k)"""
    acts = [a for a in map_acts(g.u8('scene')) if a[2] == ACT_LIFT]
    lx, ly = max(acts, key=lambda a: a[1])[:2]
    g.go_face(lx, ly + 1, 'up')
    g.act()
    g.choose(0, 1, nstops, stop_idx)
    want = (mine_layout(stop_idx * 5) if stop_idx else MAP_MINE, stop_idx * 5)
    for _ in range(60):
        g.tick(10)
        if (g.u8('scene'), g.u8('mine_floor')) == want:
            break
    g.tick(60)
    g.dismiss()
    g.wait_field()


@scenario('mine', base='town')
def s_mine(c, g):
    g.w8('energy', 200)
    to_mine(g)
    c.eq(g.pos(), (14, 3), 'arrive at the cave mouth')
    c.eq(g.u8('mine_floor'), 0, 'at the entrance')
    g.shot('entrance')
    # the notice sign
    g.go_face(3, 4, 'up')
    g.act()
    g.tick(20)
    c.check(g.ui_open(), 'the mine sign reads')
    g.shot('sign')
    g.dismiss()
    # the lift has no stops yet
    g.go_face(7, 3, 'up')
    g.act(); g.tick(20)
    c.check(g.ui_open(), 'the lift explains itself')
    g.dismiss()
    # the shaft ladder down to floor 1
    g.go(4, 3)
    g.step('down')
    for _ in range(60):
        g.tick(10)
        if g.u8('scene') == MAP_MINE_B:
            break
    g.tick(30); g.dismiss(); g.wait_field()
    c.eq(g.u8('scene'), MAP_MINE_B, 'floor 1 uses layout B')
    c.eq(g.u8('mine_floor'), 1, 'on floor 1')
    c.eq(g.u8('mine_deep'), 1, 'deepest floor 1')
    rocks = g.rocks()
    c.check(len(rocks) >= 12, f'floor 1 has rocks ({len(rocks)})')
    c.check(g.ladder() in rocks, 'the ladder hides under a rock')
    px, py = g.pos()
    c.check(all(abs(x - px) > 1 or abs(y - py) > 1 for x, y in rocks), 'the arrival spot is clear')
    g.shot('floor1')
    # break a rock with the basic hammer: energy and stone
    seed = g.sym8('_mn_seed')
    here = reach_from(g, g.pos())
    plain = [r for r in rocks if rock_kind(*r, seed, 1) in (0, 1) and r != g.ladder()
             and any((r[0] - dx, r[1] - dy) in here for dx, dy, _ in DIRS.values())]
    c.require(plain, 'a plain rock within reach')
    st, en = g.u16('stone'), g.u8('energy')
    hits = break_rock(c, g, *plain[0])
    g.shot('broke_rock')
    c.eq(hits, 1, 'a plain rock breaks in one hit')
    c.eq(g.u8('energy'), en - 2 * hits, '2 energy a strike')
    c.check(g.u16('stone') > st or g.inv(I_COAL) or g.inv(I_AMETHYST), 'stone (or a lucky find) from the rock')
    c.check(g.walkable(*plain[0]), 'the broken rock cell is open ground again')
    # an ore node takes more hits with the basic hammer
    here = reach_from(g, g.pos())
    ore = [r for r in g.rocks() if rock_kind(*r, seed, 1) in (2, 3, 5) and r != g.ladder()]
    if ore:
        k = rock_kind(*ore[0], seed, 1)
        item = {2: I_COPPER, 3: I_IRON, 5: I_COAL}[k]
        n0 = g.inv(item)
        dig_to(c, g, *ore[0])
        c.check(g.inv(item) > n0, f'the ore node gave {ITEM_NAME[item]}')
    else:
        c.note('no ore node on floor 1 this time')
    # the ladder, then down to floor 5 (a stronger hammer from here: one hit a rock)
    g.w8('tier', 3, TT_HAMMER)
    take_ladder(c, g)
    g.shot('floor2')
    c.eq(g.u8('scene'), MAP_MINE_C, 'floor 2 uses layout C')
    for f in (2, 3, 4):
        take_ladder(c, g)
    c.eq(g.u8('mine_floor'), 5, 'reached floor 5')
    c.eq(g.u8('mine_lift'), 5, 'the lift remembers floor 5')
    c.eq(g.u8('mine_deep'), 5, 'deepest floor 5')
    g.shot('floor5_lift_stop')
    # back up the ladder to the entrance, then ride the lift down to 5
    acts = [a for a in map_acts(g.u8('scene')) if a[2] == ACT_LADDERUP]
    ux, uy = acts[0][:2]
    g.go_face(ux, uy + 1, 'up')
    g.act()
    g.yesno(True)
    g.wait_scene((MAP_MINE,))
    c.eq(g.u8('mine_floor'), 0, 'climbed up to the entrance')
    use_lift(c, g, 1, 2)
    c.eq(g.u8('scene'), mine_layout(5), 'the lift went to floor 5')
    c.eq(g.u8('mine_floor'), 5, 'on floor 5 by lift')
    g.shot('lift_arrival')
    # START > map shows the floor
    open_start(g, 4)
    g.choose(8, 2, FARM_N, 1)                # FARM > MAP
    g.tick(20)
    g.shot('mine_map')
    c.check(any(g.wtile(x, 16) != GL_FILL for x in range(1, 12)), 'map page caption')
    g.press('b'); g.tick(6); g.dismiss()
    # deep down: lift to floor 20 (as if found on foot), find a gem node and break it
    g.w8('mine_lift', 20)
    use_lift(c, g, 4, 5)
    c.eq(g.u8('mine_floor'), 20, 'the lift goes to floor 20')
    c.eq(g.ladder()[0], 0xFF, 'no ladder below the bottom floor')
    g.shot('floor20')
    seed = g.sym8('_mn_seed')
    gems = [r for r in g.rocks() if rock_kind(*r, seed, 20) == 6]
    if not gems:                             # this floor's roll had none (about one floor in ten down here): each
        seed = next(v for v in range(256) if any(rock_kind(*r, v, 20) == 6 for r in g.rocks()))   # rock's kind is a
        g.symw8('_mn_seed', seed)            # hash of its place and the floor's seed, so another seed has one
        gems = [r for r in g.rocks() if rock_kind(*r, seed, 20) == 6]
    c.require(gems, 'a gem node on floor 20')
    for it in range(NUM_ITEMS):
        for q in range(3):
            g.w8('inv', 0, it * 3 + q)
    dig_to(c, g, *gems[0])
    g.tick(60)
    g.shot('gem')
    got = [it for it in GEM_ITEMS if g.inv(it)]
    c.check(len(got) == 1, f'a gem from the gem node ({[ITEM_NAME[i] for i in got]})')
    # sell it to Brick: ride up, walk to town
    use_lift(c, g, 0, 5)
    c.eq(g.u8('scene'), MAP_MINE, 'the lift went up to the entrance')
    for it in range(NUM_ITEMS):
        if it not in got:
            for q in range(3):
                g.w8('inv', 0, it * 3 + q)
    g.go(14, 3)
    g.step('right')
    g.wait_scene((MAP_TOWN,))
    c.eq(g.pos(), (1, 4), 'back in town on the west road')
    gem = got[0]
    q = next(q for q in range(3) if g.u8('inv', gem * 3 + q))
    gold = g.u32('gold')
    g.go_face(2, 4, 'up')
    g.act()
    g.choose(0, 0, 3, 1)
    g.wait_menu(0, 0, 1)
    g.shot('sell_gem')
    g.choose(0, 0, 1, 0)
    g.quantity(1)
    g.tick(10)
    g.dismiss()
    c.eq(g.u32('gold'), gold + ITEM_VALUE(gem, q), f'{ITEM_NAME[gem]} sold to Brick')
    c.check(g.u8('shipped', gem >> 3) & (1 << (gem & 7)),
            'the gem is in the collection log')


@scenario('mine_passout', base='town')
def s_mine_passout(c, g):
    """2am on a mine floor: you collapse and wake up at home"""
    to_mine(g)
    g.go(4, 3)
    g.step('down')
    for _ in range(60):
        g.tick(10)
        if g.u8('scene') == MAP_MINE_B:
            break
    g.tick(30); g.dismiss(); g.wait_field()
    c.eq(g.u8('mine_floor'), 1, 'on floor 1')
    day = g.u8('day')
    g.w16('time', 1550)
    for _ in range(200):
        g.tick(10)
        if g.ui_open():
            break
    g.tick(30)
    g.shot('collapse')
    g.dismiss()
    for _ in range(100):
        g.tick(10)
        if g.u8('day') != day:
            break
    g.after_night()
    g.shot('morning')
    c.eq(g.u8('scene'), SCENE_FARM, 'woke up on the farm')
    c.eq(g.u8('day'), day + 1, 'next day')


def beach_to(g):
    g.go(30, 15)
    g.step('right')
    g.wait_scene((MAP_BEACH,))


def fish_once(c, g, tag):
    """cast (already facing water with the rod), wait for the bite, reel it in; True if caught"""
    n0 = g.u16('stat_fish')
    g.act()
    g.tick(10)
    bite = False
    for _ in range(260):
        g.tick(1)
        if g.pb.get_sprite(2).tile_identifier == SPR_EXCLAIM:
            bite = True
            break
    if not bite:
        g.dismiss()
        return False
    g.press('a', 2, 2)
    g.tick(10)
    g.shot(f'reel_{tag}')
    held = False
    for f in range(3000):
        if not g.ui_open():
            break
        fish = zone = None
        for i in range(16):
            if g.wtile(2 + i, 13) == GL_IT_FISH:
                fish = i
            if zone is None and g.wtile(2 + i, 14) == GL_BAR_FULL:
                zone = i
        if fish is not None and zone is not None:
            want = fish > zone + 1
            if want != held:
                (g.pb.button_press if want else g.pb.button_release)('a')
                held = want
        elif fish is None and zone is None and f > 30:
            break
        g.tick(1)
    if held:
        g.pb.button_release('a')
    for _ in range(240):
        g.tick(1)
        if g.ui_open() or g.u16('stat_fish') > n0:
            break
    g.tick(30)
    g.shot(f'caught_{tag}')
    g.dismiss()
    return g.u16('stat_fish') > n0


@scenario('beach', base='town')
def s_beach(c, g):
    g.w8('energy', 200)
    beach_to(g)
    c.eq(g.pos(), (1, 5), 'arrive on the beach path')
    stamp = g.u8('beach_stamp')
    c.check(stamp != 0, 'today stamped')
    c.eq(g.u8('beach_taken'), 0, 'nothing picked yet today')
    g.shot('arrive')
    # forage everything that washed up today
    finds = [(k, beach_item(k, stamp, 0)) for k in range(len(BEACH_SPOTS)) if beach_item(k, stamp, 0) is not None]
    c.check(len(finds) >= 2, f'things washed up ({len(finds)})')
    for k, it in finds:
        x, y = BEACH_SPOTS[k]
        n0 = g.inv(it)
        g.use_on(x, y)
        g.tick(20)
        c.check(g.inv(it) == n0 + 1, f'picked up {ITEM_NAME[it]} at {(x, y)}')
        c.check(g.u8('beach_taken') & (1 << k), f'spot {k} taken')
        if k == finds[0][0]:
            g.shot('forage')
        g.dismiss()
    c.eq(beach_item(0, stamp, 0xFF), None, 'all taken')
    g.shot('foraged')
    # the beach shack's tide board
    g.go_face(12, 3, 'up')
    g.act(); g.tick(20)
    c.check(g.ui_open(), 'tide board reads')
    g.shot('tide_board')
    g.dismiss()
    # START > map
    open_start(g, 4)
    g.choose(8, 2, FARM_N, 1)                # FARM > MAP
    g.tick(20)
    g.shot('beach_map')
    c.check(any(g.wtile(x, 16) != GL_FILL for x in range(1, 12)), 'beach map caption')
    g.press('b'); g.tick(6); g.dismiss()
    # ocean fishing off the end of the pier
    g.rich()
    caught = False
    for attempt in range(5):
        g.go_face(15, 15, 'down')
        g.w8('tool', T_ROD)
        if fish_once(c, g, attempt):
            caught = True
            break
    c.check(caught, 'caught an ocean fish')
    ocean = [it for it in range(I_SARDINE, I_SQUID + 1) if g.inv(it)]
    c.check(len(ocean) == 1, f'the catch is an ocean fish ({[ITEM_NAME[i] for i in ocean]})')
    c.eq(sum(g.inv(i) for i in range(I_MINNOW, I_KOI + 1)), 0, 'no lake or river fish from the sea')
    # a new day: everything washes up again
    g.go(1, 5)
    g.step('left')
    g.wait_scene((MAP_TOWN,))
    c.eq(g.pos(), (30, 15), 'back in town on the south road')
    g.w8('day', g.u8('day') + 1)
    beach_to(g)
    c.check(g.u8('beach_stamp') != stamp, 'a new day')
    c.eq(g.u8('beach_taken'), 0, 'finds respawned overnight')
    g.shot('next_day')


# ------------------------------------------------------------------ cooking + the valley restoration
DISH_COUNT = CONST['NUM_DISHES']
I_DISH0_ = I_SALAD
BUFF_SPEED, BUFF_ALL = CONST['BUFF_SPEED'], CONST['BUFF_ALL']
VALLEY_GOLD = [int(v) for v in re.findall(r'\},\s*(\d+)\}', _table(_src('valley.c'), 'proj'))]


def kitchen(g):
    """A on the fridge (already faced): read any lines, wait for the recipe book"""
    g.act()
    g.wait_menu(0, 0, 9)


def step_frames(g, d):
    """frames from pressing a direction until the player lands on the next cell"""
    g.face(d)
    x, y = g.pos()
    g.pb.button_press(d)
    n = 0
    while g.pos() == (x, y) and n < 40:
        g.tick(1)
        n += 1
    g.pb.button_release(d)
    g.tick(8)
    return n


@scenario('cooking')
def s_cooking(c, g):
    """buy a fridge, find Grandpa's recipe cards, cook an omelet, learn a recipe from the TV show,
    eat dishes from the bag (energy + buffs on the HUD), buffs end overnight; dishes are loved gifts"""
    g.enter_house()
    g.w32('gold', 5000)
    furn_build(g, 0, FK_FRIDGE)
    g.go_face(6, 5, 'up')
    g.act(); g.tick(10)
    g.press('b'); g.tick(6)
    c.require(g.furn_at(6, 4) and g.furn_at(6, 4)[1] == FK_FRIDGE, 'fridge placed at 6,4')
    c.eq(g.u16('recipes'), 0, 'no recipes before the kitchen')
    g.w8('inv', 2, I_EGG * 3); g.w8('inv', 1, I_MILK * 3)
    g.w8('inv', 1, I_PUMPKIN * 3)
    kitchen(g)
    c.eq(g.u16('recipes'), 3, "Grandpa's cards: Salad + Omelet")
    g.shot('recipe_book')
    c.check(g.wtile(17, 2) == GL_IT_STAR2, 'omelet marked ready (gold star)')
    g.press('down', 2, 4)               # hover the omelet: its panel
    g.tick(10)
    g.shot('omelet_panel')
    g.press('down', 2, 4); g.press('down', 2, 4)    # an unknown recipe: the hint panel
    g.tick(10)
    g.shot('unknown_panel')
    g.choose(0, 0, 9, 1)                # Omelet
    g.tick(10)
    g.shot('quantity')
    g.press('a')                        # x1
    g.tick(12)
    g.shot('cooking')
    g.wait_field()
    g.tick(140)                         # the chop-sizzle-steam animation runs after the book closes
    g.shot('cooked')
    c.eq(g.inv(I_OMELET), 1, 'one omelet cooked')
    c.eq(g.inv(I_EGG), 0, 'two eggs used')
    c.eq(g.inv(I_MILK), 0, 'milk used')
    c.eq(g.inv(I_PUMPKIN), 1, 'unrelated goods untouched')
    c.eq(g.u16('stat_cooked'), 1, 'cooked count')
    # missing ingredients: nothing happens
    kitchen(g)
    g.choose(0, 0, 9, 1)
    g.tick(20); g.shot('missing')
    g.dismiss()
    c.eq(g.inv(I_OMELET), 1, 'no omelet without eggs')
    # page two, then out
    kitchen(g)
    g.choose(0, 0, 9, 8)
    g.wait_menu(0, 0, 9)
    g.tick(10); g.shot('page2')
    g.dismiss()
    # the Sunday cooking show teaches Fish Fry
    g.w8('day', 7)
    x, y, d = g.furn_spot(FK_TV)
    g.go_face(x, y, d)
    g.act(); g.tick(80); g.shot('tv_show')
    g.dismiss()
    c.check(g.u16('recipes') & (1 << (I_FISHFRY - I_DISH0_)), 'TV show taught Fish Fry')
    # eat the omelet from the bag: +45 energy, no buff
    g.w8('energy', 10)
    open_start(g, 2)
    g.tick(20)
    g.press('down', 2, 4)               # the omelet, under the pumpkin
    g.tick(10); g.shot('bag_dish')
    g.press('a')
    g.yesno(True)
    g.tick(20); g.shot('ate')
    g.dismiss(); g.wait_field()
    c.eq(g.inv(I_OMELET), 0, 'omelet eaten')
    c.eq(g.u8('energy'), 55, 'omelet restores 45 energy')
    c.eq(g.u8('buff'), 0, 'omelet gives no buff')
    c.eq(g.wtile(4, 0), GL_HUD_BLANK, 'no buff mark on the HUD')
    # Apple Pie: Brisk -- you walk at a run
    slow = step_frames(g, 'left')
    g.w8('inv', 1, I_APPLEPIE * 3)
    open_start(g, 2)
    g.tick(10)
    g.press('down', 2, 4)               # apple pie sits under the pumpkin in the bag
    g.press('a')
    g.yesno(True)
    g.dismiss(); g.wait_field()
    c.check(g.u8('buff') & BUFF_SPEED, 'apple pie: Brisk buff')
    c.eq(g.wtile(4, 0), GL_HUD_BUFF, 'buff mark on the HUD')
    g.shot('buff_hud')
    fast = step_frames(g, 'right')
    c.check(fast < slow, f'brisk walking is faster ({fast} vs {slow} frames a step)')
    # Feast: every buff, energy to full
    g.w8('inv', 1, I_FEAST * 3)
    g.w8('energy', 5)
    open_start(g, 2)
    g.tick(10)
    g.press('down', 2, 4)
    g.press('a')
    g.yesno(True)
    g.dismiss(); g.wait_field()
    c.eq(g.u8('buff'), BUFF_ALL, 'feast: every buff')
    c.eq(g.u8('energy'), g.u8('energy_max'), 'feast: full energy')
    # buffs end with the day
    g.sleep(stay=True)
    c.eq(g.u8('buff'), 0, 'buffs cleared overnight')
    c.eq(g.wtile(4, 0), GL_HUD_BLANK, 'HUD buff mark gone in the morning')


def hall_board(g):
    """town hall (ordinary day): Joe's menu -> VALLEY: the restoration board"""
    g.go_face(16, 9, 'up'); g.act()
    g.choose(0, 0, HALL_N, 1)                # VALLEY
    g.wait_menu(0, 0, CONST['NUM_PROJ'], advance='a')


def donate_all(g, n):
    """in a project's list (n item rows + the gold row): give every row"""
    for k in range(n + 1):
        g.choose(0, 0, n + 1, k)
        g.yesno(True)
        g.tick(10)
        if k < n:
            g.wait_menu(0, 0, n + 1)


@scenario('valley', base='town')
def s_valley(c, g):
    """the Valley Restoration board: donate the Town Garden's crops + gold, the park becomes a garden,
    the Sorbet recipe and a letter from Grandpa arrive; the FARM > VALLEY page shows it"""
    g.w32('gold', 9000)
    need = {I_RADISH: 5, I_TOMATO: 3, I_PUMPKIN: 2, I_SNOWPEA: 2}
    for it, n in need.items():
        g.w8('inv', n + 1, it * 3)
    hall_board(g)
    c.check(g.u8('cook_flags') & 2, 'Joe introduced the board')
    g.tick(10); g.shot('board')
    g.choose(0, 0, 6, 0)                # Town Garden
    g.wait_menu(0, 0, 5)
    g.tick(10); g.shot('project')
    g.choose(0, 0, 5, 0)                # radish x5
    g.yesno(True); g.tick(10)
    c.eq(g.inv(I_RADISH), 1, 'five radishes donated')
    c.eq(g.u8('valley', 0), 1, 'radish slot done')
    g.wait_menu(0, 0, 5)
    g.shot('one_donated')
    for k in (1, 2, 3):
        g.choose(0, 0, 5, k); g.yesno(True); g.tick(10)
        g.wait_menu(0, 0, 5)
    c.eq(g.u8('valley_done'), 0, 'not done before the gold')
    g.choose(0, 0, 5, 4)                # G1000
    g.yesno(True)
    g.tick(40); g.shot('complete')
    g.wait_menu(0, 0, 6)
    g.shot('board_after')
    g.dismiss()
    c.check(g.u8('valley_done') & 1, 'Town Garden restored')
    c.eq(g.u32('gold'), 9000 - VALLEY_GOLD[0], 'gold paid')
    for it in need:
        c.eq(g.inv(it), 1, f'{ITEM_NAME[it]} donated')
    c.check(g.u16('recipes') & (1 << (I_SORBET - I_DISH0_)), 'Sorbet recipe as the reward')
    c.check(g.u32('mail') & (1 << CONST['MAIL_VALLEY']), "Grandpa's letter is on its way")
    g.go(15, 17)
    g.tick(20)
    g.shot('park_garden')
    # the progress page
    open_start(g, 4)
    g.choose(8, 2, FARM_N, 6)
    g.tick(30); g.shot('valley_page')
    c.eq(g.wtile(17, 2), GL_IT_STAR2, 'garden starred on the VALLEY page')
    g.dismiss()
    # the letter
    to_farm(g)
    g.go_face(13, 9, 'left'); g.act()
    g.tick(60); g.shot('letter')
    g.dismiss()
    c.eq(g.u32('mail') & (1 << CONST['MAIL_VALLEY']), 0, 'letter read')


@scenario('valley_finale', base='town')
def s_valley_finale(c, g):
    """the last project (the Valley Fountain) after the other five: the town celebrates, credits roll,
    the fountain is rebuilt, the lamps glow and the park is an orchard; then the game goes on"""
    g.w8('cook_flags', 2)
    g.w8('valley_done', 0x1F)
    for p in range(5):
        g.w8('valley', 0x3F, p)
    g.w8('valley', 0x1F, 5)             # every item given; only the gold is left
    g.w32('gold', VALLEY_GOLD[5] + 5000)
    g.w16('time', 960)                  # 4pm: the hall is open (Joe keeps 9 to 5)
    hall_board(g)
    g.shot('board_all')
    g.choose(0, 0, 6, 5)
    g.wait_menu(0, 0, 6)
    g.choose(0, 0, 6, 5)                # the gold row
    g.yesno(True)
    shots = 0
    for _ in range(80):                 # read the celebration until the credits card is up
        g.tick(30)
        if g.ui_open() and g.wtile(3, 0) == GL_HDR_L:   # the credits card's title tab
            break
        if shots < 3 and g.ui_open():
            g.shot(f'celebration_{shots}'); shots += 1
        g.press('a')
    g.tick(400)
    g.shot('credits')
    c.check(g.u8('valley_done') & 0x20, 'Valley Fountain finished')
    g.press('a'); g.tick(20)
    g.dismiss()
    g.wait_field()
    c.eq(g.u8('valley_done'), 0xBF, 'all six done + the celebration seen')
    c.eq(g.u32('gold'), VALLEY_GOLD[5] + 5000 - VALLEY_GOLD[5], 'fountain gold paid')
    c.eq(g.pos(), (15, 14), 'the celebration ends in front of the fountain')
    g.w16('time', 1200)                 # 8pm: the lamps
    g.tick(20); g.shot('fountain')
    g.go(10, 14)
    g.tick(20); g.shot('lamps')
    g.go(15, 18)
    g.tick(20); g.shot('orchard')
    c.eq(g.u8('scene'), MAP_TOWN, 'still playing in town')


# ------------------------------------------------------------------ town investment (biz.c, invest.c)
MK_OPEN, MK_GATE_Y = CONST['MK_OPEN'], CONST['MK_GATE_Y']
STYLE_PRICE = CONST.get('STYLE_PRICE', 4000)
INV_MARKET_PRICE, INV_LOT_PRICE = (int(re.search(r'#define %s (\d+)' % k, _invest_c).group(1)) for k in ('MARKET_PRICE', 'LOT_PRICE'))
BIZ_BREWERY, BIZ_RESTAURANT, BIZ_INN = CONST['BIZ_BREWERY'], CONST['BIZ_RESTAURANT'], CONST['BIZ_INN']
LOT_Y = 23


def biz_door(b, lot=None):
    """the cell in front of a business's door (bottom middle of its 3x3)"""
    if b < 4:
        x, y = BIZ_SPOT[b]
        return x + 1, y + 3
    return LOT_X[lot] + 1, LOT_Y + 3


def biz_menu(g, b, idx, lot=None):
    g.go_face(*biz_door(b, lot), 'up')
    g.act()
    g.choose(0, 0, BIZ_MENU_N, idx)


def prosperity_of(g):
    pts = sum((2 if b < 4 else 3) for b in range(8) if g.u8('biz_owned') & (1 << b))
    pts += sum((g.u8('biz_look', b) >> 2) & 3 for b in range(8) if g.u8('biz_owned') & (1 << b))
    pts += 2 if g.u8('mk_flags') & MK_OPEN else 0
    pts += 4 * bin(g.u8('town_proj') & 0x3F).count('1')
    return sum(1 for t in _ints(_table(_invest_c, 'pts_level')) if pts >= t)


def to_market(g, x, y):
    """through the open gate south of the park"""
    g.go(13, 20)
    g.go(x, y)


@scenario('storefront_styles', base='town')
def s_storefront_styles(c, g):
    """renovate the bakery in Modern style, add the three facade tiers (art changes each time, prices),
    every storefront in a different look; the looks survive a save + continue"""
    g.w8('biz_owned', 1)
    g.w32('gold', 100000)
    re_enter_town(g)
    g.go(BIZ_SPOT[0][0] + 1, BIZ_SPOT[0][1] + 4)
    g.shot('classic_t0')
    before = bg_tiles(g)
    biz_menu(g, 0, 3)                                  # Facade
    g.wait_menu(0, 0, 3)
    g.tick(10); g.shot('facade_menu')
    g.choose(0, 0, 3, 0)                               # Style
    g.wait_menu(0, 0, 4)
    g.press('down'); g.tick(20)
    g.shot('style_preview_modern')
    g.press('a')
    g.yesno(True); g.tick(20)
    g.wait_menu(0, 0, 3)
    c.eq(g.u8('biz_look', 0) & 3, 1, 'bakery is Modern now')
    c.eq(g.u32('gold'), 100000 - STYLE_PRICE, 'renovation price')
    gold = g.u32('gold')
    for t in range(3):
        g.choose(0, 0, 3, 1)                           # Upgrade
        g.wait_menu(14, YN_Y, 2)
        if t == 0:
            g.shot('upgrade_preview')
        g.yesno(True); g.tick(20)
        g.wait_menu(0, 0, 3)
        c.eq((g.u8('biz_look', 0) >> 2) & 3, t + 1, f'facade tier {t + 1}')
        gold -= BIZ_TIER_PRICE[t]
        c.eq(g.u32('gold'), gold, f'tier {t + 1} price')
    g.tick(10); g.shot('facade_t3_menu')
    g.dismiss()
    g.go(BIZ_SPOT[0][0] + 1, BIZ_SPOT[0][1] + 4)
    g.tick(10); g.shot('modern_t3')
    c.check(bg_tiles(g) != before, 'the storefront was redrawn')
    # one storefront in each style, at different tiers
    g.w8('biz_owned', 0x0F)
    looks = [1 | 3 << 2, 2 | 1 << 2, 3 | 2 << 2, 0 | 3 << 2]
    for b, lk in enumerate(looks):
        g.w8('biz_look', lk, b)
    re_enter_town(g)
    g.go(27, 11); g.tick(10); g.shot('north_row')
    g.go(4, 20); g.tick(10); g.shot('creamery_cottage')
    g.go(27, 20); g.tick(10); g.shot('florist_classic')
    # status shows the look and the customer bonus
    biz_menu(g, 1, 1)
    g.tick(20); g.shot('status'); g.dismiss()
    # the looks are saved
    to_farm(g)
    g.sleep()
    boot_title(g)
    g.choose(1, 9, 2, 0)
    g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    for b, lk in enumerate(looks):
        c.eq(g.u8('biz_look', b), lk, f'business {b} look after save + continue')
    if g.in_house():
        g.leave_house()
    g.w16('time', 600)
    to_town(g)
    g.go(27, 11); g.tick(10); g.shot('after_continue')


@scenario('market_street', base='town')
def s_market_street(c, g):
    """Market Street is closed until bought at the Land Office (prosperity 1+); buy it and a lot, build a
    Cabin brewery (scaffold overnight), stock it, and it brews and sells ales the next night"""
    g.go_face(13, 20, 'down')
    g.act(); g.tick(20); g.shot('gate_for_sale'); g.dismiss()
    g.press('down', 4, 20)
    c.eq(g.pos(), (13, 20), 'the closed gate blocks the way')
    g.w8('biz_owned', 0x03)                           # two storefronts: prosperity 1
    g.w32('gold', 200000)
    c.eq(prosperity_of(g), 1, 'prosperity 1')
    g.go_face(24, 14, 'up'); g.act()
    g.choose(0, 3, 2, 1)                               # Land Office: Market St
    g.wait_menu(14, YN_Y, 2)
    g.shot('buy_market')
    g.yesno(True)
    g.wait_menu(0, 3, 4)
    c.check(g.u8('mk_flags') & MK_OPEN, 'Market Street bought')
    g.tick(10); g.shot('lots')
    g.choose(0, 3, 4, 0)                               # lot 1
    g.yesno(True)
    g.wait_menu(0, 3, 4)
    g.dismiss()
    c.check(g.u8('mk_flags') & 1, 'lot 1 bought')
    c.eq(g.u32('gold'), 200000 - INV_MARKET_PRICE - INV_LOT_PRICE, 'district + lot price')
    to_market(g, 13, 24)
    g.tick(10); g.shot('market_street_open')
    g.go_face(*biz_door(4, 0), 'up'); g.act()
    g.wait_menu(0, 0, 4)
    g.tick(10); g.shot('build_menu')
    g.choose(0, 0, 4, 0)                               # Brewery
    g.choose(0, 0, 4, 2)                               # Log Cabin
    g.yesno(True); g.tick(20); g.dismiss()
    c.check(g.u8('biz_owned') & (1 << BIZ_BREWERY), 'brewery owned')
    c.eq(g.u8('biz_lot', 0), 1, 'on lot 1')
    c.eq(g.u8('biz_look', BIZ_BREWERY), 2 | 0x80, 'Log Cabin, building tonight')
    g.go(3, 27); g.tick(10); g.shot('scaffold')
    g.go_face(*biz_door(4, 0), 'up'); g.act(); g.tick(10); g.shot('builders'); g.dismiss()
    to_farm(g)
    g.morning_find = ('Grand opening', 'grand_opening')
    g.sleep()
    c.eq(g.u8('biz_look', BIZ_BREWERY), 2, 'open this morning')
    g.w16('time', 600)
    to_town(g)
    g.w8('inv', 10, I_CORN * 3)
    g.w8('inv', 5, I_HERB * 3)
    to_market(g, 3, 27)
    g.tick(10); g.shot('brewery_cabin')
    biz_menu(g, 4, 0, lot=0)                           # Stock
    g.choose(0, 0, 2, 0); g.quantity(10); g.tick(10)
    g.choose(0, 0, 1, 0); g.quantity(5); g.tick(10)
    g.dismiss()
    c.eq(g.u8('bz_pantry', 0 * 8 + 0), 10, '10 corn on the shelf')
    c.eq(g.u8('bz_pantry', 0 * 8 + 7), 5, '5 herbs on the shelf')
    to_farm(g)
    gold = g.u32('gold')
    p = prosperity_of(g)
    g.morning_find = ('Brewery sold', 'brewery_report')
    g.sleep()
    want = 3 * ((ITEM_PRICE[I_CORN] + ITEM_PRICE[I_HERB]) * BIZ_PQ[BIZ_BREWERY] // 4) * (100 + BIZ_PCT_PROSP * p) // 100
    c.eq(g.u16('bz_today', 0), want, f'three craft ales (herb + corn) x {BIZ_PQ[BIZ_BREWERY] / 4}, + prosperity')
    c.eq(g.u8('bz_sold', 0), 3, 'sold 3')
    c.eq(g.u32('gold') - gold, want, 'paid overnight')


@scenario('restaurant_inn', base='town')
def s_restaurant_inn(c, g):
    """a restaurant serving cooked dishes with fresh sides, an inn with rooms + breakfasts (and the inn's
    visitors lift the restaurant); hiring a manager costs a nightly wage"""
    g.w8('mk_flags', MK_OPEN | 0x06)
    g.w8('biz_owned', (1 << BIZ_RESTAURANT) | (1 << BIZ_INN))
    g.w8('biz_lot', 2, 2); g.w8('biz_lot', 3, 3)
    g.w8('biz_look', 3 | 2 << 2, BIZ_RESTAURANT); g.w8('biz_look', 1 | 3 << 2, BIZ_INN)
    g.w32('gold', 50000)
    g.w8('inv', 4, I_OMELET * 3); g.w8('inv', 4, I_TOMATO * 3)
    re_enter_town(g)
    to_market(g, 21, 27)
    g.tick(10); g.shot('restaurant_inn')
    biz_menu(g, BIZ_RESTAURANT, 0, lot=1)              # Stock: the bag's dishes and produce
    g.wait_menu(0, 0, 2)
    g.tick(10); g.shot('restaurant_stock')
    g.choose(0, 0, 2, 0); g.quantity(4); g.tick(10)    # tomato (item order)
    g.choose(0, 0, 1, 0); g.quantity(4); g.tick(10)    # omelet (the tomatoes are all shelved)
    g.dismiss()
    shelf = {g.u8('rest_item', k) - 1: g.u8('bz_pantry', 2 * 8 + k) for k in range(8) if g.u8('rest_item', k)}
    c.eq(shelf, {I_TOMATO: 4, I_OMELET: 4}, 'restaurant shelves')
    for k, (it, n) in enumerate(((I_EGG, 2), (I_MILK, 2), (I_HONEY, 2))):
        g.w8('bz_pantry', n, 3 * 8 + k)
    biz_menu(g, BIZ_INN, 1, lot=2)
    g.tick(20); g.shot('inn_status'); g.dismiss()
    to_farm(g)
    p = prosperity_of(g)
    g.morning_find = ('businesses earned', 'two_businesses_report')
    g.sleep()
    tiers_r, tiers_i = 2, 3
    rest = 4 * ((ITEM_PRICE[I_OMELET] + ITEM_PRICE[I_TOMATO]) * BIZ_PQ[BIZ_RESTAURANT] // 4)   # 6 + 2 (facade) ingredients: 4 specials
    c.eq(g.u16('bz_today', 4), rest * (100 + BIZ_PCT_TIER * tiers_r + BIZ_PCT_PROSP * p + BIZ_PCT_INN) // 100,
         'four chef specials, + facade, prosperity, inn visitors')
    inn = 2 * ((ITEM_PRICE[I_EGG] + ITEM_PRICE[I_MILK] + ITEM_PRICE[I_HONEY]) * BIZ_PQ[BIZ_INN] // 4) + (4 + tiers_i + p) * INN_RATE
    c.eq(g.u16('bz_today', 6), inn * (100 + BIZ_PCT_TIER * tiers_i + BIZ_PCT_PROSP * p) // 100, 'two breakfasts + the rooms')
    c.eq(sum(g.u8('rest_item', k) for k in range(8)), 0, 'empty restaurant shelves are freed')
    # a manager
    g.w16('time', 600)
    to_town(g)
    to_market(g, 27, 27)
    biz_menu(g, BIZ_INN, 4, lot=2)                     # Staff
    g.yesno(True); g.tick(20); g.dismiss()
    c.check(g.u8('biz_staffed') & (1 << BIZ_INN), 'inn manager hired')
    to_farm(g)
    gold = g.u32('gold')
    g.sleep()
    c.eq(gold - g.u32('gold') + g.u16('bz_today', 6) + g.u16('bz_today', 4), 40, 'the manager wage (40) came out first')


@scenario('community', base='town')
def s_community(c, g):
    """Joe's INVEST: the overview, a community project (Square Paving: the streets change, prosperity
    rises), a locked tier, the commons once Market Street is open, the library's recipe, Alyssa's goods"""
    g.w32('gold', 150000); g.w16('wood', 300); g.w16('stone', 300)
    g.w8('biz_owned', 0x01); g.w16('biz_today', 1234, 0)
    g.go(16, 10); g.tick(10); g.shot('plaza_before')
    before = bg_tiles(g)
    g.go_face(16, 9, 'up'); g.act()
    g.choose(0, 0, HALL_N, HALL_N - 2)                # INVEST (before BYE)
    g.wait_menu(0, 0, 5)
    c.check(g.u8('inv_tips') & 1, 'Joe pitched investing')
    g.choose(0, 0, 5, 0)                               # Overview
    g.tick(20); g.shot('overview')
    g.press('a'); g.tick(10)
    g.choose(0, 0, 5, 1)                               # Community
    g.wait_menu(0, 0, 6)
    g.tick(10); g.shot('projects')
    g.choose(0, 0, 6, 2)                               # Boardwalk: needs prosperity 2
    g.tick(20); g.shot('locked'); g.wait_menu(0, 0, 6)
    c.eq(g.u8('town_proj'), 0, 'locked project not built')
    g.choose(0, 0, 6, 0)                               # Square Paving
    g.yesno(True); g.tick(30); g.shot('paving_done')
    g.wait_menu(0, 0, 6)
    c.check(g.u8('town_proj') & CONST['TP_PAVING'], 'paving built')
    c.eq((g.u32('gold'), g.u16('stone')), (150000 - TPROJ[0][0], 300 - TPROJ[0][2]), 'gold + stone donated')
    c.eq(prosperity_of(g), 1, 'prosperity 1 now')
    g.dismiss()
    g.go(16, 10); g.tick(10); g.shot('plaza_paved')
    c.check(bg_tiles(g) != before, 'the plaza changed')
    # more of the town: the commons, the boardwalk (poked), Alyssa's goods at prosperity 2+
    g.w8('mk_flags', MK_OPEN); g.w8('biz_owned', 0x0F)
    g.go_face(16, 9, 'up'); g.act()
    g.choose(0, 0, HALL_N, HALL_N - 2)
    g.choose(0, 0, 5, 1)
    g.choose(0, 0, 6, 1)                               # Playground (Market Street commons)
    g.yesno(True); g.tick(30)
    g.wait_menu(0, 0, 6)
    c.check(g.u8('town_proj') & CONST['TP_PLAYGROUND'], 'playground built')
    g.dismiss()
    g.w8('town_proj', 0x3F)
    re_enter_town(g)
    g.go(28, 15); g.tick(10); g.shot('boardwalk')
    to_market(g, 13, 27)
    g.tick(10); g.shot('commons_west')
    g.go(19, 27); g.tick(10); g.shot('commons_east')
    rec = g.u16('recipes')
    g.go(21, 31); g.go_face(21, 31, 'up'); g.act(); g.tick(30); g.shot('library'); g.dismiss()
    c.check(g.u16('recipes') != rec, 'the library gave a recipe')
    g.go_face(11, 4, 'up'); g.act()
    g.wait_menu(0, 0, STORE_N + 1)
    g.shot('store_goods')
    g.choose(0, 0, STORE_N + 1, 6)                     # Goods
    g.choose(0, 3, 3, 0); g.quantity(2); g.tick(10)
    c.eq(g.inv(I_EGG), 2, "bought 2 eggs from Alyssa's traders")
    g.dismiss()


@scenario('market_showcase', base='town')
def s_market_showcase(c, g):
    """the whole town invested in: every storefront in its own style and facade, Market Street with its four
    businesses, all six community projects; in the evening Joe walks down to the bandstand"""
    g.w8('biz_owned', 0xFF)
    g.w8('mk_flags', MK_OPEN | 0x0F)
    for k in range(4):
        g.w8('biz_lot', k + 1, k)
    looks = [0 | 3 << 2, 1 | 2 << 2, 3 | 3 << 2, 2 | 1 << 2, 2 | 3 << 2, 3 | 2 << 2, 1 | 3 << 2, 0 | 3 << 2]
    for b, lk in enumerate(looks):
        g.w8('biz_look', lk, b)
    g.w8('town_proj', 0x3F)
    re_enter_town(g)
    c.eq(prosperity_of(g), 5, 'a thriving town: prosperity 5')
    g.go(16, 10); g.tick(10); g.shot('plaza')
    g.go(27, 11); g.tick(10); g.shot('north_shops')
    g.go(14, 20); g.tick(10); g.shot('gate_open')
    to_market(g, 6, 26); g.tick(10); g.shot('market_west')
    g.go(24, 26); g.tick(10); g.shot('market_east')
    g.go(8, 31); g.tick(10); g.shot('commons_west')
    g.go(20, 31); g.tick(10); g.shot('commons_east')
    g.go_face(16, 27, 'down'); g.act(); g.tick(20); g.shot('bandstand'); g.dismiss()
    # evenings on Market Street: Joe's 7pm stroll in the park ends at the bandstand now
    g.w16('time', 1140)
    re_enter_town(g)
    c.check(wait_npcs(g, {NPC_HOLLIS: SPOT['BAND']}, 2400), f'Joe at the bandstand at 7pm {npc_pos(g)}')
    to_market(g, 14, 25); g.tick(10); g.shot('evening_bandstand')


# ------------------------------------------------------------------ economy (tools/econ_model.py, docs/BALANCE.md)
def pass_night(g):
    """the quickest real night: stay up past 2am and collapse (world_overnight(1)), then the morning report"""
    day = g.u8('day')
    g.w16('time', 1550)
    for _ in range(200):
        g.tick(10)
        if g.ui_open():
            break
    g.dismiss()
    for _ in range(100):
        g.tick(10)
        if g.u8('day') != day:
            break
    g.after_night()


def owned_soil(g):
    """(tilled tiles, growing crops) on the home valley (the only land these scenarios own)"""
    soil = [i for i in range(1024) if g.u8('cell', i) == C_SOIL]
    return len(soil), sum(1 for i in soil if g.u8('a1', i) & SOIL_CROP not in (0, CROP_WITHERED))


@scenario('econ_year')
def s_econ_year(c, g):
    """a scripted year of real nights (112, by passing out) on a mid-game farm: a greenhouse strawberry field with a
    waterer and a harvester, six Jersey cows with a rancher, a stocked Bakery (kitchen 1, awning + window boxes,
    a manager) and a Brewery on Market Street. Every night the ROM's wages and business takings must equal
    tools/econ_model.py's formulas exactly, and the year's shipping must land within a few percent of the
    model's expected value (quality rolls are dice)"""
    import econ_model as M
    g.w32('gold', 400000)
    for t in (B_BARN, B_GREENHOUSE):                       # Grandpa's barn and greenhouse, repaired
        k = g.bld_find(t)
        g.w8('bld', g.u8('bld', k * BLD_SIZE + 3) & ~BF_RUIN, k * BLD_SIZE + 3)
    barn = g.bld_find(B_BARN)
    field = [(x, y) for y in range(14, 20) for x in range(10, 18) if (x, y) != (13, 16)]   # home field, owned
    for n, (x, y) in enumerate(field):                     # fertilized strawberries, growth staggered over 8 days
        g.setcell(x, y, C_SOIL, 3 | SOIL_FERT | SOIL_WET, n % CROP_DAYS[3])
    g.setcell(13, 16, C_DECOR, DK_SCARECROW, 0)            # guards the whole field from crows
    g.setcell(9, 14, C_DECOR, DK_ROD, 0)                   # and the rod from summer lightning
    for r in (ROLE_WATER, ROLE_HARVEST, ROLE_RANCH):
        g.w8('staff', r + 1, r)
    for k in range(6):
        g.set_anim(k, A_COW, barn, happy=220, age=30, flags=R_RARE << 6, name='Cow')
    g.w16('hay', 999)
    # the Bakery: kitchen 1, facade tier 2, a manager; the Brewery on lot 1, kitchen 0
    g.w8('biz_owned', 0x01 | (1 << BIZ_BREWERY))
    g.w8('biz_staffed', 0x01)
    g.w8('biz_level', 1, 0)
    g.w8('biz_look', 2 << 2, 0)
    g.w8('mk_flags', MK_OPEN | 1)
    g.w8('biz_lot', 1, 0)
    pan_b, pan_w = BIZ_PAN[0], BIZ_PAN[BIZ_BREWERY]
    stock = {('biz_pantry', 0, pan_b.index(it)): 0 for it in (I_EGG, I_MILK, I_PUMPKIN)}
    stock.update({('bz_pantry', 0, pan_w.index(it)): 0 for it in (I_CORN, I_HERB)})
    g.w8('weather', 0); g.w8('tomorrow', 0)
    ship_total = want_ship = biz_total = 0.0
    bad_biz, bad_wage = [], []
    milk = 2 * M.PRICE[M.I['MILK']] + 2 * (M.PRICE[M.I['MILK']] >> 1)      # a rare cow: two gold-star milk a night
    for night in range(M.DAYS_YEAR):
        for (field_name, blk, slot) in stock:              # the player keeps the shelves stocked
            if g.u8(field_name, blk * CONST['PANTRY'] + slot) < 30:
                g.w8(field_name, 99, blk * CONST['PANTRY'] + slot)
        pan0 = {k: g.u8('biz_pantry', k) for k in range(CONST['PANTRY'])}
        pan4 = {k: g.u8('bz_pantry', k) for k in range(CONST['PANTRY'])}
        soil, crops = owned_soil(g)
        plots = g.plots_n()
        n_anim = len(g.animals())
        want_wage = [M.wage(r, plots, soil, crops, n_anim) for r in range(3)]
        P = M.prosperity(sum((2 if b < 4 else 3) for b in range(8) if g.u8('biz_owned') & (1 << b))
                         + sum((g.u8('biz_look', b) >> 2) & 3 for b in range(8) if g.u8('biz_owned') & (1 << b))
                         + (2 if g.u8('mk_flags') & MK_OPEN else 0) + 4 * bin(g.u8('town_proj') & 0x3F).count('1'))
        v0 = M.run_one(0, pan0, 1, 2, True)[0]
        v4 = M.run_one(BIZ_BREWERY, pan4, 0, 0, False)[0]
        want0 = min(65535, int(v0 * M.biz_mult(2, P, True, False, 0)))
        want4 = min(65535, int(v4 * M.biz_mult(0, P, False, False, BIZ_BREWERY)))
        ripe = sum(1 for (x, y) in field if g.cell(x, y) == C_SOIL and (g.a1(x, y) & SOIL_CROP) == 3
                   and (g.a2(x, y) & SOIL_DAYS) >= CROP_DAYS[3] - 1)
        gold = g.u32('gold')
        pass_night(g)
        got0, got4 = g.u16('biz_today', 0), g.u16('bz_today', 0)
        if (got0, got4) != (want0, want4):
            bad_biz.append((night, got0, want0, got4, want4))
        got_w = [g.u16('staff_wage', 2 * r) for r in range(3)]   # (u16 index = byte offset)
        if got_w != want_wage:
            bad_wage.append((night, got_w, want_wage))
        mgr = M.MGR_WAGE[0] + M.MGR_WAGE[1] * 1
        shipped = g.u32('gold') - gold - got0 - got4 + sum(got_w) + mgr
        ship_total += shipped
        biz_total += got0 + got4
        # the model's expectation for tonight: every strawberry that ripens is picked and shipped, every cow milked
        fed = sum(1 for _, a in g.animals() if a['flags'] & AF_FED)   # fed last night: milk waiting tonight
        want_ship += ripe * M.harvest_value(3, 0, 1, hand=False) + fed * milk
        if night in (0, 27, 55, 83, 111):
            g.shot(f'night_{night + 1:03d}')
        if g.u8('staff', ROLE_HARVEST) == 0 or g.u8('staff', ROLE_WATER) == 0:
            c.check(False, f'a farmhand quit on night {night + 1}')
            break
    c.eq(bad_biz[:3], [], 'business takings = econ_model run_one x biz_mult, every night (night, got, want)')
    c.eq(bad_wage[:3], [], 'farmhand wages = econ_model wage(), every night (night, got, want)')
    c.eq((g.u8('year'), g.u8('season'), g.u8('day')), (2, SEASON_SPRING, 1), 'a full year later: Year 2, Spring 1')
    ratio = ship_total / max(1.0, want_ship)
    c.notes.append(f'year: shipped G{ship_total:.0f} vs model G{want_ship:.0f} (x{ratio:.3f}); shops G{biz_total:.0f}')
    c.check(0.94 <= ratio <= 1.06, f'shipping within 6% of the model (x{ratio:.3f})')
    c.check(g.u32('gold') > 400000, f'the farm made money over the year (G{g.u32("gold")})')
    # the HUD's gold: thousands with a k past 9999, clamped at 999k
    g.w32('gold', 345678); g.tick(450)
    c.eq([g.wtile(x, 0) for x in (16, 17, 18)], [GL_HUD_0 + 3, GL_HUD_0 + 4, GL_HUD_0 + 5], 'HUD gold 345k')
    c.eq(g.wtile(19, 0), GL_HUD_K, 'k suffix')
    g.shot('hud_345k')
    g.w32('gold', 1234567); g.tick(450)                    # a millionaire: four figures over the coin, not stuck at 999k
    c.eq([g.wtile(x, 0) for x in range(15, 20)], [GL_HUD_0 + 1, GL_HUD_0 + 2, GL_HUD_0 + 3, GL_HUD_0 + 4, GL_HUD_K],
         'HUD gold 1234k')
    g.shot('hud_1234k')


@scenario('balance_rules', base='town')
def s_balance_rules(c, g):
    """the balance pass's rules on the ROM: food restores at most one more full bar a day (Raza's and dishes share it),
    a legendary regrower settles into a rare one after its first crop, Alyssa's traders' goods cost 4x"""
    g.w32('gold', 50000)
    em = g.u8('energy_max')
    g.w8('energy', 1)
    g.w16('time', 480)                                     # 8am: Raza is behind her counter
    g.go_face(19, 4, 'up'); g.act()
    g.choose(0, 4, 4, 2)                                   # the feast: back to full
    g.tick(20); g.dismiss(); g.wait_field()
    c.eq(g.u8('energy'), em, 'a feast fills the bar')
    c.eq(g.u8('ate'), em - 1, 'what it restored is counted')
    c.eq(g.u32('gold'), 50000 - CAFE_PRICE[2], 'feast price')
    for k in range(2):                                     # coffee: still allowed (the bar isn't used up), then not
        g.w8('energy', 1)
        g.w16('time', 480)                                 # 8am: Raza is behind her counter
        g.go_face(19, 4, 'up'); g.act()
        g.choose(0, 4, 4, 0)
        g.tick(20)
        if k:
            g.shot('too_full')
        g.dismiss(); g.wait_field()
    c.eq(g.u32('gold'), 50000 - CAFE_PRICE[2] - CAFE_PRICE[0], 'no charge once full')
    c.check(g.u8('ate') >= em, 'a full bar eaten today')
    # a dish is refused too
    for it in range(NUM_ITEMS * 3):                        # only an omelet in the bag
        g.w8('inv', 0, it)
    g.w8('inv', 1, I_OMELET * 3)
    g.w8('energy', 10)
    to_farm(g)
    open_start(g, 2)
    g.tick(20)
    g.press('a')
    g.tick(20); g.shot('dish_too_full')
    g.dismiss(); g.wait_field()
    c.eq(g.inv(I_OMELET), 1, 'the omelet is still in the bag')
    c.eq(g.u8('energy'), 10, 'no energy from it')
    pass_night(g)
    c.eq(g.u8('ate'), 0, 'a new day, a new appetite')
    # a legendary strawberry: 3-4 at the first crop, then the bush is a rare one
    g.setcell(9, 14, C_SOIL, 3 | SOIL_WET, (R_LEGEND << 6) | CROP_DAYS[3])
    clear_area(g, 9, 13, 9, 13)
    have = g.inv(I_STRAWB)
    g.go_face(9, 13, 'down'); g.w8('tool', T_HAND); g.act(); g.tick(10); g.dismiss()
    c.check(g.inv(I_STRAWB) - have in (3, 4), f'legendary first crop 3-4 (got {g.inv(I_STRAWB) - have})')
    c.eq(g.a1(9, 14) & SOIL_CROP, 3, 'the strawberry stays planted')
    c.eq(g.a2(9, 14) >> 6, R_RARE, 'and settles into a rare one')
    c.eq(g.a2(9, 14) & SOIL_DAYS, CROP_DAYS[3] - CROP_REGROW[3], 'regrowing')
    # Alyssa's money box for a millionaire
    g.w32('gold', 1234567)
    to_town(g)
    g.w16('time', 600)
    g.go_face(11, 4, 'up'); g.act()
    g.wait_menu(0, 0, STORE_N)
    g.shot('money_box_1234k')
    g.dismiss(); g.wait_field()


BASES = {'farm': (None, new_game), 'cleared': ('farm', make_cleared), 'town': ('farm', make_town)}


def get_base(g, bases, name, qa):
    if name not in bases:
        parent, maker = BASES[name]
        g.outdir = os.path.join(qa, '_base_' + name)
        if parent is None:
            g.boot(fresh=True)
        else:
            g.restore(get_base(g, bases, parent, qa))
        maker(g)
        bases[name] = g.snapshot()
    return bases[name]


# ------------------------------------------------------------------ runner
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('names', nargs='*')
    ap.add_argument('--rom', default=os.path.join(ROOT, 'build/stilwell_farms.gbc'))
    ap.add_argument('--list', action='store_true')
    args = ap.parse_args()
    if args.list:
        print('\n'.join(SCEN))
        return 0
    names = args.names or list(SCEN)
    qa = os.path.join(ROOT, 'build/qa')
    os.makedirs(qa, exist_ok=True)
    rom = os.path.join(qa, 'qa.gbc')            # private copy: its battery file is ours to wipe
    shutil.copy(args.rom, rom)
    shutil.copy(args.rom.replace('.gbc', '.map'), rom.replace('.gbc', '.map'))
    if os.path.exists(args.rom.replace('.gbc', '.noi')):
        shutil.copy(args.rom.replace('.gbc', '.noi'), rom.replace('.gbc', '.noi'))
    g = Game(rom, qa)
    bases = {}
    results = []
    for name in names:
        fn, base = SCEN[name]
        out = os.path.join(qa, name)
        shutil.rmtree(out, ignore_errors=True)
        c = Ctx(g, name)
        t0 = time.time()
        status = 'PASS'
        try:
            if base is None:
                g.boot(fresh=True)
            else:
                data = get_base(g, bases, base, qa)
                g.boot(fresh=True)
                g.restore(data)
            g.outdir = out
            fn(c, g)
            if c.fails:
                status = 'FAIL'
        except Hang as e:
            status = 'HANG'
            c.fails.append(str(e))
        except Fail as e:
            status = 'FAIL'
            c.fails.append('fatal: ' + str(e))
        except Exception as e:
            status = 'ERROR'
            c.fails.append(traceback.format_exc())
            try:
                g.shot('ERROR')
            except Exception:
                pass
        dt = time.time() - t0
        results.append((name, status, c.fails, c.notes))
        print(f'{status:5} {name:18} {dt:5.1f}s' + ''.join(f'\n      - {m}' for m in c.fails + c.notes), flush=True)
    g.stop()
    npass = sum(r[1] == 'PASS' for r in results)
    print(f'\n{npass}/{len(results)} scenarios passed')
    return 0 if npass == len(results) else 1


if __name__ == '__main__':
    sys.exit(main())
