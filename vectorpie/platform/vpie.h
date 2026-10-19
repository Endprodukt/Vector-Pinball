/*
 * vpie.h - was die VectorPie-Fassung zusaetzlich zu plat.h von ihrer Plattform braucht: Schleife, Steuerung, Striche.
 * Hier steht, was auf der Vectrex libvpy liefert (vpy_run, vpy_j1_*, vpy_draw_line_dev). Nur platform/vectorpie.c kennt
 * das SDK; das Spiel bleibt davon frei (Spielkern und SDK heissen beide vp_* - so kommen sie sich nicht in die Quere).
 */
#ifndef VPIE_H
#define VPIE_H

#include <stdint.h>
#include <stddef.h>

/* Schleife: setup einmal, loop einmal je Bild, bis der Automat das Spiel beendet. Liefert den Rueckgabewert fuer main. */
int  vpie_run(void (*setup)(void), void (*loop)(void), int argc, char **argv);

/* Steuerung, wie das Spiel sie auf der Vectrex kennt: ein Stick (-127..127, hoch = positiv) und vier Knoepfe.
 * Vorgabe der Belegung (Eingaenge mit ihrem advmame.rc-Namen):
 *   Knopf 1  p1_button1            linker Flipper
 *   Knopf 4  p1_button2            rechter Flipper
 *   Knopf 3  p1_button3, start1    Ball starten, OK
 *   Knopf 2  start2                (frei)
 *   Stick    p1_left / p1_right / p1_up   gegen den Tisch stossen,  p1_down   Stoessel ziehen
 * ui_cancel beendet das Spiel (zurueck ins Menue des Automaten); das erledigt die Schleife. */
int  vpie_stick_x(void);
int  vpie_stick_y(void);
int  vpie_button(int n);

/* Belegung aendern: frei, unabhaengig von advmame.rc (Tasten, Joystick-Knoepfe, -Achsen, Steuerkreuze, Maus; die
 * Schreibweise steht in vectorpie.c). game.ini neben dem Programm gibt die Vorgabe, save/settings.ini geht vor. Die Funktionen: */
enum {
    VPIE_F_LEFT_FLIP = 1, VPIE_F_SPARE, VPIE_F_RIGHT_FLIP,                        /* Knopf 1, 2, 4 des Spiels (3 = Abschuss gibt es nicht mehr) */
    VPIE_F_NUDGE_L, VPIE_F_NUDGE_R, VPIE_F_NUDGE_UP, VPIE_F_PLUNGER,              /* der Stick, digital */
    VPIE_F_NUDGE_AXIS, VPIE_F_YOKE_AXIS,                                          /* analog: Stoss seitlich; ziehen = Stoessel, druecken = Stoss hoch */
    VPIE_F_START, VPIE_F_COIN, VPIE_F_SERVICE,                                    /* zusaetzlich zu advmame.rc und Tab */
    VPIE_F_N = VPIE_F_SERVICE
};
const char *vpie_binding(int n);         /* Belegung der Funktion n, z. B. "key:Left Ctrl", "joy0:b3", "joy0:a1~" */
const char *vpie_binding_name(int n, char *buf, size_t size);   /* lesbar: "JOY1 AXIS 2 INV" */
void vpie_binding_clear(int n);          /* "none" */
void vpie_binding_standard(int n);       /* die Vorgabe (Entf) */
void vpie_binding_toggle(int n);         /* none, oder wieder die Vorgabe (links / rechts) */
#define VPIE_BIND_MAX 4                  /* Eingaenge je Funktion */
int  vpie_func_is_axis(int n);           /* 1 = analoge Funktion (eine ganze Achse) */
void vpie_capture_begin(int replace);    /* eine Belegung wird gewaehlt (replace: ersetzt, sonst kommt sie dazu) ... */
int  vpie_capture(int n);                /* ... je Bild: 0 = wartet, 1 = geaendert, -1 = gelassen (Enter, START 1) */
int  vpie_controls_idle(void);           /* 1 = nichts ist gedrueckt */
void vpie_play_mode(int on);             /* 1 = im Spiel: die belegten Eingaenge; sonst nur die festen der Menues */

/* Einstellungen des Automaten, die nur diese Fassung hat (save/settings.ini, zusammen mit der Belegung) */
enum { VPIE_OPT_FREE_PLAY, VPIE_OPT_ATTRACT_SOUND, VPIE_OPT_MASTER_VOL, VPIE_OPT_EFFECTS_VOL, VPIE_OPT_N };
int  vpie_opt(int which);                /* an/aus als 0/1; die beiden Lautstaerken 0..10 */
void vpie_opt_set(int which, int value);
void vpie_settings_save(void);

/* Muenzen und Start (coin1 / coin2, start1 / start2 aus advmame.rc; Credits des SDK) */
int  vpie_coins(void);                   /* in diesem Bild eingeworfen */
int  vpie_credits(void);
int  vpie_start(void);                   /* START in diesem Bild gedrueckt */
int  vpie_service(void);                 /* Service-Knopf (Tab, ui_configure) in diesem Bild gedrueckt */
int  vpie_can_start(int players);        /* reichen die Credits (oder ist FREE PLAY an)? */
int  vpie_take_credits(int players);
void vpie_lamps(int wait);               /* 1 = ein Spiel kann beginnen: START blinkt, wenn Credits da sind; 0 = Lampen aus */

/* Strich in Q4-Bildschirmkoordinaten (gfx.h), Farbe 0xRRGGBB. Nur innerhalb von loop. */
void vpie_line(int x0, int y0, int x1, int y1, uint32_t rgb);
int  vpie_vertical(void);                /* 1 = Monitor hochkant (3:4), sonst quer (4:3) */
int  vpie_on_board(void);                /* 1 = Vektorplatine (USB-DVG, BeamForge), 0 = Fenster */

/* Kosten des zuletzt gesendeten Bildes (Zaehler des SDK). Auf der Platine bestimmt vor allem die WEGLAENGE die Zeit je
 * Bild, nicht die Strichzahl: lit = gezeichnet, blank = unsichtbare Spruenge, beide in Platineneinheiten (0..4095 je Achse). */
void vpie_stats(unsigned *lines, unsigned *lit, unsigned *blank, unsigned *dropped);

/* Die besten Ergebnisse fuer das Menue des Automaten (save/highscores.txt, Format des SDK): n Zeilen, beste zuerst */
void vpie_export_scores(const char (*names)[4], const uint32_t *scores, int n);
int  vpie_file_exists(const char *name); /* im Speicherordner (save/) */

#endif
