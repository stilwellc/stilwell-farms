#!/usr/bin/env python3
"""Compile art/music/*.mml (see art/music/FORMAT.md) -> src/music.h + src/music_data.c

Stream format per channel: pairs of (note, duration-in-16ths). note 0 = rest, 1..84 = C2..B8
(drums: 1 kick 2 snare 3 hat 4 open hat). 0xFE v = volume change. 0xFF = end of stream.
Usage: gen_music.py            compile all
       gen_music.py FILE.mml   check one file and print per-channel lengths
"""
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CANON = ['TITLE', 'SPRING', 'SUMMER', 'FALL', 'WINTER', 'NIGHT', 'TOWN', 'FESTIVAL',
         'JINGLE_SLEEP', 'JINGLE_FANFARE', 'JINGLE_BUY', 'JINGLE_CATCH']
WAVES = {
    'bass':  [0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10],
    'soft':  [0x02, 0x46, 0x8A, 0xCE, 0xFF, 0xFE, 0xEE, 0xDD, 0xCB, 0xA9, 0x87, 0x65, 0x43, 0x21, 0x10, 0x00],
    'organ': [0x9B, 0xDE, 0xFE, 0xDB, 0x97, 0x54, 0x45, 0x79, 0xBC, 0xBA, 0x87, 0x52, 0x10, 0x01, 0x25, 0x78],
    'pluck': [0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00],
}
SEMI = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}
DRUM = {'k': 1, 's': 2, 'h': 3, 'o': 4}


def die(m):
    print('gen_music: ' + m, file=sys.stderr)
    sys.exit(1)


def expand(src, where):
    # [ ... ]N repeats, nestable
    pat = re.compile(r'\[([^\[\]]*)\](\d*)')
    while '[' in src:
        new = pat.sub(lambda m: (m.group(1) + ' ') * int(m.group(2) or 2), src)
        if new == src:
            die(f'{where}: unbalanced [ ]')
        src = new
    return src


def length16(num, dotted, where):
    if num not in (1, 2, 4, 8, 16):
        die(f'{where}: bad length {num}')
    l = 16 // num
    if dotted:
        if l < 2:
            die(f'{where}: cannot dot a 16th')
        l += l // 2
    return l


