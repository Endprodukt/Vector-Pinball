/*
 * pcm.h - die Originalaufnahmen des Spiels (Effekte und Musik) fuer die Buchse des UVMC2 und den Simulator.
 *
 * Die Aufnahmen sind gewoehnliche WAV-Dateien auf der SD-Karte im Ordner pinball/snd/ (16 Bit, mono oder stereo,
 * beliebige Abtastrate; die Buchse ist stereo), erzeugt von tools/audio2pcm.py aus den .ogg-Dateien des Originals - man kann sie am PC
 * anhoeren und durch eigene ersetzen. Auf dem UVMC2 werden sie nacheinander in das PSRAM geladen, waehrend das Spiel
 * schon laeuft; jede Aufnahme ist spielbar, sobald sie ganz da ist. Fehlen sie, bleibt es bei den nachgebildeten
 * Chip-Effekten.
 */
#ifndef PCM_H
#define PCM_H

#include <stdint.h>

/* Reihenfolge = Ladereihenfolge: kurze Effekte zuerst, Musik zuletzt. Dateinamen: pcm.c */
enum { SND_FLIPPER, SND_LAUNCH, SND_MESSAGE, SND_START, SND_ROLLOVER,
       SND_DING0, SND_DING1, SND_DING2, SND_DING3, SND_DING4, SND_DING5, SND_PAD, SND_DRUM, SND_N };

#define PCM_RATE 32000

void pcm_init(void);
void pcm_load_step(unsigned budget_us);           /* einmal je Bild: ein Stueck nachladen */
int  pcm_have(void);                              /* 1 = es gibt Aufnahmen (geladen oder noch im Laden) */
int  pcm_ready(int idx);                          /* 1 = diese Aufnahme ist geladen */
/* Zustand fuer die Anzeige: Zahl der geladenen Aufnahmen; *loading = 1 solange noch geladen wird;
 * *memory = 0, wenn es keinen Speicher fuer Aufnahmen gibt (PSRAM fehlt) */
int  pcm_status(int *loading, int *memory);
/* Effekt abspielen: vol 0..256, pitch in 1/65536 (65536 = Originaltonhoehe), pan 0 = ganz links, 128 = Mitte, 256 = ganz rechts.
 * In der Mitte sind beide Kanaele voll (wie bisher mono), zur Seite hin wird nur der andere Kanal leiser. */
void pcm_play(int idx, int vol, uint32_t pitch, int pan);
/* Musikspur des Originals (eigene Stimme je Aufnahme): starten, falls sie nicht laeuft; loop = endlos */
void pcm_music_start(int idx, int vol, int loop);
int  pcm_music_playing(int idx);
void pcm_music_stop(void);
void pcm_stop_all(void);
/* Lied von der Karte streamen (eigene Musik: beliebig lange WAV- oder MP3-Datei, Pfad relativ zum Datenordner).
 * MP3 wird beim Abspielen dekodiert (game/mp3.c); auf dem UVMC2 rechnet das Kern 0, waehrend Kern 1 das Bild zeichnet. */
int  pcm_song_start(const char *path, int vol, int loop);
void pcm_song_stop(void);
void pcm_song_volume(int vol);
int  pcm_song_playing(void);
int  pcm_song_step(unsigned budget_us);           /* einmal je Bild: Ringpuffer nachfuellen; 1 = hat von der Karte gelesen */
void pcm_mix(int16_t *left, int16_t *right, int n);   /* addiert auf den vorhandenen Inhalt; Stereo-Dateien bleiben stereo */

#endif
