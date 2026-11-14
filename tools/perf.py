#!/usr/bin/env python3
"""Frame-budget profiler: plays the ROM headless in PyBoy and measures how much of each frame the
game's main loop uses, by hooking wait_vbl_done (every idle loop parks there) and the VBL vector.

  busy = CPU cycles from the VBlank that ended the previous wait_vbl_done to the next call of it.
  busy > 100% of a frame = a dropped frame (the loop missed a VBlank).

Usage: .venv/bin/python tools/perf.py [SCENARIO ...] [--rom build/stilwell_farms.gbc]
Scenarios: farm, dense, town, menus, scenes, night (default: all)
"""
import argparse, os, re, random, sys, warnings
warnings.filterwarnings('ignore')
from collections import deque
from pyboy import PyBoy

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIELDS = [('magic', 2), ('version', 1), ('cell', 1024), ('a1', 1024), ('a2', 1024), ('plots', 2),
          ('bld', 80), ('anim', 132), ('gold', 4), ('earned', 4), ('ship_value', 4), ('time', 2),
          ('day', 1), ('season', 1), ('year', 1), ('energy', 1), ('energy_max', 1), ('px', 1), ('py', 1),
          ('facing', 1), ('scene', 1), ('tool', 1), ('seed_sel', 1), ('build_sel', 1), ('build_cat', 1),
          ('tier', 5), ('style', 1), ('water', 1), ('weather', 1), ('tomorrow', 1), ('inv', 120),
          ('seeds', 45), ('saplings', 4), ('fert', 1), ('wood', 2), ('stone', 2), ('hay', 2), ('friend', 6),
          ('talked', 1), ('gifted', 1), ('shipped', 6), ('goals', 4), ('mail', 4), ('req', 4),
          ('pet_love', 1), ('pet_flags', 1), ('pet_name', 6), ('farm_name', 9), ('opt_text', 1),
          ('opt_music', 1), ('opt_sfx', 1), ('opt_clock', 1), ('opt_run', 1), ('tips', 1), ('stats', 6),
          ('festival_won', 1), ('rng', 2), ('furn_init', 1), ('furn', 72), ('staff', 5), ('staff_unpaid', 1),
          ('staff_wage', 10), ('biz_owned', 1), ('biz_staffed', 1), ('biz_level', 4), ('biz_pantry', 32)]
OFF = {}
_o = 0
for _n, _s in FIELDS:
    OFF[_n] = _o
    _o += _s
SCENE_FARM, MAP_TOWN = 0xFE, 4
C_GRASS, C_SOIL, C_DECOR, C_TREE, C_STRUCT, C_WATER, C_ROAD, C_MACHINE = 0, 10, 11, 12, 13, 14, 15, 23
C_PINE, C_BIRCH, C_BUSH, C_ORE, C_ROCK, C_WEEDS, C_STUMP, C_WILDFLOWER = 16, 17, 18, 20, 4, 3, 6, 2
DIRS = {'down': (0, 1, 0), 'up': (0, -1, 1), 'left': (-1, 0, 2), 'right': (1, 0, 3)}


def town_kind():
    src = open(os.path.join(ROOT, 'src/maps.c')).read()
    m = re.search(r'm%d_kind\[1024\] = \{([^}]*)\}' % MAP_TOWN, src)
    return [int(v) for v in m.group(1).split(',')]


