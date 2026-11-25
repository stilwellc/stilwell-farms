"""Draw the townsfolk portraits (32x32) in logical colours, then solve each 8x8 tile to its palette."""
import math, pathlib, re, sys
ROOT = pathlib.Path.home() / "Dev/stilwell-farms"
PAL = {}
for l in (ROOT / "art/00_palettes.art").read_text().split("\n"):
    m = re.match(r"palette bg (\w) \w+\s+(\w{6}) (\w{6}) (\w{6}) (\w{6})", l)
    if m: PAL[m.group(1)] = [tuple(int(h[k:k + 2], 16) for k in (0, 2, 4)) for h in m.groups()[1:]]
C = dict(bg=(248, 240, 216), ink=(40, 24, 16), cream=(248, 240, 216), tan=(208, 140, 72), brown=(136, 80, 40),
         white=(248, 248, 248), grey=(184, 192, 200), slate=(112, 124, 152), navy=(32, 40, 56),
         yellow=(248, 208, 72), blue=(80, 112, 176), red=(224, 72, 56), rtan=(176, 120, 64),
         green=(60, 140, 60), lgreen=(136, 200, 80), dgreen=(24, 56, 40))
W = 32


class Pic:
    def __init__(s): s.a = [[C["bg"]] * W for _ in range(W)]
    def put(s, x, y, c):
        if 0 <= x < W and 0 <= y < W: s.a[y][x] = C[c] if isinstance(c, str) else c
    def get(s, x, y): return s.a[y][x] if 0 <= x < W and 0 <= y < W else None
    def mask(s, f): return {(x, y) for y in range(W) for x in range(W) if f(x, y)}
    def fill(s, m, c):
        for x, y in m: s.put(x, y, c)
    def edge(s, m, c="ink"):
        for x, y in m:
            if any((x + dx, y + dy) not in m for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))): s.put(x, y, c)
    def blob(s, m, c, e="ink"): s.fill(m, c); s.edge(m, e)
    def pts(s, c, *p):
        for x, y in p: s.put(x, y, c)
    def hline(s, y, x0, x1, c):
        for x in range(x0, x1 + 1): s.put(x, y, c)
    def frame(s):
        for k in range(W):
            for x, y in ((k, 0), (k, W - 1), (0, k), (W - 1, k)): s.put(x, y, "ink")
    def solve(s, palmap):
        rows = palmap.split("/"); out = []
        for y in range(W):
            line = ""
            for x in range(W):
                pal = PAL[rows[y // 8][x // 8]]; r, g, b = s.a[y][x]
                line += str(min(range(4), key=lambda i: (pal[i][0] - r) ** 2 + (pal[i][1] - g) ** 2 + (pal[i][2] - b) ** 2))
            out.append(line)
        out[0] = "0" + out[0][1:-1] + "0"; out[-1] = "0" + out[-1][1:-1] + "0"
        return out


def ell(cx, cy, rx, ry): return lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1
FX, FY, RX, RY = 15.5, 14.5, 8.3, 8.9          # the face: rows 6..23, columns 7..24
FACE = ell(FX, FY, RX, RY)


def curls(a0, a1, n, r=2.6, rx=9.4, ry=9.6, cy=13.0):
    """bumpy hair: circles along an arc over the head (angles in degrees, 90 = top)"""
    cs = [(FX + rx * math.cos(math.radians(a0 + (a1 - a0) * k / (n - 1))), cy - ry * math.sin(math.radians(a0 + (a1 - a0) * k / (n - 1)))) for k in range(n)]
    return lambda x, y: any(math.hypot(x - cx, y - cy_) <= r for cx, cy_ in cs)


def face(p, skin, shade, eyes="open", brow="ink"):
    m = p.mask(FACE); p.blob(m, skin)
    for y in (14, 15, 16): p.pts("ink", (6, y), (25, y))                   # ears
    p.pts(skin, (7, 14), (7, 15), (7, 16), (24, 14), (24, 15), (24, 16)); p.pts(shade, (7, 15), (24, 15))
    for x, y in m:                                                         # soft shadow down the right cheek and under the chin
        if y >= 12 and FACE(x, y) and not FACE(x + 2, y + 1) and (x, y) in m and p.get(x, y) == C[skin]: p.put(x, y, shade)
    p.edge(m, "ink")
    if eyes == "open":
        for ex in (11, 19):
            for dx in (0, 1):
                for dy in (0, 1, 2): p.put(ex + dx, 13 + dy, "ink")
            p.put(ex, 13, "white")
            p.hline(11, ex - 1, ex + 2, brow)
    p.pts(shade, (15, 17), (16, 17), (16, 16))                             # nose
    return m


def shoulders(p, c, edge, y0=25, slope=3, full=False):
    m = set()
    for y in range(y0, 31):
        half = 15 if full else min(15, 8 + (y - y0) * slope)
        for x in range(16 - half, 16 + half): m.add((x, y))
    p.blob(m, c, edge); return m


def alyssa():
    p = Pic()
    hair = p.mask(lambda x, y: (ell(15.5, 13, 12.2, 11.2)(x, y) and y <= 16) or (16 < y <= 30 and (3 <= x <= 10 or 21 <= x <= 28)))
    p.blob(hair, "brown")
    sh = shoulders(p, "cream", "ink", 25, 3)
    tank = p.mask(lambda x, y: 26 <= y <= 30 and 10 <= x <= 21 and not (y <= 27 and 13 <= x <= 18))
    p.fill(tank, "ink"); p.pts("ink", (10, 25), (21, 25))
    for y in range(17, 31):                                                # the hair falls in front of the shoulders
        for x in list(range(3, 9)) + list(range(23, 29)): p.put(x, y, "brown")
        p.pts("ink", (2, y), (29, y), (9, y) if y >= 24 else (2, y), (22, y) if y >= 24 else (29, y))
    for x, y in ((5, 9), (4, 13), (5, 18), (6, 22), (4, 26), (7, 28), (26, 10), (27, 15), (25, 20), (26, 25), (24, 28), (11, 4), (16, 3), (21, 4), (9, 6), (23, 7)):
        p.pts("tan", (x, y), (x, y + 1))
    for y in (24,): p.hline(y, 13, 18, "cream")
    face(p, "cream", "tan")
    fringe = p.mask(lambda x, y: FACE(x, y) and (y <= 8 or (y <= 11 and x <= 10 + (11 - y) * 2 and x >= 7) or (y <= 10 and x >= 22)))
    p.fill(fringe, "brown")
    for x, y in ((12, 9), (11, 10), (10, 11), (9, 12), (14, 8), (17, 8), (20, 8), (22, 10), (23, 11)): p.put(x, y, "ink")
    p.pts("tan", (10, 7), (13, 6), (18, 6), (21, 7))
    p.pts("ink", (10, 12), (21, 12), (9, 13), (22, 13))                    # lashes
    p.pts("tan", (9, 17), (10, 17), (21, 17), (22, 17))                    # blush
    p.hline(20, 13, 18, "ink"); p.pts("ink", (12, 19), (19, 19)); p.hline(21, 14, 17, "tan")
    p.pts("tan", (5, 17), (6, 18), (5, 19), (4, 18)); p.pts("ink", (5, 16), (6, 17), (7, 18), (6, 19), (5, 20), (4, 19), (3, 18), (4, 17)); p.put(5, 18, "brown")   # gold hoop
    p.frame(); return p.solve("WWWW/WWWW/WWWW/WWWW")


def andy():
    p = Pic()
    shoulders(p, "green", "dgreen", 24, full=True)
    for x, y in ((4, 26), (5, 26), (6, 27), (25, 26), (26, 26), (24, 27), (3, 29), (28, 29), (9, 28), (22, 28), (15, 29), (16, 29)): p.put(x, y, "lgreen")
    for x in range(10, 22): p.put(x, 25, "dgreen")                         # crew neck
    for x in range(11, 21): p.put(x, 24, "lgreen")
    hair = p.mask(lambda x, y: curls(-15, 195, 13)(x, y) or (ell(15.5, 12, 9.6, 8.6)(x, y) and y <= 13))
    p.blob(hair, "ink")
    face(p, "cream", "tan")
    over = p.mask(lambda x, y: (curls(-15, 195, 13)(x, y) or (ell(15.5, 12, 9.6, 8.6)(x, y) and y <= 9)) and (y <= 9 or x <= 7 or x >= 24))
    p.fill(over, "ink")
    for x, y in ((9, 10), (11, 10), (12, 9), (14, 10), (16, 10), (18, 9), (20, 10), (22, 10)): p.put(x, y, "ink")
    for x, y in ((8, 5), (12, 3), (17, 2), (21, 4), (24, 7), (6, 9), (14, 6), (19, 7), (10, 7), (25, 11), (5, 12)): p.pts("brown", (x, y), (x + 1, y))
    p.pts("tan", (9, 18), (10, 18), (21, 18), (22, 18))
    p.hline(20, 12, 19, "ink"); p.pts("ink", (11, 19), (20, 19)); p.hline(21, 14, 17, "tan")
    p.frame(); return p.solve("WWWW/WWWW/WWWW/GGGG")


def raza():
    p = Pic()
    shoulders(p, "red", "ink", 24, full=True)
    for x in range(11, 21):
        for y in range(24, 31): p.put(x, y, "white")
    for y in range(24, 31): p.pts("navy", (10, y), (21, y)); p.pts("slate", (11, y), (20, y))
    p.hline(25, 12, 19, "grey"); p.hline(24, 12, 19, "navy")
    for x, y in ((3, 26), (4, 27), (27, 26), (26, 27), (6, 29), (25, 29), (2, 30), (29, 30)): p.put(x, y, "rtan")
    for y in (26, 27, 28): p.pts("ink", (8, y), (23, y))
    hair = p.mask(lambda x, y: curls(-25, 205, 14, 2.9, 9.8, 10.0)(x, y) or (ell(15.5, 12, 10, 9)(x, y) and y <= 13))
    p.blob(hair, "ink")
    face(p, "tan", "brown", brow="ink")
    over = p.mask(lambda x, y: (curls(-25, 205, 14, 2.9, 9.8, 10.0)(x, y) or (ell(15.5, 12, 10, 9)(x, y) and y <= 9)) and (y <= 9 or x <= 7 or x >= 24))
    p.fill(over, "ink")
    for x, y in ((9, 10), (10, 11), (12, 10), (13, 10), (15, 9), (17, 10), (19, 10), (21, 11), (22, 10)): p.put(x, y, "ink")
    for x, y in ((9, 4), (14, 2), (19, 3), (23, 6), (6, 8), (12, 6), (17, 6), (26, 10), (4, 12)): p.pts("brown", (x, y), (x + 1, y))
    beard = p.mask(lambda x, y: FACE(x, y) and (y >= 20 or (y >= 16 and (x <= 9 or x >= 22))))
    p.fill(beard, "ink")
    p.hline(19, 11, 20, "ink"); p.pts("ink", (10, 20), (21, 20))           # moustache into the beard
    p.hline(20, 13, 18, "tan"); p.hline(21, 14, 17, "brown")
    for x, y in ((11, 22), (15, 23), (20, 22), (9, 18), (22, 18)): p.put(x, y, "brown")
    p.frame(); return p.solve("WWWW/WWWW/WWWW/RTTR")


def joe():
    p = Pic()
    shoulders(p, "white", "navy", 25, 3)
    for x, y in ((6, 28), (7, 29), (25, 28), (24, 29), (15, 28), (16, 29), (10, 27), (21, 27), (4, 30), (27, 30)): p.put(x, y, "grey")
    p.hline(26, 11, 20, "grey"); p.hline(25, 12, 19, "navy")
    hair = p.mask(lambda x, y: ell(15.5, 9.4, 10.2, 7.6)(x, y) and y <= 11)
    for x, y in hair: p.put(x, y, "yellow" if y <= 7 else "tan")
    p.edge(hair, "navy")
    face(p, "cream", "tan", brow="brown")
    H = ell(15.5, 9.6, 10.4, 7.8)
    top = p.mask(lambda x, y: H(x, y) and (y <= 9 or (y <= 11 and (x <= 8 or x >= 23))))
    for x, y in top: p.put(x, y, "yellow" if y <= 7 else "tan")
    for x, y in top:                                                       # outline only where the hair meets the background
        if any(not H(x + dx, y + dy) and not FACE(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, -1))): p.put(x, y, "navy" if y <= 7 else "ink")
    for x in (9, 12, 13, 16, 19, 20, 22): p.put(x, 10, "tan")              # a jagged fringe
    for x in (10, 14, 17, 21): p.put(x, 10, "tan"); p.put(x, 11, "tan")
    for x, y in ((10, 4), (14, 3), (19, 3), (12, 6), (17, 5), (22, 5), (8, 6)): p.pts("white", (x, y))
    for x, y in ((9, 8), (13, 9), (18, 8), (22, 9), (15, 8)): p.put(x, y, "brown")
    p.hline(19, 11, 20, "brown"); p.hline(20, 10, 12, "brown"); p.hline(20, 19, 21, "brown"); p.hline(18, 13, 18, "tan")   # moustache
    p.hline(21, 13, 18, "ink"); p.hline(22, 14, 17, "tan")
    p.pts("tan", (9, 17), (22, 17))
    p.frame(); return p.solve("UUUU/WWWW/WWWW/TTTT")


def corey():
    p = Pic()
    shoulders(p, "navy", "navy", 25, 3)
    for x, y in ((5, 28), (6, 29), (7, 30), (26, 28), (25, 29), (24, 30), (9, 26), (22, 26)): p.put(x, y, "slate")
    for y in range(26, 31): p.pts("grey", (13, y), (18, y))                # drawstrings
    hood = p.mask(lambda x, y: 24 <= y <= 26 and 8 + (26 - y) <= x <= 23 - (26 - y)); p.fill(hood, "slate"); p.hline(24, 10, 21, "navy")
    face(p, "cream", "tan", brow="brown")
    cap = p.mask(lambda x, y: ell(15.5, 9.2, 10.0, 7.4)(x, y) and y <= 7)
    p.blob(cap, "blue", "navy")
    brim = p.mask(lambda x, y: 4 <= y <= 7 and 1 <= x <= 7 and x >= 8 - (y - 3) * 2); p.blob(brim, "blue", "navy")
    arch = p.mask(lambda x, y: ell(15.5, 7.9, 4.2, 3.2)(x, y) and 5 <= y <= 7); p.fill(arch, "white"); p.edge(arch, "navy")
    p.hline(7, 12, 19, "yellow")                                           # the strap
    p.pts("yellow", (15, 2), (16, 2)); p.pts("white", (9, 4), (10, 3), (21, 4))
    for x in range(6, 26):
        if FACE(x, 8) or 6 <= x <= 25: p.put(x, 8, "brown")
    p.hline(8, 12, 19, "cream"); p.pts("ink", (5, 8), (26, 8), (11, 8), (20, 8))
    for y in (9, 10, 11, 12): p.pts("brown", (8, y), (23, y))             # sideburns
    beard = p.mask(lambda x, y: FACE(x, y) and (y >= 19 or (y >= 13 and (x <= 9 or x >= 22))))
    p.fill(beard, "brown")
    for x, y in ((9, 20), (12, 22), (15, 23), (19, 22), (22, 20), (8, 16), (23, 16)): p.put(x, y, "tan")
    p.hline(18, 11, 20, "brown"); p.pts("brown", (10, 19), (21, 19))       # moustache
    for x in range(11, 21): p.put(x, 19, "ink"); p.put(x, 21, "ink")
    for x in range(12, 20): p.put(x, 20, "white")                          # the grin
    p.pts("ink", (11, 20), (20, 20))
    p.edge(p.mask(lambda x, y: FACE(x, y) and y >= 9), "ink")
    p.frame(); return p.solve("UUUU/WWWW/WWWW/TTTT")


def mike():
    p = Pic()
    shoulders(p, "navy", "navy", 25, 3)
    for x, y in ((5, 28), (6, 29), (26, 28), (25, 29), (15, 29), (16, 29)): p.put(x, y, "slate")
    p.hline(25, 11, 20, "slate")
    face(p, "brown", "ink", eyes="none")
    for x, y in ((10, 9), (11, 9), (9, 11), (20, 9), (13, 17), (9, 18)): p.put(x, y, "tan")   # highlights on the brow and cheek
    hair = p.mask(lambda x, y: ell(15.5, 11.5, 9.2, 7.6)(x, y) and (y <= 8 or ((x <= 7 or x >= 24) and y <= 12)))
    p.fill(hair, "ink")
    for x, y in ((10, 5), (15, 4), (20, 5)): p.pts("brown", (x, y))
    for x in range(8, 24):                                                 # sunglasses
        for y in (12, 13, 14, 15): p.put(x, y, "ink")
    p.hline(12, 6, 25, "ink"); p.pts("brown", (15, 14), (16, 14), (15, 15), (16, 15), (8, 15), (23, 15)); p.pts("tan", (10, 13), (11, 13), (18, 13), (19, 13))
    p.pts("tan", (15, 17), (16, 18)); p.pts("ink", (16, 17))
    p.hline(20, 11, 20, "ink"); p.hline(21, 10, 12, "ink"); p.hline(21, 19, 21, "ink")   # moustache
    p.hline(22, 14, 17, "tan")
    p.frame(); return p.solve("WWWW/WWWW/WWWW/TTTT")


OUT = {"ROSA": ("# Alyssa, runs the General Store: long brown hair, gold hoop earring, black tank top", "WWWW/WWWW/WWWW/WWWW", alyssa),
       "JUNE": ("# Andy, carpenter: black curly hair, green sweater", "WWWW/WWWW/WWWW/GGGG", andy),
       "ADA": ("# Raza, runs the cafe: dark curly hair, short beard, red hoodie over a white tee", "WWWW/WWWW/WWWW/RTTR", raza),
       "HOLLIS": ("# Joe, mayor: blond hair, moustache, white tee", "UUUU/WWWW/WWWW/TTTT", joe),
       "COREY": ("# Corey, runs the Pub: backwards navy cap, beard, big grin, dark hoodie", "UUUU/WWWW/WWWW/TTTT", corey),
       "MIKE": ("# Mike: short dark hair, sunglasses, moustache, dark tee", "WWWW/WWWW/WWWW/TTTT", mike)}

if __name__ == "__main__":
    f = ROOT / "art/60_ui_art.art"; L = f.read_text().split("\n")
    for name, (comment, palmap, fn) in OUT.items():
        rows = fn(); hdr = f"meta PORTRAIT_{name} w=2 h=2 pal=WOOD palmap={palmap}"
        idx = [k for k, l in enumerate(L) if l.startswith(f"meta PORTRAIT_{name} ")]
        if idx:
            i = idx[0]
            if i and L[i - 1].startswith("#"): L[i - 1] = comment
            L[i:i + 33] = [hdr] + rows
        else:
            L = L + ["", comment, hdr] + rows if L[-1] else L[:-1] + ["", comment, hdr] + rows + [""]
    f.write_text("\n".join(L))
