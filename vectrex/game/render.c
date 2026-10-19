/* nicht zeitkritisch genug fuer -O3: die Zeit je Bild bestimmt der Strahl. Auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include "render.h"
#include "gfx.h"
#include "font.h"
#include "settings.h"
#include "fmath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BASE_UNITS   10.0f           /* Bildschirmeinheiten je Feldeinheit: Tisch 20 x 30 -> 200 x 300 */
#define BASE_CY      (-12.0f)        /* Tischmitte etwas unter der Bildmitte, oben bleibt Platz fuer die Punktezeile */

#define DEF_WALL     VP_RGB(64, 64, 160)
#define DEF_GREEN    VP_RGB(0, 255, 0)
#define DEF_BUMPER   VP_RGB(0, 0, 255)
#define DEF_SPINNER  VP_RGB(224, 224, 224)
#define FLIP_RISE    0.035f          /* Hoehe der Flipperfinger: Verschiebung der Deckflaeche je Abstand vom Fluchtpunkt */

static float v_ox, v_oy, v_s;        /* Q4 = v_o + feld * v_s */
static int   v_dim;
static float v_pcx, v_pcy, v_zf = 1.0f, v_dt;   /* Fluchtpunkt (Mitte des Tischfensters, Q4), Zoomfaktor, Bildzeit */
static int   w_cx, w_top, w_half;    /* Tischfenster auf dem Schirm (Q4): Mitte, Oberkante, halbe Breite - fuer die Punktezeile */
static float z_ratio, z_x, z_y;      /* Zoom: 0..1 und der Punkt, dem die Ansicht folgt */
#define DP(k, c)    (p[k][c] * VP_CQ) /* Bogenpunkt k, Koordinate c, aus den int16-Tischdaten */
#define BALL_DRAW   0.85f            /* der Ball wird etwas kleiner gezeichnet, als er in der Physik ist (wirkt sonst zu gross) */
#define ZOOM_TIME   0.45f            /* Sekunden Echtzeit (Original: 1 s Spielzeit) */

void render_view(const vp_table_def_t *t, float cx, float cy, float units)
{
    v_s = units * 16.0f;
    v_ox = cx * 16.0f - t->width * 0.5f * v_s;
    v_oy = cy * 16.0f - t->height * 0.5f * v_s;
    w_cx = (int)(cx * 16.0f); w_half = (int)(t->width * 0.5f * v_s); w_top = (int)(cy * 16.0f + t->height * 0.5f * v_s);
}

void render_zoom_reset(const vp_field_t *f) { z_ratio = 0; z_x = f->def->launch_x; z_y = f->def->launch_y; }
void render_clip_end(void) { gfx_clip_off(); }

void render_game_view(const vp_field_t *f, float dt, float zoom, float shake_x, float shake_y)
{
    const vp_table_def_t *t = f->def;
    render_default_view(t);
    v_pcx = v_ox + t->width * 0.5f * v_s; v_pcy = v_oy + t->height * 0.5f * v_s; v_zf = 1.0f; v_dt = dt;
    if (zoom > 1.001f && f->nballs == 1) { z_ratio += dt / ZOOM_TIME; if (z_ratio > 1) z_ratio = 1; }
    else { z_ratio -= dt / ZOOM_TIME; if (z_ratio < 0) z_ratio = 0; }
    if (f->nballs >= 1) vp_ball_draw_pos(f, &f->balls[0], &z_x, &z_y);
    v_ox += shake_x * 16.0f; v_oy += shake_y * 16.0f;
    if (z_ratio <= 0) return;
    {
        /* Fenster = der Platz, den der ganze Tisch ohne Zoom einnimmt; darin ein Ausschnitt um den Ball, nie ueber den Tischrand hinaus */
        const float wx0 = v_ox, wy0 = v_oy, s0 = v_s;
        static float zmax = 1.5f;                              /* beim Herausfahren (zoom schon 1.0) den letzten Faktor behalten */
        if (zoom > 1.001f) zmax = zoom;
        const float zf = 1.0f + (zmax - 1.0f) * z_ratio * z_ratio * (3.0f - 2.0f * z_ratio);
        const float span_x = t->width / zf, span_y = t->height / zf;
        float off_x = z_x - span_x * 0.5f, off_y = z_y - span_y * 0.5f;
        if (off_x < 0) off_x = 0;
        if (off_x > t->width - span_x) off_x = t->width - span_x;
        if (off_y < 0) off_y = 0;
        if (off_y > t->height - span_y) off_y = t->height - span_y;
        v_s = s0 * zf; v_zf = zf;
        v_ox = wx0 - off_x * v_s; v_oy = wy0 - off_y * v_s;
        gfx_clip((int)wx0 - 8, (int)wy0 - 8, (int)(wx0 + t->width * s0) + 8, (int)(wy0 + t->height * s0) + 8);
    }
}

void render_default_view(const vp_table_def_t *t)
{
    render_view(t, (float)g_set.cal_dx, BASE_CY + g_set.cal_dy, BASE_UNITS * (100 + g_set.cal_size) / 100.0f);
}

int render_x(float fx) { return (int)lroundf(v_ox + fx * v_s); }
int render_y(float fy) { return (int)lroundf(v_oy + fy * v_s); }
float render_units(void) { return v_s / 16.0f; }

/* Helligkeit in STUFEN, damit das Bild Tiefe hat. Das Auge sieht Verhaeltnisse, nicht Abstaende: 100 gegen 127 ist auf der
 * Roehre kaum ein Unterschied, 35 gegen 90 ein deutlicher. Ein fester Aufschlag auf alles (frueher +30) schob Waende,
 * Rampen und Bumper zusammen an die Obergrenze - dann sieht alles gleich aus.
 *   Kulisse  ruhende Rampen, Ebenen unter dem Spielfeld      LEVEL_DIM Prozent der Wandstufe
 *   Waende   WALL_LEVEL + BRIGHTNESS aus dem Einstellbild (nur diese Stufe und die Kulisse wandern mit)
 *   Wichtig  Bumper, Schleudern, Ziele, Spinner: feste Mindestwerte nahe der Obergrenze
 *   Voll     Ball, Flipper, alles was gerade aufleuchtet; Rampen mit Ball werden dazu doppelt gezogen
 * Farbe -> Helligkeit: reine Leuchtdichte wuerde Blau fast verschwinden lassen (die Waende des Originals sind blau), darum
 * zaehlt der hellste Farbkanal mit. Alpha dunkelt ab, Schwarz ist unsichtbar. Die uebliche Wandfarbe ergibt nominal 73. */
#define WALL_LEVEL 90
#define LEVEL_DIM  38
static int wall_level(void)
{
    int w = WALL_LEVEL + g_set.cal_bright;
    return w < 40 ? 40 : (w > 112 ? 112 : w);
}

int render_intensity(uint32_t c)
{
    int a = (int)(c >> 24), r = (int)((c >> 16) & 255), g = (int)((c >> 8) & 255), b = (int)(c & 255);
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int lum = (77 * r + 150 * g + 29 * b) >> 8;
    int v = ((mx * 154 + lum * 102) >> 8) * a / 255;
    if (v < 10) return 0;
    int i = (22 + v * 105 / 255) * wall_level() / 73;
    if (v_dim) i = i * (100 - v_dim) / 100;
    return i < 10 ? 10 : (i > 127 ? 127 : i);
}

