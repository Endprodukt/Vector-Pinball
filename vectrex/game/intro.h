/* intro.h - Vorspann vor dem Menue (3D: Kugel, Flipperfinger, zerspringende Wand, "PRESENTED BY ENDPRODUKT") */
#ifndef INTRO_H
#define INTRO_H
void intro_start(void);
int  intro_frame(float dt, int skip);    /* zeichnet ein Bild; 1 = fertig (zu Ende oder uebersprungen) */
#endif
