# Vector Pinball fuer Vectrex + UVMC2 - Architektur und Stand

## Ziel

Alle neun Tische des Originals und als zehnter der Tisch von Spinball (Spielfeld aus dem ROM) spielbar auf der Vectrex (UVMC2), mit der Ballphysik des Originals und so
fluessig, wie die Roehre es zulaesst. Das Original ist bereits ein Vektorspiel (Linien und Kreise), das
Spielfeld ist 20 x 30 und passt hochkant auf die Roehre (3:4).

## Schichten

```
 platform/host_sdl.c    platform/uvmc2.c      Plattform: Fenster/SD/Zeit/Ton (je ein Backend)
          \                   /
           game/main.c  ----- libvpy (Kit)    Ablauf: Menue, Spiel, Pause, Anpassung
           game/render.c                      Tisch -> Striche, Farbe -> Helligkeit, Punktezeile
           game/gfx.c  game/font.c            Pfadpuffer (Ketten), sparsame Strichschrift
           game/sound.c  game/aysynth.c       Effekte auf dem Klangchip (+ dieselben als PCM)
                |
           core/vp_field.c                    Field.java, GameState, Elementklassen
           core/vp_phys.c                     Ballphysik (ersetzt Box2D)
           core/vp_table1.c 5.c 7.c 9.c 10.c  Tischregeln: Field1Delegate .. Field9Delegate, Tisch 10 eigen
           core/gen/table_data.c star_data.c  erzeugt von tools/tables2c.py aus ../app/.../tables/*.json und tables/*.json
```

`core/` kennt weder Grafik noch Dateien noch Uhr. Der Zustand der Tischlogik liegt als `static` in den
Dateien `vp_tableN.c` (es gibt genau ein Spielfeld).

## Physik (core/vp_phys.c)

Box2D ist nicht portiert, sondern durch einen kleinen Loeser ersetzt, der dessen Verhalten fuer genau diesen
Fall nachbildet: wenige Baelle gegen feste Geometrie und Flipper. Uebernommen sind die Werte des Originals:

| | Original (Box2D) | hier |
|---|---|---|
| Ball | Kreis, Masse pi/4 (unabhaengig vom Radius), Reibung 0.3, Rueckprall 0.6, bullet | gleich; Drall der Scheibe (I = m r^2 / 2) wird mitgerechnet |
| Wand | Rechteck 0.1 dick, Reibung 0.2, Rueckprall 0 (oder aus dem Tisch) | Kapsel mit Radius 0.05 |
| Bogen | N Wandstuecke | dieselben N Stuecke (Stoss), sparsamere Punktliste nur zum Zeichnen |
| Flipper | Rechteck 0.24 dick, Dichte 5, Drehgelenk mit Motor (Moment <= 1000) und Anschlaegen | Kapsel mit Radius 0.12 als Drehkoerper mit Traegheit; Motor, Anschlag und Ballkontakt werden gemeinsam geloest (sequentielle Impulse, 6 Durchgaenge) |
| Mischung | Reibung sqrt(a b), Rueckprall max(a, b), kein Rueckprall unter 1 Einheit/s | gleich |
| Kick (Bumper, Kicker) | Impuls / Masse | gleich (`vp_ball_impulse`) |
| Schrittweite | 4 Schritte je Bild (~9,6 ms Spielzeit bei 60 Hz) | fest 480 Schritte je Sekunde Echtzeit (~4,8 ms Spielzeit), schnelle Baelle werden weiter geteilt |
| Ebenen | eine Box2D-Welt je Ebene | jedes Element und jeder Ball traegt seine Ebene |

Der Flipper ist bewusst kein "unendlich starker" Hebel: ein harter Treffer drueckt ihn wie im Original kurz
zurueck, der Motor holt mit begrenztem Moment auf. Das bestimmt, wie sich Schuesse anfuehlen.

Zeit: `vp_advance()` bekommt die vergangene Echtzeit und rechnet feste Schritte (Rest wird aufgehoben). Das Spiel
ist darum gleich schnell, egal wie viele Bilder die Roehre schafft. Die Tischlogik (Rollover, Sensoren, Regeln)
laeuft alle 4 Schritte (120 mal je Sekunde; Original: einmal je Bild). Der Ball wird fuer die Anzeige um die
noch nicht simulierte Restzeit vorausgerechnet (`vp_ball_draw_pos`), damit er nicht ruckt.

Abweichungen vom Original, bewusst:
- Punkte-Animationen und die gezeichnete Ballspur gibt es nicht.
- Gleiche geplante Aktionen innerhalb von 50 ms zaehlen als eine (ein Sensor plant sonst in jedem Takt neu).
- Beruehrungen werden wie im Original gemeldet, wenn sie ENDEN (der Ball ist dann schon abgeprallt). Das ist wichtig: ein
  Ballretter oder Kicker gibt seinen Stoss auf den zurueckspringenden Ball. Eine fruehere Fassung meldete beim Beginn - der
  Stoss hob dann nur die Fallgeschwindigkeit auf, die Wand war schon weg, der Ball ging verloren. Nur die Wand, die den
  Ball aus dem Spiel nimmt, meldet sofort. Test: `tools/savertest.exe`.

### PHYSICS: REALISTIC (Vorgabe) oder ARCADE, und BALL SPEED

Die Tische sind fuer das Verhalten des Originals gebaut; REALISTIC aendert darum nur, was keine Flugbahn verbiegt
(ARCADE = genau wie das Original):
- **Rueckprall faellt mit der Aufprallgeschwindigkeit** (e = e0 / (1 + v/25)), wie Gummi und wie es Visual Pinball als
  "elasticity falloff" kennt. Nur an Waenden und Bumpern, nicht an den Flippern (deren Schusskraft ist auf die Rampen abgestimmt).
- **Streuung**: Bumper +-4 Grad, Kicker-Waende +-7 Grad und 80-120 % Kraft. Ohne sie fand der Dauerlauf auf Tisch 8 eine stabile
  Bahn: der Ball pendelte minutenlang zwischen den beiden Schleudern.
Die Zahlen sind gewaehlt, nicht aus Visual Pinball X uebernommen (dessen Werte gelten fuer dessen eigenes Einheitensystem).

**BALL SPEED** ist davon getrennt: SLOW 0,8 (Anfaenger, Kinder), MEDIUM 1,0 (Tempo des Originals), FAST 1,2 (Vorgabe).
FAST entspricht einem echten Tisch: 20 Einheiten Breite sind gut 20 Zoll, eine Einheit also etwa ein Zoll (Ball 1 Zoll, echt
1 1/16). Mit Schwerkraft 4 und Zeitfaktor 2,3 rollt der Ball des Originals wie auf ~4,5 Grad Neigung; echte Tische stehen bei
~6,5 Grad (rollende Kugel: 5/7 g sin a) - das ist genau die 1,2-fache Geschwindigkeit bei gleichen Bahnen.

### Mehrere Spieler

1 bis 4 Spieler abwechselnd wie am echten Flipper (Menue: PLAY, links/rechts). Jeder hat Punkte, Ballnummer, Multiplikator und
Freibaelle fuer sich (`vp_field_t.pl[]`); nach jedem verlorenen Ball ist der naechste dran, ein Freiball laesst denselben
Spieler noch einmal schiessen. Der Tisch selbst (gefallene Ziele, Lichter) wird geteilt. Die Punktezeile zeigt, wer dran ist
("P2 B1"), am Ende steht der Stand aller, und jeder, der einen Platz erreicht hat, traegt seine Initialen ein.

### Tisch 10: Spinball

Der Tisch des Vectrex-Flippers von 1983 (GCE): `tables/table10.json` und `core/vp_table10.c`. Das Spielfeld stammt aus dem
ROM: `python tools/spinball_map.py F:/Vide/cartridges/SPINBALL.BIN` liest die Tabellen und schreibt `tables/table10.json`
neu (danach `python tools/tables2c.py`). Die Regeln sind NICHT aus dem Code des Originals uebersetzt, sie folgen der
Anleitung (liegt VIDE bei).

Was im ROM steht (Bildschirmeinheiten y, x; 192 Einheiten Breite = 20 Feldeinheiten):

