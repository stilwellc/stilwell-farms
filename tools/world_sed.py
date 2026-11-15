#!/usr/bin/env python3
"""Rewrite C sources from the old in-WRAM farm world (G.cell / G.a1 / G.a2) to the cart-SRAM world.

  G.cell[...]  ->  W_CELL[...]        (plain arrays in the live SRAM bank: lvalues, pointers work)
  G.a1[...]    ->  W_A1[...]
  G.a2[i] = v;      ->  W_A2_SET(i, v);          (a2 lives in another SRAM bank: accessor calls)
  G.a2[i] op= v;    ->  W_A2_SET(i, W_A2(i) op (v));
  ++G.a2[i] / G.a2[i]++ / --  ->  W_A2_SET(i, W_A2(i) +- 1)   (statement form only)
  G.a2[i]           ->  W_A2(i)        (any other read)

Usage: tools/world_sed.py src/*.c   (rewrites in place, prints what it could not convert: chained
assignments like `G.a2[i] = G.a2[j] = 0;` and increments used as values need a hand edit.)
"""
import re
import sys


def bracket(s, i):
    """s[i] == '[': index just past the matching ']'"""
    d = 0
    for j in range(i, len(s)):
        if s[j] == '[':
            d += 1
        elif s[j] == ']':
            d -= 1
            if not d:
                return j + 1
    raise ValueError('unbalanced [')


def stmt_end(s, i):
    """index of the ';' ending the expression starting at i (outside parens/brackets)"""
    d = 0
    for j in range(i, len(s)):
        c = s[j]
        if c in '([{':
            d += 1
        elif c in ')]}':
            d -= 1
        elif c == ';' and d == 0:
            return j
        if d < 0:
            return None
    return None


A2 = re.compile(r"(?:(\+\+|--)\s*)?\bG\.a2\[")


def convert_a2(s, name):
    out, pos, left = [], 0, []
    while True:
        m = A2.search(s, pos)
        if not m:
            out.append(s[pos:])
            break
        pre = m.group(1)
        b0 = s.index('[', m.start())
        b1 = bracket(s, b0)
        idx = convert_a2(s[b0 + 1:b1 - 1], name)
        rest = s[b1:]
        line_start = s.rfind('\n', 0, m.start()) + 1
        before = s[line_start:m.start()].rstrip()
        stmt_start = before == '' or before.endswith((';', '{', '}', ')', 'else'))
        if pre and stmt_start and re.match(r'\s*;', rest):           # ++G.a2[i];
            out.append(s[pos:m.start()])
            out.append(f'W_A2_SET({idx}, W_A2({idx}) {pre[0]} 1)')
            pos = b1
            continue
        mi = re.match(r'\s*(\+\+|--)\s*;', rest)
        if not pre and stmt_start and mi:                            # G.a2[i]++;
            out.append(s[pos:m.start()])
            out.append(f'W_A2_SET({idx}, W_A2({idx}) {mi.group(1)[0]} 1)')
            pos = b1 + mi.end() - 1
            continue
        ma = re.match(r'\s*([|&^+\-*]?)=(?!=)', rest)
        if not pre and stmt_start and ma:                            # G.a2[i] (op)= v;
            v0 = b1 + ma.end()
            e = stmt_end(s, v0)
            val = s[v0:e].strip() if e is not None else None
            if val is None or re.search(r'(?<![=!<>])=(?!=)', val.replace('==', '')):
                left.append(f'{name}: chained/complex a2 assignment near: {s[m.start():m.start() + 60]!r}')
                out.append(s[pos:m.start()] + f'W_A2({idx})')
                pos = b1
                continue
            val = convert_a2(val, name)
            op = ma.group(1)
            out.append(s[pos:m.start()])
            out.append(f'W_A2_SET({idx}, {val})' if not op else f'W_A2_SET({idx}, W_A2({idx}) {op} ({val}))')
            pos = e
            continue
        if pre or re.match(r'\s*(\+\+|--|[|&^+\-*]?=(?!=))', rest):
            left.append(f'{name}: a2 write used as a value near: {s[m.start():m.start() + 60]!r}')
        out.append(s[pos:m.start()] + (pre or '') + f'W_A2({idx})')
        pos = b1
    if left:
        for l in left:
            print(l)
    return ''.join(out)


def convert(s, name='?'):
    s = re.sub(r'\bG\.cell\b', 'W_CELL', s)
    s = re.sub(r'\bG\.a1\b', 'W_A1', s)
    return convert_a2(s, name)


if __name__ == '__main__':
    for path in sys.argv[1:]:
        src = open(path).read()
        new = convert(src, path)
        if new != src:
            open(path, 'w').write(new)
            print(f'rewrote {path}')
