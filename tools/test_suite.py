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


for _f in ('art.h', 'maps.h', 'game.h', 'decor.h', 'interior.c', 'menus.c', 'actors.c'):
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
PLOT_PRICE = _ints(_table(_world_c, 'plot_prices'))
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
BIZ_PAN = [[CONST[n.strip()] for n in row.split(',')] for row in re.findall(r'\{([^{}]*)\}', _table(_biz_c, 'pan_item'))]
_staff_c = _src('staff.c')
WAGE_BASE = _ints(_table(_staff_c, 'wage_base'))
WAGE_PLOT = _ints(_table(_staff_c, 'wage_plot'))
_town_c = _src('town.c')
STORE_N = len(re.findall(r'"[^"]*"', _table(_town_c, 'store_items')))
TIER_GOLD = _ints(_table(_town_c, 'tier_gold'))
CAFE_PRICE = _ints(_table(_town_c, 'price'))
BREED_PRICE = [_ints(r) for r in re.findall(r'\{([^{}]*)\}', _table(_town_c, 'breed_price'))]
RAR_MULT = _ints(_table(_town_c, 'rar_mult'))
SCENE_FARM = CONST['SCENE_FARM']
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
    src = open(os.path.join(ROOT, 'src/maps.c')).read()
    m = re.search(r'm%d_warps\[\] = \{(.*?)\};' % map_id, src)
    return [tuple(int(v) for v in w.split(',')) for w in re.findall(r'\{([\d,]+)\}', m.group(1))]


def map_kind(map_id):
    """walkability (K_* bits) of a static map, from the generated src/maps.c (m<id>_kind)"""
    src = open(os.path.join(ROOT, 'src/maps.c')).read()
    m = re.search(r'm%d_kind\[1024\] = \{([^}]*)\}' % map_id, src)
    return [int(v) for v in m.group(1).split(',')]