- `$177A` Spielfeld: sechs Linienzuege. Je Strich 7 Byte: Zielpunkt, Typ, Zeiger auf ein Zustandsbyte, Zielpunkt noch
  einmal. Bit 6 des Zustandsbytes = sichtbar; zeigt der Zeiger ins ROM, ist es eine unsichtbare Bewegung. Der Zeichner
  dazu liegt bei `$0FED` (eigene Routine, nicht das BIOS; bei Massstab `$7F` verdoppelt er die Schritte und halbiert die Zeit).
- `$1612` vier Flipper (`$C8EC..$C8EF`) mit je drei Stellungen, `$1413` Bumperlichter, `$1160` drei Rauten,
  `$14A9` vier Spinner, `$1472` fuenf Zielreihen (jedes gesetzte Bit des Zustandsbytes ist ein stehendes Ziel: 2, 5, 3, 4,
  2), `$13E9` blinkende Pfeile, `$1550` Ballteiler-Stern bei (-10, -6), `$1A6E` Raster fuer die Kollision.
- Ausgeschaltet beim Spielstart: Sperre der Startbahn (`$C8EB`), Dreieck zwischen den Flippern (`$C8EA`), Ballretter
  (`$C8F7`, `$C8F8`).

Regeln in `vp_table10.c` (Stand 2026-10-04, nach der Anleitung; der Regelcode des ROMs ist nicht zerlegt, Deutungen stehen
im Kopf der Datei):

- Rauten (drei Rollover oben) leuchten beim Start; alle drei geloescht = Multiplikator +1 (hoechstens 6) und ein Ballretter
  wird fuer ~12 s scharf (abwechselnd links/rechts). Die Ballretter sind beim Start AUS (`disabled` in der Karte).
- Spinner 300 je Durchfahrt; mit blinkendem Bonuspfeil 2000 Bonus und der Ballteiler wird unbefristet scharf.
- Zielreihen: in der Reihenfolge abgeraeumt = alle vier Bonuspfeile an; sonst nur Punkte.
- Ballteiler (Stern in der Tischmitte): aus einem Ball werden zwei, dazu kommt der Mittelpfosten zwischen den Flippern.
- Alle fuenf Zielreihen + Pfosten = 10000. Getroffener Ballretter 1000.
- Anzeigen (`t10_shapes`): blinkende Pfeile in den Formen des ROMs (`$158E`, `$159C`, `$15AA`, `$15B8`) an den vier Spinnern
  und unter den beiden Ballrettern, Stern bei (-10, -6). Sie sind reine Anzeige (`vp_shape_line`), keine Elemente.
- NICHT geprueft: der Ballteiler loest im Dauertest nie aus (das Zufallsspiel raeumt keine Reihe in Reihenfolge ab) - von
  Hand spielen. Punktwerte, die die Anleitung nicht nennt, sind geschaetzt.

Die Spielfeldtabelle liest `spinball_map.py` direkt. Rauten, Spinner, Zielreihen und Stern haengen im ROM als Ketten
relativer Bewegungen mit wechselndem Massstab aneinander; ihre Lage stammt aus einem Lauf des ROMs in `tools/vecdump.py`
(6809-Emulator, der den Strahlweg mitschreibt; Spinball hat eine eigene Hauptschleife, Bildgrenze `--sync 00DA`) und steht
als Zahlen im Skript - auf ein bis zwei Einheiten genau, weil der Emulator die Rampenzeiten nur naehert.

Fuer den Port angepasst: Bumper sind Kreise mit Sechseck-Bild (`sides` in der Tischdatei), Spinner sind sichtbare Balken
(`barAngle`), die beim Drehen aufklappen. Nicht im ROM und darum ergaenzt: unsichtbare Waende unter dem Tisch (Ball
verloren), Boden der Startbahn, zwei Fuehler fuer die Sperre. Das Typ-Byte ist nur fuer Bumper (`$82`) und Schleudern
(`$77`, `$79`) gedeutet. Der Ball ist kleiner als auf den anderen Tischen (Radius 0.36), wie im Original.

### Verdeckung

`gfx_occ_*` in gfx.c, nach dem Vorbild des Occluders im Starter Kit (vpy3d), aber in Bildschirmkoordinaten und im eigenen
Zeichenpuffer - das 3D-Modul des Kits passt mit seinen Tabellen nicht in den freien Speicher und zeichnet an unserem Puffer
vorbei. Ein Verdecker ist ein konvexes Vieleck (bis 16 Ecken, 128 Stueck, mit umschliessendem Rechteck zum schnellen
Verwerfen); ein Strich, der durch `gfx_line` geht, solange `gfx_occ_cutting` an ist, verliert den Teil darin (Cyrus-Beck je
Verdecker, Reststuecke unter gut einer Einheit fallen weg). "Innen" beginnt 6/16 Einheit hinter der Kante, damit ein Strich
genau auf der Kante ganz bleibt. Immer an - die Option OCCLUSION gibt es nicht mehr (2026-10-07); nur der Simulator schaltet
sie mit `VP_OCCLUSION=0` zum Messen ab.

Im Spiel (`register_bodies`, `render_table`): vor dem Zeichnen werden ALLE Flaechen der Koerper angemeldet - Seitenflaechen
von Waenden, Boegen, Schleudern, Zielen, Mantel und Deckel der Bumper, Deckel und Seiten der Flipper. Gezeichnet wird weiter in
der festen Reihenfolge des Tisches. Oberkanten, Ball und alles Flache bleiben ganz; nur Fuesse und Kanten nach unten gehen
durch die Verdeckung. Das genuegt ohne Tiefenvergleich: der Fuss eines Koerpers ist zur Bildmitte hin versetzt - liegt er
dabei in der Seitenflaeche eines anderen, so steht der andere naeher an der Mitte, also davor. Kein Koerper ist mehr
durchsichtig. Verdecker ausserhalb des Bildausschnitts werden gar nicht erst angenommen.

Rechenzeit (Kern 0): ein geschnittener Strich prueft nicht alle Verdecker, sondern nur die seiner waagerechten Bildstreifen
(`occ_index`, 16 Streifen, einmal je Bild aufgebaut). Deckel sind eingebuchtete Vielecke (gerade-ungerade-Regel,
`occ_spans_poly`); dort werden Kanten weit neben dem Strich ohne Gleitkomma verworfen und der Abstandstest laeuft nur fuer
Mittelpunkte, die innen liegen. Welche Waende am Rand eines Schleuder-Klotzes liegen (`on_sling_*`), steht einmal je Tisch
in zwei Bitfeldern - vorher wurde es in jedem Bild fuer jede Wand gegen alle Deckel gerechnet. Host: `VP_PROF=1` druckt
alle 60 Bilder die Zeiten fuer Logik, Zeichnen, Strichliste, Ton und die Zahl der Verdeckungstests (2026-10-04, Tisch 8
mit 3D EVERYTHING: Zeichnen von 185 auf 60-80 us auf dem PC; auf dem RP2350 entsprechend mehr).

Massive Bloecke bekommen einen Deckel. Welche Flaechen das sind, bestimmt `tools/lids.py` vorab am PC und legt sie als
Vielecke in `core/gen/lid_data.c` ab (`make lids`; braucht numpy und opencv-python - auf Ollis Rechner in Python 3.10, das
neuere Python 3.12 im PATH hat sie nicht). Verfahren: die festen Waende rastern, vom Abschusspunkt aus fluten, wohin der
Ballmittelpunkt kommt, um den Ballradius aufblasen = was der Ball beruehren kann. Der Rest zerfaellt an den Waenden in
Stuecke; ein Stueck ist ein Deckel, wenn es nicht an den Bildrand stoesst, kein Spielelement enthaelt und sein Rand fast ganz
Wand ist. Das findet auch Bloecke, die an einer anderen Wand haengen, und eingebuchtete Formen (`gfx_occ_poly`).
`--preview shots` schreibt je Tisch ein Bild mit nummerierten Deckeln; Korrekturen von Hand stehen in `LIDS_OFF` /
`LIDS_ADD` im Skript. STAND 2026-10-04: 50 Deckel, von Olli an den Vorschaubildern aller zehn Tische bestaetigt (auf der Konsole noch zu pruefen).
Tisch 7: die obere Ebene ist nur Koerper und hell, solange der Ball dort spielt (`lvl_of`). Ohne Ball ist dort oben das Licht aus
(`v_off` in render.c): Bumper, Flipper, Ziele, Rollover und Waende der Ebene stehen auf der Stufe der Kulisse (`dim_level`),
ohne Mindesthelligkeit - hell wird das obere Spielfeld erst, wenn der Ball ueber den rechten Weg hinaufkommt. Solange er oben spielt,
tritt zurueck, was darunter liegt (`under` / `under_update`): jeder Strich einer anderen Ebene, dessen Mitte im Rechteck um
die Ziele, Bumper und Flipper des oberen Spielfelds (plus eine Einheit Rand) liegt, geht auf `dim_level`. Testbild im
Simulator: `VP_BL=1 VP_BX=40 VP_BY=240` setzt einen Ball auf Ebene 1 an (4,0 / 24,0). Die Sternkarte projiziert ihre
1536 Sterne nur noch, wenn der Blickpunkt sich bewegt hat, und hoechstens einmal je Bild (`stars_frame`; vorher in jedem
Logiktakt, 120-mal je Sekunde), die Formen ebenso. Tisch 8: Ebenen unter dem
Spielfeld werden nicht gezeichnet, Bumper bleiben flach. Bumper,
die wandern (Tisch 2), bleiben flach - ihr mitwandernder Fussring flackerte (`bumper_moves`).

