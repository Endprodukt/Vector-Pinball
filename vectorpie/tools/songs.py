#!/usr/bin/env python3
"""songs.py - eigene Lieder (MP3, OGG, FLAC ...) fuer den Automaten wandeln: WAV, 16 Bit, stereo, 16 kHz.

    python tools/songs.py <ordner mit "<zahl>_name.mp3"> <zielordner>

Die Arbeit macht ../vectrex/tools/song2wav.py; hier nur mit 16 kHz statt 32 (Ollis Vorgabe 2026-10-10: reicht fuer den
Automaten voellig, halbe Dateigroesse). Die Quelldateien bleiben, wo sie sind. Braucht soundfile, numpy, scipy (auf Ollis
Rechner: Python 3.10).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'vectrex', 'tools'))
import song2wav  # noqa: E402

song2wav.RATE = 16000


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    for name in sorted(os.listdir(src)):
        base, ext = os.path.splitext(name)
        if ext.lower() in song2wav.KINDS and ext.lower() != '.wav' and base[:1].isdigit() and '_' in base:
            song2wav.convert(os.path.join(src, name), os.path.join(dst, base + '.wav'))


if __name__ == '__main__':
    main()
