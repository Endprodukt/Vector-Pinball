# Vector Pinball fuer VectorPie - Aufbau und Stand

## Ziel

Das Spiel der Vectrex-Fassung (`../vectrex`: zehn Tische, Physik, 3D-Koerper mit Verdeckung, Zoom, Menue, Ton) auf
VectorPie: Raspberry Pi mit USB-DVG oder BeamForge an einem Farb-Vektormonitor, zum Entwickeln ein Fenster auf dem PC.
Dasselbe Spiel, aber in Farbe und ohne die Enge der Vectrex (Strichpuffer, Speicher).

SDK: https://github.com/Goldfinch2/VectorPie, Ordner `sdk` (Stand beim Port: 0.1.0, Commit 96898c9 vom 2026-10-07, MIT).
Es liegt als Klon in `sdk-repo/` (nicht im Repo, siehe SETUP.md).

## Was geteilt wird und was eigen ist

Es bleibt bei EINER Spiellogik. Unveraendert aus `../vectrex` uebersetzt werden:

| | |
|---|---|
| `core/*` | Spielkern: Physik, Tischregeln, Tischdaten |
| `game/font.c intro.c settings.c hiscore.c` | Schrift, Vorspann, Einstellungen, Bestenliste |
| `game/sound.c aysynth.c pcm.c mp3.c` | Ton samt Mischer (Stereo nach Balllage, Ducking, MP3, nachgebildeter Klangchip) |
| `platform/plat.h` | der Plattformvertrag |

Eigen sind hier nur die Dateien, in denen Farbe, Bildformat und Steuerung stecken:

| | |
|---|---|
| `platform/vectorpie.c`, `vpie.h` | Plattform: Schleife, Steuerung, Striche, Ton, Dateien (einzige Datei, die das SDK kennt) |
| `platform/shim/vectrex/vectrexInterface.h` | leerer Ersatz fuer den Klangchip-Aufruf in sound.c |
| `game/gfx.c`, `gfx.h` | Pfadpuffer und Verdeckung wie auf der Vectrex, dazu Farbe je Strich und grosse Puffer |
| `game/render.c` | Tischbild: Farbton je Element, Zoomfenster im Querformat, rundere Kreise |
| `game/main.c` | Rahmen: Start, Schleife, Steuerung, Zeit, die Zustandsmaschine (seit 2026-10-11 nur noch das) |
| `game/app.h` | was die Teile des Ablaufs voneinander brauchen: Zustaende, Spielfeld, Zeit, Steuerung, Oeffner und Bilder |
| `game/attract.c` | Attract (Titel, Demo, Bestenliste), Spielerzahl, Tischwahl |
| `game/play.c` | das Spiel: Steuerung, Stoessel, Stoss, Ton, Zeichnen, Ende der Runde |
| `game/entry.c` | Initialen, Liste fuer das Menue des Automaten |
| `game/service.c` | Service-Menue, CONTROLS, ABOUT |
| `game/art.c` | Kulisse: Flipper-Rahmen, Lauflicht, Muenzzeile, Ablaufbalken, Tischvorschau |
| `game/segdisp.c` | Segmentanzeige (14 Segmente) fuer Punkte, Meldungen, Credits |
| `game/fx.c` | Leuchtspur des Balls, Funken, aufsteigende Punktzahlen |

Der Vorspann der Vectrex-Fassung (`intro.c`) wird hier nicht mehr gebaut.

`render.c` begann als Kopie mit Aenderungen; seit 2026-10-11 (Zeichnen je Elementtyp in eigenen Funktionen, Boegen aus
ihren Massen, Boden, Farbe) sind Aenderungen an der Vectrex-Fassung von Hand zu uebertragen - ein `diff` gegen
`../vectrex/game/render.c` zeigt, WAS sich dort geaendert hat. Der Ablauf (main.c und die Teile daneben) ist eigen.

