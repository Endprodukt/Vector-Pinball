#!/usr/bin/env python3
"""
spinball_map.py - holt das Spielfeld von Spinball (GCE 1983) aus dem ROM und schreibt tables/table10.json.

Aufruf:  python tools/spinball_map.py F:/Vide/cartridges/SPINBALL.BIN [--png bild.png] [--list]

Was im ROM steht (alles in Bildschirmeinheiten y, x von -128..127, Ursprung in der Bildmitte):

  $177A  Spielfeld: sechs Linienzuege, Ende mit einem negativen Zaehlbyte. Je Zug: Zahl der Striche, Startpunkt (y, x),
         dann je Strich 7 Byte: Zielpunkt (y, x), Typ, Zeiger auf ein Zustandsbyte, Zielpunkt noch einmal (der Zeichner
         bei $0FED bildet daraus die Differenz zum naechsten Punkt). Bit 6 des Zustandsbytes = Strich sichtbar. Zeigt der
         Zeiger ins ROM, ist es eine unsichtbare Bewegung; zeigt er ins RAM ($C8D2..$C8F8), gehoert der Strich zu einem
         Spielfeldteil, das ein- und ausgeschaltet werden kann.
  $1612  vier Flipper ($C8EC..$C8EF), je drei Stellungen als Linienzug im selben Format (4 Striche).
  $1413  vier Bumper ($C8D7..$C8DA): Lichter in den Sechsecken (die Sechsecke selbst stehen im Spielfeld).
  $1160  drei Rauten in den Kanaelen oben ($C8F0..$C8F2): ab (98, -24) im Abstand von 16.
  $14A9  vier Spinner ($C945, $C948, $C94B, $C94E): Rechtecke, deren Breite sich mit dem Zustand aendert.
  $1472  fuenf Zielreihen ($C936, $C939, $C93C, $C93F, $C942): eine Linie, deren Muster aus den Zustandsbits kommt -
         jedes gesetzte Bit ist ein stehendes Ziel (2, 5, 3, 4, 2 Ziele).
  $13E9  blinkende Pfeile: Ballretter in den Aussenbahnen ($C8F7, $C8F8) und Bonuspfeile der Spinner.
  $1550  Ballteiler: Stern in der Tischmitte (-10, -6), Zustand $C89F.
  $13B5  Schleudern ($C8DF, $C8E0): Zuckbewegung beim Treffer.

Die Spielfeldtabelle wird hier direkt gelesen. Die uebrigen Teile haengen als Ketten relativer Bewegungen mit
wechselndem Massstab aneinander; ihre Lage stammt aus einem Lauf des ROMs in tools/vecdump.py (Strahlweg mitgeschrieben)
und steht unten als Zahlen. Fuer den Port wird umgerechnet: 192 Einheiten Breite = 20 Feldeinheiten.

Nicht aus dem ROM: die unsichtbaren Waende unter dem Tisch (Ball verloren), der Boden der Startbahn und die beiden
Fuehler fuer die Sperre der Startbahn - das Original regelt das im Code. Die Bedeutung des Typ-Bytes ist nur fuer die
Bumper ($82) und die Schleudern ($77, $79) gedeutet, alle anderen Striche sind hier gewoehnliche Waende.
"""
import argparse, json, math, os, sys

FIELD = 0x177A
K = 20.0 / 192.0                 # Feldeinheiten je ROM-Einheit
Y0 = 116                         # ROM-y, das auf Feld-y 0 faellt (unter den Flippern)

# Zustandsbytes mit besonderer Rolle
F_BARRIER = 0xC8EB               # Sperre oben an der Startbahn
F_POST = 0xC8EA                  # Dreieck zwischen den unteren Flippern
F_SAVE_L, F_SAVE_R = 0xC8F7, 0xC8F8
F_SLING_L, F_SLING_R = 0xC8DF, 0xC8E0
F_BUMPERS = (0xC8D7, 0xC8D8, 0xC8D9, 0xC8DA)

