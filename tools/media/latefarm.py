#!/usr/bin/env python3
"""Play a farm from the first morning to the late game, for the README's maturity pictures and banner.

This is a long fast-forward of real play: every plot is bought on the map page, every rock, weed and stump is
cleared with the tools, buildings go up through build mode, paths and fences are painted by walking with A
held, crops are tilled, planted and grown by sleeping through the nights, animals are bought at the store and
help is hired at town hall.  What is fast-forwarded: gold, wood and stone are topped up (no grinding), the
tools are gold tier, and energy is refilled between swings.  The days themselves pass by going to bed.

    .venv/bin/python tools/media/latefarm.py [--from CHAPTER]

Each chapter saves an emulator state in build/media/late/; stage pictures land there too (world.render).
"""
import os, sys, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rec import Rec, base, WORK
import test_suite as T
from test_suite import *
import world

LATE = os.path.join(WORK, 'late')
OBST = {C_WEEDS: T_HAND, C_FORAGE: T_HAND, C_ROCK: T_HAMMER, C_BOULDER: T_HAMMER, C_ORE: T_HAMMER,
        C_STUMP: T_AXE, C_LOG: T_AXE, C_PINE: T_AXE, C_BIRCH: T_AXE, C_SAPLING_WILD: T_AXE}
BCAT = [B_BARN, B_COOP, B_GREENHOUSE, B_SILO, B_SHED, B_STABLE, B_WINDMILL, B_STAND, B_GAZEBO]


def topup(g):
    g.rich()
    g.w8('energy', g.u8('energy_max'))


def night_check(g):
    """past 9pm: go to bed (a real night)"""
    if g.u16('time') >= 1260:
        if g.sym8('_building'):
            g.press('b'); g.tick(4)
        g.sleep()
        topup(g)


def buy_plot(g, p):
    """START > FARM > MAP, move the bracket to plot p, A, Yes"""
    open_start(g, 4)
    g.choose(8, 2, FARM_N, 1)
    g.tick(40)
    px, py = g.real_pos()
    cx, cy = px >> 3, py >> 3
    tx, ty = p & 7, p >> 3
    for _ in range(abs(tx - cx)):
        g.press('right' if tx > cx else 'left'); g.tick(2)
    for _ in range(abs(ty - cy)):
        g.press('down' if ty > cy else 'up'); g.tick(2)
    g.tick(10)
    g.press('a')
    g.yesno(True)
    g.tick(20)
    g.dismiss()
    g.wait_field()
    if not g.plot_owned(p):
        raise Fail(f'plot {p} not bought')


def adj(g, x, y):
    """a walkable cell next to (x, y) and the direction to face from it"""
    px, py = g.pos()
    best = None
    for d, (dx, dy, _) in DIRS.items():
        sx, sy = x - dx, y - dy
        if g.walkable(sx, sy):
            k = abs(sx - px) + abs(sy - py)
            if best is None or k < best[0]:
                best = (k, sx, sy, d)
    return best


def clear_rect(g, x0, y0, x1, y1, keep=()):
    """clear every obstacle in the rect with the right tool (home-valley coordinates, inclusive)"""
    todo = [(x, y) for y in range(y0, y1 + 1) for x in (range(x0, x1 + 1) if (y - y0) % 2 == 0 else range(x1, x0 - 1, -1))
            if g.cell(x, y) in OBST and (x, y) not in keep]
    left = list(todo)
    stuck = 0
    while left:
        # nearest reachable first
        px, py = g.pos()
        left.sort(key=lambda c: abs(c[0] - px) + abs(c[1] - py))
        done_one = False
        for (x, y) in left[:12]:
            if g.cell(x, y) not in OBST:
                left.remove((x, y)); done_one = True
                break
            a = adj(g, x, y)
            if not a:
                continue
            try:
                g.go_face(a[1], a[2], a[3])
            except Fail:
                continue
            for _ in range(8):
                c = g.cell(x, y)
                if c not in OBST:
                    break
                g.w8('tool', OBST[c])
                g.act(); g.tick(8)
                if g.ui_open():
                    g.dismiss()
                g.w8('energy', g.u8('energy_max'))
            left.remove((x, y))
            done_one = True
            night_check(g)
            break
        if not done_one:
            stuck += 1
            if stuck > 3:
                print('  unreachable:', left[:20], '...' if len(left) > 20 else '')
                break
            g.tick(60)
    return len(todo)