**Umbauten pruefen** (`tools/regress.sh`): `sh tools/regress.sh base` schreibt die Strichlisten von 20 festen Szenen
(Titel, Demo, Auswahl, Service, Initialen, Tische 1-10 mit und ohne Zoom, hochkant) nach `build/regress/base`, danach
`sh tools/regress.sh check` baut sie neu und vergleicht Strich fuer Strich; `check -u` vergleicht als Menge (fuer Umbauten,
die nur die Zeichenreihenfolge aendern duerfen). Dazu laeuft der ganze Ablauf einmal durch. Die Szenen sind wiederholbar
(feste Bildzeit, fester Zufall). Vor jedem Aufraeumen die Basis schreiben, danach pruefen.

## Bild

- **Koordinaten** wie auf der Vectrex: Bildschirmeinheiten in 1/16 (Q4), senkrecht -189..189. Waagerecht -252..252 im
  Querformat (4:3), -141..141 hochkant (`gfx_half_w`). `vpie_line` rechnet auf die 0..4095 der Platine um. Die Platine
  loest 10,8 Schritte je Einheit auf, Q4 ist also feiner als die Platine.
- **Farbe**: die Helligkeitsstufen der Vectrex-Fassung bleiben (Kulisse < Waende < Wichtiges < Voll, `render_intensity`).
  Dazu kommt der Farbton des Originals aus den Tischdaten (`set_hue`, `hue_of` in render.c: hellster Kanal auf 255, damit
  nicht doppelt abgedunkelt wird; reines Blau bekommt etwas Weiss). `gfx_color` setzt den Ton, die Stufe b (0..127) dunkelt
  ihn ab. Was aufblitzt (Bumper, Schleudern, Ziele), blitzt weiss.
- **Ball** (`draw_sphere`, `draw_ball_roll` in render.c; anders als auf der Vectrex, dort ein halber Rollring): eine Kugel
  mit Gradnetz, die wirklich rollt. Jede Kugel traegt ihre Lage als Drehmatrix und dreht sich je Bild um die Achse quer
  zur Fahrt und mit dem Drall der Physik um die Hochachse. Gezeichnet: Umriss und ein Breitenkreis um einen Pol
  (`ball_color`), Aequator und zwei Laengenkreise (`ball_color2`), ein stehendes Glanzlicht (weiss). Rund 50 kurze
  Striche je Ball. Der Zeiger im Menue (`roll_ball` in main.c) ist noch die Kugel der Vectrex-Fassung.
  Menues und Einblendungen: Farbtabelle `C_*` am Anfang von main.c.
- **Kennlinie** (gfx.c, `lut_init`): auf der Platine geht die Stufe geradewegs auf den Farbwert (1,0 - so sind die Stufen
  auf der Vectrex abgestimmt), im Fenster ueber 1,6 wie im Vectrex-Simulator. `VP_GAMMA=<Prozent>` zum Ausprobieren.
- **Querformat** (2026-10-09, Ollis Vorgabe "Bildbreite immer fuellen"): im Spiel fuellt der Tisch immer die Bildbreite
  (rund 2,2-fach, `render_game_view`) und rollt senkrecht mit - auch am Rundenbeginn und im Multiball; die Ansicht folgt
  dem untersten Ball und gleitet ihm nach. Die Punktezeile steht oben ueber die ganze Bildbreite (`w_hud`). ZOOM ist im
  Querformat nur FULL WIDTH oder OFF (OFF = ganzer Tisch hochkant in der Mitte). Nach dem Spielende faehrt die Ansicht auf
  den ganzen Tisch zurueck. Menue: rueckt nach links (`gfx_shift`, `menu_side` in main.c), rechts daneben der gewaehlte
  Tisch als gedimmte Vorschau. Hochkant (`--vertical`, `VP_VERTICAL=1`) bleibt alles wie auf der Vectrex.
- **Mehr als auf der Vectrex**: Kreise und Ball mit mehr Ecken (`segs_for`), Waende immer als Koerper (kein
  `STROKE_LIMIT`), bis zu 160 Sterne auf Tisch 7, Puffer fuer 12000 Ketten und 1024 Verdecker.
- **Kosten**: auf der Platine bestimmt vor allem die WEGLAENGE die Zeit je Bild (auf der Vectrex war es die Strichzahl).
  `VP_INFO=1` blendet die Werte ein (LIT, BLK in Tausend), `VP_STATS=1` druckt sie je Sekunde. Stellschrauben: 3D
  (EVERYTHING verdoppelt die Waende), ZOOM.
