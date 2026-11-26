"""Draw the six region portraits (32x32) with the townsfolk tools in portraits.py and write art/62_region_portraits.art."""
import math, pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from portraits import Pic, C, FACE, ell, face, shoulders

OUT_FILE = pathlib.Path(__file__).resolve().parents[2] / "art/62_region_portraits.art"


def ring(cx, cy, rx, ry, a0, a1, n, r):
    """bumpy band: circles along an arc of an ellipse (degrees, 90 = top)"""
    cs = [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * k / (n - 1))), cy - ry * math.sin(math.radians(a0 + (a1 - a0) * k / (n - 1)))) for k in range(n)]
    return lambda x, y: any(math.hypot(x - a, y - b) <= r for a, b in cs)


def outer_edge(p, m, inside, c="ink"):
    """outline a mask only where it meets something that is not 'inside' (hair on a face, a beard on a cheek)"""
    for x, y in m:
        if any((x + dx, y + dy) not in m and not inside(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))): p.put(x, y, c)


def smile(p, lip="tan"):
    p.hline(20, 13, 18, "ink"); p.pts("ink", (12, 19), (19, 19)); p.hline(21, 14, 17, lip)


def wren():
    p = Pic()
    shoulders(p, "cream", "ink", 25, 3)
    p.blob(p.mask(lambda x, y: 27 <= y <= 30 and 10 <= x <= 21), "brown")          # leather apron bib and straps
    for y in (25, 26): p.pts("brown", (10, y), (11, y), (20, y), (21, y)); p.pts("ink", (9, y), (12, y), (19, y), (22, y))
    p.hline(28, 12, 19, "tan"); p.pts("tan", (12, 29), (19, 29), (6, 28), (25, 28), (4, 30), (27, 30))
    p.hline(24, 13, 18, "cream"); p.pts("tan", (13, 25), (18, 25), (14, 26), (17, 26))
    H = ell(15.5, 10.8, 10.2, 6.5)
    bun = p.mask(ell(15.5, 3.4, 4.7, 3.2)); p.blob(bun, "grey")
    p.pts("white", (14, 2), (15, 2), (13, 3)); p.pts("slate", (17, 3), (18, 4), (16, 4))
    back = p.mask(lambda x, y: H(x, y) and y <= 10); p.blob(back, "white")
    face(p, "cream", "tan", eyes="none")
    top = p.mask(lambda x, y: H(x, y) and y <= 7)
    p.fill(top, "white"); outer_edge(p, top, lambda x, y: H(x, y) or FACE(x, y))
    p.hline(5, 13, 18, "slate")                                                    # where the bun sits on the head
    p.hline(7, 9, 22, "grey"); p.pts("slate", (15, 7), (16, 7))                                                     # hairline
    for x, y in top:                                                               # silver: grey strands swept up to the bun
        if y <= 7 and p.get(x, y) == C["white"] and (x + 2 * y) % 4 in (0, 1) and abs(x - 15.5) > 1: p.put(x, y, "grey")
    for ex in (11, 19):                                                            # round glasses
        for k in range(4):
            p.pts("ink", (ex - 1 + k, 11), (ex - 1 + k, 16), (ex - 2, 12 + k), (ex + 3, 12 + k))
        p.pts("ink", (ex, 13), (ex + 1, 13), (ex, 14), (ex + 1, 14))
    p.pts("ink", (15, 13), (16, 13), (8, 13), (23, 13))
    p.pts("tan", (9, 18), (10, 18), (21, 18), (22, 18), (9, 9), (22, 9))            # blush, a line on the brow
    smile(p)
    p.frame(); return p.solve("TTTT/WWWW/WWWW/WWWW")


