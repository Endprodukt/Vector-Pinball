# Aufbau der Entwicklungsumgebung (Windows)

Alle Befehle in Git Bash im Ordner `F:\VectorPinballVectrexPort\vectrex`.

## Einmalig

1. UVMC2 Starter Kit: Das Makefile nimmt `vectrex/uvmc2-starter-kit/`, falls vorhanden, sonst das des Dracula-Ports
   (`F:\DraculaVectrexPort\uvmc2-starter-kit`). Eigenes Kit klonen:
   ```
   git clone --recursive https://github.com/tullulah/uvmc2-starter-kit.git uvmc2-starter-kit
   git clone --depth 1 --branch 2.2.0 https://github.com/raspberrypi/pico-sdk.git uvmc2-starter-kit/third_party/pico-sdk
   ```
   Gebaut und geprueft mit Kit `a674bb8`, pico-sdk 2.2.0.
2. Werkzeuge wie beim Dracula-Port: Arm GNU Toolchain, CMake, Ninja, Python 3, Rust mit
   `rustup target add thumbv8m.main-none-eabi`, MSYS2 MINGW64 (gcc + SDL2) fuer den Simulator.

## Bauen

```
source tools/env.sh
mingw32-make host < /dev/null     # Simulator  -> build/pinball_host.exe
mingw32-make test < /dev/null     # Dauerlauf des Spielkerns ueber alle Tische
mingw32-make uvm2 < /dev/null     # .um2       -> build_uvm2/pinball.um2
mingw32-make data < /dev/null     # Tischdaten neu erzeugen (nach Aenderungen an tools/tables2c.py oder den JSON-Dateien)
```

Stolpersteine: `< /dev/null` (sonst blockiert `mingw32-make`), `CMAKE_GENERATOR=Ninja`, Kurzpfad der Arm-Toolchain -
alles in `tools/env.sh` beruecksichtigt.

## SD-Karte

- `build_uvm2/pinball.um2` auf die Karte.
- Fuer den Originalton ueber die Buchse: einmal `mingw32-make sounds < /dev/null` (braucht `pip install soundfile`), dann den
  Inhalt von `sdcard/` auf die Karte kopieren - das ergibt `pinball/snd/*.wav` (13 Dateien). Eine alte `sounds.pcm` kann weg.
  Ohne die Dateien laeuft alles, die Buchse spielt dann nachgebildete Chip-Effekte. Ob sie geladen sind, zeigt die Seite OPTIONS
  unten (`JACK: 13/13 SOUND FILES`).
- Eigene Musik: WAV-Dateien nach `pinball/snd/` legen, benannt `<n>_name.wav` - `0_` fuers Menue, `1_` bis `10_` fuer die Tische
  (16 Bit, mono oder stereo; am besten 32 kHz mono). Lautstaerke: OPTIONS -> MUSIC VOLUME.
- Das Spiel legt `pinball/settings.bin` und `pinball/scores.bin` selbst an (Einstellungen, Bestenlisten).
- Bildrate und Strahlwerte der Konsole: Assistent des Kits (Knoepfe 2+3 halten, mit 4 starten) oder im Spiel unter
  OPTIONS -> ADJUST SCREEN.

## Steuerung

| | Konsole | Simulator |
|---|---|---|
| linker Flipper | Knopf 1 | linke Umschalt- oder Strg-Taste, Y/Z |
| rechter Flipper | Knopf 4 | rechte Umschalt- oder Strg-Taste, Minus, Eingabe |
| Ball starten | Knopf 3 oder ein Flipper | Leertaste, C |
| gegen den Tisch stossen | Stick links / rechts / hoch | Pfeil links / rechts / hoch |
| Pause | Knopf 2 | P, X |
| Menue: Punkt waehlen | Stick hoch/runter | Pfeil hoch/runter |
| Menue: Wert aendern | Stick links/rechts | Pfeil links/rechts |
| Menue: OK | Knopf 4 (oder 3) | Eingabe, Leertaste |
| Menue: zurueck | Knopf 1 | Ruecktaste |

Simulator: F1 Statistik, F2 Bildschirmfoto, F3 Nachleuchten, Esc Ende. Umgebung: `VP_TABLE=n` (direkt ins Spiel),
`VP_AUTOPLAY=1` (Flipper-Automat), `VP_ADJUST=1`, `VP_ADJUST_ITEM=n`, `VP_PAGE=1`, `VP_ENTRY=1`, `VP_DETAIL=0..2`, `VP_ZOOM=0/1`,
`VP_REAL=0/1`, `VP_NOPCM=1`, `VP_FRAMES=n`, `VP_SHOT=datei.bmp`,
`VP_SHOT_AT=n1,n2`, `VP_HEADLESS=1`, `VP_FIXED_DT=1` (wiederholbare Laeufe), `VP_NOSAVE=1`, `VP_MUTE=1`,
`VP_KEYS="bild:maske[:x:y],..."` (Eingaben nach Drehbuch).

## Analyse

- `tools/soak.exe [sekunden] [tisch] [seed]`: Dauerlauf, `SOAK_MSG=1` zeigt die Meldungen, `SOAK_ESC=1` Ausreisser.
- `tools/savertest.exe [0|1]`: Ballretter aller Tische (1 = mit PHYSICS REAL). `SOAK_REAL=1` / `TRACE_REAL=1` schalten die anderen
  Werkzeuge auf REAL, `SOAK_POS=1` zeigt alle 5 s den Ort des Balls (Haenger finden).
- `tools/trace.exe <tisch> <sekunden> <0|1> [jedes n-te Bild]`: Ballbahn als Text (1 = mit Flipper-Automat);
  bauen mit `mingw32-make tools/trace.exe < /dev/null`.
