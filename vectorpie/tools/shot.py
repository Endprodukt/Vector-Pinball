#!/usr/bin/env python3
"""shot.py - macht aus einer Strichliste des Spiels (VP_SHOT_AT=n -> shot_<n>.txt) ein PNG.

    python tools/shot.py shot_120.txt [bild.png] [hoehe]

Die Liste enthaelt je Zeile einen Strich in Platineneinheiten (0..4095, Ursprung unten links) und seine Farbe:
x0 y0 x1 y1 rrggbb. Die erste Zeile nennt die Form des Monitors (4:3 oder 3:4). Striche addieren sich wie auf der Roehre.
Braucht nichts ausser Python. Ein Pruefbild fuer Geometrie und Farbe - Helligkeit und Flimmern zeigt nur die Roehre.
"""
import math
import struct
import sys
import zlib


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else src.rsplit('.', 1)[0] + '.png'
    h = int(sys.argv[3]) if len(sys.argv) > 3 else 900
    lines, portrait = [], False
    for ln in open(src):
        if ln.startswith('#'):
            portrait = '3:4' in ln
            continue
        p = ln.split()
        if len(p) == 5:
            lines.append((int(p[0]), int(p[1]), int(p[2]), int(p[3]), int(p[4], 16)))
    w = h * 3 // 4 if portrait else h * 4 // 3
    buf = [bytearray(w * 3) for _ in range(h)]
    sx, sy = (w - 1) / 4095.0, (h - 1) / 4095.0

    def plot(x, y, r, g, b, k):
        if 0 <= x < w and 0 <= y < h:
            row, i = buf[y], x * 3
            row[i] = min(255, row[i] + int(r * k))
            row[i + 1] = min(255, row[i + 1] + int(g * k))
            row[i + 2] = min(255, row[i + 2] + int(b * k))

    for x0, y0, x1, y1, c in lines:
        r, g, b = c >> 16, (c >> 8) & 255, c & 255
        ax, ay, bx, by = x0 * sx, (4095 - y0) * sy, x1 * sx, (4095 - y1) * sy
        n = int(max(abs(bx - ax), abs(by - ay))) + 1
        for i in range(n + 1):
            t = i / n
            fx, fy = ax + (bx - ax) * t, ay + (by - ay) * t
            ix, iy = math.floor(fx), math.floor(fy)
            ux, uy = fx - ix, fy - iy
            plot(ix, iy, r, g, b, (1 - ux) * (1 - uy))
            plot(ix + 1, iy, r, g, b, ux * (1 - uy))
            plot(ix, iy + 1, r, g, b, (1 - ux) * uy)
            plot(ix + 1, iy + 1, r, g, b, ux * uy)

    raw = b''.join(b'\x00' + bytes(row) for row in buf)

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    open(out, 'wb').write(png)
    print(f'{out}: {len(lines)} Striche, {w}x{h}')


if __name__ == '__main__':
    main()