def place_bldg(g, t, x, y):
    """build type t with the player at (x, y) facing up (footprint above, centred)"""
    build_poke(g, 4, BCAT.index(t))
    g.go_face(x, y, 'up'); g.shoo_pet(); g.park_pet()
    g.act()
    g.yesno(True); g.tick(20); g.dismiss()
    g.press('b'); g.tick(4)


def place(g, cat, sel, cells, face='down'):
    """tap-place catalogue item at each cell, standing next to it"""
    build_poke(g, cat, sel)
    for (x, y) in cells:
        a = None
        dx, dy, _ = DIRS[face]
        if g.walkable(x - dx, y - dy):
            a = (0, x - dx, y - dy, face)
        else:
            a = adj(g, x, y)
        if not a:
            print('  no spot for', cat, sel, (x, y)); continue
        try:
            g.go_face(a[1], a[2], a[3]); g.park_pet()
        except Fail as e:
            print('  skip', (x, y), e); continue
        g.act(); g.tick(6)
        if g.ui_open():
            g.dismiss()
    g.press('b'); g.tick(4)


def paint(g, cat, sel, x, y, d, n):
    """stand on (x, y) and paint n cells walking d with A held (the stroke starts under you)"""
    build_poke(g, cat, sel)
    dx, dy, _ = DIRS[d]
    g.go(x - dx, y - dy) if False else None
    g.go_face(x, y, d)
    paint_walk(g, d, n)
    g.press('b'); g.tick(4)


def fence_ring(g, x0, y0, x1, y1, sel=4, gate=None):
    """a fence ring painted in strokes along the outside of a rectangle (gate = a cell left open)"""
    build_poke(g, 2, sel)
    cells = [(x, y0) for x in range(x0, x1 + 1)] + [(x1, y) for y in range(y0 + 1, y1 + 1)] + \
            [(x, y1) for x in range(x1 - 1, x0 - 1, -1)] + [(x0, y) for y in range(y1 - 1, y0, -1)]
    cells = [c for c in cells if c != gate]
    g.press('b'); g.tick(4)
    place_cells(g, 2, sel, cells)


def place_cells(g, cat, sel, cells):
    build_poke(g, cat, sel)
    for (x, y) in cells:
        if g.cell(x, y) == C_DECOR and g.a1(x, y) == CAT_DK(cat, sel):
            continue
        a = adj(g, x, y)
        if not a:
            print('  no spot for', (x, y)); continue
        try:
            g.go_face(a[1], a[2], a[3]); g.park_pet()
        except Fail as e:
            print('  skip', (x, y), e); continue
        g.act(); g.tick(6)
        if g.ui_open():
            g.dismiss()
        night_check(g)
        if not g.sym8('_building'):
            build_poke(g, cat, sel)
    g.press('b'); g.tick(4)


_PATHS = [DK_PATH, DK_BRICK, DK_GRAVEL, DK_PLANK, DK_FENCE, DK_PICKET, DK_STONEWALL, DK_MODERN]


def CAT_DK(cat, sel):
    return _PATHS[sel] if cat == 2 else None


def till_plant(g, x0, y0, x1, y1, crop, skip=()):
    """hoe, then seed every cell of the rect (rows, walking along the row above each)"""
    g.set_seeds(crop, 99)
    g.w8('tier', 0, TT_HOE)                  # a basic hoe tills one cell: neat field edges
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if (x, y) in skip:
                continue
            if g.cell(x, y) not in (C_GRASS, C_GRASS2, C_WILDFLOWER, C_SOIL, C_DIRT):
                continue
            a = adj(g, x, y)
            if not a:
                continue
            g.go_face(a[1], a[2], a[3])
            if g.cell(x, y) != C_SOIL:
                g.w8('tool', T_HOE); g.act(); g.tick(8)
                if g.ui_open(): g.dismiss()
            if g.cell(x, y) == C_SOIL and not g.a1(x, y) & 0x1F:
                g.w8('tool', T_SEEDS); g.w8('seed_sel', crop); g.act(); g.tick(8)
                if g.ui_open(): g.dismiss()
            g.w8('energy', g.u8('energy_max'))
            night_check(g)


def water_all(g, cells):
    for (x, y) in cells:
        if g.cell(x, y) != C_SOIL or g.a1(x, y) & SOIL_WET:
            continue
        a = adj(g, x, y)
        if not a:
            continue
        g.go_face(a[1], a[2], a[3])
        g.w8('tool', T_CAN); g.w8('water', 99); g.act(); g.tick(8)
        if g.ui_open(): g.dismiss()
        g.w8('energy', g.u8('energy_max'))


