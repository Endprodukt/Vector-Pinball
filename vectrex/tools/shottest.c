/*
 * shottest.c - sind alle Schuesse eines Tisches zu schaffen?
 *
 * Ein Automat spielt den Tisch viele Stunden Spielzeit und fuehrt Buch, welches Ziel wie oft getroffen wurde: jeder Bumper,
 * jedes Fallziel, jeder Rollover, jeder Sensor (Rampen, Bahnen), jeder Spinner, jede Schleuder. Anders als der Automat des
 * Dauerlaufs (soak.c) zielt dieser: je Ballkontakt waehlt er, an welcher Stelle des Flippers geschlagen wird (0 = Drehpunkt,
 * 1 = Spitze - das bestimmt die Schussrichtung), und in einem Teil der Faelle faengt er den Ball erst auf dem gehaltenen
 * Flipper, laesst ihn herunterrollen und schlaegt dann - der kontrollierte Schuss eines guten Spielers.
 *
 *   tools/shottest.exe <tisch 1..10> [flipper %] [neigung %] [stunden] [real 0|1] [seed] [stoessel %]
 *   (Flipper und Neigung wie im Pausenbild: Prozent AUF die feste Abstimmung des Tisches, 100 = wie ausgeliefert)
 *
 * SHOT_MAP=1: statt Dauerspiel die Schusskarte (gezielte Schuesse von jedem Flipper, siehe shot_map).
 * Ausgabe: je Ziel die Treffer je Stunde Spielzeit; "NIE" = in der ganzen Zeit nicht getroffen. SHOT_SUM=1: nur die
 * Zusammenfassung (eine Zeile), fuer Reihenuntersuchungen.
 */
#include "vp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static vp_field_t F;
static const vp_delegate_t *orig;
static vp_delegate_t wrap;

#define MAX_T 512
static struct { int16_t e; int8_t sub; char kind; long hits; } tg[MAX_T];
static int ntg;
static int16_t tg_of[VP_MAX_ELEMS][33];          /* Element, Teil (32 = ohne Teil) -> Ziel */

static void hit(int e, int sub)
{
    if (e < 0 || e >= VP_MAX_ELEMS) return;
    if (sub < 0 || sub > 32) sub = 32;
    int k = tg_of[e][sub];
    if (k < 0 && sub != 32) k = tg_of[e][32];
    if (k >= 0) tg[k].hits++;
}

static void w_collision(vp_field_t *f, int e, int sub, vp_ball_t *b) { hit(e, sub); if (orig->collision) orig->collision(f, e, sub, b); }
static void w_spinner(vp_field_t *f, int e, vp_ball_t *b) { hit(e, 32); if (orig->spinner) orig->spinner(f, e, b); }
static void w_sensor(vp_field_t *f, int e, vp_ball_t *b)
{
    if (b->last_sensor != e) hit(e, 32);          /* ein Durchgang zaehlt einmal, nicht je Takt */
    if (orig->sensor) orig->sensor(f, e, b);
}

static void add_target(int e, int sub, char kind)
{
    if (ntg >= MAX_T) return;
    tg[ntg].e = (int16_t)e; tg[ntg].sub = (int8_t)sub; tg[ntg].kind = kind; tg[ntg].hits = 0;
    tg_of[e][sub < 0 ? 32 : sub] = (int16_t)ntg++;
}

static void collect_targets(void)
{
    const vp_table_def_t *t = F.def;
    ntg = 0;
    memset(tg_of, 0xFF, sizeof tg_of);
    for (int e = 0; e < t->nelem; e++) {
        const vp_elem_def_t *d = &t->elems[e];
        switch (d->type) {
        case VP_WALL:    if (d->p[0] > 0.01f || (d->flags & VF_RETRACT_HIT)) add_target(e, -1, 'K'); break;
        case VP_BUMPER:  if (d->p[2] > 0) add_target(e, -1, 'B'); break;
        case VP_DROP:    for (int k = 0; k < d->count && k < 32; k++) add_target(e, k, 'D'); break;
        case VP_ROLL:    if (d->flags & VF_IGNORE_BALL) break;          /* nur Anzeige */
                         for (int k = 0; k < d->count && k < 32; k++) add_target(e, k, 'R'); break;
        case VP_SENSOR:  add_target(e, -1, 'S'); break;
        case VP_SPINNER: add_target(e, -1, 'P'); break;
        default: break;
        }
    }
}

