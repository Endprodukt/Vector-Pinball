/*
 * vp_phys.c - Ballphysik.
 *
 * Das Original rechnet mit Box2D. Hier steht ein kleiner eigener Loeser, der genau das nachbildet, was
 * der Tisch von Box2D braucht - mit denselben Stoffwerten:
 *
 *   Ball      Kreis, Masse pi/4 (Dichte so, dass sie vom Radius unabhaengig ist), Reibung 0.3,
 *             Rueckprall 0.6, "bullet" (kein Durchtunneln)
 *   Wand      Rechteck 0.1 dick -> hier Kapsel mit Radius 0.05; Reibung 0.2 (oder aus dem Tisch),
 *             Rueckprall 0 (oder aus dem Tisch)
 *   Bumper    fester Kreis, Reibung 0.3, Rueckprall 0.6
 *   Flipper   Rechteck 0.24 dick, Dichte 5, haengt an einem Drehgelenk mit Motor (Drehmoment <= 1000)
 *             und Anschlaegen -> hier Kapsel mit Radius 0.12, als echter Drehkoerper mit Traegheit:
 *             der Ball bremst den Flipper, der Motor holt nur mit begrenztem Moment auf
 *   Mischung  Reibung sqrt(a*b), Rueckprall max(a, b); unter 1 Einheit/s Aufprall kein Rueckprall
 *             (b2_velocityThreshold)
 *
 * Ablauf eines Schritts (wie Box2D: erst Geschwindigkeiten, dann Lagen):
 *   1. Schwerkraft auf die Baelle
 *   2. Flipper: Motor, Anschlaege und Ballkontakte gemeinsam loesen (sequentielle Impulse)
 *   3. Lagen fortschreiben
 *   4. Eindringen in Waende, Bumper, Flipper, andere Baelle aufloesen (Lage + Stoss)
 *   5. neue Beruehrungen als Ereignisse melden (das Original meldet sie beim Ende des Kontakts;
 *      fuer das Spiel ist nur wichtig, dass jede Beruehrung genau einmal zaehlt)
 */
#include "vp.h"
#include "fmath.h"
#include <math.h>
#include <string.h>

#define BALL_MASS      0.7853982f     /* pi * 0.5^2 * 1.0 */
#define BALL_FRICTION  0.3f
#define BALL_REST      0.6f
#define WALL_HALF      0.05f
#define FLIP_HALF      0.12f
#define FLIP_DENSITY   5.0f
#define FLIP_TORQUE    1000.0f
#define REST_THRESHOLD 1.0f
#define TOUCH_MARGIN   0.02f
#define MAX_MOVE       0.25f          /* laengster Weg je Teilschritt (kleiner als Ballradius + Wanddicke) */
#define SOLVE_ITERS    6
#define REST_FALLOFF   25.0f          /* bei dieser Aufprallgeschwindigkeit ist der Rueckprall halbiert (nur real_physics) */
static int g_real;                  /* 1 = Rueckprall mit Abfall (siehe vp_field_t.real_physics) */
/* Flipper in real_physics. Im Original dreht der Finger vom ersten Augenblick an mit voller Geschwindigkeit: der Ball wird
 * weggeschlagen, sobald er beruehrt wird, und fliegt immer zur Gegenseite. Ein echter Finger wird von der Spule
 * BESCHLEUNIGT: er nimmt den Ball mit und gibt ihn erst am Ende des Schlags frei - nahe am Drehpunkt in Richtung des
 * hochgestellten Fingers, also zur eigenen Seite (Rueckhand), nahe der Spitze frueher und zur Gegenseite. Dazu Gummi:
 * weniger Rueckprall, mehr Reibung - der Ball rutscht nicht am Finger entlang davon. */
#ifndef FLIP_TORQUE_REAL
/* Nach dem Vorbild von Visual Pinball X (hitflipper.cpp): der Finger ist ein Drehkoerper, den die Spule mit festem Moment
 * beschleunigt - er wird ueber den ganzen Schlag schneller statt sofort mit Nenndrehzahl zu laufen. Darum ist ein kurzer
 * Tipp ein kleiner Schubs (Tip Pass, Post Pass) und ein voller Schlag an der Spitze schwaecher als in der Mitte. Am oberen
 * Anschlag federt der Finger kurz zurueck (VPX: Drehimpuls mal -0,3). Das Gummi des Fingers: Rueckprall 0,8 (VPX), der mit
 * der Aufprallgeschwindigkeit abfaellt - ein langsamer Ball springt (Dead Bounce), ein harter Schlag bleibt wie abgestimmt. */
#define FLIP_TORQUE_REAL 1000.0f      /* Moment der Spule (wie im Original; dort laeuft der Finger aber sofort mit Nenndrehzahl) */
#define FLIP_RAMP      0.14f          /* s, in denen das Moment nach dem Druecken aufbaut (Spule: Induktivitaet; VPX "coil ramp up").
                                       * Ein kurzer Tipp bewegt den Finger darum nur wenig: Tip Pass. */