def store_menu_n(g):
    """Rosa's list grows with a stable (Horse) and goods to sell: count its rows from the box's left border"""
    for _ in range(40):
        if g.menu_live(0, 0, 10, frames=20) is not None:      # (rows 1.. either way; the cursor starts on row 1)
            side = g.wtile(0, 1)
            n = next(y for y in range(2, 18) if g.wtile(0, y) != side) - 1
            return n
        if g.ui_open():
            g.press('a')
    raise Fail('store menu never came up')


def store_buy(g, item, rar=R_COMMON, count=1):
    """in town: Rosa's store, buy animals (row item: 3 cows, 4 hens, 5 sheep) of a rarity, default names"""
    for _ in range(count):
        g.go_face(11, 4, 'up'); g.act()
        n = store_menu_n(g)
        g.choose(0, 0, n, item)
        g.choose(0, 0, 3, rar)
        for _ in range(60):
            if g.name_entry_open():
                break
            g.tick(5)
        if g.name_entry_open():
            g.press('start'); g.tick(20)
        g.dismiss()
        g.wait_field()


def repair(g, t):
    k = g.bld_find(t)
    dx, dy = door_cell(g, k)
    g.go_face(dx, dy + 1, 'up'); g.act()
    g.yesno(True); g.tick(30); g.dismiss()
    return not g.bld(k)[3] & 1


def sleep_days(g, n, water=()):
    for _ in range(n):
        if water:
            water_all(g, water)
        g.sleep()
        topup(g)


def soil_cells(g, x0, y0, x1, y1):
    return [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1) if g.cell(x, y) == C_SOIL]


# ------------------------------------------------------------------ chapters
def ch_start(g):
    """day 1, the first morning"""
    base(g, 'farm')
    g.w8('tips', 0xFF)


def ch_spring(g):
    """the first days: fix the coop and barn, clear the home field, a spring field watered by hand, hens and a cow"""
    g.outdir = LATE
    topup(g)
    for t in range(4):
        g.w8('tier', 3, t)
    g.w8('tier', 3, TT_ROD)
    print('  repaired', repair(g, B_COOP), repair(g, B_BARN))
    clear_rect(g, 8, 8, 23, 23)
    till_plant(g, 9, 14, 12, 17, 1)          # radishes
    till_plant(g, 13, 14, 14, 17, 2)         # carrots
    till_plant(g, 9, 19, 14, 20, 3)          # strawberries
    field = soil_cells(g, 8, 13, 15, 21)
    water_all(g, field)
    g.w16('time', 600)
    to_town(g)
    store_buy(g, 4, R_COMMON, 2)
    store_buy(g, 3, R_COMMON, 1)
    to_farm(g)
    g.w16('hay', 40)
    sleep_days(g, 4, field)
    water_all(g, field)
    print('  day', g.u8('day'), 'season', g.u8('season'))


def ch_land(g):
    """buy the rest of the home valley on the map page, then clear it"""
    topup(g)
    for p in [p for p in range(64) if 2 <= (p & 7) <= 5 and 2 <= (p >> 3) <= 5 and not g.plot_owned(p)]:
        buy_plot(g, p)
        topup(g)
    n = clear_rect(g, 8, 0, 26, 7)
    n += clear_rect(g, 24, 8, 26, 23)
    n += clear_rect(g, 8, 24, 26, 31)
    n += clear_rect(g, 4, 9, 7, 23)
    n += clear_rect(g, 5, 0, 7, 8)
    print('  plots', g.plots_n(), 'cleared', n, 'day', g.u8('day'))


