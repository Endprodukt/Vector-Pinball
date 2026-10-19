/* vp_stars.h - Sternkatalog und Sternbilder fuer Tisch 7 (Daten: core/gen/star_data.c) */
#ifndef VP_STARS_H
#define VP_STARS_H
#include <stdint.h>

/* Einheitsvektor (x 32767) und Helligkeit (x 256, kleiner = heller) - in 16 Bit, das spart 12 KB RAM auf dem UVMC2 */
typedef struct { int16_t x, y, z, mag; } vp_star_t;
typedef struct {
    const char *name;
    float ra, dec, radius;                                        /* Mitte und Winkelradius (Bogenmass) */
    const uint16_t *idx; int nidx;                                /* Sterne des Bildes */
    const uint16_t *seg; int nseg;                                /* Linien als Sternpaare */
} vp_const_t;

extern const vp_star_t vp_stars[];
extern const int vp_nstars;
extern const vp_const_t vp_consts[];
extern const int vp_nconst;
#endif