/* Wichtiges ist heller als die Kulisse: feste Mindesthelligkeit je Art (wandert nicht mit BRIGHTNESS) */
/* Gerade gezeichnetes Element liegt auf einem zweiten Spielfeld (Tisch 7 oben links), auf dem kein Ball ist: dort oben ist
 * das Licht aus. Bumper, Flipper, Ziele und Waende treten auf die Stufe der Kulisse zurueck und werden erst hell, wenn der
 * Ball ueber die Rampe hinaufkommt (im Original wechselt dann die Farbe der ganzen Ebene). Gesetzt in draw_elem. */
static int v_off;
static int dim_level(void);

static int at_least(int i, int min)
{
    if (!i) return 0;
    if (v_off) return dim_level();                                   /* keine Mindesthelligkeit fuer ein dunkles Spielfeld */
    if (v_dim) min = min * (100 - v_dim) / 100;
    if (min > 127) min = 127;
    return i > min ? i : min;
}

/* Kulisse: deutlich unter den Waenden, aber noch sichtbar */
static int dim_level(void)
{
    int i = wall_level() * LEVEL_DIM / 100;
    if (v_dim) i = i * (100 - v_dim) / 100;
    return i < 12 ? 12 : i;
}

/* Rampe (hoehere Ebene nur aus Waenden) */
static int is_ramp(const vp_field_t *f, int e);

/* Eine hoehere Ebene mit Bumpern, Flippern oder Zielen ist ein zweites Spielfeld (Tisch 7 oben links); Ebenen nur aus
 * Waenden sind Rampen. */
static int level_is_playfield(const vp_table_def_t *t, int layer)
{
    static const vp_table_def_t *known;
    static uint32_t mask;
    if (known != t) {
        known = t; mask = 0;
        for (int e = 0; e < t->nelem; e++) {
            const vp_elem_def_t *d = &t->elems[e];
            if (d->layer > 0 && d->layer < 32 && (d->type == VP_BUMPER || d->type == VP_FLIPPER || d->type == VP_DROP))
                mask |= 1u << d->layer;
        }
    }
    return layer < 32 && ((mask >> layer) & 1);
}

/* Helligkeit eines Elements samt Ebene. Ein zweites Spielfeld ueber dem ersten ist heller als das darunter; Rampen sind
 * dunkel und leuchten voll auf, sobald ein Ball auf ihnen ist; Ebenen unter dem Spielfeld sind dunkler. Das Original
 * zeigt dasselbe ueber Farbwechsel. *glow = 0..1: wie stark ein getroffenes Element gerade nachleuchtet. */
static int elem_intensity(const vp_field_t *f, int e, uint32_t def_color, float *glow)
{
    const vp_elem_def_t *d = &f->def->elems[e];
    const vp_estate_t *s = &f->es[e];
    int flash, i;
    *glow = s->flash > 0 ? s->flash / VP_FLASH_TIME : 0;
    if (d->layer > 0 && level_is_playfield(f->def, d->layer) && vp_has_ball_at_layer(f, d->layer)
        && !((d->flags & VF_HAS_INACT) && (d->inact_color >> 24) == 0)) {
        /* bespielte Ebene, auf der gerade der Ball ist: hell (sonst wie jede andere Wand) */
        uint32_t full = s->has_new_color ? s->new_color : ((d->flags & VF_HAS_COLOR) ? d->color : def_color);
        i = render_intensity(full);
        if (!i) return 0;
        i = at_least(i, 118);
        if (d->flags & VF_HAS_INACT) i += (int)((127 - i) * s->layer_frac);
    } else if (d->layer > 0 && (d->flags & VF_HAS_INACT)) {
        /* Rampen: Kulisse, solange kein Ball darauf ist, dann volle Helligkeit (und doppelt gezogen, siehe draw_elem) */
        if (!render_intensity(vp_elem_color(f, e, def_color, &flash))) return 0;   /* im Original gerade unsichtbar */
        i = dim_level();
        i += (int)((127 - i) * s->layer_frac);
    } else {
        i = render_intensity(vp_elem_color(f, e, def_color, &flash));
        if (d->layer < 0 && i) { i = i * 55 / 100; if (i < 12) i = 12; }   /* unter dem Spielfeld */
    }
    if (i && *glow > 0) i += (int)((127 - i) * *glow);                 /* schnell hell, weich wieder aus */
    if (v_off && i > dim_level()) i = dim_level();
    return i;
}

static int is_ramp(const vp_field_t *f, int e)
{
    const vp_elem_def_t *d = &f->def->elems[e];
    return d->layer > 0 && (d->flags & VF_HAS_INACT) && !level_is_playfield(f->def, d->layer);
}

static int segs_for(float r_units)
{
    if (r_units < 4.5f) return 4;
    if (r_units < 8.0f) return 6;
    if (r_units < 14.0f) return 8;
    if (r_units < 26.0f) return 12;
    if (r_units < 50.0f) return 16;
    return 24;
}

static int under(float x, float y, int b);

/* min_segs < 0: genau so viele Ecken (Vieleck statt Kreis) */
static void circle_f(float cx, float cy, float r, int b, int min_segs)
{
    float ru = r * v_s / 16.0f;
    int n = segs_for(ru);
    if (min_segs < 0) n = -min_segs;
    else if (n < min_segs) n = min_segs;
    gfx_circle(render_x(cx), render_y(cy), (int)lroundf(r * v_s), under(cx, cy, b), n, n == 4 ? 0 : 256 / (2 * n));
}

/* Spielt der Ball auf dem zweiten Spielfeld (Tisch 7 oben links), tritt zurueck, was darunter liegt: alles von anderen
 * Ebenen, dessen Mitte im Rechteck um das obere Spielfeld (seine Ziele, Bumper, Flipper plus eine Einheit Rand) liegt, wird auf die Stufe der Kulisse gedunkelt - je Strich, denn
 * die Aussenwand des Tisches laeuft als ein Element durch beide Bereiche. u_on gilt nur waehrend render_table. */
static int   u_on, u_layer, u_cur;       /* aktiv, Ebene des oberen Spielfelds, Ebene des gerade gezeichneten Elements */
static float u_x0, u_y0, u_x1, u_y1;
static int dim_level(void);
static int under(float x, float y, int b)
{
    if (!u_on || u_cur == u_layer || x < u_x0 || x > u_x1 || y < u_y0 || y > u_y1) return b;
    const int d = dim_level();
    return b > d ? d : b;
}