# ------------------------------------------------------------------ the driver
class Game:
    def __init__(self, rom, outdir):
        self.rom = rom
        self.sym = parse_symbols(rom.replace('.gbc', '.map'))
        self.G = self.sym['_G']
        nxt = min(a for n, a in self.sym.items() if a > self.G and n.startswith('_') and not n.startswith('__'))
        assert nxt - self.G == GSIZE, f'GameState layout mismatch: _G is {nxt - self.G} bytes, game.h says {GSIZE}'
        self.WAIT_LO = self.sym['_wait_vbl']
        self.WAIT_HI = self.WAIT_LO + 0x30
        self.kinds = {m: map_kind(m) for m in (MAP_TOWN, MAP_HOUSE1, MAP_HOUSE2)}
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
        return self.pb.memory[self.addr(name, i)]

    def u16(self, name, i=0):
        a = self.addr(name, i)
        return self.pb.memory[a] | (self.pb.memory[a + 1] << 8)

    def u32(self, name, i=0):
        a = self.addr(name, i)
        return sum(self.pb.memory[a + k] << (8 * k) for k in range(4))

    def w8(self, name, v, i=0):
        self.pb.memory[self.addr(name, i)] = v & 0xFF

    def w16(self, name, v, i=0):
        a = self.addr(name, i)
        self.pb.memory[a] = v & 0xFF
        self.pb.memory[a + 1] = (v >> 8) & 0xFF

    def w32(self, name, v, i=0):
        a = self.addr(name, i)
        for k in range(4):
            self.pb.memory[a + k] = (v >> (8 * k)) & 0xFF

    def sym8(self, name, i=0):
        return self.pb.memory[self.sym[name] + i]

    def symw8(self, name, v, i=0):
        self.pb.memory[self.sym[name] + i] = v

    def cell(self, x, y):
        return self.u8('cell', y * 32 + x)

    def a1(self, x, y):
        return self.u8('a1', y * 32 + x)

    def a2(self, x, y):
        return self.u8('a2', y * 32 + x)

    def setcell(self, x, y, c, a1=0, a2=0):
        self.w8('cell', c, y * 32 + x); self.w8('a1', a1, y * 32 + x); self.w8('a2', a2, y * 32 + x)

    def inv(self, it, q=None):
        if q is None:
            return sum(self.u8('inv', it * 3 + k) for k in range(3))
        return self.u8('inv', it * 3 + q)

    def bld(self, k):
        return tuple(self.u8('bld', k * BLD_SIZE + j) for j in range(4))

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

    def pos(self):
        return self.u8('px'), self.u8('py')

    def owned(self, x, y):
        return (self.u16('plots') >> (((y >> 3) << 2) | (x >> 3))) & 1

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
        return [(m[b + k], m[b + n + k], m[b + 2 * n + k], m[b + 3 * n + k]) for k in range(n) if m[b + k]]

    def shoo_pet(self):
        """test-only: put the pet on the player's own cell so it stops blocking a corridor"""
        b, n = self.sym['_act_kind'], MAX_ACT
        for k in range(n):
            if self.pb.memory[b + k] == K_PET:
                self.pb.memory[b + 2 * n + k], self.pb.memory[b + 3 * n + k] = self.pos()

    def actor_at(self, x, y):
        for a in self.actors():
            if a[2:] == (x, y):
                return a
        return None

    def walkable(self, x, y, scene=None):
        if not (0 <= x < 32 and 0 <= y < 32):
            return False
        if self.actor_at(x, y):
            return False
        scene = self.u8('scene') if scene is None else scene
        if scene in (MAP_HOUSE1, MAP_HOUSE2):
            if not self.kinds[scene][y * 32 + x] & K_WALK:
                return False
            f = self.furn_at(x, y)
            return f is None or f[1] == FK_RUG
        if scene != SCENE_FARM:
            return y < 24 and bool(self.tk[y * 32 + x] & K_WALK)
        c = self.cell(x, y)
        if c == C_ROAD:
            return True
        if not self.owned(x, y):
            return False
        if c == C_DECOR:
            return self.a1(x, y) in DECOR_WALK
        return c in (C_GRASS, C_GRASS2, C_WILDFLOWER, C_DIRT, C_SOIL, C_SAND)

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
        if g.menu_sel(1, 9, 3) is not None:
            return
    raise Fail('PRESS START never reached the slot menu')


def new_game(g, slot_row=0, name_farm=False):
    """title -> PRESS START -> pick a farm slot (row 0/1) -> intro"""
    title_start(g)
    g.choose(1, 9, 3, slot_row)
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


