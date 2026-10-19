/*
 * plat.h - was das Spiel von der Plattform braucht (Host-Simulator, UVMC2).
 * Das Spiel selbst (game/) ruft nur libvpy bzw. den Vertrag vectrexInterface.h und diese Funktionen.
 */
#ifndef PLAT_H
#define PLAT_H

#include <stdint.h>
#include <stddef.h>

/* Datei aus dem Datenordner lesen/schreiben (Host: Arbeitsordner, UVMC2: SD-Karte, Ordner pinball/).
 * Lesen liefert die Laenge oder 0; Schreiben 1 bei Erfolg. */
size_t plat_read_file(const char *name, uint8_t *buf, size_t max);
int    plat_write_file(const char *name, const uint8_t *data, size_t n);

void     plat_log(const char *msg);            /* Host: stderr; UVMC2: nichts */
uint32_t plat_micros(void);                    /* Mikrosekunden-Zaehler (laeuft ueber) */

/* Zaehler des Strahlstroms des letzten Bildes (UVMC2: uvm2_stats; Host: 0).
 * dropped > 0 heisst: Befehle gingen verloren, dann ist nichts auf dem Bildschirm Beweis. */
void plat_bus_stats(unsigned *bus_cycles, unsigned *dropped, unsigned *overrun);

/* Nachzentrieren des Strahls (Re-Zero): alle zero_every Striche (0 = aus) bzw. bei Spruengen ab zero_jump Einheiten */
void plat_beam_init(int zero_every, int zero_jump);

/* Strahlwerte der KONSOLE (Kit: config/uvm2.cfg, gilt fuer alle Spiele). Host: nur Zahlen ohne Wirkung.
 *   ZERO   Nullpunkt (falsch = Textzeilen kippen, Ketten wandern)
 *   TAIL   Nachlauf je Strich in 1/256 Takt (falsch = Vielecke aus vielen kurzen Strichen schliessen nicht)
 *   SCALE  Teiler der Strichlaenge (groesser = kuerzer) */
enum { PLAT_BEAM_ZERO, PLAT_BEAM_TAIL, PLAT_BEAM_SCALE, PLAT_BEAM_N };
int  plat_beam_get(int which);
void plat_beam_set(int which, int value);
void plat_beam_save(void);

/* Ton: 1 = die Plattform hat den Klangchip der Vectrex (UVMC2), 0 = nur PCM (Host) */
int  plat_audio_has_chip(void);
/* PCM-Ausgabe (stereo, 16 Bit, linker und rechter Kanal getrennt). Host: SDL-Rueckruf; UVMC2: Buchse. 1 = laeuft */
typedef void (*plat_render_t)(int16_t *left, int16_t *right, int n);
int  plat_audio_start(plat_render_t render, int rate);
void plat_audio_pump(plat_render_t render);                   /* UVMC2: Buchsenpuffer fuellen; Host: nichts */

/* Grosser Speicher fuer die Tonaufnahmen (mehrere MB): UVMC2 = das PSRAM, Host = gewoehnlicher Speicher.
 * plat_arena liefert die Leseadresse und die Groesse, oder NULL, wenn es keinen gibt.
 * Dateien werden scheibchenweise hineingeladen, zwei zugleich (h = 0 Aufnahmen, h = 1 das gestreamte Lied): stream_open oeffnet
 * (1 = gefunden, *size = Laenge), stream_read laedt hoechstens budget_us Mikrosekunden lang weitere Bytes an die Stelle offset
 * und liefert die Anzahl (0 = Ende/Fehler).
 * Stellen, die MEHRMALS beschrieben werden (Ringpuffer), nur ueber arena_get/arena_put ansprechen: die Leseadresse laeuft auf
 * dem UVMC2 ueber einen Zwischenspeicher, der nach dem Ueberschreiben noch den alten Inhalt liefern kann. */
const uint8_t *plat_arena(uint32_t *size);
int            plat_stream_open(int h, const char *name, uint32_t *size);
uint32_t       plat_stream_read(int h, uint32_t offset, uint32_t max, unsigned budget_us);
void           plat_arena_get(uint32_t offset, uint8_t *dst, uint32_t n);
void           plat_arena_put(uint32_t offset, const uint8_t *src, uint32_t n);
/* Erste Datei im Unterordner dir, deren Name mit prefix beginnt und auf ext endet (".wav", Gross/Klein egal):
 * schreibt "dir/name" nach out. 1 = gefunden */
int            plat_find_file(const char *dir, const char *prefix, const char *ext, char *out, size_t n);

/* Reset-Knopf der Konsole: liefert EINMAL, wie lange er gedrueckt war (Millisekunden), sonst 0. Host: immer 0. */
int  plat_reset_ms(void);
uint32_t plat_clock_us(void);           /* echte Uhr fuer Zeitmessungen (plat_micros kann im Test eine feste Bildzeit liefern) */
void plat_redraw_last(int on);          /* 1: fehlt ein neues Bild, zeichnet die Konsole das letzte noch einmal (nur im Vorspann) */

/* Testhilfen (nur Host, Umgebungsvariable VP_<NAME>; UVMC2 liefert immer den Vorgabewert) */
int  plat_test_int(const char *name, int def);

#endif