static void under_update(const vp_field_t *f)
{
    static const vp_table_def_t *known;
    static int layer = -1;
    static float box[4];
    const vp_table_def_t *t = f->def;
    if (known != t) {                                                /* einmal je Tisch: Rechteck um das erste zweite Spielfeld */
        known = t; layer = -1;
        for (int e = 0; e < t->nelem && layer < 0; e++)
            if (t->elems[e].layer > 0 && level_is_playfield(t, t->elems[e].layer)) layer = t->elems[e].layer;
        box[0] = box[1] = 1e9f; box[2] = box[3] = -1e9f;
        for (int e = 0; e < t->nelem && layer >= 0; e++) {
            const vp_elem_def_t *d = &t->elems[e];
            if (d->layer != layer) continue;
#define U_ADD(x, y) do { if ((x) < box[0]) box[0] = (x); if ((x) > box[2]) box[2] = (x); if ((y) < box[1]) box[1] = (y); if ((y) > box[3]) box[3] = (y); } while (0)
            /* nur was das Spielfeld ausmacht (Ziele, Bumper, Flipper): der Weg hinauf gehoert zur selben Ebene, laeuft aber um
             * den halben Tisch und wuerde das Rechteck auf alles ausdehnen */
            if (d->type == VP_DROP)
                for (int k = 0; k < d->count; k++) { const vp_seg_t g = vp_seg(t, d->first + k); U_ADD(g.x1, g.y1); U_ADD(g.x2, g.y2); }
            else if (d->type == VP_BUMPER) U_ADD(d->p[0], d->p[1]);
            else if (d->type == VP_FLIPPER) { float fx, fy; vp_flipper_pivot(f, e, &fx, &fy); U_ADD(fx, fy); }
#undef U_ADD
        }
        box[0] -= 1.0f; box[1] -= 1.0f; box[2] += 1.0f; box[3] += 1.0f;   /* Rand: die Waende um das Spielfeld */
    }
    u_on = layer >= 0 && box[0] < box[2] && vp_has_ball_at_layer(f, layer);
    u_layer = layer; u_x0 = box[0]; u_y0 = box[1]; u_x1 = box[2]; u_y1 = box[3];
}

static void line_f(float x1, float y1, float x2, float y2, int b)
{
    gfx_line(render_x(x1), render_y(y1), render_x(x2), render_y(y2), under((x1 + x2) * 0.5f, (y1 + y2) * 0.5f, b));
}

/* 3D nur in der vergroesserten Ansicht: der ganze Tisch (Rundenbeginn, mehrere Baelle, Pause beim Herausfahren) ist immer
 * flach - dort waeren es zu viele Striche, und das Bild flimmerte. */
static int view3d(void) { return g_set.view3d; }                 /* VERSUCH: auch ohne Zoom (vorher: v_zf > 1.08f) */

/* Je Element: was auf einer hoeheren Ebene liegt (Tisch 7 oben links), ist nur dann ein Koerper, wenn der Ball gerade dort
 * oben spielt - sonst ist die Ebene flach und tritt zurueck. */
static int lvl_of(const vp_field_t *f, const vp_elem_def_t *d)
{
    if (d->layer > 0 && !vp_has_ball_at_layer(f, d->layer)) return V3D_OFF;
    return view3d();
}

/* ---- Koerper ----
 * Was gezeichnet wird, ist die Oberkante - sie liegt dort, wo die Physik rechnet. Der Fuss ist zum Fluchtpunkt (Mitte des
 * Tischfensters) hin versetzt, mit Zoom staerker; folgt die Ansicht dem Ball, wandert er unter der Oberkante. */
static void foot(float *x, float *y)
{
    const float k = FLIP_RISE * v_zf;
    *x -= (*x - (v_pcx - v_ox) / v_s) * k;
    *y -= (*y - (v_pcy - v_oy) / v_s) * k;
}

/* Fuss einer Strecke samt Kanten nach unten (posts: Bit 0 am Anfang, Bit 1 am Ende) - eine Kette ohne Sprung */
static void body_seg(float x1, float y1, float x2, float y2, int b, int posts)
{
    float ax = x1, ay = y1, bx = x2, by = y2;
    foot(&ax, &ay); foot(&bx, &by);
    gfx_occ_cutting(1);                                              /* Fuss und Kanten: was hinter einem Koerper liegt, faellt weg */
    if (posts & 1) line_f(x1, y1, ax, ay, b);
    line_f(ax, ay, bx, by, b);
    if (posts & 2) line_f(bx, by, x2, y2, b);
    gfx_occ_cutting(0);
}

/* Seitenflaeche einer Strecke (zwischen Oberkante und Fuss) als Verdecker */
static void occ_face(float x1, float y1, float x2, float y2)
{
    float ax = x1, ay = y1, bx = x2, by = y2;
    foot(&ax, &ay); foot(&bx, &by);
    const pt_t q[4] = { { (int16_t)render_x(x1), (int16_t)render_y(y1) }, { (int16_t)render_x(x2), (int16_t)render_y(y2) },
                        { (int16_t)render_x(bx), (int16_t)render_y(by) }, { (int16_t)render_x(ax), (int16_t)render_y(ay) } };
    gfx_occ_add(q, 4);
}

/* Ecken eines Flipperfingers: Deckflaeche (u) dort, wo die Physik rechnet, Fuss (b) zur Bildmitte versetzt. 0 = entartet */
static int flipper_body(const vp_field_t *f, int e, int *ux, int *uy, int *bx, int *by)
{
    float x1, y1, x2, y2;
    vp_flipper_line(f, e, &x1, &y1, &x2, &y2);
    float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f) return 0;
    const float ox = -dy / l * 0.24f, oy = dx / l * 0.24f, k = FLIP_RISE * v_zf, tx = ox * 0.3f, ty = oy * 0.3f;
    const float fx[4] = { x1 + ox, x2 + tx, x2 - tx, x1 - ox }, fy[4] = { y1 + oy, y2 + ty, y2 - ty, y1 - oy };
    for (int i = 0; i < 4; i++) {
        ux[i] = render_x(fx[i]); uy[i] = render_y(fy[i]);
        bx[i] = ux[i] - (int)((ux[i] - v_pcx) * k); by[i] = uy[i] - (int)((uy[i] - v_pcy) * k);
    }
    return 1;
}

/* Bumper, die ueber den Tisch wandern (Tisch 2), bleiben flach: ein mitwandernder Fussring flackert zu sehr. Gemerkt wird je
 * Element, ob es seinen Platz je verlassen hat. */
static int bumper_moves(const vp_field_t *f, int e)
{
    if (f->def == &vp_tables[7] || f->def->elems[e].p[2] < 0.3f) return 1;   /* Tisch 8 und winzige Pfosten: flach (flackerte) */
    static const vp_table_def_t *known;
    static uint8_t moved[VP_MAX_ELEMS / 8];
    const vp_elem_def_t *d = &f->def->elems[e];
    if (known != f->def) { known = f->def; memset(moved, 0, sizeof moved); }
    if (fabsf(f->es[e].cx - d->p[0]) + fabsf(f->es[e].cy - d->p[1]) > 0.01f) moved[e >> 3] |= (uint8_t)(1u << (e & 7));
    return (moved[e >> 3] >> (e & 7)) & 1;
}

/* ---- Deckel fuer geschlossene Bloecke ----
 * Ueberall, wo von oben ein Deckel liegt, ist der Tisch massiv: Schleudern, Inseln, Ecken hinter Waenden. Welche Flaechen
 * das sind, bestimmt tools/lids.py vorab am PC ("wohin kommt der Ball nie?") und legt sie als Vielecke in
 * core/gen/lid_data.c ab - auf der Konsole wird nichts mehr gesucht. */
static int lid_range(const vp_field_t *f, int *end)
{
    const int t = (int)(f->def - vp_tables);
    *end = vp_lid_first[t + 1];
    return vp_lid_first[t];
}

