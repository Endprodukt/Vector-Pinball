/*
 * app.h - was die Teile des Ablaufs voneinander brauchen (main.c, attract.c, play.c, entry.c, service.c, art.c).
 * Der Zustand des Automaten ist eine Zustandsmaschine (s_state): jeder Zustand hat einen Oeffner (xxx_open) und ein Bild
 * (xxx_frame), das main.c je Bild aufruft. Gemeinsam sind Spielfeld, Zeit, Steuerung und ein paar Zeichenhilfen.
 */
#ifndef APP_H
#define APP_H

#include <stdint.h>
#include "vp.h"
#include "gfx.h"
#include "settings.h"

#define PORT_YEAR "2026"

/* Farben der Menues und Einblendungen (0xRRGGBB). Im Spiel kommen die Farben aus den Tischdaten (render.c). */
#define C_WHITE   0xFFFFFFu
#define C_FRAME   0x6666FFu              /* Wandblau des Originals */
#define C_FLIPPER 0x00FF00u
#define C_BALL    0xDBDBFFu
#define C_RING    0xB8C0D0u
#define C_BUMPER  0x5060FFu
#define C_LIT     0xFF6020u
#define C_TITLE   0xFFE040u
#define C_SHADOW  0xFF4020u
#define C_ACCENT  0x60D0FFu
#define C_VALUE   0xFFC040u
#define C_DIM     0x8090C0u
#define C_ALERT   0xFF3030u
#define C_PLASMA  0xFF7A20u              /* Segmentanzeigen: orange wie eine Gasentladungsanzeige */
#define C_VFD     0x40FFD0u              /* ... und tuerkis wie eine Fluoreszenzanzeige */

enum { S_ATTRACT, S_PLAYERS, S_TABLE, S_PLAY, S_OVER, S_ENTRY, S_MENU, S_ABOUT };
extern int s_state;
extern int s_service_from;               /* Service mit F2 aus dem Spiel geoeffnet: dorthin zurueck (0 = Attract) */
extern vp_field_t F;                     /* das eine Spielfeld */
extern uint32_t s_now_ms;                /* Spielzeit in ms */
extern float s_dt;                       /* Echtzeit des letzten Bildes in s */
extern int s_players;                    /* gewaehlte Spielerzahl */
extern int s_turn_shown;                 /* Spieler, fuer den der grosse Hinweis schon kam (-1 = noch keiner) */
extern float s_turn_t;
extern const float NUDGE_SCALE[NUDGE_N];
extern uint32_t s_logic_us, s_draw_us;   /* Zeiten des letzten Bildes, fuer VP_PROF und VP_INFO */

/* Steuerung, je Bild gelesen (main.c): Stick als vier Richtungen und die vier Knoepfe; edge = in diesem Bild gedrueckt */
typedef struct { int left, right, up, down, b1, b2, b3, b4; } pad_t;
extern pad_t pad, edge;
int   repeat(int idx, int held, int fast);   /* Tastenwiederholung fuer die Kanaele 0..5 */

float game_speed(void);
void  load_table(int t);
void  start_game(void);
float frnd(void);                        /* Zufall fuer die Kulisse */
void  blip(uint32_t sfx);                /* Ton ausserhalb eines Spiels */

/* ---- Zustaende: Oeffner und Bild ---- */
void attract_open(void);  void attract_frame(void);
enum { AT_TITLE, AT_DEMO, AT_SCORES };
extern int at_page, at_table;
void attract_page(int page);
void players_open(void);  void players_frame(void);
void table_open(void);    void table_frame(void);
void play_frame(void);    void over_frame(void);
void autoplay(int *l, int *r);
void draw_game(int dim, int balls, float dt);
void entry_next(int from); void entry_frame(void);
uint32_t final_score(int p);             /* Endstand des Spielers p */
enum { PAGE_OPTIONS, PAGE_CONTROLS };
void menu_open(int page, int item);  void menu_frame(void);
void service_close(void);
void about_open(void);    void about_frame(void);
extern int ab_page;
extern float m_ball_y;                   /* Zeiger des Service-Menues; 1e9 = neu ansetzen */

/* ---- Kulisse (art.c): Flipper-Rahmen, Lauflicht, Muenzzeile ---- */
extern float m_flip[2], m_time;
void wedge(int px, int py, int len, float deg, int b);
void roll_ball(int cx, int cy, int r, float roll);
void menu_frame_art(int bumpers);
void menu_flip(int side);
void menu_back(void);
void chaser(void);
void coin_line(int y, int h);
void time_bar(int y, float left);
void menu_side(void);

#endif
