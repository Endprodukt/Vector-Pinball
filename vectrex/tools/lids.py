#!/usr/bin/env python3
"""
lids.py - findet auf jedem Tisch die Flaechen, auf denen ein DECKEL liegt (massive Bloecke: Schleudern, Inseln, Schienen aus
zwei Linien, Ecken hinter Waenden), und schreibt sie als Vielecke nach core/gen/lid_data.c. Das Spiel braucht sie fuer die
Verdeckung: was unter einem Deckel liegt, wird nicht gezeichnet.

Aufruf (im Ordner vectrex/):   python tools/lids.py [--preview shots]      braucht: numpy, opencv-python

Wie ein Deckel erkannt wird - ueber die Frage "kommt der Ball dorthin?":
  1. Alle festen Waende der Spielflaeche (Ebene 0) werden gerastert (20 Punkte je Feldeinheit). Waende, die das Spiel
     schaltet (mit id, disabled, retractWhenHit), gelten als offen.
  2. Vom Abschusspunkt aus wird geflutet, wohin der MITTELPUNKT des Balls kommen kann (mit 80 % des Ballradius, damit enge
     Bahnen im Raster nicht zufallen). Um den Ballradius aufgeblasen ist das die Flaeche, die der Ball beruehren kann.
  3. Der Rest zerfaellt an den Waenden in Stuecke. Ein Stueck ist ein Deckel, wenn es nicht an den Bildrand stoesst (das
     waere "ausserhalb des Tisches"), nicht winzig ist, kein Spielelement enthaelt (Rollover, Ziel, Spinner, Flipper,
     Fuehler - dort spielt der Ball, auch wenn die Flutung es nicht fand) und sein Rand fast ganz aus Wand besteht.
  4. Der Umriss wird zum Vieleck vereinfacht (hoechstens 24 Ecken) und an die Ecken der Waende geschnappt.

Mit --preview entsteht je Tisch ein Bild mit den nummerierten Deckeln - zum Nachsehen und Korrigieren. Korrekturen stehen in
LIDS_OFF (Deckel weglassen) und LIDS_ADD (Deckel von Hand) weiter unten.
"""
import argparse, json, math, os
import numpy as np
import cv2

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
TABLES = [os.path.join(ROOT, 'app', 'src', 'main', 'assets', 'tables', 'table%d.json' % i) for i in range(1, 10)]
TABLES.append(os.path.join(HERE, '..', 'tables', 'table10.json'))
OUT = os.path.join(HERE, '..', 'core', 'gen', 'lid_data.c')

S = 20                 # Punkte je Feldeinheit
MARGIN = 1.5           # Rand um den Tisch (Feldeinheiten), damit "aussen" zusammenhaengt
MAX_PTS = 24

# Korrekturen von Hand: Tischnummer -> Nummern der Deckel aus dem Vorschaubild, die es NICHT geben soll
LIDS_OFF = {}
# Tischnummer -> Liste von Vielecken [(x, y), ...] in Feldeinheiten, die zusaetzlich Deckel sind
LIDS_ADD = {}


def fl(v): return float(v)


def table_geometry(d):
    """Liefert (strecken, kreise, punkte): feste Waende, Bumper, Orte von Spielelementen."""
    segs, circles, spots, kicks = [], [], [], []
    for idx, e in enumerate([x for x in d['elements'] if isinstance(x, dict)]):
        c = e['class']
        layer = int(e.get('layer', 0))
        if c == 'WallElement':
            x1, y1, x2, y2 = [fl(v) for v in e['position']]
            if fl(e.get('kick') or 0) > 0 and layer == 0 and not e.get('retractWhenHit'):
                kicks.append((idx, (x1, y1, x2, y2)))
            if layer != 0 or e.get('kill') is True or e.get('disabled') is True or e.get('retractWhenHit') is True \
               or e.get('ignoreBall') is True or e.get('id'):
                continue
            segs.append((x1, y1, x2, y2))
        elif c == 'WallPathElement':
            if layer != 0 or e.get('ignoreBall') or e.get('id'): continue
            p = [(fl(q[0]), fl(q[1])) for q in e['positions']]
            segs += [(p[i][0], p[i][1], p[i + 1][0], p[i + 1][1]) for i in range(len(p) - 1)]
        elif c == 'WallArcElement':
            if layer != 0 or e.get('ignoreBall') or e.get('id'): continue
            cx, cy = [fl(v) for v in e['center']]
            rx = ry = fl(e['radius']) if 'radius' in e else None
            if rx is None: rx, ry = fl(e['xradius']), fl(e['yradius'])
            a0, a1 = math.radians(fl(e.get('minangle', 0))), math.radians(fl(e.get('maxangle', 0)))
            n = 32
            p = [(cx + rx * math.cos(a0 + i * (a1 - a0) / n), cy + ry * math.sin(a0 + i * (a1 - a0) / n)) for i in range(n + 1)]
            segs += [(p[i][0], p[i][1], p[i + 1][0], p[i + 1][1]) for i in range(n)]
        elif c == 'BumperElement':
            if layer == 0 and fl(e.get('radius') or 0) > 0: circles.append((fl(e['position'][0]), fl(e['position'][1]), fl(e['radius'])))
        elif layer != 0:
            continue                                # Elemente anderer Ebenen (Tisch 7: die Flipper der oberen Ebene) zaehlen nicht
        elif c == 'RolloverGroupElement':
            spots += [(fl(r['position'][0]), fl(r['position'][1])) for r in e['rollovers']]
        elif c == 'DropTargetGroupElement':
            if 'positions' in e:
                spots += [((fl(q[0]) + fl(q[2])) / 2, (fl(q[1]) + fl(q[3])) / 2) for q in e['positions']]
            else:
                spots += [tuple(fl(v) for v in e['wallStart']), tuple(fl(v) for v in e['wallEnd'])]
        elif c in ('SpinnerElement', 'FlipperElement'):
            spots.append((fl(e['position'][0]), fl(e['position'][1])))
        elif c == 'SensorElement':
            r = [fl(v) for v in e['rect']]
            spots.append(((r[0] + r[2]) / 2, (r[1] + r[3]) / 2))
    return segs, circles, spots, kicks