/* Liegt die Strecke am Rand eines Schleuder-Klotzes (Deckel mit Kick-Wand)? Dann bekommt sie keinen eigenen Fuss: der Klotz
 * ist sein Umriss, der Deckel und die EINE Flaeche, auf die der Ball trifft - mehr Striche braucht er nicht. */
static int near_sling(const vp_field_t *f, float x1, float y1, float x2, float y2)
{
    const float mx = (x1 + x2) * 50.0f, my = (y1 + y2) * 50.0f;         /* Mitte in 1/100 Feldeinheit */
    int end, c = lid_range(f, &end);
    for (; c < end; c++) {
        if (vp_lids[c].kick < 0) continue;
        const int16_t *q = &vp_lid_pts[2 * vp_lids[c].first];
        for (int k = 0, n = vp_lids[c].n; k < n; k++) {
            const float ax = q[2 * k], ay = q[2 * k + 1], ex = q[2 * ((k + 1) % n)] - ax, ey = q[2 * ((k + 1) % n) + 1] - ay;
            const float l2 = ex * ex + ey * ey;
            float u = l2 > 0 ? ((mx - ax) * ex + (my - ay) * ey) / l2 : 0;
            u = u < 0 ? 0 : (u > 1 ? 1 : u);
            const float dx = mx - ax - u * ex, dy = my - ay - u * ey;
            if (dx * dx + dy * dy < 64.0f) return 1;                     /* naeher als 0,08 Einheiten */
        }
    }
    return 0;
}

/* Das Ergebnis haengt nur an der Tischgeometrie: einmal je Tisch fuer jede Strecke und jede Wand bestimmen, nicht in jedem
 * Bild fuer jede Wand neu gegen alle Deckel rechnen (das kostete auf dem RP2350 Millisekunden je Bild) */
#define SL_MAX_SEGS 1536
static const vp_table_def_t *sl_known;
static uint8_t sl_seg[SL_MAX_SEGS / 8], sl_wall[VP_MAX_ELEMS / 8];
static void sling_update(const vp_field_t *f)
{
    const vp_table_def_t *t = f->def;
    if (sl_known == t) return;
    sl_known = t;
    memset(sl_seg, 0, sizeof sl_seg); memset(sl_wall, 0, sizeof sl_wall);
    for (int i = 0; i < t->nseg && i < SL_MAX_SEGS; i++) {
        const vp_seg_t g = vp_seg(t, i);
        if (near_sling(f, g.x1, g.y1, g.x2, g.y2)) sl_seg[i >> 3] |= (uint8_t)(1u << (i & 7));
    }
    for (int e = 0; e < t->nelem && e < VP_MAX_ELEMS; e++) {
        const vp_elem_def_t *d = &t->elems[e];
        if (d->type != VP_WALL) continue;
        const vp_seg_t g = vp_seg(t, d->first);
        if (near_sling(f, g.x1, g.y1, g.x2, g.y2)) sl_wall[e >> 3] |= (uint8_t)(1u << (e & 7));
    }
}
static int on_sling_seg(int i)  { return i < SL_MAX_SEGS && ((sl_seg[i >> 3] >> (i & 7)) & 1); }
static int on_sling_wall(int e) { return (sl_wall[e >> 3] >> (e & 7)) & 1; }

/* Blitz ohne zusaetzliche Striche: dieselben Kanten gehen beim Treffer auf volle Helligkeit und klingen ab */
static int flash_level(int idle, float glow)
{
    float g = glow * 1.6f;
    if (g > 1) g = 1;
    return idle + (int)((127 - idle) * g);
}

/* Vieleck, als das ein Bumper gezeichnet wird (wie circle_f) */
static int bumper_segs(const vp_elem_def_t *d)
{
    if (d->p[5] >= 3) return (int)d->p[5];
    int n = segs_for(d->p[2] * v_s / 16.0f);
    return n < 6 ? 6 : n;
}

static int side_level(void) { return at_least(84, 84); }               /* Fuss und Kanten der Spielelemente */

/* 3D EVERYTHING: Waende als Koerper kosten gut 40 % mehr Striche. Strichpuffer und Befehlsliste fassen rund 560; darueber
 * fehlten Teile des Bildes. Darum bleiben die Waende flach, solange das Bild zu voll waere (voller Tisch ohne Zoom auf den
 * grossen Tischen), und werden Koerper, sobald Platz ist. Entschieden wird nach der Strichzahl des letzten Bildes, mit
 * Abstand zwischen Aus- und Einschalten, damit es nicht hin und her springt. */
#define STROKE_LIMIT 520
static int s_walls3d = 1;
static void walls3d_update(void)
{
    const int n = gfx_stats.strokes;
    if (s_walls3d) { if (n > STROKE_LIMIT) s_walls3d = 0; }
    else if (n * 31 / 20 < STROKE_LIMIT) s_walls3d = 1;
}
static int walls3d(void) { return view3d() >= V3D_ALL && s_walls3d; }

/* Vor dem Zeichnen: alle Flaechen der Koerper als Verdecker anmelden. Gezeichnet wird in der festen Reihenfolge des Tisches;
 * Oberkanten, Ball und alles Flache bleiben ganz, nur Fuesse und Kanten nach unten gehen durch die Verdeckung. Das genuegt
 * ohne Tiefenvergleich: der Fuss eines Koerpers ist zur Bildmitte hin versetzt, liegt er dabei in der Seitenflaeche eines
 * anderen, so steht der andere naeher an der Mitte - also davor. Deckflaechen (Bumper, Flipper) liegen ueber allem. */
