/* nicht zeitkritisch: auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp (das Kit baut sonst mit -O3) */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include "font.h"
#include "gfx.h"
#include <string.h>

/* Je Zeichen: Punktfolgen "xy" (x 0..4, y 0..6), '|' hebt den Stift */
static const char *glyph(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    switch (c) {
    case '0': return "0040460600";
    case '1': return "2026";
    case '2': return "064643030040";
    case '3': return "06464000|0343";
    case '4': return "060343|4640";
    case '5': return "460603434000";
    case '6': return "460600404303";
    case '7': return "064620";
    case '8': return "0040460600|0343";
    case '9': return "430306464000";
    case 'A': return "0004264440|0242";
    case 'B': return "000645034200";
    case 'C': return "46060040";
    case 'D': return "000626442000";
    case 'E': return "46060040|0333";
    case 'F': return "000646|0333";
    case 'G': return "460600404222";
    case 'H': return "0006|0343|4640";
    case 'I': return "1030|2026|1636";
    case 'J': return "02004046";
    case 'K': return "0006|460340";
    case 'L': return "060040";
    case 'M': return "0006234640";
    case 'N': return "00064046";
    case 'O': return "0040460600";
    case 'P': return "0006464303";
    case 'Q': return "0040460600|2240";
    case 'R': return "000646430340";
    case 'S': return "460603434000";
    case 'T': return "0646|2620";
    case 'U': return "06004046";
    case 'V': return "062046";
    case 'W': return "0600234046";
    case 'X': return "0046|0640";
    case 'Y': return "062346|2320";
    case 'Z': return "06460040";
    case '!': return "2622|2120";
    case '+': return "0343|2125";
    case '-': return "0343";
    case '%': return "0046|0516|3041";
    case ':': return "2425|2122";
    case '.': return "2021";
    case ',': return "2110";
    case '/': return "0046";
    case '<': return "460340";
    case '>': return "064300";
    case 39:  return "2624";              /* Apostroph */
    case '(': return "36141230";
    case ')': return "16343210";
    case '#': return "1016|3036|0242|0444";
    default:  return "";
    }
}

int text_width(const char *s, int unit)
{
    int n = (int)strlen(s);
    return n ? (n * 6 - 2) * unit : 0;
}

int text_strokes(const char *s)
{
    int total = 0;
    for (; *s; s++) {
        const char *g = glyph(*s);
        int pts = 0;
        while (*g) {
            if (*g == '|') { if (pts > 1) total += pts - 1; pts = 0; g++; }
            else { pts++; g += 2; }
        }
        if (pts > 1) total += pts - 1;
    }
    return total;
}

void text_draw(int x, int y, const char *s, int unit, int b) { text_draw_slant(x, y, s, unit, b, 0); }

void text_draw_slant(int x, int y, const char *s, int unit, int b, int slant)
{
    for (; *s; s++, x += 6 * unit) {
        const char *g = glyph(*s);
        int have = 0, px = 0, py = 0;
        while (*g) {
            if (*g == '|') { have = 0; g++; continue; }
            int gx = x + (g[0] - '0') * unit + (g[1] - '0') * unit * slant / 256, gy = y + (g[1] - '0') * unit;
            if (have) gfx_line(px, py, gx, gy, b);
            px = gx; py = gy; have = 1;
            g += 2;
        }
    }
}

void text_center(int cx, int y, const char *s, int unit, int b) { text_draw(cx - text_width(s, unit) / 2, y, s, unit, b); }