def captain():
    p = Pic()
    shoulders(p, "navy", "navy", 25, 3)
    for x, y in ((5, 28), (6, 29), (26, 28), (25, 29), (4, 30), (27, 30)): p.put(x, y, "slate")
    face(p, "tan", "brown", brow="cream")
    crown = p.mask(lambda x, y: 1 <= y <= 6 and {1: 8, 2: 6, 3: 5, 4: 5, 5: 6, 6: 6}[y] <= x <= 31 - {1: 8, 2: 6, 3: 5, 4: 5, 5: 6, 6: 6}[y])
    p.blob(crown, "blue", "navy"); p.hline(5, 6, 25, "navy")
    p.pts("white", (8, 2), (9, 2), (7, 3), (22, 2))
    for y in (3, 4, 5): p.hline(y, 14, 17, "yellow")                               # cap badge
    p.pts("yellow", (15, 2), (16, 2)); p.pts("navy", (15, 4), (16, 4))
    for y, a, b in ((7, 6, 25), (8, 6, 25), (9, 8, 23)): p.hline(y, a, b, "navy")   # the peak
    p.hline(7, 9, 12, "white")
    win = ell(15.5, 13.5, 6.5, 6.0)
    beard = p.mask(lambda x, y: (FACE(x, y) and (y >= 19 or (y >= 12 and (x <= 9 or x >= 22))))
                   or (ell(15.5, 21.5, 9.6, 7.3)(x, y) and y >= 17 and not win(x, y)) or ell(15.5, 19.2, 5.6, 1.9)(x, y))
    p.fill(beard, "cream"); outer_edge(p, beard, FACE)
    p.hline(21, 14, 17, "ink"); p.pts("tan", (15, 19), (16, 19), (11, 21), (12, 21), (19, 21), (20, 21), (13, 22), (18, 22))
    for x, y in ((9, 20), (8, 22), (11, 23), (22, 20), (20, 23), (15, 23), (16, 23)): p.put(x, y, "tan")
    for x, y in ((10, 25), (13, 26), (17, 25), (20, 26), (15, 27), (22, 24), (9, 24)): p.put(x, y, "grey")
    for x, y in ((18, 21), (19, 21), (20, 22), (21, 22), (22, 22)): p.put(x, y, "ink")   # pipe
    p.blob(p.mask(lambda x, y: 23 <= x <= 27 and 19 <= y <= 23 and not (y == 23 and x in (23, 27))), "brown")
    p.hline(20, 24, 26, "tan")
    p.frame(); return p.solve("UUUU/WWWW/WWWW/TTTT")


def isla():
    p = Pic()
    shoulders(p, "blue", "navy", 25, 3)
    for y in (26, 27): p.hline(y, 11, 13, "white"); p.hline(y, 18, 20, "white")     # collar
    p.pts("white", (7, 28), (10, 29), (15, 29), (20, 29), (5, 30), (13, 30))
    p.hline(24, 13, 18, "cream"); p.hline(25, 13, 18, "cream"); p.pts("navy", (12, 25), (19, 25))
    back = p.mask(lambda x, y: ell(15.5, 14, 10.3, 9.5)(x, y) and 8 <= y <= 20); p.blob(back, "tan")
    face(p, "cream", "tan", brow="brown")
    brim = p.mask(ell(15.5, 7.0, 14.0, 2.5)); p.blob(brim, "yellow", "ink")
    crown = p.mask(lambda x, y: ell(15.5, 5.6, 7.2, 4.8)(x, y) and y <= 6); p.blob(crown, "yellow", "ink")
    p.hline(5, 10, 21, "blue")                         # hat ribbon
    p.pts("white", (12, 2), (13, 2), (18, 3), (5, 6), (6, 6), (25, 7), (10, 7), (20, 7))
    p.pts("tan", (9, 10), (10, 10), (11, 10), (20, 10), (21, 10), (22, 10))         # fringe under the brim
    p.pts("ink", (10, 12), (21, 12), (9, 13), (22, 13))                            # lashes
    p.pts("tan", (9, 17), (11, 17), (10, 18), (20, 17), (22, 17), (21, 18), (14, 16))   # freckles
    smile(p)
    for y in range(17, 24):                                                        # the braid over her shoulder
        p.hline(y, 24, 27, "tan"); p.pts("ink", (23, y), (28, y))
    for y in (17, 20): p.pts("ink", (24, y), (27, y), (25, y + 1), (26, y + 1)); p.pts("brown", (24, y + 2), (27, y + 2))
    p.pts("ink", (24, 23), (27, 23))
    for y in (24, 25): p.hline(y, 24, 27, "blue"); p.pts("navy", (23, y), (28, y))
    for y in (26, 27): p.hline(y, 24, 27, "yellow"); p.pts("navy", (23, y), (28, y))
    p.pts("yellow", (25, 28), (26, 28)); p.pts("navy", (24, 28), (27, 28), (25, 29), (26, 29))
    p.frame(); return p.solve("UUUU/WWWW/WWWW/UUUU")