static int walls3d(void);
static void register_bodies(const vp_field_t *f)
{
    const vp_table_def_t *t = f->def;
    const int lvl = view3d();
    if (lvl == V3D_OFF) return;
    if (lvl >= V3D_OBJECTS) {                                        /* Deckel der massiven Bloecke */
        int end, c = lid_range(f, &end);
        for (; c < end; c++) {
            pt_t q[24];
            const int16_t *p = &vp_lid_pts[2 * vp_lids[c].first];
            const int n = vp_lids[c].n > 24 ? 24 : vp_lids[c].n;
            if (vp_lids[c].kick >= 0 && (f->es[vp_lids[c].kick].retracted || !f->es[vp_lids[c].kick].visible)) continue;
            for (int k = 0; k < n; k++) { q[k].x = (int16_t)render_x(p[2 * k] * 0.01f); q[k].y = (int16_t)render_y(p[2 * k + 1] * 0.01f); }
            gfx_occ_poly(q, n);
        }
    }
    for (int e = 0; e < t->nelem; e++) {
        const vp_elem_def_t *d = &t->elems[e];
        const vp_estate_t *s = &f->es[e];
        const int lv = lvl_of(f, d);
        if (d->layer < 0) continue;
        switch (d->type) {
        case VP_WALL:
            if (!s->visible || s->retracted) break;
            if (d->p[0] > 0.01f ? lv >= V3D_OBJECTS : (d->layer == 0 && walls3d() && !on_sling_wall(e)))
                occ_face(s->seg.x1, s->seg.y1, s->seg.x2, s->seg.y2);
            break;
        case VP_PATH:
            if (d->layer == 0 && walls3d() && !(d->flags & VF_IGNORE_BALL))   /* aufgemalt (Pfeile): flach */
                for (int k = 0; k < d->count; k++) {
                    if (on_sling_seg(d->first + k)) continue;
                    const vp_seg_t g = vp_seg(t, d->first + k);
                    occ_face(g.x1, g.y1, g.x2, g.y2);
                }
            break;
        case VP_ARC:
            if (d->layer == 0 && walls3d()) {
                const int16_t (*p)[2] = &t->dpts[d->dfirst];
                int n = d->dcount, step = (g_set.detail == DETAIL_LOW && n >= 5) ? 2 : 1, prev = 0;
                for (int k = step; ; k += step) {
                    if (k > n - 1) k = n - 1;
                    occ_face(DP(prev, 0), DP(prev, 1), DP(k, 0), DP(k, 1));
                    prev = k;
                    if (k == n - 1) break;
                }
            }
            break;
        case VP_DROP:
            if (lv >= V3D_OBJECTS)
                for (int k = 0; k < d->count; k++)
                    if (!(s->mask & (1u << k))) { const vp_seg_t g = vp_seg(t, d->first + k); occ_face(g.x1, g.y1, g.x2, g.y2); }
            break;
        case VP_BUMPER:
            if (lv >= V3D_OBJECTS && d->p[2] > 0) {
                const int n = bumper_segs(d);
                float fx = s->cx, fy = s->cy;
                foot(&fx, &fy);
                gfx_occ_circle(render_x(s->cx), render_y(s->cy), (int)lroundf(d->p[2] * v_s), n, n == 4 ? 0 : 256 / (2 * n));
                float dx = fx - s->cx, dy = fy - s->cy, l = sqrtf(dx * dx + dy * dy);
                if (l * v_s > 12.0f && !bumper_moves(f, e)) {          /* Mantel der Walze */
                    float nx = -dy / l * d->p[2], ny = dx / l * d->p[2];
                    const pt_t q[4] = { { (int16_t)render_x(s->cx + nx), (int16_t)render_y(s->cy + ny) }, { (int16_t)render_x(s->cx - nx), (int16_t)render_y(s->cy - ny) },
                                        { (int16_t)render_x(fx - nx), (int16_t)render_y(fy - ny) }, { (int16_t)render_x(fx + nx), (int16_t)render_y(fy + ny) } };
                    gfx_occ_add(q, 4);
                }
            }
            break;
        case VP_FLIPPER:
            if (lv >= V3D_FLIPPERS) {
                int ux[4], uy[4], bx[4], by[4];
                if (!flipper_body(f, e, ux, uy, bx, by)) break;
                const pt_t top[4] = { { (int16_t)ux[0], (int16_t)uy[0] }, { (int16_t)ux[1], (int16_t)uy[1] }, { (int16_t)ux[2], (int16_t)uy[2] }, { (int16_t)ux[3], (int16_t)uy[3] } };
                gfx_occ_add(top, 4);
                for (int i = 0; i < 4; i++) {
                    int j = (i + 1) & 3;
                    const pt_t q[4] = { { (int16_t)ux[i], (int16_t)uy[i] }, { (int16_t)ux[j], (int16_t)uy[j] }, { (int16_t)bx[j], (int16_t)by[j] }, { (int16_t)bx[i], (int16_t)by[i] } };
                    gfx_occ_add(q, 4);
                }
            }
            break;
        default: break;
        }
    }
}