/* ---- der Spieler ---- */
typedef struct { int mode; float frac, t; int16_t serial; } ai_t;   /* mode 0 frei, 1 Schuss, 2 fangen, 3 gefangen/rollt, 4 Schlag laeuft */
static ai_t ai[8];
static float rest[8][4];                          /* Ruhelage je Flipper: Drehpunkt, Spitze */

static float frand(void) { return vp_randf(&F); }

static int flipper_wants(int k, float dt)
{
    const int e = F.flip[k];
    const vp_elem_def_t *d = &F.def->elems[e];
    ai_t *a = &ai[k];
    const float px = rest[k][0], py = rest[k][1], ax = rest[k][2] - px, ay = rest[k][3] - py;
    const float len = hypotf(ax, ay), ux = ax / len, uy = ay / len;
    const float rb = F.def->ball_radius;
    /* naechster Ball im Bereich des Flippers */
    const vp_ball_t *b = NULL;
    float bfr = 0, bdist = 0;
    for (int i = 0; i < F.nballs; i++) {
        const vp_ball_t *c = &F.balls[i];
        if (c->layer != d->layer) continue;
        const float rx = c->x - px, ry = c->y - py, fr = (rx * ux + ry * uy) / len;
        float dist = -(rx * uy - ry * ux);                               /* Hoehe ueber der Ruhelage (links: positiv = oben) */
        if (d->p[2] < 0) dist = -dist;
        if (fr < -0.15f || fr > 1.15f || dist < -0.3f || dist > 2.2f) continue;
        if (!b || dist < bdist) { b = c; bfr = fr; bdist = dist; }
    }
    if (a->mode == 4) { a->t -= dt; if (a->t <= 0) a->mode = 0; return 1; }
    if (!b) { if (a->mode != 0) { a->mode = 0; } return 0; }
    if (a->mode == 0) {
        a->frac = 0.12f + 0.9f * frand();
        a->mode = frand() < 0.4f ? 2 : 1;
        a->t = 0;
        a->serial = (int16_t)b->serial;
    }
    const float near = rb + 0.30f, sp = hypotf(b->vx, b->vy);
    switch (a->mode) {
    case 1:                                                              /* Schuss aus dem Lauf: schlagen, wenn der Ball die Stelle erreicht */
        if (bdist < near + sp * 0.02f) {
            const float along = (b->vx * ux + b->vy * uy);
            if ((along >= 0 && bfr >= a->frac) || (along < 0 && bfr <= a->frac) || bfr > 0.97f) { a->mode = 4; a->t = 0.18f; return 1; }
        }
        return 0;
    case 2:                                                              /* fangen: Flipper oben halten, bis der Ball ruht */
        a->t += dt;
        if (sp < 0.6f && bdist < 1.4f && a->t > 0.5f) { a->mode = 3; a->t = 0; return 0; }
        if (a->t > 2.5f) { a->mode = 1; return 1; }                      /* liess sich nicht fangen: aus dem Lauf schiessen */
        return 1;
    default:                                                             /* 3: losgelassen, der Ball rollt zur Spitze - schlagen an der Stelle */
        a->t += dt;
        if (bfr >= a->frac || a->t > 1.5f) { a->mode = 4; a->t = 0.18f; return 1; }
        return 0;
    }
}

/* ---- Schusskarte ----
 * Gezielte Schuesse statt Dauerspiel: von jedem Flipper, an jeder Schlagstelle (0 = Drehpunkt .. 1 = Spitze), in drei Arten:
 *   A  gefangen: Ball ruht auf dem gehaltenen Flipper, loslassen, an der Stelle schlagen (der kontrollierte Schuss)
 *   B  rollend: Ball rollt aus dem Stand vom Drehpunkt zur Spitze des ruhenden Flippers
 *   C  aus dem Lauf: wie B, der Ball kommt mit Tempo 3 an (aus der Einlaufbahn)
 * Je Schuss beginnt der Tisch neu (alle Ziele stehen). Gezaehlt wird je Ziel, in wie vielen Schuessen es getroffen wurde,
 * bevor der Ball wieder herunterkommt - das ist die Breite des Trefferfensters: 0 = von den Flippern aus nicht zu schaffen. */
