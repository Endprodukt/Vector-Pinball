#include "fx.h"
#include "render.h"
#include "gfx.h"
#include "font.h"
#include "fmath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- Leuchtspur ----
 * Je Ball die letzten Orte (einer je Bild); gezeichnet als Linienzug, der nach hinten verblasst und von der Ballfarbe ins
 * Blaue laeuft. Ein Ball, der steht, hat keine Spur. */
#define TRAIL_N 14
typedef struct { uint16_t serial; int n; float x[TRAIL_N], y[TRAIL_N]; } trail_t;
static trail_t trail[VP_MAX_BALLS];

/* ---- Funken ---- */
#define SPARK_N 96
typedef struct { float x, y, vx, vy, life, span; } spark_t;
static spark_t spark[SPARK_N];

/* ---- Punktzahlen ---- */
#define POP_N 10
#define POP_LIFE 1.0f
typedef struct { float x, y, t; long long value; } pop_t;
static pop_t pop[POP_N];
static long long last_score;
static int last_player = -1;
static const vp_table_def_t *last_def;

static uint32_t rnd_s = 0x1234567u;
static float rnd(void) { rnd_s = rnd_s * 1664525u + 1013904223u; return (float)(rnd_s >> 8) * (1.0f / 16777216.0f); }

void fx_reset(void)
{
    memset(trail, 0, sizeof trail);
    memset(spark, 0, sizeof spark);
    memset(pop, 0, sizeof pop);
    last_player = -1;
}

static void sparks_at(float x, float y, float dirx, float diry)
{
    int made = 0;
    for (int i = 0; i < SPARK_N && made < 9; i++) {
        spark_t *s = &spark[i];
        if (s->life > 0) continue;
        float a = rnd() * 6.2831853f, v = 5.0f + rnd() * 11.0f, sn, c;
        fm_sincos(a, &sn, &c);
        s->x = x; s->y = y;
        s->vx = c * v + dirx * 3.0f; s->vy = sn * v + diry * 3.0f;      /* ein wenig in die Richtung, in die der Ball fliegt */
        s->span = s->life = 0.22f + rnd() * 0.22f;
        made++;
    }
}

static void pop_add(float x, float y, long long value)
{
    pop_t *best = &pop[0];
    for (int i = 0; i < POP_N; i++) {                                 /* kurz nacheinander an derselben Stelle: eine Zahl, die waechst */
        pop_t *p = &pop[i];
        if (p->t > 0 && p->t > POP_LIFE - 0.3f && fabsf(p->x - x) + fabsf(p->y - y) < 2.5f) { p->value += value; p->t = POP_LIFE; return; }
        if (p->t < best->t) best = p;
    }
    best->x = x; best->y = y; best->t = POP_LIFE; best->value = value;
}