#define FLIP_W_REAL    1.25f          /* Nenndrehzahl des Schlags, Anteil des Originalwerts */
#define FLIP_BOUNCE    0.3f           /* Rueckfedern am oberen Anschlag */
#define FLIP_REST_REAL 0.80f          /* Gummi: Rueckprall bei langsamem Aufprall ... */
#define FLIP_REST_K    20.0f          /* ... halbiert bei dieser Aufprallgeschwindigkeit (e = 0,8 / (1 + v/20); VPX laesst ihn nur schwach abfallen) */
#define FLIP_REST_MIN  0.30f          /* darunter kein Rueckprall (Original: 1,0) */
#define FLIP_FRIC_REAL 0.60f
#define FLIP_EXTRA     0.14f          /* der Schlag geht 8 Grad hoeher als im Original (40 Grad sind fuer einen echten Tisch wenig) */
#endif

/* Rueckprall des Fingergummis fuer die Aufprallgeschwindigkeit vn (< 0) */
static float flip_rest(float vn)
{
    if (!g_real) return (vn < -REST_THRESHOLD) ? BALL_REST : 0.0f;
    if (vn > -FLIP_REST_MIN) return 0.0f;
    return FLIP_REST_REAL / (1.0f - vn / FLIP_REST_K);
}

/* Gelenkwinkel am oberen Anschlag. In real_physics hoeher, solange der Schlag nicht ohnehin gross ist (Spinball: 65 Grad). */
float vp_flipper_top(const vp_field_t *f, int e)
{
    const vp_elem_def_t *d = &f->def->elems[e];
    const int rev = d->p[2] < 0;
    float top = (rev ? -d->p[4] : d->p[4]) - (rev ? -d->p[3] : d->p[3]);
    if (f->real_physics && fabsf(top) < 0.8f) top += top < 0 ? -FLIP_EXTRA : FLIP_EXTRA;
    return top;
}

/* ---- Drehpunkt ----
 * REALISTIC: die Mulde. Auf einem echten Tisch endet die Einlaufwand ein gutes Stueck VOR dem Drehpunkt; der gehaltene Ball
 * liegt dann etwa eine Einheit weit draussen auf dem Finger, und ein Nachdruecken hebt ihn kraeftig an (Post Pass). In den
 * Tischen des Originals endet die Wand genau am Drehpunkt, der Ball rutscht bis an den Fuss (Tisch 1: 0,33 der Laenge) und
 * bekommt kaum Hub. Darum rueckt der Drehpunkt so weit nach unten, dass der Ball am hochgestellten Finger am Wandende
 * liegen bleibt, mindestens 0,75 Einheiten draussen (Olli, 2026-10-09). Mit nach unten rueckt das Ende der Wand auf der
 * AUSSENSEITE des Drehpunkts (der Rand des Auslaufs): der Ball aus dem Auslauf laeuft zwischen ihr und dem Fingerfuss in
 * den Abfluss, und mit dem tieferen Fuss allein klemmte er dort fest (gefunden mit tools/shottest). Das ist die einzige
 * Die Stufe zwischen dem Ende der Einlaufwand und dem tieferen Fuss schliesst ein kurzes Wandstueck (`flip_step`), wie bei
 * einem echten Tisch der Fuss des Fingers; vp_set_real_physics schaltet alles mit der Physik um. */
#define CRADLE_S    0.75f             /* so weit draussen (Einheiten vom Drehpunkt) soll der gehaltene Ball liegen */
#define CRADLE_DROP 0.80f             /* hoechstens so tief wird der Drehpunkt gesetzt */
#define STEP_JOIN   0.60f             /* die Bahn vom Wandende trifft den ruhenden Finger so weit vom Drehpunkt (Einheiten) */

void vp_flipper_pivot(const vp_field_t *f, int e, float *x, float *y)
{
    const vp_elem_def_t *d = &f->def->elems[e];
    *x = d->p[0]; *y = d->p[1];
    if (!f->real_physics) return;
    for (int k = 0; k < f->n_flip; k++) if (f->flip[k] == e) { *y -= f->flip_drop[k]; return; }
}

/* Lage s (Einheiten vom Drehpunkt) des Balls, der am hochgestellten Finger (Drehpunkt P, Richtung u zur Spitze, n nach oben)
 * herunterrollt und am Wandende G haengen bleibt; < 0 = er kommt am Wandende vorbei bis an den Fuss */
static float cradle_s(float px, float py, float ux, float uy, float nx, float ny, float gx, float gy, float rb)
{
    const float off = FLIP_HALF + rb, reach = rb + WALL_HALF;
    const float ax = px + off * nx - gx, ay = py + off * ny - gy;      /* C(s) = P + s u + off n; |C - G| = reach */
    const float b = ux * ax + uy * ay, c = ax * ax + ay * ay - reach * reach, disc = b * b - c;
    if (disc < 0) return -1;
    return -b + sqrtf(disc);                                 /* von aussen kommend: der aeussere Schnittpunkt */
}

