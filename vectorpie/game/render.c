/*
 * render.c - VectorPie-Fassung von vectrex/game/render.c: dasselbe Bild (Helligkeitsstufen, Koerper mit Fuss zum
 * Fluchtpunkt, Verdeckung, Zoom, Blitz), dazu
 *   Farbe       jeder Strich traegt den Farbton des Originals (set_hue), die Stufe dunkelt ihn ab
 *   Querformat  im Spiel fuellt der Tisch immer die Bildbreite und rollt senkrecht mit dem Ball (render_game_view)
 *   mehr Ecken  Kreise und Ball sind runder: Ecken kosten hier nichts, es zaehlt die Weglaenge
 *   Details des Originals, die auf der Vectrex dem Strahl geopfert sind, sind wieder da (2026-10-10):
 *     - Ebenen unter dem Spielfeld (Pool Party: die Billardkugeln und die Bonuslampen)
 *     - Boegen als Boegen (aus Mittelpunkt, Radien, Winkeln) statt als sparsamer Linienzug
 *     - alle Bumper als Koerper, auch auf Tisch 8, die wandernden auf Tisch 2 und die kleinen Pfosten
 *     - auf Tisch 7 alle Sterne der Karte (die Grenzen dafuer hebt CMakeLists.txt im Kern an), geordnet gezeichnet
 * Aenderungen an der Darstellung, die beide Fassungen betreffen, in beiden Dateien nachziehen.
 */
#include "render.h"
#include "gfx.h"
#include "font.h"
#include "segdisp.h"
#include "settings.h"
#include "fmath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BASE_UNITS   11.0f           /* Bildschirmeinheiten je Feldeinheit: Tisch 20 x 30 -> 220 x 330 von 378 Hoehe */
#define BASE_CY      (-15.0f)        /* Tischmitte etwas unter der Bildmitte, oben bleibt Platz fuer die Punktezeile */

#define DEF_WALL     VP_RGB(64, 64, 160)
#define DEF_GREEN    VP_RGB(0, 255, 0)
#define DEF_BUMPER   VP_RGB(0, 0, 255)
#define DEF_SPINNER  VP_RGB(224, 224, 224)
#define FLIP_RISE    0.035f          /* Hoehe der Flipperfinger: Verschiebung der Deckflaeche je Abstand vom Fluchtpunkt */

static float v_ox, v_oy, v_s;        /* Q4 = v_o + feld * v_s */
static int   v_dim;
static float v_pcx, v_pcy, v_zf = 1.0f, v_dt;   /* Fluchtpunkt (Mitte des Tischfensters, Q4), Zoomfaktor, Bildzeit */
static int   w_hud;                  /* halbe Breite der Punktezeile: die des Tischfensters, im Querformat also die des Bildes */
static int   w_cx, w_top, w_half;    /* Tischfenster auf dem Schirm (Q4): Mitte, Oberkante, halbe Breite - fuer die Punktezeile */
static float z_ratio, z_x, z_y;      /* Zoom: 0..1 und der Punkt, dem die Ansicht folgt */
#define BALL_DRAW   0.85f            /* der Ball wird etwas kleiner gezeichnet, als er in der Physik ist (wirkt sonst zu gross) */
#define ZOOM_TIME   0.45f            /* Sekunden Echtzeit (Original: 1 s Spielzeit) */

/* ---- Farbe ----
 * Die Vectrex-Fassung macht aus jeder Farbe eine Helligkeitsstufe (render_intensity). Hier bleibt die Stufe - sie traegt die
 * Abstufung Kulisse < Waende < Wichtiges < Voll - und der Farbton des Originals kommt dazu: der hellste Kanal wird auf 255
 * gezogen, damit nicht doppelt abgedunkelt wird. Reines Blau (Bumper des Originals) leuchtet auf einer Farbroehre schwach;
 * es bekommt etwas Weiss dazu. Was aufblitzt, blitzt weiss (alle drei Strahlen = das Hellste, was die Roehre kann). */
