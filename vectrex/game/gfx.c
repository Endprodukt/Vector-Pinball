/* auf dem UVMC2 auf Groesse uebersetzen: die Zeit je Bild bestimmt der Strahl, und der Speicher ist knapp */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include "gfx.h"
#include "fmath.h"
#include <vpy.h>
#include <math.h>

#define MAXPT   1400
#define MAXPATH 560
static pt_t pts[MAXPT];
static struct { uint16_t start, n; uint8_t b; } paths[MAXPATH];
static int npt, npath;
static gfx_stats_t cur;
gfx_stats_t gfx_stats;

static int32_t dev(int q4) { return ((int32_t)q4 * 127) >> 4; }

static int clip_on, cx0, cy0, cx1, cy1;

/* ---- Verdecker ---- */
#define OCC_MAX  128
#define OCC_PTS  560
#define OCC_EDGE 6.0f                        /* Q4: so weit vom Rand entfernt beginnt "innen" - ein Strich AUF der Kante bleibt ganz */
static pt_t     occ_p[OCC_PTS];
static uint16_t occ_at[OCC_MAX + 1];
static int8_t   occ_sign[OCC_MAX];
static int16_t  occ_bb[OCC_MAX][4];          /* umschliessendes Rechteck: die meisten Striche scheitern schon daran */
static int      nocc, occ_on = 1, occ_cutting = 1;

/* Streifenindex: welche Verdecker liegen in welchem waagerechten Streifen des Bildes. Ein geschnittener Strich prueft nur
 * die Verdecker seiner Streifen statt alle (Tisch 8: 120 Verdecker, 300 geschnittene Striche je Bild - der Vergleich
 * jeder mit jedem war der groesste Posten der Zeichenzeit). Aufgebaut beim ersten Schnitt nach einer Aenderung. */
#define OCC_BANDS 16
#define OCC_IDX   768
static int      occ_dirty = 1, occ_full, occ_ymin, occ_yspan;
static uint16_t occ_band_at[OCC_BANDS + 1];
static uint8_t  occ_band_k[OCC_IDX];
static uint8_t  occ_fb[OCC_MAX];             /* erster Streifen des Verdeckers: dort wird er geprueft, in den weiteren nicht noch einmal */

static int occ_band(int y)
{
    const int b = (int)(((long)(y - occ_ymin) * OCC_BANDS) / occ_yspan);
    return b < 0 ? 0 : (b >= OCC_BANDS ? OCC_BANDS - 1 : b);
}

static void occ_index(void)
{
    uint16_t cnt[OCC_BANDS + 1];
    occ_dirty = 0; occ_full = 0;
    if (!nocc) return;
    int y0 = occ_bb[0][1], y1 = occ_bb[0][3];
    for (int k = 1; k < nocc; k++) {
        if (occ_bb[k][1] < y0) y0 = occ_bb[k][1];
        if (occ_bb[k][3] > y1) y1 = occ_bb[k][3];
    }
    occ_ymin = y0; occ_yspan = y1 - y0 + 1;
    for (int b = 0; b <= OCC_BANDS; b++) cnt[b] = 0;
    for (int k = 0; k < nocc; k++) {
        const int b0 = occ_band(occ_bb[k][1]), b1 = occ_band(occ_bb[k][3]);
        occ_fb[k] = (uint8_t)b0;
        for (int b = b0; b <= b1; b++) cnt[b]++;
    }
    int at = 0;
    for (int b = 0; b < OCC_BANDS; b++) { occ_band_at[b] = (uint16_t)at; at += cnt[b]; cnt[b] = occ_band_at[b]; }
    occ_band_at[OCC_BANDS] = (uint16_t)at;
    if (at > OCC_IDX) { occ_full = 1; return; }                     /* passt nicht: dann wieder alle pruefen */
    for (int k = 0; k < nocc; k++)
        for (int b = occ_fb[k], b1 = occ_band(occ_bb[k][3]); b <= b1; b++) occ_band_k[cnt[b]++] = (uint8_t)k;
}

void gfx_occ_enable(int on) { occ_on = on; }
void gfx_occ_cutting(int on) { occ_cutting = on; }

