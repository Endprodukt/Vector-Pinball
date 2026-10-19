#!/usr/bin/env python3
"""
song2wav.py - macht aus eigenen Liedern (MP3, OGG, FLAC, WAV ...) die WAV-Dateien, die das Spiel abspielen kann.

Das Spiel liest nur WAV (16 Bit); fuer einen MP3-Dekoder ist auf dem UVMC2 kein Speicher uebrig. Die Buchse laeuft stereo
mit 32 kHz, darum wird genau dahin gewandelt - so ist die Datei am kleinsten und das Nachladen von der Karte am leichtesten
(128 KB je Sekunde, ein Lied von 4 Minuten etwa 30 MB).

Aufruf (im Ordner vectrex/):
    python tools/song2wav.py E:/pinball/snd            alle Lieder "<zahl>_name.mp3" usw. im Ordner wandeln
    python tools/song2wav.py lied.mp3 [noch_eins.ogg]  einzelne Dateien; die WAV-Datei landet daneben
    python tools/song2wav.py lied.mp3 --as 3           ... und heisst 3_lied.wav (Tisch 3; 0 = Menue)

braucht: pip install soundfile numpy scipy
"""
import argparse, os, re, sys, wave
from math import gcd

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

RATE = 32000
KINDS = ('.mp3', '.ogg', '.flac', '.aiff', '.aif', '.wav', '.opus')


def convert(src, dst):
    d, sr = sf.read(src, dtype='float32', always_2d=True)
    d = d[:, :2]
    if sr != RATE:
        g = gcd(sr, RATE)
        d = resample_poly(d, RATE // g, sr // g, axis=0)
    peak = float(np.max(np.abs(d))) if len(d) else 0.0
    if peak > 1.0:
        d = d / peak
    pcm = np.clip(np.round(d * 32767.0), -32768, 32767).astype('<i2')
    with wave.open(dst, 'wb') as w:
        w.setnchannels(d.shape[1]); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(pcm.tobytes())
    print('%s -> %s  (%d:%02d, %.1f MB)' % (os.path.basename(src), os.path.basename(dst), len(pcm) // RATE // 60,
                                             len(pcm) // RATE % 60, os.path.getsize(dst) / 1048576.0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('paths', nargs='+')
    ap.add_argument('--as', dest='slot', type=int, help='Nummer voranstellen: 0 = Menue, 1-10 = Tisch')
    a = ap.parse_args()
    jobs = []
    for p in a.paths:
        if os.path.isdir(p):
            for n in sorted(os.listdir(p)):
                stem, ext = os.path.splitext(n)
                if ext.lower() in KINDS and ext.lower() != '.wav' and re.match(r'^(10|[0-9])_', n):
                    jobs.append((os.path.join(p, n), os.path.join(p, stem + '.wav')))
        else:
            stem = os.path.splitext(os.path.basename(p))[0]
            if a.slot is not None:
                stem = '%d_%s' % (a.slot, re.sub(r'^(10|[0-9])_', '', stem))
            dst = os.path.join(os.path.dirname(p), stem + '.wav')
            if os.path.abspath(dst) == os.path.abspath(p):
                dst = os.path.join(os.path.dirname(p), stem + '_32k.wav')
            jobs.append((p, dst))
    if not jobs:
        sys.exit('nichts zu wandeln (im Ordner muessen die Lieder "<zahl>_name.mp3" heissen)')
    for src, dst in jobs:
        convert(src, dst)


if __name__ == '__main__':
    main()
