#!/usr/bin/env python3
"""Stilwell Farms economy model: every price, yield, growth time, wage and sales formula read from the C sources
(like tools/test_suite.py does: nothing is hand-copied), the expected value of each activity, the payback of each
investment, and a day-by-day simulation of several player strategies over three in-game years.

Usage:
  .venv/bin/python tools/econ_model.py                 # summary tables on stdout
  .venv/bin/python tools/econ_model.py --report        # also rewrite the generated part of docs/BALANCE.md + charts
  .venv/bin/python tools/econ_model.py --strategy balanced --log   # one strategy's purchase log

The simulation is expected-value (no dice): rain, quality rolls, breeding, crows and mine finds use their average.
A "day" gives the player the energy of their house and a time budget (the clock runs 6am-2am, 840 s of real play
at the default clock speed); every activity costs energy and/or seconds (see TIME_* below, measured by hand on the
ROM with tools/play.py). The formulas the game uses (businesses, wages, prosperity...) are reimplemented from the
sources and cross-checked against the ROM by the econ_* scenarios in tools/test_suite.py.
"""
import argparse, math, os, re, sys
from collections import defaultdict

ROOT = os.environ.get('ECON_ROOT') or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # ECON_ROOT: another checkout


# ------------------------------------------------------------------ source parsing (same approach as test_suite.py)
def _src(name):
    return open(os.path.join(ROOT, 'src', name)).read()


def _nocomment(s):
    s = re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)
    return re.sub(r'//[^\n]*', '', s)


CONST = {}


def _cval(expr):
    e = re.sub(r'\b(0x[0-9A-Fa-f]+|\d+)[uUlL]+\b', r'\1', expr.strip())
    e = re.sub(r'\((?:u?int(?:8|16|32)_t|char|mt_t)\)', '', e)
    return int(eval(e, {'__builtins__': {}}, CONST))


def _parse_defines(text):
    for name, val in re.findall(r'^\s*#define\s+([A-Za-z_]\w*)[ \t]+([^\n]+)$', _nocomment(text), re.M):
        try:
            CONST.setdefault(name, _cval(val))
        except Exception:
            pass


def _parse_enums(text):
    for body in re.findall(r'\benum\s*\w*\s*\{([^}]*)\}', _nocomment(text)):
        v = -1
        for item in body.split(','):
            item = item.strip()
            if not item:
                continue
            if '=' in item:
                name, expr = item.split('=', 1)
                try:
                    v = _cval(expr)
                except Exception:
                    continue
            else:
                name, v = item, v + 1
            CONST[name.strip()] = v


def _table(src, name):
    m = re.search(r'(?<!\w)' + re.escape(name) + r'\s*(?:\[[^\]]*\])+\s*=\s*\{(.*?)\};', _nocomment(src), re.S)
    if not m:
        raise KeyError(name)
    return m.group(1)


def _ints(body):
    return [_cval(v) for v in body.split(',') if v.strip()]


def _rows(body):
    return [_ints(r) for r in re.findall(r'\{([^{}]*)\}', body)]


def _lit(src, pattern, n=1):
    """integers captured by a regex on a (comment-free) source: for the few prices written inline"""
    m = re.search(pattern, _nocomment(src), re.S)
    if not m:
        raise KeyError(pattern)
    v = [int(x) for x in m.groups()]
    return v[0] if n == 1 else v


for _f in ('game.h', 'decor.h'):
    _t = _src(_f)
    _parse_defines(_t)
    _parse_enums(_t)
for _f in ('world.c', 'biz.c', 'invest.c', 'cooking.c', 'areas.c', 'town.c', 'staff.c', 'valley.c', 'farm.c'):
    _t = _src(_f)
    _parse_enums(_t)
    _parse_defines(_t)
C = CONST
NUM_ITEMS, NUM_CROPS, NUM_PLOTS = C['NUM_ITEMS'], C['NUM_CROPS'], C['NUM_PLOTS']

_items_c, _town_c, _world_c, _menus_c, _farm_c = _src('items.c'), _src('town.c'), _src('world.c'), _src('menus.c'), _src('farm.c')
_biz_c, _invest_c, _staff_c, _cook_c, _areas_c = _src('biz.c'), _src('invest.c'), _src('staff.c'), _src('cooking.c'), _src('areas.c')

ITEMS = re.findall(r'\{"([^"]*)",\s*(\d+)\}', _table(_items_c, 'items'))
NAME = [n for n, _ in ITEMS]
PRICE = [int(p) for _, p in ITEMS]
assert len(PRICE) == NUM_ITEMS
CROPS = [None] + [r[:4] for r in _rows(re.sub(r'MT_\w+', '0', _table(_items_c, 'crops')))[1:]]   # season seed days regrow
RAR_MULT = _ints(_table(_town_c, 'rar_mult'))
LEGEND_EARNED = C.get('LEGEND_EARNED') or _lit(_town_c, r'G\.earned\s*>=\s*(\d+)')
BREED_PRICE = _rows(_table(_town_c, 'breed_price'))            # [cow, hen, sheep][common, rare, golden]
TIER_GOLD = _ints(_table(_town_c, 'tier_gold'))
CAFE_PRICE = _ints(_table(_town_c, 'price'))
CAFE_ENERGY = _ints(_table(_town_c, 'energy'))
FERT_PRICE = C.get('FERT_PRICE') or _lit(_town_c, r'G\.gold\s*/\s*(\d+)\s*>\s*99')
HAY_PRICE = C.get('HAY_PRICE') or 20
HOUSE_GOLD = _lit(_town_c, r'uint32_t g = lv \? (\d+) : (\d+)', 2)[::-1]   # shack -> house, house -> big house
HOUSE_ENERGY = _lit(_town_c, r'G\.energy_max \+= (\d+)')
PLOT_PRICE = _ints(_table(_world_c, 'plot_prices'))
BLDG_GOLD = _ints(_table(_menus_c, 'bldg_gold'))
BLDG_WOOD = _ints(_table(_menus_c, 'bldg_wood'))
BLDG_STONE = _ints(_table(_menus_c, 'bldg_stone'))
_mk = re.search(r'BK_MACHINE:\s*\*g = arg == MK_(\w+) \? (\d+) : (\d+)', _nocomment(_menus_c))
MACHINE_GOLD = {k: int(_mk.group(2)) if k == _mk.group(1).lower() else int(_mk.group(3)) for k in ('keg', 'press', 'jar')}
MACHINE_DAYS = dict(zip(('keg', 'press', 'jar'), [int(d) for d in re.findall(r'days = (\d+);', _nocomment(_farm_c))]))
TREE_GOLD = _lit(_menus_c, r'BK_TREE:\s*\*g = (\d+) \+ arg \* (\d+)', 2)
REPAIR = {k: _lit(_farm_c, r'repair\(slot, "' + s + r'", (\d+), (\d+), (\d+)\)', 3)
          for k, s in (('barn', 'the barn'), ('coop', 'the coop'), ('greenhouse', 'greenhouse'))}
DECOR = re.findall(r'\{"([^"]*)",\s*\w+,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}', _table(_src('decor.c'), 'decor'))
DECOR_GOLD = {d[0]: (int(d[3]), int(d[4]), int(d[5])) for d in DECOR}
WAGE_BASE = _ints(_table(_staff_c, 'wage_base'))
WAGE_PLOT = _ints(_table(_staff_c, 'wage_plot'))
BIZ_PRICE = _ints(_table(_biz_c, 'biz_price'))
BIZ_CAP = _ints(_table(_biz_c, 'cap_tab'))
BIZ_UP = _ints(_table(_biz_c, 'up_price'))
BIZ_TIER = _ints(_table(_biz_c, 'tier_price'))
BQ = _ints(_table(_biz_c, 'bq_base'))
PQ = _ints(_table(_biz_c, 'pq_base'))
INN_RATE = C['INN_RATE']
MGR_WAGE = _lit(_biz_c, r'#define MGR_WAGE\(lv\) \((\d+) \+ (\d+) \* \(lv\)\)', 2)
BIZ_NAME = re.findall(r'"([^"]*)"', _table(_biz_c, 'biz_name'))
PAN_ITEM = [[C.get(n.strip(), 0xFF) for n in row.split(',')] for row in re.findall(r'\{([^{}]*)\}', _table(_biz_c, 'pan_item'))]
PAN_ROLE = _rows(_table(_biz_c, 'pan_role'))
R_B, R_M, R_S, R_X = C['R_B'], C['R_M'], C['R_S'], C['R_X']
MARKET_PRICE, LOT_PRICE = C['MARKET_PRICE'], C['LOT_PRICE']
PTS_LEVEL = _ints(_table(_invest_c, 'pts_level'))
TPROJ = [tuple(r) for r in _rows(_table(_invest_c, 'tproj'))]  # gold wood stone need_p market
TRADER_MULT = C.get('TRADER_MULT') or _lit(_invest_c, r'items\[it\]\.price \* (\d+);')
BIZ_PCT = [C.get('BIZ_PCT_TIER', 8), C.get('BIZ_PCT_PROSP', 5), C.get('BIZ_PCT_MGR', 10), C.get('BIZ_PCT_INN', 10)]
GEM_STEP = _lit(_areas_c, r'while \(g && rnd\(\) < (\d+)\) g--')
VALLEY = []
for row in re.findall(r'\{\{([^}]*)\},\s*\{([^}]*)\},\s*(\d+)\}', _nocomment(_src('valley.c'))):
    its = [C.get(n.strip(), 0xFF) if n.strip() != 'NO_ITEM' else 0xFF for n in row[0].split(',')]
    VALLEY.append((its, _ints(row[1]), int(row[2])))
_dish_rows = re.findall(r'\{\{([^}]*)\},\s*\{([^}]*)\},\s*(\w+),\s*([\w|]+)\}', _table(_cook_c, 'dish'))
ANY_FISH = 0xFE
DISH = []
for its, ns, en, bf in _dish_rows:
    its = [ANY_FISH if n.strip() == 'ANY_FISH' else (0xFF if n.strip() == 'NO_ING' else C[n.strip()]) for n in its.split(',')]
    DISH.append((its, _ints(ns)))
MINE_ODDS = _rows(_table(_areas_c, 'odds'))
MINE_HARD = _ints(_table(_areas_c, 'hardness'))
GEMS = [C['I_GEM'] if n.strip() == 'I_AMETHYST' else C[n.strip()] for n in _table(_areas_c, 'gem_item').split(',') if n.strip()]
FEST_GOLD = _ints(_table(_src('festival.c'), 'prize_gold'))
GOAL_REWARD = _ints(_table(_src('goals.c'), 'rewards'))
COAT_PRICE = _ints(_table(_src('horse.c'), 'coat_price'))
FURN = re.findall(r'\{"([^"]*)",\s*\w+,\s*\d+,\s*\d+,\s*[\w|]+,\s*(\d+),', _table(_src('interior.c'), 'fdef'))
FURN_PRICE = {n: int(p) for n, p in FURN}
STYLE_PRICE = C['STYLE_PRICE']
LEGEND_SETTLES = 'rr == R_LEGEND ? R_RARE << 6' in _farm_c   # a legendary regrower turns rare after its first crop
FOOD_CAP = 1 if re.search(r'uint8_t ate;', _nocomment(_src('game.h'))) else 3   # full bars of energy food may restore a day
# land: what each region's plots are made of (world.c gen_plot mixes)
_mix_rows = re.findall(r'\{([^{}]*)\}', _table(_world_c, 'mix'))
MIX = []
for r in _mix_rows:
    parts = [p.strip() for p in r.split(',') if p.strip()]
    MIX.append([(int(parts[i]), C[parts[i + 1]]) for i in range(0, len(parts), 2)])
