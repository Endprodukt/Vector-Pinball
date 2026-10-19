/*
 * font.h - sparsame Strichschrift (Grossbuchstaben, Ziffern, wenige Zeichen) auf einem Raster 4 x 6.
 * Jedes Zeichen besteht aus moeglichst wenigen, moeglichst zusammenhaengenden Strichen, denn auf der
 * Roehre kostet jeder Strich Bildrate. unit = Groesse eines Rasterschritts in Q4.
 */
#ifndef FONT_H
#define FONT_H
int  text_width(const char *s, int unit);
void text_draw(int x, int y, const char *s, int unit, int b);        /* x, y = links unten (Q4) */
void text_center(int cx, int y, const char *s, int unit, int b);
void text_draw_slant(int x, int y, const char *s, int unit, int b, int slant);   /* slant: Neigung nach rechts in 1/256 */
int  text_strokes(const char *s);                                     /* Strichzahl, zum Abschaetzen */
#endif