static int MAP_FRACS = 60;            /* Schlagstellen je Flipper und Art (SHOT_FRACS) */
static float g_flip, g_slope, g_plunger;
static int g_real, g_table;
static long map_hits[MAX_T], map_trials, map_cradle_fail;
static float map_top;

static void fresh(uint32_t seed)
{
    vp_load(&F, g_table - 1, seed);
    vp_set_real_physics(&F, g_real);
    F.flip_scale = g_flip / 100.0f; F.slope_scale = g_slope / 100.0f; F.launch_k = g_plunger / 100.0f;
    orig = F.dg;
    wrap = *orig;
    wrap.collision = w_collision; wrap.spinner = w_spinner; wrap.sensor = w_sensor;
    F.dg = &wrap;
    vp_start_game(&F, 1);
}

static uint32_t m_us;
static uint32_t m_was[VP_MAX_ELEMS];
static void mstep(int l, int r)
{
    vp_set_flippers(&F, l, r);
    m_us += 4167;
    vp_advance(&F, 1.0f / 240.0f, m_us / 1000, 1.0f);
    F.sfx = 0;
    for (int e = 0; e < F.def->nelem; e++) {
        if (F.def->elems[e].type != VP_ROLL) continue;
        const uint32_t now = F.es[e].prev_hit, fr = now & ~m_was[e];
        m_was[e] = now;
        for (int k = 0; fr >> k; k++) if ((fr >> k) & 1) hit(e, k);
    }
}

static void one_shot(int k, int variant, float frac, uint32_t seed)
{
    fresh(seed);
    memset(m_was, 0, sizeof m_was);
    const int e = F.flip[k], right = F.def->elems[e].p[2] < 0;
    const float rb = F.def->ball_radius;
    float r0[4];
    vp_flipper_line(&F, e, &r0[0], &r0[1], &r0[2], &r0[3]);
    const float ax = r0[2] - r0[0], ay = r0[3] - r0[1], len = hypotf(ax, ay), ux = ax / len, uy = ay / len;
    float nx = -uy, ny = ux;
    if (ny < 0) { nx = -nx; ny = -ny; }                               /* "oben" auf dem Flipper */
    vp_ball_t *b;
    m_us = 0;
    if (variant == 0) {
        float h[4];
        for (int i = 0; i < 72; i++) mstep(!right, right);
        vp_flipper_line(&F, e, &h[0], &h[1], &h[2], &h[3]);
        float hx = h[2] - h[0], hy = h[3] - h[1], hl = hypotf(hx, hy), hnx = -hy / hl, hny = hx / hl;
        if (hny < 0) { hnx = -hnx; hny = -hny; }
        b = vp_create_ball(&F, h[0] + hx * 0.4f + hnx * (rb + 0.2f), h[1] + hy * 0.4f + hny * (rb + 0.2f));
        if (!b) return;
        b->layer = F.def->elems[e].layer;
        int ok = 0;
        for (int i = 0; i < 240 * 3 && F.nballs; i++) {
            mstep(!right, right);
            if (F.nballs && i > 120 && hypotf(F.balls[0].vx, F.balls[0].vy) < 0.25f) { ok = 1; break; }
        }
        if (!ok || !F.nballs) { map_cradle_fail++; return; }
    } else {
        b = vp_create_ball(&F, r0[0] + ax * 0.08f + nx * (rb + 0.14f), r0[1] + ay * 0.08f + ny * (rb + 0.14f));
        if (!b) return;
        b->layer = F.def->elems[e].layer;
        if (variant == 2) { b->vx = ux * 3.0f; b->vy = uy * 3.0f; }
    }
    /* loslassen bzw. rollen lassen, an der Stelle schlagen */
    int fired = 0;
    for (int i = 0; i < 240 * 3 && F.nballs; i++) {
        const vp_ball_t *c = &F.balls[0];
        const float fr = ((c->x - r0[0]) * ux + (c->y - r0[1]) * uy) / len;
        if (fr >= frac) { fired = 1; break; }
        if (fr < -0.3f || c->y < r0[1] - 3.0f) break;
        mstep(0, 0);
    }
    if (!fired || !F.nballs) return;
    map_trials++;
    for (int q = 0; q < ntg; q++) tg[q].hits = 0;
    const float y0 = F.balls[0].y;
    int away = 0;
    for (int i = 0; i < 240 * 7 && F.nballs; i++) {
        mstep(i < 48 ? !right : 0, i < 48 ? right : 0);
        if (!F.nballs) break;
        const vp_ball_t *c = &F.balls[0];
        if (c->y > map_top && c->layer == F.def->elems[e].layer) map_top = c->y;
        if (c->y > y0 + 3.0f || c->layer != F.def->elems[e].layer) away = 1;
        if (away && c->layer == F.def->elems[e].layer && c->y < y0 + 0.8f) break;   /* wieder unten */
    }
    for (int q = 0; q < ntg; q++) if (tg[q].hits) map_hits[q]++;
}

