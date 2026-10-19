#!/usr/bin/env python3
"""
tables2c.py - macht aus den Tischbeschreibungen des Originals (app/src/main/assets/tables/table*.json)
und dem Sternkatalog (fields/StarData.java, Stars.java) die C-Daten des Vectrex-Ports.

Aufruf (im Ordner vectrex/):   python tools/tables2c.py
Ergebnis:                      core/gen/table_data.c, core/gen/star_data.c

Was hier schon erledigt wird, damit die Konsole es nicht tun muss:
  - Zahlen, die im JSON als Text stehen, werden zu float.
  - Boegen: die Stossgeometrie bleibt die des Originals (gleiche Segmentzahl, gleiche Physik); zum
    ZEICHNEN gibt es eine eigene, sparsamere Punktliste (Pfeilhoehe <= ARC_TOL Feldeinheiten),
    denn auf der Roehre zaehlt jeder Strich (siehe docs/ARCHITECTURE.md, Strahlbudget).
  - Ziele an einer Wand (wallStart/wallEnd/...) werden zu Strecken ausgerechnet.
  - Zeichenreihenfolge: die Elemente werden so geordnet, dass der Strahl wenig springt (naechster
    Nachbar ueber die Enden der Linienzuege). Die Reihenfolge ist FEST - sie aendert sich nie von
    Bild zu Bild; das haelt die Geometrie ruhig.
"""
import json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
TABLES = os.path.join(ROOT, 'app', 'src', 'main', 'assets', 'tables')
FIELDS = os.path.join(ROOT, 'app', 'src', 'main', 'java', 'com', 'dozingcatsoftware', 'vectorpinball', 'fields')
OUT = os.path.join(HERE, '..', 'core', 'gen')

ARC_TOL = 0.07          # groesste Abweichung der gezeichneten Sehne vom Bogen, in Feldeinheiten

T_WALL, T_ARC, T_PATH, T_BUMPER, T_DROP, T_ROLL, T_SENSOR, T_SPINNER, T_FLIPPER = range(9)
TYPE_NAMES = ['VP_WALL', 'VP_ARC', 'VP_PATH', 'VP_BUMPER', 'VP_DROP', 'VP_ROLL', 'VP_SENSOR', 'VP_SPINNER', 'VP_FLIPPER']
CLASSES = {'WallElement': T_WALL, 'WallArcElement': T_ARC, 'WallPathElement': T_PATH, 'BumperElement': T_BUMPER,
           'DropTargetGroupElement': T_DROP, 'RolloverGroupElement': T_ROLL, 'SensorElement': T_SENSOR,
           'SpinnerElement': T_SPINNER, 'FlipperElement': T_FLIPPER}

F_KILL, F_RETRACT_HIT, F_DISABLED, F_IGNORE_BALL, F_TOGGLE_OFF, F_CYCLE = 1, 2, 4, 8, 16, 32
F_HAS_COLOR, F_HAS_INACT, F_HAS_OUTER, F_HAS_INACT_OUTER, F_LAYER_TO, F_LAYER_FROM = 64, 128, 256, 512, 1024, 2048


def fl(v, default=0.0):
    if v is None:
        return default
    return float(v)


def truthy(v):
    return v is True or (isinstance(v, (int, float)) and not isinstance(v, bool) and v != 0)


def color(lst):
    if len(lst) == 3:
        r, g, b = [int(x) for x in lst]
        a = 255
    else:
        r, g, b, a = [int(x) for x in lst]
    return (a << 24) | (r << 16) | (g << 8) | b


def cq(x):
    """Koordinate als int16 in 1/1024 Feldeinheit (Strecken und Bogenpunkte liegen so in den Tischdaten)"""
    v = int(round(float(x) * 1024))
    assert -32768 <= v <= 32767, 'Koordinate %r passt nicht in 1/1024-Einheiten' % x
    return '%d' % v

def cf(x):
    s = repr(round(float(x), 5))
    if 'e' in s or 'E' in s:
        s = '%.6f' % float(x)
    if '.' not in s:
        s += '.0'
    return s + 'f'


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def arc_draw_points(cx, cy, rx, ry, a0, a1):
    r = max(abs(rx), abs(ry))
    span = abs(a1 - a0)
    if r <= ARC_TOL:
        n = 2
    else:
        step = 2.0 * math.acos(max(-1.0, 1.0 - ARC_TOL / r))
        n = max(1, int(math.ceil(span / step)))
    if span > math.radians(100):
        n = max(n, 3)
    elif span > math.radians(40):
        n = max(n, 2)
    return [(cx + rx * math.cos(a0 + i * (a1 - a0) / n), cy + ry * math.sin(a0 + i * (a1 - a0) / n)) for i in range(n + 1)]


