#!/usr/bin/env python3
"""Headless playtest driver: runs the ROM in PyBoy and executes a script of inputs,
saving screenshots. Usage: play.py SCRIPT [--out DIR] [--rom ROM]

SCRIPT is a string of ';'-separated commands:
  w N        wait N frames
  p BTN [N]  press BTN (a b start select up down left right) and hold N frames (default 4), then release + 6 frames
  h BTN N    hold BTN for N frames
  s NAME     save screenshot NAME.png (scaled 3x)
  r N CMD    repeat CMD (a single p/h) N times
  poke OFF V / peek OFF [N]   write / print GameState bytes (offsets: see OFF)
"""
import argparse, os, sys
from pyboy import PyBoy

ap = argparse.ArgumentParser()
ap.add_argument('script')
ap.add_argument('--out', default='build/shots')
ap.add_argument('--rom', default='build/stilwell_farms.gbc')
ap.add_argument('--keep-save', action='store_true')
args = ap.parse_args()
os.makedirs(args.out, exist_ok=True)
ram = args.rom + '.ram'
if not args.keep_save and os.path.exists(ram):
    os.remove(ram)

pb = PyBoy(args.rom, window='null', cgb=True, sound_emulated=False)
pb.set_emulation_speed(0)


def tick(n):
    for _ in range(n):
        pb.tick()


def press(btn, n=4):
    pb.button_press(btn)
    tick(n)
    pb.button_release(btn)
    tick(6)


import re
from collections import deque
_map = open(args.rom.replace('.gbc', '.map')).read()
GADDR = int(re.search(r'([0-9A-F]{8})\s+_G\b', _map).group(1), 16)
OFF = dict(cell=3, a1=1027, a2=2051, plots=3075, bld=3077, gold=3289, time=3301, day=3303, season=3304, energy=3306, px=3308, py=3309, facing=3310, scene=3311, tool=3312, wood=3495, stone=3497, style=3321, inv=3325, anim=3157, seeds=3445, year=3305)
WALK = {0, 1, 2, 9, 10, 15, 21}
DIRS = {'down': (0, 1, 0), 'up': (0, -1, 1), 'left': (-1, 0, 2), 'right': (1, 0, 3)}


def mem(name, i=0):
    return pb.memory[GADDR + OFF[name] + i]


def state():
    return mem('px'), mem('py'), mem('facing')


def face(d):
    if mem('facing') != DIRS[d][2]:
        press(d, 2); tick(4)


def go(tx, ty):
    for _ in range(200):
        x, y, f = state()
        if (x, y) == (tx, ty):
            return
        t = mem('time') | (pb.memory[GADDR + OFF['time'] + 1] << 8)
        merch = 480 <= t < 1080
        prev = {(x, y): None}
        q = deque([(x, y)])
        while q:
            c = q.popleft()
            if c == (tx, ty):
                break
            for d, (dx, dy, _) in DIRS.items():
                n = (c[0] + dx, c[1] + dy)
                if n in prev or not (0 <= n[0] < 32 and 0 <= n[1] < 32):
                    continue
                if mem('cell', n[1] * 32 + n[0]) not in WALK:
                    continue
                prev[n] = (c, d)
                q.append(n)
        if (tx, ty) not in prev:
            sys.exit(f'go: no path from {(x, y)} to {(tx, ty)}')
        c = (tx, ty)
        while prev[c][0] != (x, y):
            c = prev[c][0]
        d = prev[c][1]
        face(d)
        press(d, 2); tick(10)
    pb.screen.image.save(os.path.join(args.out, 'stuck.png'))
    sys.exit('go: gave up (see stuck.png)')


def near(ctype):
    """walk next to the nearest cell of type ctype and face it"""
    x0, y0, _ = state()
    best = None
    for i in range(1024):
        if mem('cell', i) != ctype:
            continue
        cx, cy = i % 32, i // 32
        for d, (dx, dy, _) in DIRS.items():
            sx, sy = cx - dx, cy - dy
            if 0 <= sx < 32 and 0 <= sy < 32 and (mem('cell', sy * 32 + sx) in WALK):
                dist = abs(sx - x0) + abs(sy - y0)
                if best is None or dist < best[0]:
                    best = (dist, sx, sy, d)
    if not best:
        sys.exit(f'near: no cell {ctype}')
    go(best[1], best[2])
    face(best[3])


def run(cmd):
    parts = cmd.split()
    if not parts:
        return
    op = parts[0]
    if op == 'w':
        tick(int(parts[1]))
    elif op == 'p':
        press(parts[1], int(parts[2]) if len(parts) > 2 else 4)
    elif op == 'h':
        pb.button_press(parts[1]); tick(int(parts[2])); pb.button_release(parts[1]); tick(2)
    elif op == 's':
        img = pb.screen.image.copy()
        img = img.resize((img.width * 3, img.height * 3), 0)
        img.save(os.path.join(args.out, parts[1] + '.png'))
        print('shot', parts[1])
    elif op == 'go':
        go(int(parts[1]), int(parts[2]))
    elif op == 'poke':  # poke OFFSET VALUE (byte offset into GameState)
        pb.memory[GADDR + int(parts[1])] = int(parts[2])
    elif op == 'peek':  # peek OFFSET [N]: print N bytes of GameState from OFFSET
        n = int(parts[2]) if len(parts) > 2 else 1
        print('PEEK', parts[1], [pb.memory[GADDR + int(parts[1]) + k] for k in range(n)])
    elif op == 'rich':  # test cheat: lots of gold/wood/stone
        for k, b in enumerate((0x40, 0x42, 0x0F, 0)):
            pb.memory[GADDR + OFF['gold'] + k] = b
        for n in ('wood', 'stone'):  # 250 each
            pb.memory[GADDR + OFF[n]] = 250; pb.memory[GADDR + OFF[n] + 1] = 0
    elif op == 'near':
        near(int(parts[1]))
    elif op == 'face':
        face(parts[1])
    elif op == 'stat':
        x, y, f = state()
        print('STAT', dict(px=x, py=y, scene=mem('scene'), time=mem('time') | (mem('time',1) << 8), season=mem('season'), gold=sum(mem('gold', k) << (8 * k) for k in range(4)), day=mem('day'), energy=mem('energy')))
    elif op == 'r':
        for _ in range(int(parts[1])):
            run(' '.join(parts[2:]))
    else:
        sys.exit('bad command ' + cmd)


for c in args.script.split(';'):
    run(c.strip())
pb.stop(save=True)