static int shot_map(int seeds)
{
    const int sum = getenv("SHOT_SUM") != NULL;
    if (getenv("SHOT_FRACS")) MAP_FRACS = atoi(getenv("SHOT_FRACS"));
    if (MAP_FRACS < 2) MAP_FRACS = 2;
    fresh(1);
    collect_targets();
    const int nflip = F.n_flip < 8 ? F.n_flip : 8;
    static long per[8][MAX_T];
    memset(per, 0, sizeof per);
    long total = 0;
    for (int k = 0; k < nflip; k++) {
        long before[MAX_T];
        memcpy(before, map_hits, sizeof before);
        for (int v = 0; v < 3; v++)
            for (int fi = 0; fi < MAP_FRACS; fi++)
                for (int sd = 0; sd < seeds; sd++)
                    one_shot(k, v, 1.0f * fi / (MAP_FRACS - 1), 1000u + (uint32_t)(sd * 7919 + fi * 31 + v * 101 + k * 13));   /* ab 0: die Spule braucht Zeit, der Ball rollt derweil weiter */
        for (int q = 0; q < ntg; q++) per[k][q] = map_hits[q] - before[q];
    }
    total = map_trials;
    int never = 0, narrow = 0;
    for (int q = 0; q < ntg; q++) {
        const float pct = total ? 100.0f * map_hits[q] / total : 0;
        if (!map_hits[q]) never++; else if (pct < 0.5f) narrow++;
    }
    fresh(1);
    if (sum) {
        printf("T%-2d flip %3.0f slope %3.0f %s  ziele %3d  kein fenster %2d  schmal %2d  gipfel %4.1f/%2.0f  schuesse %ld",
               g_table, g_flip, g_slope, g_real ? "REAL" : "ARC ", ntg, never, narrow, map_top, F.def->height, total);
        for (int q = 0; q < ntg; q++) if (!map_hits[q]) printf("  !%s%s%.0d", F.def->elems[tg[q].e].id ? F.def->elems[tg[q].e].id : "?", tg[q].sub >= 0 ? "." : "", tg[q].sub >= 0 ? tg[q].sub + 1 : 0);
        printf("\n");
        return 0;
    }
    printf("Schusskarte Tisch %d %s  Flipper %.0f%%  Neigung %.0f%%  %s  %ld Schuesse von %d Flippern (Fangen misslang %ld-mal), Gipfel %.1f von %.0f\n\n",
           g_table, F.def->name, g_flip, g_slope, g_real ? "REALISTIC" : "ARCADE", total, nflip, map_cradle_fail, map_top, F.def->height);
    printf("  Art  Ziel                              Fenster %%   je Flipper (Treffer)\n");
    for (int q = 0; q < ntg; q++) {
        char name[64];
        const char *id = F.def->elems[tg[q].e].id;
        if (tg[q].sub >= 0) snprintf(name, sizeof name, "%s.%d", id ? id : "?", tg[q].sub + 1);
        else snprintf(name, sizeof name, "%s", id ? id : "?");
        printf("  %c    %-32s %7.2f  ", tg[q].kind, name, total ? 100.0f * map_hits[q] / total : 0);
        for (int k = 0; k < nflip; k++) printf(" %4ld", per[k][q]);
        printf("%s\n", !map_hits[q] ? "   KEIN FENSTER" : (100.0f * map_hits[q] / total < 0.5f ? "   schmal" : ""));
    }
    printf("\n%d Ziele, %d ohne Fenster, %d schmal (unter 0,5 %% der Schuesse)\n", ntg, never, narrow);
    return 0;
}

