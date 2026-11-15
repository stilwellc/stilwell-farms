"""README GIFs for the town side of v0.3: town life, festivals, Valley Restoration, town investment."""
from shots import *
import rec as _rec
from rec import DIALOG_MS


class brisk:
    """quicker menu pacing (menus held shorter) for shots with a lot of menu work"""
    def __enter__(self):
        self.old = _rec.MENU_MS, _rec.PICK_MS
        _rec.MENU_MS, _rec.PICK_MS = 380, 220

    def __exit__(self, *a):
        _rec.MENU_MS, _rec.PICK_MS = self.old


def read_scene(g, choice=0, maxp=80, ms=(1300, 1300)):
    """play a heart event: the first lines at reading pace, the middle quicker, the question (pick `choice`),
    then the rest of the scene"""
    g.paced = False
    n = 0
    for _ in range(maxp):
        if g.menu_live(0, 5, 2, frames=8) is not None:
            break
        if g.ui_open():
            g._typed()
            g.hold(ms[n] if n < len(ms) else ms[-1])
            n += 1
            g.rec = n < len(ms)          # (a cut: the middle of her story, straight to the question)
            g.press('a')
    g.rec = True
    g.tick(8)
    g.paced = True
    g.choose(0, 5, 2, choice)
    still = 0
    while still < 90:
        g.tick(1)
        if g.ui_open():
            still = 0
            g._typed()
            g.hold(800)
            g.paced = False
            g.press('a')
            g.paced = True
        else:
            still += 1


@shot
def shot_townlife(g):
    """noon: the mayor and Milo walk over to the cafe terrace for lunch; back from the farm, Rosa (1 heart)
    walks up for her first heart event. Set up: tips seen, Rosa's friendship at 1 heart."""
    base(g, 'town')
    quiet(g)
    g.w8('friend', 50, NPC_ROSA)
    g.go(21, 8)
    g.face('down')
    g.w16('time', 715)
    g.rec = True
    g.pause(500)
    g.every = 8                      # the lunch walk at 4x
    wait_npcs(g, {NPC_HOLLIS: SPOT['CAFE_T1'], NPC_MILO: SPOT['CAFE_T2']}, 1800)
    g.every = 2
    g.pause(700)
    walk = g.take()
    g.rec = False
    to_farm(g)
    come_to_town(g)
    g.rec = True
    for _ in range(200):
        g.tick(4)
        if g.ui_open():
            break
    read_scene(g, 0)
    g.pause(800)
    return out(g, 'townlife', walk + g.take())


KEEP = ("Time's up", 'Total', 'Judging!', 'Join the', 'blue ribbon', 'And the', 'Enter the')


def field_quiet(g):
    if g.ui_open():
        return False
    g.tick(6)
    return not g.ui_open()


def skim(g, until, keep=KEEP, ms=1000, after=True, maxshow=99):
    """page through dialogs (A) until until(); only the first page of a line containing a keep word is shown,
    the other pages are cut"""
    g.paced = False
    last = None
    for _ in range(300):
        if until():
            break
        if not g.ui_open():
            g.tick(1)
            continue
        g.rec = False
        g._typed()
        t = g.sbuf()
        show = t != last and any(k in t for k in keep) and maxshow > 0
        maxshow -= show
        last = t
        if show:
            g.rec = True
            g.tick(2)
            g.hold(ms)
        g.press('a')
    g.paced = True
    g.rec = after


def fest_join(g, season, day):
    """festival day (set up: season/day/sunny, 10am), off camera to the hall; on camera: Hollis's
    FESTIVAL row, the welcome, yes"""
    g.rec = False
    fest_setup(g, season, day)
    g.go(16, 9)
    g.face('up')
    g.act()
    skim(g, lambda: g.menu_live(0, 0, HALL_N + 1, frames=6) is not None, keep=())
    g.rec = False
    g.choose(0, 0, HALL_N + 1, 0)
    skim(g, lambda: g.menu_live(14, YN_Y, 2, frames=6) is not None)
    g.yesno(True)