static void cradle_build(vp_field_t *f)
{
    const vp_table_def_t *t = f->def;
    /* nur das untere Flipperpaar: obere Finger und Mini-Spielfelder haben andere Aufgaben, dort verschob die Absenkung
     * die Schusswinkel (Tisch 2, 7 - gemessen mit tools/shottest) */
    float low = 1e9f;
    for (int k = 0; k < f->n_flip; k++) if (t->elems[f->flip[k]].p[1] < low) low = t->elems[f->flip[k]].p[1];
    for (int k = 0; k < f->n_flip; k++) {
        const vp_elem_def_t *d = &t->elems[f->flip[k]];
        const float px = d->p[0], py = d->p[1];
        f->flip_drop[k] = 0; f->flip_apron[k] = -1; f->flip_apron_end[k] = 0;
        f->flip_step[k].x1 = f->flip_step[k].y1 = f->flip_step[k].x2 = f->flip_step[k].y2 = 0;
        if (py > low + 0.5f) continue;
        const int rev = d->p[2] < 0;
        float top = (rev ? -d->p[4] : d->p[4]) - (rev ? -d->p[3] : d->p[3]);
        if (fabsf(top) < 0.8f) top += top < 0 ? -FLIP_EXTRA : FLIP_EXTRA;   /* wie vp_flipper_top in REALISTIC */
        const float ang = (rev ? -d->p[3] : d->p[3]) + top;
        float ux = fm_cosf(ang), uy = fm_sinf(ang);
        if (rev) { ux = -ux; uy = -uy; }
        float nx = -uy, ny = ux;
        if (ny < 0) { nx = -nx; ny = -ny; }
        /* naechstes Wandende am Drehpunkt (die Einlaufwand) */
        float gx = 0, gy = 0, best = 0.6f * 0.6f;
        int found = 0;
        for (int e = 0; e < t->nelem; e++) {
            const vp_elem_def_t *w = &t->elems[e];
            if ((w->type != VP_WALL && w->type != VP_PATH) || w->layer != d->layer || (w->flags & VF_IGNORE_BALL)) continue;
            for (int i = 0; i < 2; i++) {
                const vp_seg_t g = vp_seg(t, w->first + (i ? w->count - 1 : 0));
                const float x = i ? g.x2 : g.x1, y = i ? g.y2 : g.y1, d2 = (x - px) * (x - px) + (y - py) * (y - py);
                if (d2 < best) { best = d2; gx = x; gy = y; found = 1; }
            }
        }
        if (!found) continue;
        for (float drop = 0; drop <= CRADLE_DROP + 1e-3f; drop += 0.05f)
            if (cradle_s(px, py - drop, ux, uy, nx, ny, gx, gy, t->ball_radius) >= CRADLE_S) { f->flip_drop[k] = drop; break; }
        if (f->flip_drop[k] <= 0) continue;
        /* die Wand am Rand des Auslaufs: naechstes Wandende auf der Aussenseite des Drehpunkts (ausser dem Einlaufende) */
        best = 1.6f * 1.6f;
        for (int e = 0; e < t->nelem; e++) {
            const vp_elem_def_t *w = &t->elems[e];
            if (w->type != VP_WALL || w->layer != d->layer || (w->flags & VF_IGNORE_BALL)) continue;
            for (int i = 0; i < 2; i++) {
                const vp_seg_t g = vp_seg(t, w->first);
                const float x = i ? g.x2 : g.x1, y = i ? g.y2 : g.y1, d2 = (x - px) * (x - px) + (y - py) * (y - py);
                if (x == gx && y == gy) continue;
                if (rev ? x <= px : x >= px) continue;                  /* innen: nicht der Auslauf */
                if (d2 < best) { best = d2; f->flip_apron[k] = (int16_t)e; f->flip_apron_end[k] = (uint8_t)i; }
            }
        }
        if (f->flip_apron[k] < 0) f->flip_drop[k] = 0;                  /* kein solches Wandende (Tisch 10): lieber nichts aendern */
        else {
            /* Bahn vom Wandende bis auf die Oberseite des RUHENDEN Fingers (bei STEP_JOIN Einheiten vom Drehpunkt): der Ball rollt aus
             * dem Einlauf durchgehend auf den Finger, und am gehobenen Finger stoppt ihn dieselbe Bahn in der Mulde */
            const float ad = rev ? -d->p[3] : d->p[3];
            float dx = fm_cosf(ad), dy = fm_sinf(ad);
            if (rev) { dx = -dx; dy = -dy; }
            float mx = -dy, my = dx;
            if (my < 0) { mx = -mx; my = -my; }
            f->flip_step[k].x1 = gx; f->flip_step[k].y1 = gy;
            f->flip_step[k].x2 = px + STEP_JOIN * dx + FLIP_HALF * mx;
            f->flip_step[k].y2 = py - f->flip_drop[k] + STEP_JOIN * dy + FLIP_HALF * my;
        }
    }
}

void vp_set_real_physics(vp_field_t *f, int on)
{
    f->real_physics = (uint8_t)(on != 0);
    f->n_step = on ? f->n_flip : 0;
    for (int k = 0; k < f->n_flip; k++) {
        if (f->flip_apron[k] < 0) continue;
        const int e = f->flip_apron[k];
        const vp_seg_t g = vp_seg(f->def, f->def->elems[e].first);     /* Lage im Tisch */
        const float dy = on ? f->flip_drop[k] : 0;
        if (f->flip_apron_end[k]) { f->es[e].seg.x2 = g.x2; f->es[e].seg.y2 = g.y2 - dy; }
        else                      { f->es[e].seg.x1 = g.x1; f->es[e].seg.y1 = g.y1 - dy; }
    }
}

