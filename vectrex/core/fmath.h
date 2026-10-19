/* Sinus und Kosinus nur mit float. Die sinf/cosf, die das Kit auf dem RP2350 mitbringt, rechnen innerlich mit double -
 * und double gibt es auf dem Cortex-M33 nur in Software, jeder Aufruf kostet dort ueber tausend Takte. Hier: Quadrant
 * abtrennen, Polynome von Cephes auf [-pi/4, pi/4], Fehler um 1e-7 - fuer Striche in Vectrex-Einheiten mehr als genug.
 * Ueberall im Spiel statt sinf/cosf benutzen, auch auf dem Host, damit beide dasselbe rechnen. */
#ifndef FMATH_H
#define FMATH_H

static inline void fm_sincos(float x, float *s, float *c)
{
    const float j = x * 0.63661977f;                                 /* 2/pi */
    const int q = (int)(j >= 0 ? j + 0.5f : j - 0.5f);
    const float fq = (float)q;
    const float r = ((x - fq * 1.5703125f) - fq * 4.837512969970703e-4f) - fq * 7.549789948768648e-8f;   /* pi/2 in drei Teilen */
    const float z = r * r;
    const float sn = r + r * z * (-1.6666654611e-1f + z * (8.3321608736e-3f + z * -1.9515295891e-4f));
    const float cs = 1.0f - 0.5f * z + z * z * (4.166664568298827e-2f + z * (-1.388731625493765e-3f + z * 2.443315711809948e-5f));
    switch (q & 3) {
    case 0:  *s = sn;  *c = cs;  break;
    case 1:  *s = cs;  *c = -sn; break;
    case 2:  *s = -sn; *c = -cs; break;
    default: *s = -cs; *c = sn;  break;
    }
}

static inline float fm_sinf(float x) { float s, c; fm_sincos(x, &s, &c); return s; }
static inline float fm_cosf(float x) { float s, c; fm_sincos(x, &s, &c); return c; }

#endif