def fest_egg(g):
    fest_join(g, SEASON_SPRING, 14)
    skim(g, lambda: field_quiet(g))
    mask = sym32(g, '_egg_mask')
    eggs = [HIDE[i] for i in range(len(HIDE)) if mask >> i & 1]
    eggs.sort(key=lambda e: abs(e[0] - g.pos()[0]) + abs(e[1] - g.pos()[1]))
    g.symw8('_egg_tmr', 0xFF); g.symw8('_egg_tmr', 0x7F, 1)   # (the harness walks slower than you would)
    g.every = 4                                               # the hunt at 2x
    for (x, y) in eggs[:2]:
        g.use_on(x, y); g.tick(20)
    g.every = 2
    g.pause(400)
    g.rec = False
    for (x, y) in eggs[2:6]:
        g.use_on(x, y); g.tick(10)
    g.symw8('_egg_tmr', 3); g.symw8('_egg_tmr', 0, 1)         # time's up
    g.tick(20)
    skim(g, lambda: field_quiet(g))


def fest_koi(g):
    fest_join(g, SEASON_SUMMER, 14)
    for cast in range(5):
        show = cast in (0, 4)
        g.rec = show
        skim(g, lambda: g.sym8('_fest_cue') == 1, keep=KEEP if cast == 1 else (), after=show)   # (the catch line of the cast before)
        g.press('a', 2, 2)
        g.tick(45)
    g.rec = True
    skim(g, lambda: field_quiet(g))


def fest_snow(g):
    fest_join(g, SEASON_WINTER, 25)
    keys = {1: 'left', 2: 'up', 3: 'right'}
    for r in range(12):
        g.rec = r < 3 or r >= 11
        for _ in range(600):
            cue = g.sym8('_fest_cue')
            if cue in keys:
                break
            if g.ui_open() and g.wtile(2, 9) != font_tile('L'):
                rec, g.rec = g.rec, False         # the rules (cut)
                g.press('a')
                g.rec = rec
            else:
                g.tick(1)
        g.press(keys[cue], 2, 2)
        g.tick(30)
    g.rec = True
    g.tick(30)
    skim(g, lambda: field_quiet(g))


def fest_judge(g):
    g.w8('inv', 1, I_PUMPKIN * 3 + 2)
    fest_join(g, SEASON_FALL, 14)
    skim(g, lambda: g.menu_live(0, 0, 2, frames=6) is not None)
    g.choose(0, 0, 2, 0)
    for _ in range(600):
        if g.sym8('_fest_cue') in (7, 8) and g.ui_open() and g.wtile(2, 5) != GL_FILL:
            break
        g.tick(1)
    g.press('a', 2, 4)
    g.tick(30)
    g.hold(800)
    skim(g, lambda: field_quiet(g), keep=KEEP + ('lumpy', 'shine'))


@shot
def shot_festivals(g):
    """four festivals back to back. Set up per clip: the festival date, sunny, 10am; the egg hunt's timer is
    lengthened for the walking harness; the judging gets one gold-star pumpkin in the bag. Cuts skip the
    middle of each minigame."""
    base(g, 'town')
    quiet(g)
    cap = []
    for f in (fest_egg, fest_koi, fest_judge, fest_snow):
        f(g)
        g.pause(400)
        cap += g.take()
        print(f.__name__, len(cap))
    return out(g, 'festivals', cap)


@shot
def shot_restoration(g):
    with brisk():
        return _restoration(g)


