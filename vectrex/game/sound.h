/*
 * sound.h - Ton.
 *   Lautsprecher der Vectrex: nur Effekte, neu gesetzt fuer den Klangchip (keine Musik).
 *   Buchse des UVMC2 und Simulator: die Originalaufnahmen und die Musik des Spiels (pcm.h); fehlt das
 *   Buendel auf der Karte, dieselben Chip-Effekte als Nachbildung.
 */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>

void sound_init(void);
void sound_apply(int mode);                          /* SOUND_* aus settings.h */
void sound_events(uint32_t sfx, uint32_t score_hits);/* VSFX_* und Zaehler der Punkte-Toene aus dem Spielkern */
/* Stereobild der Buchse fuer die naechsten Ereignisse, je 0 = links, 128 = Mitte, 256 = rechts:
 * ball = wo der Ball gerade ist (Bumper, Ziele, Abschuss), flipper = welche Flipper schlagen */
void sound_stereo(int ball, int flipper);
/* Vorspann. ROLL: Rollgeraeusch ab jetzt, CRASH: Einschlag. Liegt snd/intro_roll.mp3 (oder .wav) bzw. snd/intro_crash.mp3
 * (oder .wav) auf der Karte, laeuft die Aufnahme ueber die Buchse; sonst kommt dort die Nachbildung vom Klangchip, die der
 * Lautsprecher der Konsole in jedem Fall spielt. FLIP: Flipperschlag, END: alles anhalten. */
enum { INTRO_ROLL, INTRO_FLIP, INTRO_CRASH, INTRO_END };
void sound_intro(int what);
void sound_game_reset(void);                         /* Spielstart/-ende: Musik des Originals anhalten, Zaehler auf Anfang */
/* Musik waehlen: n = 0 Menue, 1.. = Tisch, -1 = keine. Im Menue ohne eigenes Lied spielt der Klangchip eine kleine Schleife.
 * Eigenes Lied: Gesucht wird snd/<n>_*.wav, sonst snd/<n>_*.mp3; gibt es keins, bleibt es fuer einen Tisch
 * bei der Musik des Originals. Laeuft in Schleife. */
void sound_music(int n);
void sound_frame(int load_ok);                       /* einmal je Bild; load_ok = 0: gerade keine Aufnahmen von der Karte nachladen */
void sound_silence(void);
/* Zustand der Originalaufnahmen fuer die Anzeige: Zahl der geladenen Dateien, oder -1, wenn die Buchse nicht benutzt wird */
int  sound_status(int *loading, int *memory);
int  sound_files(void);

#endif
