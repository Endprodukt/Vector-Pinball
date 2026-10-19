#!/bin/sh
# regress.sh - Pruefstand fuer Umbauten, die das Bild nicht aendern duerfen (Aufraeumen, Umstellen).
#
#   sh tools/regress.sh base      schreibt die Strichlisten fester Szenen nach build/regress/base/
#   sh tools/regress.sh check     schreibt sie nach build/regress/new/ und vergleicht mit base
#   sh tools/regress.sh check -u  ... vergleicht ungeordnet (die Striche als Menge): fuer Umbauten, die nur die
#                                 Zeichenreihenfolge aendern duerfen
#
# Jede Szene laeuft mit fester Bildzeit und festem Zufall (VP_FIXED_DT), ist also wiederholbar; verglichen werden die
# Strichlisten (VP_SHOT_AT) der genannten Bilder. Dazu laeuft der ganze Ablauf einmal durch (Muenze, START, Spiel bis
# zum Ende), nur auf Absturz. Braucht build/win/pinball.exe.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
exe="$root/build/win/pinball.exe"
mode=${1:-check}
unordered=${2:-}
out="$root/build/regress/$([ "$mode" = base ] && echo base || echo new)"
rm -rf "$out"; mkdir -p "$out"
cd "$out" || exit 1
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy VP_NOSAVE=1 VP_FIXED_DT=1 VP_HZ=1000

scene() {   # name bild [VAR=wert ...]
    name=$1; at=$2; shift 2
    env "$@" VP_FRAMES=$((at + 2)) VP_SHOT_AT=$at "$exe" --windowed > /dev/null 2>&1 || echo "regress: $name: rc=$?"
    mv "shot_$at.txt" "$name.txt" 2>/dev/null || echo "regress: $name: kein Bild"
}
scene title     200 VP_X=0
scene demo      300 VP_ATTRACT=1
scene scores     30 VP_ATTRACT=2
scene players    40 VP_SELECT=1 VP_KEYS=5:16,8:0,12:16,16:0,20:8,24:0
scene table      40 VP_SELECT=2
scene service    30 VP_PAGE=1 VP_ITEM=3
scene controls   60 VP_PAGE=1 VP_ITEM=14 VP_KEYS=10:32,14:0
scene about      30 VP_ABOUT=1
scene entry      90 VP_ENTRY=1 VP_KEYS=10:8,13:0,20:32,23:0,30:1,33:0,36:1,39:0,42:1,45:0,60:32,63:0
scene t1full     60 VP_TABLE=1
scene t1play    420 VP_TABLE=1 VP_AUTOPLAY=1
scene t2        500 VP_TABLE=2 VP_AUTOPLAY=1
scene t7        400 VP_TABLE=7 VP_AUTOPLAY=1 VP_ZOOM=0
scene t7z       900 VP_TABLE=7 VP_AUTOPLAY=1
scene t8        300 VP_TABLE=8 VP_AUTOPLAY=1 VP_ZOOM=0
scene t8z       420 VP_TABLE=8 VP_AUTOPLAY=1
scene t9        500 VP_TABLE=9 VP_AUTOPLAY=1
scene t10       300 VP_TABLE=10 VP_AUTOPLAY=1
scene vert      400 VP_TABLE=5 VP_AUTOPLAY=1 VP_VERTICAL=1
scene over      200 VP_INTRO=0 VP_PAGE=0
env VP_AUTOPLAY=1 VP_KEYS=20:16,24:0,60:32,64:0,130:32,134:0,210:32,214:0 VP_FRAMES=20000 "$exe" --windowed > /dev/null 2>&1 || echo "regress: Ablauf: rc=$?"

[ "$mode" = base ] && { echo "regress: Basis geschrieben: $out"; exit 0; }
fail=0
for f in "$root"/build/regress/base/*.txt; do
    n=$(basename "$f")
    if [ "$unordered" = "-u" ]; then
        # als Menge, und ein Strich darf rueckwaerts gefahren werden: beide Enden in feste Reihenfolge bringen
        if ! cmp -s <(python "$root/tools/canon.py" "$f") <(python "$root/tools/canon.py" "$out/$n"); then echo "regress: $n weicht ab (als Menge)"; fail=1; fi
    else
        if ! cmp -s "$f" "$out/$n"; then echo "regress: $n weicht ab"; fail=1; fi
    fi
done
[ $fail = 0 ] && echo "regress: alle Szenen gleich"
exit $fail