def parse_channel(src, drums, where):
    src = expand(src.replace('|', ' '), where)
    i, octave, deflen = 0, 4, 4
    out = []  # (note, len) or ('v', vol)
    tie = False
    while i < len(src):
        c = src[i].lower()
        if c in ' \t\n':
            i += 1; continue
        if c == 'o' and not drums:
            m = re.match(r'o(\d)', src[i:])
            octave = int(m.group(1)); i += len(m.group(0)); continue
        if c == '>':
            octave += 1; i += 1; continue
        if c == '<':
            octave -= 1; i += 1; continue
        if c == 'l':
            m = re.match(r'l(\d+)(\.?)', src[i:])
            deflen = length16(int(m.group(1)), m.group(2), where); i += len(m.group(0)); continue
        if c == 'v' and not drums:
            m = re.match(r'v([0-9a-fA-F])', src[i:])
            out.append(('v', int(m.group(1), 16))); i += 2; continue
        if c == '&':
            tie = True; i += 1; continue
        if (not drums and (c in SEMI or c == 'r')) or (drums and (c in DRUM or c == 'r')):
            j = i + 1
            semi = 0
            if not drums and c != 'r':
                while j < len(src) and src[j] in '+#-':
                    semi += -1 if src[j] == '-' else 1
                    j += 1
            m = re.match(r'(\d*)(\.?)', src[j:])
            ln = length16(int(m.group(1)), m.group(2), where) if m.group(1) else (deflen + (deflen // 2 if m.group(2) else 0))
            j += len(m.group(0))
            if drums:
                note = 0 if c == 'r' else DRUM[c]
            elif c == 'r':
                note = 0
            else:
                n = (octave - 2) * 12 + SEMI[c] + semi
                if not 0 <= n < 84:
                    die(f'{where}: note out of range (octave {octave})')
                note = n + 1
            if tie and out and out[-1][0] == note:
                out[-1] = (note, out[-1][1] + ln)
            else:
                out.append((note, ln))
            tie = False
            i = j
            continue
        die(f"{where}: unexpected '{src[i]}' near '{src[i:i+12]}'")
    return out


def parse_file(path):
    name, tempo, loop = None, 7, True
    chans, cfg, cur = {}, {}, None
    for raw in open(path):
        line = re.sub(r'(^|\s)#(\s.*|$)', '', raw.rstrip()).rstrip()   # '#' comment needs whitespace around it
        if not line.strip():
            continue
        p = line.split()
        if p[0] == 'song':
            name = p[1]
            o = dict(x.split('=', 1) for x in p[2:] if '=' in x)
            tempo = int(o.get('tempo', 7)); loop = o.get('loop', 'yes') == 'yes'
        elif p[0] in ('ch1', 'ch2', 'ch3', 'ch4'):
            cur = p[0]
            cfg[cur] = dict(x.split('=', 1) for x in p[1:] if '=' in x)
            chans[cur] = ''
        else:
            if cur is None:
                die(f'{path}: notes before a channel line')
            chans[cur] += ' ' + line
    if not name:
        die(f'{path}: missing song line')
    ev = {c: parse_channel(chans[c], c == 'ch4', f'{path}:{c}') for c in chans}
    lens = {c: sum(l for n, l in e if n != 'v') for c, e in ev.items()}
    if len(set(lens.values())) > 1:
        die(f'{path}: channel lengths differ (in 16ths): {lens}')
    return dict(name=name, tempo=tempo, loop=loop, ev=ev, cfg=cfg, lens=lens)


def stream(ev):
    out = []
    for n, l in ev:
        if n == 'v':
            out += [0xFE, l]
            continue
        while l > 255:
            out += [n, 255]; l -= 255
            if n:
                n = 0  # remainder of a very long note becomes rest (rare)
        out += [n, l]
    return out + [0xFF]


def main():
    files = sys.argv[1:] or sorted(glob.glob(os.path.join(ROOT, 'art', 'music', '*.mml')))
    songs = [parse_file(f) for f in files]
    if sys.argv[1:]:
        for s in songs:
            print(f"{s['name']}: OK, tempo {s['tempo']}, lengths (16ths) {s['lens']}")
        return
    names = [s['name'] for s in songs]
    H = ['// generated by tools/gen_music.py -- do not edit', '#ifndef MUSIC_H', '#define MUSIC_H',
         '#include <gb/gb.h>', '#include <stdint.h>', '#define MUS_NONE 0xFF']
    for k, n in enumerate(names):
        H.append(f'#define MUS_{n} {k}')
    for n in CANON:
        if n not in names:
            H.append(f'#define MUS_{n} MUS_NONE')
    H += [f'#define NUM_SONGS {len(names)}',
          'typedef struct { uint8_t tempo, loop, duty1, env1, duty2, env2, wave, wvol; const uint8_t *ch[4]; } SongDef;',
          'void music_play(uint8_t id);', 'void music_stop(void);', 'void music_tick(void);',
          'void music_hold(uint8_t ch, uint8_t frames);  // let a sound effect own a channel',
          'extern uint8_t music_on;', 'void music_get(uint8_t id, SongDef *out) BANKED;',
          'void music_wave(uint8_t w) BANKED;', '#endif']
    open(os.path.join(ROOT, 'src', 'music.h'), 'w').write('\n'.join(H) + '\n')
    C = ['// generated by tools/gen_music.py -- do not edit', '#pragma bank 255', '#include <gb/gb.h>',
         '#include "music.h"', 'BANKREF(music_get)']
    wl = list(WAVES)
    defs = []
    for k, s in enumerate(songs):
        ptrs = []
        for c in ('ch1', 'ch2', 'ch3', 'ch4'):
            if c in s['ev']:
                data = stream(s['ev'][c])
                C.append(f"static const uint8_t s{k}_{c}[] = {{{','.join(map(str, data))}}};")
                ptrs.append(f's{k}_{c}')
            else:
                ptrs.append('0')
        cf = s['cfg']
        d1 = int(cf.get('ch1', {}).get('duty', 2)); e1 = int(cf.get('ch1', {}).get('env', 'a2'), 16)
        d2 = int(cf.get('ch2', {}).get('duty', 1)); e2 = int(cf.get('ch2', {}).get('env', '62'), 16)
        wv = wl.index(cf.get('ch3', {}).get('wave', 'bass')); vol = int(cf.get('ch3', {}).get('vol', 2))
        defs.append(f"  {{{s['tempo']}, {1 if s['loop'] else 0}, {d1}, {e1}, {d2}, {e2}, {wv}, {vol}, {{{', '.join(ptrs)}}}}},")
    C += ['static const SongDef songs[] = {'] + (defs or ['  {0}']) + ['};']
    C += ['void music_get(uint8_t id, SongDef *out) BANKED { *out = songs[id]; }']
    # note frequency tables live here so bank 0 stays free; the driver reads them with this bank mapped
    C.append('const uint16_t sq_freq[84] = {44,156,263,363,457,547,631,711,786,856,923,986,1046,1102,1155,1205,1253,1297,1339,1379,1417,1452,1486,1517,1547,1575,1602,1627,1650,1673,1694,1714,1732,1750,1767,1783,1798,1812,1825,1837,1849,1860,1871,1881,1890,1899,1907,1915,1923,1930,1936,1943,1949,1954,1959,1964,1969,1974,1978,1982,1985,1989,1992,1995,1998,2001,2004,2006,2009,2011,2013,2015,2017,2018,2020,2022,2023,2025,2026,2027,2028,2029,2030,2031};')
    C.append('const uint16_t wv_freq[84] = {1046,1102,1155,1205,1253,1297,1339,1379,1417,1452,1486,1517,1547,1575,1602,1627,1650,1673,1694,1714,1732,1750,1767,1783,1798,1812,1825,1837,1849,1860,1871,1881,1890,1899,1907,1915,1923,1930,1936,1943,1949,1954,1959,1964,1969,1974,1978,1982,1985,1989,1992,1995,1998,2001,2004,2006,2009,2011,2013,2015,2017,2018,2020,2022,2023,2025,2026,2027,2028,2029,2030,2031,2032,2033,2034,2035,2036,2036,2037,2038,2038,2039,2039,2040};')
    C.append('static const uint8_t waves[][16] = {' + ','.join('{' + ','.join(map(str, WAVES[w])) + '}' for w in wl) + '};')
    C += ['void music_wave(uint8_t w) BANKED {', '  uint8_t i; NR30_REG = 0;',
          '  for (i = 0; i < 16; i++) (*(volatile uint8_t *)(0xFF30 + i)) = waves[w][i];', '  NR30_REG = 0x80;', '}']
    open(os.path.join(ROOT, 'src', 'music_data.c'), 'w').write('\n'.join(C) + '\n')
    print(f'gen_music: {len(songs)} songs: ' + ', '.join(names))


if __name__ == '__main__':
    main()
