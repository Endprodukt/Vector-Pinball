#include "segdisp.h"
#include "gfx.h"
#include <string.h>

/* Die 14 Segmente einer Stelle:
 *      aaaaaaa
 *     f h i j b
 *     f  hij  b
 *      g1   g2
 *     e  kml  c
 *     e k l m c
 *      ddddddd      dazu Punkt und Doppelpunkt */
enum { A = 1, B = 2, C = 4, D = 8, E = 16, F = 32, G1 = 64, G2 = 128, H = 256, I = 512, J = 1024, K = 2048, L = 4096, M = 8192,
       DOT = 16384, COLON = 32768, G = G1 | G2 };

static unsigned mask_of(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    switch (c) {
    case '0': return A | B | C | D | E | F | J | K;
    case '1': return B | C | J;
    case '2': return A | B | G | E | D;
    case '3': return A | B | C | D | G2;
    case '4': return F | G | B | C;
    case '5': return A | F | G | C | D;
    case '6': return A | F | E | D | C | G;
    case '7': return A | B | C;
    case '8': return A | B | C | D | E | F | G;
    case '9': return A | B | C | D | F | G;
    case 'A': return A | B | C | E | F | G;
    case 'B': return A | B | C | D | I | L | G2;
    case 'C': return A | D | E | F;
    case 'D': return A | B | C | D | I | L;
    case 'E': return A | D | E | F | G1;
    case 'F': return A | E | F | G1;
    case 'G': return A | C | D | E | F | G2;
    case 'H': return B | C | E | F | G;
    case 'I': return A | D | I | L;
    case 'J': return B | C | D | E;
    case 'K': return E | F | G1 | J | M;
    case 'L': return D | E | F;
    case 'M': return B | C | E | F | H | J;
    case 'N': return B | C | E | F | H | M;
    case 'O': return A | B | C | D | E | F;
    case 'P': return A | B | E | F | G;
    case 'Q': return A | B | C | D | E | F | M;
    case 'R': return A | B | E | F | G | M;
    case 'S': return A | C | D | F | G;
    case 'T': return A | I | L;
    case 'U': return B | C | D | E | F;
    case 'V': return E | F | K | J;
    case 'W': return B | C | E | F | K | M;
    case 'X': return H | J | K | M;
    case 'Y': return H | J | L;
    case 'Z': return A | D | J | K;
    case '-': return G;
    case '+': return G | I | L;
    case '/': return J | K;
    case '*': return G | H | I | J | K | L | M;
    case '<': return J | M;
    case '>': return H | K;
    case '(': return J | M;
    case ')': return H | K;
    case '=': return G | D;
    case '_': return D;
    case 39:  return I;
    case '!': return I | DOT;
    case '.': return DOT;
    case ',': return DOT;
    case ':': return COLON;
    case '#': return A | B | C | D | E | F | G | H | I | J | K | L | M;
    default:  return 0;
    }
}

/* Endpunkte der Segmente in Halben: x 0..2 (links, Mitte, rechts), y 0..2 (unten, Mitte, oben) */
static const signed char SEG[14][4] = {
    { 0, 2, 2, 2 }, { 2, 2, 2, 1 }, { 2, 1, 2, 0 }, { 0, 0, 2, 0 }, { 0, 0, 0, 1 }, { 0, 1, 0, 2 }, { 0, 1, 1, 1 }, { 1, 1, 2, 1 },
    { 0, 2, 1, 1 }, { 1, 2, 1, 1 }, { 2, 2, 1, 1 }, { 0, 0, 1, 1 }, { 1, 0, 1, 1 }, { 2, 0, 1, 1 }
};

#define CELL_W(h)   ((h) * 11 / 20)     /* Breite einer Stelle */
#define ADVANCE(h)  ((h) * 17 / 20)     /* Abstand der Stellen */
#define SLANT(h)    ((h) * 3 / 20)      /* Neigung: so weit steht die Oberkante weiter rechts */

int seg_width(const char *s, int h)
{
    const int n = (int)strlen(s);
    return n ? (n - 1) * ADVANCE(h) + CELL_W(h) + SLANT(h) : 0;
}

static void cell(int x, int y, unsigned m, int h, int b)
{
    const int w = CELL_W(h), sl = SLANT(h);
    for (int k = 0; k < 14; k++) {
        if (!(m & (1u << k))) continue;
        const int x0 = x + SEG[k][0] * w / 2 + SEG[k][1] * sl / 2, y0 = y + SEG[k][1] * h / 2;
        const int x1 = x + SEG[k][2] * w / 2 + SEG[k][3] * sl / 2, y1 = y + SEG[k][3] * h / 2;
        const int gx = (x1 - x0) / 8, gy = (y1 - y0) / 8;            /* Luecke an beiden Enden: die Segmente beruehren sich nicht */
        gfx_line(x0 + gx, y0 + gy, x1 - gx, y1 - gy, b);
    }
    if (m & DOT) gfx_line(x + w / 2 - h / 24, y, x + w / 2 + h / 24, y + h / 16, b);
    if (m & COLON) {
        gfx_line(x + w / 2 + sl / 4 - h / 24, y + h / 4, x + w / 2 + sl / 4 + h / 24, y + h / 4 + h / 16, b);
        gfx_line(x + w / 2 + sl * 3 / 4 - h / 24, y + h * 3 / 4, x + w / 2 + sl * 3 / 4 + h / 24, y + h * 3 / 4 + h / 16, b);
    }
}

void seg_draw(int x, int y, const char *s, int h, int b, int ghost)
{
    for (; *s; s++, x += ADVANCE(h)) {
        const unsigned m = mask_of(*s);
        if (ghost > 0) cell(x, y, (A | B | C | D | E | F | G) & ~m, h, ghost);
        if (m) cell(x, y, m, h, b);
    }
}

void seg_center(int cx, int y, const char *s, int h, int b, int ghost) { seg_draw(cx - seg_width(s, h) / 2, y, s, h, b, ghost); }
void seg_right(int xr, int y, const char *s, int h, int b, int ghost) { seg_draw(xr - seg_width(s, h), y, s, h, b, ghost); }