def _restoration(g):
    """Valley Restoration. Part 1: the board at the hall, the Town Garden project: crops donated row by row
    (the middle rows cut), the gold, done; the park outside is a garden now. Set up: the crops in the bag,
    gold. Part 2, the finale: set up with the other five projects done and the fountain's items given (poke);
    on camera the last gold row, the celebration, the credits card, the rebuilt fountain and the orchard park."""
    base(g, 'town')
    quiet(g)
    g.w32('gold', 9000)
    for it, n in {I_RADISH: 5, I_TOMATO: 3, I_PUMPKIN: 2, I_SNOWPEA: 2}.items():
        g.w8('inv', n + 1, it * 3)
    g.go(16, 10)
    g.face('up')
    g.rec = True
    g.pause(300)
    g.go_face(16, 9, 'up'); g.act()
    skim(g, lambda: g.menu_live(0, 0, HALL_N, frames=6) is not None, keep=())
    g.choose(0, 0, HALL_N, 1)                     # VALLEY
    skim(g, lambda: g.menu_live(0, 0, CONST['NUM_PROJ'], frames=6) is not None, keep=('park', 'board', 'Board'),
         ms=1500, maxshow=2)
    g.hold(900)
    g.choose(0, 0, 6, 0)                          # Town Garden
    skim(g, lambda: g.menu_live(0, 0, 5, frames=6) is not None, keep=('park',), ms=1500, maxshow=1)
    g.hold(600)
    g.choose(0, 0, 5, 0); g.yesno(True); g.tick(10)
    g.wait_menu(0, 0, 5)
    g.hold(500)
    g.rec = False
    for k in (1, 2, 3):
        g.choose(0, 0, 5, k); g.yesno(True); g.tick(10)
        g.wait_menu(0, 0, 5)
    g.rec = True
    g.choose(0, 0, 5, 4)                          # the gold
    g.yesno(True)
    skim(g, lambda: g.menu_live(0, 0, 6, frames=6) is not None, keep=('!', 'Garden', 'garden'), ms=1300, maxshow=2)
    g.hold(800)
    g.paced = False
    g.dismiss()
    g.paced = True
    g.every = 4                                   # the walk to the park at 2x
    g.go(15, 17)
    g.every = 2
    g.pause(1500)
    one = g.take()
    # ---- the finale
    g.rec = False
    base(g, 'town')
    quiet(g)
    g.w8('cook_flags', 2)
    g.w8('valley_done', 0x1F)
    for p in range(5):
        g.w8('valley', 0x3F, p)
    g.w8('valley', 0x1F, 5)
    g.w32('gold', 30000)
    g.w16('time', 960)
    g.go_face(16, 9, 'up'); g.act()
    skim(g, lambda: g.menu_live(0, 0, HALL_N, frames=6) is not None, keep=())
    g.choose(0, 0, HALL_N, 1)
    skim(g, lambda: g.menu_live(0, 0, CONST['NUM_PROJ'], frames=6) is not None, keep=())
    g.rec = True
    g.hold(700)
    g.choose(0, 0, 6, 5)                          # the Valley Fountain
    skim(g, lambda: g.menu_live(0, 0, 6, frames=6) is not None, keep=(), maxshow=0)
    g.choose(0, 0, 6, 5)                          # its gold row
    g.yesno(True)
    shown = 0
    g.paced = False
    for _ in range(80):                           # the celebration, until the credits card is up
        g.tick(30)
        if g.ui_open() and g.wtile(3, 0) == GL_HDR_L:
            break
        if g.ui_open():
            g._typed()
            if shown < 2:
                g.hold(1300)
            shown += 1
            g.rec = shown < 2         # (a cut: the rest of the town's toasts, straight to the credits card)
            g.press('a')
    g.rec = True
    g.tick(200)
    g.hold(1300)
    g.press('a'); g.tick(20)
    g.dismiss()
    g.paced = True
    g.wait_field()
    g.pause(600)
    g.go(15, 17)                                  # the fountain behind, the park an orchard
    g.face('up')
    g.pause(1500)
    return out(g, 'restoration', one + g.take())


@shot
def shot_invest(g):
    with brisk():
        return _invest(g)