/* ---- Raster ---- */
static int cell_of(float v, int n)
{
    int c = (int)floorf(v / VP_GRID_CELL);
    return c < 0 ? 0 : (c >= n ? n - 1 : c);
}

static int elem_solid(const vp_elem_def_t *d)
{
    if (d->type == VP_ARC || d->type == VP_PATH) return !(d->flags & VF_IGNORE_BALL);
    return d->type == VP_DROP;
}

void vp_phys_build(vp_field_t *f)
{
    const vp_table_def_t *t = f->def;
    static uint16_t count[VP_GRID_W * VP_GRID_H];
    const float m = 0.65f;                               /* groesster Ballradius + Wanddicke + Luft */
    f->n_flip = f->n_bump = f->n_wall = 0;
    for (int pass = 0; pass < 2; pass++) {
        memset(count, 0, sizeof count);
        for (int e = 0; e < t->nelem; e++) {
            const vp_elem_def_t *d = &t->elems[e];
            if (pass == 0) {
                if (d->type == VP_FLIPPER && f->n_flip < 8) f->flip[f->n_flip++] = (uint8_t)e;
                if (d->type == VP_BUMPER && d->p[2] > 0 && f->n_bump < 16) f->bump[f->n_bump++] = (uint8_t)e;
                if (d->type == VP_WALL && !(d->flags & VF_IGNORE_BALL) && f->n_wall < 80) f->walls[f->n_wall++] = (uint8_t)e;
            }
            if (!elem_solid(d)) continue;
            for (int k = 0; k < d->count; k++) {
                const vp_seg_t sg = vp_seg(t, d->first + k), *s = &sg;
                int x0 = cell_of(fminf(s->x1, s->x2) - m, VP_GRID_W), x1 = cell_of(fmaxf(s->x1, s->x2) + m, VP_GRID_W);
                int y0 = cell_of(fminf(s->y1, s->y2) - m, VP_GRID_H), y1 = cell_of(fmaxf(s->y1, s->y2) + m, VP_GRID_H);
                for (int cy = y0; cy <= y1; cy++)
                    for (int cx = x0; cx <= x1; cx++) {
                        int c = cy * VP_GRID_W + cx;
                        if (pass == 1) {
                            int at = f->cell_start[c] + count[c];
                            if (at < VP_GRID_ITEMS && at < f->cell_start[c + 1]) {
                                f->cell_seg[at] = (uint16_t)(d->first + k);
                                f->cell_elem[at] = (uint8_t)e;
                            }
                        }
                        count[c]++;
                    }
            }
        }
        if (pass == 0) {
            cradle_build(f);
            unsigned sum = 0;
            for (int c = 0; c < VP_GRID_W * VP_GRID_H; c++) {
                f->cell_start[c] = (uint16_t)(sum > VP_GRID_ITEMS ? VP_GRID_ITEMS : sum);
                sum += count[c];
            }
            f->cell_start[VP_GRID_W * VP_GRID_H] = (uint16_t)(sum > VP_GRID_ITEMS ? VP_GRID_ITEMS : sum);
        }
    }
}

/* ---- Flipper ---- */
typedef struct {
    int e;
    float cx, cy, len, down, lo, hi, inertia;
    float x1, y1, x2, y2;            /* Kapselachse in der aktuellen Lage */
} flip_t;

static void flip_geom(const vp_field_t *f, int e, flip_t *g)
{
    const vp_elem_def_t *d = &f->def->elems[e];
    float len = d->p[2];
    int rev = len < 0;
    float down = rev ? -d->p[3] : d->p[3], up = rev ? -d->p[4] : d->p[4];
    vp_flipper_pivot(f, e, &g->cx, &g->cy);
    g->e = e; g->len = len; g->down = down;
    (void)up;
    if (rev) { g->lo = vp_flipper_top(f, e); g->hi = 0; } else { g->lo = 0; g->hi = vp_flipper_top(f, e); }
    {   /* Traegheit des Rechtecks um den Drehpunkt (Box2D: Dichte 5, Laenge |L| + 0.05, Dicke 0.24) */
        float bl = fabsf(len) + 0.05f, th = 2 * FLIP_HALF;
        float mass = FLIP_DENSITY * bl * th;
        float dc = fabsf(len) * 0.5f - 0.025f;
        g->inertia = mass * (bl * bl + th * th) / 12.0f + mass * dc * dc;
    }
    float ang = down + f->es[e].a, c = fm_cosf(ang), s = fm_sinf(ang);
    float a0 = rev ? -(FLIP_HALF - 0.05f) : (FLIP_HALF - 0.05f);
    float a1 = len - (rev ? -FLIP_HALF : FLIP_HALF);
    g->x1 = g->cx + a0 * c; g->y1 = g->cy + a0 * s;
    g->x2 = g->cx + a1 * c; g->y2 = g->cy + a1 * s;
}

void vp_flipper_line(const vp_field_t *f, int e, float *x1, float *y1, float *x2, float *y2)
{
    const vp_elem_def_t *d = &f->def->elems[e];
    int rev = d->p[2] < 0;
    float down = rev ? -d->p[3] : d->p[3];
    float ang = down + f->es[e].a, px, py;
    vp_flipper_pivot(f, e, &px, &py);
    *x1 = px; *y1 = py;
    *x2 = px + d->p[2] * fm_cosf(ang); *y2 = py + d->p[2] * fm_sinf(ang);
}