def convert_table(path, idx):
    d = json.load(open(path, encoding='utf-8'))
    segs, rolls, dpts, elems = [], [], [], []
    for e in d['elements']:
        if not isinstance(e, dict):
            continue
        t = CLASSES[e['class']]
        el = dict(type=t, layer=int(e.get('layer', 0)), flags=0, id=e.get('id', ''), color=0, inact=0, outer=0,
                  inact_outer=0, score=int(e.get('score', 0)), first=0, count=0, dfirst=0, dcount=0, p=[0.0] * 8,
                  ends=None)
        if 'color' in e:
            el['color'] = color(e['color']); el['flags'] |= F_HAS_COLOR
        if 'inactiveLayerColor' in e:
            el['inact'] = color(e['inactiveLayerColor']); el['flags'] |= F_HAS_INACT
        p = el['p']
        if t == T_WALL:
            x1, y1, x2, y2 = [fl(v) for v in e['position']]
            el['first'] = len(segs); el['count'] = 1
            segs.append((x1, y1, x2, y2))
            p[0] = fl(e.get('kick')); p[1] = fl(e.get('restitution')); p[2] = fl(e.get('friction'), 0.2)
            if e.get('kill') is True: el['flags'] |= F_KILL
            if e.get('retractWhenHit') is True: el['flags'] |= F_RETRACT_HIT
            if e.get('disabled') is True: el['flags'] |= F_DISABLED
            if e.get('ignoreBall') is True: el['flags'] |= F_IGNORE_BALL
            el['ends'] = ((x1, y1), (x2, y2))
        elif t == T_ARC:
            cx, cy = [fl(v) for v in e['center']]
            if 'radius' in e:
                rx = ry = fl(e['radius'])
            else:
                rx, ry = fl(e.get('xradius')), fl(e.get('yradius'))
            n = int(e['segments']) if e.get('segments') is not None else 5
            a0, a1 = math.radians(fl(e.get('minangle'))), math.radians(fl(e.get('maxangle')))
            pts = [(cx + rx * math.cos(a0 + i * (a1 - a0) / n), cy + ry * math.sin(a0 + i * (a1 - a0) / n)) for i in range(n + 1)]
            el['first'] = len(segs); el['count'] = n
            for i in range(n):
                segs.append((pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1]))
            dp = arc_draw_points(cx, cy, rx, ry, a0, a1)
            if len(dp) > len(pts):
                dp = pts
            el['dfirst'] = len(dpts); el['dcount'] = len(dp)
            dpts.extend(dp)
            p[0] = fl(e.get('restitution')); p[1] = fl(e.get('friction'), 0.2)
            p[2], p[3], p[4], p[5], p[6], p[7] = cx, cy, rx, ry, a0, a1
            if truthy(e.get('ignoreBall')): el['flags'] |= F_IGNORE_BALL
            el['ends'] = (dp[0], dp[-1])
        elif t == T_PATH:
            pts = [(fl(q[0]), fl(q[1])) for q in e['positions']]
            el['first'] = len(segs); el['count'] = len(pts) - 1
            for i in range(len(pts) - 1):
                segs.append((pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1]))
            p[0] = fl(e.get('restitution')); p[1] = fl(e.get('friction'), 0.2)
            if truthy(e.get('ignoreBall')): el['flags'] |= F_IGNORE_BALL
            el['ends'] = (pts[0], pts[-1])
        elif t == T_BUMPER:
            p[0], p[1] = [fl(v) for v in e['position']]
            p[2] = fl(e.get('radius')); p[3] = fl(e.get('outerRadius')); p[4] = fl(e.get('kick'))
            p[5] = fl(e.get('sides'))                            # feste Eckenzahl (Spinball: Sechsecke), 0 = rund
            el['outer'] = color(e['outerColor']) if 'outerColor' in e else color([0, 0, 255, 128])
            el['flags'] |= F_HAS_OUTER
            if 'inactiveLayerOuterColor' in e:
                el['inact_outer'] = color(e['inactiveLayerOuterColor']); el['flags'] |= F_HAS_INACT_OUTER
            el['ends'] = ((p[0], p[1]), (p[0], p[1]))
        elif t == T_DROP:
            pos = e.get('positions')
            tl = []
            if pos:
                tl = [tuple(fl(v) for v in q) for q in pos]
            else:
                ws = [fl(v) for v in e['wallStart']]; we = [fl(v) for v in e['wallEnd']]
                gap = fl(e['gapFromWall']); start = fl(e['startDistanceAlongWall']); tw = fl(e['targetWidth'])
                gb = fl(e.get('gapBetweenTargets')); nt = int(e['numTargets'])
                wa = math.atan2(we[1] - ws[1], we[0] - ws[0]); pa = wa + math.pi / 2
                gx, gy = gap * math.cos(pa), gap * math.sin(pa)
                for i in range(nt):
                    s0 = start + i * (tw + gb); s1 = s0 + tw
                    tl.append((ws[0] + s0 * math.cos(wa) + gx, ws[1] + s0 * math.sin(wa) + gy,
                               ws[0] + s1 * math.cos(wa) + gx, ws[1] + s1 * math.sin(wa) + gy))
            assert 0 < len(tl) <= 32
            el['first'] = len(segs); el['count'] = len(tl)
            segs.extend(tl)
            p[0] = fl(e.get('reset'))
            el['ends'] = ((tl[0][0], tl[0][1]), (tl[-1][2], tl[-1][3]))
        elif t == T_ROLL:
            if e.get('toggleOff') is True: el['flags'] |= F_TOGGLE_OFF
            if e.get('cycleOnFlipper') is True: el['flags'] |= F_CYCLE
            if e.get('ignoreBall') is True: el['flags'] |= F_IGNORE_BALL
            drad = fl(e.get('radius')); dreset = fl(e.get('reset'))
            el['first'] = len(rolls); el['count'] = len(e['rollovers'])
            assert 0 < el['count'] <= 32
            for r in e['rollovers']:
                rolls.append(dict(x=fl(r['position'][0]), y=fl(r['position'][1]),
                                  r=fl(r['radius']) if 'radius' in r else drad,
                                  color=color(r['color']) if 'color' in r else 0, has_color=1 if 'color' in r else 0,
                                  score=int(r['score']) if 'score' in r else el['score'],
                                  reset=fl(r['reset']) if 'reset' in r else dreset))
            a, b = rolls[el['first']], rolls[-1]
            el['ends'] = ((a['x'], a['y']), (b['x'], b['y']))
        elif t == T_SENSOR:
            r = [fl(v) for v in e['rect']]
            p[0], p[1], p[2], p[3] = min(r[0], r[2]), min(r[1], r[3]), max(r[0], r[2]), max(r[1], r[3])
            if e.get('ballLayer') is not None:
                p[4] = float(int(e['ballLayer'])); el['flags'] |= F_LAYER_TO
            if e.get('ballLayerFrom') is not None:
                p[5] = float(int(e['ballLayerFrom'])); el['flags'] |= F_LAYER_FROM
        elif t == T_SPINNER:
            p[0], p[1] = [fl(v) for v in e['position']]
            p[2] = fl(e.get('radius')); p[3] = fl(e.get('minActivationSpeed'), 4.0)
            p[4] = fl(e.get('baseCyclesPerSecond'), 8.0); p[5] = fl(e.get('minCyclesPerSecond'), 1.5)
            p[6] = fl(e.get('speedDecayRate'), 0.9)
            # Spinball: sichtbarer Balken in diesem Winkel (Grad); 0 in p[7] = unsichtbar wie im Original von Vector Pinball
            p[7] = math.radians(fl(e['barAngle'])) + 10.0 if 'barAngle' in e else 0.0
            el['ends'] = ((p[0], p[1]), (p[0], p[1]))
        elif t == T_FLIPPER:
            p[0], p[1] = [fl(v) for v in e['position']]
            p[2] = fl(e['length']); p[3] = math.radians(fl(e['minangle'])); p[4] = math.radians(fl(e['maxangle']))
            p[5] = fl(e['upspeed']); p[6] = fl(e['downspeed'])
            el['ends'] = ((p[0], p[1]), (p[0] + p[2], p[1]))
        elems.append(el)

    # Zeichenreihenfolge: naechster Nachbar ueber die Enden, Start unten links
    todo = [i for i, el in enumerate(elems) if el['ends'] is not None]
    order, cur = [], (0.0, 0.0)
    while todo:
        best, brev, bd = None, 0, 1e30
        for i in todo:
            a, b = elems[i]['ends']
            da = math.hypot(a[0] - cur[0], a[1] - cur[1]); db = math.hypot(b[0] - cur[0], b[1] - cur[1])
            if da < bd: best, brev, bd = i, 0, da
            if db < bd and elems[i]['type'] in (T_WALL, T_ARC, T_PATH): best, brev, bd = i, 1, db
        todo.remove(best)
        order.append(best | (0x8000 if brev else 0))
        a, b = elems[best]['ends']
        cur = a if brev else b

    dead = [fl(v) for v in d.get('launchDeadZone', [])]
    lp = [fl(v) for v in d['launchPosition']]; lv = [fl(v) for v in d['launchVelocity']]
    ld = [fl(v) for v in d.get('launchVelocityRandomDelta', [0, 0])]
    n = 'vt%d' % idx
    o = []
    o.append('/* ---- Tisch %d: %s ---- */' % (idx, d.get('name', '?')))
    o.append('static const vp_cseg_t %s_segs[] = {' % n)
    for s in segs:
        o.append('    { %s, %s, %s, %s },' % tuple(cq(v) for v in s))
    o.append('};')
    o.append('static const vp_roll_def_t %s_rolls[] = {' % n)
    for r in rolls:
        o.append('    { %s, %s, %s, 0x%08Xu, %d, %s, %d },' % (cf(r['x']), cf(r['y']), cf(r['r']), r['color'], r['score'], cf(r['reset']), r['has_color']))
    if not rolls:
        o.append('    { 0, 0, 0, 0, 0, 0, 0 },')
    o.append('};')
    o.append('static const int16_t %s_dpts[][2] = {' % n)
    for q in dpts:
        o.append('    { %s, %s },' % (cq(q[0]), cq(q[1])))
    if not dpts:
        o.append('    { 0, 0 },')
    o.append('};')
    o.append('static const vp_elem_def_t %s_elems[] = {' % n)
    for el in elems:
        o.append('    { %s, %d, 0x%04X, %s, 0x%08Xu, 0x%08Xu, 0x%08Xu, 0x%08Xu, %d, %d, %d, %d, %d, { %s } },' % (
            TYPE_NAMES[el['type']], el['layer'], el['flags'], cstr(el['id']), el['color'], el['inact'], el['outer'],
            el['inact_outer'], el['score'], el['first'], el['count'], el['dfirst'], el['dcount'],
            ', '.join(cf(v) for v in el['p'])))
    o.append('};')
    o.append('static const uint16_t %s_order[] = { %s };' % (n, ', '.join('0x%04X' % v for v in order)))
    deleg = int(re.search(r'(\d+)', d.get('delegate', 'Field0Delegate')).group(1))
    bc = color(d['ballcolor']) if 'ballcolor' in d else color([255, 0, 0])
    bc2 = color(d['secondaryBallColor']) if 'secondaryBallColor' in d else color([176, 176, 176])
    tdef = ('    { %s, %d, %d, %s, %s, %s, %s, %s, 0x%08Xu, 0x%08Xu, %s, %s, %s, %s, %s, %s, { %s }, %d, %d, %d, %d,\n'
            '      %s_elems, %d, %s_segs, %d, %s_rolls, %d, %s_dpts, %d, %s_order, %d },') % (
        cstr(d.get('name', 'Table %d' % idx)), deleg, int(d.get('numballs', 3)),
        cf(fl(d.get('width'), 20)), cf(fl(d.get('height'), 30)), cf(fl(d.get('gravity'), 4)), cf(fl(d.get('targetTimeRatio'))),
        cf(fl(d.get('ballradius'), 0.5)), bc, bc2, cf(lp[0]), cf(lp[1]), cf(lv[0]), cf(lv[1]), cf(ld[0]), cf(ld[1]),
        ', '.join(cf(v) for v in (dead if len(dead) == 4 else [0, 0, 0, 0])), 1 if len(dead) == 4 else 0,
        int(d.get('mercyBallDuration', 20000)), int(d.get('multiballSaverDuration', 30000)),
        int(d.get('preserveMultiplierIncreaseDuration', 7500)),
        n, len(elems), n, len(segs), n, len(rolls), n, len(dpts), n, len(order))
    # Statistik: wie viele Striche kostet die feste Geometrie
    strokes = 0
    for el in elems:
        if el['flags'] & F_HAS_COLOR and (el['color'] >> 24) == 0 or (el['flags'] & F_HAS_COLOR and (el['color'] & 0xFFFFFF) == 0):
            continue
        if el['type'] == T_WALL: strokes += 1
        elif el['type'] == T_ARC: strokes += el['dcount'] - 1
        elif el['type'] == T_PATH: strokes += el['count']
    stats = dict(name=d.get('name'), elems=len(elems), segs=len(segs), rolls=len(rolls), wall_strokes=strokes,
                 bumpers=sum(1 for el in elems if el['type'] == T_BUMPER), targets=sum(el['count'] for el in elems if el['type'] == T_DROP),
                 flippers=sum(1 for el in elems if el['type'] == T_FLIPPER))
    return '\n'.join(o), tdef, stats