# Flipper: Zustandsbyte -> drei Stellungen (unten, Mitte, oben)
FLIPPERS = {0xC8EC: (0x163B, 0x1675, 0x16AF), 0xC8ED: (0x161E, 0x1658, 0x1692),
            0xC8EE: (0x16CC, 0x1706, 0x1740), 0xC8EF: (0x16E9, 0x1723, 0x175D)}

# Aus dem Lauf in vecdump.py (ROM-Einheiten y, x)
DIAMONDS = [(100, -24), (100, -8), (100, 8)]                                   # Mitte der Rauten
# Spinner: Mitte, halbe Laenge, Winkel der langen Seite - oben links, unten links, unten rechts, rechts Mitte
SPINNERS = [((88.9, -84.4), 3.7, 33.7), ((-64.9, -78.0), 4.1, 0.0), ((-64.9, 66.0), 4.1, 0.0), ((-6.2, 68.0), 2.9, -45.0)]
SPLITTER = (-10, -6)
# Zielreihen: Start, Schritt je Ziel, Zahl der Ziele (Bits im Zustandsbyte beim Spielstart)
TARGET_ROWS = [('DropLowRight', (-18.8, 69.3), (-7.1, 5.9), 2), ('DropTopRight', (100.5, 17.8), (1.48, 10.05), 5),
               ('DropTopLeft', (101.1, -37.1), (2.4, -10.6), 3), ('DropMidLeft', (-6.0, -65.5), (-7.1, -6.5), 4),
               ('DropMidRight', (-10.9, 35.5), (-1.2, 10.6), 2)]


def s8(v): return v - 256 if v > 127 else v


def read_polys(rom, at=FIELD):
    """Liefert [(start, [(ziel, typ, zeiger), ...]), ...]"""
    polys, x = [], at
    while not rom[x] & 0x80:
        n = rom[x]; x += 1
        start = (s8(rom[x]), s8(rom[x + 1])); x += 2
        segs = []
        for _ in range(n):
            segs.append(((s8(rom[x]), s8(rom[x + 1])), rom[x + 2], (rom[x + 3] << 8) | rom[x + 4]))
            x += 7
        x -= 2
        polys.append((start, segs))
    return polys


def read_flipper(rom, at):
    pts = [(s8(rom[at]), s8(rom[at + 1]))]
    x = at + 2
    for _ in range(4):
        pts.append((s8(rom[x]), s8(rom[x + 1]))); x += 7
    return pts


def fx(p): return round((p[1] + 96) * K, 3)
def fy(p): return round((p[0] + Y0) * K, 3)
def fp(p): return [fx(p), fy(p)]