void fx_update(const vp_field_t *f, float dt)
{
    /* der Ball, um den es geht: der schnellste (ein eben getroffener Ball ist schnell) */
    int hot = -1;
    float hot_v = -1;
    for (int i = 0; i < f->nballs; i++) {
        const float v = f->balls[i].vx * f->balls[i].vx + f->balls[i].vy * f->balls[i].vy;
        if (v > hot_v) { hot_v = v; hot = i; }
    }

    for (int i = 0; i < VP_MAX_BALLS; i++) {
        trail_t *t = &trail[i];
        if (i >= f->nballs) { t->n = 0; continue; }
        float x, y;
        vp_ball_draw_pos(f, &f->balls[i], &x, &y);
        if (t->serial != f->balls[i].serial) { t->serial = f->balls[i].serial; t->n = 0; }
        if (t->n && fabsf(x - t->x[0]) + fabsf(y - t->y[0]) > 6.0f) t->n = 0;      /* versetzt (Truhe, Loch): keine Linie quer ueber den Tisch */
        memmove(t->x + 1, t->x, sizeof(float) * (TRAIL_N - 1));
        memmove(t->y + 1, t->y, sizeof(float) * (TRAIL_N - 1));
        t->x[0] = x; t->y[0] = y;
        if (t->n < TRAIL_N) t->n++;
    }

    if ((f->sfx & VSFX_KICK) && hot >= 0) {
        const vp_ball_t *b = &f->balls[hot];
        const float v = sqrtf(hot_v) + 1e-3f;
        sparks_at(b->x, b->y, b->vx / v, b->vy / v);
    }
    for (int i = 0; i < SPARK_N; i++) {
        spark_t *s = &spark[i];
        if (s->life <= 0) continue;
        s->life -= dt;
        s->x += s->vx * dt; s->y += s->vy * dt;
        s->vx *= 1.0f - 3.0f * dt; s->vy *= 1.0f - 3.0f * dt;
    }

    for (int i = 0; i < POP_N; i++) if (pop[i].t > 0) pop[i].t -= dt;
    if (f->def != last_def || f->player != last_player || f->score < last_score) {     /* neuer Tisch, anderer Spieler, neues Spiel */
        last_def = f->def; last_player = f->player; last_score = f->score;
    } else if (f->score > last_score) {
        const long long d = f->score - last_score;
        last_score = f->score;
        if (hot >= 0) pop_add(f->balls[hot].x, f->balls[hot].y + 0.9f, d);
        else pop_add(f->def->width * 0.5f, 7.0f, d);                  /* Bonus ohne Ball im Spiel: ueber den Flippern */
    }
}

void fx_draw_trails(const vp_field_t *f)
{
    for (int i = 0; i < f->nballs && i < VP_MAX_BALLS; i++) {
        const trail_t *t = &trail[i];
        if (t->n < 3) continue;
        if (fabsf(t->x[0] - t->x[2]) + fabsf(t->y[0] - t->y[2]) < 0.05f) continue;   /* steht */
        for (int k = 1; k < t->n; k++) {
            const float a = 1.0f - (float)k / (float)t->n;             /* 1 am Ball, 0 am Ende */
            const int b = (int)(96.0f * a * a) + 8;
            /* vom hellen Ball ins Blaue: Rot und Gruen fallen schneller als Blau */
            const uint32_t r = (uint32_t)(60.0f + 180.0f * a * a), g = (uint32_t)(110.0f + 130.0f * a), bl = 255;
            gfx_color((r << 16) | (g << 8) | bl);
            gfx_line(render_x(t->x[k - 1]), render_y(t->y[k - 1]), render_x(t->x[k]), render_y(t->y[k]), b);
        }
    }
}

void fx_draw_top(void)
{
    for (int i = 0; i < SPARK_N; i++) {
        const spark_t *s = &spark[i];
        if (s->life <= 0) continue;
        const float a = s->life / s->span;
        gfx_color(a > 0.6f ? 0xFFFFC0u : (a > 0.3f ? 0xFFC020u : 0xFF5010u));       /* weissgelb, gelb, rot: der Funke kuehlt ab */
        gfx_line(render_x(s->x), render_y(s->y), render_x(s->x - s->vx * 0.035f), render_y(s->y - s->vy * 0.035f), 40 + (int)(87.0f * a));
    }
    for (int i = 0; i < POP_N; i++) {
        const pop_t *p = &pop[i];
        char buf[24];
        if (p->t <= 0) continue;
        const float age = 1.0f - p->t / POP_LIFE;                       /* 0 neu .. 1 weg */
        const float grow = p->value >= 10000 ? 1.7f : (p->value >= 1000 ? 1.3f : 1.0f);
        const float in = age < 0.12f ? 0.6f + age / 0.12f * 0.4f : 1.0f; /* springt kurz auf */
        const int unit = (int)(render_units() * 16.0f * 0.11f * grow * in) + 1;
        const int b = age < 0.55f ? 127 : (int)(127.0f * (1.0f - age) / 0.45f) + 6;
        snprintf(buf, sizeof buf, "%lld", p->value);
        gfx_color(p->value >= 10000 ? 0xFF8030u : (p->value >= 1000 ? 0xFFE040u : 0xFFFFFFu));
        text_center(render_x(p->x), render_y(p->y + 1.6f * age), buf, unit, b);
    }
}