- **Strahlweg**: ausgegeben wird in der festen Reihenfolge des Zeichnens. Eine Sortierung nach kuerzestem Strahlweg (am
  PC rund 60 % weniger unsichtbarer Weg) war vom 2026-10-11 bis 2026-10-14 drin und ist auf Ollis Wunsch wieder heraus -
  nicht wieder einbauen, ohne zu fragen.
  Auf der USB-DVG gemessen (`tools/measure.sh`, 2026-10-11, VOR der Ordnung): Tisch 8 lit 87-101k / blank 76-87k,
  Tisch 7 lit 97-118k / blank 83-97k, Attract lit 71-90k / blank 72-90k, 40 Bilder/s, keine verlorenen Striche.

## Schleife, Steuerung, Ton

- **Schleife**: `vpie_run` in vectorpie.c, bewusst NICHT `vp_run` des SDK. `vp_run` oeffnet immer das Audiogeraet von
  `vp_audio`; unser Mischer braucht ein eigenes, und ein zweites gab es im Test nicht ("Audio device already open").
  `vpie_run` tut mit den oeffentlichen Aufrufen dasselbe wie `vp_run`: Ausgang waehlen (`VP_OUTPUT`, advmame.rc, Schalter
  `--dvg [PORT]`, `--window`, `--windowed`, `--vertical`), Ende auf `ui_cancel` / Fenster zu / Strg+C mit Endefolge an die
  Platine, Marquee (`marquee.png` neben dem Programm) auf dem HDMI-Schirm, START-Lampen zuruecksetzen, `VP_STATS`.
  Aendert sich `vp_run` im SDK, hier nachziehen.
- **Bildrate**: Fenster 60, Platine 40 (Vorgabe des SDK); `VP_HZ=n` aendert sie. Die Physik laeuft wie auf der Vectrex
  in festen Schritten nach der Echtzeit, unabhaengig von der Bildrate.
