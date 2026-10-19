"""shotsweep.py - Schusskarte (tools/shottest.exe, SHOT_MAP) ueber mehrere Abstimmungen eines Tisches nebeneinander.

    python tools/shotsweep.py <tisch> [flipper:neigung ...]

Erste Spalte ist immer ARCADE 100:100 (die Physik des Originals als Massstab), danach REALISTIC mit den genannten
Abstimmungen (Vorgabe: Neigung 100..70 bei Flipper 100, dazu Flipper 115 und 130 bei Neigung 100). Je Ziel das
Trefferfenster in Prozent der Schuesse; '-' = kein Fenster.
"""
import os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shottest.exe')

def run(table, flip, slope, real):
    # ARCADE ist ohne Zufall: dort mehr Schlagstellen statt Wiederholungen, damit beide gleich viele Schuesse haben
    env = dict(os.environ, SHOT_MAP='1', SHOT_FRACS='150' if real else '600', SHOT_SEEDS='4' if real else '1')
    env.pop('SHOT_SUM', None)
    out = subprocess.run([EXE, str(table), str(flip), str(slope), '0', str(real)], capture_output=True, text=True, env=env).stdout
    rows, top = [], 0.0
    m = re.search(r'Gipfel ([\d.]+)', out)
    if m:
        top = float(m.group(1))
    for line in out.splitlines():
        m = re.match(r'\s+([A-Z])\s{4}(.{32}) +([\d.]+)', line)
        if m:
            rows.append((m.group(1), m.group(2).strip(), float(m.group(3))))
    return rows, top

def main():
    table = int(sys.argv[1])
    cfgs = [tuple(int(v) for v in a.split(':')) for a in sys.argv[2:]] or [(100, 100), (100, 90), (100, 80), (100, 70), (115, 100), (130, 100)]
    jobs = [(100, 100, 0)] + [(f, s, 1) for f, s in cfgs]
    with ThreadPoolExecutor(max_workers=8) as ex:
        res = list(ex.map(lambda j: run(table, *j), jobs))
    head = ['ARC'] + ['%d:%d' % c for c in cfgs]
    print('Tisch %d   Fenster in %% der Schuesse (Flipper:Neigung)' % table)
    print('  %-3s %-30s' % ('Art', 'Ziel') + ''.join('%9s' % h for h in head))
    names = [(k, n) for k, n, _ in res[0][0]]
    for i, (k, n) in enumerate(names):
        vals = [r[0][i][2] if i < len(r[0]) else 0 for r in res]
        print('  %-3s %-30s' % (k, n or '(ohne Namen)') + ''.join('%9s' % ('-' if v == 0 else '%.2f' % v) for v in vals))
    print('  %-34s' % 'ohne Fenster' + ''.join('%9d' % sum(1 for x in r[0] if x[2] == 0) for r in res))
    print('  %-34s' % 'schmal (< 0,5 %)' + ''.join('%9d' % sum(1 for x in r[0] if 0 < x[2] < 0.5) for r in res))
    print('  %-34s' % 'Gipfel' + ''.join('%9.1f' % r[1] for r in res))

if __name__ == '__main__':
    main()
