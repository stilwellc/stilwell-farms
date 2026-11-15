"""README pictures of the grown farm, from the state latefarm.py plays to (build/media/late/fall.state: the home
valley bought, cleared and built up from day 1 to fall).  Run through shots.py:

    .venv/bin/python tools/media/latefarm.py             # once (a few minutes)
    .venv/bin/python tools/media/shots.py banner valley maturity
"""
import os
from shots import *
from PIL import Image, ImageDraw, ImageFilter
import numpy as np
import rec as R
import world
import latefarm as LF
from device import compose, F

LATE_STATE = os.path.join(WORK, 'late', '{}.state')
TAGLINE = 'A whole valley to farm and build. On a cartridge.'


def late(g, name='fall'):
    g.restore(open(LATE_STATE.format(name), 'rb').read())
    g.outdir = WORK
    g.w8('tips', 0xFF); g.w8('barn_tips', 0xFF); g.w8('inv_tips', 0xFF)


def wallet(g, gold):
    """a believable purse on the HUD (latefarm tops gold up): set it, and let the HUD redraw off camera"""
    g.w32('gold', gold)
    p, g.paced = g.paced, False
    g.press('start'); g.tick(20); g.dismiss()
    g.paced = p


def sunny(g, time):
    """a clear day at `time` (the clock tick after next re-applies the palettes), off camera"""
    g.w8('weather', 0); g.w8('tomorrow', 0)
    g.w16('time', time)
    g.tick(700)


def face_horse(g):
    for _ in range(12):
        hs = [a for a in g.actors() if a[0] == K_HORSE]
        if not hs:
            raise Fail('no horse on the farm')
        _, _, hx, hy = hs[0]
        for d, (ddx, ddy, _) in DIRS.items():
            if g.walkable(hx - ddx, hy - ddy):
                g.go(hx - ddx, hy - ddy); g.face(d)
                break
        x, y = g.pos()
        dx, dy, _ = DIRS[DIRNAME[g.u8('facing')]]
        h = g.actor_at(x + dx, y + dy)
        if h and h[0] == K_HORSE:
            return
        g.tick(20)
    raise Fail('cannot reach the horse')


def mount(g):
    face_horse(g)
    g.act()
    g.choose(12, 3, 4, 0)            # Ride
    g.tick(10)
    p, g.paced = g.paced, False
    g.dismiss()
    g.paced = p


@shot
def shot_banner(g):
    """the hero banner: a fall morning walk round the grown farm, inside the purple handheld"""
    late(g)
    sunny(g, 470)
    wallet(g, 48620)
    g.go(8, 12)
    g.face('right')
    g.rec = True
    g.paced = False
    g.pause(500)
    for x, y in ((16, 12), (16, 23), (9, 23)):        # past the house and barn, down the road, along the fields
        g.go(x, y)
    g.pause(500)
    cap = g.take()
    sheet(cap, os.path.join(WORK, 'banner_sheet.png'))
    banner([f for f, _ in cap])


