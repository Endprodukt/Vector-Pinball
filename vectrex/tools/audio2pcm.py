#!/usr/bin/env python3
"""
audio2pcm.py - macht aus den Aufnahmen des Originals (.ogg unter ../app) die WAV-Dateien fuer die Buchse des UVMC2
und den Simulator:  sdcard/pinball/snd/*.wav   (Namen und Reihenfolge: game/pcm.c)

Aufruf (im Ordner vectrex/):  python tools/audio2pcm.py        braucht: pip install soundfile numpy scipy

Alles wird mono, 16 Bit. Effekte und Flaechenklang mit 32 kHz (der Takt der Buchse), die 72 Sekunden lange
Schlagzeugspur mit 24 kHz, damit alles zusammen in das PSRAM des UVMC2 passt (7,5 MB nutzbar).
Die Lautstaerke der Dateien bleibt unveraendert; das Spiel setzt die Pegel des Originals (VPSoundpool.java).
Eigene Aufnahmen: einfach eine WAV-Datei gleichen Namens hinlegen (16 Bit, mono oder stereo, beliebige Rate).
"""
import os, sys, wave
from math import gcd

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.normpath(os.path.join(HERE, '..', '..', 'app', 'src', 'main'))
OUT = os.path.normpath(os.path.join(HERE, '..', 'sdcard', 'pinball', 'snd'))

SOUNDS = [
    ('flipper.wav',  'assets/audio/misc/flipper1.ogg', 32000),
    ('launch.wav',   'assets/audio/misc/andBounce2.ogg', 32000),
    ('message.wav',  'assets/audio/misc/message2.ogg', 32000),
    ('start.wav',    'assets/audio/misc/startup1.ogg', 32000),
    ('rollover.wav', 'assets/audio/misc/rolloverE.ogg', 32000),
    ('ding1.wav',    'assets/audio/bumper/dinga1.ogg', 32000),
    ('ding2.wav',    'assets/audio/bumper/dingc1.ogg', 32000),
    ('ding3.wav',    'assets/audio/bumper/dingc2.ogg', 32000),
    ('ding4.wav',    'assets/audio/bumper/dingd1.ogg', 32000),
    ('ding5.wav',    'assets/audio/bumper/dinge1.ogg', 32000),
    ('ding6.wav',    'assets/audio/bumper/dingg1.ogg', 32000),
    ('pad.wav',      'res/raw/androidpad.ogg', 32000),
    ('drums.wav',    'res/raw/drumbassloop.ogg', 24000),
]


def load(path, rate):
    d, sr = sf.read(os.path.join(APP, path), dtype='float64')
    if d.ndim > 1:
        d = d.mean(axis=1)
    if sr != rate:
        g = gcd(sr, rate)
        d = resample_poly(d, rate // g, sr // g)
    # Stille am Ende kostet nur Speicher und eine Stimme im Mischer: abschneiden (unter -66 dB), kurz ausblenden
    nz = np.nonzero(np.abs(d) > 0.0005)[0]
    if len(nz):
        d = d[:min(len(d), nz[-1] + rate // 50)]
    fade = min(len(d), rate // 200)
    d[-fade:] *= np.linspace(1.0, 0.0, fade)
    return np.clip(np.round(d * 32767.0), -32768, 32767).astype('<i2')


def main():
    os.makedirs(OUT, exist_ok=True)
    old = os.path.join(OUT, 'sounds.pcm')
    if os.path.exists(old):
        os.remove(old)
    total = 0
    for name, src, rate in SOUNDS:
        d = load(src, rate)
        with wave.open(os.path.join(OUT, name), 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
            w.writeframes(d.tobytes())
        size = os.path.getsize(os.path.join(OUT, name))
        total += (size + 3) & ~3
        print('%-13s <- %-18s %6d Hz %7.2f s %8d Bytes' % (name, os.path.basename(src), rate, len(d) / rate, size))
    print('%s: %.2f MB' % (OUT, total / 1048576.0))
    if total > 7.5 * 1048576:
        sys.exit('zusammen zu gross fuer das PSRAM (7,5 MB)')


if __name__ == '__main__':
    main()
