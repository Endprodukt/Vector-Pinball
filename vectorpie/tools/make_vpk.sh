#!/bin/sh
# make_vpk.sh - das Spiel als VectorPie-Paket: build/vpinball-<version>.vpk
#
# Im Ordner vectorpie/ nach dem Pi-Build aufrufen (cmake --build --preset pi-clang). Das Paket enthaelt das Pi-Programm,
# game.ini und data/ (Tonaufnahmen), dazu marquee.png, wenn es eins gibt. Gepackt wird mit vp-pack des SDK.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
stage="$root/build/pkg/vpinball"
[ -f "$root/build/pi/pinball" ] || { echo "make_vpk: kein Pi-Build in build/pi" >&2; exit 1; }
rm -rf "$root/build/pkg"
mkdir -p "$stage"
cp "$root/build/pi/pinball" "$root/game.ini" "$stage/"
[ -f "$root/marquee.png" ] && cp "$root/marquee.png" "$stage/"
cp -r "$root/build/pi/data" "$stage/data"
sh "$root/sdk-repo/sdk/tools/vp-pack/vp-pack.sh" "$stage" "$root/build"

# Unter Windows gebaut ist das Programm im Archiv nicht als ausfuehrbar markiert (das Dateisystem kennt das Zeichen
# nicht, das vp-pack setzt): hier nachtragen.
vpk=$(ls "$root"/build/vpinball-*.vpk | tail -n 1)
python - "$vpk" <<'PY'
import io, sys, tarfile
path = sys.argv[1]
out = io.BytesIO()
with tarfile.open(path, "r:gz") as old, tarfile.open(fileobj=out, mode="w:gz") as new:
    for member in old.getmembers():
        if member.name == "vpinball/pinball":
            member.mode = 0o755
        new.addfile(member, old.extractfile(member) if member.isfile() else None)
open(path, "wb").write(out.getvalue())
PY
echo "make_vpk: $vpk"