def _invest(g):
    """Town investment, in four beats. 1) the Bakery's Facade menu: renovate it Modern (live preview), a facade
    upgrade, out front the storefront redrawn (set up: the bakery owned, gold; the 2nd/3rd tiers are bought off
    camera). 2) the plaza, then Hollis's INVEST > Community: Square Paving, the plaza repaved. 3) the Land Office
    sells Market Street, the gate south of the park opens, a Log Cabin Brewery goes up on a lot (scaffold).
    4) the whole town invested in (poke: every business owned and styled, all four lots built, every community
    project): a walk down Market Street."""
    menu = lambda x, y, n: (lambda: g.menu_live(x, y, n, frames=6) is not None)
    base(g, 'town')
    quiet(g)
    g.w8('biz_owned', 1)
    g.w32('gold', 200000); g.w16('wood', 300); g.w16('stone', 300)
    re_enter_town(g)
    door = biz_door(0)
    g.go(door[0], door[1] + 1)
    g.face('up')
    g.rec = True
    g.pause(500)
    g.go_face(*door, 'up'); g.act()
    g.choose(0, 0, BIZ_MENU_N, 3)                 # Facade
    g.choose(0, 0, 3, 0)                          # Style
    g.wait_menu(0, 0, 4)
    g.press('down'); g.tick(20)                   # Modern, previewed on the shop behind the menu
    g.hold(700)
    g.press('a')
    skim(g, menu(14, YN_Y, 2), keep=(), maxshow=0)
    g.yesno(True); g.tick(20)
    skim(g, menu(0, 0, 3), keep=('wonderful',), maxshow=1)
    g.choose(0, 0, 3, 1)                          # Upgrade
    skim(g, menu(14, YN_Y, 2), keep=(), maxshow=0)
    g.yesno(True); g.tick(20)
    skim(g, menu(0, 0, 3), keep=('smart',), maxshow=1)
    g.rec = False
    for t in range(2):
        g.choose(0, 0, 3, 1); g.wait_menu(14, YN_Y, 2); g.yesno(True); g.tick(20); g.wait_menu(0, 0, 3)
    g.paced = False
    g.dismiss()
    g.paced = True
    g.go(door[0], door[1] + 1)
    g.face('up')
    g.rec = True
    g.pause(1500)
    cap = g.take()
    # 2) community: Square Paving
    g.rec = False
    g.go(16, 10)
    g.face('down')
    g.rec = True
    g.pause(500)
    g.go_face(16, 9, 'up'); g.act()
    skim(g, menu(0, 0, HALL_N), keep=(), maxshow=0)
    g.rec = False
    g.choose(0, 0, HALL_N, HALL_N - 2)            # INVEST
    skim(g, menu(0, 0, 4), keep=(), maxshow=0)
    g.choose(0, 0, 4, 1)                          # Community
    g.wait_menu(0, 0, 6)
    g.rec = True
    g.choose(0, 0, 6, 0)                          # Square Paving
    skim(g, menu(14, YN_Y, 2), keep=(), maxshow=0)
    g.yesno(True); g.tick(30)
    skim(g, menu(0, 0, 6), keep=(), maxshow=0)
    g.paced = False
    g.dismiss()
    g.paced = True
    g.face('down')
    g.pause(1000)
    cap += g.take()
    # 3) Market Street
    g.rec = False
    g.w8('biz_owned', 0x03)
    g.go(24, 15)
    g.face('up')
    g.go_face(24, 14, 'up'); g.act()
    g.choose(0, 3, 2, 1)                          # the Land Office: Market St
    g.rec = True
    skim(g, menu(14, YN_Y, 2), keep=('Market Street:',), maxshow=1, ms=1400)
    g.yesno(True)
    skim(g, menu(0, 3, 4), keep=('open!',), maxshow=1, ms=1300)
    g.rec = False
    g.choose(0, 3, 4, 0)                          # lot 1
    g.yesno(True)
    skim(g, menu(0, 3, 4), keep=(), maxshow=0, after=False)
    g.paced = False
    g.dismiss()
    g.paced = True
    g.go(13, 18)
    g.rec = True
    g.every = 4
    to_market(g, *biz_door(4, 0))
    g.every = 2
    g.face('up'); g.act()
    skim(g, menu(0, 0, 4), keep=(), maxshow=0)
    g.choose(0, 0, 4, 0)                          # Brewery
    g.choose(0, 0, 4, 2)                          # Log Cabin
    skim(g, menu(14, YN_Y, 2), keep=(), maxshow=0)
    g.yesno(True); g.tick(20)
    skim(g, field_quiet_f(g), keep=('builders',), maxshow=1)
    g.go(3, 27)
    g.pause(1300)
    cap += g.take()
    # 4) the whole town invested in
    g.rec = False
    base(g, 'town')
    quiet(g)
    g.w8('biz_owned', 0xFF)
    g.w8('mk_flags', MK_OPEN | 0x0F)
    for k in range(4):
        g.w8('biz_lot', k + 1, k)
    for b, lk in enumerate([0 | 3 << 2, 1 | 2 << 2, 3 | 3 << 2, 2 | 1 << 2, 2 | 3 << 2, 3 | 2 << 2, 1 | 3 << 2, 0 | 3 << 2]):
        g.w8('biz_look', lk, b)
    g.w8('town_proj', 0x3F)
    re_enter_town(g)
    g.go(13, 19)
    g.rec = True
    g.pause(300)
    g.every = 4
    g.go(13, 24)
    g.go(4, 26)
    g.go(26, 26)
    g.every = 2
    g.pause(800)
    return out(g, 'invest', cap + g.take())


def field_quiet_f(g):
    return lambda: field_quiet(g)