def dusty():
    p = Pic()
    shoulders(p, "brown", "ink", 24, full=True)
    for y in range(25, 31): p.hline(y, 8, 23, "tan"); p.pts("ink", (7, y), (24, y))
    p.pts("tan", (3, 26), (4, 27), (28, 26), (27, 27), (5, 29), (26, 29))
    band = p.mask(lambda x, y: (24 <= y <= 25 and 10 <= x <= 21) or (26 <= y <= 30 and 11 + (y - 26) <= x <= 20 - (y - 26)))
    p.blob(band, "red"); p.pts("cream", (14, 26), (17, 27), (15, 28))
    face(p, "tan", "brown", eyes="none")
    for x, y in p.mask(lambda x, y: FACE(x, y) and (y >= 18 or (y >= 15 and (x <= 9 or x >= 22))) and (x + y) % 2 == 0):
        if p.get(x, y) != C["ink"]: p.put(x, y, "brown")                           # stubble
    brim = p.mask(ell(15.5, 8.2, 14.0, 2.6)); p.blob(brim, "brown")
    crown = p.mask(lambda x, y: ell(15.5, 5.2, 7.0, 4.6)(x, y) and y <= 7); p.blob(crown, "brown")
    p.hline(6, 10, 21, "tan")                                                      # hat band
    p.pts("bg", (15, 1), (16, 1)); p.pts("ink", (14, 1), (17, 1), (15, 2), (16, 2))  # dented crown
    p.pts("tan", (10, 3), (11, 2), (11, 3), (12, 2)); p.hline(8, 4, 8, "tan"); p.hline(8, 23, 27, "tan")
    p.hline(11, 9, 22, "brown")                                                    # shade under the brim
    for ex in (11, 19):
        p.hline(12, ex - 1, ex + 2, "ink"); p.hline(14, ex - 1, ex + 1, "ink"); p.hline(15, ex - 1, ex + 1, "brown")
    p.pts("ink", (13, 13), (18, 13), (21, 14)); p.pts("brown", (8, 14), (23, 14), (9, 13), (22, 13))   # furrowed brow, crow's feet
    p.hline(20, 13, 18, "ink"); p.put(19, 19, "ink"); p.hline(21, 14, 17, "tan")
    p.frame(); return p.solve("WWWW/WWWW/WWWW/WRRW")


def nora():
    p = Pic()
    shoulders(p, "slate", "navy", 24, full=True)
    hood = p.mask(lambda x, y: ell(15.5, 14.0, 14.2, 13.2)(x, y) and y <= 25); p.blob(hood, "slate", "navy")
    p.pts("grey", (5, 6), (6, 5), (3, 11), (3, 12), (25, 5), (28, 11), (4, 27), (5, 28), (27, 27), (26, 28), (3, 30), (28, 30))
    face(p, "cream", "tan", brow="brown")
    opening = ell(15.5, 14.7, 7.0, 7.7)
    p.fill(p.mask(lambda x, y: FACE(x, y) and y <= 10), "brown")                   # hair under the hood
    p.pts("ink", (10, 11), (12, 10), (14, 11), (17, 10), (19, 11), (21, 10)); p.pts("brown", (11, 11), (13, 11), (18, 11), (20, 11), (9, 11), (22, 11))
    p.pts("tan", (12, 9), (15, 8), (19, 9))
    R = ring(15.5, 14.5, 9.0, 9.3, -40, 220, 15, 2.6)
    fur = p.mask(lambda x, y: R(x, y) and not opening(x, y)); p.fill(fur, "cream")
    for x, y in fur:                                                               # ink round the face, soft shade on the outside
        if any(opening(x + dx, y + dy) or (y + dy >= 20 and FACE(x + dx, y + dy) and (x + dx, y + dy) not in fur) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))): p.put(x, y, "ink")
        elif (x + 1, y) not in fur or (x, y + 1) not in fur: p.put(x, y, "grey")
    for x, y in ((10, 8), (14, 6), (20, 8), (9, 13), (22, 13), (8, 18), (23, 18), (17, 6)):
        if (x, y) in fur and 8 <= x <= 23 and y >= 8: p.put(x, y, "tan")
    p.pts("tan", (10, 17), (11, 17), (10, 18), (11, 18), (20, 17), (21, 17), (20, 18), (21, 18))   # rosy cheeks
    smile(p)
    scarf = p.mask(lambda x, y: 8 <= x <= 23 and 24 <= y <= 30); p.blob(scarf, "red")   # fills the two middle tiles
    p.hline(27, 9, 15, "ink")
    for y in (28, 29): p.put(15, y, "ink")
    for x in (10, 13):
        for y in (25, 26, 28, 29): p.put(x, y, "lgreen")
    p.hline(26, 17, 21, "lgreen"); p.hline(29, 17, 21, "lgreen")
    p.frame(); return p.solve("TTTT/TWWT/TWWT/TRRT")