void gfx_begin(void) { npt = npath = 0; cur.paths = cur.strokes = cur.jumps = cur.lost = cur.occluders = cur.cut_lines = cur.span_tests = cur.poly_tests = 0; clip_on = 0; nocc = 0; occ_at[0] = 0; occ_cutting = 1; occ_dirty = 1; }

int  gfx_occ_mark(void) { return nocc; }
void gfx_occ_cut(int mark) { if (mark >= 0 && mark < nocc) { nocc = mark; occ_dirty = 1; } }

static int occ_add(const pt_t *p, int n, int concave)
{
    if (!occ_on || n < 3 || n > 24 || nocc >= OCC_MAX || occ_at[nocc] + n > OCC_PTS) return -1;
    long area = 0;
    int x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y;
    for (int i = 0; i < n; i++) {
        const pt_t *a = &p[i], *b = &p[(i + 1) % n];
        area += (long)a->x * b->y - (long)b->x * a->y;
        if (a->x < x0) x0 = a->x;
        if (a->x > x1) x1 = a->x;
        if (a->y < y0) y0 = a->y;
        if (a->y > y1) y1 = a->y;
    }
    if (area > -64 && area < 64) return -1;                          /* von der Kante gesehen: verdeckt nichts */
    if (clip_on && (x1 < cx0 || x0 > cx1 || y1 < cy0 || y0 > cy1)) return -1;   /* ausserhalb des Bildes verdeckt nichts */
    pt_t *o = &occ_p[occ_at[nocc]];
    for (int i = 0; i < n; i++) o[i] = p[i];
    occ_sign[nocc] = concave ? 0 : (area > 0 ? 1 : -1);          /* 0 = beliebige Form, gerade-ungerade-Regel */
    occ_bb[nocc][0] = (int16_t)x0; occ_bb[nocc][1] = (int16_t)y0; occ_bb[nocc][2] = (int16_t)x1; occ_bb[nocc][3] = (int16_t)y1;
    occ_at[nocc + 1] = (uint16_t)(occ_at[nocc] + n);
    if (nocc + 1 > cur.occluders) cur.occluders = nocc + 1;
    occ_dirty = 1;
    return nocc++;
}

int gfx_occ_add(const pt_t *p, int n) { return occ_add(p, n, 0); }
int gfx_occ_poly(const pt_t *p, int n) { return occ_add(p, n, 1); }

/* Ecken eines Kreises: Startwinkel und Schrittwinkel einmal, dann je Ecke eine Drehung (zwei Winkelfunktionen je Kreis statt
 * zwei je Ecke; der Fehler nach 24 Schritten liegt weit unter einer Q4-Einheit) */
static void circle_pts(int cx, int cy, int r, int segs, int turn, pt_t *p)
{
    float s, c, ds, dc;
    fm_sincos(turn * (6.2831853f / 256.0f), &s, &c);
    fm_sincos(6.2831853f / segs, &ds, &dc);
    for (int i = 0; i < segs; i++) {
        p[i].x = (int16_t)(cx + (int)lroundf(r * c)); p[i].y = (int16_t)(cy + (int)lroundf(r * s));
        const float nc = c * dc - s * ds;
        s = s * dc + c * ds; c = nc;
    }
}

int gfx_occ_circle(int cx, int cy, int r, int segs, int turn)
{
    pt_t p[16];
    if (segs > 16) segs = 16;
    circle_pts(cx, cy, r, segs, turn, p);
    return gfx_occ_add(p, segs);
}

/* Beliebige (auch eingebuchtete) Form: Schnittpunkte mit allen Kanten sortieren, jedes Teilstueck an seiner Mitte pruefen
 * (innen nach der gerade-ungerade-Regel und weiter als OCC_EDGE von jeder Kante). Liefert die verdeckten Abschnitte. */