- **Steuerung**: frei belegbar, unabhaengig von advmame.rc (Olli, 2026-10-15: "die button belegung muss frei von
  advmame.rc sein"). Im Service unter CONTROLS (Zeile, START, dann druecken bzw. bewegen) wird das Geraet selbst
  gespeichert: Taste (`key:Left Ctrl`), Joystick-Knopf (`joy0:b3`), Steuerkreuz (`joy0:h0:u`), Achse als Knopf
  (`joy0:a1+`), Maustaste (`mouse:b1`); fuer NUDGE AXIS und YOKE AXIS eine ganze Achse (`joy0:a1`, `~` = umgedreht; NUDGE
  AXIS nach rechts bewegen, YOKE AXIS zurueckziehen - so merkt es sich die Richtung). Schon vergeben: die beiden tauschen;
  Stick links loescht. Eine Achse belegen stellt NUDGE auf ANALOG (Yoke auch PLUNGER). Funktionen: LEFT/RIGHT FLIPPER (mehrere
  Eingaenge je Funktion, etwa beide linken bzw. rechten Knoepfe eines Yokes; keine eigenen FLIPPER-2-Zeilen mehr, und
  kein LAUNCH BALL: abgeschossen wird nur mit dem Stoessel, PLUNGER digital oder YOKE AXIS analog; alte
  `left/right_flipper_button_2` und `launch_button` werden beim Lesen zu Flipper bzw. Stoessel), NUDGE LEFT/RIGHT/UP, PLUNGER, NUDGE AXIS, YOKE AXIS, START, COIN, SERVICE (die letzten drei zusaetzlich).
  Gespeichert in `save/settings.ini`, das vor `game.ini` geht. Nur die Vorgabe in `game.ini` nennt advmame-Aktionen
  (`p1_button1` ...), damit das Spiel vor dem ersten Belegen bedienbar ist. `p1_stickx/y` wird NICHT gelesen: ein
  digitaler Stick bewegt sie mit, und liegt darauf ein Flipper, stiesse jedes Flippen. Fest bleiben Muenze, START (OK; im
  Spiel wechselt es die Ansicht: Bildbreite, naeher, ganz nah, ganzer Tisch - bleibt als ZOOM), F2 (Service, wie an den
  anderen Automaten; auch aus dem Spiel, das solange wartet, F2 fuehrt zurueck), Tab/ui_configure und Esc (nicht belegbar). Ausserhalb des Spiels gilt der Stick (`p1_left` ...)
  immer mit. Code: `read_controls`, `vpie_capture`, `src_of` in vectorpie.c, `controls_frame` in service.c. Test ohne
  Hardware: `VP_VJOY="bild:a0=v:a1=v:b0=1,..."` steckt einen Ersatz-Joystick an (am PC mit vier Geraeten `joy4`).
- **Stoessel analog** (Yoke, Stick): der Stoessel bleibt auf dem tiefsten Punkt stehen, bis der Hebel ganz losgelassen
  ist, und schiesst dann mit diesem Zug (`plunger` in play.c).
- **game.ini**: Werte duerfen kein `:` und kein `|` enthalten (Trennzeichen des Menues; `vp-pack` bricht sonst ab und es
  entsteht KEIN neues Paket). Die Belegungen (`key:...`, `joy0:...`) stehen darum nur im Programm (`BIND_STANDARD`) und in
  `save/settings.ini`, nicht in game.ini.
- **Menues mit den Flippern**: Spielerzahl, Tisch und Initialen waehlt man mit den Flippern (links / rechts) - ausserhalb
  des Spiels gelten sonst nur die festen Menue-Eingaenge (vpbind.h). Liegt ein Flipper auch auf dem Stick, zaehlt er einmal.
- **Vorgaben auf dem Automaten** (solange keine `save/settings.bin` existiert): PLUNGER DIGITAL (Stick unten halten zieht
  auf), DETAIL HIGH. Sonst die Vorgaben der Vectrex-Fassung.
- **Ton**: `pcm.c` mischt, die Plattform oeffnet ein SDL-Audiogeraet (32 kHz stereo, SDL wandelt). Die Aufnahmen liegen als
  WAV in `data/snd/` neben dem Programm (aus `../vectrex/sdcard/pinball/snd`, `make sounds` dort). Eigene Musik wie auf der
  Vectrex: `data/snd/<n>_name.wav|mp3`.
- **Dateien**: `save/settings.bin`, `save/scores.bin` (je Tisch vier Plaetze, wie auf der Vectrex) und
  `save/highscores.txt` fuer die Hinweiszeile im Menue des Automaten (die zehn besten ueber alle Tische, `export_scores`).
- **Lampen**: START blinkt im Attract, sobald ein Spiel bezahlt ist (`vpie_lamps`), sonst aus.

## Pruefen ohne Automaten

Auf dem Automaten: `tools/run.sh` (Kopie im Spielordner) haelt das Menue an, startet das Spiel auf der Platine mit
`VP_STATS=1` nach `run.log` und holt das Menue danach zurueck - `ssh ... 'nohup setsid /persistent/games/vpinball/run.sh
> /dev/null 2>&1 < /dev/null &'`, Umgebung wird durchgereicht (`VP_FRAMES=1200` = 30 s). `measure.sh` misst Tisch 8, Tisch 7
und den Attract nacheinander (`measure_*.log`). Beides nur, wenn gerade niemand spielt (`pgrep -f vpinball_`).

Wie beim Vectrex-Simulator ueber die Umgebung: `VP_TABLE=n` startet Tisch n, `VP_AUTOPLAY=1` spielt selbst, `VP_ZOOM`,
`VP_3D`, `VP_DETAIL`, `VP_PAGE=1` (Service, `VP_ITEM` = Zeile), `VP_ADJUST`, `VP_ENTRY`, `VP_ABOUT`, `VP_ATTRACT=1|2` (Demo,
Bestenliste), `VP_ATTRACT_TABLE=n`, `VP_SELECT=1|2` (Spielerzahl, Tischwahl), `VP_INFO=1`, `VP_PROF=1`,
`VP_NOSAVE=1`. Dazu: `VP_FRAMES=n` (Ende nach n Bildern), `VP_FIXED_DT=1` (feste Bildzeit, wiederholbar), `VP_HZ=1000`
(ohne Bildtakt), `VP_KEYS="bild:maske[:x:y],..."` (Bit 0-3 Knopf 1-4, 16 Muenze, 32 START, 64 Service; Stick),
`VP_CAP="bild:eingang"` (Belegung im Test), `VP_SHOT_AT=n1,n2` (Striche des Bildes nach
`shot_<n>.txt`; `python tools/shot.py shot_<n>.txt` macht ein PNG daraus). Ohne Fenster und Ton:
`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`.

## Stand (2026-10-08)

Laeuft im Fenster auf dem PC (Build `win`, MSYS2): Vorspann, Menue, Optionen, alle zehn Tische, Zoom, 3D mit Verdeckung,
Pause, Einstellbild, Bestenliste mit Initialen, Ton (13/13 Aufnahmen geladen). Geprueft per Testbild (Menue, Optionen,
Tisch 1, 7, 8, 10, Pause, Einstellbild, Vorspann, hochkant) und Dauerlauf mit `VP_AUTOPLAY` ueber alle Tische (je 6000
Bilder, kein Absturz, keine verlorenen Striche).

2026-10-09: Stand der Vectrex-Fassung vom selben Tag uebernommen (Flipper in REALISTIC nach dem Vorbild von VPX, tiefere
Drehpunkte mit Einlaufbahn `flip_step`, dunkles oberes Spielfeld auf Tisch 7 mit `v_off` / `under`). Kern, Werkzeuge und
Doku kopiert; die Aenderungen an render.c und main.c als Diff auf die Kopien hier angewandt. Geprueft: `tools/soak` 10/10 in
beiden Physik-Modi, Dauerlauf des Spiels ueber alle Tische, Testbilder Tisch 1 (Flipper) und Tisch 7 (Ball oben,
`VP_BL=1 VP_BX=40 VP_BY=240`). So geht das Nachziehen auch kuenftig:
`diff -u alt/game/render.c neu/game/render.c | patch vectorpie/game/render.c`, ebenso main.c.

Auf dem Automaten (Pi 5, VectorPie 1.1.8) seit 2026-10-09 installiert: `/persistent/games/vpinball/`, Verweise und Zeile in
`gamelist.ini` (SETUP.md). Dort gelaufen ist das Programm bisher nur ohne Bild und Platine (300 Bilder Tisch 1 mit
`VP_AUTOPLAY`, 62 Bilder/s).

2026-10-09 abends: Querformat-Ansicht und belegbare Knoepfe gebaut (PC und Pi-Paket), am PC per Testbild und Dauerlauf
geprueft; das Belegen selbst nur ueber die Testhilfe `VP_CAP="bild:eingang"`, nicht mit echten Knoepfen. NOCH NICHT auf
dem Automaten: er war beim Hinueberschieben nicht erreichbar - `build/vpinball-0.1.0.vpk` nach SETUP.md entpacken.

2026-10-09 nachts: Automatenfassung (Abschnitt "Automat"). Am PC geprueft: Testbilder aller neuen Seiten, der ganze
Ablauf mit abgespielten Eingaben (Muenze, START, Spielerzahl, Tisch, Spiel bis zum Ende, zurueck in den Attract, 40000
Bilder) und der Attract allein ueber 60000 Bilder, ohne Absturz. Pi-Paket gebaut (`build/vpinball-0.1.0.vpk`), aber NICHT
auf dem Automaten: er war nicht erreichbar. Von Hand gespielt hat das noch niemand; Muenzpruefer, START-Lampen und Tab am
Automaten sind ungeprueft.

2026-10-10: nach dem ersten Blick auf die Roehre (Olli):
- Schriften: alle kleinen waren zu klein. Service, CONTROLS und ABOUT stehen im Querformat jetzt gross ueber die ganze
  Bildbreite (`m_lay` in main.c: Zeilen 25, Werte 22, Hinweise 15-16; ohne Tischvorschau), Hinweise und Nebenzeilen der
  anderen Seiten sind auf 15-27 angehoben, die Punktezeile im Spiel ist knapp doppelt so hoch. Nur der Tischname in der
  Tischwahl ist kleiner geworden (er muss zwischen die Pfeile passen). Faustregel: Strichschrift nicht unter 15,
  Segmentschrift nicht unter 17 Einheiten.
- Rollover-Lampen, aufgemalte Linien und die Formen der Tische liegen jetzt auf dem Boden (`v_floor`, `floor_pt` in
  render.c, halber Versatz der Wandfuesse) statt auf der Hoehe der Oberkanten - sie sassen sonst scheinbar auf den
  Seitenwaenden. Nur hier, die Vectrex-Fassung zeichnet sie weiter unversetzt.
- Stand der Vectrex-Fassung vom 2026-10-10 uebernommen: die Bahnen vom Einlauf auf die tieferen Finger wirken nur noch in
  der Physik und werden nicht gezeichnet, Neigung und Schlagkraft je Tisch neu abgestimmt (`TABLE_SLOPE`, `TABLE_FLIP`),
  ruhende Rollover als Umriss, Flipperton bei jeder Bewegung im Service.
- Lieder in 16 kHz, Lautstaerken MASTER / EFFECTS / MUSIC, ohne ADJUST SCREEN und DETAIL (siehe "Automat").

2026-10-10 abends, "Details des Originals zurueck" (Olli): was die Vectrex-Fassung dem Strahl geopfert hat, zeigt diese
Fassung wieder. Gefunden durch Vergleich mit dem Java-Original (`renderer.draw*` in elements/, model/, fields/) und den
Tischdateien:
- Ebenen unter dem Spielfeld (`layer < 0`, nur Tisch 8 Pool Party): die acht Billardkugeln und die vier Bonuslampen. Auf
  der Vectrex gar nicht gezeichnet; hier flach auf dem Boden, in ihren Farben.
- Boegen: aus Mittelpunkt, Radien und Winkeln gerechnet (`arc_pts` in render.c) statt aus der sparsamen Punktliste
  `dpts` (Pfeilhoehe bis 0,07 Einheiten). Auch die Bogenformen der Tische (Kartenfarben auf Tisch 9) sind feiner.
- Bumper: alle als Koerper; auf der Vectrex bleiben die von Tisch 8, die wandernden auf Tisch 2 und kleine Pfosten flach.
- Tisch 7: die ganze Sternkarte. Der Kern gibt auf der Vectrex nur das aktive Sternbild und die 12 hellsten uebrigen
  Sterne heraus (hoechstens 48, Liste der Formen 160). Der Kern bleibt unveraendert geteilt: CMakeLists.txt uebersetzt
  eine KOPIE (`build/<preset>/core`), in der `VP_MAX_SHAPES`, `T7_MAXPROJ`, `T7_MAX_DOTS`, `T7_OTHER_STARS` angehoben
  sind, und bricht ab, wenn eine dieser Zeilen im Kern nicht mehr so steht. render.c zeichnet die Sterne spaltenweise
  geordnet (`draw_shapes`), sonst spraenge der Strahl kreuz und quer.
- Schon vorher wieder da: Ballspur, Punktzahlen, aeussere Bumperringe, ruhende Rollover als Umriss.
- Geprueft und vollstaendig uebertragen (nichts zu holen): die Formen von Tisch 9 (Karten) und Tisch 10.
Kosten am PC gemessen (ganzer Tisch, Zaehler des SDK): Tisch 7 rund 90000 gezeichnet + 93000 unsichtbar, Tisch 8 rund
90000 + 81000 - mehr als vorher (60000-90000 + 50000-70000). Ob die USB-DVG das sauber zeichnet, zeigt der Automat.
Die Punktezeile passt sich jetzt an ein schmales Fenster an (ganzer Tisch: Punkte und Ball ueberlappten).
Paket gebaut, aber NICHT auf dem Automaten: er war nicht erreichbar.

NICHT geprueft:
- auf dem Automaten: Start aus dem Menue, USB-DVG, Farben und Helligkeitsstufen auf der Roehre, Bildrate, Ton, Lampen.
- der Ton am echten Audiogeraet in dieser Fassung (im Test lief SDLs Ersatztreiber; der Code ist der des
  Vectrex-Simulators).
- Spielen von Hand im Fenster (nur Automat und abgespielte Eingaben).

## Belegung wie bei allen Vektorspielen (2026-10-19)

- Jede Funktion bis zu 4 Eingänge (`, ` dazwischen), alles direkt gelesen; Vorgabe (STANDARD) = die AdvanceMAME-Tasten/-Knöpfe/-Maustasten, alte advmame-Namen in Dateien werden beim Lesen dazu. Kein Tausch mehr beim Belegen: ein Eingang darf mehrere Funktionen haben.
- CONTROLS: START/Enter, dann ein Knopf – der erste seit der Cursor auf der Zeile steht ersetzt, jedes weitere START fügt hinzu (Achsen ersetzen immer). Während des Wartens: Enter (außer bei START/COIN/SERVICE auch START 1) zurück, START 2 NONE, Entf Vorgabe; Links/Rechts NONE/Vorgabe; Entf auf der Zeile Vorgabe; kein 8-Sekunden-Abbruch. Nie belegbar: Tab, F2, Esc, Enter, Keypad-Enter, Entf, Rücktaste; außer bei START/COIN/SERVICE auch 1, 2, 5, 6. Lange Werte werden kleiner geschrieben.
- Außerhalb des Spiels (Menüs, Service, Spielerzahl, Initialen) gelten nur feste Eingänge (`platform/vpbind.[ch]`, Kopie aus dem Tempest-2000-Port): Pfeile, jeder Stick/Hat, START 1/Enter. Flipper wirken dort nicht mehr.
- Am PC geprüft (ersetzen, hinzufügen, NONE/Vorgabe, Entf), Pi-Build am Automaten installiert (alte Datei `pinball.before-vpbind`), dort nicht ausprobiert.

## Offen

1. Auf dem Automaten spielen und abstimmen:
   `VP_INFO=1` / `VP_STATS=1` ansehen, Bildrate (`VP_HZ`), Kennlinie (`VP_GAMMA`), 3D-Stufe und DETAIL waehlen und die
   Vorgaben danach setzen.
2. (erledigt 2026-10-10: `marquee.png` liegt im Projekt, im Paket und auf dem Automaten, auch als `artwork/marquees/vpinball.png` fuer das Menue.)
3. Der Vorspann ist einfarbig (er kommt unveraendert aus `../vectrex/game/intro.c`).
4. Die Kennlinie ist nur ueber `VP_GAMMA` einstellbar, nicht im Einstellbild (`settings_t` wird mit der Vectrex-Fassung
   geteilt; neue Felder duerfen dort nur angehaengt werden).
6. Noch moeglich: Ebenen unter dem Spielfeld (Tisch 8), ein Marquee, Farbe und Effekte je Tisch.
7. Flipper liegen auf `p1_button1` / `p1_button2`; ob das zum Bedienpult passt, entscheidet der Automat (advmame.rc).
8. Yoke noch nicht mit echter Hardware geprueft (nur mit dem Ersatz-Joystick `VP_VJOY`). Die Joystick-Nummer
   (`joy0` ...) ist die Reihenfolge, in der SDL die Geraete findet; steckt man um, kann sie sich aendern.
9. (erledigt 2026-10-19) Highscores im Menue/Marquee: Vector Pinball ist jetzt ein Paket des Menues wie Vectortron (Zeile
   aus gamelist.ini genommen, Sicherung `/home/pi/gamelist.ini.before-vpinball-package`; `vpinball_dvg`/`_sdl` entfernt;
   Paket in `/persistent/games/packages/`). Das Menue meldet "packages: vpinball 0.1.0 (Other)" und liest
   `save/highscores.txt`.

## Weiterarbeiten

- Bauen und starten: SETUP.md. Schnelltest nach einer Aenderung:
  `VP_TABLE=1 VP_AUTOPLAY=1 VP_FRAMES=400 VP_SHOT_AT=390 build/win/pinball.exe --windowed`, dann `tools/shot.py`.
- Der Ordner `../vectrex` war beim Port nicht in Git (lag unversioniert im Hauptarbeitsordner); in diesem Worktree liegt
  eine Kopie der Quellen. Beides gehoert zusammen eingecheckt - `vectorpie/` baut ohne `../vectrex` nicht.
- Python-Skripte und C-Quellen mit dem Schreibwerkzeug anlegen, nicht per Shell-Heredoc.