#define HUE_FLASH    0xFFFFFFu
static uint32_t v_hue = 0xFFFFFFu;
static void set_hue(uint32_t rgb) { v_hue = rgb & 0xFFFFFFu; gfx_color(v_hue); }
static uint32_t hue_of(uint32_t argb)
{
    int r = (int)((argb >> 16) & 255), g = (int)((argb >> 8) & 255), b = (int)(argb & 255);
    const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (mx < 8) return 0xFFFFFFu;
    r = r * 255 / mx; g = g * 255 / mx; b = b * 255 / mx;
    const int lum = (77 * r + 150 * g + 29 * b) >> 8;
    if (lum < 64) {
        const int w = (64 - lum) * 100 / 89;
        r = r + w > 255 ? 255 : r + w; g = g + w > 255 ? 255 : g + w; b = b + w > 255 ? 255 : b + w;
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void render_view(const vp_table_def_t *t, float cx, float cy, float units)
{
    v_s = units * 16.0f;
    v_ox = cx * 16.0f - t->width * 0.5f * v_s;
    v_oy = cy * 16.0f - t->height * 0.5f * v_s;
    w_cx = (int)(cx * 16.0f); w_half = (int)(t->width * 0.5f * v_s); w_top = (int)(cy * 16.0f + t->height * 0.5f * v_s);
    w_hud = w_half;
    v_pcx = cx * 16.0f; v_pcy = cy * 16.0f; v_zf = 1.0f;             /* Fluchtpunkt: Tischmitte (render_game_view setzt ihn je Bild neu) */
}

void render_zoom_reset(const vp_field_t *f) { z_ratio = 0; z_x = f->def->launch_x; z_y = f->def->launch_y; }
void render_clip_end(void) { gfx_clip_off(); }

void render_game_view(const vp_field_t *f, float dt, float zoom, float shake_x, float shake_y)
{
    const vp_table_def_t *t = f->def;
    render_default_view(t);
    v_pcx = v_ox + t->width * 0.5f * v_s; v_pcy = v_oy + t->height * 0.5f * v_s; v_zf = 1.0f; v_dt = dt;
    /* QUERFORMAT: der Tisch fuellt im Spiel immer die Bildbreite (rund 2,2-fach) und rollt senkrecht mit - auch am
     * Rundenbeginn und mit mehreren Baellen; die Ansicht folgt dann dem untersten Ball (dem, der den Flippern am naechsten
     * ist) und gleitet ihm nach, statt zu springen, wenn der unterste wechselt. Die Zoomstufe der Einstellungen zaehlt hier
     * nur als an/aus. Hochkant bleibt es wie auf der Vectrex: Zoom nur mit genau einem Ball, sonst der ganze Tisch. */
    const int wide = gfx_half_w() > 200.0f;
    if (zoom > 1.001f && (wide || f->nballs == 1)) { z_ratio += dt / ZOOM_TIME; if (z_ratio > 1) z_ratio = 1; }
    else { z_ratio -= dt / ZOOM_TIME; if (z_ratio < 0) z_ratio = 0; }
    if (f->nballs >= 1) {
        int k = 0;
        float bx, by;
        if (wide) for (int i = 1; i < f->nballs; i++) if (f->balls[i].y < f->balls[k].y) k = i;
        vp_ball_draw_pos(f, &f->balls[k], &bx, &by);
        if (wide) { const float a = dt * 14.0f > 1.0f ? 1.0f : dt * 14.0f; z_x += (bx - z_x) * a; z_y += (by - z_y) * a; }
        else { z_x = bx; z_y = by; }
    }
    v_ox += shake_x * 16.0f; v_oy += shake_y * 16.0f;
    if (z_ratio <= 0) return;
    {
        /* Fenster = der Platz, den der ganze Tisch ohne Zoom einnimmt; darin ein Ausschnitt um den Ball, nie ueber den Tischrand hinaus */
        const float wx0 = v_ox, wy0 = v_oy, s0 = v_s;
        static float zmax = 1.5f;                              /* beim Herausfahren (zoom schon 1.0) den letzten Faktor behalten */
        static float z_was;                                    /* z_ratio im letzten Bild: beim Hineinfahren springt zmax aufs Ziel */
        /* Das Fenster behaelt seine Hoehe und waechst mit dem Zoom in die Breite, bis es an den Bildrand stoesst. Im
         * Querformat ist der Faktor der, bei dem der Tisch genau die Bildbreite fuellt. */
        const float cx = wx0 + t->width * 0.5f * s0, room = (gfx_half_w() - 4.0f) * 16.0f - fabsf(cx);
        /* Querformat: Stufe 3 (zoom 1.6) = Bildbreite, 4 und 5 naeher heran (START im Spiel, play.c); ein Wechsel
         * waehrend der Zoom steht, gleitet */
        if (zoom > 1.001f) {
            const float target = wide ? 2.0f * room / (t->width * s0) * (zoom > 1.6f ? 1.0f + (zoom - 1.6f) * 1.5f : 1.0f) : zoom;
            if (z_was <= 0) zmax = target;
            else { const float a = dt * 6.0f > 1.0f ? 1.0f : dt * 6.0f; zmax += (target - zmax) * a; }
        }
        z_was = z_ratio;
        if (zmax < 1.0f) zmax = 1.0f;
        const float zf = 1.0f + (zmax - 1.0f) * z_ratio * z_ratio * (3.0f - 2.0f * z_ratio);
        float win_w = t->width * s0 * zf;
        if (win_w > 2.0f * room) win_w = 2.0f * room;
        if (win_w < t->width * s0) win_w = t->width * s0;
        v_s = s0 * zf; v_zf = zf;
        const float span_x = win_w / v_s, span_y = t->height / zf, win_x0 = cx - win_w * 0.5f;
        float off_x = z_x - span_x * 0.5f, off_y = z_y - span_y * 0.5f;
        if (off_x > t->width - span_x) off_x = t->width - span_x;
        if (off_x < 0) off_x = 0;
        if (off_y > t->height - span_y) off_y = t->height - span_y;
        if (off_y < 0) off_y = 0;
        v_ox = win_x0 - off_x * v_s; v_oy = wy0 - off_y * v_s;
        w_hud = (int)(win_w * 0.5f);
        gfx_clip((int)win_x0 - 8, (int)wy0 - 8, (int)(win_x0 + win_w) + 8, (int)(wy0 + t->height * s0) + 8);
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

/* Stufe einer Farbe; setzt zugleich ihren Farbton fuer die folgenden Striche */
static int col_intensity(uint32_t argb) { set_hue(hue_of(argb)); return render_intensity(argb); }

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
        i = col_intensity(full);
        if (!i) return 0;
        i = at_least(i, 118);
        if (d->flags & VF_HAS_INACT) i += (int)((127 - i) * s->layer_frac);
    } else if (d->layer > 0 && (d->flags & VF_HAS_INACT)) {
        /* Rampen: Kulisse, solange kein Ball darauf ist, dann volle Helligkeit (und doppelt gezogen, siehe draw_elem) */
        if (!col_intensity(vp_elem_color(f, e, def_color, &flash))) return 0;   /* im Original gerade unsichtbar */
        i = dim_level();
        i += (int)((127 - i) * s->layer_frac);
    } else {
        i = col_intensity(vp_elem_color(f, e, def_color, &flash));
        if (d->layer < 0 && i) { i = i * 85 / 100; if (i < 12) i = 12; }   /* unter dem Spielfeld: einen Hauch zurueck */
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
    if (r_units < 3.0f) return 6;
    if (r_units < 6.0f) return 8;
    if (r_units < 10.0f) return 12;
    if (r_units < 18.0f) return 16;
    if (r_units < 36.0f) return 24;
    if (r_units < 70.0f) return 32;
    return 48;
}

static int under(float x, float y, int b);
/* BODEN. Die Oberkanten der Koerper liegen dort, wo die Physik rechnet; ihr Fuss - und damit die Spielflaeche - ist zum
 * Fluchtpunkt hin versetzt. Was auf der Spielflaeche LIEGT (Rollover-Lampen, aufgemalte Linien, die Formen der Tische), sass
 * unversetzt neben einer Wand scheinbar auf deren Seitenflaeche (Olli, 2026-10-10). Es rueckt darum mit - um den HALBEN
 * Versatz der Fuesse (FLOOR_K): von einer Bahn zwischen zwei Waenden sieht man nur den Streifen zwischen dem Fuss der
 * aeusseren und der Oberkante der inneren Wand (der Rest liegt hinter der inneren), und dessen Mitte liegt genau einen
 * halben Versatz neben der Bahnmitte. Mit dem ganzen Versatz saesse die Lampe auf der Oberkante der inneren Wand.
 * v_floor ist gesetzt, solange so etwas gezeichnet wird und die Waende Koerper sind; line_f und circle_f versetzen selbst. */
#define FLOOR_K 0.5f
static int v_floor;
static void floor_pt(float *x, float *y)
{
    if (!v_floor) return;
    const float k = FLIP_RISE * v_zf * FLOOR_K;
    *x -= (*x - (v_pcx - v_ox) / v_s) * k;
    *y -= (*y - (v_pcy - v_oy) / v_s) * k;
}
static float floor_scale(void) { return v_floor ? 1.0f - FLIP_RISE * v_zf * FLOOR_K : 1.0f; }

/* min_segs < 0: genau so viele Ecken (Vieleck statt Kreis) */
static void circle_f(float cx, float cy, float r, int b, int min_segs)
{
    floor_pt(&cx, &cy); r *= floor_scale();
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
    floor_pt(&x1, &y1); floor_pt(&x2, &y2);
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
    /* Auf der Vectrex bleiben wandernde Bumper (Tisch 2), die von Tisch 8 und winzige Pfosten flach: ihr Fussring flackerte
     * dort. Hier sind alle Koerper. */
    (void)f; (void)e;
    return 0;
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
#define STROKE_LIMIT 20000             /* VectorPie: der Puffer fasst alles, die Waende sind immer Koerper */
static int s_walls3d = 1;
static void walls3d_update(void)
{
    const int n = gfx_stats.strokes;
    if (s_walls3d) { if (n > STROKE_LIMIT) s_walls3d = 0; }
    else if (n * 31 / 20 < STROKE_LIMIT) s_walls3d = 1;
}
static int walls3d(void) { return view3d() >= V3D_ALL && s_walls3d; }

/* Punkte eines Bogens aus Mittelpunkt, Radien und Winkeln (p[2..7] des Elements), so fein, wie ein Kreis dieser Groesse
 * gezeichnet wuerde. Die Vectrex-Fassung zeichnet eine sparsame Punktliste (t->dpts); die Physik rechnet in beiden mit den
 * Wandstuecken des Tisches. Liefert die Zahl der Punkte (2..ARC_MAX). */
#define ARC_MAX 97
static int arc_pts(const vp_elem_def_t *d, float (*q)[2])
{
    const float cx = d->p[2], cy = d->p[3], rx = d->p[4], ry = d->p[5], a0 = d->p[6], span = d->p[7] - d->p[6];
    const float turn = fabsf(span) / 6.2831853f;
    int n = (int)ceilf(turn * (float)segs_for((rx > ry ? rx : ry) * v_s / 16.0f) * 1.5f);
    float sn, c, ds, dc;
    if (n < 2) n = 2;
    if (n > ARC_MAX - 1) n = ARC_MAX - 1;
    fm_sincos(a0, &sn, &c);
    fm_sincos(span / (float)n, &ds, &dc);
    for (int i = 0; i <= n; i++) {
        q[i][0] = cx + rx * c; q[i][1] = cy + ry * sn;
        const float nc = c * dc - sn * ds;
        sn = sn * dc + c * ds; c = nc;
    }
    return n + 1;
}

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
                float q[ARC_MAX][2];
                const int n = arc_pts(d, q);
                for (int k = 1; k < n; k++) occ_face(q[k - 1][0], q[k - 1][1], q[k][0], q[k][1]);
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

/* ---- Zeichnen je Element ----
 * Je Typ eine Funktion, draw_elem waehlt sie. Ueberall: b ist die Helligkeitsstufe (0 = unsichtbar), glow 0..1 das
 * Nachleuchten eines Treffers, rev = die Reihenfolge des Tisches will die Strecke rueckwaerts (der Strahl springt so
 * weniger). Was ein Element als Koerper zeigt (Fuss, Kanten), haengt an lvl_of / walls3d. */

/* Blitz: die Kante n-mal obendrauf (jeder Durchgang legt noch einmal so viel Licht auf dieselbe Linie), abwechselnd hin
 * und zurueck ... */
static void flash_edge(float x1, float y1, float x2, float y2, int n)
{
    for (int k = n; k > 0; k--) {
        if (k & 1) line_f(x2, y2, x1, y1, 127);
        else line_f(x1, y1, x2, y2, 127);
    }
}

/* ... und links und rechts daneben ein Strich im Abstand off */
static void flash_sides(float x1, float y1, float x2, float y2, float off)
{
    const float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy);
    if (l <= 1e-4f) return;
    const float ox = -dy / l * off, oy = dx / l * off;
    line_f(x1 + ox, y1 + oy, x2 + ox, y2 + oy, 127);
    line_f(x2 - ox, y2 - oy, x1 - ox, y1 - oy, 127);
}

/* Schleuder, Kicker, Ballretter (Wand mit Kick): die Kante doppelt gezogen (hell), als Koerper dazu die Flaeche, auf die
 * der Ball trifft (zwei Kanten, Fuss). Getroffen leuchtet genau das auf - Kante und Flaeche gehen auf volle Helligkeit und
 * klingen ab. Dazu der BLITZ: die Kante noch zwei- bis viermal obendrauf, der ganze Klotz leuchtet mit (Umriss seines
 * Deckels voll hell), links und rechts der Kante ein Strich daneben. Klingt mit dem Nachleuchten ab. */
static void draw_kicker(const vp_field_t *f, int e, const vp_elem_def_t *d, const vp_estate_t *s, int b, float glow)
{
    const vp_seg_t g = s->seg;
    if (!b) return;
    b = flash_level(at_least(b, 100) > 104 ? 104 : at_least(b, 100), glow);
    line_f(g.x1, g.y1, g.x2, g.y2, b);
    line_f(g.x2, g.y2, g.x1, g.y1, b);
    if (lvl_of(f, d) >= V3D_OBJECTS) body_seg(g.x1, g.y1, g.x2, g.y2, flash_level(at_least(60, 60), glow), 3);
    if (glow <= 0.12f) return;
    gfx_color(HUE_FLASH);
    flash_edge(g.x1, g.y1, g.x2, g.y2, glow > 0.6f ? 4 : (glow > 0.3f ? 3 : 2));
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
    flash_sides(g.x1, g.y1, g.x2, g.y2, 0.22f);
    gfx_color(v_hue);
}

static void draw_wall(const vp_field_t *f, int e, const vp_elem_def_t *d, const vp_estate_t *s, int rev)
{
    float glow;
    int b;
    if (!s->visible || s->retracted) return;
    b = elem_intensity(f, e, DEF_WALL, &glow);
    if (d->p[0] > 0.01f) { draw_kicker(f, e, d, s, b, glow); return; }
    if (rev) line_f(s->seg.x2, s->seg.y2, s->seg.x1, s->seg.y1, b);
    else line_f(s->seg.x1, s->seg.y1, s->seg.x2, s->seg.y2, b);
    if (b && d->layer == 0 && walls3d() && !on_sling_wall(e))
        body_seg(s->seg.x1, s->seg.y1, s->seg.x2, s->seg.y2, b * 55 / 100, 3);
}

static void draw_path(const vp_field_t *f, int e, const vp_elem_def_t *d, const vp_estate_t *s, int rev)
{
    const vp_table_def_t *t = f->def;
    float glow;
    const int b = elem_intensity(f, e, DEF_WALL, &glow);
    if (!b) return;
    /* Rampe mit Ball: hin und zurueck gezogen = doppelt so hell wie ein einzelner Strich voller Helligkeit */
    const int twice = is_ramp(f, e) && s->layer_frac > 0.5f;
    v_floor = (d->flags & VF_IGNORE_BALL) && d->layer <= 0 && walls3d();   /* aufgemalt: liegt auf dem Boden */
    if (rev || twice) for (int k = d->count - 1; k >= 0; k--) { const vp_seg_t g = vp_seg(t, d->first + k); line_f(g.x2, g.y2, g.x1, g.y1, b); }
    if (!rev || twice) for (int k = 0; k < d->count; k++) { const vp_seg_t g = vp_seg(t, d->first + k); line_f(g.x1, g.y1, g.x2, g.y2, b); }
    if (d->layer == 0 && walls3d() && !(d->flags & VF_IGNORE_BALL))   /* aufgemalt (Pfeile, Fuehrungen): flach */
        for (int k = 0; k < d->count; k++) {
            if (on_sling_seg(d->first + k)) continue;
            const vp_seg_t g = vp_seg(t, d->first + k);
            body_seg(g.x1, g.y1, g.x2, g.y2, b * 55 / 100, 1 | (k == d->count - 1 ? 2 : 0));
        }
}

static void draw_arc(const vp_field_t *f, int e, const vp_elem_def_t *d, const vp_estate_t *s, int rev)
{
    float glow, q[ARC_MAX][2];
    const int b = elem_intensity(f, e, DEF_WALL, &glow);
    if (!b) return;
    const int n = arc_pts(d, q);
    for (int pass = 0, passes = (is_ramp(f, e) && s->layer_frac > 0.5f) ? 2 : 1; pass < passes; pass++) {
        const int back = rev ^ pass;                                   /* zweiter Durchgang: denselben Weg zurueck */
        for (int k = 1; k < n; k++) {
            const int a = back ? n - k : k - 1, c = back ? n - 1 - k : k;
            line_f(q[a][0], q[a][1], q[c][0], q[c][1], b);
        }
    }
    if (d->layer == 0 && walls3d())                                    /* Fuss des Bogens, Kanten nur an den Enden */
        for (int k = 1; k < n; k++)
            body_seg(q[k - 1][0], q[k - 1][1], q[k][0], q[k][1], b * 55 / 100, (k == 1 ? 1 : 0) | (k == n - 1 ? 2 : 0));
}

static void draw_bumper(const vp_field_t *f, int e, const vp_elem_def_t *d, const vp_estate_t *s)
{
    float glow;
    int b;
    if (d->p[3] > 0 && g_set.detail == DETAIL_HIGH) {                  /* der aeussere Ring */
        int fl;
        b = col_intensity(vp_elem_outer_color(f, e, &fl));
        if (b) circle_f(s->cx, s->cy, d->p[3], b, 6);
    }
    if (d->p[2] <= 0) return;
    const int ns = -bumper_segs(d);
    b = at_least(elem_intensity(f, e, DEF_BUMPER, &glow), 120);
    if (!b) return;
    circle_f(s->cx, s->cy, d->p[2], b, ns);
    if (glow > 0.12f) {
        /* getroffen: BLITZ - der Ring noch mehrmals obendrauf, der Kern leuchtet, aussen ein Hof; klingt weich ab */
        gfx_color(HUE_FLASH);
        for (int k = glow > 0.6f ? 4 : (glow > 0.3f ? 3 : 2); k > 0; k--) circle_f(s->cx, s->cy, d->p[2], 127, ns);
        circle_f(s->cx, s->cy, d->p[2] * 0.6f, 127, ns);
        if (glow > 0.2f) circle_f(s->cx, s->cy, d->p[2] * 1.35f, 127, ns);
        if (glow > 0.3f)                                               /* Strahlenkranz, der mit dem Abklingen einzieht */
            for (int k = 0; k < 8; k++) {
                const float a = k * 0.785398f + 0.39f, c = fm_cosf(a), sn = fm_sinf(a), r0 = d->p[2] * 1.5f, r1 = d->p[2] * (1.5f + 0.9f * glow);
                line_f(s->cx + c * r0, s->cy + sn * r0, s->cx + c * r1, s->cy + sn * r1, 127);
            }
        gfx_color(v_hue);
    } else gfx_dot(render_x(s->cx), render_y(s->cy), b);
    if (lvl_of(f, d) >= V3D_OBJECTS && !bumper_moves(f, e)) {
        /* Walze: zweiter Ring als Fuss, dazu die beiden Mantellinien quer zur Verschiebung */
        float fx = s->cx, fy = s->cy;
        foot(&fx, &fy);
        const float dx = fx - s->cx, dy = fy - s->cy, l = sqrtf(dx * dx + dy * dy);
        gfx_occ_cutting(1);                                            /* Fussring und Mantel: verdeckt von der eigenen Deckflaeche und allem davor */
        circle_f(fx, fy, d->p[2], side_level(), ns);
        if (l * v_s > 12.0f) {
            const float nx = -dy / l * d->p[2], ny = dx / l * d->p[2];
            line_f(s->cx + nx, s->cy + ny, fx + nx, fy + ny, side_level());
            line_f(fx - nx, fy - ny, s->cx - nx, s->cy - ny, side_level());
        }
        gfx_occ_cutting(0);
    }
}

static void draw_drop(const vp_field_t *f, int e, const vp_elem_def_t *d, const vp_estate_t *s)
{
    const vp_table_def_t *t = f->def;
    float glow;
    const int b = at_least(elem_intensity(f, e, DEF_GREEN, &glow), 112);
    if (!b) return;
    for (int k = 0; k < d->count; k++) {                                /* die stehenden Ziele */
        if (s->mask & (1u << k)) continue;
        const vp_seg_t g = vp_seg(t, d->first + k);
        line_f(g.x1, g.y1, g.x2, g.y2, b);
        if (lvl_of(f, d) >= V3D_OBJECTS) body_seg(g.x2, g.y2, g.x1, g.y1, side_level(), 3);
    }
    if (glow <= 0.12f || !s->prev_hit) return;
    for (int k = 0; k < d->count; k++) {                                /* das eben getroffene Ziel: leuchtet voll auf, dann ist es weg */
        if (!(s->prev_hit & (1u << k)) || !(s->mask & (1u << k))) continue;
        const vp_seg_t g = vp_seg(t, d->first + k);
        gfx_color(HUE_FLASH);
        flash_edge(g.x1, g.y1, g.x2, g.y2, glow > 0.5f ? 4 : 2);
        flash_sides(g.x1, g.y1, g.x2, g.y2, 0.25f);
        gfx_color(v_hue);
        if (lvl_of(f, d) >= V3D_OBJECTS) body_seg(g.x2, g.y2, g.x1, g.y1, flash_level(side_level(), glow), 3);
    }
}

static void draw_roll(const vp_field_t *f, int e, const vp_elem_def_t *d, const vp_estate_t *s)
{
    int flash;
    if (!s->visible) return;
    v_floor = d->layer <= 0 && walls3d();
    const uint32_t group = vp_elem_color(f, e, DEF_GREEN, &flash);
    for (int k = 0; k < d->count; k++) {
        const vp_roll_t *r = &f->rolls[d->first + k];
        int b = col_intensity(r->has_color ? r->color : group);
        if (!b) continue;
        const float ru = r->r * v_s / 16.0f;
        float px = r->x, py = r->y;
        floor_pt(&px, &py);
        const int x = render_x(px), y = render_y(py);
        if (s->mask & (1u << k)) {                                     /* aktiv: im Original gefuellt -> hell, mit Kern */
            b = at_least(b, 118);
            if (ru < 2.2f) { gfx_dot(x, y, b); continue; }
            circle_f(r->x, r->y, r->r, b, 4);
            if (ru < 12.0f) gfx_dot(x, y, b);
            else circle_f(r->x, r->y, r->r * 0.55f, b, 4);
        } else {                                                       /* inaktiv: im Original nur Umriss -> dunkel */
            int bd = b * 45 / 100;
            if (bd < 10) bd = 10;
            /* Umriss auch klein, sonst sieht man die Lampe nicht (Olli, 2026-10-09); nur DETAIL LOW oder winzig: ein Punkt */
            if (ru < 2.2f || (ru < 6.0f && g_set.detail == DETAIL_LOW)) gfx_dot(x, y, bd);
            else circle_f(r->x, r->y, r->r, bd, ru < 6.0f ? -4 : 4);     /* klein: genau vier Striche */
        }
    }
}

static void draw_spinner(const vp_field_t *f, int e, const vp_elem_def_t *d, const vp_estate_t *s)
{
    float glow;
    int b;
    if (d->p[7] != 0) {
        /* Spinball: der Spinner ist immer zu sehen - in Ruhe ein Strich, beim Drehen klappt er zum Rechteck auf */
        const float a = d->p[7] - 10.0f, ux = fm_cosf(a) * d->p[2], uy = fm_sinf(a) * d->p[2];
        const float x = d->p[0], y = d->p[1];
        b = at_least(elem_intensity(f, e, DEF_SPINNER, &glow), 118);
        if (s->st == 1) {
            const float ox = -uy * 0.7f, oy = ux * 0.7f;
            line_f(x - ux - ox, y - uy - oy, x + ux - ox, y + uy - oy, b);
            line_f(x + ux - ox, y + uy - oy, x + ux + ox, y + uy + oy, b);
            line_f(x + ux + ox, y + uy + oy, x - ux + ox, y - uy + oy, b);
            line_f(x - ux + ox, y - uy + oy, x - ux - ox, y - uy - oy, b);
        } else line_f(x - ux, y - uy, x + ux, y + uy, b);
        return;
    }
    if (s->st != 1) return;
    b = at_least(elem_intensity(f, e, DEF_SPINNER, &glow), 118);
    circle_f(d->p[0], d->p[1], d->p[2], b, 4);
}

/* Flipper: volle Helligkeit und als schmaler Keil (drei Striche) statt als einzelner Strich; als Koerper: die helle
 * Deckflaeche liegt genau dort, wo die Physik den Finger hat (der Ball soll sichtbar an der hellen Kante abprallen), der
 * Fuss rueckt zur Bildmitte - je weiter aussen und je naeher die Kamera (Zoom), desto mehr. Physik: unveraendert. */
static void draw_flipper(const vp_field_t *f, int e, const vp_elem_def_t *d)
{
    float glow;
    const int b = at_least(elem_intensity(f, e, DEF_GREEN, &glow), 127);
    if (!b) return;
    if (lvl_of(f, d) < V3D_FLIPPERS) {                                 /* flach: drei Striche */
        float x1, y1, x2, y2;
        vp_flipper_line(f, e, &x1, &y1, &x2, &y2);
        const float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy);
        if (l < 1e-4f) return;
        const float ox = -dy / l * 0.24f, oy = dx / l * 0.24f;
        line_f(x1 + ox, y1 + oy, x2, y2, b);
        line_f(x2, y2, x1 - ox, y1 - oy, b);
        line_f(x1 - ox, y1 - oy, x1 + ox, y1 + oy, b);
        return;
    }
    int ux[4], uy[4], bx[4], by[4];
    const int side = at_least(84, 84);
    if (!flipper_body(f, e, ux, uy, bx, by)) return;
    for (int i = 0; i < 4; i++) gfx_line(ux[i], uy[i], ux[(i + 1) & 3], uy[(i + 1) & 3], b);      /* Deckflaeche */
    gfx_occ_cutting(1);                                                /* was unter der Deckflaeche oder hinter einer Seite liegt, faellt weg */
    for (int i = 0; i < 4; i++) {
        gfx_line(ux[i], uy[i], bx[i], by[i], side);                                                /* Kante nach unten */
        gfx_line(bx[i], by[i], bx[(i + 1) & 3], by[(i + 1) & 3], side);                            /* Fuss */
    }
    gfx_occ_cutting(0);
}

static void draw_elem(const vp_field_t *f, int e, int rev)
{
    const vp_table_def_t *t = f->def;
    const vp_elem_def_t *d = &t->elems[e];
    const vp_estate_t *s = &f->es[e];
    /* Ebenen unter dem Spielfeld (Tisch 8: die Billardkugeln) sind aufgemalt: flach, auf dem Boden */
    v_floor = d->layer < 0 && walls3d();
    v_off = d->layer > 0 && level_is_playfield(t, d->layer) && !vp_has_ball_at_layer(f, d->layer);
    u_cur = d->layer;
    switch (d->type) {
    case VP_WALL:    draw_wall(f, e, d, s, rev); break;
    case VP_PATH:    draw_path(f, e, d, s, rev); break;
    case VP_ARC:     draw_arc(f, e, d, s, rev); break;
    case VP_BUMPER:  draw_bumper(f, e, d, s); break;
    case VP_DROP:    draw_drop(f, e, d, s); break;
    case VP_ROLL:    draw_roll(f, e, d, s); break;
    case VP_SPINNER: draw_spinner(f, e, d, s); break;
    case VP_FLIPPER: draw_flipper(f, e, d); break;
    default: break;                                                    /* Sensoren sieht man nicht */
    }
}

/* Kleine Kreise unter den Formen (die Sterne von Tisch 7, im Original gefuellte Punkte): es sind hunderte, und der Kern
 * liefert sie nach Helligkeit geordnet - der Strahl spraenge kreuz und quer ueber die Karte. Darum werden sie in Spalten
 * abgefahren, abwechselnd hinauf und hinunter; die Spruenge sind dann so kurz wie der Abstand zum naechsten Stern. */
#define STAR_COLS 14
static struct { int16_t i; int16_t key; } s_star[VP_MAX_SHAPES];
static int star_cmp(const void *a, const void *b) { return ((const int16_t *)a)[1] - ((const int16_t *)b)[1]; }

static void draw_shapes(const vp_field_t *f)
{
    const float t = f->now_ms * 0.001f, small = 4.0f * 16.0f / v_s;   /* Radius unter vier Bildschirmeinheiten */
    int nstar = 0;
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < f->nshapes && i < VP_MAX_SHAPES; i++) {
        const vp_shape_t *s = &f->shapes[i];
        if ((s->kind != VS_DISC && s->kind != VS_CIRCLE) || s->v[2] >= small) continue;
        if (s->v[0] < x0) x0 = s->v[0];
        if (s->v[0] > x1) x1 = s->v[0];
        if (s->v[1] < y0) y0 = s->v[1];
        if (s->v[1] > y1) y1 = s->v[1];
        s_star[nstar++].i = (int16_t)i;
    }
    if (nstar > 1) {
        const float w = (x1 - x0) > 0.01f ? (x1 - x0) : 0.01f, h = (y1 - y0) > 0.01f ? (y1 - y0) : 0.01f;
        for (int k = 0; k < nstar; k++) {
            const vp_shape_t *s = &f->shapes[s_star[k].i];
            int col = (int)((s->v[0] - x0) / w * STAR_COLS), row = (int)((s->v[1] - y0) / h * 1000.0f);
            if (col >= STAR_COLS) col = STAR_COLS - 1;
            s_star[k].key = (int16_t)(col * 1024 + ((col & 1) ? 1000 - row : row));
        }
        qsort(s_star, (size_t)nstar, sizeof s_star[0], star_cmp);
    }
    for (int k = 0; k < nstar; k++) {
        const vp_shape_t *s = &f->shapes[s_star[k].i];
        int b = col_intensity(s->color);
        if (!b) continue;
        /* Sterne funkeln: jeder in seinem eigenen Takt. Der Ort (nicht die Nummer) bestimmt den Takt, damit ein Stern
         * beim Wandern der Karte nicht den Takt wechselt. */
        const float ph = s->v[0] * 12.9898f + s->v[1] * 78.233f;
        const float tw = 0.72f + 0.28f * fm_sinf(t * (2.6f + 1.9f * fm_sinf(ph)) + ph);
        const float ru = s->v[2] * v_s / 16.0f;
        if (ru < 1.3f) {
            float px = s->v[0], py = s->v[1];
            floor_pt(&px, &py);
            gfx_dot(render_x(px), render_y(py), (int)(b * tw));
        } else circle_f(s->v[0], s->v[1], s->v[2], (int)(b * tw), ru < 2.4f ? -4 : -6);   /* helle Sterne: eine kleine Scheibe */
    }
    for (int i = 0; i < f->nshapes; i++) {
        const vp_shape_t *s = &f->shapes[i];
        int b;
        if ((s->kind == VS_DISC || s->kind == VS_CIRCLE) && s->v[2] < small) continue;   /* schon gezeichnet */
        b = col_intensity(s->color);
        if (!b) continue;
        switch (s->kind) {
        case VS_LINE: line_f(s->v[0], s->v[1], s->v[2], s->v[3], b); break;
        case VS_DISC:
        case VS_CIRCLE: circle_f(s->v[0], s->v[1], s->v[2], b, 4); break;
        case VS_ARC: {
            float span = s->v[5] - s->v[4];
            int n = (int)ceilf(fabsf(span) / 0.3f);                      /* rund: alle 17 Grad eine Ecke (Vectrex: alle 52) */
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
    v_floor = 0;
    v_off = 0;
    u_on = 0;
    /* die Bahnen vom Einlauf auf die tieferen Finger (f->flip_step, REALISTIC) wirken nur in der Physik - gezeichnet sahen sie
     * nicht gut aus (Olli, 2026-10-09) */
    v_floor = walls3d();                                             /* Formen der Tische liegen auf dem Boden */
    draw_shapes(f);
    v_floor = 0;
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
    for (int k = 0; k < passes; k++) gfx_circle(render_x(x), render_y(y), r, b, 20, 0);
}

/* ---- Kugel ----
 * Auf der Vectrex reichte der Strahl fuer einen halben Rollring. Hier ist der Ball eine Kugel mit Gradnetz, die wirklich
 * rollt: jede Kugel traegt ihre Lage als Drehmatrix; je Bild dreht sie sich um die Achse quer zur Fahrt (Rollen ohne
 * Rutschen) und um die Hochachse mit dem Drall aus der Physik. Gezeichnet wird, was auf der oberen Halbkugel liegt:
 *   Aequator und zwei Laengenkreise (Farbe 2 des Balls)
 *   ein Breitenkreis um EINEN Pol (Farbe 1): er bricht die Gleichfoermigkeit - an ihm sieht man, wie herum sie rollt,
 *     auch wenn sie je Bild weit dreht
 *   ein Glanzlicht oben links, das stehen bleibt (weiss): die Kugel wirkt blank
 * Das sind rund 50 kurze Striche je Ball; ihre Weglaenge ist klein gegen den Tisch. */
static float s_rot[VP_MAX_BALLS + 1][9];                              /* der letzte Platz: der ruhende Ball auf dem Stoessel */

/* m = Drehung(Achse a, Winkel) * m; die Achse hat die Laenge 1 */
static void rot_turn(float *m, float ax, float ay, float az, float ang)
{
    float sn, c, o[9];
    fm_sincos(ang, &sn, &c);
    const float t = 1.0f - c;
    const float r[9] = { t * ax * ax + c,       t * ax * ay - sn * az, t * ax * az + sn * ay,
                         t * ax * ay + sn * az, t * ay * ay + c,       t * ay * az - sn * ax,
                         t * ax * az - sn * ay, t * ay * az + sn * ax, t * az * az + c };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) o[i * 3 + j] = r[i * 3] * m[j] + r[i * 3 + 1] * m[3 + j] + r[i * 3 + 2] * m[6 + j];
    memcpy(m, o, sizeof o);
}

/* Ruhelage: etwas gekippt, damit man das Gradnetz von schraeg oben sieht */
static void rot_rest(float *m)
{
    memset(m, 0, 9 * sizeof m[0]);
    m[0] = m[4] = m[8] = 1.0f;
    rot_turn(m, 1, 0, 0, 0.62f);
    rot_turn(m, 0, 1, 0, 0.38f);
}

/* Rundungsfehler sammeln sich ueber tausende Bilder: die Spalten wieder senkrecht aufeinander und auf Laenge 1 bringen */
static void rot_fix(float *m)
{
    float x[3] = { m[0], m[3], m[6] }, y[3] = { m[1], m[4], m[7] };
    float l = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    if (l < 1e-4f) { rot_rest(m); return; }
    for (int i = 0; i < 3; i++) x[i] /= l;
    const float d = x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
    for (int i = 0; i < 3; i++) y[i] -= d * x[i];
    l = sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
    if (l < 1e-4f) { rot_rest(m); return; }
    for (int i = 0; i < 3; i++) y[i] /= l;
    const float z[3] = { x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0] };
    for (int i = 0; i < 3; i++) { m[i * 3] = x[i]; m[i * 3 + 1] = y[i]; m[i * 3 + 2] = z[i]; }
}

/* Kreis auf der Kugel: axis 0/1/2 = seine Achse im Kugelsystem, lat = Breite (0 = Grosskreis). Nur der Teil auf der oberen
 * Halbkugel (zum Betrachter), am Rand sauber abgeschnitten. */
static void sphere_ring(const float *m, float x, float y, float r, int axis, float lat, int segs, int b)
{
    float sl, cl, px = 0, py = 0, pz = 0;
    fm_sincos(lat, &sl, &cl);
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    for (int k = 0; k <= segs; k++) {
        float sn, c, q[3];
        fm_sincos(k * (6.2831853f / segs), &sn, &c);
        q[u] = c * cl; q[v] = sn * cl; q[axis] = sl;
        const float wx = (m[0] * q[0] + m[1] * q[1] + m[2] * q[2]) * r, wy = (m[3] * q[0] + m[4] * q[1] + m[5] * q[2]) * r,
                    wz = m[6] * q[0] + m[7] * q[1] + m[8] * q[2];
        if (k && (pz >= 0 || wz >= 0)) {
            float ax = px, ay = py, bx = wx, by = wy;
            if (pz < 0) { const float t = pz / (pz - wz); ax = px + (wx - px) * t; ay = py + (wy - py) * t; }
            else if (wz < 0) { const float t = pz / (pz - wz); bx = px + (wx - px) * t; by = py + (wy - py) * t; }
            line_f(x + ax, y + ay, x + bx, y + by, b);
        }
        px = wx; py = wy; pz = wz;
    }
}

static void draw_sphere(const float *m, float x, float y, float radius, uint32_t color, uint32_t color2, int b)
{
    const float r = radius * BALL_DRAW;
    const int net = b * 3 / 4;
    set_hue(hue_of(color));
    draw_ball(x, y, radius, b);
    sphere_ring(m, x, y, r, 2, 0.95f, 12, b);                        /* Breitenkreis um den einen Pol */
    set_hue(hue_of(color2));
    for (int axis = 0; axis < 3; axis++) sphere_ring(m, x, y, r, axis, 0, 16, net);
    set_hue(HUE_FLASH);                                              /* Glanzlicht: kurzer Bogen oben links, dreht nicht mit */
    {
        float px = 0, py = 0;
        for (int k = 0; k <= 3; k++) {
            float sn, c;
            fm_sincos(1.75f + k * 0.28f, &sn, &c);
            const float qx = x + c * r * 0.74f, qy = y + sn * r * 0.74f;
            if (k) line_f(px, py, qx, qy, b);
            px = qx; py = qy;
        }
    }
}

/* Die Kugel i rollt mit ihrer Geschwindigkeit weiter und wird gezeichnet */
static void draw_ball_roll(int i, float x, float y, const vp_ball_t *ball, float radius, int b)
{
    float *m = s_rot[i];
    const float sp = sqrtf(ball->vx * ball->vx + ball->vy * ball->vy);
    if (m[0] == 0 && m[1] == 0 && m[2] == 0) rot_rest(m);
    if (sp > 0.05f) rot_turn(m, -ball->vy / sp, ball->vx / sp, 0, sp * v_dt / radius);   /* Achse quer zur Fahrt, in der Tischebene */
    if (ball->angvel > 0.05f || ball->angvel < -0.05f) rot_turn(m, 0, 0, 1, ball->angvel * v_dt);
    rot_fix(m);
    draw_sphere(m, x, y, radius, ball->color, ball->color2, b);
}

/* Stoessel von oben, wie er im Automaten sitzt: rote Gummikappe, auf der der Ball ruht, darunter der Bund und die
 * Schraubenfeder, die sich beim Ziehen zusammendrueckt und dabei von Stahlgrau nach Rot geht, die feste Lagerplatte, und
 * unter ihr Stange und Griff, die beim Ziehen herausfahren. (Auf der Vectrex: zwei Striche und ein Zickzack.) */
static void draw_plunger(const vp_table_def_t *t, float pull)
{
    const float x = t->launch_x;
    const float cap = t->launch_y - 0.7f * pull - t->ball_radius - 0.06f, neck = cap - 0.24f, collar = neck - 0.07f;
    const float plate = t->launch_y - t->ball_radius - 1.42f;
    const float grip = plate - 0.42f - 0.34f * pull;                  /* Mitte des Griffs */
    const int hi = at_least(118, 118), mid = at_least(96, 96), lo = at_least(70, 70);
    /* Kappe: abgerundet */
    set_hue(0xFF4028u);
    line_f(x - 0.36f, neck, x - 0.36f, cap - 0.09f, hi);
    line_f(x - 0.36f, cap - 0.09f, x - 0.26f, cap, hi);
    line_f(x - 0.26f, cap, x + 0.26f, cap, hi);
    line_f(x + 0.26f, cap, x + 0.36f, cap - 0.09f, hi);
    line_f(x + 0.36f, cap - 0.09f, x + 0.36f, neck, hi);
    line_f(x + 0.36f, neck, x - 0.36f, neck, hi);
    /* Bund */
    set_hue(0xD0D8E8u);
    line_f(x - 0.44f, collar, x + 0.44f, collar, mid);
    /* Feder: zehn Windungen zwischen Bund und Lagerplatte; gespannt wird sie rot */
    {
        const int turns = 10;
        const uint32_t r = (uint32_t)(184 + 71 * pull), g = (uint32_t)(192 - 120 * pull), bl = (uint32_t)(208 - 150 * pull);
        float px = x - 0.3f, py = collar;
        set_hue((r << 16) | (g << 8) | bl);
        for (int k = 1; k <= 2 * turns; k++) {
            const float nx = x + ((k & 1) ? 0.3f : -0.3f), ny = collar + (plate - collar) * (float)k / (float)(2 * turns);
            line_f(px, py, nx, ny, mid);
            px = nx; py = ny;
        }
    }
    /* Lagerplatte (fest), Stange und Griff */
    set_hue(0xD0D8E8u);
    line_f(x - 0.5f, plate, x + 0.5f, plate, hi);
    line_f(x + 0.5f, plate - 0.09f, x - 0.5f, plate - 0.09f, mid);
    line_f(x - 0.07f, plate - 0.09f, x - 0.07f, grip + 0.2f, lo);
    line_f(x + 0.07f, grip + 0.2f, x + 0.07f, plate - 0.09f, lo);
    set_hue(0xFF4028u);
    circle_f(x, grip, 0.24f, hi, 10);
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
        draw_ball_roll(i, x, y, &f->balls[i], f->def->ball_radius, b);
    }
    if (f->in_progress && f->nballs == 0 && f->nactions == 0 && !(f->lost_ms >= 0 && (int32_t)f->now_ms - f->lost_ms <= 750)) {
        /* kein Ball im Spiel: der naechste liegt in der Startbahn auf dem Stoessel bereit, bis ein Knopf ihn abschiesst */
        const vp_table_def_t *t = f->def;
        float *m = s_rot[VP_MAX_BALLS];
        rot_rest(m);
        draw_sphere(m, t->launch_x, t->launch_y - 0.7f * s_plunger, t->ball_radius, t->ball_color, t->ball_color2, b);   /* gezogen: Ball und Gummi tiefer */
        draw_plunger(t, s_plunger);
    } else if (f->in_progress) draw_plunger(f->def, 0);              /* der Stoessel gehoert zum Tisch: immer zu sehen */
}

/* Punktezeile als Segmentanzeige eines alten Flipperautomaten (segdisp.c): die Punkte orange in einem festen Feld, in
 * dem die dunklen Segmente mitglimmen, rechts Spieler, Multiplikator und Ball in Tuerkis, Meldungen ueber die ganze Breite. */
#define HUD_SCORE 0xFF7A20u
#define HUD_INFO  0x40FFD0u
void render_hud(const vp_field_t *f)
{
    char buf[48], who[8];
    const int cx = w_cx, half = w_half, wide = w_hud;                 /* Schriftgroesse nach dem Tisch, Lage nach dem Fenster */
    int y = w_top + Q4(6 + g_set.cal_hud_dy);
    int h = half * 36 / (int)(f->def->width * 10);                    /* Stellenhoehe: knapp zwei Feldeinheiten (ohne Zoom) */
    if (h < 150) h = 150;
    if (f->msg[0]) {
        int w = seg_width(f->msg, h);
        if (w > 2 * wide) h = h * 2 * wide / w;
        if (h < 110) h = 110;
        gfx_color(HUD_SCORE);
        seg_center(cx, y, f->msg, h, 124, 0);
        return;
    }
    {
        char info[48];
        info[0] = 0;
        if (f->in_progress) {
            who[0] = 0;
            if (f->nplayers > 1) snprintf(who, sizeof who, "P%d ", f->player + 1);  /* wer gerade dran ist */
            if (f->mult > 1.001) {
                int whole = (int)f->mult, frac = (int)lround((f->mult - whole) * 100);
                if (frac >= 100) { whole++; frac = 0; }
                if (frac) snprintf(info, sizeof info, "%sX%d.%02d BALL %d", who, whole, frac, f->ball_number);
                else snprintf(info, sizeof info, "%sX%d BALL %d", who, whole, f->ball_number);
            } else snprintf(info, sizeof info, "%sBALL %d", who, f->ball_number);
        }
        snprintf(buf, sizeof buf, "%9lld", (long long)f->score);
        {   /* beides muss nebeneinander passen: ist das Fenster schmal (ganzer Tisch), wird die Anzeige kleiner */
            const int need = seg_width(buf, h) + seg_width(info, h) + h;
            if (need > 2 * wide) h = h * 2 * wide / need;
        }
        gfx_color(HUD_SCORE);
        seg_draw(cx - wide, y, buf, h, 124, 18);
        if (info[0]) { gfx_color(HUD_INFO); seg_right(cx + wide, y, info, h, 118, 0); }
    }
}
