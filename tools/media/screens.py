#!/usr/bin/env python3
"""docs/media/screens.png: twelve screens in a grid, picked out of the gameplay GIFs (exact frames, 1x) plus the
title screen straight from the ROM.  Run after shots.py:  .venv/bin/python tools/media/screens.py"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw, ImageSequence
import numpy as np
from rec import Rec, MEDIA
from device import F

# (label, gif, moment as a fraction of the GIF's running time)
PICKS = [('Title screen', None, 0), ('The farm in fall', 'banner_screen', 0.1), ('Valley map', 'valley', 0.17),
         ('Market Street', 'invest', 0.93), ('A heart event', 'townlife', 0.69), ('Inside the barn', 'barns', 0.25),
         ('The mine, floor 20', 'mine', 0.94), ('The beach', 'beach', 0.76), ('Recipe book', 'cooking', 0.25),
         ('Snowball fight', 'festivals', 0.88), ('Farm colours', 'design', 0.11), ('Night', 'seasons', 0.93)]


def frame_at(name, frac):
    im = Image.open(os.path.join(MEDIA, name + '.gif'))
    frames, t = [], 0
    for f in ImageSequence.Iterator(im):
        d = f.info.get('duration', 30)
        frames.append((t, d, f.convert('RGB').copy()))
        t += d
    want = t * frac
    for (t0, d, f) in frames:
        if t0 <= want < t0 + d:
            break
    s = f.width // 160
    return f.resize((160, 144), Image.NEAREST) if s > 1 else f


def title():
    g = Rec()
    g.tick(240)
    im = g.pb.screen.image.convert('RGB').copy()
    g.stop()
    return im


def main():
    shots = []
    for label, name, frac in PICKS:
        if name is None:
            im = title()
        elif name == 'banner_screen':          # the banner's own gameplay, without the handheld
            from device import compose
            _, (x, y, w, h), _ = compose()
            b = Image.open(os.path.join(MEDIA, 'banner.png')).convert('RGB')
            im = b.crop((x, y, x + w, y + h)).resize((160, 144), Image.NEAREST)
        else:
            im = frame_at(name, frac)
        shots.append((label, im.resize((480, 432), Image.NEAREST)))
    W, H = 480, 432; cols, rows = 4, 3; gx, gy, m = 28, 64, 56
    BG, INK = (246, 244, 239), (40, 40, 44)
    font = F(24, 'Medium')
    CW = m * 2 + cols * W + (cols - 1) * gx; CH = m + rows * (H + gy) + (m - gy) + 8
    c = Image.new('RGB', (CW, CH), BG); d = ImageDraw.Draw(c)
    mask = Image.new('L', (W, H), 0); ImageDraw.Draw(mask).rounded_rectangle((0, 0, W - 1, H - 1), radius=12, fill=255)
    for i, (label, im) in enumerate(shots):
        x = m + (i % cols) * (W + gx); y = m + (i // cols) * (H + gy)
        c.paste(im, (x, y), mask)
        d.text((x + 4, y + H + 14), label, font=font, fill=INK)
    p = os.path.join(MEDIA, 'screens.png')
    c.save(p, optimize=True)
    print(p, c.size, os.path.getsize(p) // 1000, 'KB')


if __name__ == '__main__':
    main()
