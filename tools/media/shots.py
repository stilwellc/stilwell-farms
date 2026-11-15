#!/usr/bin/env python3
"""README gameplay GIFs, each shot from real headless play of build/stilwell_farms.gbc.

    .venv/bin/python tools/media/shots.py farming design ...     # writes docs/media/<name>.gif
    .venv/bin/python tools/media/shots.py --list

Each shot starts from one of the QA suite's bases (a new game, the cleared farm, or standing in town) and then
plays: real D-pad walks, real tool use, real menus.  Where a shot needs a later-game state to start from, the
setup is done with the emulator paused (gold, a season, an unlocked area) and the shot's docstring says so.
A contact sheet of every shot lands in build/media/<name>_sheet.png for picking cuts.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rec import Rec, base, gif, sheet, speed, png, MEDIA, WORK
import test_suite as T
from test_suite import *          # constants (C_*, DK_*, T_*, B_*, MAP_*...) and the scenario helpers

SHOTS = {}


def _purse(self):
    """the shots' gold: plenty for what each one buys, and a believable number on the HUD"""
    self.w32('gold', 98500)
    self.w16('wood', 900)
    self.w16('stone', 900)


Rec.rich = _purse


def shot(fn):
    SHOTS[fn.__name__.replace('shot_', '')] = fn
    return fn


def tool(g, t):
    """SELECT through the tools until t is in hand (the HUD shows each one)"""
    for _ in range(NUM_TOOLS + 1):
        if g.u8('tool') == t:
            break
        g.press('select', 4, 8)
    g.tick(6)


def use(g, t):
    tool(g, t)
    g.act()
    g.tick(18)
    if g.ui_open():
        g.dismiss()


def quiet(g):
    """the one-time tutorial tips have all been seen (they would pop up over the action)"""
    g.w8('tips', 0xFF); g.w8('barn_tips', 0xFF); g.w8('inv_tips', 0xFF)


def sleep(g, k=2):
    """the trip to bed, k times faster"""
    e, g.every, g.paced = g.every, g.every * k, False
    g.sleep()
    g.every, g.paced = e, True


def out(g, name, cap=None, **kw):
    cap = g.take() if cap is None else cap
    sheet(cap, os.path.join(WORK, name + '_sheet.png'))
    return gif(cap, os.path.join(MEDIA, name + '.gif'), **kw)


# (the original-feature shots live in shots_core.py)


def main():
    import importlib, glob
    for f in sorted(glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shots_*.py'))):
        importlib.import_module(os.path.basename(f)[:-3])    # more shots register themselves with @shot
    names = sys.argv[1:]
    if not names or names == ['--list']:
        print('\n'.join(SHOTS))
        return
    g = Rec()
    for n in names:
        print('shot', n, flush=True)
        g.cap = []
        g.rec = False
        SHOTS[n](g)
    g.stop()


if __name__ == '__main__':
    sys.modules['shots'] = sys.modules['__main__']          # shots_*.py do `from shots import *`
    main()