RG_MIX = [C[n.strip()] for n in _table(_world_c, 'rg_mix').split(',') if n.strip()]
RG_KIND = [C[n.strip()] for n in _table(_world_c, 'rg_kind').split(',') if n.strip()]
PLOT_RG = [C[n.strip()] for n in _table(_world_c, 'plot_rg').split(',') if n.strip()]
RG_NAME = re.findall(r'"([^"]*)"', _table(_world_c, 'rg_name'))

I = {k[2:]: v for k, v in C.items() if k.startswith('I_') and isinstance(v, int) and v < NUM_ITEMS}
SPRING, SUMMER, FALL, WINTER = range(4)
DAYS_SEASON, DAYS_YEAR = 28, 112


# ------------------------------------------------------------------ expected values of the game's dice
def qmult(gold, silver):
    """expected value multiple of a quality roll (item_value: silver +25%, gold +50%), odds out of 256"""
    return 1 + (silver * 0.25 + gold * 0.5) / 256


Q_COMMON = (qmult(6, 24), qmult(30, 70))                  # roll_quality: unfertilized, fertilized
Q_RARE = (qmult(20, 50), qmult(50, 80))
Q_LEGEND = (qmult(110, 110), qmult(160, 60))
Q_HAPPY = 1.5                                              # animal produce at happy > 200


def crop_q(rar, fert, hand=True):
    if not hand:   # the harvester rolls every item like a common crop; a legendary's third roll is as if fertilized
        return Q_COMMON[fert]
    return (Q_COMMON, Q_RARE, Q_LEGEND)[rar][fert]


def crop_yield(rar, hand=True):
    return (1, 2, 3.5)[rar] if hand else (1, 2, 3)[rar]


def harvest_value(crop, rar, fert, hand=True):
    it = crop - 1
    if hand:
        return PRICE[it] * crop_yield(rar) * crop_q(rar, fert)
    n = crop_yield(rar, False)
    v = PRICE[it] * (min(n, 2) * Q_COMMON[fert] + (1 if n == 3 else 0) * Q_COMMON[1])
    return v


def harvest_days(crop, plant_day, last_day):
    """the days (from plant_day, 0 = planted this morning) a tile planted today is harvested before last_day"""
    _, _, days, regrow = CROPS[crop]
    out, d = [], plant_day + days
    while d < last_day:
        out.append(d)
        if not regrow:
            break
        d += regrow
    return out


def tile_season_profit(crop, rar, fert=1, start=0, end=DAYS_SEASON, hand=True):
    """expected profit of one tile planted on day `start` of a season and replanted while it pays, to day `end`"""
    _, seed, days, regrow = CROPS[crop]
    cost = seed * RAR_MULT[rar]
    hv = harvest_value(crop, rar, fert, hand)
    hv2 = harvest_value(crop, 1 if (rar == 2 and LEGEND_SETTLES) else rar, fert, hand)
    total, d = 0.0, start
    while True:
        hs = harvest_days(crop, d, end)
        if not hs or hv + hv2 * (len(hs) - 1) <= cost:
            break
        total += hv + hv2 * (len(hs) - 1) - cost
        if regrow:
            break
        d = hs[-1]
    return total


# land: expected composition of a plot (gen_plot rolls pct() 0..99 against its region's mix)
CLEAR_TYPES = {C['C_WEEDS'], C['C_ROCK'], C['C_STUMP'], C['C_BOULDER'], C['C_LOG'], C['C_PINE'], C['C_BIRCH'], C['C_FORAGE'], C['C_SAPLING_WILD']}
FREE_TYPES = {C['C_GRASS'], C['C_GRASS2'], C['C_WILDFLOWER']}


def plot_cells(p):
    """expected count of each cell type on plot p"""
    rg = PLOT_RG[p]
    mix, out, lo = MIX[RG_MIX[rg]], defaultdict(float), 0
    for lim, c in mix:
        hi = min(lim, 100)
        if hi > lo:
            out[c] += 64 * (hi - lo) / 100
        lo = max(lo, hi)
    if RG_KIND[rg] == C['RK_ORCHARD']:   # trees every 3 cells (9 a plot), the rest from the mix
        for c in out:
            out[c] *= 55 / 64
        out[C['C_TREE']] += 9
    if RG_KIND[rg] in (C['RK_LAKE'],) and RG_NAME[rg] == 'Lakeside':
        out.clear(); out[C['C_WATER']] = 21; out[C['C_SAND']] = 12; out[C['C_GRASS']] = 23; out[C['C_PINE']] = 8
    return out


def clear_energy(c, tier_tool):
    """energy to turn a cell into grass: weeds with the sickle, rocks/boulders with the hammer, wood with the axe"""
    def strikes(need, tier):
        if tier >= 3:
            return 1
        if tier == 2 and need > 1:
            return need - 1
        return need
    if c == C['C_WEEDS']:
        return 1
    if c == C['C_FORAGE']:
        return 0
    if c == C['C_ROCK']:
        return 2 * strikes(2, tier_tool['hammer'])
    if c == C['C_BOULDER']:
        return 2 * strikes(4, max(1, tier_tool['hammer']))
    if c == C['C_STUMP']:
        return 2 * strikes(2, tier_tool['axe'])
    if c == C['C_LOG']:
        return 2 * strikes(4, max(1, tier_tool['axe']))
    if c in (C['C_PINE'], C['C_BIRCH']):
        return 2 * strikes(3, tier_tool['axe']) + 4   # then the wild sapling it leaves
    if c == C['C_SAPLING_WILD']:
        return 4
    return 0


def plot_profile(p, tiers):
    """(farmable cells, energy to clear them, wood, stone, fruit trees, water cells) of plot p"""
    cells = plot_cells(p)
    farm = sum(n for c, n in cells.items() if c in CLEAR_TYPES or c in FREE_TYPES)
    energy = sum(n * clear_energy(c, tiers) for c, n in cells.items() if c in CLEAR_TYPES)
    wood = cells[C['C_STUMP']] * 3 + cells[C['C_LOG']] * 12 + (cells[C['C_PINE']] + cells[C['C_BIRCH']]) * 9
    stone = cells[C['C_ROCK']] * 2 + cells[C['C_BOULDER']] * 10
    return farm * 0.85, energy * 0.85, wood, stone, cells[C['C_TREE']], cells[C['C_WATER']]


# the mine: expected gold per rock and energy per rock on a floor, by hammer tier
def mine_rock(floor, tier):
    band = 0 if floor <= 5 else 1 if floor <= 10 else 2 if floor <= 15 else 3
    odds = MINE_ODDS[band]
    kinds = [6, 4, 3, 2, 5]   # gem node, gold, iron, copper, coal (areas.c odds_kind)
    probs, lo = {}, 0
    for k, o in zip(kinds, odds):
        probs[k] = (o - lo) / 256
        lo = o
    probs[0] = (256 - lo) / 256
    item_of = {4: I['GOLD'], 3: I['IRON'], 2: I['COPPER'], 5: I['COAL']}
    g = min(4, (floor + 4) >> 2)
    gem = defaultdict(float)
    p = 1.0
    for k in range(g, 0, -1):          # each rarer gem less likely: while (g && rnd() < GEM_STEP) g--
        gem[GEMS[k]] += p * (1 - GEM_STEP / 256)
        p *= GEM_STEP / 256
    gem[GEMS[0]] += p
    gold, energy, items = 0.0, 0.0, defaultdict(float)
    for k, pr in probs.items():
        hard = MINE_HARD[k]
        hits = hard - tier if hard > tier + 1 else 1
        energy += pr * 2 * hits
        if k in item_of:
            items[item_of[k]] += pr * 1.5
        elif k == 6:
            for it, q in gem.items():
                items[it] += pr * q * qmult(0, 40)
        else:
            coal = 24 / 256
            items[I['COAL']] += pr * coal
            items[I['GEM']] += pr * (1 - coal) * (2 + band) / 256
            items['stone'] += pr * 1.5
    gold = sum(PRICE[it] * n for it, n in items.items() if it != 'stone')
    return gold, energy, items


def fish_cast(tier, lucky=False, ocean=False):
    """expected sale value of one cast at a lake by day (or the beach), success ~80% for a normal player"""
    t = tier + (2 if lucky else 0)
    if ocean:
        r_tuna = (12 + t * 8) / 256 * 0.5   # summer/winter only
        v = r_tuna * PRICE[I['TUNA']] + (120 / 256) * PRICE[I['SARDINE']] + (1 - r_tuna - 120 / 256) * PRICE[I['FLOUNDER']]
    else:
        pk = (3 + t * 3) / 256
        v = pk * PRICE[I['KOI']] + (1 - pk) * (PRICE[I['BASS']] + PRICE[I['CARP']]) / 2
    return v * 0.8


# businesses: biz.c run_one / biz_overnight
def biz_units(level, tier, mgr):
    return BIZ_CAP[level] + tier + (4 if mgr else 0)


def biz_mult(tier, P, mgr, inn, b):
    return (100 + BIZ_PCT[0] * tier + BIZ_PCT[1] * P + (BIZ_PCT[2] if mgr else 0) + (BIZ_PCT[3] if inn and b != C['BIZ_INN'] else 0)) / 100


def run_one(b, pantry, level, tier, mgr, items_of=None):
    """biz.c run_one on a pantry {slot: count}; returns (value before the multiplier, premium, basic). Mutates pantry."""
    units = biz_units(level, tier, mgr)
    bq, pq = BQ[b] + level, PQ[b] + level
    item = items_of or PAN_ITEM[b]
    role = PAN_ROLE[b] if b != C['BIZ_RESTAURANT'] else [(R_M | R_S) if (it != 0xFF and it >= I['SALAD']) else (R_X if it != 0xFF else 0) for it in item]

    def best(r, skip=None):
        k_best = None
        for k in range(8):
            if k == skip or not pantry.get(k) or not (role[k] & r):
                continue
            if k_best is None or PRICE[item[k]] > PRICE[item[k_best]]:
                k_best = k
        return k_best
    nb = sum(1 for k in range(8) if role[k] & R_B)
    v, prem, basic = 0, 0, 0
    while units:
        if b == C['BIZ_RESTAURANT']:
            m, m2 = best(R_M), best(R_X)
            if units >= 2 and m is not None and m2 is not None:
                pantry[m] -= 1; pantry[m2] -= 1
                v += (PRICE[item[m]] + PRICE[item[m2]]) * pq // 4
                units -= 2; prem += 1
                continue
        elif nb:
            ok = units > nb and all(pantry.get(k) for k in range(8) if role[k] & R_B)
            m = best(R_M)
            if ok and m is not None:
                s = PRICE[item[m]]
                pantry[m] -= 1
                for k in range(8):
                    if role[k] & R_B:
                        pantry[k] -= 1; s += PRICE[item[k]]
                v += s * pq // 4
                units -= nb + 1; prem += 1
                continue
        else:
            m = best(R_M)
            m2 = best(R_M, m) if m is not None else None
            if units >= 2 and m is not None and m2 is not None:
                pantry[m] -= 1; pantry[m2] -= 1
                v += (PRICE[item[m]] + PRICE[item[m2]]) * pq // 4
                units -= 2; prem += 1
                continue
        m = best(R_S)
        if m is None:
            break
        pantry[m] -= 1
        v += PRICE[item[m]] * bq // 4
        units -= 1; basic += 1
    return v, prem, basic