/* ---- Abstand Punkt - Strecke ---- */
typedef struct { float nx, ny, dist, qx, qy; } hit_t;

static int seg_hit(float px, float py, float x1, float y1, float x2, float y2, float reach, float vx, float vy, hit_t *h)
{
    float minx = x1 < x2 ? x1 : x2, maxx = x1 < x2 ? x2 : x1;
    if (px < minx - reach || px > maxx + reach) return 0;
    float miny = y1 < y2 ? y1 : y2, maxy = y1 < y2 ? y2 : y1;
    if (py < miny - reach || py > maxy + reach) return 0;
    float dx = x2 - x1, dy = y2 - y1;
    float l2 = dx * dx + dy * dy;
    float t = l2 > 1e-12f ? ((px - x1) * dx + (py - y1) * dy) / l2 : 0.0f;
    if (t < 0) t = 0; else if (t > 1) t = 1;
    float qx = x1 + t * dx, qy = y1 + t * dy;
    float ex = px - qx, ey = py - qy;
    float d2 = ex * ex + ey * ey;
    if (d2 >= reach * reach) return 0;
    float d = sqrtf(d2);
    if (d > 1e-6f) { h->nx = ex / d; h->ny = ey / d; }
    else {                                                  /* genau auf der Linie: Normale gegen die Bewegung */
        float l = sqrtf(l2);
        if (l < 1e-6f) { h->nx = 0; h->ny = 1; }
        else {
            h->nx = -dy / l; h->ny = dx / l;
            if (h->nx * vx + h->ny * vy > 0) { h->nx = -h->nx; h->ny = -h->ny; }
        }
    }
    h->dist = d; h->qx = qx; h->qy = qy;
    return 1;
}

/* Stoss gegen einen festen Koerper: Normale n zeigt vom Koerper zum Ball */

/* post = 1: Ecke oder Ende einer Wand. In real_physics ist das ein Gummipfosten: er federt auch einen langsamen Ball zurueck
 * (das Original laesst Stoesse unter 1 Einheit/s tot liegen) - ohne das gibt es keinen Post Pass. */
#ifndef POST_REST
#define POST_REST      0.80f
#define POST_THRESHOLD 0.30f
#endif
static void bounce_static(vp_ball_t *b, float rb, float nx, float ny, float rest, float fric, int post)
{
    /* Geschwindigkeit des Beruehrpunkts auf dem Ball: v + w x (-n * rb) */
    float cvx = b->vx + b->angvel * ny * rb, cvy = b->vy - b->angvel * nx * rb;
    float vn = cvx * nx + cvy * ny;
    if (vn >= 0) return;
    if (g_real && post && rest < POST_REST) rest = POST_REST;
    float e = (-vn > (g_real && post ? POST_THRESHOLD : REST_THRESHOLD)) ? rest : 0.0f;
    if (g_real) e /= 1.0f - vn / REST_FALLOFF;              /* echtes Gummi: je haerter der Aufprall, desto kleiner der Rueckprall */
    float jn = -(1.0f + e) * vn;                            /* je Masseneinheit */
    b->vx += jn * nx; b->vy += jn * ny;
    /* Reibung: Tangente t = (-ny, nx); wirksame Masse mit Drall der Scheibe: 1/m + rb^2/I = 3/m */
    float tx = -ny, ty = nx;
    float vt = cvx * tx + cvy * ty;
    float jt = -vt / 3.0f;
    float lim = fric * jn;
    if (jt > lim) jt = lim; else if (jt < -lim) jt = -lim;
    b->vx += jt * tx; b->vy += jt * ty;
    b->angvel -= 2.0f * jt / rb;                            /* I = m rb^2 / 2, Hebel (-n rb) x t = -rb */
}

typedef struct { uint16_t key[VP_MAX_TOUCH]; uint8_t e[VP_MAX_TOUCH], sub[VP_MAX_TOUCH]; int n; } touch_t;

static void event_add(vp_field_t *f, const vp_ball_t *b, int e, int sub)
{
    if (f->nevents < VP_MAX_EVENTS) {
        vp_event_t *ev = &f->events[f->nevents++];
        ev->serial = b->serial; ev->e = (uint8_t)e; ev->sub = (uint8_t)sub;
    }
}

/* Beruehrung merken. Gemeldet wird sie wie im Original erst, wenn sie ENDET (der Ball ist dann schon abgeprallt): ein Kicker oder
 * Ballretter gibt seinen Stoss also auf den zurueckspringenden Ball und nicht auf den noch anfliegenden. Nur die Wand, die den Ball
 * aus dem Spiel nimmt, meldet sofort - ein langsamer Ball bliebe sonst auf ihr liegen. */
static void touch_add(vp_field_t *f, vp_ball_t *b, touch_t *now, uint16_t key, int e, int sub)
{
    for (int i = 0; i < now->n; i++) if (now->key[i] == key) return;
    if (now->n < VP_MAX_TOUCH) { now->key[now->n] = key; now->e[now->n] = (uint8_t)e; now->sub[now->n] = (uint8_t)sub; now->n++; }
    if (f->def->elems[e].type == VP_WALL && (f->def->elems[e].flags & VF_KILL)) {
        for (int i = 0; i < b->ntouch; i++) if (b->touch[i] == key) return;
        event_add(f, b, e, sub);
    }
}

