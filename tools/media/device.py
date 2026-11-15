"""The README banner's backdrop: a clear purple handheld drawn in PIL on a purple gradient with the title.
`compose()` returns the scene, the screen rectangle to paste gameplay into, and the glass reflection."""
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops
import math
SF='/System/Library/Fonts/SFNS.ttf'
def F(sz, w='Bold'):
    f=ImageFont.truetype(SF, sz)
    try: f.set_variation_by_name(w)
    except Exception: pass
    return f

def vgrad(w, h, stops):
    """stops: list of (t, (r,g,b))"""
    im = Image.new('RGB', (1, h))
    for y in range(h):
        t = y / (h - 1)
        for i in range(len(stops) - 1):
            t0, c0 = stops[i]; t1, c1 = stops[i + 1]
            if t0 <= t <= t1:
                u = (t - t0) / (t1 - t0) if t1 > t0 else 0
                u = u * u * (3 - 2 * u)
                im.putpixel((0, y), tuple(int(c0[k] + (c1[k] - c0[k]) * u) for k in range(3)))
                break
    return im.resize((w, h))

def rr_mask(size, box, r, corners=None):
    m = Image.new('L', size, 0)
    ImageDraw.Draw(m).rounded_rectangle(box, r, fill=255, corners=corners)
    return m

def body_mask(size, box, r, rbr):
    """rounded body with a big bottom-right radius (handheld silhouette)"""
    S = 4
    W, H = size
    m = Image.new('L', (W * S, H * S), 0); d = ImageDraw.Draw(m)
    x0, y0, x1, y1 = [v * S for v in box]
    d.rounded_rectangle((x0, y0, x1, y1), r * S, fill=255)
    # cut the bottom-right corner to a big radius
    R = rbr * S
    d.rectangle((x1 - R, y1 - R, x1, y1), fill=0)
    d.pieslice((x1 - 2 * R, y1 - 2 * R, x1, y1), 0, 90, fill=255)
    return m.resize(size, Image.LANCZOS)