def prosperity(pts):
    p = 0
    while p < 5 and pts >= PTS_LEVEL[p]:
        p += 1
    return p


def wage(role, plots, soil=0, crops=0, animals=0):
    w = WAGE_BASE[role] + WAGE_PLOT[role] * plots
    if role == 0:
        w += soil >> 1
    elif role == 1:
        w += crops << 1
    elif role == 2:
        w += 12 * animals
    return w


# ------------------------------------------------------------------ the simulation
# Time costs in seconds of real play (default clock: 840 s from 6am to 2am). A normal player spends about a quarter
# of the day walking between jobs, reading and in menus, so the budget for the activities below is DAY_SECONDS.
DAY_SECONDS = 840 * 0.72
TIME_WATER = 0.6        # one swing of the can (it covers 1/3/5/9 tiles by tier)
TIME_TILL = 0.6
TIME_PLANT = 0.3        # holding A while walking plants a tile a step
TIME_HARVEST = 0.9      # walk + A + the pose
TIME_CLEAR = 0.35       # per energy point spent clearing (a strike is 2 energy)
TIME_ROCK = 1.6         # walk to the next mine rock and break it
TIME_CAST = 9.0         # fishing: cast, wait, reel
TIME_ANIMAL = 2.5       # feed + collect + pet, by hand
TIME_MACHINE = 3.0
TIME_TREE = 1.0
TIME_TOWN = 70.0        # a trip to town with some shopping and stocking
P_RAIN = [50 / 256, 70 / 256, 50 / 256, 0.0]   # tomorrow rains (summer storms water too); snow does not water
AREA = [1, 3, 5, 9]     # hoe / can tiles per swing by tier
FIELD_CAP = [60, 110, 160, 160, 220, 300, 400, 400, 500, 600, 700, 700]   # tiles a normal player keeps planted, season by season (tending takes real time)
DILIGENCE = 0.9         # a normal player doesn't use every last point of energy
FISH_ITEMS = [I['MINNOW'], I['SARDINE'], I['CARP'], I['BASS'], I['TROUT'], I['FLOUNDER'], I['CATFISH'], I['SQUID'], I['SNAPPER'], I['TUNA'], I['KOI']]
TREE_RIPE = C['TREE_RIPE_DAYS']
KEEP = {I['COPPER']: 14, I['IRON']: 10, I['GOLD']: 8, I['COAL']: 10, I['TOPAZ']: 1, I['DIAMOND']: 1, I['GEM']: 1,
        I['KOI']: 1, I['TUNA']: 1, I['BASS']: 2, I['TROUT']: 2, I['CATFISH']: 2, I['CARP']: 3}