/* Ball gegen eine Strecke eines Elements: Lage und Stoss. Liefert 1 bei Beruehrung. */
static int collide_seg(vp_field_t *f, vp_ball_t *b, float rb, const vp_seg_t *s, float rest_w, float fric_w)
{
    hit_t h;
    float reach = rb + WALL_HALF;
    if (!seg_hit(b->x, b->y, s->x1, s->y1, s->x2, s->y2, reach + TOUCH_MARGIN, b->vx, b->vy, &h)) return 0;
    (void)f;
    if (h.dist < reach) {
        float pen = reach - h.dist;
        b->x += h.nx * pen; b->y += h.ny * pen;
        const int post = (h.qx == s->x1 && h.qy == s->y1) || (h.qx == s->x2 && h.qy == s->y2);
        bounce_static(b, rb, h.nx, h.ny, rest_w > BALL_REST ? rest_w : BALL_REST, sqrtf(BALL_FRICTION * fric_w), post);
    }
    return 1;
}

/* Kontakt Ball - Flipper fuer den Geschwindigkeitsloeser */
typedef struct {
    vp_ball_t *b; flip_t *g;
    float nx, ny, rx, ry;            /* Normale (Flipper -> Ball), Hebel vom Drehpunkt zum Beruehrpunkt */
    float bias, accn, acct;
} fcon_t;

static void flip_contact_vel(const fcon_t *c, float rb, float w, float *vn, float *vt)
{
    const vp_ball_t *b = c->b;
    float cvx = b->vx + b->angvel * c->ny * rb, cvy = b->vy - b->angvel * c->nx * rb;
    float fvx = -w * c->ry, fvy = w * c->rx;
    float rvx = cvx - fvx, rvy = cvy - fvy;
    *vn = rvx * c->nx + rvy * c->ny;
    *vt = rvx * -c->ny + rvy * c->nx;
}

static void solve_flip_contact(vp_field_t *f, fcon_t *c, float rb)
{
    vp_estate_t *s = &f->es[c->g->e];
    float inv_m = 1.0f / BALL_MASS, inv_i = 1.0f / c->g->inertia;
    float vn, vt;
    /* Normale */
    flip_contact_vel(c, rb, s->w, &vn, &vt);
    float rn = c->rx * c->ny - c->ry * c->nx;               /* r x n */
    float kn = inv_m + rn * rn * inv_i;
    float dl = -(vn - c->bias) / kn;
    float old = c->accn;
    c->accn = old + dl > 0 ? old + dl : 0;
    dl = c->accn - old;
    c->b->vx += dl * c->nx * inv_m; c->b->vy += dl * c->ny * inv_m;
    s->w -= rn * dl * inv_i;
    /* Reibung */
    flip_contact_vel(c, rb, s->w, &vn, &vt);
    float tx = -c->ny, ty = c->nx;
    float rt = c->rx * ty - c->ry * tx;                     /* r x t */
    float kt = 3.0f * inv_m + rt * rt * inv_i;
    float dt = -vt / kt;
    float lim = (g_real ? FLIP_FRIC_REAL : sqrtf(BALL_FRICTION * 0.2f)) * c->accn;
    old = c->acct;
    float na = old + dt;
    if (na > lim) na = lim; else if (na < -lim) na = -lim;
    c->acct = na; dt = na - old;
    c->b->vx += dt * tx * inv_m; c->b->vy += dt * ty * inv_m;
    c->b->angvel -= 2.0f * dt / (BALL_MASS * rb);           /* I_ball = m rb^2 / 2, Hebel -rb */
    s->w -= rt * dt * inv_i;
}

