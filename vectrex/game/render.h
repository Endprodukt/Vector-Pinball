/*
 * render.h - Tisch, Baelle und Anzeige als Vektorbild.
 *
 * Farbe gibt es auf der Vectrex nicht: jede Farbe des Originals wird zu einer Helligkeitsstufe
 * (render_intensity). Was im Original die Farbe wechselt (aufleuchtende Ziele, Ebenenwechsel,
 * "Temperatur"-Anzeigen), wechselt hier die Helligkeit.
 */
#ifndef RENDER_H
#define RENDER_H

#include "vp.h"

/* Ansicht festlegen: Tischmitte auf (cx, cy) Bildschirmeinheiten, units = Einheiten je Feldeinheit.
 * render_default_view() nimmt die Kalibrierung aus den Einstellungen. */
void render_view(const vp_table_def_t *t, float cx, float cy, float units);
void render_default_view(const vp_table_def_t *t);
/* Spielansicht: wie render_default_view, dazu der Zoom des Originals - bei genau einem Ball faehrt die Ansicht auf das
 * zoom-fache (Original: 1,5) und folgt dem Ball; alles ausserhalb des Tischfensters wird abgeschnitten. shake = kurzer Versatz beim Stoss
 * gegen den Tisch (Bildschirmeinheiten). Einmal je Bild vor dem Zeichnen aufrufen. */
void render_game_view(const vp_field_t *f, float dt, float zoom, float shake_x, float shake_y);   /* zoom: 1.0 = aus .. 1.5 */
void render_zoom_reset(const vp_field_t *f);
void render_clip_end(void);              /* nach Tisch und Ball: Ausschnitt aufheben (Punktezeile, Meldungen) */
int  render_x(float fx);                 /* Feld -> Q4 */
int  render_y(float fy);
float render_units(void);

int  render_intensity(uint32_t argb);    /* 0 = unsichtbar, sonst 14..127 */

/* dim: 0..100 Prozent Abdunklung (Menue-Hintergrund) */
void render_table(const vp_field_t *f, int dim);
void render_balls(const vp_field_t *f);
void render_plunger(float pull);                           /* 0..1: wie weit der Stoessel gezogen ist */
void render_hud(const vp_field_t *f);    /* Punktezeile bzw. Meldung ueber dem Tisch */

#endif