static int occ_spans_poly(int k, float ax, float ay, float dx, float dy, int lx0, int ly0, int lx1, int ly1, float (*out)[2])
{
    const pt_t *p = &occ_p[occ_at[k]];
    const int n = occ_at[k + 1] - occ_at[k];
    float t[26];
    int m = 0, nout = 0;
    t[m++] = 0;
    for (int i = 0, j = 1; i < n; i++, j = (j + 1 == n) ? 0 : j + 1) {
        const pt_t *a = &p[i], *e = &p[j];
        /* Kante ganz neben dem Rechteck des Strichs: kein Schnitt, ohne Gleitkomma (die meisten Kanten eines Deckels) */
        if ((a->x < lx0 && e->x < lx0) || (a->x > lx1 && e->x > lx1) || (a->y < ly0 && e->y < ly0) || (a->y > ly1 && e->y > ly1)) continue;
        const float x1 = a->x, y1 = a->y, ex = e->x - x1, ey = e->y - y1;
        const float den = dx * ey - dy * ex;
        if (den > -1e-6f && den < 1e-6f) continue;
        const float u = ((x1 - ax) * ey - (y1 - ay) * ex) / den, v = ((x1 - ax) * dy - (y1 - ay) * dx) / den;
        if (u > 0 && u < 1 && v >= 0 && v <= 1 && m < 25) t[m++] = u;
    }
    t[m++] = 1;
    for (int i = 1; i < m; i++) for (int j = i; j > 0 && t[j] < t[j - 1]; j--) { float x = t[j]; t[j] = t[j - 1]; t[j - 1] = x; }
    for (int i = 0; i + 1 < m; i++) {
        if (t[i + 1] - t[i] < 1e-4f) continue;
        const float mx = ax + dx * (t[i] + t[i + 1]) * 0.5f, my = ay + dy * (t[i] + t[i + 1]) * 0.5f;
        int in = 0;
        for (int a = 0, b = n - 1; a < n; b = a++) {                 /* erst nur: innen? (billig) */
            const float xa = p[a].x, ya = p[a].y, xb = p[b].x, yb = p[b].y;
            if ((ya > my) != (yb > my) && mx < (xb - xa) * (my - ya) / (yb - ya) + xa) in ^= 1;
        }
        if (!in) continue;
        for (int a = 0, b = n - 1; a < n; b = a++) {                 /* dann: weit genug von jeder Kante? */
            const float xa = p[a].x, ya = p[a].y, ex = p[b].x - xa, ey = p[b].y - ya, l2 = ex * ex + ey * ey;
            float q = l2 > 0 ? ((mx - xa) * ex + (my - ya) * ey) / l2 : 0;
            q = q < 0 ? 0 : (q > 1 ? 1 : q);
            const float ddx = mx - xa - q * ex, ddy = my - ya - q * ey;
            if (ddx * ddx + ddy * ddy <= OCC_EDGE * OCC_EDGE) { in = 0; break; }
        }
        if (in && nout < 6) { out[nout][0] = t[i]; out[nout][1] = t[i + 1]; nout++; }
    }
    return nout;
}

/* Abschnitt [t0, t1] des Strichs A + t*D, der im Verdecker k liegt. 0 = keiner */
static int occ_span(int k, float ax, float ay, float dx, float dy, float *t0, float *t1)
{
    const pt_t *p = &occ_p[occ_at[k]];
    const int n = occ_at[k + 1] - occ_at[k];
    const float s = occ_sign[k];
    float lo = 0, hi = 1;
    for (int i = 0; i < n; i++) {
        const float ex = (float)(p[(i + 1) % n].x - p[i].x), ey = (float)(p[(i + 1) % n].y - p[i].y);
        const float ax_ = ex < 0 ? -ex : ex, ay_ = ey < 0 ? -ey : ey;
        const float len = ax_ > ay_ ? ax_ + 0.4f * ay_ : ay_ + 0.4f * ax_;         /* Kantenlaenge, grob */
        /* innen: Abstand von der Kante mindestens OCC_EDGE, also c0 + t*c1 >= 0 */
        const float c0 = s * (ex * (ay - p[i].y) - ey * (ax - p[i].x)) - OCC_EDGE * len, c1 = s * (ex * dy - ey * dx);
        if (c1 > -1e-6f && c1 < 1e-6f) { if (c0 < 0) return 0; continue; }
        const float t = -c0 / c1;
        if (c1 > 0) { if (t > lo) lo = t; } else if (t < hi) hi = t;
        if (lo >= hi) return 0;
    }
    *t0 = lo; *t1 = hi;
    return 1;
}

static void emit_line(int x0, int y0, int x1, int y1, int b);

