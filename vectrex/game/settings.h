/*
 * settings.h - Einstellungen und Bildanpassung des Spiels (SD-Karte: pinball/settings.bin).
 * Die Strahlwerte der Konsole gehoeren nicht hierher, sondern in config/uvm2.cfg (plat_beam_*).
 * Bestenlisten: hiscore.h.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

#define NUM_TABLES_MAX 12
enum { SOUND_OFF, SOUND_CONSOLE, SOUND_JACK, SOUND_BOTH, SOUND_N };
enum { DETAIL_LOW, DETAIL_MED, DETAIL_HIGH, DETAIL_N };
enum { PHYSICS_ARCADE, PHYSICS_REALISTIC, PHYSICS_N };   /* ARCADE = wie das Original */
#define ZOOM_STEPS 5
enum { SPEED_SLOW, SPEED_MEDIUM, SPEED_FAST, SPEED_N };
enum { NUDGE_WEAK, NUDGE_NORMAL, NUDGE_STRONG, NUDGE_ANALOG, NUDGE_N };   /* ANALOG: Staerke nach Ausschlag des Sticks */
enum { TILT_LOW, TILT_MEDIUM, TILT_HIGH, TILT_N };
enum { PLUNGER_ANALOG, PLUNGER_DIGITAL, PLUNGER_N };   /* Stick-Ausschlag = Zug / Halten zieht langsam auf */
/* Was als Koerper gezeichnet wird: nichts / die Flipper / alles, womit der Ball spielt / dazu alle Waende (kostet Bildrate) */
enum { V3D_OFF, V3D_FLIPPERS, V3D_OBJECTS, V3D_ALL, V3D_N };

typedef struct {
    uint8_t  table;          /* zuletzt gewaehlter Tisch (0..) */
    uint8_t  sound;          /* SOUND_* */
    uint8_t  detail;         /* immer DETAIL_HIGH; die Option gibt es nicht mehr, der Platz bleibt fuer die Dateiform */
    uint8_t  balls_unlimited;
    uint8_t  info;           /* 1 = Messzeile unten (Bildrate, Striche, Buszyklen); nicht mehr im Menue, nur Simulator (VP_INFO=1) */
    uint8_t  zoom;           /* 0 = aus, 1..5 = Ansicht folgt dem Ball, Vergroesserung 1,2 bis 2,0 in Schritten von 0,2 */
    uint8_t  physics;        /* PHYSICS_* */
    uint8_t  music;          /* 1 = Musik (nur Buchse/Simulator) */
    uint8_t  music_vol;      /* 0..10: Lautstaerke der eigenen Lieder (snd/<n>_*.wav) */
    uint8_t  players;        /* 1..4, spielen abwechselnd */
    uint8_t  speed;          /* SPEED_*: Tempo des Balls */
    /* Anpassung des Bildes an die eigene Roehre */
    int8_t   cal_dx, cal_dy; /* Verschiebung des Tisches in Bildschirmeinheiten */
    int8_t   cal_size;       /* Groesse in Prozent Abweichung von 100 */
    int8_t   cal_bright;     /* Helligkeit des Tisches, additiv */
    int8_t   cal_ball;       /* Helligkeit des Balls: unter 0 dunkler, ueber 0 je 10 ein Durchgang mehr (Vorgabe 0) */
    int8_t   cal_hud_dy;     /* Hoehe der Punktezeile */
    uint8_t  zero_every;     /* Nachzentrieren nach so vielen Strichen - immer 0: jeder andere Wert liess das Bild auf der Roehre zucken */
    uint8_t  zero_jump;      /* Nachzentrieren bei Spruengen ab so vielen Einheiten */
    /* ab hier spaeter dazugekommen: neue Felder nur ANHAENGEN, dann bleibt eine aeltere settings.bin gueltig */
    uint8_t  nudge_invert;   /* 1 = Stick links stoesst den Ball nach rechts und umgekehrt */
    uint8_t  nudge_power;    /* NUDGE_* */
    uint8_t  tilt_sens;      /* TILT_* */
    uint8_t  view3d;         /* V3D_* */
    uint8_t  unused_occ;     /* frueher OCCLUSION ON/OFF - die Verdeckung ist jetzt immer an; der Platz bleibt fuer die Dateiform */
    uint8_t  unused_flip[NUM_TABLES_MAX];  /* frueher Schlagkraft und Neigung je Tisch (Pausenbild); jetzt fest abgestimmt. Die Plaetze
                                            * bleiben, damit die Datei ihre Form behaelt */
    uint8_t  unused_slope[NUM_TABLES_MAX];
    uint8_t  plunger;        /* PLUNGER_* */
    uint8_t  unused_rev;
} settings_t;

extern settings_t g_set;
void settings_defaults(void);
void settings_load(void);
void settings_save(void);
int  settings_min_zoom(void);            /* kleinste erlaubte Zoomstufe zur gewaehlten 3D-Stufe */

#endif