static void draw_elem(const vp_field_t *f, int e, int rev)
{
    const vp_table_def_t *t = f->def;
    const vp_elem_def_t *d = &t->elems[e];
    const vp_estate_t *s = &f->es[e];
    float glow;
    int b;
    if (d->layer < 0) return;                                        /* Ebenen unter dem Spielfeld (Tisch 8): weg, sie flackerten nur */
    v_off = d->layer > 0 && level_is_playfield(t, d->layer) && !vp_has_ball_at_layer(f, d->layer);
    u_cur = d->layer;
    switch (d->type) {
    case VP_WALL: {
        if (!s->visible || s->retracted) return;
        b = elem_intensity(f, e, DEF_WALL, &glow);
        if (d->p[0] > 0.01f) {
            /* Schleuder, Kicker, Ballretter: die Kante doppelt gezogen (hell), als Koerper dazu die Flaeche, auf die der Ball
             * trifft (zwei Kanten, Fuss). Getroffen leuchtet genau das auf - Kante und Flaeche gehen auf volle Helligkeit
             * und klingen ab. Keine Striche nur fuer den Blitz. */
            if (!b) break;
            b = flash_level(at_least(b, 100) > 104 ? 104 : at_least(b, 100), glow);
            line_f(s->seg.x1, s->seg.y1, s->seg.x2, s->seg.y2, b);
            line_f(s->seg.x2, s->seg.y2, s->seg.x1, s->seg.y1, b);
            if (lvl_of(f, d) >= V3D_OBJECTS) body_seg(s->seg.x1, s->seg.y1, s->seg.x2, s->seg.y2, flash_level(at_least(60, 60), glow), 3);
            if (glow > 0.12f) {
                /* BLITZ: die Kante noch zwei- bis viermal obendrauf (jeder Durchgang legt noch einmal so viel Licht auf dieselbe
                 * Linie), der ganze Klotz leuchtet mit (Umriss voll hell) und links und rechts der Kante ein Strich daneben.
                 * Klingt mit dem Nachleuchten ab; kostet nur waehrend der Blitz steht. */
                const float dx = s->seg.x2 - s->seg.x1, dy = s->seg.y2 - s->seg.y1, l = sqrtf(dx * dx + dy * dy);
                for (int k = glow > 0.6f ? 4 : (glow > 0.3f ? 3 : 2); k > 0; k--) {
                    if (k & 1) line_f(s->seg.x2, s->seg.y2, s->seg.x1, s->seg.y1, 127);
                    else line_f(s->seg.x1, s->seg.y1, s->seg.x2, s->seg.y2, 127);
                }
                if (glow > 0.25f) {
                    int end, c = lid_range(f, &end);
                    for (; c < end; c++) {
                        if (vp_lids[c].kick != e) continue;
                        const int16_t *q = &vp_lid_pts[2 * vp_lids[c].first];
                        for (int k = 0, n = vp_lids[c].n; k < n; k++) {
                            const int k2 = (k + 1) % n;
                            line_f(q[2 * k] * 0.01f, q[2 * k + 1] * 0.01f, q[2 * k2] * 0.01f, q[2 * k2 + 1] * 0.01f, 127);
                        }
                        break;
                    }
                }
                if (l > 1e-4f) {
                    const float ox = -dy / l * 0.22f, oy = dx / l * 0.22f;
                    line_f(s->seg.x1 + ox, s->seg.y1 + oy, s->seg.x2 + ox, s->seg.y2 + oy, 127);
                    line_f(s->seg.x2 - ox, s->seg.y2 - oy, s->seg.x1 - ox, s->seg.y1 - oy, 127);
                }
            }
            break;
        }
        if (rev) line_f(s->seg.x2, s->seg.y2, s->seg.x1, s->seg.y1, b);
        else line_f(s->seg.x1, s->seg.y1, s->seg.x2, s->seg.y2, b);
        if (b && d->layer == 0 && walls3d() && !on_sling_wall(e))
            body_seg(s->seg.x1, s->seg.y1, s->seg.x2, s->seg.y2, b * 55 / 100, 3);
        break;
    }
    case VP_PATH: {
        b = elem_intensity(f, e, DEF_WALL, &glow);
        if (!b) return;
        /* Rampe mit Ball: hin und zurueck gezogen = doppelt so hell wie ein einzelner Strich voller Helligkeit */
        int twice = is_ramp(f, e) && s->layer_frac > 0.5f;
        if (rev || twice) for (int k = d->count - 1; k >= 0; k--) { const vp_seg_t g = vp_seg(t, d->first + k); line_f(g.x2, g.y2, g.x1, g.y1, b); }
        if (!rev || twice) for (int k = 0; k < d->count; k++) { const vp_seg_t g = vp_seg(t, d->first + k); line_f(g.x1, g.y1, g.x2, g.y2, b); }
        if (d->layer == 0 && walls3d() && !(d->flags & VF_IGNORE_BALL))   /* aufgemalt (Pfeile, Fuehrungen): flach */
            for (int k = 0; k < d->count; k++) {
                if (on_sling_seg(d->first + k)) continue;
                const vp_seg_t g = vp_seg(t, d->first + k);
                body_seg(g.x1, g.y1, g.x2, g.y2, b * 55 / 100, 1 | (k == d->count - 1 ? 2 : 0));
            }
        break;
    }
    case VP_ARC: {
        b = elem_intensity(f, e, DEF_WALL, &glow);
        if (!b) return;
        const int16_t (*p)[2] = &t->dpts[d->dfirst];
        int n = d->dcount, step = (g_set.detail == DETAIL_LOW && n >= 5) ? 2 : 1;
        for (int pass = 0, passes = (is_ramp(f, e) && s->layer_frac > 0.5f) ? 2 : 1; pass < passes; pass++) {
            int back = rev ^ pass;                                     /* zweiter Durchgang: denselben Weg zurueck */
            int prev = back ? n - 1 : 0;
            for (int k = step; ; k += step) {
                if (k > n - 1) k = n - 1;
                int cur = back ? n - 1 - k : k;
                line_f(DP(prev, 0), DP(prev, 1), DP(cur, 0), DP(cur, 1), b);
                prev = cur;
                if (k == n - 1) break;
            }
        }
        if (d->layer == 0 && walls3d()) {                               /* Fuss des Bogens, Kanten nur an den Enden */
            int prev = 0;
            for (int k = step; ; k += step) {
                if (k > n - 1) k = n - 1;
                body_seg(DP(prev, 0), DP(prev, 1), DP(k, 0), DP(k, 1), b * 55 / 100, (prev == 0 ? 1 : 0) | (k == n - 1 ? 2 : 0));
                prev = k;
                if (k == n - 1) break;
            }
        }
        break;
    }
    case VP_BUMPER: {
        if (d->p[3] > 0 && g_set.detail == DETAIL_HIGH) {
            int fl;
            b = render_intensity(vp_elem_outer_color(f, e, &fl));
            if (b) circle_f(s->cx, s->cy, d->p[3], b, 6);
        }
        if (d->p[2] > 0) {
            const int ns = -bumper_segs(d);
            b = at_least(elem_intensity(f, e, DEF_BUMPER, &glow), 120);
            if (!b) return;
            circle_f(s->cx, s->cy, d->p[2], b, ns);
            if (glow > 0.12f) {
                /* getroffen: BLITZ - der Ring noch mehrmals obendrauf, der Kern leuchtet, aussen ein Hof; klingt weich ab */
                for (int k = glow > 0.6f ? 4 : (glow > 0.3f ? 3 : 2); k > 0; k--) circle_f(s->cx, s->cy, d->p[2], 127, ns);
                circle_f(s->cx, s->cy, d->p[2] * 0.6f, 127, ns);
                if (glow > 0.2f) circle_f(s->cx, s->cy, d->p[2] * 1.35f, 127, ns);
                if (glow > 0.3f)                                       /* Strahlenkranz, der mit dem Abklingen einzieht */
                    for (int k = 0; k < 8; k++) {
                        float a = k * 0.785398f + 0.39f, c = fm_cosf(a), sn = fm_sinf(a), r0 = d->p[2] * 1.5f, r1 = d->p[2] * (1.5f + 0.9f * glow);
                        line_f(s->cx + c * r0, s->cy + sn * r0, s->cx + c * r1, s->cy + sn * r1, 127);
                    }
            } else gfx_dot(render_x(s->cx), render_y(s->cy), b);
            if (lvl_of(f, d) >= V3D_OBJECTS && !bumper_moves(f, e)) {
                /* Walze: zweiter Ring als Fuss, dazu die beiden Mantellinien quer zur Verschiebung */
                float fx = s->cx, fy = s->cy;
                foot(&fx, &fy);
                float dx = fx - s->cx, dy = fy - s->cy, l = sqrtf(dx * dx + dy * dy);
                gfx_occ_cutting(1);                                    /* Fussring und Mantel: verdeckt von der eigenen Deckflaeche und allem davor */
                circle_f(fx, fy, d->p[2], side_level(), ns);
                if (l * v_s > 12.0f) {
                    float nx = -dy / l * d->p[2], ny = dx / l * d->p[2];
                    line_f(s->cx + nx, s->cy + ny, fx + nx, fy + ny, side_level());
                    line_f(fx - nx, fy - ny, s->cx - nx, s->cy - ny, side_level());
                }
                gfx_occ_cutting(0);
            }
        }
        break;
    }
    case VP_DROP: {
        b = at_least(elem_intensity(f, e, DEF_GREEN, &glow), 112);
        if (!b) return;
        for (int k = 0; k < d->count; k++) {
            if (s->mask & (1u << k)) continue;
            const vp_seg_t g = vp_seg(t, d->first + k);
            line_f(g.x1, g.y1, g.x2, g.y2, b);
            if (lvl_of(f, d) >= V3D_OBJECTS) body_seg(g.x2, g.y2, g.x1, g.y1, side_level(), 3);
        }
        if (glow > 0.12f && s->prev_hit)                                /* das eben getroffene Ziel: leuchtet voll auf, dann ist es weg */
            for (int k = 0; k < d->count; k++) {
                if (!(s->prev_hit & (1u << k)) || !(s->mask & (1u << k))) continue;
                const vp_seg_t g = vp_seg(t, d->first + k);
                const float dx = g.x2 - g.x1, dy = g.y2 - g.y1, l = sqrtf(dx * dx + dy * dy);
                for (int q = glow > 0.5f ? 4 : 2; q > 0; q--) {            /* mehrmals obendrauf = noch heller */
                    if (q & 1) line_f(g.x2, g.y2, g.x1, g.y1, 127);
                    else line_f(g.x1, g.y1, g.x2, g.y2, 127);
                }
                if (l > 1e-4f) {                                          /* links und rechts daneben */
                    const float ox = -dy / l * 0.25f, oy = dx / l * 0.25f;
                    line_f(g.x1 + ox, g.y1 + oy, g.x2 + ox, g.y2 + oy, 127);
                    line_f(g.x2 - ox, g.y2 - oy, g.x1 - ox, g.y1 - oy, 127);
                }
                if (lvl_of(f, d) >= V3D_OBJECTS) body_seg(g.x2, g.y2, g.x1, g.y1, flash_level(side_level(), glow), 3);
            }
        break;
    }
    case VP_ROLL: {
        int flash;
        if (!s->visible) return;
        uint32_t group = vp_elem_color(f, e, DEF_GREEN, &flash);
        for (int k = 0; k < d->count; k++) {
            const vp_roll_t *r = &f->rolls[d->first + k];
            b = render_intensity(r->has_color ? r->color : group);
            if (!b) continue;
            float ru = r->r * v_s / 16.0f;
            int x = render_x(r->x), y = render_y(r->y);
            if (s->mask & (1u << k)) {                                 /* aktiv: im Original gefuellt -> hell, mit Kern */
                b = at_least(b, 118);
                if (ru < 2.2f) { gfx_dot(x, y, b); continue; }
                circle_f(r->x, r->y, r->r, b, 4);
                if (ru < 12.0f) gfx_dot(x, y, b);
                else circle_f(r->x, r->y, r->r * 0.55f, b, 4);
            } else {                                                   /* inaktiv: im Original nur Umriss -> dunkel */
                int bd = b * 45 / 100;
                if (bd < 10) bd = 10;
                /* Umriss auch klein, sonst sieht man die Lampe nicht (Olli, 2026-10-09); nur DETAIL LOW oder winzig: ein Punkt */
                if (ru < 2.2f || (ru < 6.0f && g_set.detail == DETAIL_LOW)) gfx_dot(x, y, bd);
                else circle_f(r->x, r->y, r->r, bd, ru < 6.0f ? -4 : 4);   /* klein: genau vier Striche */
            }
        }
        break;
    }
    case VP_SPINNER: {
        if (d->p[7] != 0) {
            /* Spinball: der Spinner ist immer zu sehen - in Ruhe ein Strich, beim Drehen klappt er zum Rechteck auf */
            float a = d->p[7] - 10.0f, ux = fm_cosf(a) * d->p[2], uy = fm_sinf(a) * d->p[2];
            float x = d->p[0], y = d->p[1];
            b = at_least(elem_intensity(f, e, DEF_SPINNER, &glow), 118);
            if (s->st == 1) {
                float ox = -uy * 0.7f, oy = ux * 0.7f;
                line_f(x - ux - ox, y - uy - oy, x + ux - ox, y + uy - oy, b);
                line_f(x + ux - ox, y + uy - oy, x + ux + ox, y + uy + oy, b);
                line_f(x + ux + ox, y + uy + oy, x - ux + ox, y - uy + oy, b);
                line_f(x - ux + ox, y - uy + oy, x - ux - ox, y - uy - oy, b);
            } else line_f(x - ux, y - uy, x + ux, y + uy, b);
            break;
        }
        if (s->st != 1) return;
        b = at_least(elem_intensity(f, e, DEF_SPINNER, &glow), 118);
        circle_f(d->p[0], d->p[1], d->p[2], b, 4);
        break;
    }
    case VP_FLIPPER: {
        /* Flipper: volle Helligkeit und als schmaler Keil (drei Striche) statt als einzelner Strich */
        b = at_least(elem_intensity(f, e, DEF_GREEN, &glow), 127);
        if (!b) return;
        if (lvl_of(f, d) < V3D_FLIPPERS) {                             /* flach: drei Striche */
            float x1, y1, x2, y2;
            vp_flipper_line(f, e, &x1, &y1, &x2, &y2);
            float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy);
            if (l < 1e-4f) return;
            float ox = -dy / l * 0.24f, oy = dx / l * 0.24f;
            line_f(x1 + ox, y1 + oy, x2, y2, b);
            line_f(x2, y2, x1 - ox, y1 - oy, b);
            line_f(x1 - ox, y1 - oy, x1 + ox, y1 + oy, b);
            break;
        }
        {
            /* Koerper: die helle Deckflaeche liegt genau dort, wo die Physik den Finger hat (der Ball soll sichtbar an der
             * hellen Kante abprallen); der Fuss rueckt zur Bildmitte - je weiter aussen und je naeher die Kamera (Zoom),
             * desto mehr. Folgt die Ansicht dem Ball, wandert er unter der Deckflaeche. Physik: unveraendert. */
            int ux[4], uy[4], bx[4], by[4];
            const int side = at_least(84, 84);
            if (!flipper_body(f, e, ux, uy, bx, by)) return;
            for (int i = 0; i < 4; i++) gfx_line(ux[i], uy[i], ux[(i + 1) & 3], uy[(i + 1) & 3], b);      /* Deckflaeche */
            gfx_occ_cutting(1);                                    /* was unter der Deckflaeche oder hinter einer Seite liegt, faellt weg */
            for (int i = 0; i < 4; i++) {
                gfx_line(ux[i], uy[i], bx[i], by[i], side);                                                /* Kante nach unten */
                gfx_line(bx[i], by[i], bx[(i + 1) & 3], by[(i + 1) & 3], side);                            /* Fuss */
            }
            gfx_occ_cutting(0);
        }
        break;
    }
    default: break;
    }
}