class Prof:
    def __init__(self, rom):
        txt = open(rom.replace('.gbc', '.map')).read()
        self.sym = {n: int(a, 16) for a, n in re.findall(r'([0-9A-F]{8})\s+([_.A-Za-z][\w.]*)', txt)}
        self.G = self.sym['_G']
        ram = rom + '.ram'
        if os.path.exists(ram):
            os.remove(ram)
        self.pb = PyBoy(rom, window='null', cgb=True, sound_emulated=False)
        self.pb.set_emulation_speed(0)
        self.waiting = False
        self.exit_vbl = 0
        self.last_vbl = 0
        self.frame_cyc = []
        self.rec = None          # list of (busy_cycles, caller) while recording
        self.caller = None
        self.pb.hook_register(0, self.sym['_wait_vbl'], self._on_wait, None)
        self.pb.hook_register(0, 0x40, self._on_vbl, None)
        self.FRAME = None
        # full symbol names (build.sh links with -Wl-j): stamp the entry of a few functions
        self.noi = {}
        noi = rom.replace('.gbc', '.noi')
        if os.path.exists(noi):
            for line in open(noi):
                m = re.match(r'DEF _(\w+) 0x([0-9A-Fa-f]+)', line)
                if m:
                    self.noi[m.group(1)] = int(m.group(2), 16)
        self.stamps = []
        for f in ('enter_scene', 'start_menu', 'fade_out', 'morning_report', 'world_overnight', 'save_write',
                  'view_reset', 'fade_in', 'ui_menu', 'staff_overnight', 'biz_overnight', 'goals_check'):
            if f in self.noi:
                a = self.noi[f]
                self.pb.hook_register(0 if (a & 0xFFFF) < 0x4000 else a >> 16, a & 0xFFFF, self._stamp, f)

    def _on_vbl(self, _):
        c = self.pb._cycles()
        if self.last_vbl and len(self.frame_cyc) < 200:
            self.frame_cyc.append(c - self.last_vbl)
        self.last_vbl = c
        if self.waiting:
            self.exit_vbl = c
            self.waiting = False

    def _stamp(self, f):
        self.stamps.append((f, self.pb._cycles()))

    def since(self, f, until, after=0):
        """frames from the first stamp f (after cycle `after`) to the first stamp/wait `until` after it"""
        t0 = next((c for n, c in self.stamps if n == f and c >= after), None)
        if t0 is None:
            return None
        t1 = next((c for n, c in self.stamps if n == until and c > t0), None)
        return None if t1 is None else (t1 - t0) / self.frame()

    def _on_wait(self, _):
        c = self.pb._cycles()
        sp = self.pb.register_file.SP
        self.caller = self.pb.memory[sp] | (self.pb.memory[sp + 1] << 8)
        if self.caller == getattr(self, 'FIELD', None):
            self.stamps.append(('FIELD', c))
        if self.rec is not None and self.exit_vbl:
            self.rec.append((c - self.exit_vbl, self.caller))
        self.waiting = True

    # ---- helpers
    def tick(self, n=1):
        for _ in range(n):
            self.pb.tick()

    def u8(self, f, i=0):
        return self.pb.memory[self.G + OFF[f] + i]

    def poke(self, f, i, v):
        self.pb.memory[self.G + OFF[f] + i] = v

    def pos(self):
        return self.u8('px'), self.u8('py')

    def press(self, b, n=3, after=4):
        self.pb.button_press(b); self.tick(n); self.pb.button_release(b); self.tick(after)

    def frame(self):
        if not self.FRAME:
            fc = sorted(self.frame_cyc[5:])
            self.FRAME = fc[len(fc) // 2]
        return self.FRAME

    def start(self):
        """mash through title + intro until the field loop is idle (its wait_vbl_done call sits in
        field_loop, the static function just before main())"""
        lo, hi = self.sym['_walkable'], self.sym['_main']
        self.tick(120)
        for k in range(600):
            self.tick(12)
            if lo < self.caller < hi and self.u8('scene') == SCENE_FARM:
                self.FIELD = self.caller
                self.tick(30)
                return
            self.press('a' if k % 3 else 'start', 3, 12)
        sys.exit('start: never reached the field')

    def record(self, fn):
        self.rec = []
        fn()
        r, self.rec = self.rec, None
        return r


def summarize(name, rec, F, only=None):
    xs = [b for b, c in rec if only is None or c in only]
    if not xs:
        print(f'  {name:34s} (no frames)')
        return None
    xs.sort()
    drops = sum(1 for b in xs if b > F)
    mx = xs[-1] / F * 100
    p95 = xs[int(len(xs) * .95)] / F * 100
    avg = sum(xs) / len(xs) / F * 100
    print(f'  {name:34s} frames={len(xs):5d}  avg={avg:5.1f}%  p95={p95:5.1f}%  max={mx:6.1f}%  dropped={drops}')
    return dict(frames=len(xs), avg=avg, p95=p95, max=mx, drops=drops)


# ------------------------------------------------------------------ scenarios
def corridors(p, row, col):
    p.poke('tips', 0, 0xFF)          # no first-day tip dialogs mid-run
    for x in range(32):
        p.poke('cell', row * 32 + x, C_ROAD)
    for y in range(32):
        p.poke('cell', y * 32 + col, C_ROAD)


def no_pet(p):
    """the pet follows you around and can stand in the corridor: switch it off (actors.c act_kind)"""
    a = p.sym['_act_kind']
    for k in range(8):
        if p.pb.memory[a + k] == 3:
            p.pb.memory[a + k] = 0


def run_to(p, d, steps):
    """hold B (run) + direction for a number of cells (keeps holding until there, if blocked a while)"""
    dx, dy, _ = DIRS[d]
    x, y = p.pos()
    tx, ty = x + dx * steps, y + dy * steps
    if p.u8('scene') == SCENE_FARM:
        no_pet(p)
    p.pb.button_press('b')
    p.pb.button_press(d)
    for _ in range(steps * 4 + 200):
        p.tick(1)
        if p.pos() == (tx, ty) or p.caller != p.FIELD:
            break
    p.pb.button_release(d)
    p.pb.button_release('b')
    p.tick(8)
    unstick(p)


def unstick(p):
    for _ in range(10):
        if p.caller == p.FIELD:
            return
        p.press('a', 2, 20)


def walk_to(p, d, steps):
    p.pb.button_press(d)
    p.tick(steps * 8 + 6)
    p.pb.button_release(d)
    p.tick(10)


def scroll_tour(p, label):
    x, y = p.pos()
    res = {}
    for d, n in (('right', 31 - x), ('left', 31), ('right', x), ('up', y), ('down', 31 - y), ('up', 31 - y)):
        r = p.record(lambda: run_to(p, d, n))
        res.setdefault(d, []).extend(r)
    walk = p.record(lambda: (walk_to(p, 'right', 8), walk_to(p, 'left', 8)))
    out = {}
    for d in ('right', 'left', 'up', 'down'):
        out[d] = summarize(f'{label} run {d}', res[d], p.frame(), {p.FIELD})
    out['walk'] = summarize(f'{label} walk l/r', walk, p.frame(), {p.FIELD})
    return out


def make_dense(p, row, col, seed=1):
    """fill the farm with a worst-case mix: every plot owned, water bodies, buildings, fences, lamps,
    crops of every stage, trees, machines -- lots of distinct cache tiles on screen at once"""
    rnd = random.Random(seed)
    p.poke('plots', 0, 0xFF); p.poke('plots', 1, 0xFF)
    # buildings on a grid
    types = [1, 2, 3, 4, 5, 6, 7, 8, 9]
    bw = [3, 3, 2, 3, 1, 2, 3, 2, 2, 2]
    bh = [3, 3, 2, 3, 2, 2, 2, 3, 2, 2]
    slot = 1
    occupied = set()
    for i in range(1024):
        cx, cy = i % 32, i // 32
        if cy == row or cx == col:
            continue
        r = rnd.random()
        if r < .18:
            c, a1, a2 = C_WATER, 0, 0
        elif r < .40:
            c, a1, a2 = C_SOIL, rnd.randint(1, 14) | (0x80 if rnd.random() < .5 else 0), rnd.randint(0, 12)
        elif r < .62:
            kind = rnd.randint(2, 32)
            c, a1, a2 = C_DECOR, kind, rnd.randint(0, 2)
        elif r < .72:
            c, a1, a2 = C_TREE, (rnd.randint(0, 3) << 5) | rnd.randint(0, 9), rnd.randint(5, 20)
        elif r < .78:
            c, a1, a2 = C_MACHINE, rnd.randint(0, 2), rnd.choice((0, 3, 0x83))
        else:
            c, a1, a2 = rnd.choice((C_PINE, C_BIRCH, C_BUSH, C_ORE, C_ROCK, C_WEEDS, C_STUMP, C_WILDFLOWER)), rnd.randint(0, 6), 0
        p.poke('cell', i, c); p.poke('a1', i, a1); p.poke('a2', i, a2)
    # stamp buildings (skipping the corridors)
    for by in range(1, 30, 5):
        for bx in range(1, 30, 5):
            if slot >= 20:
                break
            t = types[slot % len(types)]
            w, h = bw[t], bh[t]
            cells = [(bx + i, by + j) for j in range(h) for i in range(w)]
            if any(x == col or y == row or x > 31 or y > 31 for x, y in cells):
                continue
            base = OFF['bld'] + slot * 4
            for k, v in enumerate((t, bx, by, 0)):
                p.pb.memory[p.G + base + k] = v
            for part, (x, y) in enumerate(cells):
                p.poke('cell', y * 32 + x, C_STRUCT); p.poke('a1', y * 32 + x, slot); p.poke('a2', y * 32 + x, part)
            slot += 1


def s_farm(p):
    print('farm (new game farm, run/walk corridors through the camera range)')
    corridors(p, 12, 10)
    return scroll_tour(p, 'farm')


def s_dense(p):
    print('dense (every plot owned, water/buildings/decor/crops everywhere, evening lamps)')
    make_dense(p, 12, 10)
    p.poke('time', 0, 1150 & 255); p.poke('time', 1, 1150 >> 8)
    return scroll_tour(p, 'dense')


def go_town(p, row, col):
    # corridor down column col to the south edge then over to the road (x=15)
    x, y = p.pos()
    for yy in range(32):
        p.poke('cell', yy * 32 + 15, C_ROAD)
    for xx in range(32):
        p.poke('cell', 31 * 32 + xx, C_ROAD)
    run_to(p, 'down', 31 - y)
    x, y = p.pos()
    run_to(p, 'right' if x < 15 else 'left', abs(15 - x))
    p.press('down', 24, 4)


def bfs_town(p, tx, ty, tk):
    for _ in range(80):
        x, y = p.pos()
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
                if n in prev or not (0 <= n[0] < 32 and 0 <= n[1] < 32) or not (tk[n[1] * 32 + n[0]] & 1):
                    continue
                prev[n] = (c, d)
                q.append(n)
        if (tx, ty) not in prev:
            return False
        path = []
        c = (tx, ty)
        while prev[c]:
            path.append(prev[c][1]); c = prev[c][0]
        path.reverse()
        # run along runs of the same direction
        d = path[0]
        n = 1
        while n < len(path) and path[n] == d:
            n += 1
        run_to(p, d, n)
    return False


def s_town(p):
    print('town (run the streets)')
    corridors(p, 12, 10)
    scene = p.record(lambda: (go_town(p, 12, 10), p.tick(80)))
    tk = town_kind()
    walk = [i for i in range(1024) if tk[i] & 1]
    xs = sorted(set(i % 32 for i in walk)); ys = sorted(set(i // 32 for i in walk))
    targets = []
    for (tx, ty) in ((xs[-1], None), (xs[0], None), (None, ys[-1]), (None, ys[0]), (xs[-1], ys[-1]), (xs[0], ys[0])):
        cands = [i for i in walk if (tx is None or i % 32 == tx) and (ty is None or i // 32 == ty)]
        if cands:
            i = cands[len(cands) // 2]; targets.append((i % 32, i // 32))
    rec = []
    for t in targets:
        rec += p.record(lambda: bfs_town(p, t[0], t[1], tk))
    return dict(town=summarize('town run', rec, p.frame(), {p.FIELD}))


def timed(p, fn, settle_caller=None):
    """cycles from now until the first wait_vbl_done not called from the field loop (= UI is up)"""
    c0 = p.pb._cycles()
    t = {}

    def on(_):
        pass
    fn()
    return c0


def s_menus(p):
    print('menus (START: press -> menu box up and polling; B: back to the field loop)')
    corridors(p, 12, 10)
    out = {}
    for label, prep in (('farm', None), ('dense', lambda: make_dense(p, 12, 10))):
        if prep:
            prep()
            run_to(p, 'right', 6); run_to(p, 'left', 6)
        p.tick(10)
        c0 = p.pb._cycles()
        p.stamps = []
        rec = p.record(lambda: (p.press('start', 2, 60)))
        t = p.since('start_menu', 'ui_menu')
        busy = max(b for b, c in rec) / p.frame()
        print(f'  {label:10s} start menu open: {t:5.2f} frames to the menu loop (longest busy span {busy:4.2f})')
        out[label] = t
        rec = p.record(lambda: p.press('b', 2, 40))
        print(f'  {label:10s} close: longest busy span {max(b for b, c in rec) / p.frame():5.2f} frames')
    return out


def s_scenes(p):
    print('scenes (warp step -> field loop running in the new scene; includes the 2x8-step fades)')
    corridors(p, 12, 10)
    make_dense(p, 12, 10)
    out = {}
    p.stamps = []
    rec = p.record(lambda: (go_town(p, 12, 10), p.tick(60)))
    t = p.since('enter_scene', 'FIELD')
    vr = p.since('view_reset', 'fade_in')
    busy = max(b for b, c in rec) / p.frame()
    print(f'  {"farm->town":20s} {t:6.2f} frames total; view_reset..fade_in {vr:5.2f}; longest busy {busy:5.2f}')
    out['to_town'] = t
    p.stamps = []
    rec = p.record(lambda: (p.press('up', 24, 4), p.tick(80)))
    t = p.since('enter_scene', 'FIELD')
    vr = p.since('view_reset', 'fade_in')
    busy = max(b for b, c in rec) / p.frame()
    print(f'  {"town->farm (dense)":20s} {t:6.2f} frames total; view_reset..fade_in {vr:5.2f}; longest busy {busy:5.2f}  scene={p.u8("scene"):#x}')
    out['to_farm'] = t
    return out


def s_night(p):
    print('night (pass out at 2am on a dense farm, staff + shops: fade out -> overnight -> morning report)')
    corridors(p, 12, 10)
    make_dense(p, 12, 10)
    for k in range(5):
        p.poke('staff', k, k + 1)
    p.poke('biz_owned', 0, 0x0F)
    p.poke('gold', 0, 0xFF); p.poke('gold', 1, 0xFF); p.poke('gold', 2, 0x0F)
    p.poke('time', 0, 1560 & 255); p.poke('time', 1, 1560 >> 8)
    p.stamps = []
    rec = []
    p.rec = rec
    for _ in range(60):
        p.press('a', 2, 10)
        if p.u8('time') == 360 & 255 and p.u8('time', 1) == 360 >> 8:
            break
    p.tick(240)
    p.rec = None
    F = p.frame()
    tot = p.since('fade_out', 'morning_report')
    parts = []
    for a, b in (('fade_out', 'world_overnight'), ('world_overnight', 'staff_overnight'), ('staff_overnight', 'biz_overnight'),
                 ('goals_check', 'save_write'), ('save_write', 'enter_scene'), ('enter_scene', 'view_reset'),
                 ('view_reset', 'fade_in'), ('fade_in', 'morning_report')):
        v = p.since(a, b)
        if v is not None:
            parts.append(f'{a}->{b} {v:.1f}')
    print(f'  {"sleep transition":20s} {tot:6.2f} frames ({tot / 59.7:4.2f} s)  day={p.u8("day")}')
    print('    ' + ', '.join(parts))
    return dict(overnight=tot)


SCEN = dict(farm=s_farm, dense=s_dense, town=s_town, menus=s_menus, scenes=s_scenes, night=s_night)

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('scen', nargs='*')
    ap.add_argument('--rom', default=os.path.join(ROOT, 'build/stilwell_farms.gbc'))
    a = ap.parse_args()
    for s in a.scen or list(SCEN):
        p = Prof(a.rom)
        p.start()
        if s == 'farm':
            print(f'frame = {p.frame()} cycles, field-loop caller = {p.FIELD:#06x}')
        SCEN[s](p)
        p.pb.stop(save=False)