def season_of(day):
    return (day // DAYS_SEASON) % 4


def tree_yield(age):
    return 2 if age < 16 else 3 if age < 22 else 4 if age < 28 else 5


def tree_home(kind, season):
    return season == (FALL if kind == 0 else SPRING if kind == 3 else SUMMER)


TREE_FRUIT = [I['APPLE'], I['PEACH'], I['ORANGE'], I['CHERRY']]
ANIMAL_ITEM = [I['MILK'], I['EGG'], I['WOOL']]


class Farm:
    def __init__(self, strat):
        self.s = strat
        self.day = 0
        self.gold = 500
        self.earned = 0
        self.energy_max = 100
        self.tier = {'hoe': 0, 'can': 0, 'axe': 0, 'hammer': 0, 'rod': 0}
        self.house = 0
        self.plots = set()
        self.uncleared = {}       # plot -> [cells left to clear, energy a cell, wood a cell, stone a cell]
        self.free = 0.0           # cleared cells not yet tilled
        self.trees = []           # [kind, age, count, fruit_days]
        for p in (27, 28, 35, 36):
            self.add_plot(p)
        self.free -= 45           # the house, coop, barn, greenhouse, bin, well, paths
        self.tilled = 0.0
        self.sprinkled = 0.0
        self.batches = []         # [crop, rar, tiles, next_ripe_day]
        self.inv = defaultdict(float)
        self.inv_v = defaultdict(float)
        self.got = defaultdict(float)   # lifetime count of each item obtained (valley donations need them)
        self.animals = []         # [kind, breed, happy, age]
        self.baby_acc = [0.0, 0.0]   # barn, coop
        self.bld = defaultdict(int)
        self.machines = {'keg': 0, 'press': 0, 'jar': 0}
        self.biz = {}
        self.market = False
        self.lots = 0
        self.tproj = set()
        self.valley = set()
        self.staff = set()
        self.wood, self.stone = 20.0, 0.0
        self.mine_deep = 0
        self.recipes = {0, 1}
        self.fert = False
        self.log = []
        self.days = []            # per day: income by source and state
        self.spent = defaultdict(float)
        self.casts = 0.0

    # ---------------------------------------------------------- helpers
    @property
    def season(self):
        return season_of(self.day)

    @property
    def year(self):
        return self.day // DAYS_YEAR + 1

    def add_plot(self, p):
        cells = plot_cells(p)
        farm, energy, wood, stone, trees, water = plot_profile(p, self.tier)
        free = sum(n for c, n in cells.items() if c in FREE_TYPES) * 0.85
        clutter = farm - free
        self.free += free
        self.plots.add(p)
        if clutter > 0.5:
            self.uncleared[p] = [clutter, energy / clutter, wood / clutter, stone / clutter]
        if trees:
            kinds = (0, 1) if RG_NAME[PLOT_RG[p]] == 'Apple Hills' else (2, 3) if RG_NAME[PLOT_RG[p]] == 'Cherry Slope' else (0, 1, 2, 3)
            for k in kinds:
                self.trees.append([k, 30, trees / len(kinds), 0])

    def say(self, what):
        self.log.append((self.day, what))

    def wages_night(self):
        w = sum(self.wage(r) for r in self.staff)
        w += sum(MGR_WAGE[0] + MGR_WAGE[1] * z['level'] for z in self.biz.values() if z['mgr'])
        return w

    def reserve(self):
        seeds = 40 * max(0.0, self.tilled - self.planted())   # seed money for the empty soil
        return self.s.get('reserve', 200) + 2 * self.wages_night() + seeds

    def afford(self, g, w=0, s=0):
        """gold left after the reserve, buying any missing wood (G10) / stone (G15) from June"""
        extra = max(0, w - self.wood) * 10 + max(0, s - self.stone) * 15
        return self.gold - g - extra >= self.reserve()

    def pay(self, what, g, w=0, s=0, cat='build'):
        buy_w, buy_s = max(0, w - self.wood), max(0, s - self.stone)
        self.wood += buy_w; self.stone += buy_s
        g += buy_w * 10 + buy_s * 15
        self.gold -= g; self.wood -= w; self.stone -= s
        self.spent[cat] += g
        self.say(f'{what} (G{int(g)})')

    def planted(self):
        return sum(b[2] for b in self.batches)

    def wage(self, role):
        return wage(role, len(self.plots), int(self.tilled), int(self.planted()), len(self.animals))

    def prosperity_pts(self):
        n = sum(2 if b < 4 else 3 for b in self.biz) + sum(z['tier'] for z in self.biz.values())
        return n + (2 if self.market else 0) + 4 * len(self.tproj)

    def P(self):
        return prosperity(self.prosperity_pts())

    def add(self, it, n, value):
        self.inv[it] += n
        self.inv_v[it] += value
        self.got[it] += n

    def take(self, it, n):
        have = self.inv[it]
        n = max(0.0, min(n, have))
        if have > 0:
            self.inv_v[it] *= (have - n) / have
        self.inv[it] = have - n
        return n

    def legend_ok(self):
        return self.year > 1 or self.earned >= LEGEND_EARNED

    # ---------------------------------------------------------- crops
    def best_crop(self, budget_per_tile, days_left, hand):
        best = None
        rars = [0] + ([1] if self.s.get('rare', True) else []) + ([2] if self.legend_ok() and self.s.get('legend', True) else [])
        green = self.bld['greenhouse']
        for c in range(1, NUM_CROPS + 1):
            if CROPS[c][0] != self.season and not green:
                continue
            for r in rars:
                cost = CROPS[c][1] * RAR_MULT[r]
                if cost > budget_per_tile:
                    continue
                end = days_left if not green else (56 if CROPS[c][3] else days_left)
                hs = harvest_days(c, 0, end)
                if not hs:
                    continue
                f1 = 1 if self.fert else 0
                later = 1 if (r == 2 and LEGEND_SETTLES) else r
                v = harvest_value(c, r, f1, hand) + harvest_value(c, later, f1, hand) * (len(hs) - 1) - cost
                score = v / (1 + cost / max(1.0, budget_per_tile) * self.s.get('thrift', 0.6))
                if v > 0 and (best is None or score > best[0]):
                    best = (score, c, r, cost, v)
        return best

    def plant(self, t):
        empty = min(self.tilled - self.planted(), max(0.0, t) / TIME_PLANT)
        if empty < 1:
            return 0
        days_left = DAYS_SEASON - self.day % DAYS_SEASON
        budget = max(0.0, self.gold - self.s.get('reserve', 200) - 2 * self.wages_night())
        hand = 1 not in self.staff
        n_total = 0
        for it, n in zip(*VALLEY[0][:2]):
            c = it + 1
            if 0 not in self.valley and it != 0xFF and CROPS[c][0] == self.season and self.inv[it] < n \
                    and not any(b[0] == c for b in self.batches) and budget > 4 * CROPS[c][1]:
                self.batches.append([c, 0, 4, self.day + CROPS[c][2]])
                self.gold -= 4 * CROPS[c][1]; budget -= 4 * CROPS[c][1]; empty -= 4; n_total += 4
        while empty >= 1:
            pick = self.best_crop(budget / empty, days_left, hand) or self.best_crop(budget, days_left, hand)
            if not pick:
                break
            _, c, r, cost, _ = pick
            n = min(empty, budget // cost)
            if n < 1:
                break
            self.gold -= n * cost; budget -= n * cost
            self.spent['seeds'] += n * cost
            self.batches.append([c, r, n, self.day + CROPS[c][2]])
            empty -= n; n_total += n
        return n_total

    def pick(self, b, hand):
        c, r, n, _ = b
        v = n * harvest_value(c, r, 1 if self.fert else 0, hand)
        if hand:
            self.add(c - 1, n * crop_yield(r), v)
        else:
            keep = max(0.0, self.valley_keep(c - 1) - self.inv[c - 1])
            if keep:
                self.add(c - 1, keep, keep * PRICE[c - 1])
                v -= keep * PRICE[c - 1]
            self.got[c - 1] += n * crop_yield(r, False)
            self.ship += v
            self.inc['crops'] += v
        if CROPS[c][3]:
            b[3] = b[3] + CROPS[c][3]
            if r == 2 and LEGEND_SETTLES:
                b[1] = 1
        else:
            b[2] = 0

    def harvest(self, time_left):
        if 1 in self.staff:
            return 0.0
        used = 0.0
        for b in self.batches:
            if b[3] <= self.day and b[2] > 0 and used + b[2] * TIME_HARVEST <= time_left:
                used += b[2] * TIME_HARVEST
                b[3] = max(b[3], self.day)
                self.pick(b, True)
        self.batches = [b for b in self.batches if b[2] > 0]
        return used

    def night_harvester(self):
        if 1 not in self.staff:
            return
        for b in self.batches:
            if b[3] <= self.day + 1 and b[2] > 0:   # ripe by morning: picked and shipped tonight
                b[3] = max(b[3], self.day + 1)
                self.pick(b, False)
        self.batches = [b for b in self.batches if b[2] > 0]

    # ---------------------------------------------------------- businesses
    def stock_and_run(self):
        P = self.P()
        inn = C['BIZ_INN'] in self.biz and self.biz[C['BIZ_INN']]['open'] <= self.day
        total = 0
        order = sorted(self.biz, key=lambda b: (b in (2, 7, 0), b), reverse=True)   # the ones that need eggs/milk first
        for b in order:
            z = self.biz[b]
            if z['open'] > self.day:
                continue
            units = biz_units(z['level'], z['tier'], z['mgr'])
            if b == C['BIZ_RESTAURANT']:
                self.cook_for_restaurant(z, units)
                items = z['items']
            else:
                items = PAN_ITEM[b]
                for k, it in enumerate(items):
                    want = units * 2 - z['pantry'].get(k, 0)
                    if want <= 0:
                        continue
                    if self.s.get('trader') and it in (I['EGG'], I['MILK'], I['HONEY']) and P >= 2:
                        price = PRICE[it] * TRADER_MULT
                        q = int(min(want, max(0, (self.gold - self.reserve()) // price)))
                        self.gold -= q * price; self.inc['trader goods'] -= q * price
                        z['pantry'][k] = z['pantry'].get(k, 0) + q
                        continue
                    if self.avail(it) >= 1:
                        q = int(self.take(it, min(want, int(self.avail(it)))))
                        self.inc['stocked (ship value)'] -= q * PRICE[it]
                        z['pantry'][k] = z['pantry'].get(k, 0) + q
            if z['mgr']:
                w = MGR_WAGE[0] + MGR_WAGE[1] * z['level']
                self.gold -= w; self.inc['wages'] -= w
            v, prem, basic = run_one(b, z['pantry'], z['level'], z['tier'], z['mgr'], items)
            if b == C['BIZ_INN']:
                v += (4 + 2 * z['level'] + z['tier'] + P) * INN_RATE
            v = min(int(v * biz_mult(z['tier'], P, z['mgr'], inn, b)), 65535)
            z['last'] = v
            z['units_used'] = prem + basic
            total += v
        self.inc['business'] += total
        return total

    def cook_for_restaurant(self, z, units):
        """the restaurant serves dishes with a fresh side: cook the dishes the bag allows, the most valuable first"""
        if 'items' not in z:
            z['items'] = [0xFF] * 8
        want = units // 2 - sum(z['pantry'].get(k, 0) for k in range(8) if z['items'][k] != 0xFF and z['items'][k] >= I['SALAD'])
        order = sorted(self.recipes, key=lambda d: -(PRICE[I['SALAD'] + d] - self.dish_cost(d)))
        if self.s.get('cheap_dishes'):
            order = sorted(self.recipes, key=lambda d: self.dish_cost(d))
        for d in order:
            if want <= 0:
                break
            its, ns = DISH[d]
            can = 99
            for it, n in zip(its, ns):
                if it != 0xFF:
                    have = sum(self.avail(f) for f in FISH_ITEMS) if it == ANY_FISH else self.avail(it)
                    can = min(can, int(max(0.0, have) // n))
            can = min(can, want)
            if can <= 0:
                continue
            dish_it = I['SALAD'] + d
            if dish_it not in z['items']:
                if 0xFF not in z['items']:
                    continue
                z['items'][z['items'].index(0xFF)] = dish_it
            for it, n in zip(its, ns):
                if it == ANY_FISH:
                    left = can * n
                    for f in FISH_ITEMS:
                        got = self.take(f, left)
                        self.inc['stocked (ship value)'] -= got * PRICE[f]
                        left -= got
                elif it != 0xFF:
                    self.inc['stocked (ship value)'] -= PRICE[it] * can * n
                    self.take(it, can * n)
            k = z['items'].index(dish_it)
            z['pantry'][k] = z['pantry'].get(k, 0) + can
            want -= can
        sides = [it for it in range(I['CHERRY'] + 1) if self.avail(it) >= 1]
        if sides:
            side = min(sides, key=lambda it: PRICE[it])
            if side not in z['items'] and 0xFF in z['items']:
                z['items'][z['items'].index(0xFF)] = side
            if side in z['items']:
                k = z['items'].index(side)
                q = int(self.take(side, min(units, self.inv[side])))
                self.inc['stocked (ship value)'] -= q * PRICE[side]
                z['pantry'][k] = z['pantry'].get(k, 0) + q
        for k in range(8):
            if not z['pantry'].get(k):
                z['items'][k] = 0xFF

    def dish_cost(self, d):
        its, ns = DISH[d]
        return sum((PRICE[I['MINNOW']] if it == ANY_FISH else PRICE[it]) * n for it, n in zip(its, ns) if it != 0xFF)

    # ---------------------------------------------------------- one day
    def step(self):
        s, se = self.s, self.season
        self.inc = defaultdict(float)
        self.ship = 0.0
        energy = self.energy_max * DILIGENCE
        t = DAY_SECONDS - TIME_TOWN
        if self.day % DAYS_SEASON == 0 and self.day:
            if not self.bld['greenhouse']:
                self.batches = []
        self.shop()
        # crops: plant, harvest, water
        t -= self.plant(t * 0.5) * TIME_PLANT
        t -= self.harvest(t * 0.8)
        need = max(0.0, self.planted() - (1e9 if 0 in self.staff else self.sprinkled))
        uses = math.ceil(need / AREA[self.tier['can']]) * (1 - P_RAIN[se])
        if uses > energy:   # can't water it all: the dry share doesn't grow today
            short = 1 - energy / uses
            for b in self.batches:
                b[3] += short
            uses = energy
        energy -= uses; t -= uses * TIME_WATER
        # animals: hay, feeding, produce (by hand or by the rancher overnight)
        self.animals_day()
        if 2 not in self.staff:
            t -= len(self.animals) * TIME_ANIMAL
        if self.bld['silo'] and s.get('cut_hay', True) and self.animals:
            cut = min(energy * 0.3, len(self.animals))
            energy -= cut; t -= cut * 0.6
            self.hay_cut = cut
        # fruit trees
        for tr in self.trees:
            tr[1] += 1
            if tr[1] >= 10 and se != WINTER:
                tr[3] += 1
                if tr[3] >= TREE_RIPE:
                    tr[3] = 0
                    n = tr[2] * (tree_yield(tr[1]) + (2 if tree_home(tr[0], se) else 0))
                    q = Q_COMMON[1] if tree_home(tr[0], se) else Q_COMMON[0]
                    self.add(TREE_FRUIT[tr[0]], n, n * PRICE[TREE_FRUIT[tr[0]]] * q)
                    t -= tr[2] * TIME_TREE
        # machines
        self.machines_day()
        t -= sum(self.machines.values()) * TIME_MACHINE / 2
        # the strategy's use of the rest of the day
        energy, t = self.expand(energy, t)
        self.side_jobs(energy, t)
        # businesses (stocked in town, sell overnight), then ship the rest
        self.stock_and_run()
        self.ship_bag()
        # night: wages, the harvester, money
        wages = sum(self.wage(r) for r in self.staff)
        self.gold -= wages
        self.inc['wages'] -= wages
        self.night_harvester()
        income = self.ship + self.inc['business']
        self.gold += income
        self.earned += income
        self.days.append(dict(day=self.day, gold=self.gold, earned=self.earned, inc=dict(self.inc), plots=len(self.plots),
                              tilled=self.tilled, biz=len(self.biz), P=self.P(), valley=len(self.valley),
                              tproj=len(self.tproj), staff=len(self.staff), animals=len(self.animals),
                              trees=sum(tr[2] for tr in self.trees), tiers=dict(self.tier), deep=self.mine_deep,
                              energy=self.energy_max))
        self.day += 1

    def ship_bag(self):
        for it in list(self.inv):
            keep = KEEP.get(it, 0) + self.valley_keep(it)
            n = self.inv[it] - keep
            if n > 0.01:
                v = self.inv_v[it] * n / self.inv[it]
                self.take(it, n)
                self.ship += v
                cat = 'crops' if it < I['APPLE'] else 'fruit' if it <= I['CHERRY'] else 'animal' if it in (I['MILK'], I['EGG'], I['WOOL'], I['HONEY'], I['CHEESE']) else 'mine' if IS_MIN(it) else 'fish' if it in FISH_ITEMS else 'goods'
                self.inc[cat] += v

    def valley_keep(self, it):
        for p, (its, ns, g) in enumerate(VALLEY):
            if p not in self.valley and it in its:
                return ns[its.index(it)]
        return 0

    # ---------------------------------------------------------- animals and machines
    def animals_day(self):
        n = len(self.animals)
        if not n:
            return
        cut = getattr(self, 'hay_cut', 0)
        buy = max(0, n - cut)
        self.gold -= buy * HAY_PRICE
        self.inc['hay'] -= buy * HAY_PRICE
        self.hay_cut = 0
        rancher = 2 in self.staff
        for a in self.animals:
            kind, breed, happy, age = a
            if age >= C['ADULT_AGE']:
                q = 1.5 if breed == 2 else (1.5 if happy > 200 else 1.25 if happy > 120 else 1.0)
                cnt = 2 if breed else 1
                if kind == C['A_SHEEP']:
                    cnt /= C['WOOL_DAYS']
                it = ANIMAL_ITEM[kind]
                self.add(it, cnt, cnt * PRICE[it] * q)
            a[2] = min(235, happy + 20) + (0 if rancher else 10)
            a[3] += 1
        # breeding: a happy pair in a building with room has a baby on 40 nights in 256
        for home, kinds, cap in ((0, (C['A_COW'], C['A_SHEEP']), C['BARN_CAP'] * max(1, self.bld['barn'])), (1, (C['A_HEN'],), 6 * max(1, self.bld['coop']))):
            here = [a for a in self.animals if a[0] in kinds]
            if len(here) >= cap or len(self.animals) >= C['MAX_ANIMALS']:
                continue
            for k in kinds:
                pair = [a for a in here if a[0] == k and a[3] >= C['ADULT_AGE'] and a[2] >= 180]
                if len(pair) >= 2 and self.s.get('breed', True):
                    self.baby_acc[home] += 40 / 256
                    if self.baby_acc[home] >= 1:
                        self.baby_acc[home] -= 1
                        br = max(a[1] for a in pair)
                        self.animals.append([k, br, 150, 0])
                        self.say(f'baby {"cow hen sheep".split()[k]} (breed {br})')
                    break

    def machines_day(self):
        m = self.machines
        if m['press']:
            n = min(m['press'], self.avail(I['MILK']))
            if n >= 1:
                self.take(I['MILK'], n)
                self.add(I['CHEESE'], n / MACHINE_DAYS['press'], n / MACHINE_DAYS['press'] * PRICE[I['CHEESE']])
        for kind, fruit_out, veg_out in (('keg', I['CIDER'], I['JUICE']), ('jar', I['JAM'], I['PICKLES'])):
            if not m[kind]:
                continue
            per_day = m[kind] / MACHINE_DAYS[kind]
            # the cheapest produce in the bag goes in
            for it in sorted(range(I['CHERRY'] + 1), key=lambda i: PRICE[i]):
                if per_day <= 0:
                    break
                out = fruit_out if it >= I['APPLE'] else veg_out
                if PRICE[out] <= PRICE[it] * 1.1 or self.avail(it) < 1:
                    continue
                n = min(per_day, self.avail(it))
                self.take(it, n)
                self.add(out, n, n * PRICE[out])
                per_day -= n

    # ---------------------------------------------------------- energy: land, the mine, fishing
    def expand(self, energy, t):
        """clear and till more land while the farm can water and seed it"""
        s = self.s
        share = s.get('expand', 0.6)
        budget_e = energy * share
        free_water = 1e9 if 0 in self.staff else self.sprinkled
        water_tiles = free_water + self.energy_max * DILIGENCE * s.get('water_share', 0.45) * AREA[self.tier['can']]
        seed_money = max(0.0, self.gold - self.reserve()) / 60.0
        target = min(water_tiles, self.tilled + seed_money, s.get('max_tiles', 1e9), FIELD_CAP[min(11, self.day // DAYS_SEASON)] * s.get('field_scale', 1.0))
        want = max(0.0, target - self.tilled)
        if want < 1:
            return energy, t
        # clear clutter if we're out of free cleared cells
        while self.free < want and budget_e > 1 and self.uncleared:
            p = next(iter(self.uncleared))
            cells, epc, wpc, spc = self.uncleared[p]
            n = min(cells, budget_e / max(0.5, epc), want - self.free + 4)
            budget_e -= n * epc; energy -= n * epc; t -= n * epc * TIME_CLEAR
            self.free += n; self.wood += n * wpc; self.stone += n * spc
            self.uncleared[p][0] -= n
            if self.uncleared[p][0] < 0.5:
                del self.uncleared[p]
        n = min(want, self.free, budget_e / 2 * AREA[self.tier['hoe']])
        if n > 0:
            e = math.ceil(n / AREA[self.tier['hoe']]) * 2
            energy -= e; t -= e / 2 * TIME_TILL
            self.free -= n; self.tilled += n
        return energy, t

    def side_jobs(self, energy, t):
        s = self.s
        need_ore = self.next_tool_ore()
        mine_e = energy * s.get('mine', 0.0)
        fish_e = energy * s.get('fish', 0.0)
        rest = energy - mine_e - fish_e
        # goals pull the leftover energy: ore for the next tool, the valley's mine and fish goods
        if need_ore or self.wants_mine():
            mine_e += rest * 0.7; rest *= 0.3
        if self.wants_fish():
            fish_e += rest * 0.7; rest *= 0.3
        if s.get('cafe') and self.mine_deep >= 10 and self.gold > 5000:   # feasts at Ada's to keep mining (one bar each)
            for _ in range(FOOD_CAP):
                self.gold -= CAFE_PRICE[2]; self.inc['cafe'] -= CAFE_PRICE[2]
                mine_e += self.energy_max
        if mine_e > 1:
            t = self.mine(mine_e, t)
        if fish_e > 4:
            casts = min(fish_e / 4, max(0.0, t) / TIME_CAST)
            if casts > 0:
                self.casts += casts
                ocean = self.season in (SUMMER, WINTER) and self.inv[I['TUNA']] < 1 and 2 not in self.valley
                v = fish_cast(self.tier['rod'], ocean=ocean)
                pk = (3 + self.tier['rod'] * 3) / 256 * 0.8
                if ocean:
                    self.add(I['TUNA'], casts * (12 + self.tier['rod'] * 8) / 256 * 0.4, 0)
                    self.add(I['FLOUNDER'], casts * 0.5, casts * v)
                else:
                    self.add(I['KOI'], casts * pk, casts * pk * PRICE[I['KOI']])
                    for f in (I['BASS'], I['CARP'], I['CATFISH'], I['TROUT']):
                        self.add(f, casts * (1 - pk) / 4 * 0.8, casts * (1 - pk) / 4 * 0.8 * PRICE[f])

    def mine(self, e, t):
        lift = int(self.mine_deep) // 5 * 5
        floor = max(1, lift)
        g, epr, items = mine_rock(floor, self.tier['hammer'])
        rocks = min(e / epr, max(0.0, t) / TIME_ROCK)
        if rocks <= 0:
            return t
        if self.mine_deep < 20 and self.s.get('descend', True):
            self.mine_deep = min(20, self.mine_deep + rocks / 20)   # about 20 rocks broken to find each ladder
        for it, n in items.items():
            if it == 'stone':
                self.stone += n * rocks
            else:
                self.add(it, n * rocks, n * rocks * PRICE[it])
        return t - rocks * TIME_ROCK

    def next_tool_ore(self):
        for tool in self.s.get('tools', ()):
            t = self.tier[tool]
            if t < 3:
                ore = [I['COPPER'], I['IRON'], I['GOLD']][t]
                return ore if self.inv[ore] < 5 else None
        return None

    def avail(self, it):
        return self.inv[it] - self.valley_keep(it)

    def wants_mine(self):
        v = self.valley
        return (3 not in v and (self.mine_deep < 6 or self.inv[I['TOPAZ']] < 1 or self.inv[I['GOLD']] < 3)) or \
               (5 not in v and len(v) >= 3 and (self.mine_deep < 16 or self.inv[I['DIAMOND']] < 1))

    def wants_fish(self):
        v = self.valley
        return (2 not in v and (self.inv[I['TUNA']] < 1 or self.inv[I['CATFISH']] < 2)) or \
               (5 not in v and len(v) >= 3 and self.inv[I['KOI']] < 1)

    def next_valley(self):
        for p in self.s.get('valley_order', range(6)):
            if p not in self.valley:
                return p
        return None

    # ---------------------------------------------------------- shopping: the strategy's wish list, in order
    def avg_income(self, n=7):
        recent = self.days[-n:]
        if not recent:
            return 0.0
        return (recent[-1]['earned'] - (recent[0]['earned'] if len(recent) > 1 else 0)) / max(1, len(recent) - 1 or 1)

    def offer(self, key):
        """the next concrete purchase for a wish-list key: (label, gold, wood, stone, apply) or None"""
        kind, _, arg = key.partition(':')
        a = arg.split(':') if arg else []
        b = self.bld
        if kind in ('coop', 'barn', 'greenhouse') and not b[kind]:
            g, w, st = REPAIR[kind]

            def f():
                b[kind] = 1
            return (f'repair the {kind}', g, w, st, f)
        if kind == 'silo' and not b['silo']:
            return ('silo', BLDG_GOLD[C['B_SILO']], BLDG_WOOD[C['B_SILO']], BLDG_STONE[C['B_SILO']], lambda: b.__setitem__('silo', 1))
        if kind == 'house' and self.house < 2:
            g = HOUSE_GOLD[self.house]
            w, st = (60, 0) if self.house == 0 else (120, 80)

            def f():
                self.house += 1; self.energy_max += HOUSE_ENERGY
            return (f'house level {self.house + 1}', g, w, st, f)
        if kind == 'fridge' and not b['fridge']:
            return ('fridge', FURN_PRICE['Fridge'], 0, 0, lambda: b.__setitem__('fridge', 1))
        if kind in ('hens', 'cows', 'sheep'):
            k = {'hens': C['A_HEN'], 'cows': C['A_COW'], 'sheep': C['A_SHEEP']}[kind]
            want, br = int(a[0]), int(a[1])
            if br == 2 and not self.legend_ok():
                br = 1
            home = 'coop' if k == C['A_HEN'] else 'barn'
            if not b[home]:
                return None
            have = sum(1 for x in self.animals if x[0] == k)
            room = (6 if home == 'coop' else C['BARN_CAP']) * b[home] - sum(1 for x in self.animals if (x[0] == C['A_HEN']) == (home == 'coop'))
            if have >= want or room <= 0 or len(self.animals) >= C['MAX_ANIMALS']:
                return None
            return (f'{kind[:-1] if kind != "sheep" else "sheep"} breed {br}', BREED_PRICE[k][br], 0, 0, lambda: self.animals.append([k, br, 100, C['ADULT_AGE']]))
        if kind == 'tool':
            t = self.tier[arg]
            ore = [I['COPPER'], I['IRON'], I['GOLD']][t] if t < 3 else None
            if t >= 3 or self.inv[ore] < 5:
                return None

            def f():
                self.take(ore, 5); self.tier[arg] += 1
            return (f'{arg} tier {t + 1}', TIER_GOLD[t], 0, 0, f)
        if kind == 'sprinklers':
            need = self.tilled - self.sprinkled
            if need < 8 or 0 in self.staff:
                return None
            sg, _, ss = DECOR_GOLD['Sprinkler']
            qg, _, qs = DECOR_GOLD['Big Sprinkler']
            big = qg / 24 < sg / 8 or self.s.get('big_spr')
            g, st, cov = (qg, qs, 24) if big else (sg, ss, 8)
            n = max(1, min(int(need // cov), int((self.gold - self.reserve()) // (g + st * 15)) if self.gold > self.reserve() else 1, 10))

            def f():
                self.sprinkled += n * cov
            return (f'{n} {"big " if big else ""}sprinklers', n * g, 0, n * st, f)
        if kind == 'fert' and not self.fert and self.tilled > 20:
            g = int(self.tilled * FERT_PRICE)
            return (f'fertilize {int(self.tilled)} tiles', g, 0, 0, lambda: setattr(self, 'fert', True))
        if kind in ('plot', 'plot!'):
            if len(self.plots) >= NUM_PLOTS or (kind == 'plot' and self.free + sum(u[0] for u in self.uncleared.values()) > self.s.get('land_low', 40)):
                return None
            cands = [p for p in range(NUM_PLOTS) if p not in self.plots]
            p = min(cands, key=lambda p: PLOT_PRICE[p] / max(8.0, plot_profile(p, self.tier)[0] + 6 * plot_profile(p, self.tier)[4]))
            return (f'plot {p} {RG_NAME[PLOT_RG[p]]}', PLOT_PRICE[p], 0, 0, lambda: self.add_plot(p))
        if kind in ('press', 'jar', 'keg'):
            if self.machines[kind] >= int(a[0]):
                return None
            st = 10 if kind == 'press' else 0
            if kind == 'press' and not any(x[0] == C['A_COW'] for x in self.animals):
                return None

            def f():
                self.machines[kind] += 1
            return (kind, MACHINE_GOLD[kind], 20, st, f)
        if kind == 'trees':
            have = sum(tr[2] for tr in self.trees if tr[1] < 1000)
            if have >= int(a[0]) + self.orchard_trees() or self.free < 4:
                return None
            k = int(have) % 4
            g = TREE_GOLD[0] + TREE_GOLD[1] * k

            def f():
                self.trees.append([k, 0, 1, 0]); self.free -= 1
            return (f'sapling {k}', g, 0, 0, f)
        if kind == 'biz':
            bz = int(a[0])
            if bz in self.biz:
                return None
            if bz < 4:
                return (f'open the {BIZ_NAME[bz]}', BIZ_PRICE[bz], 0, 0, lambda: self.open_biz(bz))
            if not self.market:
                return None
            return (f'lot + build the {BIZ_NAME[bz]}', LOT_PRICE + BIZ_PRICE[bz], 0, 0, lambda: self.open_biz(bz, built=True))
        if kind == 'market' and not self.market and self.P() >= 1:
            return ('Market Street', MARKET_PRICE, 0, 0, lambda: setattr(self, 'market', True))
        if kind in ('kitchen', 'facade', 'mgr'):
            for bz in (self.biz if a[0] == '*' else [int(a[0])]):
                z = self.biz.get(bz)
                if not z or z['open'] > self.day:
                    continue
                full = z.get('units_used', 0) >= biz_units(z['level'], z['tier'], z['mgr']) - 1
                if kind == 'kitchen' and z['level'] < 2 and (full or self.s.get('upgrade_blind')):
                    return (f'{BIZ_NAME[bz]} kitchen {z["level"] + 1}', BIZ_UP[z['level']], 0, 0, lambda z=z: z.__setitem__('level', z['level'] + 1))
                if kind == 'facade' and z['tier'] < 3 and (full or self.s.get('upgrade_blind') or self.P() < 5):
                    return (f'{BIZ_NAME[bz]} facade {z["tier"] + 1}', BIZ_TIER[z['tier']], 0, 0, lambda z=z: z.__setitem__('tier', z['tier'] + 1))
                if kind == 'mgr' and not z['mgr'] and full and z['level'] >= 1:
                    return (f'{BIZ_NAME[bz]} manager', 0, 0, 0, lambda z=z: z.__setitem__('mgr', True))
            return None
        if kind == 'tproj':
            for k in (range(6) if a[0] == '*' else [int(a[0])]):
                if k in self.tproj:
                    continue
                g, w, st, need_p, mk = TPROJ[k]
                if self.P() < need_p or (mk and not self.market):
                    continue
                return (f'community project {k}', g, w, st, lambda k=k: self.tproj.add(k))
            return None
        if kind == 'valley':
            p = int(a[0])
            if p in self.valley or (p == 5 and len(self.valley) < 5):
                return None
            its, ns, g = VALLEY[p]
            for it, n in zip(its, ns):
                if it == 0xFF:
                    continue
                if it == I['FEAST']:
                    if 4 not in self.valley or not b['fridge']:
                        return None
                    continue
                if it == I['CIDER'] and not self.machines['keg']:
                    return None
                if self.inv[it] < n:
                    return None

            def f():
                for it, n in zip(its, ns):
                    if it != 0xFF and it != I['FEAST']:
                        self.take(it, n)
                self.valley.add(p)
                if p == 0:
                    self.recipes.add(I['SORBET'] - I['SALAD'])
                if p == 4:
                    self.recipes.add(I['FEAST'] - I['SALAD'])
            return (f'valley project {p}', g, 0, 0, f)
        if kind == 'staff':
            r = int(a[0])
            if r in self.staff:
                return None
            ok = {0: self.tilled > self.sprinkled + 60, 1: self.planted() >= self.s.get('harvester_at', 120), 2: len(self.animals) >= 6, 3: len(self.plots) >= 10}[r]
            if not ok or self.wage(r) > 0.2 * max(1.0, self.avg_income()):
                return None
            return (f'hire role {r}', 0, 0, 0, lambda: self.staff.add(r))
        if kind == 'horse' and not b['stable']:
            def f():
                b['stable'] = 1
            return ('stable + horse', BLDG_GOLD[C['B_STABLE']] + COAT_PRICE[0], BLDG_WOOD[C['B_STABLE']], BLDG_STONE[C['B_STABLE']], f)
        return None

    def orchard_trees(self):
        return sum(tr[2] for tr in self.trees if tr[1] >= 30 and tr[2] > 1.5)

    def open_biz(self, bz, built=False):
        self.biz[bz] = dict(level=0, tier=0, mgr=False, pantry={}, open=self.day + (1 if built else 0))
        if built:
            self.lots += 1

    def shop(self):
        saving = None
        # the TV's Sunday show and good friends teach recipes through the first year
        if self.day % 7 == 6:
            for d in (2, 6, 4, 9, 12, 14, 5, 8, 7, 13, 10, 3):
                if d not in self.recipes:
                    self.recipes.add(d)
                    break
        for key in self.s['wish']:
            for _ in range(12):
                o = self.offer(key)
                if not o:
                    break
                label, g, w, st, apply = o
                if saving is not None and g > saving * 0.15:
                    break
                if not self.afford(g, w, st):
                    if g >= 0.2 * max(self.gold, 1) and saving is None:
                        saving = g
                    break
                self.pay(label, g, w, st, cat=key.split(':')[0])
                apply()


def IS_MIN(it):
    return isinstance(it, int) and (I['COPPER'] <= it <= I['GEM'] or I['COAL'] <= it <= I['DIAMOND'])


# ------------------------------------------------------------------ strategies
_LATE = ['valley:0', 'valley:1', 'valley:2', 'valley:3', 'valley:4', 'tproj:*', 'kitchen:*', 'facade:*', 'mgr:*', 'valley:5']
_ALL_BIZ = ['biz:0', 'biz:1', 'biz:2', 'biz:3', 'market', 'biz:4', 'biz:5', 'biz:6', 'biz:7']
STRATEGIES = {
    'balanced': dict(
        desc='a bit of everything, the way the game suggests it',
        expand=0.55, mine=0.1, fish=0.05, tools=('can', 'hoe', 'hammer'),
        wish=['coop', 'hens:2:0', 'sprinklers', 'tool:can', 'barn', 'cows:2:0', 'sheep:1:0', 'house', 'fridge', 'scarecrow', 'plot', 'silo',
              'biz:0', 'fert', 'tool:hoe', 'staff:0', 'press:2', 'valley:0', 'valley:1', 'sheep:2:0', 'tool:hammer', 'biz:1',
              'staff:1', 'greenhouse', 'staff:2', 'trees:12', 'jar:3', 'keg:2', 'market', 'biz:2', 'biz:3', 'valley:2', 'valley:3',
              'valley:4', 'horse'] + _ALL_BIZ + _LATE + ['plot!']),
    'casual': dict(
        desc='the balanced plan at a relaxed pace: a smaller field, fewer trips to the mine and the river',
        expand=0.45, mine=0.05, fish=0.05, tools=('can', 'hoe', 'hammer'), field_scale=0.6, reserve=1500),
    'crops': dict(
        desc='fields first: sprinklers, land, seeds, field hands; shops that sell produce',
        expand=0.8, mine=0.05, fish=0.0, tools=('can', 'hoe', 'hammer'), land_low=60,
        wish=['sprinklers', 'tool:can', 'fert', 'plot', 'tool:hoe', 'house', 'staff:0', 'staff:1', 'greenhouse', 'coop',
              'hens:2:0', 'biz:0', 'biz:1', 'barn', 'cows:2:0', 'sheep:1:0', 'fridge', 'valley:0', 'tool:hammer', 'market', 'biz:4', 'biz:5',
              'biz:3', 'biz:2', 'trees:20', 'keg:2', 'valley:1', 'valley:2', 'valley:3', 'valley:4', 'sheep:2:0', 'press:1',
              'biz:6', 'biz:7'] + _LATE + ['plot!']),
    'animals': dict(
        desc='barn and coop full of rare breeds, presses and jars, the Creamery and the Inn',
        expand=0.4, mine=0.1, fish=0.05, tools=('can', 'hammer', 'hoe'), max_tiles=160,
        wish=['coop', 'hens:4:1', 'barn', 'cows:3:1', 'silo', 'sprinklers', 'house', 'sheep:2:1', 'press:3', 'jar:3',
              'tool:can', 'biz:2', 'fridge', 'plot', 'staff:2', 'cows:6:1', 'hens:6:1', 'valley:4', 'valley:0', 'valley:1',
              'tool:hammer', 'biz:0', 'market', 'biz:7', 'staff:0', 'keg:2', 'greenhouse', 'staff:1', 'valley:2', 'valley:3',
              'biz:1', 'biz:3', 'biz:4', 'biz:5', 'biz:6'] + _LATE + ['plot!']),
    'mine': dict(
        desc='the hammer first: deep floors, ore and gems sold at the forge',
        expand=0.3, mine=0.6, fish=0.0, tools=('hammer', 'can', 'hoe'), max_tiles=120,
        wish=['tool:hammer', 'sprinklers', 'house', 'coop', 'hens:2:0', 'tool:can', 'plot', 'fridge', 'barn', 'cows:2:0', 'sheep:1:0',
              'valley:3', 'biz:0', 'valley:0', 'valley:1', 'valley:2', 'staff:0', 'staff:1', 'greenhouse', 'market'] +
             _ALL_BIZ + ['valley:4'] + _LATE + ['plot!']),
    'business': dict(
        desc='storefronts as early as possible, Market Street, kitchens and facades; the farm feeds the shops',
        expand=0.6, mine=0.05, fish=0.05, tools=('can', 'hoe', 'hammer'), upgrade_blind=False,
        wish=['sprinklers', 'coop', 'hens:2:0', 'biz:0', 'tool:can', 'barn', 'cows:2:0', 'sheep:1:0', 'house', 'biz:1', 'plot',
              'kitchen:*', 'facade:*', 'fridge', 'staff:0', 'biz:2', 'market', 'biz:3', 'biz:4', 'biz:5', 'biz:6', 'biz:7',
              'mgr:*', 'tproj:*', 'staff:1', 'greenhouse', 'tool:hoe', 'tool:hammer', 'sheep:2:0', 'press:2', 'keg:2'] +
             _LATE + ['plot!']),
    # ---- the exploits we must not let trivialize the game
    'legend_rush': dict(
        desc='EXPLOIT CHECK: legendary seeds the day they unlock, greenhouse, harvester, every coin into fields',
        expand=0.85, mine=0.05, fish=0.0, tools=('can', 'hoe', 'hammer'), thrift=0.0, land_low=80, harvester_at=60,
        wish=['sprinklers', 'tool:can', 'fert', 'plot', 'staff:1', 'staff:0', 'greenhouse', 'tool:hoe', 'house', 'coop',
              'hens:2:0', 'barn', 'cows:2:0', 'sheep:1:0', 'fridge', 'keg:1', 'tool:hammer'] + _ALL_BIZ + _LATE + ['plot!']),
    'gem_farmer': dict(
        desc='EXPLOIT CHECK: every point of energy in the deep mine, plus a cafe feast a day to keep swinging',
        expand=0.15, mine=0.85, fish=0.0, tools=('hammer', 'can'), cafe=True, max_tiles=60,
        wish=['tool:hammer', 'house', 'sprinklers', 'coop', 'hens:2:0', 'fridge', 'barn', 'cows:2:0', 'sheep:1:0', 'valley:3', 'plot',
              'keg:1', 'valley:0', 'valley:1', 'valley:2', 'valley:4'] + _ALL_BIZ + _LATE + ['plot!']),
    'trader_shops': dict(
        desc="EXPLOIT CHECK: once prosperity 2 brings traders, buy their eggs/milk/honey to fill the Creamery, Bakery and Inn",
        expand=0.5, mine=0.05, fish=0.05, tools=('can', 'hoe', 'hammer'), trader=True, upgrade_blind=True,
        wish=['sprinklers', 'coop', 'hens:2:0', 'biz:0', 'biz:2', 'tool:can', 'barn', 'cows:2:0', 'sheep:1:0', 'house', 'kitchen:*',
              'facade:*', 'market', 'biz:7', 'mgr:*', 'biz:1', 'biz:3', 'plot', 'fridge', 'biz:4', 'biz:5', 'biz:6', 'tproj:*',
              'staff:0', 'staff:1', 'greenhouse', 'tool:hoe', 'tool:hammer'] + _LATE + ['plot!']),
    'cheap_kitchen': dict(
        desc='EXPLOIT CHECK: a fishing rod and the Restaurant: cheap fish dishes served as chef specials',
        expand=0.4, mine=0.05, fish=0.45, tools=('can', 'rod', 'hoe'), cheap_dishes=True, upgrade_blind=True,
        wish=['sprinklers', 'fridge', 'coop', 'hens:2:0', 'biz:0', 'tool:can', 'house', 'barn', 'cows:2:0', 'sheep:1:0', 'market',
              'biz:6', 'kitchen:6', 'facade:6', 'mgr:6', 'plot', 'biz:1', 'biz:2', 'biz:3', 'biz:4', 'biz:5', 'biz:7'] + _LATE + ['plot!']),
}
MAIN = ['balanced', 'casual', 'crops', 'animals', 'mine', 'business']
STRATEGIES['casual']['wish'] = STRATEGIES['balanced']['wish']
EXPLOITS = ['legend_rush', 'gem_farmer', 'trader_shops', 'cheap_kitchen']


def simulate(name, years=3):
    f = Farm(STRATEGIES[name])
    for _ in range(DAYS_YEAR * years):
        f.step()
    return f


# ------------------------------------------------------------------ milestones + reports
MILESTONES = [
    ('first plot bought', lambda d: d['plots'] > 4),
    ('a tool at copper', lambda d: max(d['tiers'].values()) >= 1),
    ('first storefront', lambda d: d['biz'] >= 1),
    ('a gold tool', lambda d: max(d['tiers'].values()) >= 3),
    ('10 plots', lambda d: d['plots'] >= 10),
    ('Market Street business', lambda d: d['biz'] >= 5),
    ('half the valley (32 plots)', lambda d: d['plots'] >= 32),
    ('all 8 businesses', lambda d: d['biz'] >= 8),
    ('prosperity 5', lambda d: d['P'] >= 5),
    ('valley finale', lambda d: d['valley'] >= 6),
    ('every plot (64)', lambda d: d['plots'] >= 64),
]


def when(f, test):
    for d in f.days:
        if test(d):
            return d['day']
    return None


def season_name(k):
    return f'Y{k // 4 + 1} {["Spring", "Summer", "Fall", "Winter"][k % 4]:<6}'


def date(day):
    if day is None:
        return '-'
    return f'Y{day // DAYS_YEAR + 1} {"SpSuFaWi"[2 * season_of(day):2 * season_of(day) + 2]} {day % DAYS_SEASON + 1}'


def season_rows(f):
    rows = []
    for k in range(len(f.days) // DAYS_SEASON):
        a, b = f.days[k * DAYS_SEASON], f.days[(k + 1) * DAYS_SEASON - 1]
        start = f.days[k * DAYS_SEASON - 1]['earned'] if k else 0
        rows.append(dict(season=k, earned=b['earned'], per_day=(b['earned'] - start) / DAYS_SEASON, gold=b['gold'],
                         plots=b['plots'], biz=b['biz'], P=b['P'], valley=b['valley'], tilled=b['tilled'], staff=b['staff'],
                         animals=b['animals'], trees=b['trees']))
    return rows


def payback_table():
    """investment -> (cost, gain a day, payback days, context): each in the context a player first meets it"""
    rows = []
    hq = Q_HAPPY
    # land: a plot of meadow planted with the season's best common crop, sprinklers included
    best = max(tile_season_profit(c, 0, 1) for c in range(1, NUM_CROPS + 1) if CROPS[c][0] == SUMMER) / DAYS_SEASON
    for p in (43, 19, 29, 21, 54):
        farm = plot_profile(p, {'hammer': 1, 'axe': 1})[0]
        spr = farm / 8 * DECOR_GOLD['Sprinkler'][0]
        gain = farm * best + plot_profile(p, {'hammer': 1, 'axe': 1})[4] * 5 * 75 / TREE_RIPE
        rows.append((f'plot {p} ({RG_NAME[PLOT_RG[p]]}), sprinklered, common crops', PLOT_PRICE[p] + spr, gain))
    # animals (adult, happy, hay bought)
    for k, kname in ((C['A_COW'], 'cow'), (C['A_HEN'], 'hen'), (C['A_SHEEP'], 'sheep')):
        for br, bname in enumerate(('common', 'rare', 'golden')):
            it = ANIMAL_ITEM[k]
            n = (2 if br else 1) / (C['WOOL_DAYS'] if k == C['A_SHEEP'] else 1)
            gain = n * PRICE[it] * hq - HAY_PRICE
            rows.append((f'{bname} {kname}', BREED_PRICE[k][br], gain))
    # machines on their best cheap input
    cheap_veg = min(range(I['APPLE']), key=lambda i: PRICE[i])
    cheap_fruit = min(range(I['APPLE'], I['CHERRY'] + 1), key=lambda i: PRICE[i])
    rows.append(('cheese press (milk -> cheese)', MACHINE_GOLD['press'], (PRICE[I['CHEESE']] - PRICE[I['MILK']] * hq) / MACHINE_DAYS['press']))
    rows.append((f'keg ({NAME[cheap_fruit]} -> cider)', MACHINE_GOLD['keg'], (PRICE[I['CIDER']] - PRICE[cheap_fruit]) / MACHINE_DAYS['keg']))
    rows.append((f'keg ({NAME[cheap_veg]} -> juice)', MACHINE_GOLD['keg'], (PRICE[I['JUICE']] - PRICE[cheap_veg]) / MACHINE_DAYS['keg']))
    rows.append((f'preserves jar ({NAME[cheap_fruit]} -> jam)', MACHINE_GOLD['jar'], (PRICE[I['JAM']] - PRICE[cheap_fruit]) / MACHINE_DAYS['jar']))
    # fruit tree (mature, year average incl. winter)
    for k in range(4):
        fr = TREE_FRUIT[k]
        per_year = (84 / TREE_RIPE) * 5 + (28 / TREE_RIPE) * 2
        rows.append((f'{NAME[fr].lower()} tree (mature, year average)', TREE_GOLD[0] + TREE_GOLD[1] * k, per_year * PRICE[fr] / DAYS_YEAR))
    # sprinklers: the energy they free, priced at what that energy earns clearing + mining (G15 a point)
    for nm, cov in (('Sprinkler', 8), ('Big Sprinkler', 24)):
        g, _, st = DECOR_GOLD[nm]
        rows.append((f'{nm.lower()} ({cov} tiles, basic can)', g + 15 * st, cov * (1 - 0.2) * 15))
    rows.append(('fertilizer (one tile, kept for good)', FERT_PRICE, (Q_COMMON[1] - Q_COMMON[0]) * best / Q_COMMON[0]))
    rows.append(('greenhouse repair (100 regrowing tiles carried through a season change)', REPAIR['greenhouse'][0],
                 100 * PRICE[I['STRAWB']] / CROPS[3][3] * 7 / DAYS_SEASON))
    # businesses: stocked with their typical produce, at kitchen 0 / facade 0, prosperity 1
    typical = {0: {6: 99, 7: 99, 3: 99}, 1: {0: 99, 3: 99}, 2: {0: 99, 1: 99, 2: 99}, 3: {0: 99, 4: 99, 7: 99},
               4: {0: 99, 1: 99, 7: 99}, 5: {0: 99, 2: 99}, 7: {0: 99, 1: 99, 2: 99}}
    for bz in range(8):
        if bz == 6:
            items = [I['PUMPSOUP'], I['RADISH']] + [0xFF] * 6
            pan = {0: 99, 1: 99}
        else:
            items, pan = PAN_ITEM[bz], dict(typical[bz])
        before = dict(pan)
        v, prem, basic = run_one(bz, pan, 0, 0, False, items)
        used = sum(before[k] - pan[k] for k in before)
        cost = sum((before[k] - pan[k]) * PRICE[items[k]] for k in before)
        if bz == 7:
            v += (4 + 1) * INN_RATE
        gain = v * biz_mult(0, 1, False, False, bz) - cost
        price = BIZ_PRICE[bz] + (LOT_PRICE if bz >= 4 else 0)
        rows.append((f'{BIZ_NAME[bz]} (kitchen 0, {used} units a night)', price, gain))
    # kitchen / facade / manager on a stocked bakery making pies (egg + milk + pumpkin)
    pie = (PRICE[I['EGG']] + PRICE[I['MILK']] + PRICE[I['PUMPKIN']])
    for lv in (0, 1):
        extra = (BIZ_CAP[lv + 1] - BIZ_CAP[lv]) / 3 * (pie * (PQ[0] + lv + 1) / 4 - pie) + BIZ_CAP[lv] / 3 * pie / 4
        rows.append((f'kitchen {lv + 1} (Bakery pies)', BIZ_UP[lv], extra))
    night = BIZ_CAP[1] / 3 * pie * (PQ[0] + 1) / 4
    for t in range(3):
        rows.append((f'facade tier {t + 1} (Bakery, kitchen 1)', BIZ_TIER[t], night * 0.08 + pie * (PQ[0] + 1) / 4 / 3 - pie / 3))
    rows.append(('manager (Bakery, kitchen 1)', 0, night * 0.10 + 4 / 3 * (pie * (PQ[0] + 1) / 4 - pie) - (MGR_WAGE[0] + MGR_WAGE[1])))
    rows.append(('Market Street (unlocks the lots, +2 prosperity points)', MARKET_PRICE, 0))
    for k, (g, w, st, need_p, mk) in enumerate(TPROJ):
        rows.append((f'community project {k} (+4 prosperity points)', g + 10 * w + 15 * st, 0))
    for p, (its, ns, g) in enumerate(VALLEY):
        goods = sum(PRICE[it] * n for it, n in zip(its, ns) if it != 0xFF)
        rows.append((f'valley project {p} (gold + G{goods} of goods)', g + goods, 0))
    for t in range(3):
        rows.append((f'tool tier {t + 1}', TIER_GOLD[t] + 5 * PRICE[[I["COPPER"], I["IRON"], I["GOLD"]][t]], 0))
    return [(n, c, g, (c / g if g > 0 else None)) for n, c, g in rows]


def per_energy():
    rows = []
    for t in range(4):
        for fl in (1, 5, 10, 15, 20):
            g, e, _ = mine_rock(fl, t)
            rows.append((f'mine floor {fl}, hammer tier {t}', g / e))
    for t in range(4):
        rows.append((f'fishing (lake by day), rod tier {t}', fish_cast(t) / 4))
    for t in range(4):
        best = max((tile_season_profit(c, 0, 1), c) for c in range(1, NUM_CROPS + 1) if CROPS[c][0] == SUMMER)
        c = best[1]
        e = 2 / AREA[t] + (CROPS[c][2] + 18) * (1 - P_RAIN[1]) / AREA[t]
        rows.append((f'hand-watered {NAME[c - 1].lower()} (common), can tier {t}', best[0] / e))
    return rows


COLORS = {'balanced': '#2a6fdb', 'casual': '#7aa6ef', 'crops': '#3a9a4a', 'animals': '#c4862b', 'mine': '#7a5cc4',
          'business': '#c4425a', 'legend_rush': '#3a9a4a', 'gem_farmer': '#7a5cc4', 'trader_shops': '#c4425a',
          'cheap_kitchen': '#c4862b'}


def svg_chart(series, title, path, ymax=None):
    """cumulative gold earned by day, log scale, one polyline per strategy; series = [(name, [(day, earned)], dashed)]"""
    W, H, L, R, T, B = 720, 360, 64, 150, 34, 36
    lo, hi = 3, 7.5   # 10^3 .. ~3*10^7
    x = lambda d: L + (W - L - R) * d / (DAYS_YEAR * 3)
    y = lambda v: T + (H - T - B) * (1 - (math.log10(max(v, 10 ** lo)) - lo) / (hi - lo))
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="Helvetica,Arial,sans-serif" font-size="11">',
           f'<rect width="{W}" height="{H}" fill="#ffffff"/>',
           f'<text x="{L}" y="20" font-size="13" font-weight="bold" fill="#222">{title}</text>']
    for e in range(lo, 8):
        for m in (1, 3):
            v = m * 10 ** e
            if math.log10(v) > hi:
                continue
            yy = y(v)
            out.append(f'<line x1="{L}" y1="{yy:.1f}" x2="{W - R}" y2="{yy:.1f}" stroke="#e6e6e6"/>')
            lab = f'{v // 1000000}M' if v >= 10 ** 6 else f'{v // 1000}k'
            out.append(f'<text x="{L - 6}" y="{yy + 4:.1f}" text-anchor="end" fill="#666">{lab}</text>')
    for yr in range(4):
        for se in range(4):
            d = yr * DAYS_YEAR + se * DAYS_SEASON
            if d > DAYS_YEAR * 3:
                continue
            xx = x(d)
            out.append(f'<line x1="{xx:.1f}" y1="{T}" x2="{xx:.1f}" y2="{H - B}" stroke="{"#bbb" if se == 0 else "#f0f0f0"}"/>')
        if yr < 3:
            out.append(f'<text x="{x(yr * DAYS_YEAR + DAYS_YEAR / 2):.1f}" y="{H - B + 16}" text-anchor="middle" fill="#444">Year {yr + 1}</text>')
    for k, (name, pts, dashed) in enumerate(series):
        col = COLORS.get(name, '#444')
        poly = ' '.join(f'{x(d):.1f},{y(v):.1f}' for d, v in pts[::2])
        dash = ' stroke-dasharray="5,4"' if dashed else ''
        out.append(f'<polyline points="{poly}" fill="none" stroke="{col}" stroke-width="2"{dash}/>')
        ly = T + 14 + 16 * k
        out.append(f'<line x1="{W - R + 12}" y1="{ly - 4}" x2="{W - R + 34}" y2="{ly - 4}" stroke="{col}" stroke-width="2"{dash}/>')
        out.append(f'<text x="{W - R + 40}" y="{ly}" fill="#333">{name}{" (before)" if dashed else ""}</text>')
    out.append('</svg>')
    open(path, 'w').write('\n'.join(out))


def _fmt(v):
    return f'{v / 1e6:.2f}M' if v >= 1e6 else f'{v / 1e3:.0f}k' if v >= 1e4 else f'{v:.0f}'


def write_report(results, before_path=None):
    """rewrite the generated part of docs/BALANCE.md (between the markers) and the charts in docs/media"""
    import json
    snap = snapshot(results)
    before = json.load(open(before_path)) if before_path else None
    media = os.path.join(ROOT, 'docs', 'media')
    os.makedirs(media, exist_ok=True)
    svg_chart([(n, [(d, e) for d, e, _ in snap['strategies'][n]['curve']], False) for n in MAIN],
              'Gold earned (cumulative, log scale): the five play styles + a casual player', os.path.join(media, 'balance_styles.svg'))
    svg_chart([(n, [(d, e) for d, e, _ in snap['strategies'][n]['curve']], False) for n in ['balanced'] + EXPLOITS],
              'Exploit checks against the balanced player (log scale)', os.path.join(media, 'balance_exploits.svg'))
    if before:
        svg_chart([(n, [(d, e) for d, e, _ in before['strategies'][n]['curve']], True) for n in ('balanced', 'gem_farmer')] +
                  [(n, [(d, e) for d, e, _ in snap['strategies'][n]['curve']], False) for n in ('balanced', 'gem_farmer')],
                  'Before and after: the balanced player and the gem farmer', os.path.join(media, 'balance_before_after.svg'))
    L = []
    L.append('### Income by season (after)\n')
    L.append('Gold earned during the season (per day) and what the farm holds at its end. `tiles` = planted soil, '
             '`P` = Town Prosperity, `valley` = restoration projects done.\n')
    for n in MAIN + EXPLOITS:
        L.append(f'**{n}**: {STRATEGIES[n]["desc"]}\n')
        L.append('| season | earned / day | earned total | plots | tiles | shops | P | valley | staff | animals |')
        L.append('|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|')
        for r in snap['strategies'][n]['seasons']:
            L.append(f'| {season_name(r["season"]).strip()} | {r["per_day"]:.0f} | {_fmt(r["earned"])} | {r["plots"]} | {r["tilled"]:.0f} | '
                     f'{r["biz"]} | {r["P"]} | {r["valley"]} | {r["staff"]} | {r["animals"]} |')
        L.append('')
    L.append('### Milestones: when each strategy reaches them (before -> after)\n')
    ms = [m for m, _ in MILESTONES]
    L.append('| strategy | ' + ' | '.join(ms) + ' |')
    L.append('|---|' + '---|' * len(ms))
    for n in MAIN + EXPLOITS:
        cells = []
        for m in ms:
            a = date(snap['strategies'][n]['milestones'][m])
            b = date(before['strategies'][n]['milestones'][m]) if before else None
            cells.append(f'{b} -> **{a}**' if before else a)
        L.append(f'| {n} | ' + ' | '.join(cells) + ' |')
    L.append('')
    if before:
        L.append('### Gold earned by the end of each year (before -> after)\n')
        L.append('| strategy | year 1 | year 2 | year 3 |')
        L.append('|---|---:|---:|---:|')
        for n in MAIN + EXPLOITS:
            row = []
            for yr in range(3):
                k = yr * 4 + 3
                a = snap['strategies'][n]['seasons'][k]['earned']
                b = before['strategies'][n]['seasons'][k]['earned']
                row.append(f'{_fmt(b)} -> **{_fmt(a)}**')
            L.append(f'| {n} | ' + ' | '.join(row) + ' |')
        L.append('')
    L.append('### Payback of each investment (after)\n')
    L.append('Cost (gold, wood/stone at June\'s prices) over the extra gold a day it brings in the situation a player first '
             'meets it (stated in the row). Town projects pay back in prosperity (+4% business takings a level, traders, '
             'the town changing), not directly.\n')
    L.append('| investment | cost | gain / day | payback (days) |')
    L.append('|---|---:|---:|---:|')
    bp = {r[0]: r for r in before['payback']} if before else {}
    for name, cost, gain, pb in snap['payback']:
        L.append(f'| {name} | {cost:.0f} | {gain:.0f} | {pb:.0f} |' if pb else f'| {name} | {cost:.0f} | - | - |')
    L.append('')
    L.append('### Gold per point of energy (after' + (', before in brackets' if before else '') + ')\n')
    L.append('| activity | gold / energy |')
    L.append('|---|---:|')
    be = {r[0]: r[1] for r in before['per_energy']} if before else {}
    for name, v in snap['per_energy']:
        L.append(f'| {name} | {v:.1f}' + (f' ({be[name]:.1f})' if name in be else '') + ' |')
    L.append('')
    path = os.path.join(ROOT, 'docs', 'BALANCE.md')
    doc = open(path).read()
    a, b = '<!-- econ_model:begin -->', '<!-- econ_model:end -->'
    i, j = doc.index(a) + len(a), doc.index(b)
    open(path, 'w').write(doc[:i] + '\n' + '\n'.join(L) + '\n' + doc[j:])
    print('wrote', path)


def snapshot(results):
    """everything the report shows, as plain data (--json keeps a 'before' copy to compare against)"""
    out = {'strategies': {}, 'payback': payback_table(), 'per_energy': per_energy()}
    for n, f in results.items():
        out['strategies'][n] = dict(seasons=season_rows(f), milestones={m: when(f, t) for m, t in MILESTONES},
                                    curve=[(d['day'], d['earned'], d['gold']) for d in f.days])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--strategy', default=None)
    ap.add_argument('--log', action='store_true')
    ap.add_argument('--report', action='store_true')
    ap.add_argument('--json', default=None, help='write the snapshot as JSON to this file')
    ap.add_argument('--before', default=None, help='a --json snapshot of the old numbers, for the report')
    args = ap.parse_args()
    names = [args.strategy] if args.strategy else MAIN + EXPLOITS
    results = {n: simulate(n) for n in names}
    if args.json:
        import json
        json.dump(snapshot(results), open(args.json, 'w'))
    for n, f in results.items():
        print(f'== {n}: {STRATEGIES[n]["desc"]}')
        for r in season_rows(f):
            print(f'  {season_name(r["season"])}  earned {r["earned"]:>9.0f}  /day {r["per_day"]:>7.0f}  gold {r["gold"]:>8.0f}  '
                  f'plots {r["plots"]:>2}  tiles {r["tilled"]:>4.0f}  biz {r["biz"]}  P{r["P"]}  valley {r["valley"]}  '
                  f'staff {r["staff"]}  animals {r["animals"]}  trees {r["trees"]:.0f}')
        print('  milestones: ' + ', '.join(f'{m} {date(when(f, t))}' for m, t in MILESTONES))
        if args.log:
            for d, what in f.log:
                print(f'    {date(d)}: {what}')
    if args.report:
        write_report(results, args.before)


if __name__ == '__main__':
    main()