static void one_step(vp_field_t *f, float h)
{
    const vp_table_def_t *t = f->def;
    const float rb = t->ball_radius;
    flip_t fg[8];
    static fcon_t con[VP_MAX_BALLS * 8];      /* statisch: der Kern 0 hat auf dem UVMC2 nur 4 KB Stapel (Doku des Kits, 02) */
    int ncon = 0;

    /* 1. Schwerkraft */
    const float grav = t->gravity * vp_slope(f);
    for (int i = 0; i < f->nballs; i++) f->balls[i].vy -= grav * f->balls[i].gscale * h;

    /* 2. Flipper: Motor, Anschlaege, Ballkontakte */
    for (int k = 0; k < f->n_flip; k++) {
        flip_geom(f, f->flip[k], &fg[k]);
        f->es[f->flip[k]].macc = 0; f->es[f->flip[k]].t = 0;
        f->es[f->flip[k]].cy += h;                                  /* Zeit seit dem Druecken (vp_set_flippers setzt sie zurueck) */
        for (int i = 0; i < f->nballs; i++) {
            vp_ball_t *b = &f->balls[i];
            hit_t hh;
            if (b->layer != t->elems[f->flip[k]].layer) continue;
            if (!seg_hit(b->x, b->y, fg[k].x1, fg[k].y1, fg[k].x2, fg[k].y2, rb + FLIP_HALF + TOUCH_MARGIN, b->vx, b->vy, &hh)) continue;
            fcon_t *c = &con[ncon++];
            c->b = b; c->g = &fg[k]; c->nx = hh.nx; c->ny = hh.ny;
            c->rx = hh.qx + hh.nx * FLIP_HALF - fg[k].cx; c->ry = hh.qy + hh.ny * FLIP_HALF - fg[k].cy;
            c->accn = c->acct = 0;
            float vn, vt;
            flip_contact_vel(c, rb, f->es[f->flip[k]].w, &vn, &vt);
            c->bias = -flip_rest(vn) * vn;
        }
    }
    for (int it = 0; it < SOLVE_ITERS; it++) {
        for (int k = 0; k < f->n_flip; k++) {
            vp_estate_t *s = &f->es[fg[k].e];
            float maxi = (g_real ? FLIP_TORQUE_REAL : FLIP_TORQUE) * h;
            float target = s->motor;
            if (g_real) {
                /* Schlag nach oben: Nenndrehzahl hoeher, erreicht wird sie erst durch die Beschleunigung mit begrenztem Moment,
                 * und das Moment baut sich nach dem Druecken erst auf */
                const int rev = t->elems[fg[k].e].p[2] < 0, up = rev ? target < -0.6f : target > 0.6f;
                if (up) {
                    target *= FLIP_W_REAL;
                    if (s->cy < FLIP_RAMP) maxi *= s->cy / FLIP_RAMP;
                }
            }
            float imp = fg[k].inertia * (target - s->w);
            float old = s->macc, na = old + imp;
            if (na > maxi) na = maxi; else if (na < -maxi) na = -maxi;
            s->macc = na;
            s->w += (na - old) / fg[k].inertia;
            /* Anschlaege mit eigenem aufsummiertem Impuls (s->t): drueckt der Motor in den Anschlag, heben sich beide auf; kommt ein Stoss
             * dagegen, gibt der Anschlag nach und das volle Motormoment haelt dagegen - wie die Gelenkgrenze in Box2D */
            if (s->a >= fg[k].hi - 1e-4f) {
                float nl = s->t - fg[k].inertia * s->w;
                if (nl > 0) nl = 0;
                s->w += (nl - s->t) / fg[k].inertia; s->t = nl;
            } else if (s->a <= fg[k].lo + 1e-4f) {
                float nl = s->t - fg[k].inertia * s->w;
                if (nl < 0) nl = 0;
                s->w += (nl - s->t) / fg[k].inertia; s->t = nl;
            }
        }
        for (int c = 0; c < ncon; c++) solve_flip_contact(f, &con[c], rb);
    }

    /* 3. Lagen */
    for (int k = 0; k < f->n_flip; k++) {
        vp_estate_t *s = &f->es[fg[k].e];
        s->a += s->w * h;
        if (s->a < fg[k].lo) { s->a = fg[k].lo; if (s->w < 0) s->w = 0; }
        if (s->a > fg[k].hi) { s->a = fg[k].hi; if (s->w > 0) s->w = g_real ? -FLIP_BOUNCE * s->w : 0; }   /* am Anschlag federt der Finger zurueck */
        flip_geom(f, fg[k].e, &fg[k]);
    }
    for (int i = 0; i < f->nballs; i++) {
        vp_ball_t *b = &f->balls[i];
        b->x += b->vx * h; b->y += b->vy * h; b->ang += b->angvel * h;
    }

    /* 4. Eindringen aufloesen, 5. Beruehrungen melden */
    for (int i = 0; i < f->nballs; i++) {
        vp_ball_t *b = &f->balls[i];
        touch_t now;
        now.n = 0;
        for (int pass = 0; pass < 2; pass++) {
            int c = cell_of(b->y, VP_GRID_H) * VP_GRID_W + cell_of(b->x, VP_GRID_W);
            for (int k = f->cell_start[c]; k < f->cell_start[c + 1]; k++) {
                int e = f->cell_elem[k];
                const vp_elem_def_t *d = &t->elems[e];
                if (d->layer != b->layer) continue;
                int sub = f->cell_seg[k] - d->first;
                float rest, fric;
                if (d->type == VP_DROP) {
                    if (f->es[e].mask & (1u << sub)) continue;          /* schon versenkt */
                    rest = 0; fric = 0.2f;
                } else { rest = d->p[0]; fric = d->p[1]; }
                const vp_seg_t sg = vp_seg(t, f->cell_seg[k]);
                if (collide_seg(f, b, rb, &sg, rest, fric))
                    touch_add(f, b, &now, f->cell_seg[k], e, sub);
            }
            for (int k = 0; k < f->n_wall; k++) {
                int e = f->walls[k];
                const vp_elem_def_t *d = &t->elems[e];
                if (d->layer != b->layer || f->es[e].retracted) continue;
                if (collide_seg(f, b, rb, &f->es[e].seg, d->p[1], d->p[2]))
                    touch_add(f, b, &now, d->first, e, 0);
            }
            for (int k = 0; k < f->n_step; k++) {                        /* Wandstuecke an den tieferen Fingern (REALISTIC) */
                if (f->flip_drop[k] <= 0 || t->elems[f->flip[k]].layer != b->layer) continue;
                if (collide_seg(f, b, rb, &f->flip_step[k], 0, 0.2f))
                    touch_add(f, b, &now, (uint16_t)(0xC000 | k), f->flip[k], 0);
            }
            for (int k = 0; k < f->n_bump; k++) {
                int e = f->bump[k];
                const vp_elem_def_t *d = &t->elems[e];
                if (d->layer != b->layer) continue;
                float dx = b->x - f->es[e].cx, dy = b->y - f->es[e].cy;
                float reach = rb + d->p[2];
                float d2 = dx * dx + dy * dy;
                if (d2 >= (reach + TOUCH_MARGIN) * (reach + TOUCH_MARGIN)) continue;
                float dist = sqrtf(d2);
                float nx = dist > 1e-6f ? dx / dist : 0.0f, ny = dist > 1e-6f ? dy / dist : 1.0f;
                if (dist < reach) {
                    b->x += nx * (reach - dist); b->y += ny * (reach - dist);
                    bounce_static(b, rb, nx, ny, BALL_REST, BALL_FRICTION, 0);
                }
                touch_add(f, b, &now, (uint16_t)(0x8000 | e), e, 0);
            }
            for (int k = 0; k < f->n_flip; k++) {
                hit_t hh;
                const vp_elem_def_t *d = &t->elems[fg[k].e];
                float reach = rb + FLIP_HALF;
                if (d->layer != b->layer) continue;
                if (!seg_hit(b->x, b->y, fg[k].x1, fg[k].y1, fg[k].x2, fg[k].y2, reach + TOUCH_MARGIN, b->vx, b->vy, &hh)) continue;
                if (hh.dist < reach) {
                    b->x += hh.nx * (reach - hh.dist); b->y += hh.ny * (reach - hh.dist);
                    fcon_t c;
                    float vn, vt;
                    c.b = b; c.g = &fg[k]; c.nx = hh.nx; c.ny = hh.ny;
                    c.rx = hh.qx + hh.nx * FLIP_HALF - fg[k].cx; c.ry = hh.qy + hh.ny * FLIP_HALF - fg[k].cy;
                    c.accn = c.acct = 0;
                    flip_contact_vel(&c, rb, f->es[fg[k].e].w, &vn, &vt);
                    if (vn < 0) {
                        c.bias = -flip_rest(vn) * vn;
                        solve_flip_contact(f, &c, rb);
                    }
                }
                touch_add(f, b, &now, (uint16_t)(0x4000 | fg[k].e), fg[k].e, 0);
            }
        }
        /* Ball gegen Ball (gleiche Masse) */
        for (int j = i + 1; j < f->nballs; j++) {
            vp_ball_t *o = &f->balls[j];
            if (o->layer != b->layer) continue;
            float dx = o->x - b->x, dy = o->y - b->y;
            float d2 = dx * dx + dy * dy, reach = 2 * rb;
            if (d2 >= reach * reach || d2 < 1e-12f) continue;
            float dist = sqrtf(d2), nx = dx / dist, ny = dy / dist, pen = (reach - dist) * 0.5f;
            b->x -= nx * pen; b->y -= ny * pen; o->x += nx * pen; o->y += ny * pen;
            float vn = (o->vx - b->vx) * nx + (o->vy - b->vy) * ny;
            if (vn < 0) {
                float e = (-vn > REST_THRESHOLD) ? BALL_REST : 0.0f;
                float j2 = -(1.0f + e) * vn * 0.5f;
                b->vx -= j2 * nx; b->vy -= j2 * ny; o->vx += j2 * nx; o->vy += j2 * ny;
            }
        }
        for (int k = 0; k < b->ntouch; k++) {                       /* beendete Beruehrungen melden */
            int still = 0;
            for (int m = 0; m < now.n; m++) if (now.key[m] == b->touch[k]) still = 1;
            const vp_elem_def_t *d = &t->elems[b->touch_e[k]];
            if (!still && !(d->type == VP_WALL && (d->flags & VF_KILL))) event_add(f, b, b->touch_e[k], b->touch_sub[k]);
        }
        b->ntouch = (uint8_t)now.n;
        memcpy(b->touch, now.key, sizeof now.key);
        memcpy(b->touch_e, now.e, sizeof now.e);
        memcpy(b->touch_sub, now.sub, sizeof now.sub);
    }
}

void vp_phys_step(vp_field_t *f, float h)
{
    /* Schnelle Baelle: Schritt teilen, damit keiner durch eine Wand springt */
    float vmax = 0;
    for (int i = 0; i < f->nballs; i++) {
        float v = fabsf(f->balls[i].vx) > fabsf(f->balls[i].vy) ? fabsf(f->balls[i].vx) : fabsf(f->balls[i].vy);
        if (v > vmax) vmax = v;
    }
    g_real = f->real_physics;
    int n = (int)(vmax * h / MAX_MOVE) + 1;
    if (n > 8) n = 8;
    for (int k = 0; k < n; k++) one_step(f, h / n);

    /* Ereignisse abarbeiten (ein Ball kann dabei verschwinden) */
    int ne = f->nevents;
    vp_event_t ev[VP_MAX_EVENTS];
    memcpy(ev, f->events, sizeof(vp_event_t) * (size_t)ne);
    f->nevents = 0;
    for (int i = 0; i < ne; i++) {
        vp_ball_t *b = vp_ball_by_serial(f, ev[i].serial);
        if (b) vp_on_collision(f, b, ev[i].e, ev[i].sub);
    }
}
