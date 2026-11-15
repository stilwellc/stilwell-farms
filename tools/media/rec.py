"""Recording layer for the README media: real headless play of the ROM, captured to GIFs.

`Rec` is the QA suite's driver (tools/test_suite.py `Game`) with a frame recorder bolted on, plus pacing so a
viewer can read what happens: dialogs and menus are held on screen before they are answered, walks hold the
D-pad for whole runs (no stop-start), and repeated frames collapse into one longer frame.

    from rec import Rec, base, gif
    g = Rec()                    # boots build/stilwell_farms.gbc (a private copy under build/media)
    base(g, 'cleared')           # the QA suite's bases: 'farm' (new game), 'cleared', 'town'
    g.rec = True; ...play...; g.rec = False
    gif(g.cap, 'docs/media/x.gif')
"""
import os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import numpy as np
from PIL import Image, ImageDraw
import test_suite as T
from test_suite import DIRS

MEDIA = os.path.join(ROOT, 'docs', 'media')
WORK = os.path.join(ROOT, 'build', 'media')

DIALOG_MS = 1500      # hold a dialog page this long before it is answered
MENU_MS = 650         # hold a menu before the cursor moves
PICK_MS = 380         # hold the chosen row before A


class Rec(T.Game):
    def __init__(self, rom=None):
        os.makedirs(WORK, exist_ok=True)
        src = rom or os.path.join(ROOT, 'build', 'stilwell_farms.gbc')
        dst = os.path.join(WORK, f'media_{os.getpid()}.gbc')   # own battery file per process
        for ext in ('.gbc', '.map', '.noi'):
            if os.path.exists(src.replace('.gbc', ext)):
                shutil.copy(src.replace('.gbc', ext), dst.replace('.gbc', ext))
        self.rec = False
        self.cap = []          # [ [frame ndarray, duration ms] ]
        self.every = 2         # keep every Nth frame (2 = 30 fps)
        self.paced = True      # hold dialogs/menus while recording
        import atexit
        atexit.register(lambda: [os.remove(dst.replace('.gbc', e)) for e in ('.gbc', '.map', '.noi', '.gbc.ram')
                                 if os.path.exists(dst.replace('.gbc', e))])
        super().__init__(dst, WORK)

    # ---- capture
    def tick(self, n=1):
        for _ in range(n):
            super().tick(1)
            if self.rec and self.frames % self.every == 0:
                self.cap.append([np.asarray(self.pb.screen.image.convert('RGB')).copy(), int(1000 * self.every / 60)])

    def hold(self, ms, btn=None):
        """(btn given: QA suite's hold = press a button for n frames)"""
        if btn is not None or isinstance(ms, str):
            return super().hold(ms, btn)
        if self.rec and self.cap:
            self.cap[-1][1] += ms

    def pause(self, ms):
        """let the game run (animations keep going) for ms"""
        self.tick(int(ms * 60 / 1000))

    def take(self):
        c, self.cap = self.cap, []
        return c

    # ---- pacing
    def _typed(self):
        """let a dialog page finish typing (the window stops changing; the blinking "more" arrow, which keeps
        toggling one fixed cell, does not count)"""
        last, same, arrow = None, 0, None
        for _ in range(240):
            self.tick(2)
            wb = 0x9C00 if self.pb.memory[0xFF40] & 0x40 else 0x9800
            cur = bytes(self.pb.memory[0, wb + k] for k in range(32 * 18))
            if last is None:
                last = cur
                continue
            diff = [k for k in range(len(cur)) if cur[k] != last[k]]
            if not diff or diff == [arrow]:
                same += 1
            elif len(diff) == 1 and same >= 3:
                arrow = diff[0]          # one cell changing after a quiet spell: the arrow
                same += 1
            else:
                same = 0
            last = cur
            if same >= 5:
                return

    def dismiss(self, maxn=80, btn='b'):
        if not (self.rec and self.paced):
            return super().dismiss(maxn, btn)
        n = 0
        self.tick(3)
        while self.ui_open() and n < maxn:
            self._typed()
            self.hold(DIALOG_MS)
            self.press('start' if n % 10 == 9 or self.name_entry_open() else btn)
            n += 1
            if not self.ui_open():
                self.tick(6)
        if self.ui_open():
            raise T.Fail('UI did not close after %d presses' % n)
        return n

    def wait_menu(self, x, y, n, advance='a', maxp=60):
        if not (self.rec and self.paced):
            return super().wait_menu(x, y, n, advance, maxp)
        for _ in range(maxp):
            if self.menu_live(x, y, n) is not None:
                self.hold(MENU_MS)
                return
            if self.ui_open():
                self._typed()
                self.hold(DIALOG_MS)
                self.press(advance)
        raise T.Fail(f'menu at {x},{y} n={n} never appeared')

    def press(self, btn, hold=4, after=6):
        if self.rec and self.paced and btn == 'a' and self.ui_open():
            self.hold(PICK_MS)
        super().press(btn, hold, after)
        if self.rec and self.paced and btn in ('up', 'down', 'left', 'right') and self.ui_open():
            self.tick(4)       # cursor moves read as steps, not a blur

    # ---- smooth walking: hold the D-pad over a whole straight run
    def go(self, tx, ty, blocked_ok=False):
        if not self.rec:
            return super().go(tx, ty, blocked_ok)
        for _ in range(60):
            if self.ui_open():
                self.dismiss()
            if self.pos() == (tx, ty):
                return True
            path = self._path(tx, ty)
            if path is None:
                return super().go(tx, ty, blocked_ok)
            d = path[0]
            n = 1
            while n < len(path) and path[n] == d:
                n += 1
            self.face(d)
            self._run(d, n)
        return super().go(tx, ty, blocked_ok)

    def _path(self, tx, ty):
        from collections import deque
        x, y = self.pos()
        prev = {(x, y): None}
        q = deque([(x, y)])
        while q:
            c = q.popleft()
            if c == (tx, ty):
                break
            for d, (dx, dy, _) in DIRS.items():
                nb = (c[0] + dx, c[1] + dy)
                if nb in prev or not self.walkable(*nb):
                    continue
                prev[nb] = (c, d)
                q.append(nb)
        if (tx, ty) not in prev:
            return None
        out, c = [], (tx, ty)
        while prev[c]:
            out.append(prev[c][1]); c = prev[c][0]
        return out[::-1]

    def _run(self, d, n):
        dx, dy, _ = DIRS[d]
        x0, y0 = self.pos()
        tgt = (x0 + dx * n, y0 + dy * n)
        pen = (tgt[0] - dx, tgt[1] - dy)
        self.pb.button_press(d)
        held, last, stall = True, (x0, y0), 0
        for _ in range(n * 40 + 40):
            self.tick(1)
            p = self.pos()
            if p == tgt or self.ui_open():
                break
            if p == last:
                stall += 1
                if stall > 40:
                    break
            else:
                last, stall = p, 0
            if held and (p == pen or n == 1):
                if n == 1 and p == (x0, y0):
                    if stall < 3:
                        continue
                self.pb.button_release(d); held = False
        if held:
            self.pb.button_release(d)
        for _ in range(30):            # finish the step's slide
            self.tick(1)
            if self.pos() == tgt:
                break
        self.tick(2)


