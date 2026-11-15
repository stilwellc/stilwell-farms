"""README GIFs for the v0.3 farm-side features: design tools, barn + coop interiors, the horse, the mine,
the beach and cooking.  Run through shots.py:  .venv/bin/python tools/media/shots.py design barns ..."""
from shots import *
import test_suite as T
import rec as R


def brisk(dialog=900, menu=250, pick=160):
    """shorter reading pauses for shots that go through many menus"""
    R.DIALOG_MS, R.MENU_MS, R.PICK_MS = dialog, menu, pick


def quick(g, idx):
    """START while building: the BUILD quick menu (UNDO, ITEMS, ERASER, COLOURS, MAIN MENU, DONE)"""
    g.press('start')
    g.choose(8, 0, 6, idx)
    g.tick(10)


def read(g, pages=1, btn='b', rest=True):
    """hold the first `pages` dialog pages for reading, skip the rest (rest=False: leave them be)"""
    for _ in range(pages):
        if not g.ui_open():
            return
        g._typed()
        g.hold(R.DIALOG_MS)
        g.press(btn)
        g.tick(4)
    if rest:
        fast(g, lambda: g.dismiss(btn=btn))


def pick(g, cat, item, menu='start', pages=0):
    """START > BUILD (or the quick menu > ITEMS) > category > item"""
    if menu == 'start':
        open_start(g, 3)
    else:
        quick(g, 1)
    g.choose(0, 0, 8, cat)
    n = CAT_N[cat]
    if n > 11:
        g.choose(0, 4, 11, min(item, 10))
        if item >= 10:
            g.choose(0, 4, n - 10, item - 10)
    else:
        g.choose(0, 4, n, item)
    g.tick(10)
    read(g, pages, 'a')


def quick_items(g, cat, item):
    """quick menu > ITEMS > category > item, then read the how-to line"""
    quick(g, 1)
    g.choose(0, 0, 8, cat)
    n = CAT_N[cat]
    if n > 11:
        g.choose(0, 4, 11, min(item, 10))
        if item >= 10:
            g.choose(0, 4, n - 10, item - 10)
    else:
        g.choose(0, 4, n, item)
    g.tick(10)
    fast(g, lambda: g.dismiss(btn='a'))


def fast(g, fn, k=1):
    """run fn without the reading pauses (and k times faster)"""
    p, e = g.paced, g.every
    g.paced, g.every = False, e * k
    r = fn()
    g.paced, g.every = p, e
    return r


def cut(g, fn):
    """do fn off camera (a jump cut)"""
    r, g.rec = g.rec, False
    fn()
    g.rec = r


def walk(g, x, y, d=None, k=1):
    """a walk between set pieces: hold B to run (outside build mode)"""
    run = not g.sym8('_building')
    k = k if run else 2
    if run:
        g.pb.button_press('b')
    fast(g, lambda: g.go_face(x, y, d) if d else g.go(x, y), k)
    if run:
        g.pb.button_release('b')
        g.tick(2)


@shot
def shot_design(g):
    """roof colours live; paint a brick path and a fence row by holding A; SELECT eraser; quick-menu UNDO;
    copy the patch to a blueprint and stamp it with its ghost.  Setup: cleared base, gold/wood/stone, tips seen."""
    base(g, 'cleared')
    quiet(g)
    brisk()
    g.rich()
    g.w16('time', 540)
    g.go(14, 8)
    g.face('down')
    g.rec = True
    g.pause(400)
    # 1. colours: START > BUILD > COLOURS, roofs to terracotta, wood darker
    open_start(g, 3)
    g.choose(0, 0, 8, 7)
    g.tick(20)
    for b in ('right', 'right', 'down', 'right'):
        g.press(b, 4, 10)
        g.hold(330)
    g.hold(500)
    fast(g, lambda: (g.press('a'), g.tick(10), g.dismiss(), g.wait_field()))
    g.pause(500)
    # 2. a brick path, painted by holding A and walking
    cut(g, lambda: g.go_face(9, 16, 'right'))
    pick(g, 2, 1, pages=2)
    T.paint_walk(g, 'right', 5)
    g.pause(300)
    # 3. a fence row the same way, walking back (picked from the quick menu)
    walk(g, 14, 19, 'left')
    pick(g, 2, 4, 'quick')
    T.paint_walk(g, 'left', 5)
    g.pause(400)
    # 4. SELECT: the eraser, walked back into the fence row
    g.face('right')
    g.press('select'); g.tick(8)
    g.hold(500)
    T.paint_walk(g, 'right', 3)
    g.pause(300)
    g.press('select'); g.tick(6)
    walk(g, g.pos()[0], 18, 'down')
    g.pause(300)
    # 5. START > UNDO puts the whole stroke back
    quick(g, 0)
    g.pause(900)
    # 6. copy the patch to a blueprint...
    walk(g, 8, 16, 'right')
    pick(g, 6, 0, 'quick')
    g.act(); g.tick(6)
    walk(g, 15, 19, 'left')
    g.pause(300)
    g.act()
    g.wait_menu(0, 0, CONST['BP_SLOTS'])
    g.choose(0, 0, CONST['BP_SLOTS'], 0)
    g.tick(20); read(g, 1)
    # ...and stamp it in the east field, ghost first
    cut(g, lambda: g.go_face(20, 12, 'down'))
    g.park_pet()
    g.pause(900)
    g.act()
    g.wait_menu(14, YN_Y, 2)
    g.yesno(True); g.tick(20); read(g, 1)
    g.press('b'); g.tick(6)
    walk(g, 20, 14, 'down')
    g.pause(1400)
    return out(g, 'design', collapse_ms=1500)