def build(rom):
    polys = read_polys(rom)
    special = {F_BARRIER, F_POST, F_SAVE_L, F_SAVE_R, F_SLING_L, F_SLING_R} | set(F_BUMPERS)
    el, groups = [], {}
    for start, segs in polys:
        prev, run = start, None
        for p, typ, ptr in segs:
            if ptr < 0xC800:                                    # unsichtbare Bewegung
                run = None
            elif ptr in special:
                groups.setdefault(ptr, []).append((prev, p, typ)); run = None
            else:
                if run is None or run['flag'] != ptr:
                    run = {'flag': ptr, 'pts': [prev]}; el.append(run)
                run['pts'].append(p)
            prev = p
    out = []
    for r in el:
        out.append({'class': 'WallPathElement', 'positions': [fp(p) for p in r['pts']]})

    def walls(flag, **kw):
        res = []
        for i, (a, b, _t) in enumerate(groups[flag]):
            w = {'class': 'WallElement', 'position': fp(a) + fp(b)}
            w.update({k: (v % (i + 1) if k == 'id' and '%' in v else v) for k, v in kw.items()})
            res.append(w)
        return res

    out += walls(F_BARRIER, id='LaunchBarrier%d', disabled=True, color=[128, 128, 128])
    out += walls(F_POST, id='CenterPost%d', disabled=True, kick=2.5, color=[255, 221, 0])
    out += walls(F_SAVE_L, id='BallSaver-left', kick=6.0, retractWhenHit=True, disabled=True, color=[0, 255, 0])
    out += walls(F_SAVE_R, id='BallSaver-right', kick=6.0, retractWhenHit=True, disabled=True, color=[0, 255, 0])
    out += walls(F_SLING_L, kick=3.5, color=[0, 0, 255])
    out += walls(F_SLING_R, kick=3.5, color=[0, 0, 255])

    # Bumper: Sechsecke aus dem Spielfeld, Mitte und Radius aus den Ecken
    centers = []
    for i, flag in enumerate(F_BUMPERS):
        pts = [a for a, _b, _t in groups[flag]]
        cy = sum(p[0] for p in pts) / len(pts); cx = sum(p[1] for p in pts) / len(pts)
        r = max(math.hypot(p[0] - cy, p[1] - cx) for p in pts)
        centers.append((cy, cx))
        out.append({'class': 'BumperElement', 'id': 'Bumper%d' % (i + 1), 'position': fp((cy, cx)), 'radius': round(r * K * 0.96, 3),
                    'sides': 6, 'kick': 3.0, 'color': [0, 153, 255]})   # Punkte vergibt vp_table10.c
    out.append({'class': 'RolloverGroupElement', 'id': 'BumperLights', 'ignoreBall': True, 'radius': 0.26, 'color': [255, 255, 255],
                'rollovers': [{'position': fp(c)} for c in centers]})
    out.append({'class': 'RolloverGroupElement', 'id': 'Chutes', 'radius': 0.4, 'color': [0, 224, 224], 'ignoreBall': True,
                'rollovers': [{'position': fp(c)} for c in DIAMONDS]})
    out.append({'class': 'RolloverGroupElement', 'id': 'Splitter', 'radius': 0.6, 'color': [255, 221, 0], 'ignoreBall': True,
                'rollovers': [{'position': fp(SPLITTER)}]})
    for i, (c, half, ang) in enumerate(SPINNERS):
        out.append({'class': 'SpinnerElement', 'id': 'Spinner%d' % (i + 1), 'position': fp(c), 'radius': round(half * K, 3),
                    'barAngle': ang, 'color': [255, 255, 255]})
    for name, st, step, n in TARGET_ROWS:
        pos = []
        for i in range(n):
            a = (st[0] + step[0] * (i + 0.08), st[1] + step[1] * (i + 0.08))
            b = (st[0] + step[0] * (i + 0.86), st[1] + step[1] * (i + 0.86))
            pos.append(fp(a) + fp(b))
        out.append({'class': 'DropTargetGroupElement', 'id': name, 'color': [0, 255, 0], 'reset': 3.0, 'positions': pos})

    # Flipper: Drehpunkt = Mitte der kurzen Seite, Spitze = Mitte der gegenueberliegenden
    for flag in sorted(FLIPPERS):
        fr = [read_flipper(rom, a) for a in FLIPPERS[flag]]
        def axis(q):
            piv = ((q[0][0] + q[3][0]) / 2, (q[0][1] + q[3][1]) / 2)
            tip = ((q[1][0] + q[2][0]) / 2, (q[1][1] + q[2][1]) / 2)
            return piv, tip
        piv, tip = axis(fr[0]); _p2, top = axis(fr[2])
        right = tip[1] < piv[1]
        length = math.hypot(tip[0] - piv[0], tip[1] - piv[1]) * K
        def ang(t):
            dx = (t[1] - piv[1]) * (-1 if right else 1)
            return math.degrees(math.atan2(t[0] - piv[0], dx))
        out.append({'class': 'FlipperElement', 'position': fp(piv), 'length': round(-length if right else length, 3),
                    'minangle': round(ang(tip), 1), 'maxangle': round(ang(top), 1), 'upspeed': 7, 'downspeed': 3})

    # Zutaten, die nicht im ROM stehen
    W = lambda a, b, **k: dict({'class': 'WallElement', 'position': fp(a) + fp(b)}, **k)
    out += [
        W((-86, 84), (-86, 96)),                                               # Boden der Startbahn (Feder)
        W((-114, -100), (-114, 100), kill=True, color=[0, 0, 0]),
        W((-86, -100), (-114, -100), kill=True, color=[0, 0, 0]),
        W((-90, 100), (-114, 100), kill=True, color=[0, 0, 0]),
        W((-90, 84), (-90, 100), kill=True, color=[0, 0, 0]),
        {'class': 'SensorElement', 'id': 'LaunchBarrierSensor', 'rect': fp((106, -60)) + fp((127, 40))},
        {'class': 'SensorElement', 'id': 'LaunchBarrierRetract', 'rect': fp((-86, 84)) + fp((-40, 96))},
    ]
    return {
        'name': 'Spinball', 'delegate': 'Field10Delegate', 'version': 2,
        'width': 20, 'height': round((127 + Y0) * K + 0.2, 2), 'gravity': 4, 'targetTimeRatio': 2.3, 'numballs': 5,
        'ballradius': 0.36, 'ballcolor': [192, 192, 224],
        'launchPosition': fp((-80, 90)), 'launchVelocity': [0, 15.5], 'launchVelocityRandomDelta': [0, 0.6],
        'launchDeadZone': fp((-87, 84)) + fp((-74, 96)),
        'elements': out,
    }, polys