def banner(frames):
    bg, (x, y, w, h), (glass, gx, gy) = compose(sub=TAGLINE)
    gm = glass.getchannel('A'); d = ImageDraw.Draw(gm)
    d.rectangle((x - gx - 3, y - gy - 3, x - gx + w + 2, y - gy + h + 2), fill=0)   # glass over the bezel only
    glass.putalpha(gm)
    bg.alpha_composite(glass, (gx, gy))
    base_im = bg.convert('RGB')
    still = base_im.copy()
    still.paste(Image.fromarray(frames[len(frames) // 3]).resize((w, h), Image.NEAREST), (x, y))
    still.save(os.path.join(MEDIA, 'banner.png'), optimize=True)
    # one palette: the game's exact colours + the backdrop quantized into what's left
    gcols = set()
    for f in frames[::3]:
        gcols.update(map(tuple, np.unique(f.reshape(-1, 3), axis=0)))
    gcols = sorted(gcols)
    if len(gcols) > 120:
        mos = Image.fromarray(np.concatenate(frames[::max(1, len(frames) // 40)], axis=0))
        q = mos.quantize(120, method=Image.Quantize.MEDIANCUT).getpalette()[:360]
        gcols = [tuple(q[i:i + 3]) for i in range(0, 360, 3)]
    nb = 256 - len(gcols)
    bpal = base_im.quantize(nb, method=Image.Quantize.MEDIANCUT).getpalette()[:nb * 3]
    flat = bpal + [v for c in gcols for v in c]
    P = Image.new('P', (1, 1)); P.putpalette(flat + [0] * (768 - len(flat)))
    baseP = base_im.quantize(palette=P, dither=Image.Dither.FLOYDSTEINBERG)
    out_frames = []
    for f in frames:
        sc = Image.fromarray(f).resize((w, h), Image.NEAREST).quantize(palette=P, dither=Image.Dither.NONE)
        fr = baseP.copy(); fr.paste(sc, (x, y)); out_frames.append(fr)
    pat = [30, 30, 40]
    path = os.path.join(MEDIA, 'banner.gif')
    out_frames[0].save(path, save_all=True, append_images=out_frames[1:], duration=[pat[i % 3] for i in range(len(out_frames))],
                       loop=0, optimize=False, disposal=1)
    print(f'docs/media/banner.gif: {len(out_frames)} frames, {os.path.getsize(path) / 1e6:.2f} MB; banner.png {base_im.size}')


@shot
def shot_valley(g):
    """the 64x64 valley: the map page (whole valley, your land bright, for sale shaded), pick a plot across the
    river, buy it, ride over to it (Setup: the grown farm from latefarm.py; gold topped up)"""
    late(g)
    sunny(g, 620)
    wallet(g, 152400)
    mount(g)
    g.go(23, 12)
    g.rec = True
    g.pause(500)
    p, g.paced = g.paced, False
    open_start(g, 4)
    g.paced = p
    g.choose(8, 2, FARM_N, 1)                  # MAP
    g.pause(2600)
    px, py = g.real_pos()
    for d in ('right', 'right', 'up'):          # across the river, one plot up
        g.press(d); g.pause(450)
    g.pause(700)
    g.press('a')
    g.yesno(True)
    g.tick(20)
    for _ in range(12):                        # read the deed lines (B only: START would open the menu)
        if not g.ui_open():
            break
        g._typed(); g.hold(R.DIALOG_MS); g.press('b'); g.tick(6)
    g.wait_field()
    g.pause(300)
    g.go(26, 12)                               # to the river bank: the new land is just across
    g.pause(900)
    return out(g, 'valley')


STAGES = [('start', 'Day 1', 'An overgrown shack in the middle of a wild valley.'),
          ('spring', 'Spring 5', 'The coop and barn fixed, the first radishes and strawberries.'),
          ('summer', 'Summer 9', 'The home valley bought and built up: Big House, windmill, fields.'),
          ('fall', 'Fall 11', 'Pumpkins, a golden herd, a horse and three farmhands.')]


@shot
def shot_maturity(g):
    """the same farm at four points of latefarm.py's play: maturity.png (the valley around the farm) and
    maturity.gif (the home valley, cross-faded, ending on the late game after dark)"""
    pics = {}
    for name, _, _ in STAGES:
        late(g, name)
        pics[name] = LF.stage(g)
    late(g, 'fall')
    pics['night'] = LF.stage(g, night=True)
    for k, im in pics.items():
        im.save(os.path.join(WORK, 'late', f'stage_{k}.png'))
    # the still: four panels of the valley around the farm (real cells 8..56), captions under each
    box = (8 * 16, 8 * 16, 56 * 16, 56 * 16)
    panels = [pics[n].crop(box) for n, _, _ in STAGES]
    pw, ph = panels[0].size
    ft, fs = F(40, 'Bold'), F(24, 'Regular')
    gx, gy, m = 48, 150, 64
    W = m * 2 + 2 * pw + gx
    H = m + 2 * (ph + gy)
    c = Image.new('RGB', (W, H), (246, 244, 239)); d = ImageDraw.Draw(c)
    for i, (p, (_, t, sub)) in enumerate(zip(panels, STAGES)):
        x = m + (i % 2) * (pw + gx); y = m + (i // 2) * (ph + gy)
        mk = Image.new('L', (pw, ph), 0); ImageDraw.Draw(mk).rounded_rectangle((0, 0, pw - 1, ph - 1), 20, fill=255)
        c.paste(p, (x, y), mk)
        d.text((x + 4, y + ph + 20), t, font=ft, fill=(36, 34, 40))
        d.text((x + 4, y + ph + 74), sub, font=fs, fill=(110, 106, 98))
    c.save(os.path.join(MEDIA, 'maturity.png'), optimize=True)
    print('maturity.png', c.size, os.path.getsize(os.path.join(MEDIA, 'maturity.png')) // 1000, 'KB')
    # the animation: the home valley (real 14..50), 1x, caption chip, cross-fades
    box = (14 * 16, 14 * 16, 50 * 16, 50 * 16)
    seq = [(pics[n].crop(box), t) for n, t, _ in STAGES] + [(pics['night'].crop(box), 'Fall 11, after dark')]
    fc = F(22, 'Bold')

    def chip(im, t):
        im = im.convert('RGB'); dd = ImageDraw.Draw(im, 'RGBA')
        tw = dd.textlength(t, font=fc)
        dd.rounded_rectangle((16, 16, 16 + tw + 32, 58), 21, fill=(20, 16, 30, 180))
        dd.text((32, 24), t, font=fc, fill=(255, 255, 255))
        return im
    cs = [chip(a, t) for a, t in seq]
    frames, durs = [], []
    for i in range(len(cs)):
        frames.append(cs[i]); durs.append(2000)
        nx = cs[(i + 1) % len(cs)]
        for k in range(1, 6):
            frames.append(Image.blend(cs[i], nx, k / 6)); durs.append(70)
    pal = [f.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in frames]
    path = os.path.join(MEDIA, 'maturity.gif')
    pal[0].save(path, save_all=True, append_images=pal[1:], duration=durs, loop=0, optimize=True)
    print('maturity.gif', frames[0].size, os.path.getsize(path) // 1000, 'KB')