def ch_build(g):
    """buildings, orchard, paths, fences and a garden; the Big House; more animals and help"""
    topup(g)
    clear_rect(g, 4, 0, 26, 31)              # weeds came back overnight (and the woods' edge for the pasture)
    place_bldg(g, B_SILO, 23, 11)
    place_bldg(g, B_WINDMILL, 21, 7)
    place_bldg(g, B_SHED, 23, 15)
    place_bldg(g, B_STAND, 16, 15)
    place_bldg(g, B_STABLE, 21, 24)
    place_bldg(g, B_GAZEBO, 6, 12)
    print('  buildings', [g.bld(k)[0] for k in range(MAX_BLD) if g.bld(k)[0] != 0xFF])
    topup(g)
    # orchard along the river and in the north-east corner
    trees = [(25, y) for y in range(9, 23, 2)] + [(x, y) for x in (23, 25) for y in (4, 6)]
    for i, (x, y) in enumerate(trees):
        place(g, 0, 11 + (i % 4), [(x, y)])
    # paths
    place_cells(g, 2, 1, [(x, 12) for x in range(5, 17)] + [(x, 12) for x in range(19, 24)])
    place_cells(g, 2, 1, [(15, y) for y in range(4, 12)])
    place_cells(g, 2, 2, [(x, 24) for x in range(9, 17)] + [(x, 24) for x in range(19, 27)])
    # fences: the pasture, the south fields
    place_ring(g, 2, 4, 6, 1, 14, 7, gate=(14, 4))
    place_ring(g, 2, 5, 8, 25, 15, 31, gate=(15, 28))
    place_ring(g, 2, 5, 19, 25, 26, 31, gate=(19, 28))
    # garden by the gazebo, flowers by the house, lamps down the road
    place(g, 1, 0, [(4, 10), (4, 11), (7, 10), (7, 11)])
    place(g, 1, 1, [(8, 9), (8, 10)])
    place(g, 1, 3, [(8, 11), (12, 9)])
    place(g, 1, 10, [(16, y) for y in (17, 21, 26, 30)] + [(19, y) for y in (17, 21)] + [(4, 12), (8, 13)])
    place(g, 1, 11, [(5, 13)])
    place(g, 0, 6, [(8, 2), (9, 2), (10, 2)])                          # beehives in the pasture corner
    place(g, 0, 3, [(16, 19)])                                            # scarecrow
    topup(g)
    # the Big House and more animals, help from town hall
    g.w16('time', 600)
    to_town(g)
    g.go_face(27, 4, 'up'); g.act()
    g.choose(0, 0, 5, 0); g.yesno(True); g.tick(30); g.dismiss()
    g.go_face(27, 4, 'up'); g.act()
    g.choose(0, 0, 5, 0); g.yesno(True); g.tick(30); g.dismiss()
    print('  house level', (g.bld(0)[3] >> 1) & 3)
    store_buy(g, 3, R_RARE, 1)
    store_buy(g, 3, R_LEGEND, 1)
    store_buy(g, 5, R_COMMON, 2)
    store_buy(g, 4, R_LEGEND, 1)
    store_buy(g, 6, 1, 1)                    # a chestnut horse for the stable
    print('  horse', g.u8('horse'))
    for role in (ROLE_WATER, ROLE_RANCH, ROLE_GROUNDS):
        hall_hire(g, role)
    to_farm(g)
    print('  animals', len(g.animals()), 'staff', [g.u8('staff', k) for k in range(5)])


def place_ring(g, cat, sel, x0, y0, x1, y1, gate=None):
    """a fence ring with a gate, each piece placed from inside where it can be"""
    build_poke(g, cat, sel)
    for (x, y) in ring(x0, y0, x1, y1, gate):
        if g.cell(x, y) == C_DECOR:
            continue
        opts = []
        if y == y0: opts.append((x, y + 1, 'up'))          # from inside (the gate keeps the inside open)
        if y == y1: opts.append((x, y - 1, 'down'))
        if x == x0: opts.append((x + 1, y, 'left'))
        if x == x1: opts.append((x - 1, y, 'right'))
        if (x, y) in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):   # corners: from outside too
            opts += [(x, y - 1, 'down') if y == y0 else (x, y + 1, 'up'), (x - 1, y, 'right') if x == x0 else (x + 1, y, 'left')]
        for (sx, sy, d) in opts:
            if not g.walkable(sx, sy):
                continue
            try:
                g.go_face(sx, sy, d); g.park_pet()
            except Fail:
                continue
            g.act(); g.tick(6)
            if g.ui_open():
                g.dismiss()
            break
        else:
            print('  ring: no spot for', (x, y))
        night_check(g)
        if not g.sym8('_building'):
            build_poke(g, cat, sel)
    g.press('b'); g.tick(4)


def ring(x0, y0, x1, y1, gate=None):
    cells = [(x, y0) for x in range(x0, x1 + 1)] + [(x1, y) for y in range(y0 + 1, y1 + 1)] + \
            [(x, y1) for x in range(x1 - 1, x0 - 1, -1)] + [(x0, y) for y in range(y1 - 1, y0, -1)]
    return [c for c in cells if c != gate]