def sage():
    p = Pic()
    F = ell(15.5, 14.5, 6.6, 9.2)                                                  # a thin face
    shoulders(p, "navy", "navy", 26, 2)
    for x, y in ((8, 28), (9, 29), (23, 28), (22, 29), (6, 30), (25, 30)): p.put(x, y, "slate")
    wing = lambda x, y: 18 <= y <= 27 and 6 <= x <= min(13, 7 + (y - 18))           # the high collar
    col = p.mask(lambda x, y: wing(x, y) or wing(31 - x, y)); p.blob(col, "navy", "navy")
    for y in range(20, 27): p.pts("slate", (7, y), (24, y))
    for y in range(24, 31): p.hline(y, 14, 17, "white")                            # cravat
    p.pts("grey", (14, 26), (17, 26), (15, 28), (16, 28), (14, 30), (17, 30)); p.pts("navy", (13, 28), (18, 28), (13, 29), (18, 29), (13, 30), (18, 30))
    m = p.mask(F); p.blob(m, "cream")
    for y in (14, 15, 16): p.pts("ink", (8, y), (23, y))
    for x, y in m:
        if y >= 12 and not F(x + 2, y + 1): p.put(x, y, "tan")
    p.edge(m, "ink")
    H = ell(15.5, 9.6, 7.6, 6.4)
    hair = p.mask(lambda x, y: H(x, y) and (y <= 7 or ((x <= 9 or x >= 22) and y <= 12)))
    p.fill(hair, "ink"); p.pts("ink", (15, 8), (16, 8), (10, 8), (21, 8))           # slicked back, widow's peak
    p.pts("brown", (11, 5), (12, 4), (13, 4), (18, 5), (19, 5), (15, 6))
    p.pts("ink", (12, 13), (13, 13), (12, 14), (13, 14)); p.hline(10, 11, 14, "ink"); p.put(10, 11, "ink")   # a raised brow
    for k in range(4): p.pts("ink", (17 + k, 11), (17 + k, 16), (16, 12 + k), (21, 12 + k))                # monocle
    p.pts("ink", (18, 13), (19, 13), (18, 14), (19, 14)); p.put(20, 12, "white")
    p.pts("tan", (22, 17), (23, 19), (24, 21), (24, 23)); p.pts("grey", (23, 25), (22, 27))                # its chain
    p.pts("tan", (15, 15), (15, 16), (15, 17), (16, 17), (11, 18), (12, 19))        # long nose, a hollow cheek
    p.hline(19, 12, 19, "ink"); p.pts("ink", (11, 18), (20, 18), (10, 17), (21, 17), (13, 18), (14, 18), (17, 18), (18, 18))   # neat moustache
    p.hline(21, 15, 16, "ink")
    p.frame(); return p.solve("WWWW/WWWW/WWWW/TTTT")


OUT = [("WREN", "# Wren, Amberwood's woodcarver: grey hair in a bun, round glasses, leather apron", wren),
       ("CAPTAIN", "# The Captain, Saltmarsh Harbour's harbourmaster: navy peaked cap, big white beard, pipe", captain),
       ("ISLA", "# Isla, Lakeshore's ferrywoman: straw sun hat, freckles, a braid over one shoulder", isla),
       ("DUSTY", "# Dusty, Sunbake Mesa's prospector: wide-brimmed hat, red bandana, squint, stubble", dusty),
       ("NORA", "# Nora, Pinecrest's innkeeper: fur-trimmed hood, rosy cheeks, red scarf", nora),
       ("SAGE", "# Sage, Stonegate's collector: thin, monocle, neat moustache, dark coat with a high collar", sage)]

if __name__ == "__main__":
    L = ["# Stilwell Farms region portraits: the six people met outside the valley (32x32 busts, same rules as 60_ui_art.art).",
         "# Written by tools/art_src/region_portraits.py: edit the drawing there, then run it.",
         "# portraits are menu art only: far tables (gen_art.py)", "far"]
    for name, comment, fn in OUT:
        rows = fn()
        palmap = [c for c in fn.__code__.co_consts if isinstance(c, str) and c.count("/") == 3][0]   # the solve() argument
        for r in (0, 31):                                                           # round the corners to the backdrop, not UI gold
            t = palmap.split("/")[r // 8]; row = rows[r]
            rows[r] = ("1" if t[0] == "U" else "0") + row[1:-1] + ("1" if t[3] == "U" else "0")
        L += ["", comment, f"meta PORTRAIT_{name} w=2 h=2 pal=WOOD palmap={palmap}"] + rows
    OUT_FILE.write_text("\n".join(L) + "\n")
