# Projektkontext fuer Claude Code

Fork von dozingcat/Vector-Pinball (Android, Java, GPL 3). Auf dem Zweig `vectrex-port` entsteht in `vectrex/`
die Fassung fuer Vectrex + UVMC2 (Ultimate Vectrex Multicart 2, RP2350). Der Java-Code unter `app/` bleibt
unveraendert: er ist die Referenz und die Quelle der Tischdaten.
Sprache fuer Kommentare und Doku: Deutsch. Alles auf GitHub (Commits, Releases, PRs, Tags): Englisch.
Sichtbare Texte im Spiel: Englisch (wie im Original).

Architektur, Stand, Befunde: `vectrex/docs/ARCHITECTURE.md`. Werkzeuge und Bauen: `vectrex/docs/SETUP.md`.
Schwesterprojekt mit derselben Werkzeugkette und vielen Hardware-Erfahrungen: `F:\DraculaVectrexPort`
(dort `docs/OPTIMIERUNG.md` und `uvmc2-starter-kit/docs`, zuerst `09-source-map.md`).

## Regeln
- Eine Spiellogik: `vectrex/core/` (C, keine Grafik, keine Dateien). Host-Simulator und `.um2` bauen dieselben Quellen;
  nur `vectrex/platform/` unterscheidet sich.
- Tischdaten nie von Hand aendern: `core/gen/*.c` entsteht aus `app/src/main/assets/tables/*.json` (`make data`).
- Tischregeln (`core/vp_tableN.c`) folgen den `FieldNDelegate.java` Zeile fuer Zeile; bei Zweifeln gilt das Original.
- Physik (`core/vp_phys.c`) bildet Box2D mit den Stoffwerten des Originals nach. Aenderungen mit `make test`
  (Dauerlauf aller Tische: kein Ball darf das Feld verlassen) und `tools/trace.exe` absichern.
- Strahlbudget: jeder Strich kostet Bildrate (~100-136 Buszyklen), die Laenge fast nichts. Striche ketten, feste
  Zeichenreihenfolge, nichts von Bild zu Bild umsortieren (fuehrte bei Dracula zu springenden Objekten).
- Der Simulator ersetzt die Roehre nicht: Flackern, Helligkeit, Schliessen von Figuren nur auf Hardware beurteilbar.
- C-Quellen mit dem Schreibwerkzeug anlegen, nicht per Shell-Heredoc (Anfuehrungszeichen/Backslashes gehen kaputt).
- `mingw32-make` immer mit `< /dev/null` starten.
- Keine Patch-Dateien schicken, Aenderungen direkt im Code machen.