static void draw_shapes(const vp_field_t *f)
{
    static const int MAX_DOTS[DETAIL_N] = { 16, 28, 48 };
    int dots = 0;
    const float t = f->now_ms * 0.001f;
    for (int i = 0; i < f->nshapes; i++) {
        const vp_shape_t *s = &f->shapes[i];
        int b = render_intensity(s->color);
        if (!b) continue;
        switch (s->kind) {
        case VS_LINE: line_f(s->v[0], s->v[1], s->v[2], s->v[3], b); break;
        case VS_DISC:
        case VS_CIRCLE:
            if (s->v[2] * v_s / 16.0f < 2.5f) {
                if (dots++ >= MAX_DOTS[g_set.detail]) break;
                /* Sterne funkeln: jeder in seinem eigenen Takt. Der Ort (nicht die Nummer) bestimmt den Takt, damit ein Stern
                 * beim Wandern der Karte nicht den Takt wechselt. */
                float ph = s->v[0] * 12.9898f + s->v[1] * 78.233f;
                float tw = 0.72f + 0.28f * fm_sinf(t * (2.6f + 1.9f * fm_sinf(ph)) + ph);
                gfx_dot(render_x(s->v[0]), render_y(s->v[1]), (int)(b * tw));
            } else circle_f(s->v[0], s->v[1], s->v[2], b, 4);
            break;
        case VS_ARC: {
            float span = s->v[5] - s->v[4];
            int n = (int)ceilf(fabsf(span) / 0.9f);
            if (n < 2) n = 2;
            float px = s->v[0] + s->v[2] * fm_cosf(s->v[4]), py = s->v[1] + s->v[3] * fm_sinf(s->v[4]);
            for (int k = 1; k <= n; k++) {
                float a = s->v[4] + span * k / n;
                float x = s->v[0] + s->v[2] * fm_cosf(a), y = s->v[1] + s->v[3] * fm_sinf(a);
                line_f(px, py, x, y, b);
                px = x; py = y;
            }
            break;
        }
        default: break;
        }
    }
}