def find_lids(d):
    W, H = fl(d.get('width', 20)), fl(d.get('height', 30))
    r = fl(d.get('ballradius', 0.5))
    segs, circles, spots, kicks = table_geometry(d)
    w, h = int((W + 2 * MARGIN) * S), int((H + 2 * MARGIN) * S)
    P = lambda x, y: (int(round((x + MARGIN) * S)), int(round((y + MARGIN) * S)))
    wall = np.zeros((h, w), np.uint8)
    for x1, y1, x2, y2 in segs: cv2.line(wall, P(x1, y1), P(x2, y2), 255, 2)
    solid = wall.copy()
    for cx, cy, cr in circles: cv2.circle(solid, P(cx, cy), int(cr * S), 255, -1)
    dist = cv2.distanceTransform(255 - solid, cv2.DIST_L2, 5)
    free = (dist > (0.8 * r + 0.05) * S).astype(np.uint8)
    n, lab = cv2.connectedComponents(free, connectivity=4)
    lx, ly = P(fl(d['launchPosition'][0]), fl(d['launchPosition'][1]))
    reach = np.zeros_like(free)
    if lab[ly, lx]:
        reach = (lab == lab[ly, lx]).astype(np.uint8)
    else:                                           # Abschusspunkt selbst zu eng: den naechsten freien Punkt nehmen
        ys, xs = np.nonzero(free)
        k = np.argmin((xs - lx) ** 2 + (ys - ly) ** 2)
        reach = (lab == lab[ys[k], xs[k]]).astype(np.uint8)
    covered = cv2.distanceTransform(1 - reach, cv2.DIST_L2, 5) <= (r + 0.12) * S
    rest = ((~covered) & (solid == 0)).astype(np.uint8)
    n, lab, stats, _ = cv2.connectedComponentsWithStats(rest, connectivity=4)
    wall_fat = cv2.dilate(wall, np.ones((5, 5), np.uint8))
    verts = [(s[0], s[1]) for s in segs] + [(s[2], s[3]) for s in segs]
    lids = []
    for i in range(1, n):
        x, y, bw, bh, area = stats[i]
        if x <= 1 or y <= 1 or x + bw >= w - 1 or y + bh >= h - 1: continue          # stoesst an den Rand: ausserhalb
        if area < 0.12 * S * S: continue
        comp = (lab == i).astype(np.uint8)
        # ein Spielelement MITTEN im Stueck heisst: dort spielt der Ball. Elemente am Rand zaehlen nicht - Ziele haengen an der
        # Wand eines Blocks, und die oberen Flipper sitzen mit dem Drehpunkt auf seiner Ecke.
        core = cv2.erode(comp, np.ones((5, 5), np.uint8))
        if any(0 <= P(*s)[1] < h and 0 <= P(*s)[0] < w and core[P(*s)[1], P(*s)[0]] for s in spots): continue
        ring = cv2.dilate(comp, np.ones((3, 3), np.uint8)) - comp
        if (wall_fat[ring > 0] > 0).mean() < 0.9: continue                             # Rand nicht ganz aus Wand: offen
        grown = cv2.dilate(comp, np.ones((3, 3), np.uint8))                             # bis auf die Mitte der Wandlinie
        cs, _ = cv2.findContours(grown, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
        c = max(cs, key=cv2.contourArea)
        eps = 1.6
        while True:
            a = cv2.approxPolyDP(c, eps, True)[:, 0, :]
            if len(a) <= MAX_PTS: break
            eps *= 1.3
        if len(a) < 3: continue
        poly = []
        for px, py in a:
            fx, fy = px / S - MARGIN, py / S - MARGIN
            best = min(verts, key=lambda v: (v[0] - fx) ** 2 + (v[1] - fy) ** 2)
            if math.hypot(best[0] - fx, best[1] - fy) < 0.22: fx, fy = best
            if not poly or (abs(poly[-1][0] - fx) + abs(poly[-1][1] - fy)) > 1e-6: poly.append((fx, fy))
        if len(poly) > 1 and abs(poly[0][0] - poly[-1][0]) + abs(poly[0][1] - poly[-1][1]) < 1e-6: poly.pop()
        if len(poly) < 3: continue
        lids.append(poly)
    return lids, segs, circles, kicks, (W, H)


def seg_dist(px, py, a, b):
    ax, ay = a; bx, by = b
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2)) if l2 else 0.0
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def kick_of(poly, kicks):
    for idx, (x1, y1, x2, y2) in kicks:
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        if min(seg_dist(mx, my, poly[i], poly[(i + 1) % len(poly)]) for i in range(len(poly))) < 0.15: return idx
    return -1