# ------------------------------------------------------------------ bases
_BASES = {}


def base(g, name):
    """restore one of the QA suite's bases (built once per process)"""
    rec, g.rec = g.rec, False
    data = T.get_base(g, _BASES, name, WORK)
    g.boot(fresh=True)
    g.restore(data)
    g.rec = rec
    g.outdir = WORK


# ------------------------------------------------------------------ GIF writing
def collapse(cap, cap_ms=2400):
    """merge identical consecutive frames (their durations add up, capped)"""
    out = []
    for f, d in cap:
        if out and np.array_equal(out[-1][0], f):
            out[-1][1] = min(cap_ms, out[-1][1] + d)
        else:
            out.append([f, d])
    return out


def speed(cap, a, b, k):
    """frames a..b play k times faster (drop frames)"""
    seg = cap[a:b]
    keep = [[f, d] for i, (f, d) in enumerate(seg) if i % k == 0]
    return cap[:a] + keep + cap[b:]


def sheet(cap, path, step=None, cols=10, scale=1):
    """contact sheet with frame indices, to pick cuts"""
    step = step or max(1, len(cap) // 60)
    idx = list(range(0, len(cap), step))
    rows = (len(idx) + cols - 1) // cols
    W, H = 160 * scale, 144 * scale
    im = Image.new('RGB', (cols * W, rows * (H + 12)), (30, 30, 30))
    d = ImageDraw.Draw(im)
    for k, i in enumerate(idx):
        x, y = (k % cols) * W, (k // cols) * (H + 12)
        im.paste(Image.fromarray(cap[i][0]).resize((W, H), Image.NEAREST), (x, y + 12))
        d.text((x + 2, y), f'{i} {cap[i][1]}', fill=(255, 255, 0))
    im.save(path)
    return path


def gif(cap, path, scale=2, crop=None, collapse_ms=2400, end_hold=1200):
    """write frames as a GIF: shared exact palette, nearest-neighbour scale, 10 ms-rounded timing"""
    cap = collapse(cap, collapse_ms)
    if end_hold and cap:
        cap[-1][1] = max(cap[-1][1], end_hold)
    frames = [f if crop is None else f[crop[1]:crop[3], crop[0]:crop[2]] for f, _ in cap]
    cols = np.unique(np.concatenate([f.reshape(-1, 3) for f in frames[::max(1, len(frames) // 400)]]), axis=0)
    allc = set(map(tuple, cols))
    for f in frames:        # exact colour set (cheap: game frames have few colours)
        if len(allc) > 256:
            break
        allc.update(map(tuple, np.unique(f.reshape(-1, 3), axis=0)))
    if len(allc) <= 256:
        pal = sorted(allc)
        P = Image.new('P', (1, 1))
        P.putpalette([v for c in pal for v in c] + [0] * (768 - 3 * len(pal)))
    else:
        mos = Image.fromarray(np.concatenate(frames[::max(1, len(frames) // 60)], axis=0))
        P = mos.quantize(256, method=Image.Quantize.MEDIANCUT)
    ims = []
    for f in frames:
        im = Image.fromarray(f)
        if scale != 1:
            im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
        ims.append(im.quantize(palette=P, dither=Image.Dither.NONE))
    # GIF delays are in 10 ms units: carry the rounding error so 33 ms frames average out to 30 fps
    durs, err = [], 0.0
    for _, d in cap:
        t = d + err
        r = max(20, int(round(t / 10.0)) * 10)
        err = t - r
        durs.append(r)
    ims[0].save(path, save_all=True, append_images=ims[1:], duration=durs, loop=0, optimize=True, disposal=1)
    sz = os.path.getsize(path)
    print(f'{os.path.relpath(path, ROOT)}: {len(ims)} frames, {sum(durs) / 1000:.1f}s, {sz / 1e6:.2f} MB')
    return sz


def png(frame, path, scale=3):
    im = Image.fromarray(frame) if isinstance(frame, np.ndarray) else frame
    im.resize((im.width * scale, im.height * scale), Image.NEAREST).save(path, optimize=True)
