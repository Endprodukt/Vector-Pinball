# Vector Pinball fuer VectorPie - Werkzeuge und Bauen

## Einmal: das SDK holen

Im Ordner `vectorpie/`:

```sh
git clone https://github.com/Goldfinch2/VectorPie.git sdk-repo
```

`sdk-repo/` steht in `.gitignore`. CMake baut das SDK als Unterordner mit (`VP_SDK_DIR`, Vorgabe `sdk-repo/sdk`).
Die Tonaufnahmen kommen aus `../vectrex/sdcard/pinball/snd` (`make sounds` in `../vectrex`); fehlen sie, baut das Spiel
trotzdem und spielt die nachgebildeten Chip-Effekte.

## PC (Windows), direkt

Mit denselben Werkzeugen wie der Vectrex-Simulator: MSYS2 MINGW64 (gcc, SDL2 unter `C:/msys64/mingw64`), dazu CMake und
Ninja. Kein WSL noetig.

```sh
cmake --preset win
cmake --build --preset win
build/win/pinball.exe --windowed
```

`--window` = Vollbild, `--vertical` = Monitor hochkant, `--dvg COM5` = eine USB-DVG am PC.
Tasten ohne advmame.rc: 5 = Muenze, 1 = START (auch Ball starten und OK), Tab = Service, Pfeiltasten = Stick,
Strg links / Alt links = Flipper, Leertaste = Ball starten, Esc = Ende.

## Pi (der Automat): bauen auf dem PC, hinueberschieben, Menue-Eintrag

So ist es am 2026-10-09 gemacht, derselbe Weg wie im Tempest-2000-Port (`F:/Tempest 2000 Port/BUILD.md`).
NICHT auf dem Pi kompilieren: ein Build mit vier Kernen hat den Automaten am 2026-10-09 ausgehen lassen (der Pi meldet
Unterspannung, `vcgencmd get_throttled` = 0x50005).

Bauen und packen (clang und lld vom PC gegen die Kopie der Bibliotheken des Automaten, `cmake/toolchain-pi-clang.cmake`;
der Sysroot ist der des Tempest-Ports):

```sh
cmake --preset pi-clang && cmake --build --preset pi-clang    # -> build/pi/pinball (arm64)
sh tools/make_vpk.sh                                         # -> build/vpinball-<version>.vpk
```

Auf den Automaten (192.168.0.122, Schluessel `~/.ssh/id_ed25519_vectorpie`): die `.vpk` nach `/persistent/games`
entpacken, wie es der Paket-Installer taete, daneben `save/`:

```sh
scp -i ~/.ssh/id_ed25519_vectorpie build/vpinball-0.1.0.vpk pi@192.168.0.122:/tmp/
ssh -i ~/.ssh/id_ed25519_vectorpie pi@192.168.0.122 'cd /persistent/games && tar xzf /tmp/vpinball-0.1.0.vpk --no-same-owner && mkdir -p vpinball/save'
```

Fuer ein Update genuegt das; `save/` bleibt dabei stehen. Die `.vpk` zusaetzlich nach `/persistent/games/packages/`
kopieren (dort sucht das Menue seine Pakete, wie bei Vectortron), damit dort nicht eine alte liegt.

Seit 2026-10-19 ist Vector Pinball ein Paket des Menues (vorher von Hand angebunden, als das Menue noch keine Pakete
kannte). Nur als Paket zeigt das Menue die Bestenliste (`save/highscores.txt`, `hiscores = yes`) beim Marquee. Dafuer
wurden entfernt: die Zeile `Other|Vector Pinball|vpinball|vpinball|vpinball` aus `/usr/local/share/advance/gamelist.ini`
(die Fassung davor: `/home/pi/gamelist.ini.before-vpinball-package`) und die Verweise `vpinball_dvg`/`vpinball_sdl`.
Steht ein Spiel in gamelist.ini, uebergeht das Menue sein Paket ("packages: ... skipped: gamelist.ini already has that
rom" in `vector_pie_menu.log`). Das Menue startet das Paket mit `VP_OUTPUT=dvg`.
- das Marquee: `marquee.png` im Spielordner (kommt mit dem Paket) und als Kopie `artwork/marquees/vpinball.png` fuer das Menue

Musik (eigene Lieder je Tisch, `<n>_name`, 0 = Titelbild mit ATTRACT SOUND, 1-10 = Tisch): gehoert NICHT ins Paket (rund
100 MB), sondern liegt als WAV 16 kHz stereo 16 Bit in `/persistent/games/vpinball/data/snd/` und bleibt bei einem Update
stehen. 16 kHz ist Ollis Vorgabe (2026-10-10: reicht voellig). Aus MP3 wandeln und hochladen (Python 3.10 hat
`soundfile`; Ollis Lieder liegen als MP3 in `F:/VectorPinballVectrexPort/vectrex/build/pinball/snd`):

```sh
python tools/songs.py "F:/VectorPinballVectrexPort/vectrex/build/pinball/snd" build/music
scp -i ~/.ssh/id_ed25519_vectorpie build/music/[0-9]_*.wav pi@192.168.0.122:/persistent/games/vpinball/data/snd/
```

Das Spiel sucht die Lieder nur beim Start, und ein laufendes Spiel vertraegt es nicht, wenn sein Lied ersetzt wird: danach
neu starten. Stand 2026-10-10: Lieder 0-9 liegen auf dem Automaten, fuer Tisch 10 gibt es keins (dort spielt die Musik des
Originals).

Auf der Platine starten, vom PC aus (das Menue haelt an und kommt nach dem Spiel zurueck; Bildkosten in `run.log`):

```sh
ssh -i ~/.ssh/id_ed25519_vectorpie pi@192.168.0.122 'nohup setsid /persistent/games/vpinball/run.sh > /dev/null 2>&1 < /dev/null &'
```

`run.sh` und `measure.sh` liegen in `tools/` und als Kopie im Spielordner (mit `scp` nachlegen, wenn sie sich aendern).

Pruefen ohne Bild und Platine, vom PC aus (leichte Last):
`ssh ... 'cd /persistent/games/vpinball && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy VP_NOSAVE=1 VP_TABLE=1 VP_AUTOPLAY=1 VP_FRAMES=300 ./pinball --windowed'`

## Pi-Build nach dem Muster des SDK (nicht eingerichtet)

Das SDK baut die Pi-Fassung in WSL2 mit Debian 13 (trixie), demselben Stand wie das Pi OS des Automaten:

```powershell
wsl --install -d Debian
```

Dann in Debian (das Projekt am besten im Debian-Dateisystem, nicht unter `/mnt`):

```sh
bash sdk-repo/sdk/tools/setup-wsl.sh        # Compiler fuer Windows und Pi, SDL2
cmake --preset pi && cmake --build --preset pi
cmake --build --preset pi --target vpk      # -> build/pi/vpinball-<version>.vpk
```

Die `.vpk` auf einen USB-Stick (Ordner `games/`), am Automaten: Settings -> GAMES -> INSTALL FROM USB.
Zum Probieren ohne Paket: `build/pi/pinball` samt `data/` auf den Pi kopieren, das Menue beenden, `./pinball`.

Stand 2026-10-09: `win` und `pi-clang` sind gebaut, das Paket liegt auf dem Automaten. `pc`, `pi` und das Ziel `vpk` in
CMake sind nach dem Muster des SDK angelegt, aber nie gelaufen (auf diesem Rechner ist nur Ubuntu 24.04 in WSL).