int main(int argc, char **argv)
{
    const int table = argc > 1 ? atoi(argv[1]) : 1;
    const float flip = argc > 2 ? (float)atof(argv[2]) : 100, slope = argc > 3 ? (float)atof(argv[3]) : 100;
    const float hours = argc > 4 ? (float)atof(argv[4]) : 2;
    const int real = argc > 5 ? atoi(argv[5]) : 1;
    const uint32_t seed = argc > 6 ? (uint32_t)atoi(argv[6]) : 4711;
    const float plunger = argc > 7 ? (float)atof(argv[7]) : (table == 5 ? 80 : 100);
    const int sum = getenv("SHOT_SUM") != NULL;
    if (table < 1 || table > vp_num_tables) return 1;
    g_table = table; g_flip = flip; g_slope = slope; g_plunger = plunger; g_real = real;
    if (getenv("SHOT_MAP")) return shot_map(getenv("SHOT_SEEDS") ? atoi(getenv("SHOT_SEEDS")) : (real ? 3 : 1));

    vp_load(&F, table - 1, seed);
    vp_set_real_physics(&F, real);
    F.flip_scale = flip / 100.0f; F.slope_scale = slope / 100.0f; F.launch_k = plunger / 100.0f;
    collect_targets();
    orig = F.dg;
    wrap = *orig;
    wrap.collision = w_collision; wrap.spinner = w_spinner; wrap.sensor = w_sensor;
    F.dg = &wrap;
    vp_start_game(&F, 1);
    for (int k = 0; k < F.n_flip && k < 8; k++) vp_flipper_line(&F, F.flip[k], &rest[k][0], &rest[k][1], &rest[k][2], &rest[k][3]);

    const float dt = 1.0f / 120.0f;
    const long steps = (long)(hours * 3600 * 120);
    uint32_t us = 0;
    long balls = 0, escapes = 0, flips = 0;
    double vsum = 0; long vn = 0;
    float vmax = 0;
    for (long st = 0; st < steps; st++) {
        int l = 0, r = 0;
        for (int k = 0; k < F.n_flip && k < 8; k++) {
            if (!flipper_wants(k, dt)) continue;
            if (F.def->elems[F.flip[k]].p[2] < 0) r = 1; else l = 1;
        }
        if ((l && !F.flip_left) || (r && !F.flip_right)) flips++;
        vp_set_flippers(&F, l, r);
        int lying = F.nballs > 0;                                        /* liegt ein Ball still in der Startbahn? */
        for (int i = 0; i < F.nballs; i++) if (hypotf(F.balls[i].vx, F.balls[i].vy) > 0.4f) lying = 0;
        if ((F.nballs == 0 || lying) && vp_launch_ready(&F)) { const int fresh = F.nballs == 0; if (vp_launch_power(&F, 1.0f) && fresh) balls++; }   /* auch ein in der Startbahn liegengebliebener Ball */
        vp_catch_returned_ball(&F);
        us += 8333;
        vp_advance(&F, dt, us / 1000, 1.0f);
        F.sfx = 0;
        for (int e = 0; e < F.def->nelem; e++) {                         /* Rollover: frisch beruehrt? */
            static uint32_t was[VP_MAX_ELEMS];
            if (F.def->elems[e].type != VP_ROLL) continue;
            const uint32_t now = F.es[e].prev_hit, fresh = now & ~was[e];
            was[e] = now;
            for (int k = 0; fresh >> k; k++) if ((fresh >> k) & 1) hit(e, k);
        }
        if (getenv("SHOT_POS") && st % 240 == 0 && F.nballs)             /* Lage alle 2 s (Fehlersuche) */
            printf("POS %6.1f s  x %.2f y %.2f v %.2f  flipper %d%d  modus %d/%d\n", st / 120.0, F.balls[0].x, F.balls[0].y,
                   hypotf(F.balls[0].vx, F.balls[0].vy), F.flip_left, F.flip_right, ai[0].mode, ai[1].mode);
        if (getenv("SHOT_DBG") && F.nballs) {                           /* wo bleibt ein Ball liegen? */
            static long still; static int shown;
            const vp_ball_t *b = &F.balls[0];
            if (hypotf(b->vx, b->vy) < 0.3f) still++; else still = 0;
            if (still == 120 * 8 && shown++ < 12) printf("DBG liegt seit 8 s bei x %.2f y %.2f Ebene %d (%.0f s Spielzeit, Baelle %d)\n", b->x, b->y, b->layer, st / 120.0, F.nballs);
        }
        for (int i = 0; i < F.nballs; i++) {
            vp_ball_t *b = &F.balls[i];
            const float v = hypotf(b->vx, b->vy);
            if (v > vmax) vmax = v;
            vsum += v; vn++;
            if (!(v == v) || b->x < -1 || b->x > F.def->width + 1 || b->y < -1 || b->y > F.def->height + 1) { escapes++; vp_remove_ball(&F, b); break; }
        }
        if (!F.in_progress) vp_start_game(&F, 1);
    }

    int never = 0, rare = 0;
    for (int k = 0; k < ntg; k++) { if (!tg[k].hits) never++; else if (tg[k].hits / hours < 2.0f) rare++; }
    if (sum) {
        printf("T%-2d flip %3.0f slope %3.0f %s  ziele %3d  nie %2d  selten %2d  baelle/h %5.0f  s/ball %5.1f  vmittel %4.1f  vmax %4.1f",
               table, flip, slope, real ? "REAL" : "ARC ", ntg, never, rare, balls / hours, balls ? hours * 3600 / balls : 0, vn ? vsum / vn : 0, vmax);
        for (int k = 0; k < ntg; k++) if (!tg[k].hits) printf("  !%s%s%.0d", F.def->elems[tg[k].e].id ? F.def->elems[tg[k].e].id : "?", tg[k].sub >= 0 ? "." : "", tg[k].sub >= 0 ? tg[k].sub + 1 : 0);
        printf("\n");
        return 0;
    }
    printf("Tisch %d %s  Flipper %.0f%%  Neigung %.0f%%  Stoessel %.0f%%  %s  %.1f h Spielzeit, %ld Baelle (%.1f s je Ball), %ld Schlaege, Ausreisser %ld\n",
           table, F.def->name, flip, slope, plunger, real ? "REALISTIC" : "ARCADE", hours, balls, balls ? hours * 3600 / balls : 0, flips, escapes);
    printf("Balltempo: Mittel %.1f, Spitze %.1f\n\n  Art  Ziel                              Treffer/h\n", vn ? vsum / vn : 0, vmax);
    for (int k = 0; k < ntg; k++) {
        char name[64];
        const char *id = F.def->elems[tg[k].e].id;
        if (tg[k].sub >= 0) snprintf(name, sizeof name, "%s.%d", id ? id : "?", tg[k].sub + 1);
        else snprintf(name, sizeof name, "%s", id ? id : "?");
        if (tg[k].hits) printf("  %c    %-32s %8.1f%s\n", tg[k].kind, name, tg[k].hits / hours, tg[k].hits / hours < 2.0f ? "   selten" : "");
        else printf("  %c    %-32s      NIE\n", tg[k].kind, name);
    }
    printf("\n%d Ziele, %d nie getroffen, %d selten (unter 2 je Stunde)\n", ntg, never, rare);
    return 0;
}
