"""README GIFs for the original features (farming, building, land, animals, farmhands, town, home, seasons,
fishing).  Run through shots.py:  .venv/bin/python tools/media/shots.py building animals ...

Setup done by poke before recording starts is listed in each shot's docstring."""
from shots import *
import rec as _rec


def pace(dialog=1250, menu=420, pick=260):
    """this process's hold times (each shot runs in its own process when shot in parallel)"""
    _rec.DIALOG_MS, _rec.MENU_MS, _rec.PICK_MS = dialog, menu, pick


def face_actor(g, kind, aid=None, tries=12):
    """walk up to a (wandering) actor of `kind` and face it; returns the actor or None"""
    for _ in range(tries):
        acts = [a for a in g.actors() if a[0] == kind and (aid is None or a[1] == aid)]
        if not acts:
            g.tick(20)
            continue
        px, py = g.pos()
        acts.sort(key=lambda a: abs(a[2] - px) + abs(a[3] - py))
        _, _, ax, ay = acts[0]
        for d, (dx, dy, _) in DIRS.items():
            if g.walkable(ax - dx, ay - dy) or (ax - dx, ay - dy) == g.pos():
                g.go(ax - dx, ay - dy)
                g.face(d)
                break
        fx, fy = {0: (0, 1), 1: (0, -1), 2: (-1, 0), 3: (1, 0)}[g.u8('facing')]
        a = g.actor_at(g.pos()[0] + fx, g.pos()[1] + fy)
        if a and a[0] == kind and (aid is None or a[1] == aid):
            return a
        g.tick(10)
    return None


def bpick(g, cat, item, help_paced=False, first=False):
    """START > BUILD > category > item through the real menus (the BUILD list has 8 categories); after the
    first time the START and BUILD menus go by at play speed, only the item list is held"""
    p, g.paced = g.paced, first
    open_start(g, 3)
    g.choose(0, 0, 8, cat)
    g.paced = p
    n = CAT_N[cat]
    if n > 11:
        g.choose(0, 4, 11, min(item, 10))
        if item >= 10:
            g.choose(0, 4, n - 10, item - 10)
    else:
        g.choose(0, 4, n, item)
    g.tick(10)
    p, g.paced = g.paced, help_paced
    g.dismiss(btn='a')
    g.paced = p


def sleep_cut(g, k=3, cap=300):
    """the trip to bed at k times speed, with the still stretches of the night cut to `cap` ms"""
    from rec import collapse
    before = g.take()
    sleep(g, k)
    g.cap = before + collapse(g.take(), cap)


def reload_farm(g):
    """off camera: a round trip to town redraws the farm after setup pokes"""
    rec, g.rec = g.rec, False
    to_town(g)
    to_farm(g)
    g.rec = rec


# ------------------------------------------------------------------ farming
@shot
def shot_farming(g):
    """till, water, sow three radishes; sleep (at 3x); sprouts. Three more watered days happen off camera
    (real play, cut); then the harvest. Setup: tips marked seen."""
    pace()
    base(g, 'cleared')
    quiet(g)
    g.w16('time', 420)
    reload_farm(g)
    g.go(10, 15)
    g.rec = True
    g.tick(4)
    xs = (10, 11, 12)
    for x in xs:
        g.go_face(x, 15, 'down')
        use(g, T_HOE)
    for x in reversed(xs):
        g.go_face(x, 15, 'down')
        use(g, T_CAN)
    for x in xs:
        g.go_face(x, 15, 'down')
        use(g, T_SEEDS)
    g.pause(200)
    sleep_cut(g, 3)
    g.go_face(11, 15, 'down')
    g.pause(500)
    first = g.take()
    g.rec = False
    for _ in range(3):              # three more days of watering, off camera
        for x in xs:
            g.go_face(x, 15, 'down')
            g.w8('tool', T_CAN); g.act(); g.tick(10); g.dismiss()
        g.sleep()
    g.go(13, 15)
    g.w8('tool', T_HAND)
    g.rec = True
    for x in reversed(xs):
        g.go_face(x, 15, 'down')
        use(g, T_HAND)
    g.pause(300)
    return out(g, 'farming', first + g.take())