@scenario('intro', base=None)
def s_intro(c, g):
    g.tick(240)
    g.shot('title')
    title_start(g)
    g.wait_menu(1, 9, 3)
    g.shot('slots')
    g.choose(1, 9, 3, 0)
    g.tick(90)
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
def to_town(g):
    g.go(15, 30)
    g.step('down')                      # arriving on y=31 facing down warps
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
    c.eq(g.u16('time'), 360, 'morning time')
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
    c.eq(g.u32('gold'), 5000 - PLOT_PRICE[13], 'paid the South Pasture price')
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
    c.eq(g.pos(), (15, 30), 'back on the farm road')
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
    g.wait_menu(0, 3, 12)
    g.shot('realtor')
    g.choose(0, 3, 12, 9)               # South Pasture
    g.wait_menu(14, YN_Y, 2)
    g.shot('buy_prompt')
    g.yesno(True); g.tick(20); g.dismiss()
    c.check(g.u16('plots') & (1 << 13), 'bought South Pasture at the land office')
    c.eq(g.u32('gold'), 5000 - CAFE_PRICE[0] - PLOT_PRICE[13], 'paid the South Pasture price')


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
    g.go_face(16, 9, 'up'); g.act(); g.choose(0, 0, 4, 0); g.tick(20); g.shot('hall'); g.dismiss()
    # spring festival with 8 flower beds
    g.w8('day', 14)
    for k in range(8):
        g.setcell(8 + k, 15, C_DECOR, DK_FLOWERBED)
    gold = g.u32('gold')
    g.go_face(16, 9, 'up'); g.act(); g.choose(0, 0, 4, 0); g.tick(20); g.shot('festival'); g.dismiss()
    c.eq(g.u32('gold'), gold + 500, 'flower show prize')
    c.check(g.u8('festival_won') & 1, 'festival won flag')
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
    c.require(talked, 'could not reach Rosa')
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
    c.eq(ready, [[False, True, False], [False, True, True], [False, True, True], [False, True, True], [True, True, True]],
         'keg 5 days, press 1, jar 2')
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
    for k, name in enumerate(('status', 'map', 'calendar', 'friends', 'goods', 'staff')):
        open_start(g, 4)
        if name == 'calendar':
            g.w8('req_qty', 3); g.w8('req_day', 5)
        g.choose(8, 2, 7, k)
        g.tick(40)
        g.shot(name)
        if name == 'calendar':
            marks = [g.wtile(x, y) for y in range(3, 10) for x in range(4, 19)]
            c.eq(marks.count(GL_CAL_FEST), 1, 'one festival flag (day 14)')
            c.eq(marks.count(GL_CAL_DOT), 1, 'one request-due dot (day 6)')
        g.dismiss()
    c.check(not g.ui_open(), 'menus closed')


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
    g.choose(1, 9, 3, 0)
    g.choose(8, 4, 3, 0)                # Continue
    g.tick(60)
    g.dismiss(); g.wait_field()
    g.shot('continued')
    c.eq(g.u32('gold'), 12345, 'slot 1 gold restored')
    c.eq(g.u8('scene'), SCENE_FARM, 'on the farm')
    # new game in slot 2, sleep (auto-save), reload it
    boot_title(g)
    g.choose(1, 9, 3, 1)
    g.tick(60)
    g.yesno(False, advance='a')
    g.tick(20); g.dismiss(); g.wait_field()
    c.eq(g.u32('gold'), 500, 'slot 2 is a new farm')
    g.w32('gold', 777)
    g.sleep()
    boot_title(g)
    g.shot('two_slots')
    g.choose(1, 9, 3, 1)
    g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    c.eq((g.u32('gold'), g.u8('day')), (777, 2), 'slot 2 restored after its morning auto-save')
    boot_title(g)
    g.choose(1, 9, 3, 0)
    g.choose(8, 4, 3, 0)
    g.tick(60); g.dismiss(); g.wait_field()
    c.eq(g.u32('gold'), 12345, 'slot 1 untouched by slot 2')


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
    g.choose(0, 3, 12, 0); g.yesno(True); g.tick(60)
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
    g.choose(1, 9, 3, 0)
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
    g.choose(0, 0, 4, 1)                         # HIRE HELP
    g.choose(0, 0, 4, role)
    g.shot(f'hire_{role}')
    if expect:
        g.yesno(True)
    g.tick(20)
    g.dismiss()