@shot
def shot_barns(g):
    """into the barn on a rainy morning: milk the cow, shear the sheep, fill the mangers; then the coop:
    an egg from the nest box, hay in the trough.  Setup: barn + coop repaired, animals and hay given, rain."""
    base(g, 'cleared')
    quiet(g)
    brisk(dialog=1100, menu=300, pick=200)
    barn = T.fix_up(g, B_BARN)
    coop = T.fix_up(g, B_COOP)
    g.set_anim(0, A_COW, barn, happy=230, flags=AF_READY, name='Daisy')
    g.set_anim(1, A_SHEEP, barn, happy=200, flags=AF_READY | (R_RARE << 6), name='Wooly')
    g.set_anim(2, A_COW, barn, happy=200, flags=AF_READY | (R_RARE << 6), name='Mocha')
    g.set_anim(3, A_HEN, coop, happy=200, flags=AF_READY, name='Penny')
    g.set_anim(4, A_HEN, coop, happy=210, flags=AF_READY | (R_LEGEND << 6), name='Sunny')
    g.w16('hay', 20)
    g.w8('weather', 1)
    g.w16('time', 450)
    T.to_town(g); T.to_farm(g)
    dx, dy = T.door_cell(g, barn)
    g.go_face(dx, dy + 1, 'up')
    g.park_pet()
    g.rec = True
    g.pause(500)
    g.hold('up', 12)
    fast(g, lambda: g.wait_scene((MAP_BARN,)), 4)
    g.pause(300)
    walk(g, 1, 4, 'up'); g.act(); g.tick(30); read(g, 2)      # milk Daisy (her panel)
    g.go_face(2, 4, 'up'); g.act(); g.tick(30); read(g, 1)      # shear Wooly
    walk(g, 8, 4, 'up'); g.act(); g.tick(20); read(g, 1)      # hay into the mangers
    walk(g, 4, 5, 'up')
    g.pause(900)
    cdx, cdy = T.door_cell(g, coop)
    cut(g, lambda: (T.walk_out(g), g.go_face(cdx, cdy + 2, 'up')))
    g.pause(300)
    walk(g, cdx, cdy + 1, 'up')
    g.hold('up', 12)
    fast(g, lambda: g.wait_scene((MAP_COOP,)), 4)
    g.pause(300)
    for x in (2,):                                              # the egg in nest box 2
        free = [c for c in ((x, 3),) if not g.actor_at(*c)]
        if free:
            walk(g, x, 3, 'up'); g.act(); g.tick(20); read(g, 1)
        else:                                                   # a hen sits there: pet her instead
            walk(g, x, 4, 'up'); g.act(); g.tick(30); read(g, 1)
    walk(g, 6, 4, 'down'); g.act(); g.tick(20); read(g, 1)    # the trough
    walk(g, 3, 6, 'up')
    g.pause(1500)
    return out(g, 'barns', collapse_ms=1500)


def face_horse(g):
    """walk up to the horse (it wanders a little) and face it"""
    for _ in range(12):
        hs = [a for a in g.actors() if a[0] == K_HORSE]
        _, _, hx, hy = hs[0]
        for d, (ddx, ddy, _) in DIRS.items():
            if g.walkable(hx - ddx, hy - ddy):
                g.go(hx - ddx, hy - ddy); g.face(d)
                break
        x, y = g.pos()
        dx, dy, _ = DIRS[DIRNAME[g.u8('facing')]]
        h = g.actor_at(x + dx, y + dy)
        if h and h[0] == K_HORSE:
            return h[2], h[3]
        g.tick(20)
    raise Fail('cannot reach the horse')