def convert_stars():
    src = open(os.path.join(FIELDS, 'StarData.java'), encoding='utf-8').read()
    body = src[src.index('STAR_DATA'):]
    body = body[body.index('{') + 1:body.index('};')]
    body = re.sub(r'//.*', '', body)
    nums = [float(x) for x in re.findall(r'-?\d+\.?\d*', body)]
    assert len(nums) % 3 == 0
    stars = []
    for i in range(0, len(nums), 3):
        dec, ra, mag = math.radians(nums[i]), math.radians(nums[i + 1]), nums[i + 2]
        stars.append((math.cos(dec) * math.cos(ra), -math.cos(dec) * math.sin(ra), math.sin(dec), mag, dec, ra))
    ssrc = open(os.path.join(FIELDS, 'Stars.java'), encoding='utf-8').read()
    cons = []
    for m in re.finditer(r'withSegments\(CATALOG,\s*"([^"]+)",([^)]*)\)', ssrc):
        seg = [int(x) for x in re.findall(r'\d+', m.group(2))]
        idxs = []
        for v in seg:
            if v not in idxs:
                idxs.append(v)
        ras = [stars[i][5] for i in idxs]; decs = [stars[i][4] for i in idxs]
        ara, adec = sum(ras) / len(ras), sum(decs) / len(decs)
        rad = 0.0
        for i in idxs:
            ra, dec = stars[i][5], stars[i][4]
            c = math.sin(dec) * math.sin(adec) + math.cos(dec) * math.cos(adec) * math.cos(ra - ara)
            rad = max(rad, math.acos(max(-1.0, min(1.0, c))))
        cons.append(dict(name=m.group(1), seg=seg, idx=idxs, ra=ara, dec=adec, rad=rad))
    assert len(cons) == 6, len(cons)
    o = ['/* Erzeugt von tools/tables2c.py aus StarData.java / Stars.java - nicht von Hand aendern. */',
         '#include "vp_stars.h"', '',
         'const int vp_nstars = %d;' % len(stars),
         'const vp_star_t vp_stars[] = {']
    for s in stars:
        o.append('    { %d, %d, %d, %d },' % (round(s[0] * 32767), round(s[1] * 32767), round(s[2] * 32767), round(s[3] * 256)))
    o.append('};')
    for k, c in enumerate(cons):
        o.append('static const uint16_t con%d_idx[] = { %s };' % (k, ', '.join(str(v) for v in c['idx'])))
        o.append('static const uint16_t con%d_seg[] = { %s };' % (k, ', '.join(str(v) for v in c['seg'])))
    o.append('const int vp_nconst = %d;' % len(cons))
    o.append('const vp_const_t vp_consts[] = {')
    for k, c in enumerate(cons):
        o.append('    { %s, %s, %s, %s, con%d_idx, %d, con%d_seg, %d },' % (cstr(c['name']), cf(c['ra']), cf(c['dec']), cf(c['rad']), k, len(c['idx']), k, len(c['seg']) // 2))
    o.append('};')
    open(os.path.join(OUT, 'star_data.c'), 'w', newline='\n').write('\n'.join(o) + '\n')
    return len(stars)


def main():
    os.makedirs(OUT, exist_ok=True)
    # Tische des Originals und eigene (vectrex/tables/), nach Nummer geordnet
    extra = os.path.join(HERE, '..', 'tables')
    files = [os.path.join(d, f) for d in (TABLES, extra) if os.path.isdir(d) for f in os.listdir(d) if re.match(r'table\d+\.json$', f)]
    files.sort(key=lambda f: int(re.search(r'\d+', os.path.basename(f)).group()))
    bodies, defs = [], []
    for f in files:
        idx = int(re.search(r'\d+', os.path.basename(f)).group())
        b, t, st = convert_table(f, idx)
        bodies.append(b); defs.append(t)
        print('table %d %-18s elems %3d  segs %3d  rollovers %3d  wall strokes %3d  bumpers %d  targets %2d  flippers %d' % (
            idx, st['name'], st['elems'], st['segs'], st['rolls'], st['wall_strokes'], st['bumpers'], st['targets'], st['flippers']))
    o = ['/* Erzeugt von tools/tables2c.py aus app/src/main/assets/tables/*.json - nicht von Hand aendern. */',
         '#include "vp.h"', ''] + bodies + ['', 'const int vp_num_tables = %d;' % len(defs), 'const vp_table_def_t vp_tables[] = {'] + defs + ['};']
    open(os.path.join(OUT, 'table_data.c'), 'w', newline='\n').write('\n'.join(o) + '\n')
    print('stars:', convert_stars())


if __name__ == '__main__':
    main()