def wage(g, role):
    plots = bin(g.u16('plots')).count('1')
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
    g.choose(8, 2, 7, 5)                         # FARM > STAFF
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
    g.choose(0, 0, 4, 0)                         # Stock
    g.choose(0, 0, 1, 0)
    g.quantity(6)
    g.tick(10)
    g.dismiss()
    pan = BIZ_PAN[0]
    c.eq(g.u8('biz_pantry', pan.index(I_STRAWB)), 6, '6 strawberries on the shelf')
    c.eq(g.inv(I_STRAWB), 0, 'taken from the bag')
    g.act()
    g.choose(0, 0, 4, 1)                         # Status
    g.tick(20); g.shot('status'); g.dismiss()
    to_farm(g)
    gold = g.u32('gold')
    g.morning_shot = 'biz_report'
    g.sleep()
    want = BIZ_CAP[0] * ITEM_PRICE[I_STRAWB] * 10 // 4
    c.eq(g.u16('biz_today', 0), want, 'six tarts at 2.5x')
    c.eq(g.u32('gold') - gold, want, 'income paid overnight')
    c.eq(g.u8('biz_sold', 0), 6, 'sold 6')
    c.eq(g.u8('biz_pantry', pan.index(I_STRAWB)), 0, 'pantry used up')
    # premium: one of each base (egg, milk) + a main
    for it, n in ((I_EGG, 1), (I_MILK, 1), (I_APPLE, 1)):
        g.w8('biz_pantry', n, pan.index(it))
    g.sleep()
    prem = (ITEM_PRICE[I_EGG] + ITEM_PRICE[I_MILK] + ITEM_PRICE[I_APPLE]) * 14 // 4
    c.eq(g.u16('biz_today', 0), prem, 'a pie: (egg + milk + apple) x 3.5')
    # upgrade
    g.w16('time', 600)
    to_town(g)
    gold = g.u32('gold')
    g.go_face(*door, 'up'); g.act()
    g.choose(0, 0, 4, 2)
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
    """rare packets cost 3x and yield 2; legendary is locked until G20000 earned, yields 3-4"""
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
    g.w32('earned', 20000)
    g.go_face(11, 4, 'up'); g.act()
    g.choose(0, 0, STORE_N, 0)
    g.choose(0, 0, 3, R_LEGEND)
    g.choose(0, 0, 4, 0)
    g.quantity(1); g.tick(10)
    g.dismiss()
    c.eq(g.seeds(1, R_LEGEND), 1, 'legendary radish seed after G20000 earned')
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
    for night in range(40):
        g.set_anim(0, A_COW, barn, happy=230, age=30, flags=AF_FED | (R_RARE << 6), name='Ma')
        g.set_anim(1, A_COW, barn, happy=230, age=30, flags=AF_FED | (R_LEGEND << 6), name='Pa')
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
    # babies give nothing until grown
    g.w8('anim', AF_FED | (a['flags'] & 0xC0), k * ANIM_SIZE + ANIM_OFF['flags'])
    g.sleep(stay=True)
    c.check(not g.anim(k)['flags'] & AF_READY, 'a calf gives no milk')
    g.w8('anim', ADULT_AGE, k * ANIM_SIZE + ANIM_OFF['age'])
    g.w8('anim', AF_FED | (a['flags'] & 0xC0), k * ANIM_SIZE + ANIM_OFF['flags'])
    g.sleep()
    c.check(g.anim(k)['flags'] & AF_READY, 'grown up: milk')


def v3_bytes(g):
    """the current GameState rewritten in the v3 layout (39 items, seeds without rarity), with its checksum"""
    raw = bytes(g.pb.memory[g.G + i] for i in range(GSIZE))
    inv = OFF['inv']
    seeds = OFF['seeds']
    old = bytearray(raw[:inv + 39 * 3])
    old += bytes(raw[seeds + k * NUM_RARITY] for k in range(NUM_CROPS + 1))
    old += raw[OFF['saplings']:OFF['furn_init']]
    old[0], old[1], old[2] = SAVE_MAGIC & 0xFF, SAVE_MAGIC >> 8, 3
    s = 0x1234
    for b in old:
        s = (((s << 1) | (s >> 15)) + b) & 0xFFFF
    return bytes(old) + bytes([s & 0xFF, s >> 8])


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
    sav = bytearray(open(ram, 'rb').read()) if os.path.exists(ram) else bytearray(0x8000)
    sav[0:len(data)] = data
    sav[0x1000:0x1002] = b'\0\0'                 # slot 2 empty
    open(ram, 'wb').write(bytes(sav))
    g.boot()
    title_start(g)
    g.choose(1, 9, 3, 0)
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