def ch_summer(g):
    """sleep to summer; summer fields with sprinklers"""
    while g.u8('season') == 0:
        g.sleep()
        topup(g)
    print('  summer day', g.u8('day'))
    spr = [(10, 15), (13, 15), (10, 18), (13, 18), (10, 21), (13, 21)]
    clear_spent(g, 8, 13, 15, 22)
    to_grass(g, spr)
    place(g, 0, 5, spr)
    for k, (x0, y0) in enumerate(((9, 14), (9, 17), (9, 20))):
        till_plant(g, x0, y0, x0 + 5, y0 + 2, (5, 6, 9)[k], skip=spr)
    spr2 = [(10, 27), (13, 27), (10, 29), (13, 29), (21, 27), (24, 27), (21, 29), (24, 29)]
    place(g, 0, 5, spr2)
    till_plant(g, 9, 26, 14, 27, 7, skip=spr2)
    till_plant(g, 9, 28, 14, 30, 8, skip=spr2)
    till_plant(g, 20, 26, 25, 27, 5, skip=spr2)
    till_plant(g, 20, 28, 25, 30, 9, skip=spr2)
    sleep_days(g, 8)
    print('  day', g.u8('day'), 'season', g.u8('season'))


def ch_fall(g):
    """sleep on to fall: pumpkins, cranberries and sunflowers' last days; the late game"""
    while g.u8('season') == 1:
        g.sleep()
        topup(g)
    print('  fall day', g.u8('day'))
    for (x0, y0, x1, y1, crop) in ((9, 14, 14, 16, 10), (9, 17, 14, 19, 13), (9, 20, 14, 22, 12),
                                   (9, 26, 14, 27, 10), (9, 28, 14, 30, 11), (20, 26, 25, 27, 13), (20, 28, 25, 30, 10)):
        clear_spent(g, x0, y0, x1, y1)
        till_plant(g, x0, y0, x1, y1, crop)
    sleep_days(g, 10)
    print('  day', g.u8('day'), 'season', g.u8('season'))


def to_grass(g, cells):
    for (x, y) in cells:
        if g.cell(x, y) == C_SOIL:
            a = adj(g, x, y)
            g.go_face(a[1], a[2], a[3])
            g.w8('tool', T_HAMMER); g.act(); g.tick(8)
            if g.ui_open(): g.dismiss()


def clear_spent(g, x0, y0, x1, y1):
    """harvest anything ripe and hoe out withered crops"""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if g.cell(x, y) == C_SOIL and g.a1(x, y) & 0x1F:
                a = adj(g, x, y)
                if not a:
                    continue
                g.go_face(a[1], a[2], a[3])
                g.w8('tool', T_HAND); g.act(); g.tick(8)
                if g.ui_open(): g.dismiss()
                if g.a1(x, y) & 0x1F:
                    g.w8('tool', T_HAMMER); g.act(); g.tick(8)
                    if g.ui_open(): g.dismiss()
                g.w8('energy', g.u8('energy_max'))
                night_check(g)



def stage(g, time=700, night=False):
    """the whole valley on a clear day (or night) at this point of the game; the game itself is left as it was"""
    snap = g.snapshot()
    g.w8('weather', 0)
    g.w16('time', 1290 if night else time)
    g.tick(720)                          # the next clock tick re-applies the palettes
    im = world.render(g)
    g.restore(snap)
    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--from', dest='start', default=None)
    ap.add_argument('--to', default=None)
    a = ap.parse_args()
    os.makedirs(LATE, exist_ok=True)
    chapters = [(n[3:], f) for n, f in globals().items() if n.startswith('ch_')]
    g = Rec(); g.outdir = LATE
    names = [n for n, _ in chapters]
    i0 = names.index(a.start) if a.start else 0
    if i0 > 0:
        prev = os.path.join(LATE, names[i0 - 1] + '.state')
        g.restore(open(prev, 'rb').read())
    for n, f in chapters[i0:]:
        print('chapter', n, flush=True)
        f(g)
        open(os.path.join(LATE, n + '.state'), 'wb').write(g.snapshot())
        if g.u8('scene') == SCENE_FARM:
            stage(g).save(os.path.join(LATE, n + '.png'))
        if a.to == n:
            break
    g.stop()


if __name__ == '__main__':
    main()