void gfx_line(int x0, int y0, int x1, int y1, int b)
{
    if (!nocc || !occ_cutting) { emit_line(x0, y0, x1, y1, b); return; }
    const int lx0 = x0 < x1 ? x0 : x1, lx1 = x0 < x1 ? x1 : x0, ly0 = y0 < y1 ? y0 : y1, ly1 = y0 < y1 ? y1 : y0;
    if (occ_dirty) occ_index();
    if (ly1 < occ_ymin || ly0 >= occ_ymin + occ_yspan) { emit_line(x0, y0, x1, y1, b); return; }
    /* sichtbare Abschnitte: mit [0,1] anfangen, jeden Verdecker abziehen */
    float v[8][2];
    int nv = 1, hit = 0;
    const float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
    v[0][0] = 0; v[0][1] = 1;
    cur.cut_lines++;
    const int b0 = occ_full ? 0 : occ_band(ly0), b1 = occ_full ? 0 : occ_band(ly1);
    for (int band = b0; band <= b1 && nv; band++)
    for (int q = occ_full ? 0 : occ_band_at[band], qe = occ_full ? nocc : occ_band_at[band + 1]; q < qe && nv; q++) {
        const int k = occ_full ? q : occ_band_k[q];
        float sp[6][2];
        int ns;
        if (!occ_full && band != (occ_fb[k] > b0 ? occ_fb[k] : b0)) continue;   /* schon in einem frueheren Streifen geprueft */
        if (lx1 < occ_bb[k][0] || lx0 > occ_bb[k][2] || ly1 < occ_bb[k][1] || ly0 > occ_bb[k][3]) continue;
        if (occ_sign[k]) { cur.span_tests++; ns = occ_span(k, (float)x0, (float)y0, dx, dy, &sp[0][0], &sp[0][1]); }
        else { cur.poly_tests++; ns = occ_spans_poly(k, (float)x0, (float)y0, dx, dy, lx0, ly0, lx1, ly1, sp); }
        if (!ns) continue;
        hit = 1;
        for (int q = 0; q < ns; q++) {
        const float a = sp[q][0], c = sp[q][1];
        for (int i = nv - 1; i >= 0; i--) {
            if (c <= v[i][0] || a >= v[i][1]) continue;
            const float l = v[i][0], r = v[i][1];
            if (a > l && c < r) {                                    /* mittendrin: zwei Stuecke */
                v[i][1] = a;
                if (nv < 8) { v[nv][0] = c; v[nv][1] = r; nv++; }
            } else if (a > l) v[i][1] = a;
            else if (c < r) v[i][0] = c;
            else { v[i][0] = v[nv - 1][0]; v[i][1] = v[nv - 1][1]; nv--; }
        }
        }
    }
    if (!hit) { emit_line(x0, y0, x1, y1, b); return; }
    const float len = sqrtf(dx * dx + dy * dy);
    for (int i = 0; i < nv; i++) {
        if ((v[i][1] - v[i][0]) * len < 20.0f) continue;             /* Splitter unter gut einer Einheit weglassen */
        emit_line(x0 + (int)(dx * v[i][0]), y0 + (int)(dy * v[i][0]), x0 + (int)(dx * v[i][1]), y0 + (int)(dy * v[i][1]), b);
    }
}
void gfx_clip(int x0, int y0, int x1, int y1) { clip_on = 1; cx0 = x0; cy0 = y0; cx1 = x1; cy1 = y1; }
void gfx_clip_off(void) { clip_on = 0; }

/* Strich am Ausschnitt abschneiden (Liang-Barsky, Parameter in 1/4096). 0 = ganz ausserhalb */
static int clip_line(int *x0, int *y0, int *x1, int *y1)
{
    long dx = *x1 - *x0, dy = *y1 - *y0, t0 = 0, t1 = 4096;
    long p[4] = { -dx, dx, -dy, dy };
    long q[4] = { *x0 - cx0, cx1 - *x0, *y0 - cy0, cy1 - *y0 };
    for (int k = 0; k < 4; k++) {
        if (p[k] == 0) { if (q[k] < 0) return 0; continue; }
        long t = q[k] * 4096 / p[k];
        if (p[k] < 0) { if (t > t1) return 0; if (t > t0) t0 = t; }
        else { if (t < t0) return 0; if (t < t1) t1 = t; }
    }
    int ax = *x0, ay = *y0;
    if (t1 < 4096) { *x1 = ax + (int)(dx * t1 / 4096); *y1 = ay + (int)(dy * t1 / 4096); }
    if (t0 > 0) { *x0 = ax + (int)(dx * t0 / 4096); *y0 = ay + (int)(dy * t0 / 4096); }
    return 1;
}

