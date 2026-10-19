/*
 * fx.h - Effekte, fuer die auf der Vectrex der Strahl nicht reichte: die Leuchtspur des Balls, Funken am Bumper und an
 * der Schleuder, und die aufsteigenden Punktzahlen des Originals. Nur Darstellung; der Spielkern weiss davon nichts.
 */
#ifndef FX_H
#define FX_H

#include "vp.h"

void fx_reset(void);
/* Einmal je Bild nach vp_advance, BEVOR f->sfx geloescht wird: merkt sich die Ballbahn, zuendet Funken (VSFX_KICK) und
 * macht aus jedem Zuwachs der Punkte eine aufsteigende Zahl am Ball. dt = Echtzeit des Bildes. */
void fx_update(const vp_field_t *f, float dt);
/* Zeichnen in der Spielansicht (nach render_game_view, vor render_clip_end): die Spur vor den Baellen, der Rest danach */
void fx_draw_trails(const vp_field_t *f);
void fx_draw_top(void);

#endif