def preview(path, lids, segs, circles, size):
    W, H = size
    k = 30
    im = np.zeros((int(H * k) + 40, int(W * k) + 40, 3), np.uint8)
    Q = lambda x, y: (int(20 + x * k), int(20 + (H - y) * k))
    cols = [(60, 60, 255), (60, 200, 255), (60, 255, 120), (255, 200, 60), (255, 90, 200), (200, 255, 60)]
    for n, poly in enumerate(lids):
        pts = np.array([Q(*p) for p in poly], np.int32)
        cv2.fillPoly(im, [pts], tuple(int(c * 0.45) for c in cols[n % len(cols)]))
        cv2.polylines(im, [pts], True, cols[n % len(cols)], 2)
    for x1, y1, x2, y2 in segs: cv2.line(im, Q(x1, y1), Q(x2, y2), (210, 210, 210), 1)
    for cx, cy, cr in circles: cv2.circle(im, Q(cx, cy), int(cr * k), (210, 210, 210), 1)
    for n, poly in enumerate(lids):
        cx, cy = sum(p[0] for p in poly) / len(poly), sum(p[1] for p in poly) / len(poly)
        cv2.putText(im, str(n + 1), (Q(cx, cy)[0] - 8, Q(cx, cy)[1] + 8), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2)
    cv2.imwrite(path, im)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--preview', help='Ordner fuer die Vorschaubilder')
    a = ap.parse_args()
    pts, lid_rows, first = [], [], [0]
    for t, path in enumerate(TABLES, 1):
        d = json.load(open(path, encoding='utf-8'))
        lids, segs, circles, kicks, size = find_lids(d)
        if a.preview:
            os.makedirs(a.preview, exist_ok=True)
            preview(os.path.join(a.preview, 'lids_%d.png' % t), lids, segs, circles, size)
        keep = [p for n, p in enumerate(lids, 1) if n not in LIDS_OFF.get(t, ())] + [list(p) for p in LIDS_ADD.get(t, ())]
        for poly in keep:
            lid_rows.append((len(pts) // 2, len(poly), kick_of(poly, kicks)))
            for x, y in poly: pts += [int(round(x * 100)), int(round(y * 100))]
        first.append(len(lid_rows))
        print('Tisch %2d: %d Deckel (%s)' % (t, len(keep), ', '.join(str(len(p)) for p in keep)))
    o = ['/* Erzeugt von tools/lids.py - nicht von Hand aendern. Deckel: Flaechen, in die der Ball nie kommt. */',
         '#include "vp.h"', '',
         '/* Ecken in 1/100 Feldeinheit, x und y abwechselnd */',
         'const int16_t vp_lid_pts[] = {']
    for i in range(0, len(pts), 16): o.append('    ' + ', '.join(str(v) for v in pts[i:i + 16]) + ',')
    o += ['};', '', '/* erste Ecke (Index des Punktes), Zahl der Ecken, Element der Kick-Wand am Rand oder -1 */',
          'const vp_lid_t vp_lids[] = {']
    o += ['    { %d, %d, %d },' % r for r in lid_rows] or ['    { 0, 0, -1 },']
    o += ['};', '', '/* je Tisch: Index des ersten Deckels; ein Eintrag mehr als Tische */',
          'const uint8_t vp_lid_first[] = { %s };' % ', '.join(str(v) for v in first), '']
    open(OUT, 'w', encoding='utf-8', newline='\n').write('\n'.join(o))
    print('%s: %d Deckel, %d Ecken, %d Byte' % (os.path.normpath(OUT), len(lid_rows), len(pts) // 2, len(pts) * 2 + len(lid_rows) * 6))


if __name__ == '__main__':
    main()