static void emit_line(int x0, int y0, int x1, int y1, int b)
{
    if (b <= 0) return;
    if (clip_on) {
        if ((x0 < cx0 && x1 < cx0) || (x0 > cx1 && x1 > cx1) || (y0 < cy0 && y1 < cy0) || (y0 > cy1 && y1 > cy1)) return;
        if ((x0 < cx0 || x0 > cx1 || y0 < cy0 || y0 > cy1 || x1 < cx0 || x1 > cx1 || y1 < cy0 || y1 > cy1) && !clip_line(&x0, &y0, &x1, &y1)) return;
        if (x0 < -32000 || x0 > 32000 || y0 < -32000 || y0 > 32000 || x1 < -32000 || x1 > 32000 || y1 < -32000 || y1 > 32000) return;
    }
    if (x0 == x1 && y0 == y1) return;                     /* Nulllaenge stuende als heller Punkt auf der Roehre */
    if (b > 127) b = 127;
    if (npath > 0) {
        int last = npath - 1;
        int e = paths[last].start + paths[last].n - 1;
        if (paths[last].b == b && e == npt - 1 && pts[e].x == x0 && pts[e].y == y0) {
            /* in der Flucht des letzten Strichs? dann nur verlaengern */
            long ax = pts[e].x - pts[e - 1].x, ay = pts[e].y - pts[e - 1].y, bx = x1 - x0, by = y1 - y0;
            long long cross = (long long)ax * by - (long long)ay * bx, dot = (long long)ax * bx + (long long)ay * by;
            if (dot > 0 && cross * cross * 2500 <= ((long long)ax * ax + (long long)ay * ay) * ((long long)bx * bx + (long long)by * by)) {
                pts[e].x = (int16_t)x1; pts[e].y = (int16_t)y1;
                return;
            }
            if (npt < MAXPT) {
                pts[npt].x = (int16_t)x1; pts[npt].y = (int16_t)y1; npt++;
                paths[last].n++;
                return;
            }
        }
    }
    if (npath >= MAXPATH || npt + 2 > MAXPT) { cur.lost++; return; }
    paths[npath].start = (uint16_t)npt; paths[npath].n = 2; paths[npath].b = (uint8_t)b;
    npath++;
    pts[npt].x = (int16_t)x0; pts[npt].y = (int16_t)y0; npt++;
    pts[npt].x = (int16_t)x1; pts[npt].y = (int16_t)y1; npt++;
}

void gfx_poly(const pt_t *p, int n, int b, int closed)
{
    for (int i = 1; i < n; i++) gfx_line(p[i - 1].x, p[i - 1].y, p[i].x, p[i].y, b);
    if (closed && n > 2) gfx_line(p[n - 1].x, p[n - 1].y, p[0].x, p[0].y, b);
}

void gfx_dot(int x, int y, int b) { gfx_line(x - 5, y, x + 5, y, b); }

void gfx_circle(int cx, int cy, int r, int b, int segs, int turn)
{
    pt_t p[24];
    if (segs > 24) segs = 24;
    circle_pts(cx, cy, r, segs, turn, p);
    for (int i = 1; i <= segs; i++) {                     /* der letzte Strich schliesst exakt auf dem Startpunkt */
        const pt_t *a = &p[i - 1], *e = &p[i == segs ? 0 : i];
        gfx_line(a->x, a->y, e->x, e->y, b);
    }
}

void gfx_flush(void)
{
    int lx = 0x7FFF, ly = 0x7FFF;
    for (int i = 0; i < npath; i++) {
        const pt_t *p = &pts[paths[i].start];
        int m = paths[i].n;
        if (p[0].x != lx || p[0].y != ly) cur.jumps++;
        for (int k = 1; k < m; k++) vpy_draw_line_dev(dev(p[k - 1].x), dev(p[k - 1].y), dev(p[k].x), dev(p[k].y), paths[i].b);
        lx = p[m - 1].x; ly = p[m - 1].y;
        cur.strokes += m - 1;
    }
    cur.paths = npath;
    gfx_stats = cur;
    npath = npt = 0;
}