# ------------------------------------------------------------------ building
@shot
def shot_building(g):
    """through the real START > BUILD menus: a windmill (ghost preview, confirm), a picket fence painted by
    walking, red flowers, a pond. Setup: gold/wood/stone, tips seen."""
    pace(1100, 320, 220)
    base(g, 'cleared')
    quiet(g)
    g.rich()
    g.w16('time', 540)
    reload_farm(g)
    g.go(11, 20)
    g.park_pet()
    g.rec = True
    g.tick(10)
    bpick(g, 4, 6, first=True)          # BUILDINGS > Windmill
    g.go_face(12, 22, 'up')
    g.pause(500)
    g.go_face(13, 22, 'up')             # the ghost follows the cursor
    g.pause(600)
    g.act()
    g.yesno(True); g.tick(20)
    g.paced = False; g.dismiss(); g.paced = True
    g.press('b'); g.tick(6)
    # a picket fence along the top of the field, painted by holding A and walking
    bpick(g, 2, 5)
    g.go_face(8, 17, 'right')
    paint_walk(g, 'right', 4)
    g.press('b'); g.tick(6)
    # red flowers in front of it
    bpick(g, 1, 1)
    g.go_face(8, 18, 'right')
    paint_walk(g, 'right', 3)
    g.press('b'); g.tick(6)
    # a pond
    bpick(g, 3, 0)
    g.go_face(8, 20, 'right')
    paint_walk(g, 'right', 3)
    g.go_face(8, 21, 'right')
    paint_walk(g, 'right', 3)
    g.press('b'); g.tick(6)
    g.go(11, 22)
    g.pause(1400)
    return out(g, 'building')


# ------------------------------------------------------------------ land
@shot
def shot_land(g):
    """walk to the rope line, read the plot's sign, buy it; the rope comes down and you walk in.
    Setup: gold, tips seen."""
    pace()
    base(g, 'cleared')
    quiet(g)
    g.w32('gold', 20000)
    g.w16('time', 540)
    reload_farm(g)
    g.go(12, 19)
    g.rec = True
    g.tick(10)
    g.go_face(12, 23, 'down')
    g.pause(300)
    g.act()
    g.wait_menu(14, YN_Y, 2)
    g.yesno(True)
    g.tick(40)
    g.dismiss()
    g.pause(600)
    for (x, y) in [(12, 27), (11, 27), (12, 26), (11, 28)]:
        if g.walkable(x, y):
            g.go(x, y)
            break
    g.pause(1000)
    return out(g, 'land')


# ------------------------------------------------------------------ animals
@shot
def shot_animals(g):
    """a sunny morning with the herd out: pet and milk a golden cow, shear a sheep, pet a Jersey and a hen.
    Setup: barn + coop repaired, the animals (golden cow, Jersey cow, sheep with a fleece, cocoa sheep,
    golden hen, brown hen) placed in the save by poke, produce ready, tips seen."""
    base(g, 'cleared')
    quiet(g)
    for b in (B_BARN, B_COOP):
        g.w8('bld', 0, g.bld_find(b) * BLD_SIZE + 3)
    barn, coop = g.bld_find(B_BARN), g.bld_find(B_COOP)
    g.set_anim(0, A_COW, barn, happy=240, flags=AF_READY | (R_LEGEND << 6), name='Goldie')
    g.set_anim(1, A_COW, barn, happy=200, flags=AF_READY | (R_RARE << 6), name='Mocha')
    g.set_anim(2, A_SHEEP, barn, happy=200, flags=AF_READY, name='Wooly')
    g.set_anim(3, A_SHEEP, barn, happy=200, flags=AF_READY | (R_RARE << 6), name='Cocoa')
    g.set_anim(4, A_HEN, coop, happy=200, flags=R_LEGEND << 6, name='Sunny')
    g.set_anim(5, A_HEN, coop, happy=200, flags=R_RARE << 6, name='Penny')
    g.w16('hay', 20)
    g.w8('weather', 0); g.w8('tomorrow', 0)
    g.w16('time', 450)
    reload_farm(g)
    g.go(19, 14)
    g.tick(60)
    g.rec = True
    g.pause(600)
    for kind_id in (0, 2, 4):
        a = face_actor(g, K_ANIMAL, kind_id)
        if a:
            g.act(); g.tick(20)
            g.dismiss()
            g.pause(300)
    g.pause(800)
    return out(g, 'animals')