Das Starter Kit (Stand 2026-10-03, sdk 51d228a = main) hat weiterhin nur den Occluder in vpy3d (Kameraraum, 64 Huellen,
Reihenfolge nah nach fern durch den Aufrufer) - fuer die Draufsicht des Tisches bleibt es beim eigenen Verfahren.

Menue und Vorspann melden ihre Verdecker selbst an, in der Reihenfolge nah nach fern (Flipper im Menue; Kugel vor Finger,
Boden und Wand; Platten der Wand vor der Schrift).

### Menue in 3D

Die beiden Flipper sind Koerper wie im Spiel (`wedge`), der Zeiger ist die Kugel des Vorspanns mit zwei Rollringen
(`roll_ball`). "PINBALL" ist wieder die schraege, doppelt gezogene Strichschrift: zwei Fassungen mit Tiefe (zwei Schichten,
dann Blockbuchstaben mit Verdeckung) bewegten sich auf der Roehre nicht ruhig und sind wieder draussen.

### Tilt: Regel

Wie am echten Tisch: zwei Warnungen sind frei, die dritte Ausloesung ist der Tilt (`tilt_warnings`, gilt je Ball). Ob ein
Stoss das Pendel ausloest, haengt vom Pegel ab (jeder Stoss +1, klingt ab) und von TILT SENSOR: LOW erst beim dritten
schnellen Stoss, MEDIUM beim zweiten, HIGH bei jedem. Nach einem Tilt rettet nichts mehr den Ball, auch nicht in der
Schonfrist am Rundenbeginn.

**Ball gerettet**: der Ball laeuft erst durch die Truhe - 1,7 s lang steht gross BALL SAVE im Bild, dann wird er von selbst
wieder eingeschossen (`save_ms`, `vp_advance`); so lange geht kein eigener Abschuss.

### Tilt: Toene und Musik

`VSFX_WARN` (DANGER) und `VSFX_TILT` kommen aus `vp_nudge`. Beide Toene erzeugt der Klangchip, ohne Aufnahme, nach dem
Vorbild der Williams-Tische der 90er, und nur sie benutzen den Saegezahn (Huellkurve des Chips als Tonquelle: Lautstaerke 16
in `step_t`, Register 11-13; im Nachbau aysynth.c ist dafuer die Huellkurvenform 8 dazugekommen). Warnung: zwei kurze, harte
Brummer, die in der Tonhoehe absacken. Tilt: ein langer Saegezahn von 325 Hz bis in den Keller, ein Rechteck faellt mit, am
Ende ein Schlag aus Rauschen. An der Buchse laufen sie auch dann, wenn dort sonst die Aufnahmen spielen (`synth_hold`).
Bei Tilt geht die Musik aus (`music_muted`) und setzt mit der naechsten Runde neu an.

**Ducking** (pcm.c): mit jedem Effekt gehen Lied, Schlagzeug und Flaeche in ~30 ms um etwa 4 dB zurueck und kommen in 0,6 s
wieder. Kostet nichts (ein Faktor je Mischblock).

**TABLE SLOPE** (Pausenbild, je Tisch, 40-150 %): Faktor auf die Schwerkraft des Tisches (`vp_field_t.slope_scale`) - wie
schnell der Ball herunterkommt. Daneben FLIPPER POWER; Stick hoch/runter waehlt die Zeile.

**Stoessel** (Option PLUNGER): ANALOG = Ausschlag des Sticks ist der Zug; DIGITAL = solange der Stick unten ist, faehrt der
Stoessel langsam herunter (voll nach 1,3 s), loslassen schiesst.

**Treffer-Blitz**: Schleudern/Kicker und Fallziele blitzen MIT Zusatzstrichen, solange das Nachleuchten steht (Olli, 2026-10-04:
es muss richtig knallen). Schleuder: Kante zwei- bis viermal obendrauf, Umriss des ganzen Klotzes (Deckel mit `kick == e`) voll
hell, links und rechts der Kante ein Strich; im Ruhezustand bleibt es bei hoechstens 9 Strichen je Schleuder. Ziel: Kante
zwei- bis viermal plus je ein Strich daneben. Die Kanten selbst gehen mit `flash_level` zusaetzlich auf volle Helligkeit.
Bumper blitzen dagegen MIT
Zusatzstrichen (Ring mehrfach, Kern, Hof, Strahlenkranz) - die Fassung ohne gefiel Olli nicht (2026-10-04, zurueckgebaut).
Aufgemalte Linien (`ignoreBall`: Rampenpfeile auf Tisch 7, Fuehrungen) bleiben in jeder 3D-Stufe flach und verdecken nichts.

**Dateien auf der Karte** werden nur einmal beim Start gesucht (`find_paths` in sound.c): jedes Suchen oder vergebliche
Oeffnen kostete mitten im Vorspann ein paar Millisekunden und liess das Bild kurz aussetzen.

**Buchse abgeschaltet** (SOUND auf CONSOLE/OFF): der Ring der Buchse wird weiter gefuettert, mit Stille - sonst spielt sie
ihren letzten Inhalt in Schleife.

### Reset-Knopf der Konsole

Am Modulschacht liegt keine Reset-Leitung. Der Knopf setzt aber auch den VIA-Baustein zurueck, und dessen Richtungsregister
DDRB (vom Kit je Bild auf $9F gesetzt) liest sich dann als 0. `uvm2_core1_gap` in platform/uvmc2.c (Haken des Kits auf dem
Kern, dem der Bus gehoert) liest es einmal je Bild und misst, wie lange der Zustand dauerte; `plat_reset_ms()` liefert das
einmal. Ab 0,5 s geht das Spiel von ueberall ins Hauptmenue. Auf der Konsole noch NICHT bestaetigt (2026-10-02).

### Mehrere Spieler

Bei jedem Wechsel steht "PLAYER n" knapp zwei Sekunden gross mitten im Bild. Die Initialen beginnen bei mehreren Spielern
leer. Eingabe: Flipperknoepfe oder Stick hoch/runter = Buchstabe, Knopf 3 = weiter/fertig, Stick links = zurueck.

### Vorspann

`game/intro.c`, Zustand `S_INTRO` vor dem Menue, jeder Knopf ueberspringt ihn (Simulator: `VP_INTRO=0` laesst ihn weg). Drei
Einstellungen mit der Kamera dicht hinter der Kugel: sie rollt auf einen Flipperfinger (als Koerper gezeichnet) zu, der
Finger schlaegt, Schnitt, die Kugel fliegt auf eine Wand aus zwoelf Platten zu, die beim Einschlag auseinanderfliegen und sich
drehen und dabei auf die Kamera zufliegen - dahinter steht, in der Ebene der Wand und von den Platten verdeckt, bis sie weg sind, "PRESENTED BY ENDPRODUKT" und bleibt stehen. Ton (`sound_intro`): eigene
Aufnahmen `snd/intro_roll.mp3|wav` (Rollen, laeuft in Schleife bis zum Einschlag nach 3,7 s) und `snd/intro_crash.mp3|wav`
(Einschlag) ueber die Buchse; fehlen sie, kommt dort die Nachbildung vom Klangchip (pulsierendes Rauschen und Brummen,
dann Krach, Schlag und klappernde Toene), die der Lautsprecher der Konsole immer spielt. Eigenes kleines 3D (Lochkamera mit Neigung, Abschneiden an der
Nahebene, `line3`) statt des 3D-Teils des Starter Kits - der ist fuer den knappen Speicher zu gross. 70-160 Striche je Bild.