@shot
def shot_horse(g):
    """buy a chestnut horse at the general store and name it; brush it by the stable, climb on, gallop the
    farm, climb down.  Setup: gold; the stable is built off camera (real build-mode placement)."""
    base(g, 'cleared')
    quiet(g)
    brisk(dialog=1100, menu=320, pick=200)
    g.rich()
    g.w16('time', 600)
    build_poke(g, 4, 5)                         # BUILDINGS > Stable, placed for real (off camera)
    g.go_face(20, 12, 'down'); g.act()
    g.yesno(True); g.tick(20); g.dismiss()
    g.press('b'); g.tick(4)
    g.w8('barn_tips', 0xFF)
    T.to_town(g)
    g.go_face(11, 4, 'up')
    g.rec = True
    g.pause(300)
    g.act()
    g.tick(20)
    read(g, 1, 'a', False)                      # Rosa's hello (first page)
    fast(g, lambda: g.wait_menu(0, 0, STORE_N + 1))
    g.choose(0, 0, STORE_N + 1, 6)              # Horse
    g.choose(0, 0, 3, 1)                        # Chestnut
    for _ in range(60):
        if g.name_entry_open():
            break
        g.tick(5)
    print('name entry', g.name_entry_open())
    g.tick(20)
    g.hold(1400)
    g.press('start'); g.tick(20)
    read(g, 2, 'a', False)                      # "Rusty will be waiting by your stable..."
    fast(g, lambda: (g.wait_menu(0, 0, STORE_N, advance='a'), g.dismiss()))
    g.rec = False
    T.to_farm(g)
    g.w8('barn_tips', 0xFF)
    g.go(19, 15)
    g.tick(30)
    g.rec = True
    g.pause(300)
    face_horse(g)
    g.act()
    g.choose(12, 3, 4, 1); g.tick(10)           # Brush
    read(g, 1)
    face_horse(g)
    g.act()
    g.choose(12, 3, 4, 0); g.tick(10)           # Ride
    read(g, 1)
    g.pause(300)
    for x, y in ((g.pos()[0], 16), (9, 16), (9, 22), (14, 22), (14, 16), (21, 16)):
        g.go(x, y)
    g.pause(300)
    g.press('b'); g.tick(10)                    # climb down
    g.go(21, 17); g.face('up')
    g.pause(1300)
    return out(g, 'horse', collapse_ms=1500)


@shot
def shot_mine(g):
    """west out of town into the mine: down the shaft, hammer rocks for stone and ore, find the ladder under a
    rock; later (floors 2-20 dug for real off camera) ride the lift from the entrance to floor 20 and break a
    gem node.  Setup: energy topped up between strikes (as the QA suite does); hammer tier 3 for the off-camera dig."""
    base(g, 'town')
    quiet(g)
    brisk(dialog=1000, menu=320, pick=200)
    c = T.Ctx(g, 'media')
    g.w16('time', 420)
    g.w8('energy', 200)
    g.go(3, 4)
    g.rec = True
    g.pause(200)
    T.to_mine(g)
    walk(g, 4, 3)
    g.step('down')                               # the shaft ladder to floor 1
    for _ in range(60):
        g.tick(10)
        if g.u8('scene') == MAP_MINE_B:
            break
    g.tick(30); read(g, 1); g.wait_field()
    g.pause(400)
    seed = g.sym8('_mn_seed')
    here = T.reach_from(g, g.pos())
    near = [r for r in g.rocks() if r != g.ladder() and any((r[0] - dx, r[1] - dy) in here for dx, dy, _ in DIRS.values())]
    near.sort(key=lambda r: abs(r[0] - g.pos()[0]) + abs(r[1] - g.pos()[1]))
    for r in near[:1]:
        T.break_rock(c, g, *r)
        g.pause(250)
    p = g.pos()
    ore = [r for r in g.rocks() if T.rock_kind(*r, seed, 1) in (2, 3, 5) and r != g.ladder()]
    ore.sort(key=lambda r: abs(r[0] - p[0]) + abs(r[1] - p[1]))
    if ore:
        fast(g, lambda: T.dig_to(c, g, *ore[0]), 2)
        g.pause(500)
    fast(g, lambda: T.take_ladder(c, g), 2)     # floor 2
    g.pause(900)
    first = g.take()
    g.rec = False
    g.w8('tier', 3, TT_HAMMER)
    while g.u8('mine_floor') < 20:               # the dig to the bottom, for real, off camera
        g.w16('time', 420)
        T.take_ladder(c, g) if g.ladder()[0] != 0xFF else None
    acts = [a for a in T.map_acts(g.u8('scene')) if a[2] == ACT_LADDERUP]
    ux, uy = acts[0][:2]
    g.go_face(ux, uy + 1, 'up'); g.act(); g.yesno(True)
    g.wait_scene((MAP_MINE,))
    acts = [a for a in T.map_acts(g.u8('scene')) if a[2] == ACT_LIFT]
    lx, ly = max(acts, key=lambda a: a[1])[:2]
    g.go(lx, ly + 2)
    g.rec = True
    g.pause(300)
    T.use_lift(c, g, 4, 5)                       # the lift: Entrance, 5, 10, 15, 20
    g.pause(500)
    seed = g.sym8('_mn_seed')
    p = g.pos()
    gems = sorted((r for r in g.rocks() if T.rock_kind(*r, seed, 20) == 6), key=lambda r: abs(r[0] - p[0]) + abs(r[1] - p[1]))
    if gems:
        fast(g, lambda: T.dig_to(c, g, *gems[0]), 2)
        g.tick(30)
        g.pause(1500)
    print('gems', [ITEM_NAME[i] for i in T.GEM_ITEMS if g.inv(i)])
    return out(g, 'mine', first + g.take(), collapse_ms=1500)