# ------------------------------------------------------------------ farmhands
@shot
def shot_farmhands(g):
    """Mayor Hollis's HIRE HELP menu at town hall: a Waterer and a Harvester; next morning the crew is out on
    the farm and one stops to chat. Setup: gold, a strip of ripe crops (so there is work), tips seen; the walk
    back to the farm and the night are cut."""
    pace(1150, 380, 240)
    base(g, 'cleared')
    quiet(g)
    g.w32('gold', 30000)
    for x in range(9, 14):
        for y in (21, 22):
            g.setcell(x, y, C_SOIL, 1, (R_COMMON << 6) | 2)
    g.w16('time', 600)
    to_town(g)
    g.go_face(16, 9, 'up')
    g.rec = True
    g.tick(10)
    g.act()
    g.choose(0, 0, HALL_N, 2)           # HIRE HELP
    g.choose(0, 0, 4, ROLE_WATER)
    g.yesno(True)
    g.paced = False
    g.wait_menu(0, 0, 4)                # (the wages line goes by) back at the list: one more
    g.paced = True
    g.hold(500)
    g.choose(0, 0, 4, ROLE_HARVEST)
    g.yesno(True)
    g.tick(20)
    p, g.paced = g.paced, False
    g.dismiss()
    g.paced = p
    g.pause(300)
    first = g.take()
    g.rec = False
    to_farm(g)
    g.w8('weather', 0); g.w8('tomorrow', 0)
    g.sleep()
    g.go(12, 18)
    g.rec = True
    g.pause(1200)
    a = face_actor(g, K_STAFF, ROLE_WATER)
    if a:
        g.act(); g.tick(30); g.dismiss()
    g.pause(1500)
    return out(g, 'farmhands', first + g.take())


# ------------------------------------------------------------------ town
@shot
def shot_town(g):
    """walk the road south into town, buy the boarded-up bakery (the storefront redraws open), stock its
    pantry with strawberries and check its status. Setup: gold, 6 strawberries in the bag, tips seen."""
    base(g, 'cleared')
    quiet(g)
    g.w32('gold', 60000)
    g.w8('inv', 6, I_STRAWB * 3)
    g.w16('time', 600)
    pace(950, 380, 240)
    g.go(15, WH - FO - 3)
    g.rec = True
    g.tick(10)
    to_town(g)
    bx, by = BIZ_SPOT[0]
    door = (bx + 1, by + 3)
    g.go_face(*door, 'up')
    g.pause(400)
    g.act()
    g.yesno(True)
    g.tick(30)
    g.dismiss()
    g.go(door[0], door[1] + 1)
    g.pause(900)
    g.go_face(*door, 'up')
    g.act()
    g.choose(0, 0, BIZ_MENU_N, 0)       # Stock
    g.choose(0, 0, 1, 0)
    g.quantity(6)
    g.tick(10)
    p, g.paced = g.paced, False
    g.dismiss()
    g.paced = p
    g.pause(300)
    g.act()
    g.choose(0, 0, BIZ_MENU_N, 1)       # Status
    g.tick(20)
    g._typed(); g.hold(700)
    g.paced = False
    g.dismiss()
    g.paced = True
    g.pause(600)
    return out(g, 'town')


# ------------------------------------------------------------------ home
def furn_pick(g, kind, first=False):
    """inside: START > BUILD > FURNITURE > kind (the how-to line after it is skipped quickly)"""
    p, g.paced = g.paced, first
    open_start(g, 3)
    g.choose(0, 0, 3, 0)
    g.paced = p
    g.choose(0, 0, len(FURN_CAT), FURN_CAT.index(kind))
    g.tick(10)
    p, g.paced = g.paced, False
    g.dismiss(btn='a')
    g.paced = p