def device(scale=1.0, label='STILWELL FARMS'):
    """returns (RGBA device image, screen box (x,y,w,h) in device coords, glass overlay RGBA same size)"""
    s = scale
    W, H = int(520 * s), int(860 * s)
    pad = int(60 * s)
    CW, CH = W + pad * 2, H + pad * 2
    box = (pad, pad, pad + W, pad + H)
    dev = Image.new('RGBA', (CW, CH), (0, 0, 0, 0))
    bm = body_mask((CW, CH), box, int(34 * s), int(120 * s))
    # --- translucent purple shell
    shell = Image.new('RGBA', (CW, CH), (0, 0, 0, 0))
    col = vgrad(CW, CH, [(0, (176, 140, 236)), (0.5, (138, 98, 214)), (1, (96, 62, 170))]).convert('RGBA')
    col.putalpha(bm.point(lambda v: int(v * 0.80)))
    shell.alpha_composite(col)
    # internal board ghost (visible through the clear shell)
    ghost = Image.new('RGBA', (CW, CH), (0, 0, 0, 0)); gd = ImageDraw.Draw(ghost)
    gb = (box[0] + int(36 * s), box[1] + int(470 * s), box[2] - int(36 * s), box[3] - int(70 * s))
    gd.rounded_rectangle(gb, int(18 * s), fill=(60, 40, 110, 34))
    for i in range(7):  # traces
        y = gb[1] + int((30 + i * 34) * s)
        gd.line((gb[0] + int(20 * s), y, gb[2] - int(20 * s), y), fill=(220, 200, 255, 22), width=max(1, int(2 * s)))
    ghost.putalpha(ImageChops.multiply(ghost.getchannel('A'), bm))
    shell.alpha_composite(ghost)
    # rim light + edge darkening
    edge = Image.new('RGBA', (CW, CH), (255, 255, 255, 0))
    inner = bm.filter(ImageFilter.GaussianBlur(int(10 * s)))
    rim = ImageChops.subtract(bm, inner)
    edge.putalpha(rim.point(lambda v: int(min(255, v * 0.9))))
    shell.alpha_composite(edge)
    # left vertical highlight band
    hl = Image.new('L', (CW, CH), 0); hd = ImageDraw.Draw(hl)
    hd.rounded_rectangle((box[0] + int(14 * s), box[1] + int(30 * s), box[0] + int(40 * s), box[3] - int(140 * s)), int(14 * s), fill=70)
    hl = hl.filter(ImageFilter.GaussianBlur(int(8 * s)))
    hli = Image.new('RGBA', (CW, CH), (255, 255, 255, 0)); hli.putalpha(ImageChops.multiply(hl, bm))
    shell.alpha_composite(hli)
    dev.alpha_composite(shell)
    d = ImageDraw.Draw(dev)
    # --- bezel
    bz = (box[0] + int(40 * s), box[1] + int(48 * s), box[2] - int(40 * s), box[1] + int(430 * s))
    bez = Image.new('RGBA', (CW, CH), (0, 0, 0, 0))
    bzm = rr_mask((CW, CH), bz, int(22 * s), corners=(True, True, True, True))
    # big bottom-right radius on the bezel too
    bzm2 = body_mask((CW, CH), bz, int(22 * s), int(64 * s))
    bc = vgrad(CW, CH, [(0, (38, 34, 50)), (1, (22, 20, 30))]).convert('RGBA'); bc.putalpha(bzm2)
    bez.alpha_composite(bc)
    dev.alpha_composite(bez)
    # screen
    sw, sh = 320, 288
    sx = (CW - sw) // 2; sy = bz[1] + max(18, (bz[3] - bz[1] - sh - int(30 * s)) // 2)
    d.rectangle((sx - 3, sy - 3, sx + sw + 2, sy + sh + 2), fill=(8, 8, 12, 255))
    # bezel text + LED
    f = F(max(10, int(15 * s)), 'Semibold')
    tw = d.textlength(label, font=f)
    d.text(((CW - tw) / 2, sy + sh + int(14 * s)), label, font=f, fill=(150, 140, 175, 255))
    lx, ly = bz[0] + int(22 * s), sy + int(90 * s)
    glow = Image.new('RGBA', (CW, CH), (0, 0, 0, 0)); ImageDraw.Draw(glow).ellipse((lx - 10 * s, ly - 10 * s, lx + 10 * s, ly + 10 * s), fill=(255, 90, 110, 120))
    dev.alpha_composite(glow.filter(ImageFilter.GaussianBlur(5 * s)))
    d.ellipse((lx - 4 * s, ly - 4 * s, lx + 4 * s, ly + 4 * s), fill=(255, 110, 130, 255))
    d.text((lx - int(14 * s), ly + int(10 * s)), 'PWR', font=F(max(8, int(10 * s)), 'Semibold'), fill=(110, 100, 135, 255))
    # --- controls
    def shadowed(shape_fn, off=4):
        sh_ = Image.new('RGBA', (CW, CH), (0, 0, 0, 0)); shape_fn(ImageDraw.Draw(sh_), (20, 8, 40, 120), int(off * s))
        dev.alpha_composite(sh_.filter(ImageFilter.GaussianBlur(4 * s)))
    # d-pad
    cx, cy = box[0] + int(130 * s), box[1] + int(570 * s); a, b = int(26 * s), int(80 * s)
    def dpad(dd, c, o=0):
        dd.rounded_rectangle((cx - a, cy - b + o, cx + a, cy + b + o), int(8 * s), fill=c)
        dd.rounded_rectangle((cx - b, cy - a + o, cx + b, cy + a + o), int(8 * s), fill=c)
    shadowed(dpad, 6)
    dp = Image.new('RGBA', (CW, CH), (0, 0, 0, 0)); dpad(ImageDraw.Draw(dp), (44, 38, 58, 255))
    dev.alpha_composite(dp)
    d.ellipse((cx - 14 * s, cy - 14 * s, cx + 14 * s, cy + 14 * s), fill=(36, 31, 48, 255))
    for ang in range(4):  # arrows
        dx, dy = [(0, -1), (1, 0), (0, 1), (-1, 0)][ang]
        tx, ty = cx + dx * int(56 * s), cy + dy * int(56 * s)
        pts = [(tx + dx * 8 * s, ty + dy * 8 * s), (tx - dy * 8 * s - dx * 4 * s, ty + dx * 8 * s - dy * 4 * s), (tx + dy * 8 * s - dx * 4 * s, ty - dx * 8 * s - dy * 4 * s)]
        d.polygon(pts, fill=(70, 62, 90, 255))
    # A / B
    def btn(x, y, r, lab):
        def sh_(dd, c, o): dd.ellipse((x - r, y - r + o, x + r, y + r + o), fill=c)
        shadowed(sh_, 6)
        bt = Image.new('RGBA', (CW, CH), (0, 0, 0, 0)); bd = ImageDraw.Draw(bt)
        bd.ellipse((x - r, y - r, x + r, y + r), fill=(92, 40, 120, 255))
        hlm = Image.new('L', (CW, CH), 0); ImageDraw.Draw(hlm).ellipse((x - r * 0.7, y - r * 0.85, x + r * 0.3, y - r * 0.1), fill=90)
        hlm = hlm.filter(ImageFilter.GaussianBlur(4 * s))
        w_ = Image.new('RGBA', (CW, CH), (255, 255, 255, 0)); w_.putalpha(hlm)
        dev.alpha_composite(bt); dev.alpha_composite(w_)
        ff = F(int(20 * s), 'Bold'); t = d.textlength(lab, font=ff)
        d.text((x - t / 2 + r * 0.9, y + r * 0.75), lab, font=ff, fill=(70, 45, 110, 255))
    btn(box[0] + int(420 * s), box[1] + int(540 * s), int(36 * s), 'A')
    btn(box[0] + int(330 * s), box[1] + int(585 * s), int(36 * s), 'B')
    # start / select pills
    for i, lab in enumerate(('SELECT', 'START')):
        px_ = CW // 2 - int(70 * s) + i * int(90 * s); py_ = box[1] + int(705 * s)
        pill = Image.new('RGBA', (CW, CH), (0, 0, 0, 0)); pd = ImageDraw.Draw(pill)
        pd.rounded_rectangle((px_, py_, px_ + int(52 * s), py_ + int(14 * s)), int(7 * s), fill=(48, 42, 62, 255))
        dev.alpha_composite(pill)
        ff = F(max(8, int(11 * s)), 'Semibold'); t = d.textlength(lab, font=ff)
        d.text((px_ + int(26 * s) - t / 2, py_ + int(20 * s)), lab, font=ff, fill=(200, 185, 235, 255))
    # speaker slots
    for i in range(6):
        x0 = box[2] - int(160 * s) + i * int(18 * s); y0 = box[3] - int(70 * s) - i * int(8 * s)
        sp = Image.new('RGBA', (CW, CH), (0, 0, 0, 0)); spd = ImageDraw.Draw(sp)
        spd.line((x0, y0 + int(40 * s), x0 + int(26 * s), y0 - int(20 * s)), fill=(50, 30, 80, 200), width=int(7 * s))
        dev.alpha_composite(sp)
    # screws
    for (x, y) in [(box[0] + 22 * s, box[1] + 22 * s), (box[2] - 22 * s, box[1] + 22 * s), (box[0] + 24 * s, box[3] - 26 * s),
                   (box[0] + 24 * s, box[1] + 450 * s), (box[2] - 24 * s, box[1] + 450 * s), (box[2] - 70 * s, box[3] - 24 * s)]:
        r = 7 * s
        d.ellipse((x - r, y - r, x + r, y + r), fill=(196, 190, 210, 230), outline=(110, 100, 130, 255), width=max(1, int(s)))
        d.line((x - r * 0.6, y, x + r * 0.6, y), fill=(90, 80, 110, 255), width=max(1, int(1.6 * s)))
        d.line((x, y - r * 0.6, x, y + r * 0.6), fill=(90, 80, 110, 255), width=max(1, int(1.6 * s)))
    # top edge: power switch nub + cartridge slot hint
    d.rounded_rectangle((box[0] + int(60 * s), box[1] - int(6 * s), box[0] + int(110 * s), box[1] + int(2 * s)), int(3 * s), fill=(120, 90, 180, 220))
    # glass overlay (reflection band over bezel + screen)
    glass = Image.new('RGBA', (CW, CH), (255, 255, 255, 0))
    gm = Image.new('L', (CW, CH), 0); gmd = ImageDraw.Draw(gm)
    gmd.polygon([(bz[0], bz[1]), (bz[0] + int(170 * s), bz[1]), (bz[0] + int(40 * s), bz[3]), (bz[0], bz[3])], fill=26)
    gmd.polygon([(bz[0] + int(200 * s), bz[1]), (bz[0] + int(240 * s), bz[1]), (bz[0] + int(110 * s), bz[3]), (bz[0] + int(70 * s), bz[3])], fill=14)
    glass.putalpha(ImageChops.multiply(gm, bzm2))
    return dev, (sx, sy, sw, sh), glass