def fish(g, tag):
    """cast, wait for the bite, reel it in (the QA suite's fish_once, without its screenshots)"""
    def hook(name):
        if name.startswith('caught') and g.ui_open():
            g._typed()
            g.hold(2000)
    shot0, g.shot = g.shot, hook
    p, g.paced = g.paced, False
    try:
        return T.fish_once(T.Ctx(g, 'media'), g, tag)
    finally:
        g.shot, g.paced = shot0, p


@shot
def shot_beach(g):
    """east out of town to the beach: pick up what washed up today, then cast off the end of the pier for an
    ocean fish.  Setup: none beyond the town base (plus energy)."""
    base(g, 'town')
    quiet(g)
    brisk(dialog=1000, menu=320, pick=200)
    g.w16('time', 480)
    g.w8('energy', 200)
    g.go(28, 15)
    g.rec = True
    g.pause(200)
    T.beach_to(g)
    g.pause(300)
    stamp = g.u8('beach_stamp')
    finds = [(k, T.beach_item(k, stamp, 0)) for k in range(len(T.BEACH_SPOTS)) if T.beach_item(k, stamp, 0) is not None]
    for k, it in finds[:2]:
        x, y = T.BEACH_SPOTS[k]
        g.use_on(x, y)
        g.tick(20)
        read(g, 1)
    g.pause(300)
    caught = None
    for attempt in range(6):
        g.go_face(15, 15, 'down')
        tool(g, T_ROD)
        mark = len(g.cap)
        if fish(g, attempt):
            caught = attempt
            break
        g.cap = g.cap[:mark]                       # a fish that got away: cut the try
    g.pause(1200)
    print('caught', [ITEM_NAME[i] for i in range(I_SARDINE, I_SQUID + 1) if g.inv(i)], [ITEM_NAME[it] for _, it in finds[:3]])
    return out(g, 'beach', collapse_ms=1500)


@shot
def shot_cooking(g):
    """A at the new fridge: Grandpa's recipe cards, the recipe book, cook an omelet (chop, sizzle, steam);
    then eat an apple pie from the bag: energy back and the Brisk buff on the HUD.  Setup: the fridge is bought
    and placed off camera (real furniture menu); eggs + milk and one apple pie put in the bag; energy lowered."""
    base(g, 'farm')
    quiet(g)
    brisk(dialog=1100, menu=420, pick=260)
    g.enter_house()
    g.w32('gold', 5000)
    T.furn_build(g, 0, FK_FRIDGE)
    g.go_face(6, 5, 'up')
    g.act(); g.tick(10)
    g.press('b'); g.tick(6)
    g.w8('inv', 2, I_EGG * 3); g.w8('inv', 1, I_MILK * 3)
    g.w8('inv', 1, I_APPLEPIE * 3)
    g.w8('energy', 25)
    g.go(6, 6)
    g.rec = True
    g.pause(300)
    g.go_face(6, 5, 'up')
    g.act()
    g.tick(20)
    read(g, 3, 'a', rest=False)                  # Grandpa's cards: Salad and Omelet
    cut(g, lambda: fast(g, lambda: g.wait_menu(0, 0, 9)))   # (the rest of his note), then the book
    g.press('down', 2, 4)                        # hover the omelet: its ingredients
    g.tick(10)
    g.hold(1200)
    g.choose(0, 0, 9, 1)                         # Omelet
    g.tick(10)
    g.press('a')                                 # x1
    g.tick(12)
    read(g, 1)
    g.wait_field()
    g.tick(150)                                  # the chop-sizzle-steam animation
    g.pause(400)
    open_start(g, 2)                             # BAG
    g.tick(20)
    g.hold(500)
    g.press('down', 2, 4); g.tick(8)             # Omelet, Apple Pie
    g.hold(900)
    g.press('a')
    g.yesno(True)
    g.tick(20)
    read(g, 1)
    g.wait_field()
    g.pause(300)
    fast(g, lambda: (g.go(3, 7), g.go(8, 7)))     # Brisk: walking at a run
    g.pause(1200)
    print('buff', g.u8('buff'), 'omelet', g.inv(I_OMELET), 'energy', g.u8('energy'))
    return out(g, 'cooking', collapse_ms=1600)