Speicher: das Kit uebersetzt mit -O3. Menue, Vorspann, Bestenliste, Schrift und die Tischregeln tragen darum
`#pragma GCC optimize("Os")` (nur ARM); das spart rund 13 KB. Physik, Zeichnen, Mischer und MP3 bleiben auf Tempo.

### Flipper in REALISTIC (Vorbild: Visual Pinball X, hitflipper.cpp)

Im Original dreht der Finger vom ersten Augenblick an mit voller Geschwindigkeit. Der Ball wird weggeschlagen, sobald er
beruehrt wird, und fliegt immer zur Gegenseite (`tools/fliptest.exe 0`: +7 bis +27 Grad, egal wann man schlaegt). REALISTIC
rechnet den Finger wie VPX als Drehkoerper mit Spule und Gummi (vp_phys.c, `FLIP_*`, `POST_*`; Stand 2026-10-09):

- Spule: Moment 1000 (wie der Box2D-Motor des Originals), aber es baut sich nach dem Druecken erst ueber 0,14 s auf
  (`FLIP_RAMP`, Induktivitaet; VPX "coil ramp up"), und der Finger wird damit beschleunigt statt sofort mit Nenndrehzahl zu
  laufen (Nenndrehzahl 125 % des Originals, `FLIP_W_REAL`). Folge: ein Tipp von ein, zwei Bildern bewegt den Finger nur wenig
  (Tip Pass), ein voller Schlag nimmt den Ball mit und ist an der Spitze schwaecher als in der Mitte. `es.cy` = Zeit seit
  dem Druecken, `es.cx` = Winkel dabei.
- Am oberen Anschlag federt der Finger zurueck (Drehimpuls mal -0,3, `FLIP_BOUNCE`; VPX ebenso).
- Gummi: Rueckprall 0,8 (VPX), der mit der Aufprallgeschwindigkeit abfaellt, e = 0,8 / (1 + v/20) (`FLIP_REST_K`; VPX laesst
  ihn nur schwach abfallen), unter 0,3 Einheiten/s kein Rueckprall (`FLIP_REST_MIN`; Original 1,0). Reibung 0,6. Ein langsam
  fallender Ball springt also vom Gummi (Dead Bounce), ein harter Schlag bleibt wie abgestimmt.
- Er geht 8 Grad hoeher (`vp_flipper_top`; nicht bei Flippern, die ohnehin weit schlagen wie auf Tisch 10).
- Ecken und Enden von Waenden sind Gummipfosten: Rueckprall 0,8 auch bei langsamem Stoss (`POST_*`).