@shot
def shot_interior(g):
    """inside the house, through START > BUILD > FURNITURE: a rug, a fireplace, a painting, an armchair and a
    house plant, each set down in front of you. Setup: gold, tips seen; the first-visit line is skipped off camera."""
    pace()
    base(g, 'farm')
    quiet(g)
    g.w32('gold', 60000)
    g.enter_house()
    g.go(4, 3)
    g.rec = True
    g.tick(10)
    plan = [(FK_RUG, (4, 3), 'down'), (FK_FIRE, (6, 2), 'up'), (FK_PAINT, (3, 2), 'up'),
            (FK_CHAIR, (6, 5), 'left'), (FK_PLANT, (8, 6), 'down')]
    for i, (kind, (x, y), d) in enumerate(plan):
        furn_pick(g, kind, first=(i == 0))
        g.go_face(x, y, d)
        g.pause(200)
        g.act(); g.tick(12)
        if g.ui_open():
            g.dismiss()
        g.press('b'); g.tick(6)
    g.go(5, 7)
    g.pause(1300)
    return out(g, 'interior')


# ------------------------------------------------------------------ seasons
@shot
def shot_seasons(g):
    """the front of the house in spring, summer, fall, winter snow, a rainy day and a lamp-lit night.
    STAGED: the little front garden (path, flower beds, lamps, string lights) is set into the save by poke, and
    season, weather and clock are poked between scenes; each scene then plays live."""
    base(g, 'cleared')
    quiet(g)
    for y in range(12, 16):
        g.setcell(10, y, C_DECOR, DK_PATH)
    for x, k in ((8, DK_FLOWERS_RED), (9, DK_FLOWERS_YELLOW), (11, DK_FLOWERS_YELLOW), (12, DK_FLOWERS_RED)):
        g.setcell(x, 14, C_DECOR, k)
    g.setcell(8, 12, C_DECOR, DK_LAMP)
    g.setcell(13, 14, C_DECOR, DK_LAMP)
    for x in (14, 15, 16):
        g.setcell(x, 12, C_DECOR, DK_STRING)
    g.setcell(14, 13, C_GRASS)              # (a rock in the way)
    scenes = [(0, 0, 600), (1, 0, 660), (2, 0, 900), (3, 2, 600), (0, 1, 840), (1, 0, 1290)]
    caps = []
    for season, weather, t in scenes:
        g.rec = False
        g.w8('season', season); g.w8('weather', weather); g.w8('tomorrow', weather)
        g.w16('time', t)
        to_town(g); to_farm(g)
        g.w16('time', t)
        g.go(16, 13)
        g.face('left')
        g.park_pet()
        g.tick(240)
        g.rec = True
        g.pause(700)
        g.go(12, 13)
        g.pause(1300)
        caps += g.take()
    return out(g, 'seasons', caps)


# ------------------------------------------------------------------ fishing
@shot
def shot_fishing(g):
    """cast into a pond, wait for the bite, play the reel-in bar, land the fish.
    Setup: gold; the pond is dug through build mode off camera; tips seen."""
    base(g, 'cleared')
    quiet(g)
    g.rich()
    build_poke(g, 3, 0)
    for y in (21, 22, 23):
        g.go_face(8, y, 'right'); paint_walk(g, 'right', 5)
    g.press('b'); g.tick(6)
    g.w16('time', 600)
    reload_farm(g)
    g.go(10, 19)
    g.w8('tool', T_ROD - 1)
    g.rec = True
    g.tick(10)
    caught = 0
    for attempt in range(6):
        g.go_face(10, 20, 'down')
        tool(g, T_ROD)
        n0 = g.u16('stat_fish')
        g.act()
        bite = False
        for _ in range(400):
            g.tick(1)
            if g.pb.get_sprite(2).tile_identifier == SPR_EXCLAIM:
                bite = True
                break
        if not bite:
            g.dismiss()
            continue
        g.press('a', 2, 2)
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
            if fish is not None and zone is not None and f % 9 == 0:    # a human's reaction time
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
        g.dismiss()
        if g.u16('stat_fish') > n0:
            caught += 1
            if caught == 2:
                break
            g.pause(400)
    g.pause(800)
    return out(g, 'fishing')