void render_table(const vp_field_t *f, int dim)
{
    const vp_table_def_t *t = f->def;
    v_dim = dim;
    under_update(f);
    sling_update(f);
    walls3d_update();
    const int mark = gfx_occ_mark();
    register_bodies(f);
    gfx_occ_cutting(0);                                              /* Oberkanten und alles Flache bleiben ganz */
    for (int i = 0; i < t->norder; i++) draw_elem(f, t->order[i] & 0x7FFF, t->order[i] & 0x8000);
    v_off = 0;
    u_on = 0;
    /* die Bahnen vom Einlauf auf die tieferen Finger (f->flip_step, REALISTIC) wirken nur in der Physik - gezeichnet sahen sie
     * nicht gut aus (Olli, 2026-10-09) */
    draw_shapes(f);
    gfx_occ_cut(mark);                                               /* Ball, Punktezeile und Schrift danach: unverdeckt */
    gfx_occ_cutting(1);
    v_dim = 0;
}

static void draw_ball(float x, float y, float radius, int b)
{
    /* ein Ring aus zwoelf kurzen Strichen, mehrmals gezogen: jeder Durchgang legt noch einmal so viel Licht auf dieselbe
     * Linie. Zwei Durchgaenge sind die Vorgabe; BALL BRIGHTNESS ueber 0 legt je 10 einen weiteren dazu (bis fuenf). */
    int r = (int)lroundf(radius * BALL_DRAW * v_s);
    int passes = 2 + (g_set.cal_ball > 0 ? (g_set.cal_ball + 9) / 10 : 0);
    for (int k = 0; k < passes; k++) gfx_circle(render_x(x), render_y(y), r, b, 12, 0);
}

/* Rollen sichtbar machen: ein Ring um die Kugel, dessen Ebene die Rollachse enthaelt. Von oben sieht man nur seine obere
 * Haelfte - einen Bogen quer zur Fahrtrichtung, der von hinten ueber die Kuppe nach vorn wandert und dann hinten neu
 * auftaucht (die andere Haelfte des Rings). Nur EIN Ring: zwei saehen sich nach einer Vierteldrehung gleich, und ein
 * schneller Ball dreht sich je Bild so weit, dass sie zu stehen oder rueckwaerts zu laufen schienen. */
static struct { float roll, mx, my; } s_roll[VP_MAX_BALLS];
static void draw_ball_roll(int i, float x, float y, float vx, float vy, float radius, int b)
{
    const float sp = sqrtf(vx * vx + vy * vy), r = radius * BALL_DRAW;
    if (sp > 0.25f) { s_roll[i].mx = vx / sp; s_roll[i].my = vy / sp; }
    else if (s_roll[i].mx == 0 && s_roll[i].my == 0) s_roll[i].my = 1.0f;
    s_roll[i].roll = fmodf(s_roll[i].roll + sp * v_dt / radius, 3.14159265f);
    const float mx = s_roll[i].mx, my = s_roll[i].my, off = -fm_cosf(s_roll[i].roll) * r;   /* -r hinten ... +r vorn */
    float px = 0, py = 0;
    for (int k = 0; k <= 5; k++) {
        float t = k * (3.14159265f / 5.0f), c = fm_cosf(t) * r, s = fm_sinf(t) * off;
        float qx = x - my * c + mx * s, qy = y + mx * c + my * s;
        if (k) line_f(px, py, qx, qy, b);
        px = qx; py = qy;
    }
}

/* Stoessel von oben: Abschussgummi, auf dem der Ball ruht, darunter die Feder (wird beim Ziehen zusammengedrueckt) */
static void draw_plunger(const vp_table_def_t *t, float pull)
{
    const float x = t->launch_x;
    const float head = t->launch_y - 0.7f * pull - t->ball_radius - 0.1f, top = head - 0.14f;
    const float base = t->launch_y - t->ball_radius - 1.75f;
    const int rubber = at_least(118, 118), spring = at_least(84, 84);
    line_f(x - 0.42f, head, x + 0.42f, head, rubber);
    line_f(x + 0.42f, top, x - 0.42f, top, rubber);
    float px = x - 0.42f, py = top;
    for (int k = 1; k <= 6; k++) {
        float nx = k == 6 ? x : x + ((k & 1) ? 0.27f : -0.27f), ny = top + (base - top) * (float)k / 6.0f;
        line_f(px, py, nx, ny, spring);
        px = nx; py = ny;
    }
}

static float s_plunger;
void render_plunger(float pull) { s_plunger = pull < 0 ? 0 : (pull > 1 ? 1 : pull); }

void render_balls(const vp_field_t *f)
{
    /* Der Ball ist das Hellste im Bild: volle Helligkeit und kurze Striche (langsamer Strahl = heller). Die Leuchtspur des
     * Originals wird nicht gezeichnet - sie entsteht wie bei den Schuessen alter Vektorspiele aus dem Nachleuchten der Roehre. */
    int b = 127 + (g_set.cal_ball < 0 ? g_set.cal_ball : 0);
    if (b < 30) b = 30;
    for (int i = 0; i < f->nballs; i++) {
        float x, y;
        vp_ball_draw_pos(f, &f->balls[i], &x, &y);
        draw_ball(x, y, f->def->ball_radius, b);
        draw_ball_roll(i, x, y, f->balls[i].vx, f->balls[i].vy, f->def->ball_radius, b);
    }
    if (f->in_progress && f->nballs == 0 && f->nactions == 0 && !(f->lost_ms >= 0 && (int32_t)f->now_ms - f->lost_ms <= 750)) {
        /* kein Ball im Spiel: der naechste liegt in der Startbahn auf dem Stoessel bereit, bis ein Knopf ihn abschiesst */
        const vp_table_def_t *t = f->def;
        draw_ball(t->launch_x, t->launch_y - 0.7f * s_plunger, t->ball_radius, b);   /* gezogen: Ball und Gummi tiefer */
        draw_plunger(t, s_plunger);
    } else if (f->in_progress) draw_plunger(f->def, 0);              /* der Stoessel gehoert zum Tisch: immer zu sehen */
}

void render_hud(const vp_field_t *f)
{
    char buf[48], who[8];
    const int cx = w_cx, half = w_half;
    int y = w_top + Q4(7 + g_set.cal_hud_dy);
    int unit = half * 3 / (int)(f->def->width * 10);                  /* Rasterschritt: Zeichen 0.6 x 0.9 Feldeinheiten (ohne Zoom) */
    if (unit < 12) unit = 12;
    if (f->msg[0]) {
        int w = text_width(f->msg, unit);
        if (w > 2 * half) unit = unit * 2 * half / w;
        if (unit < 9) unit = 9;
        text_center(cx, y, f->msg, unit, 124);
        return;
    }
    snprintf(buf, sizeof buf, "%lld", (long long)f->score);
    text_draw(cx - half, y, buf, unit, 120);
    if (f->in_progress) {
        who[0] = 0;
        if (f->nplayers > 1) snprintf(who, sizeof who, "P%d ", f->player + 1);      /* wer gerade dran ist */
        if (f->mult > 1.001) {
            int whole = (int)f->mult, frac = (int)lround((f->mult - whole) * 100);
            if (frac >= 100) { whole++; frac = 0; }
            if (frac) snprintf(buf, sizeof buf, "%sX%d.%02d B%d", who, whole, frac, f->ball_number);
            else snprintf(buf, sizeof buf, "%sX%d B%d", who, whole, f->ball_number);
        } else snprintf(buf, sizeof buf, "%sB%d", who, f->ball_number);
        text_draw(cx + half - text_width(buf, unit), y, buf, unit, 100);
    }
}
