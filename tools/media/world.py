"""Whole-valley pictures stitched from the game's own screen: the camera is moved across the 64x64 world
(the player is set down at each spot, the renderer streams the view in exactly as it does when walking) and
each screen is pasted at its scroll position.  Sprites (animals, farmhands, the dog) stay in; the player's
own sprite is painted out with a background-only capture of the same screen."""
import numpy as np
from PIL import Image
from test_suite import WW, WH, SCENE_FARM

VIEW_H = 136          # the HUD bar covers the bottom 8 rows


def _w16(g, name, v):
    g.symw8(name, v & 255); g.symw8(name, (v >> 8) & 255, 1)


def _r16(g, name):
    return g.sym8(name) | (g.sym8(name, 1) << 8)


def place(g, x, y, settle=24):
    """set the player down on real cell (x, y); the camera follows"""
    g.w8('px', x); g.w8('py', y)
    _w16(g, '_ppx', x * 16); _w16(g, '_ppy', y * 16)
    g.tick(settle)


def _grab(g, sprites=True):
    lcdc = g.pb.memory[0xFF40]
    m = 0x20 | (0 if sprites else 0x02)
    g.pb.memory[0xFF40] = lcdc & ~m
    g.pb.tick()
    im = np.asarray(g.pb.screen.image.convert('RGB')).copy()
    g.pb.memory[0xFF40] |= lcdc & m      # only the bits taken away (the game may have changed others)
    g.pb.tick()
    return im


def render(g, box=(0, 0, WW, WH), sprites=True):
    """the world cells box=(x0, y0, x1, y1) (real coordinates) as a picture, 16 px per cell"""
    assert g.u8('scene') == SCENE_FARM
    x0, y0, x1, y1 = box
    W, H = (x1 - x0) * 16, (y1 - y0) * 16
    canvas = np.zeros((H, W, 3), np.uint8)
    have = np.zeros((H, W), bool)
    snap = g.snapshot()            # rendering moves the player about: put everything back after
    xs = list(range(x0 * 16, x1 * 16, 144))
    ys = list(range(y0 * 16, y1 * 16, 120))
    cur = list(g.real_pos())

    def walk_to(tx, ty):
        """move the player one cell at a time, as walking does: the renderer streams each new column/row in
        (a long jump can leave stale columns in the tile map)"""
        while (cur[0], cur[1]) != (tx, ty):
            if cur[0] != tx:
                cur[0] += 1 if tx > cur[0] else -1
            else:
                cur[1] += 1 if ty > cur[1] else -1
            place(g, cur[0], cur[1], 3)
        g.tick(20)

    walk_to(min(WW - 1, max(0, (xs[0] + 72) // 16)), min(WH - 1, max(0, (ys[0] + 64) // 16)))
    for j, cy in enumerate(ys):
        for cx in (xs if j % 2 == 0 else xs[::-1]):
            # player so that the camera (ppx - 72, ppy - 64) lands on cx, cy
            px, py = min(WW - 1, max(0, (cx + 72) // 16)), min(WH - 1, max(0, (cy + 64) // 16))
            want = (min(WW * 16 - 160, max(0, px * 16 - 72)), min(WH * 16 - VIEW_H, max(0, py * 16 - 64)))
            walk_to(px, py)
            scx, scy = _r16(g, '_scx'), _r16(g, '_scy')
            if (scx, scy) != want:
                print('world.render: camera at', (scx, scy), 'not', want)
            full = _grab(g, sprites)
            if sprites:            # paint the player out
                bg = _grab(g, False)
                for k in (0, 1):   # the player is OAM 0-1 (an 8x16 pair); its spot lags when it isn't moving
                    oy, ox = g.pb.memory[0xFE00 + 4 * k] - 16, g.pb.memory[0xFE01 + 4 * k] - 8
                    a, b = max(0, oy), min(VIEW_H, oy + 16)
                    l, r = max(0, ox), min(160, ox + 8)
                    if a < b and l < r:
                        full[a:b, l:r] = bg[a:b, l:r]
            # paste the part inside the box that isn't filled yet
            for sy in range(VIEW_H):
                wy = scy + sy - y0 * 16
                if not 0 <= wy < H:
                    continue
                for_x0 = scx - x0 * 16
                l, r = max(0, -for_x0), min(160, W - for_x0)
                if l >= r:
                    continue
                row = have[wy, for_x0 + l:for_x0 + r]
                if row.all():
                    continue
                m = ~row
                canvas[wy, for_x0 + l:for_x0 + r][m] = full[sy, l:r][m]
                have[wy, for_x0 + l:for_x0 + r] |= m
    g.restore(snap)
    return Image.fromarray(canvas)
