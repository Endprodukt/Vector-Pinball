/*
 * gfx.h - Zeichenpuffer der VectorPie-Fassung (Gegenstueck zu vectrex/game/gfx.h, dieselben Aufrufe plus Farbe).
 * Koordinaten in 1/16 Bildschirmeinheit (Q4), dieselben Einheiten wie auf der Vectrex: senkrecht -189..189; waagerecht
 * -252..252 auf einem Monitor im Querformat (4:3), -141..141 hochkant (gfx_half_w).
 *
 * Alle Striche landen zuerst in einem Pfadpuffer: ein Strich, der dort beginnt, wo der letzte endete
 * (gleiche Helligkeit), verlaengert die Kette - Ketten kosten keinen Sprung. Liegt er ausserdem genau
 * in der Flucht des letzten, wird der letzte nur verlaengert (ein Strich statt zwei).
 * Die Reihenfolge der Aufrufe ist die Reihenfolge auf der Roehre und aendert sich nie von selbst.
 */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#define Q4(u) ((u) * 16)

typedef struct { int16_t x, y; } pt_t;
typedef struct { int paths, strokes, jumps, lost, occluders, cut_lines, span_tests, poly_tests; } gfx_stats_t;
extern gfx_stats_t gfx_stats;            /* Werte des zuletzt ausgegebenen Bildes */

void gfx_begin(void);
/* Farbton (0xRRGGBB) der folgenden Striche; die Helligkeit b (0..127) dunkelt ihn ab. gfx_begin setzt Weiss. */
void gfx_color(uint32_t rgb);
/* Alles, was danach gezeichnet, als Verdecker angemeldet oder als Ausschnitt gesetzt wird, steht um dx (Q4) weiter rechts.
 * Fuer Bilder, die fuer die Bildmitte gebaut sind und im Querformat zur Seite ruecken (Menue). gfx_begin setzt 0. */
void gfx_shift(int dx);
float gfx_half_w(void);                                              /* halbe Bildbreite in Bildschirmeinheiten */
/* Ausschnitt: Striche danach werden an diesem Rechteck (Q4) abgeschnitten; gfx_clip_off() hebt ihn auf */
void gfx_clip(int x0, int y0, int x1, int y1);
void gfx_clip_off(void);
void gfx_line(int x0, int y0, int x1, int y1, int b);
/* Verdeckung (nach dem Vorbild des Occluders im Starter Kit, hier in Bildschirmkoordinaten): ein Verdecker ist ein konvexes
 * Vieleck; jeder Strich, der DANACH gezeichnet wird (und solange gfx_occ_cutting an ist), verliert den Teil, der darin liegt. Es gibt keine Tiefenpruefung -
 * die Reihenfolge traegt sie: erst das Nahe zeichnen, dann als Verdecker anmelden, dann das Fernere zeichnen.
 * Ein Koerper, der nur seine eigene Rueckseite verdecken soll: mark = gfx_occ_mark(), Deckflaeche anmelden, Rueckseite
 * zeichnen, gfx_occ_cut(mark). gfx_begin leert die Liste. Voll (1024 Verdecker, 8192 Ecken) heisst: es verdeckt nichts mehr. */
int  gfx_occ_add(const pt_t *p, int n);                              /* konvex, 3..24 Ecken, Umlaufsinn egal; -1 = nicht angenommen */
int  gfx_occ_poly(const pt_t *p, int n);                             /* beliebige Form ohne Loch (eingebuchtet erlaubt), 3..24 Ecken */
int  gfx_occ_circle(int cx, int cy, int r, int segs, int turn);     /* dasselbe Vieleck, das gfx_circle zeichnet */
void gfx_occ_enable(int on);                                         /* aus: nichts wird angenommen, alles bleibt durchsichtig */
void gfx_occ_cutting(int on);                                        /* aus: Striche bleiben ganz, die Verdecker bleiben gemerkt (gfx_begin: an) */
int  gfx_occ_mark(void);
void gfx_occ_cut(int mark);
void gfx_poly(const pt_t *p, int n, int b, int closed);
void gfx_dot(int x, int y, int b);                                   /* sehr kurzer Strich: leuchtet als Punkt */
void gfx_circle(int cx, int cy, int r, int b, int segs, int turn);   /* turn: Startwinkel in 1/256 Kreis */
void gfx_flush(void);                                                /* einmal je Bild: Ketten ausgeben (vpie_line) */

#endif