def scene(W=1200, H=1210, title='Stilwell Farms', sub=None):
    bg = vgrad(W, H, [(0, (164, 140, 236)), (0.35, (104, 78, 190)), (0.72, (34, 22, 66)), (1, (8, 6, 14))]).convert('RGBA')
    d = ImageDraw.Draw(bg)
    f = F(128, 'Heavy')
    tw = d.textlength(title, font=f)
    d.text(((W - tw) / 2, 54), title, font=f, fill=(255, 255, 255, 255))
    if sub:
        fs = F(30, 'Medium'); t2 = d.textlength(sub, font=fs)
        d.text(((W - t2) / 2, 214), sub, font=fs, fill=(236, 230, 255, 230))
    return bg

def compose(W=1200, H=1210, sub='A farm that’s yours to design. On a cartridge.'):
    bg = scene(W, H, sub=sub)
    dev, (sx, sy, sw, sh), glass = device(0.95)
    ox = (W - dev.width) // 2; oy = 226
    # drop shadow
    sha = Image.new('RGBA', bg.size, (0, 0, 0, 0))
    a = dev.getchannel('A').point(lambda v: int(v * 0.75))
    shc = Image.new('RGBA', dev.size, (10, 4, 24, 255)); shc.putalpha(a)
    sha.alpha_composite(shc, (ox + 18, oy + 34))
    bg.alpha_composite(sha.filter(ImageFilter.GaussianBlur(26)))
    bg.alpha_composite(dev, (ox, oy))
    return bg, (ox + sx, oy + sy, sw, sh), (glass, ox, oy)