Gemessen mit `tools/fliptest.exe 1` (Tisch 1, linker Finger; `FT_DBG=1` schreibt die Ballbahn im Live-Catch-Test mit):
- A Zielen: Ball nahe am Drehpunkt -8 bis -1 Grad (Rueckhand), Mitte senkrecht, Spitze +30 Grad; Tempo bis 21,6.
- B Halten: der Ball ruht in der Mulde am hochgestellten Finger.
- D Dead Bounce: ein auf den ruhenden Finger fallender Ball springt in flachem Bogen zum anderen Finger (alle Fallhoehen).
- E Tip Pass: Ball an der Spitze, Tipp von 20-40 ms (ein bis zwei Bilder) = flacher Bogen hinueber; ab 60 ms ein Schuss.
- F Drop Catch: Finger oben, bei der Beruehrung loslassen - der Ball bleibt meist tot liegen (haengt von der Fallhoehe ab).
- G Live Catch: 100 ms oder mehr vor der Beruehrung druecken und halten - der Ball kommt in der Mulde zur Ruhe.
- C Post Pass: geht, wenn man 60-100 ms (drei bis fuenf Bilder) loslaesst und wieder drueckt - laenger wird ein Schuss.
  Dafuer musste die MULDE geaendert werden: in den Tischen des Originals endet die Einlaufwand genau am Drehpunkt und
  trifft den hochgestellten Finger unter 124 Grad (echte Tische: um 90 Grad, die Wand endet gut eine Einheit vor dem
  Drehpunkt); der gehaltene Ball lag darum bei 0,33 der Fingerlaenge und bekam beim Nachdruecken nur ~3 Einheiten/s Hub,
  noetig sind ~5. In REALISTIC sitzt das untere Flipperpaar deshalb TIEFER (`cradle_build` in vp_phys.c: so weit, dass der
  Ball am hochgestellten Finger am Wandende liegen bleibt, mindestens 0,75 Einheiten draussen; Tisch 1-3, 6, 7, 9: 0,55,
  Tisch 4: 0,60), und mit ihm das Ende der Wand auf der Aussenseite des Drehpunkts (Rand des Auslaufs) - mit dem tieferen
  Fuss allein war der Auslauf zu, der Ball klemmte neben dem Fuss (gefunden mit tools/shottest). Keine Absenkung, wo es
  kein solches Wandende gibt (Tisch 5, 8, 10) oder bei oberen Fingern. `vp_set_real_physics` schaltet Drehpunkt und Wandende
  mit der Physik um (die Werkzeuge und main.c rufen es statt `real_physics = ...`); `vp_flipper_pivot` liefert den
  wirksamen Drehpunkt. Vom Ende der Einlaufwand fuehrt eine kurze schraege Bahn (`flip_step`, Physik wie eine
  Wand, NICHT gezeichnet - sah nicht gut aus) bis auf die Oberseite des RUHENDEN Fingers, 0,6 Einheiten vom Drehpunkt (`STEP_JOIN`): der Ball
  rollt aus dem Einlauf durchgehend auf den Finger (Ollis Einwand 2026-10-09: erst eine Stufe, dann "am echten Tisch rollt
  die Kugel sauber auf den Flipper"), und am gehobenen Finger stoppt ihn dieselbe Bahn in der Mulde. Post Pass danach bei
  80-120 ms Loslassen. Alles nach unten zu schieben haette nichts gebracht:
  die Mulde entsteht nur, wenn das Wandende HOEHER liegt als der Drehpunkt. Ein dicker Fingerfuss (VPX: 0,43/0,26) haette nichts gebracht, der Ball rollt dann auf den Fuss.
- Schusskarte danach (tools/shottest, SHOT_MAP, mit abgesenkten Flippern): 0 verlorene Ziele bis auf drei schon im Original
  knappe (Tisch 7 TopRollovers.1/.2, Tisch 9 ein Sensor, Fenster 0,35-0,57 % -> 0,03-0,17 %); 8 h Dauerspiel je Tisch ohne
  ungetroffenes Ziel ausser Tisch 8 `SpecialTarget` (s. u.) und `InlineTargets.3/4` (dort selten, 0,2-0,5 je Stunde).
  Dauertest 10/10 in beiden Modi.

Vorsicht Speicher: `.bss` beginnt auf einer 8-KB-Grenze. Wachsen Code und Daten ueber die naechste Grenze, kostet das auf
einen Schlag 8 KB (Stand: 7,7 KB Luft bis `0x2005A000`, 11,6 KB frei am Ende). Platz schafft `#pragma GCC optimize("Os")`:
das Kit baut mit -O3, aber die Zeit je Bild bestimmt der Strahl - nur vp_phys.c, gfx.c, der Mischer und MP3 bleiben auf Tempo.

**Schlagkraft und Neigung je Tisch** sind fest abgestimmt (siehe "Sind alle Schuesse zu schaffen?"); die Zeilen FLIPPER POWER
und TABLE SLOPE im Pausenbild gibt es nicht mehr. `vp_field_t.flip_scale` / `slope_scale` bleiben fuer die Testwerkzeuge.

### Stoss gegen den Tisch (nicht im Original)

Optionen dazu: NUDGE STRENGTH (WEAK 0,35 / NORMAL 0,6 / STRONG 1,0 - STRONG ist die erste Fassung, NORMAL die Vorgabe),
ANALOG (`nudge_analog`: der groesste Ausschlag des Sticks bestimmt die Wucht, 25 % bei knappem Ausschlag bis 100 % =
STRONG am Anschlag; ein sanfter Stoss reizt den Tilt entsprechend weniger),
INVERT NUDGE (tauscht links und rechts), TILT SENSOR (Vorgabe MEDIUM; LOW: Tilt beim vierten schnellen Stoss, Pegel klingt in 3,5 s je
Stufe ab; MEDIUM: beim dritten, 5 s; HIGH: beim zweiten, 6,5 s - man muss gut drei Sekunden zwischen zwei Stoessen warten).
Neue Felder in `settings_t` nur anhaengen: `settings_load` nimmt auch eine aeltere, kuerzere Datei an.


Stick links/rechts/hoch gibt allen Baellen einen kleinen Impuls (seitlich 1,3, nach oben 1,8 - die Groessenordnung des
"Bump!" des Originals), das Bild ruckt kurz mit. Jeder Stoss fuellt einen Zaehler, der in ~1,5 s Echtzeit um eins abklingt:
ueber 2,2 Warnung DANGER, ueber 3,2 TILT - die Flipper bleiben tot, bis der Ball verloren ist.

Pruefen: `make test` (Dauerlauf mit Flipper-Automat ueber alle Tische: kein Ball darf das Feld verlassen, keine
ungueltigen Werte), `tools/trace.exe <tisch> <sekunden> <0|1>` zeigt die Ballbahn als Text.
Gemessen (PC): 4-10 Mikrosekunden Rechenzeit je Bild; auf dem RP2350 (FPU) geschaetzt deutlich unter 1 ms.

## Bild

- Ansicht: der ganze Tisch, 10 Bildschirmeinheiten je Feldeinheit (200 x 300 von 282 x 378 Einheiten der Roehre),
  darueber die Punktezeile. Lage, Groesse und Helligkeit sind einstellbar (ADJUST).
- **Zoom** (Option OFF, 1.1X .. 1.5X; 1,5 ist der Wert des Originals und das Maximum, Vorgabe 1.3X): ist genau ein Ball im
  Spiel, faehrt die Ansicht in ~0,45 s auf diesen Faktor und folgt
  dem Ball, ohne ueber den Tischrand zu schauen; `gfx.c` schneidet alles am Tischfenster ab. Nebenwirkung: es sind weniger
  Striche im Bild (Tisch 7: 366 -> ~140), die grossen Tische laufen damit deutlich fluessiger. Auf der Konsole zu pruefen:
  ob das staendig wandernde Bild ruhig steht.
- **Farbe -> Helligkeit** (`render_intensity`): hellster Farbkanal und Leuchtdichte gemischt (reine Leuchtdichte
  liesse die blauen Waende verschwinden), Alpha dunkelt ab, Schwarz ist unsichtbar. Dazu fest +30 (`BASE_BRIGHT`): das ist der
  Wert, den Olli auf seiner Roehre als gut eingestellt hat; BRIGHTNESS im Einstellbild zaehlt obendrauf.
- **Wichtiges ist heller als die Kulisse** - ueber Mindesthelligkeit und, weil die Helligkeit oben begrenzt ist, ueber mehr
  Strahl auf kleinem Raum: Flipper volle Helligkeit und als Keil aus drei Strichen; Schleudern/Kicker/Ballretter doppelt
  gezogen; Bumper mindestens 120 mit Kern; Ball zwei Ringe (zwoelf + sechs Striche); aktive Rollover, Ziele, Spinner heller.
- **Nachleuchten**: ein getroffener Bumper oder Kicker ist sofort voll hell und klingt in `VP_FLASH_TIME` (0,7 s Spielzeit,
  gut 0,25 s echt) weich ab; solange leuchtet beim Bumper ein innerer Ring, beim Kicker ein zweiter Strich daneben.
- **Helligkeitsstufen** (render.c, `wall_level`/`dim_level`/`at_least`): das Bild lebt von klar getrennten Stufen, denn das
  Auge sieht Verhaeltnisse - 100 gegen 127 ist auf der Roehre kaum ein Unterschied. Kulisse (ruhende Rampen, 38 % der
  Wandstufe) < Waende (90 + BRIGHTNESS, begrenzt auf 40..112) < Wichtiges (Bumper, Schleudern, Ziele, Spinner: feste
  Mindestwerte 112-120) < Voll (Ball, Flipper, Aufleuchten). BRIGHTNESS bewegt nur Waende und Kulisse; ein fester
  Aufschlag auf alles (frueher +30) schob alle Stufen an die Obergrenze und nahm dem Bild die Tiefe.
- **Rampen** (hoehere Ebenen nur aus Waenden): Kulisse, solange kein Ball darauf ist; rollt einer darueber, blenden sie auf
  volle Helligkeit und werden doppelt gezogen (hin und zurueck) - gut das Siebenfache an Licht. Ein zweites Spielfeld
  ueber dem ersten (Ebene mit Bumpern, Flippern oder Zielen: der kleine Tisch oben links auf Tisch 7) ist dagegen immer
  hell (mindestens 118). Ebenen unter dem Spielfeld (Tisch 8) haben 55 % ihrer Helligkeit.
- **Sterne auf Tisch 7 funkeln**: jeder Stern in seinem eigenen Takt, abgeleitet aus seinem Ort.
- **Ball in der Startbahn**: ist kein Ball im Spiel, liegt der naechste sichtbar auf dem Stoessel, bis ein Knopf ihn abschiesst.
  Der Stoessel gehoert zum Tisch und ist immer zu sehen: von oben ein Abschussgummi (zwei Striche), darunter die Feder als
  Zickzack, die sich beim Ziehen zusammendrueckt (`draw_plunger`, 8 Striche). Faellt ein zu schwach geschossener Ball
  zurueck, bleibt er an der Abschussstelle auf dem Gummi liegen (`vp_catch_returned_ball`) und wartet wieder.
- **DETAIL**: LOW = Boegen mit halb so vielen Strichen, hoechstens 16 Zierpunkte; MEDIUM = volle Boegen, 28 Punkte; HIGH =
  dazu die Aussenringe der Bumper, ruhende Rollover als Kreis statt Punkt, 48 Punkte.
- **3D** (Option, `g_set.view3d`): die helle Oberkante liegt, wo die Physik rechnet; der dunklere Fuss ist zum Fluchtpunkt
  (Mitte des Tischfensters) hin versetzt um `FLIP_RISE` x Zoomfaktor x Abstand (`foot`, `body_seg`) - mit Zoom wandert er,
  waehrend die Ansicht dem Ball folgt. Nur Darstellung. FLIPPERS: Finger als Klotz (10 Striche statt 3). OBJECTS (Vorgabe):
  dazu Bumper als Walze (zweiter Ring, zwei Mantellinien), Schleudern/Kicker/Ballretter und Ziele mit Fuss und Kanten
  (+3 je Stueck). EVERYTHING: dazu alle Waende und Boegen der Spielflaeche - das verdoppelt die Strichzahl ungefaehr und
  ist die Vorgabe. Passt das Bild damit nicht in Strichpuffer und Befehlsliste (ueber 520 Striche, voller Tisch ohne
  Zoom auf den grossen Tischen), bleiben die Waende von selbst flach (`walls3d_update`).
- **3D nur mit Zoom**: der ganze Tisch (Rundenbeginn, mehrere Baelle) wird immer flach gezeichnet (`view3d()` in render.c),
  und die Zoomstufe laesst sich nicht unter 1,4 (EVERYTHING) bzw. 1,2 (OBJECTS, FLIPPERS) stellen (`settings_min_zoom`).
- **Zoom**: OFF, 1,2 bis 2,0 in Schritten von 0,2 (Vorgabe 1,6). INFO steht nicht mehr im Menue (Simulator: `VP_INFO=1`).
- **Rollring**: die obere Haelfte eines Rings um die Rollachse, 5 Striche; wandert von hinten ueber die Kuppe
  nach vorn (`draw_ball_roll`). Nur ein Ring, sonst steht das Muster bei schnellem Ball scheinbar still.
- **Ball**: ein Ring aus zwoelf Strichen, zweimal gezogen (kein Innenkreis mehr). BALL BRIGHTNESS im Einstellbild: unter 0
  dunkler, ueber 0 je 10 ein weiterer Durchgang (bis fuenf) - mehr Licht geht bei voller Helligkeit nur ueber mehr Strahl.
- **Stoessel am Stick**: Stick nach unten ziehen spannt (analog, Ball und Stoessel wandern mit), loslassen schiesst mit
  50-100 % der vollen Geschwindigkeit (`vp_launch_power`; es zaehlt der groesste Zug der letzten sechs Bilder, weil der
  Stick beim Loslassen durch die Mitte schnellt). Ein zu schwacher Schuss rollt in die Startbahn zurueck und kann neu
  abgeschossen werden (`vp_launch_ready` erkennt den liegenden Ball ueber die launchDeadZone). Die Knoepfe schiessen
  wie bisher mit voller Staerke.
- **Ballspur**: wird nicht gezeichnet. Der Ball ist das hellste Objekt (volle Helligkeit, rund aus zwoelf kurzen Strichen, mit 85 % seines Radius gezeichnet = langsamer
  Strahl); die Spur entsteht aus dem Nachleuchten der Roehre. Der Simulator ahmt das mit einem abklingenden Bild nach.
- **Strahlbudget**: Kit-Angabe ~136 Buszyklen je Strich bei 30000 je 50-Hz-Bild; gemessen bei Dracula 233 Striche =
  56 Bilder/s. Die Laenge eines Strichs kostet fast nichts, die Anzahl alles. Darum:
  - Boegen werden mit eigener Punktliste gezeichnet (Pfeilhoehe <= 0,07 Feldeinheiten statt bis zu 30 Stuecke),
  - `gfx.c` kettet Striche und verlaengert Striche in gleicher Flucht,
  - die Zeichenreihenfolge der Elemente ist vorab so geordnet, dass der Strahl wenig springt, und bleibt fest,
  - eigene Schrift mit ~3,5 Strichen je Zeichen.
  Strichzahl im Spiel (DETAIL MEDIUM, Simulator): Tisch 1: 183, 2: 178, 3: 200, 4: 192, 5: 213, 6: 325, 7: 366,
  8: 385, 9: 358 (je Ball zwoelf Striche). Die Tische 1-5 sollten 50-60 Bilder/s erreichen, 6-9 voraussichtlich 30-40. Das ist auf der
  Konsole zu messen (Menue: INFO ON zeigt F = Bilder/s, S = Striche, J = Spruenge, B/X/O = Buszyklen/verloren/Ueberlauf,
  A = Ton und Karte, L = Spiel und Zeichnen auf Kern 0, beide in 0,1 ms; liegt L nahe 200, bremst Kern 0 und nicht der Strahl).
- Winkelfunktionen: `core/fmath.h` (`fm_sinf`, `fm_cosf`, `fm_sincos`) ueberall statt sinf/cosf - die des Kits rechnen auf
  dem RP2350 innerlich mit double, das der Cortex-M33 nur in Software kann (ueber tausend Takte je Aufruf; Kreise, Ball,
  Sterne, Flipper brauchten einige hundert je Bild). Kreise (`gfx_circle`) drehen ausserdem Ecke fuer Ecke weiter statt je
  Ecke neu zu rechnen.
- DETAIL: immer HIGH, die Option ist weg (Olli, 2026-10-09; inaktive Rollover als Umriss - als Punkt sah man die Lampen
  nicht). Die Stufen im Code (`g_set.detail`): LOW (Boegen groeber, weniger Sterne, inaktive Rollover nur als Punkt), MEDIUM (inaktive Rollover als dunkles Viereck,
  vier Striche - als Punkt sah man die Lampen nicht, Olli 2026-10-09), HIGH (aeussere Bumperringe, Rollover rund).

## Sind alle Schuesse zu schaffen? (tools/shottest, 2026-10-07)

`tools/shottest.exe <tisch> [flipper %] [neigung %] [stunden] [real 0|1] [seed]` (`make tools/shottest.exe`), zwei Arten:

- Dauerspiel: ein zielender Automat spielt viele Stunden Spielzeit (waehlt je Ballkontakt die Schlagstelle auf dem Flipper,
  faengt in 40 % der Faelle erst und schiesst dann kontrolliert) und zaehlt je Ziel die Treffer je Stunde: Bumper, jedes
  Fallziel, jeder Rollover, Sensoren (Rampen, Bahnen), Spinner, Schleudern. `SHOT_SUM=1` = eine Zeile, `SHOT_DBG=1` meldet,
  wo ein Ball 8 s liegen bleibt.
- Schusskarte (`SHOT_MAP=1`): gezielte Schuesse von jedem Flipper an 150 Schlagstellen, gefangen / rollend / aus dem Lauf,
  der Tisch je Schuss frisch. Je Ziel der Anteil der Schuesse, die es treffen = Breite des Trefferfensters.
  `python tools/shotsweep.py <tisch> [flipper:neigung ...]` stellt Abstimmungen neben ARCADE (= Physik des Originals).

Befund: mit REALISTIC waren die langen Schuesse nicht zu schaffen - 47 Ziele ueber alle Tische hatten weniger als 30 % ihres
Fensters im Original oder gar keins (Rampen Tisch 4, 5, 9; Orbits Tisch 6; Schleifen und obere Rollover Tisch 7). Ursache:
der beschleunigende Schlag begann mit 45 % der Nenngeschwindigkeit; ein Ball an der Flipperspitze wird gleich zu Beginn
getroffen und ging mit 60-75 % des Originaltempos weg. FLIPPER POWER half kaum (das Motormoment begrenzt). Loesung, flacher
statt staerker (Ollis Vorgabe):

- Schlag: zuerst `FLIP_W_LO` 0,45 -> 0,70; am 2026-10-09 durch das VPX-Modell ersetzt (Moment mit Aufbau, s. "Flipper in
  REALISTIC"), Schusskarte danach erneut geprueft.
- feste Neigung und Schlagkraft je Tisch (`TABLE_SLOPE` / `vp_slope`, `TABLE_FLIP` / `vp_table_flip` in vp_field.c, nur
  REALISTIC; ARCADE bleibt beim Original). Stand 2026-10-09 nach der VPX-Flipperphysik, aus einem Raster Kraft 100-130 % x
  Neigung 70-100 % je Tisch die kleinste Abweichung ohne verlorenes Ziel und mit moeglichst wenigen unter 70 % des
  Originalfensters (Olli: die Schuesse muessen bequem kommen):
  Tisch 1: Neigung 64 %, Kraft 120 %; 2: 100/110; 3: 77/100; 4: 64/110; 5: 68/100; 6: 68/100; 7: 64/120; 8: 72/110;
  9: 60/100; 10: 90/100. Ergebnis der Schusskarte: 0 verlorene, 42 schwache Ziele (vorher 5/81). Der Stoessel schiesst um die Wurzel der
  Neigung sanfter, damit der Ball so hoch kommt wie vorher.
- Ergebnis: Schusskarte 0 verlorene Ziele auf allen Tischen (auch bei +-6 % Neigung noch hoechstens 2), 20 h Dauerspiel je
  Tisch ohne ungetroffenes Ziel - ausser `SpecialTarget` auf Tisch 8, das auch mit der Physik des Originals nie faellt (haengt
  am Spielstand, nicht an der Physik; offen).
- TABLE SLOPE und FLIPPER POWER sind aus dem Pausenbild heraus (Olli, 2026-10-07): die Abstimmung ist fest. Ihre Plaetze in
  `settings_t` bleiben ungenutzt stehen, damit die Datei ihre Form behaelt. Zum Probieren: `tools/shottest.exe` nimmt
  beide weiter als Prozent auf die Abstimmung (`flip_scale`, `slope_scale` im Kern).

Nebenbefund: auf Tisch 10 klemmte der Ball oben rechts zwischen der Zielreihe und der Startbahn-Sperre fest (stundenlang,
"Bump!" half nicht) - die Reihe stellte sich unter dem Ball wieder auf. `vp_drop_reset` wartet jetzt, bis kein Ball in der
Reihe steht (alle Tische).

## VERSUCH 3D ohne Zoom, Knopf 2 (2026-10-04)

`settings_min_zoom()` liefert 0 und `view3d()` (render.c) kennt keine Zoomschwelle mehr: jede 3D-Stufe geht auch bei
ganzem Tisch (ZOOM 1.0). Zum Zurueckstellen steht die alte Bedingung als Kommentar an beiden Stellen (EVERYTHING ab 1,4-fach,
OBJECTS/FLIPPERS ab 1,2-fach, 3D nur bei `v_zf > 1.08`). Der Strichgrenzwert (`STROKE_LIMIT`, Waende fallen dann auf flach)
greift weiter. Im Spiel: Knopf 2 oeffnet wie frueher die Pause; dort stehen ZOOM und 3D MODE (wirken sofort, werden gespeichert). Host-Messung Tisch 7, 3D EVERYTHING, ZOOM 1.0: ~460 Striche (mit
Zoom ~220), Zeichnen 14 us, ~60000 Buszyklen = ~24 Bilder/s (Schaetzung des Hosts).

## Menue

Eine gewohnte Liste mit Flipper-Gesicht, ohne Tisch im Hintergrund: der Rahmen ist der Umriss eines Tisches, unten sitzen
zwei Flipper (sie schlagen beim Waehlen, und jeder Schlag klingt wie im Spiel: der Flipperton ist der Menueton, links/rechts
im Stereobild der Buchse; Olli, 2026-10-10), oben blinken drei Bumper, der Zeiger ist eine Kugel, die von Zeile zu Zeile rollt,
der Titel steht schraeg und doppelt gezogen. Stick hoch/runter waehlt, links/rechts aendert einen Wert (der gewaehlte Wert
steht zwischen < >), Knopf 4 fuehrt aus (bei einem Wert: naechster Wert), Knopf 1 geht zurueck.
- Hauptseite: PLAY (rechts daneben die Spielerzahl 1-4), TABLE, OPTIONS, darunter die vier besten Ergebnisse des Tisches.
- OPTIONS (rollt, sieben Zeilen sichtbar): BALLS, BALL SPEED, PHYSICS, ZOOM, SOUND, MUSIC, MUSIC VOLUME, DETAIL, INFO,
  ADJUST SCREEN, ABOUT, BACK. Unten der Zustand der Tondateien.
- ABOUT (drei Seiten): Original von Brian Nenninger (dozingcat) samt Adresse, Ton von pdx / Twittering Machine, Tisch 8 von
  Jack Morgan, Tisch 10 aus Spinball; Vectrex-Fassung Oliver Carell (Endprodukt) mit Jahr (`PORT_YEAR` in main.c); GPL 3.
Im Simulator sind die Pfeiltasten der Stick, die Flipper liegen auf den Umschalttasten.

Bestenliste (`hiscore.c`, `pinball/scores.bin`): je Tisch vier Plaetze mit drei Initialen. Nach einem Spiel, das einen Platz
erreicht, folgt die Eingabe: Stick hoch/runter = Buchstabe, Knopf 4 = weiter, Knopf 1 = zurueck. Freies Spiel zaehlt nicht.

## Bild einstellen (Menue: OPTIONS -> ADJUST SCREEN)

Zweck: geschlossene Objekte und saubere Geometrie auf der eigenen Roehre - so, dass man ohne Vorwissen weiss, worauf zu
achten ist. Eingestellt wird am ECHTEN TISCH (ruhender Ball, Punktezeile wie im Spiel). Ein eigenes Pruefbild gab es in
einer frueheren Fassung; Olli fand, es mache alles schlimmer - nicht wieder einfuehren.
Der Text (Name und Wert, worauf achten, Tasten) steht mitten im Tisch und waechst mit ihm; unter dem Tisch waere er weg,
sobald man das Bild gross zieht. Stick hoch/runter = Punkt, links/rechts = Wert, Knopf 3 = Wert zuruecksetzen,
Knopf 4 = speichern, Knopf 1 = verwerfen.

| Punkt | am Tisch darauf achten | was es ist |
|---|---|---|
| MOVE LEFT/RIGHT, MOVE UP/DOWN | Tisch mittig | Lage des Spielbildes |
| PICTURE SIZE | gross, aber nichts abgeschnitten | Groesse des Spielbildes |
| BRIGHTNESS | Waende dunkel, Ball am hellsten | Helligkeit des Tisches |
| BALL GLOW | senken, wenn der Ball schmiert | Helligkeit des Balls |
| SCORE HEIGHT | - | Hoehe der Punktezeile |
| CLOSE SHAPES | Ball und Bumper muessen sich schliessen | Nachlauf je Strich (Kit: TAIL) |
| STRAIGHTEN | die Ziffern der Punktezeile stehen in einer geraden Reihe | Nullpunkt (Kit: ZERO) |
| RECENTER DISTANCE | senken, wenn Teile gegeneinander verrutschen | Nachzentrieren bei Spruengen ab so vielen Einheiten (Vorgabe 24) |

Zwei fruehere Punkte gibt es nicht mehr (Befund auf der Konsole): LINE LENGTH (Strichlaenge der Roehre, Kit: SCALE) machte
einfach alles groesser oder kleiner, also dasselbe wie PICTURE SIZE; RECENTER COUNT (Nachzentrieren nach N Strichen) liess
das Bild zucken ("sieht giftig fuer die Vectrex aus") und ist jetzt fest aus.

CLOSE SHAPES und STRAIGHTEN gehoeren der Konsole (`config/uvm2.cfg`, gelten fuer alle Spiele - das Bild sagt es
dazu); die uebrigen dem Spiel (`pinball/settings.bin`).

## Ton

- **Lautsprecher der Vectrex** (Klangchip): nur Effekte, keine Musik. Glocken (sechs Toene wie die sechs Aufnahmen des
  Originals), Rollover (Fuenftonleiter), Flipper-Klacken, Bumper-Schlag, Ballstart, Meldung, Start/Ende, Ballverlust.
- **Buchse des UVMC2 und Simulator**: die Aufnahmen des Originals, mit dessen Lautstaerken und Tonhoehen
  (`VPSoundpool.java`): Glocke 0,5 (nie zweimal dieselbe), Rollover 0,3 in bis zu drei Tonhoehen, Ballstart und Flipper 1,0,
  Meldung 0,66, Spielstart/-ende 0,5. Musik wie dort: nach je 20 Punkte-Treffern das Schlagzeug (falls es nicht laeuft),
  der Flaechenklang nach 10 Treffern, danach in wachsenden Abstaenden (+42); Spielstart und -ende setzen zurueck.
  Option MUSIC schaltet sie ab.
- Die Aufnahmen sind **WAV-Dateien** in `pinball/snd/` auf der Karte: `flipper`, `launch`, `message`, `start`, `rollover`,
  `ding1`..`ding6`, `pad`, `drums` (`.wav`, 16 Bit, mono oder stereo, beliebige Rate; zusammen hoechstens 7,5 MB). Erzeugt
  mit `make sounds` (`tools/audio2pcm.py`, braucht `pip install soundfile`) nach `sdcard/pinball/snd/`: Effekte und
  Flaechenklang 32 kHz, die 72-s-Schlagzeugspur 24 kHz. Eigene Aufnahmen: Datei gleichen Namens hinlegen.
  MP3 geht nicht (dafuer braeuchte es einen Decoder, der RAM ist knapp).
- Auf dem UVMC2 werden die Dateien nacheinander in das PSRAM geladen, waehrend das Spiel schon laeuft (4 ms je Bild im
  Spiel, 15 ms im Menue); jede spielt, sobald sie ganz da ist, kurze Effekte zuerst, Musik zuletzt. Mischer: `game/pcm.c`,
  8 Effektstimmen + 2 Musikstimmen.
- **Das PSRAM muss das Image selbst einschalten**: `UVM2_PSRAM_START = 1` im Makefile. Das Kit faehrt es sonst nur hoch,
  wenn ein Spiel ein ROM-Archiv hat. Ohne den Schalter blieb `uvm2_psram_ready` auf -1, die Aufnahmen wurden nie geladen
  und die Buchse spielte still die Ersatz-Effekte - so lief der erste Hardwaretest ("klingt nicht wie Android, keine Musik").
- Damit das nicht wieder raten heisst: die Seite OPTIONS zeigt unten den Zustand - `JACK: 13/13 SOUND FILES`,
  `JACK: LOADING SOUNDS n/13`, `JACK: NO SOUND FILES FOUND` oder `JACK: NO SOUND MEMORY` (PSRAM antwortet nicht).
- Fehlen die Dateien oder das PSRAM, kommen ueber die Buchse die Chip-Effekte als Nachbildung (`aysynth.c`).
- **Eigene Musik**: eine WAV-Datei `pinball/snd/<n>_beliebigername.wav` wird fuer Tisch n (1 bis 10) gespielt, `0_...wav` im
  Menue - in Schleife, mit der Lautstaerke MUSIC VOLUME (0-10, gilt fuer alle eigenen Lieder). Gibt es fuer einen Tisch keins,
  laeuft die Musik des Originals. Die Lieder duerfen beliebig lang sein: sie werden von der Karte gestreamt (Ringpuffer von
  512 KB am Ende des PSRAM, `pcm_song_*`), 16 Bit, mono oder stereo, beliebige Rate.
- **MP3**: `<n>_name.mp3` geht ebenso (gesucht wird erst `.wav`, dann `.mp3`). Dekoder: minimp3 (CC0,
  `third_party/minimp3`, eingebunden ueber `game/mp3.c`: nur Layer 3, ohne SIMD). Im Ring liegen dann die Bytes der Datei,
  dekodiert wird Rahmen fuer Rahmen beim Mischen (`mp3_next` in pcm.c); ein ID3v2-Kopf wird uebersprungen, nicht durchsucht.
  Rechenzeit: das Abbild laeuft mit `UVM2_DUAL_CORE` (Vorgabe des Kits) - Kern 1 spielt die Strichliste ab, Kern 0 rechnet
  das naechste Bild. Der Dekoder laeuft auf Kern 0 in der Zeit, in der Kern 1 zeichnet; teurer wird das Bild erst, wenn
  Kern 0 laenger braucht als der Strahl. INFO ON zeigt die Zeit fuer Ton und Karte je Bild als `A` (in 0,1 ms).
  Speicher: 18 KB Code, 6,7 KB Zustand, 16 KB Arbeitsbereich (im Original auf dem Stapel, hier statisch - der Stapel hat
  2 KB; `MINIMP3_SCRATCH_STORAGE` ist die einzige Aenderung an minimp3.h), 6,6 KB Puffer. Dafuer verkleinert: Sternkatalog
  in 16 Bit (-12 KB), Befehlsliste des Kits 6656 statt 8192 (-9 KB; reicht fuer ~560 Striche, Kontrolle `X` = 0),
  Strich- und Rasterpuffer. Frei bleiben 5,6 KB; es wird kein malloc benutzt.
  `python tools/song2wav.py E:/pinball/snd` wandelt weiterhin nach WAV (auch ogg, flac) - noetig ist das nur noch fuer
  andere Formate oder falls MP3 auf der Konsole die Bildrate drueckt.
- **Menuemusik vom Klangchip**: liegt kein `0_*`-Lied auf der Karte (oder gibt es keine Buchse), spielt im Menue eine kleine
  eigene Schleife auf dem Chip (`music_tick` in sound.c: a-Moll, acht Takte, Zupfstimme, Bass, Takt aus Rauschen) - am
  Lautsprecher der Konsole und nachgebildet an der Buchse. MUSIC OFF schaltet sie ab, Effekte haben auf ihrem Kanal Vorrang.
  Im Spiel bleibt der Chip bei Effekten.
- **Stereo**: die Buchse ist stereo (`uvm2_jack_write_lr`, Starter Kit ab 30.09.2026). Eigene Lieder laufen stereo. Effekte
  liegen dort, wo der Ball ist (am Tischrand etwa 70 % zur Seite), der Flipperschlag auf der Seite des Flippers, Meldungen
  und die Musik des Originals in der Mitte. In der Mitte sind beide Kanaele voll, zur Seite wird nur der andere leiser -
  nichts wird lauter als vorher in Mono. Der Klangchip der Vectrex bleibt mono.
- Musik des Originals: dort startet das Schlagzeug ohne Wiederholung, nach 72 s war also Stille bis zum naechsten 20. Treffer.
  Hier laeuft es, einmal gestartet, in Schleife bis zum Spielende.
- Verzeichnis lesen kann das Kit nicht; `platform/uvmc2.c` ruft dafuer FatFs direkt (`f_opendir`), das der Kartenleser des
  Kits ohnehin eingehaengt hat.

## Speicher (RP2350)

STAND 2026-10-09 (die Zahlen im uebernaechsten Absatz sind aelter): `__end__` 0x20076C4C = 37 KB frei bis zur
Stapelgrenze, `__bss_start__` 0x200503D0 = 7,0 KB bis zur naechsten 8-KB-Grenze (dahinter kostet jedes Byte Code 8 KB RAM).
Gewonnen durch die Tischdaten: Strecken und Bogenpunkte liegen als int16 in 1/1024 Feldeinheit (`vp_cseg_t`, `VP_CQ`,
Zugriff ueber `vp_seg(t, i)`; `tables2c.py` prueft den Wertebereich) statt als float - 40 KB weniger RAM, das Abbild laeuft
komplett aus dem RAM. Weitere Reserven, falls noetig: die Elementbeschreibungen (`vp_elem_def_t`, 80 Byte x ~870 = 70 KB,
davon 32 Byte Parameter als float), die Namen der Elemente (~10 KB Text, nur fuer `vp_is`), der Zustand von Tisch 7 (7,8 KB,
Sternprojektion als float), Formen in `vp_field_t` (160 x 32 Byte).
`-DUVM2_REPLAY_LAST`: kommt kein neues Bild rechtzeitig, zeichnet Kern 1 das letzte noch einmal (gegen die Aussetzer im
Vorspann; dazu laedt `sound_frame(0)` im Vorspann keine Aufnahmen von der Karte). Auf der Konsole noch nicht bestaetigt.
NUR im Vorspann aktiv (`plat_redraw_last`, Riegel `uvm2_replay_hold` des Kits): im Spiel halbierte es fast die Bildrate -
ist die Logik nur wenig langsamer als der Strahl, wartet jedes neue Bild ein ganzes Wiederholbild ab (Olli, 2026-10-04).

`.um2` 359 KB; im RAM belegt bis 0x20076CA0, die Stapelgrenze liegt bei 0x20080000 - knapp 38 KB frei. Text 363 KB (davon
~175 KB Tischdaten und Sternkatalog), BSS 128 KB. Zwei Schalter im Makefile halten das BSS klein: `UVM2_ROMZIP_MAX=1024`
(das Spiel hat kein ROM-Archiv; der Puffer des Kits dafuer kostet sonst 24 KB) und `VPY_SB_MAX=720` (Strichpuffer von
libvpy). Neue grosse Puffer vermeiden (Ton liegt darum im PSRAM), vor jedem Zuwachs `arm-none-eabi-size` ansehen.

## Stand

Fertig und im Simulator geprueft: 10 Tische mit Regeln, Physik (ARCADE und REALISTIC), Tempo, Zoom, Stoss/TILT, 1-4 Spieler,
Bild, Punktezeile, Meldungen, Menue, Pause, Bestenliste mit Initialen, Bildeinstellung am Tisch, eigene Musik, Chip-Effekte,
Originalton samt Musik aus WAV-Dateien, `.um2` baut.

Olli testet auf der Konsole (Bild und Spiel laufen gut). Dort noch zu klaeren:
1. Bildrate je Tisch (INFO ON), ohne und mit ZOOM. Reicht sie nicht (DETAIL ist fest HIGH): naechste Stufe waere, die feste Kulisse auf
   zwei Bilder zu verteilen (Ball und Flipper weiter in jedem Bild).
2. Steht das Bild bei ZOOM ruhig (die Ansicht wandert staendig, die Strichfolge aendert sich)?
3. Schliessen der Figuren und Lage der Elemente zueinander (ADJUST SCREEN), Wirkung von RECENTER.
4. Helligkeitsstufen: sind Waende, Ziele, Flipper, Ball klar zu unterscheiden? Reicht das Nachleuchten als Ballspur?
5. Buchse: zeigt OPTIONS `JACK: 13/13 SOUND FILES`? Wie lange dauert das Laden, stoert es das Bild? Stoert das eingeschaltete
   PSRAM das Bild (im Kit gab es dazu Befunde, siehe `uvm2_pico_main.c`)?
6. Menue: die Seite OPTIONS hat ~400 Striche - flackert sie zu stark?

Nicht per Automat geprueft: Multiball-Ablaeufe und seltene Regeln der Tische (Jackpots, Kartenhaende, Sternbilder);
sie sind nach dem Java-Code uebertragen.
