/*
 * segdisp.h - Anzeigen als Segmentanzeige alter Flipperautomaten: 14 Segmente je Stelle (Ziffern und Grossbuchstaben),
 * leicht geneigt, mit Luecken zwischen den Segmenten. Fuer Punkte, Ballzahl, Meldungen, Credits.
 * Gezeichnet wird ueber gfx in der gerade gesetzten Farbe (gfx_color).
 */
#ifndef SEGDISP_H
#define SEGDISP_H

/* Breite eines Textes der Hoehe h (Q4) */
int  seg_width(const char *s, int h);
/* Text, linke untere Ecke (x, y) in Q4, Stellenhoehe h, Helligkeit b. ghost > 0: die unbeleuchteten Segmente jeder Stelle
 * glimmen mit dieser Helligkeit mit (wie bei einer echten Anzeige; kostet Strahlweg, darum nur fuer Zahlenfelder). */
void seg_draw(int x, int y, const char *s, int h, int b, int ghost);
void seg_center(int cx, int y, const char *s, int h, int b, int ghost);
void seg_right(int xr, int y, const char *s, int h, int b, int ghost);

#endif
