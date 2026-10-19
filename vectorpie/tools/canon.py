"""canon.py - eine Strichliste (shot_<n>.txt) als geordnete Menge: fuer tools/regress.sh check -u.
Jeder Strich mit seinen Enden in fester Reihenfolge (er darf rueckwaerts gefahren werden), die Striche sortiert."""
import sys

lines = []
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) == 5:
        a, b = ' '.join(p[0:2]), ' '.join(p[2:4])
        lines.append(' '.join(sorted([a, b]) + [p[4]]))
print('\n'.join(sorted(lines)))