def draw_png(table, path):
    from PIL import Image, ImageDraw
    s = 34
    im = Image.new('RGB', (int(table['width'] * s) + 20, int(table['height'] * s) + 20)); d = ImageDraw.Draw(im)
    P = lambda x, y: (10 + x * s, 10 + (table['height'] - y) * s)
    for e in table['elements']:
        c = e['class']; col = tuple(e.get('color', [200, 200, 200])[:3])
        if col == (0, 0, 0): col = (70, 0, 0)
        if c == 'WallPathElement':
            d.line([P(*p) for p in e['positions']], fill=col)
        elif c == 'WallElement':
            p = e['position']; d.line([P(p[0], p[1]), P(p[2], p[3])], fill=col)
        elif c == 'DropTargetGroupElement':
            for p in e['positions']: d.line([P(p[0], p[1]), P(p[2], p[3])], fill=col, width=2)
        elif c in ('BumperElement', 'SpinnerElement'):
            x, y = e['position']; r = e['radius']
            a, b = P(x - r, y + r), P(x + r, y - r); d.ellipse([a, b], outline=col)
        elif c == 'RolloverGroupElement':
            for r_ in e['rollovers']:
                x, y = r_['position']; r = e['radius']
                d.ellipse([P(x - r, y + r), P(x + r, y - r)], outline=col)
        elif c == 'FlipperElement':
            x, y = e['position']; L = e['length']
            for a in (e['minangle'], e['maxangle']):
                t = math.radians(a)
                d.line([P(x, y), P(x + L * math.cos(t), y + abs(L) * math.sin(t))], fill=(0, 255, 0))
        elif c == 'SensorElement':
            r = e['rect']; a, b = P(r[0], r[3]), P(r[2], r[1]); d.rectangle([a, b], outline=(60, 60, 0))
    x, y = table['launchPosition']; r = table['ballradius']
    d.ellipse([P(x - r, y + r), P(x + r, y - r)], outline=(255, 255, 255))
    im.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tables', 'table10.json'))
    ap.add_argument('--png'); ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    rom = open(a.rom, 'rb').read()
    if rom[0x11:0x19] != b'SPINBALL':
        sys.exit('das ist nicht Spinball')
    table, polys = build(rom)
    if a.list:
        for start, segs in polys:
            print('Start y=%d x=%d' % start)
            for p, typ, ptr in segs:
                print('   y=%4d x=%4d  Typ %02X  %s' % (p[0], p[1], typ, 'Bewegung' if ptr < 0xC800 else 'Zustand $%04X' % ptr))
    json.dump(table, open(a.out, 'w'), indent=1)
    print('%s: %d Elemente' % (os.path.normpath(a.out), len(table['elements'])))
    if a.png: draw_png(table, a.png)


if __name__ == '__main__':
    main()
