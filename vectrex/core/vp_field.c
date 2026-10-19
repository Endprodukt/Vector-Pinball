/*
 * vp_field.c - Spielfeld: Elemente, Spielstand, Zeitsteuerung. Entspricht Field.java, GameState.java
 * und den Elementklassen des Originals (Verhalten und Reihenfolge der Schritte bewusst gleich gehalten).
 */
/* nicht zeitkritisch genug fuer -O3: die Zeit je Bild bestimmt der Strahl. Auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include "vp.h"
#include "fmath.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SUBSTEPS_PER_SEC 480.0f      /* Physikschritte je Sekunde ECHTZEIT (Original: 4 je Bild, also 240 bei 60 Hz) */
#define LOGIC_EVERY      4           /* Elemente/Tischlogik alle 4 Schritte (120 mal je Sekunde) */
#define STUCK_SECONDS    10.0f
#define END_BALL_DELAY_MS 750

static const vp_delegate_t NO_DELEGATE;

/* ---- kleine Helfer ---- */
uint32_t vp_rand(vp_field_t *f)
{
    uint32_t x = f->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return f->rng = x ? x : 0x1234567u;
}
float vp_randf(vp_field_t *f) { return (float)(vp_rand(f) >> 8) / 16777216.0f; }

int vp_find(const vp_field_t *f, const char *id)
{
    for (int e = 0; e < f->def->nelem; e++) if (!strcmp(f->def->elems[e].id, id)) return e;
    return -1;
}
int vp_is(const vp_field_t *f, int e, const char *id) { return e >= 0 && !strcmp(f->def->elems[e].id, id); }
int vp_starts(const vp_field_t *f, int e, const char *prefix) { return e >= 0 && !strncmp(f->def->elems[e].id, prefix, strlen(prefix)); }

uint32_t vp_with_alpha(uint32_t c, int a) { return (c & 0xFFFFFFu) | ((uint32_t)a << 24); }

uint32_t vp_blend(uint32_t s, uint32_t e, float frac)
{
    if (frac <= 0) return s;
    if (frac >= 1) return e;
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int a = (int)((s >> sh) & 255), b = (int)((e >> sh) & 255);
        out |= (uint32_t)(int)(a + (b - a) * frac) << sh;
    }
    return out;
}

int vp_has_ball_at_layer(const vp_field_t *f, int layer)
{
    for (int i = 0; i < f->nballs; i++) if (f->balls[i].layer == layer) return 1;
    return 0;
}

uint32_t vp_elem_color(const vp_field_t *f, int e, uint32_t def_color, int *flash)
{
    const vp_elem_def_t *d = &f->def->elems[e];
    const vp_estate_t *s = &f->es[e];
    uint32_t base = s->has_new_color ? s->new_color : ((d->flags & VF_HAS_COLOR) ? d->color : def_color);
    *flash = 0;
    if ((d->flags & VF_HAS_INACT) && s->layer_frac < 1) return vp_blend(d->inact_color, base, s->layer_frac);
    *flash = s->flash > 0;
    return base;
}

uint32_t vp_elem_outer_color(const vp_field_t *f, int e, int *flash)
{
    const vp_elem_def_t *d = &f->def->elems[e];
    const vp_estate_t *s = &f->es[e];
    uint32_t base = s->has_new_outer ? s->new_outer : d->outer_color;
    *flash = 0;
    if ((d->flags & VF_HAS_INACT_OUTER) && s->layer_frac < 1) return vp_blend(d->inact_outer_color, base, s->layer_frac);
    *flash = s->flash > 0;
    return base;
}

void vp_set_color(vp_field_t *f, int e, int has, uint32_t c) { if (e >= 0) { f->es[e].has_new_color = (uint8_t)has; f->es[e].new_color = c; } }
void vp_set_outer(vp_field_t *f, int e, int has, uint32_t c) { if (e >= 0) { f->es[e].has_new_outer = (uint8_t)has; f->es[e].new_outer = c; } }

/* ---- Formen der Tischlogik (Sterne, Spielkarten) ---- */
static vp_shape_t *shape_new(vp_field_t *f, int kind, uint32_t c)
{
    if (f->nshapes >= VP_MAX_SHAPES) return NULL;
    vp_shape_t *s = &f->shapes[f->nshapes++];
    s->kind = (uint8_t)kind; s->color = c;
    return s;
}
void vp_shape_line(vp_field_t *f, float x1, float y1, float x2, float y2, uint32_t c)
{
    vp_shape_t *s = shape_new(f, VS_LINE, c);
    if (s) { s->v[0] = x1; s->v[1] = y1; s->v[2] = x2; s->v[3] = y2; }
}
void vp_shape_circle(vp_field_t *f, float cx, float cy, float r, int solid, uint32_t c)
{
    vp_shape_t *s = shape_new(f, solid ? VS_DISC : VS_CIRCLE, c);
    if (s) { s->v[0] = cx; s->v[1] = cy; s->v[2] = r; }
}
void vp_shape_arc(vp_field_t *f, float cx, float cy, float rx, float ry, float a0, float a1, uint32_t c)
{
    vp_shape_t *s = shape_new(f, VS_ARC, c);
    if (s) { s->v[0] = cx; s->v[1] = cy; s->v[2] = rx; s->v[3] = ry; s->v[4] = a0 * 0.01745329252f; s->v[5] = a1 * 0.01745329252f; }
}

/* ---- Zeitgesteuerte Aktionen ---- */
void vp_schedule(vp_field_t *f, int ms, vp_action_fn fn, int arg)
{
    double at = f->time + ms / 1000.0;
    /* Ein Sensor meldet sich in jedem Takt, solange der Ball in ihm liegt, und plant dabei jedes Mal dieselbe Aktion: gleiche Aktionen,
     * die weniger als 50 ms auseinander liegen, zaehlen als eine (sonst liefe die Liste voll) */
    for (int i = 0; i < f->nactions; i++)
        if (f->actions[i].fn == fn && f->actions[i].arg == arg && at - f->actions[i].at < 0.05 && at - f->actions[i].at > -0.05) return;
    if (f->nactions >= VP_MAX_ACTIONS) return;
    vp_action_t *a = &f->actions[f->nactions++];
    a->at = at; a->fn = fn; a->arg = arg; a->seq = f->action_seq++;
}

static void run_actions(vp_field_t *f)
{
    for (;;) {
        int best = -1;
        for (int i = 0; i < f->nactions; i++) {
            if (f->actions[i].at > f->time) continue;
            if (best < 0 || f->actions[i].at < f->actions[best].at ||
                (f->actions[i].at == f->actions[best].at && f->actions[i].seq < f->actions[best].seq)) best = i;
        }
        if (best < 0) return;
        vp_action_t a = f->actions[best];
        f->actions[best] = f->actions[--f->nactions];
        a.fn(f, a.arg);
    }
}

/* ---- Meldungen ---- */
void vp_message(vp_field_t *f, const char *text, int ms, int sound)
{
    if (sound) f->sfx |= VSFX_MESSAGE;
    strncpy(f->msg, text, sizeof f->msg - 1);
    f->msg[sizeof f->msg - 1] = 0;
    f->msg_until = f->now_ms + (uint32_t)ms;
    if (!f->msg_until) f->msg_until = 1;
}

void vp_messagef(vp_field_t *f, int ms, const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    vp_message(f, buf, ms, 1);
}

/* ---- Punkte ---- */
int64_t vp_add_score(vp_field_t *f, int64_t points)
{
    int64_t add = (int64_t)llround((double)points * f->mult);
    f->score += add;
    return add;
}

void vp_inc_multiplier(vp_field_t *f, int ms)
{
    f->t_last_mult = f->time;
    f->mult += 1;
    vp_messagef(f, ms, "%dx Multiplier", (int)f->mult);
}

/* ---- Baelle ---- */
vp_ball_t *vp_ball_by_serial(vp_field_t *f, int serial)
{
    for (int i = 0; i < f->nballs; i++) if (f->balls[i].serial == serial) return &f->balls[i];
    return NULL;
}

void vp_ball_impulse(vp_ball_t *b, float ix, float iy)
{
    const float inv_m = 1.0f / 0.7853982f;
    b->vx += ix * inv_m; b->vy += iy * inv_m;
}

vp_ball_t *vp_create_ball(vp_field_t *f, float x, float y)
{
    if (f->nballs >= VP_MAX_BALLS) return NULL;
    vp_ball_t *b = &f->balls[f->nballs++];
    memset(b, 0, sizeof *b);
    b->x = x; b->y = y; b->gscale = 1;
    b->color = f->def->ball_color; b->color2 = f->def->ball_color2;
    b->last_sensor = b->prev_sensor = -1;
    b->serial = ++f->next_serial;
    if (!b->serial) b->serial = ++f->next_serial;
    return b;
}

void vp_update_launch_times(vp_field_t *f)
{
    f->lost_ms = -1;
    f->t_last_launch = f->time;
    if (f->nballs > 1) { if (f->t_multi_start < 0) f->t_multi_start = f->time; }
    else f->t_ball_start = f->time;
}

/* Neigung je Tisch, fest abgestimmt (Faktor auf die Schwerkraft des Originals). Mit der Physik REALISTIC reichte die Neigung
 * des Originals nicht: die langen Schuesse (Rampen, Orbits, obere Bahnen) waren von den Flippern aus nicht oder kaum zu
 * schaffen. Gemessen mit tools/shottest (SHOT_MAP): je Tisch die steilste Neigung, bei der kein Ziel gegenueber der Physik
 * des Originals verloren geht. Flacher statt staerker - der Schlag selbst bleibt, wie er ist. */
static const float TABLE_SLOPE[] = { 0.64f, 1.00f, 0.77f, 0.64f, 0.68f, 0.68f, 0.64f, 0.72f, 0.60f, 0.90f };
/* dazu die Schlagkraft der Flipper je Tisch (Faktor auf die Nenndrehzahl). Beides 2026-10-09 mit der VPX-Flipperphysik neu
 * abgestimmt (tools/shottest, Raster Kraft 100-130 % x Neigung 70-100 %): je Tisch die kleinste Abweichung, bei der kein Ziel
 * unter 30 % seines Fensters im Original faellt und moeglichst wenige unter 70 % - die Schuesse sollen bequem kommen, nicht
 * mit Brechen und Biegen (Olli). Neigung nicht unter 80 % des vorigen Stands, lieber etwas mehr Kraft. */
static const float TABLE_FLIP[]  = { 1.20f, 1.10f, 1.00f, 1.10f, 1.00f, 1.00f, 1.20f, 1.10f, 1.00f, 1.00f };

float vp_table_flip(const vp_field_t *f)
{
    return f->real_physics && f->table >= 0 && f->table < (int)(sizeof TABLE_FLIP / sizeof TABLE_FLIP[0]) ? TABLE_FLIP[f->table] : 1.0f;
}

float vp_slope(const vp_field_t *f)
{
    /* nur REALISTIC: ARCADE ist die Physik des Originals und bleibt bei dessen Neigung */
    const float base = f->real_physics && f->table >= 0 && f->table < (int)(sizeof TABLE_SLOPE / sizeof TABLE_SLOPE[0]) ? TABLE_SLOPE[f->table] : 1.0f;
    return base * (f->slope_scale > 0 ? f->slope_scale : 1.0f);
}

vp_ball_t *vp_launch_ball(vp_field_t *f)
{
    const vp_table_def_t *t = f->def;
    vp_ball_t *b = vp_create_ball(f, t->launch_x, t->launch_y);
    if (!b) return NULL;
    b->vx = t->launch_vx + (t->launch_dvx > 0 ? t->launch_dvx * vp_randf(f) : 0);
    b->vy = t->launch_vy + (t->launch_dvy > 0 ? t->launch_dvy * vp_randf(f) : 0);
    if (f->launch_scale > 0) { b->vx *= f->launch_scale; b->vy *= f->launch_scale; }
    if (f->launch_k > 0) { b->vx *= f->launch_k; b->vy *= f->launch_k; }
    {   /* flachere Neigung: der Stoessel schiesst entsprechend sanfter, damit der Ball so hoch kommt wie vorher */
        const float k = sqrtf(vp_slope(f));
        b->vx *= k; b->vy *= k;
    }
    f->sfx |= VSFX_BALL;
    vp_update_launch_times(f);
    return b;
}

void vp_remove_ball_quiet(vp_field_t *f, vp_ball_t *b)
{
    int i = (int)(b - f->balls);
    if (i < 0 || i >= f->nballs) return;
    for (; i < f->nballs - 1; i++) f->balls[i] = f->balls[i + 1];
    f->nballs--;
}

static void act_show_ball_message(vp_field_t *f, int arg)
{
    char b[32];
    if (arg < 0) { vp_message(f, "Shoot Again", 1500, 0); return; }
    if (f->nplayers > 1) snprintf(b, sizeof b, "Player %d Ball %d", (arg >> 8) + 1, arg & 255);
    else snprintf(b, sizeof b, "Ball %d", arg & 255);
    vp_message(f, b, 1500, 0);
}

static void player_save(vp_field_t *f)
{
    f->pl[f->player].score = f->score; f->pl[f->player].mult = f->mult;
    f->pl[f->player].ball_number = f->ball_number; f->pl[f->player].extra_balls = f->extra_balls;
}

static void player_load(vp_field_t *f, int p)
{
    f->player = (uint8_t)p;
    f->score = f->pl[p].score; f->mult = f->pl[p].mult;
    f->ball_number = f->pl[p].ball_number; f->extra_balls = f->pl[p].extra_balls;
}

int64_t vp_player_score(const vp_field_t *f, int player) { return player == f->player ? f->score : f->pl[player].score; }

void vp_end_game(vp_field_t *f)
{
    f->sfx |= VSFX_START;
    f->nballs = 0;
    f->in_progress = 0;
    vp_message(f, "Game Over", 2500, 1);
}

static void do_ball_lost(vp_field_t *f)
{
    f->lost_ms = (int32_t)f->now_ms;
    f->used_mercy = 0;
    f->tilted = 0; f->tilt_level = 0; f->tilt_warnings = 0;
    f->sfx |= VSFX_DRAIN;
    int has_extra = f->extra_balls > 0;
    /* Multiplikator-Erhoehung kurz vor dem Verlust gilt fuer den naechsten Ball */
    int preserve = f->t_last_mult >= 0 && f->time - f->t_last_mult <= f->def->preserve_ms / 1000.0;
    if (preserve) f->mult -= 1;
    {   /* GameState.doNextBall, Verhalten ROUND_HALF_DOWN */
        double m = floor(f->mult / 2);
        f->mult = m > 1 ? m : 1;
        if (preserve) f->mult += 1;
        if (f->extra_balls > 0) f->extra_balls--;                 /* Freiball: derselbe Spieler noch einmal */
        else {
            if (f->unlimited || f->ball_number < f->total_balls) f->ball_number++;
            else f->pl[f->player].done = 1;
            player_save(f);
            int next = -1;                                        /* der naechste Spieler, der noch Baelle hat */
            for (int k = 1; k <= f->nplayers && next < 0; k++) {
                int p = (f->player + k) % f->nplayers;
                if (!f->pl[p].done) next = p;
            }
            if (next < 0) f->in_progress = 0;
            else player_load(f, next);
        }
    }
    f->t_last_mult = -1;
    if (has_extra) vp_schedule(f, 1500, act_show_ball_message, -1);
    else if (f->in_progress) vp_schedule(f, 1500, act_show_ball_message, (f->player << 8) | (f->ball_number & 255));
    else vp_end_game(f);
    if (f->dg->ball_lost) f->dg->ball_lost(f);
}

#define SAVE_DELAY_MS 1700u

void vp_remove_ball(vp_field_t *f, vp_ball_t *b)
{
    vp_remove_ball_quiet(f, b);
    if (f->nballs == 0) {
        /* Ball gerettet - aber nie nach einem Tilt. Wie am echten Tisch laeuft der Ball erst durch die Balltruhe: er wird
         * nach SAVE_DELAY_MS von selbst wieder eingeschossen (vp_advance), solange zeigt das Spiel BALL SAVE. */
        if (!f->tilted && !f->used_mercy && f->t_ball_start >= 0 && f->time - f->t_ball_start <= f->def->mercy_ms / 1000.0) {
            f->used_mercy = 1;
            f->save_ms = f->now_ms + SAVE_DELAY_MS;
            if (!f->save_ms) f->save_ms = 1;
        } else do_ball_lost(f);
    } else if (!f->tilted && f->t_multi_start >= 0 && f->time - f->t_multi_start <= f->def->saver_ms / 1000.0) {
        vp_message(f, "Ball Saved", 1000, 1);
        vp_launch_ball(f);
    }
}

static int remove_dead_balls(vp_field_t *f)
{
    const vp_table_def_t *t = f->def;
    int n = 0;
    if (!t->has_dead) return 0;
    for (int i = f->nballs - 1; i >= 0; i--) {
        vp_ball_t *b = &f->balls[i];
        if (b->x > t->dead[0] && b->y > t->dead[1] && b->x < t->dead[2] && b->y < t->dead[3]) { vp_remove_ball_quiet(f, b); n++; }
    }
    return n;
}

int vp_launch_if_needed(vp_field_t *f)
{
    if (!f->in_progress) return 0;
    if (f->lost_ms >= 0 && (int32_t)f->now_ms - f->lost_ms <= END_BALL_DELAY_MS) return 0;
    if (f->save_ms) return 0;                                 /* der gerettete Ball kommt von selbst */
    int removed = remove_dead_balls(f);
    if (f->nballs == 0 || removed > 0) { vp_launch_ball(f); return 1; }
    return 0;
}

int vp_launch_ready(const vp_field_t *f)
{
    const vp_table_def_t *t = f->def;
    if (!f->in_progress) return 0;
    if (f->lost_ms >= 0 && (int32_t)f->now_ms - f->lost_ms <= END_BALL_DELAY_MS) return 0;
    if (f->save_ms) return 0;
    if (f->nballs == 0) return 1;
    if (!t->has_dead) return 0;
    for (int i = 0; i < f->nballs; i++) {
        const vp_ball_t *b = &f->balls[i];
        if (b->x > t->dead[0] && b->y > t->dead[1] && b->x < t->dead[2] && b->y < t->dead[3]) return 1;
    }
    return 0;
}

/* Der einzige Ball faellt in der Startbahn zurueck und kommt an der Abschussstelle vorbei: er bleibt dort auf dem Stoessel
 * liegen und wartet wieder auf den Abschuss (kein Ball im Spiel, wie vor dem ersten Schuss). Ohne das laege er am Boden der
 * Bahn unter dem Stoessel. */
int vp_catch_returned_ball(vp_field_t *f)
{
    const vp_table_def_t *t = f->def;
    if (!f->in_progress || !t->has_dead || f->nballs != 1) return 0;
    vp_ball_t *b = &f->balls[0];
    if (b->x <= t->dead[0] || b->x >= t->dead[2] || b->vy >= 0 || b->y > t->launch_y || b->y < t->launch_y - 1.0f) return 0;
    vp_remove_ball_quiet(f, b);
    return 1;
}

/* Stoessel: ein schwacher Abschuss kommt nicht aus der Startbahn, der Ball rollt zurueck und kann neu abgeschossen werden.
 * Das geht nur, wo der Tisch den liegengebliebenen Ball erkennt (launchDeadZone); sonst immer volle Staerke. */
int vp_launch_power(vp_field_t *f, float power)
{
    if (power < 0) power = 0;
    if (power > 1 || !f->def->has_dead) power = 1;
    f->launch_scale = 0.5f + 0.5f * power;
    int r = vp_launch_if_needed(f);
    f->launch_scale = 0;
    return r;
}

void vp_ball_draw_pos(const vp_field_t *f, const vp_ball_t *b, float *x, float *y)
{
    *x = b->x + b->vx * f->step_acc;
    *y = b->y + b->vy * f->step_acc;
}

/* ---- Waende ---- */
void vp_wall_retract(vp_field_t *f, int e, int retracted) { if (e >= 0) f->es[e].retracted = (uint8_t)retracted; }

void vp_wall_set(vp_field_t *f, int e, float x1, float y1, float x2, float y2)
{
    if (e < 0) return;
    const vp_seg_t og = vp_seg(f->def, f->def->elems[e].first), *o = &og;
    float len = hypotf(o->x2 - o->x1, o->y2 - o->y1);
    float ang = atan2f(y2 - y1, x2 - x1);
    vp_seg_t *s = &f->es[e].seg;
    s->x1 = x1; s->y1 = y1; s->x2 = x1 + len * fm_cosf(ang); s->y2 = y1 + len * fm_sinf(ang);
}

/* ---- Ziele ---- */
int vp_drop_all_hit(const vp_field_t *f, int e)
{
    uint32_t all = (f->def->elems[e].count >= 32) ? 0xFFFFFFFFu : ((1u << f->def->elems[e].count) - 1);
    return (f->es[e].mask & all) == all;
}
static void act_drop_reset(vp_field_t *f, int e);
/* Ziele wieder aufstellen - aber nicht unter einem Ball: steht ein Ball gerade in der Reihe, kaeme das Ziel in ihm hoch, er
 * gaelte als schon beruehrt (kein Treffer) und konnte zwischen Ziel und Wand festklemmen (Tisch 10 oben rechts, gefunden mit
 * tools/shottest). Dann eine Drittelsekunde spaeter noch einmal versuchen. */
void vp_drop_reset(vp_field_t *f, int e)
{
    if (e < 0) return;
    const vp_elem_def_t *d = &f->def->elems[e];
    const float reach = f->def->ball_radius + 0.15f;
    for (int i = 0; i < f->nballs; i++) {
        const vp_ball_t *b = &f->balls[i];
        if (b->layer != d->layer) continue;
        for (int k = 0; k < d->count; k++) {
            const vp_seg_t g = vp_seg(f->def, d->first + k);
            const float ex = g.x2 - g.x1, ey = g.y2 - g.y1, l2 = ex * ex + ey * ey;
            float u = l2 > 0 ? ((b->x - g.x1) * ex + (b->y - g.y1) * ey) / l2 : 0;
            u = u < 0 ? 0 : (u > 1 ? 1 : u);
            const float dx = b->x - g.x1 - u * ex, dy = b->y - g.y1 - u * ey;
            if (dx * dx + dy * dy < reach * reach) { vp_schedule(f, 330, act_drop_reset, e); return; }
        }
    }
    f->es[e].mask = 0;
}
static void act_drop_reset(vp_field_t *f, int e) { vp_drop_reset(f, e); }

/* ---- Rollover ---- */
static uint32_t roll_all_mask(const vp_field_t *f, int e)
{
    int n = f->def->elems[e].count;
    return n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1);
}
int  vp_roll_all(const vp_field_t *f, int e) { return (f->es[e].mask & roll_all_mask(f, e)) == roll_all_mask(f, e); }
int  vp_roll_active(const vp_field_t *f, int e, int i) { return (f->es[e].mask >> i) & 1; }
void vp_roll_set(vp_field_t *f, int e, int i, int active) { if (e < 0) return; if (active) f->es[e].mask |= 1u << i; else f->es[e].mask &= ~(1u << i); }
void vp_roll_set_all(vp_field_t *f, int e, int active) { if (e >= 0) f->es[e].mask = active ? roll_all_mask(f, e) : 0; }
void vp_roll_activate_first(vp_field_t *f, int e)
{
    if (e < 0) return;
    for (int i = 0; i < f->def->elems[e].count; i++) if (!vp_roll_active(f, e, i)) { f->es[e].mask |= 1u << i; return; }
}
vp_roll_t *vp_roll(vp_field_t *f, int e, int i) { return &f->rolls[f->def->elems[e].first + i]; }
static void act_roll_reset(vp_field_t *f, int arg) { f->es[arg >> 8].mask &= ~(1u << (arg & 255)); }

static void roll_cycle(vp_field_t *f, int e, int to_right)
{
    int n = f->def->elems[e].count;
    uint32_t old = f->es[e].mask, nm = 0;
    for (int i = 0; i < n; i++) {
        int prev = to_right ? (i == 0 ? n - 1 : i - 1) : (i == n - 1 ? 0 : i + 1);
        if ((old >> prev) & 1) nm |= 1u << i;
    }
    f->es[e].mask = nm;
}

static void roll_tick(vp_field_t *f, int e)
{
    const vp_elem_def_t *d = &f->def->elems[e];
    vp_estate_t *s = &f->es[e];
    if (s->ignore_ball) return;
    int all_prev = vp_roll_all(f, e);
    uint32_t all_hit = 0;
    for (int bi = 0; bi < f->nballs; bi++) {
        vp_ball_t *b = &f->balls[bi];
        if (b->layer != d->layer) continue;
        uint16_t serial = b->serial;
        for (int i = 0; i < d->count; i++) {
            b = vp_ball_by_serial(f, serial);                  /* die Tischlogik darf Baelle entfernen */
            if (!b) break;
            const vp_roll_t *r = &f->rolls[d->first + i];
            float dx = b->x - r->x, dy = b->y - r->y;
            if (dx * dx + dy * dy > r->r * r->r) continue;
            if (all_hit & (1u << i)) continue;
            all_hit |= 1u << i;
            if (s->prev_hit & (1u << i)) continue;
            const vp_roll_def_t *rd = &f->def->rolls[d->first + i];
            if (!(s->mask & (1u << i))) {
                s->mask |= 1u << i;
                if (rd->score) vp_add_score(f, rd->score);
                f->sfx |= VSFX_ROLLOVER;
                if (rd->reset > 0) vp_schedule(f, (int)(rd->reset * 1000), act_roll_reset, (e << 8) | i);
                if (!all_prev && vp_roll_all(f, e) && f->dg->roll_all) f->dg->roll_all(f, e, b);
            } else if (d->flags & VF_TOGGLE_OFF) {
                s->mask &= ~(1u << i);
                if (rd->score) vp_add_score(f, rd->score);
                f->sfx |= VSFX_ROLLOVER;
            }
        }
    }
    s->prev_hit = all_hit;
}

/* ---- Sensor ---- */
static void sensor_tick(vp_field_t *f, int e)
{
    const vp_elem_def_t *d = &f->def->elems[e];
    for (int bi = 0; bi < f->nballs; bi++) {
        vp_ball_t *b = &f->balls[bi];
        if (b->x < d->p[0] || b->x > d->p[2] || b->y < d->p[1] || b->y > d->p[3]) continue;
        if ((d->flags & VF_LAYER_FROM) && (int)d->p[5] != b->layer) continue;
        if (d->flags & VF_LAYER_TO) b->layer = (int8_t)d->p[4];
        uint16_t serial = b->serial;
        if (f->dg->sensor) f->dg->sensor(f, e, b);
        b = vp_ball_by_serial(f, serial);
        if (b) b->last_sensor = (int16_t)e;
    }
}

/* ---- Spinner ---- */
static void spinner_tick(vp_field_t *f, int e, float dt)
{
    const vp_elem_def_t *d = &f->def->elems[e];
    vp_estate_t *s = &f->es[e];
    if (s->st == 0) {
        for (int bi = 0; bi < f->nballs; bi++) {
            vp_ball_t *b = &f->balls[bi];
            float dx = b->x - d->p[0], dy = b->y - d->p[1];
            if (dx * dx + dy * dy > d->p[2] * d->p[2]) continue;
            float speed = hypotf(b->vx, b->vy);
            if (speed < d->p[3]) continue;
            s->a = speed / d->p[3] * d->p[4];
            s->w = 1.0f / s->a;
            s->t = 0.25f;
            s->st = 2;                                        /* aktiv, unsichtbar */
            if (f->dg->spinner) f->dg->spinner(f, e, b);
            break;
        }
    } else {
        s->w -= dt; s->t += dt;
        if (s->w <= 0) {
            s->st = (s->st == 1) ? 2 : 1;
            vp_add_score(f, s->score);
            if (s->t >= 0.25f) { s->t = 0; f->sfx |= VSFX_SCORE; }
            s->a *= d->p[6];
            s->w = 1.0f / s->a;
            if (s->st == 2 && s->a < d->p[5]) s->st = 0;
        }
    }
}

/* ---- Flipper ---- */
static float flip_eff_speed(const vp_field_t *f, int e) { return f->def->elems[e].p[2] < 0 ? -f->es[e].motor : f->es[e].motor; }
static void flip_set_eff(vp_field_t *f, int e, float v) { f->es[e].motor = f->def->elems[e].p[2] < 0 ? -v : v; }
int vp_flipper_engaged(const vp_field_t *f, int e) { return flip_eff_speed(f, e) > 0; }

static void flipper_tick(vp_field_t *f, int e)
{
    /* Kurz vor dem oberen Anschlag bremsen, damit ein aufliegender Ball nicht weggeschleudert wird */
    const vp_elem_def_t *d = &f->def->elems[e];
    if (flip_eff_speed(f, e) > 0.5f) {
        float top = vp_flipper_top(f, e);                      /* Gelenkwinkel oben (bei gespiegelten Flippern negativ) */
        (void)d;
        if (fabsf(top - f->es[e].a) < 0.05f) flip_set_eff(f, e, 0.5f);
    }
}

static void engage(vp_field_t *f, int right, int engaged)
{
    int all_prev = 1, any = 0;
    for (int k = 0; k < f->n_flip; k++) {
        int e = f->flip[k];
        const vp_elem_def_t *d = &f->def->elems[e];
        if ((d->p[2] < 0) != right) continue;
        any = 1;
        int was = vp_flipper_engaged(f, e);
        if (!was) all_prev = 0;
        if (engaged != was && engaged) { f->es[e].cx = f->es[e].a; f->es[e].cy = 0; }   /* Winkel und Zeit des Schlagbeginns */
        if (engaged != was) flip_set_eff(f, e, engaged ? d->p[5] * vp_table_flip(f) * (f->flip_scale > 0 ? f->flip_scale : 1.0f) : -d->p[6]);
    }
    if (any && engaged && !all_prev) {
        f->sfx |= VSFX_FLIPPER;
        for (int e = 0; e < f->def->nelem; e++)
            if (f->def->elems[e].type == VP_ROLL && (f->def->elems[e].flags & VF_CYCLE)) roll_cycle(f, e, right);
    }
}

int vp_nudge(vp_field_t *f, int dir)
{
    if (!f->in_progress || f->tilted || f->nballs == 0 || f->nudge_cool > 0) return 0;
    /* Tilt wie am echten Tisch: ein Pendel, das bei zu heftigem Ruetteln den Kontakt beruehrt. Der Pegel steht fuer sein
     * Schwingen - jeder Stoss hebt ihn um 1, mit der Zeit klingt er ab (vp_advance). Beruehrt wird ab einer Schwelle, die
     * von der Empfindlichkeit abhaengt (LOW: erst der dritte schnelle Stoss, MEDIUM: der zweite, HIGH: jeder). Jede
     * Beruehrung ist eine Warnung; zwei sind frei, die dritte ist der Tilt. Die Warnungen gelten fuer den laufenden Ball. */
    static const float TOUCH_AT[3] = { 2.2f, 1.2f, 0.5f };
    const float k = f->nudge_scale > 0 ? f->nudge_scale : 1.0f;
    f->nudge_cool = 0.6f;
    for (int i = 0; i < f->nballs; i++) vp_ball_impulse(&f->balls[i], dir * 1.3f * k, (dir ? 0.4f : 1.8f) * k);
    f->tilt_level += f->nudge_tilt > 0 ? f->nudge_tilt : 1.0f;
    f->sfx |= VSFX_KICK;
    if (f->tilt_level > TOUCH_AT[f->tilt_sens < 3 ? f->tilt_sens : 0]) {
        if (++f->tilt_warnings >= 3) {
            f->tilted = 1;
            f->sfx |= VSFX_TILT;
            vp_message(f, "TILT", 3000, 1);
        } else { f->sfx |= VSFX_WARN; vp_message(f, "DANGER", 900, 0); }
    }
    return 1;
}

void vp_set_flippers(vp_field_t *f, int left, int right)
{
    if (f->tilted) left = right = 0;
    engage(f, 0, left);
    engage(f, 1, right);
    f->flip_left = (uint8_t)left; f->flip_right = (uint8_t)right;
}

/* ---- Stoesse ---- */
void vp_on_collision(vp_field_t *f, vp_ball_t *b, int e, int sub)
{
    const vp_elem_def_t *d = &f->def->elems[e];
    vp_estate_t *s = &f->es[e];
    uint16_t serial = b->serial;
    switch (d->type) {
    case VP_WALL:
        if (d->flags & VF_RETRACT_HIT) s->retracted = 1;
        if (d->flags & VF_KILL) { vp_remove_ball(f, b); return; }
        if (d->p[0] > 0.01f) {
            float ix = s->seg.y2 - s->seg.y1, iy = s->seg.x1 - s->seg.x2;
            float mag = hypotf(ix, iy);
            if (mag > 1e-6f) {
                float k = d->p[0] / mag;
                ix *= k; iy *= k;
                if ((b->x - s->seg.x1) * ix + (b->y - s->seg.y1) * iy < 0) { ix = -ix; iy = -iy; }
                if (f->real_physics) {                        /* echte Schleudern streuen: sonst pendelt ein Ball endlos zwischen zweien */
                    float a = (vp_randf(f) - 0.5f) * 0.24f, c = fm_cosf(a), sn = fm_sinf(a), tx = ix * c - iy * sn;
                    float g = 0.8f + 0.4f * vp_randf(f);      /* +-7 Grad, Kraft 80 bis 120 Prozent */
                    iy = (ix * sn + iy * c) * g; ix = tx * g;
                }
                vp_ball_impulse(b, ix, iy);
                s->flash = VP_FLASH_TIME;
                f->sfx |= (d->flags & VF_RETRACT_HIT) ? VSFX_BALL : VSFX_KICK;   /* Ballretter: klingt wie der Stoessel */
            }
        }
        break;
    case VP_BUMPER:
        if (d->p[4] > 0.01f) {
            float ix = b->x - s->cx, iy = b->y - s->cy;
            float mag = hypotf(ix, iy);
            if (mag > 1e-6f) {
                float k = d->p[4] / mag;
                if (f->real_physics) {                        /* ein echter Bumper trifft nie zweimal gleich: +-4 Grad Streuung */
                    float a = (vp_randf(f) - 0.5f) * 0.14f, c = fm_cosf(a), sn = fm_sinf(a), tx = ix * c - iy * sn;
                    iy = ix * sn + iy * c; ix = tx;
                }
                vp_ball_impulse(b, ix * k, iy * k);
                s->flash = VP_FLASH_TIME;
                f->sfx |= VSFX_KICK;
            }
        }
        break;
    case VP_DROP:
        if (s->mask & (1u << sub)) return;                    /* schon versenkt (z.B. von einem zweiten Ball) */
        s->mask |= 1u << sub;
        s->prev_hit = 1u << sub; s->flash = VP_FLASH_TIME;        /* das getroffene Ziel blitzt beim Fallen */
        if (vp_drop_all_hit(f, e)) {
            if (f->dg->drop_all) f->dg->drop_all(f, e, b);
            if (d->p[0] > 0) vp_schedule(f, (int)(d->p[0] * 1000), act_drop_reset, e);
        }
        break;
    default:
        break;
    }
    b = vp_ball_by_serial(f, serial);
    if (!b) return;
    if (f->dg->collision) f->dg->collision(f, e, sub, b);
    if (s->score != 0) {
        vp_add_score(f, s->score);
        f->sfx |= VSFX_SCORE;
        f->score_hits++;
    }
}

/* ---- Laden, Start ---- */
void vp_load(vp_field_t *f, int table, uint32_t seed)
{
    memset(f, 0, sizeof *f);
    if (table < 0 || table >= vp_num_tables) table = 0;
    const vp_table_def_t *t = &vp_tables[table];
    f->def = t; f->table = table;
    f->dg = vp_delegate_for(t->delegate);
    if (!f->dg) f->dg = &NO_DELEGATE;
    f->rng = seed ? seed : 0x9E3779B9u;
    f->save_ms = 0;
    f->t_ball_start = f->t_multi_start = f->t_last_launch = f->t_last_mult = -1;
    f->lost_ms = -1;
    f->mult = 1; f->total_balls = t->numballs;
    for (int e = 0; e < t->nelem && e < VP_MAX_ELEMS; e++) {
        const vp_elem_def_t *d = &t->elems[e];
        vp_estate_t *s = &f->es[e];
        s->visible = 1;
        s->score = d->score;
        switch (d->type) {
        case VP_WALL:
            s->seg = vp_seg(t, d->first);
            s->retracted = (d->flags & VF_DISABLED) ? 1 : 0;
            break;
        case VP_BUMPER: s->cx = d->p[0]; s->cy = d->p[1]; break;
        case VP_ROLL:   s->ignore_ball = (d->flags & VF_IGNORE_BALL) ? 1 : 0; break;
        case VP_FLIPPER: flip_set_eff(f, e, -d->p[6]); break;
        default: break;
        }
    }
    for (int i = 0; i < t->nroll && i < VP_MAX_ROLLS; i++) {
        f->rolls[i].x = t->rolls[i].x; f->rolls[i].y = t->rolls[i].y; f->rolls[i].r = t->rolls[i].r;
        f->rolls[i].color = t->rolls[i].color; f->rolls[i].has_color = t->rolls[i].has_color;
    }
    vp_phys_build(f);
    if (f->dg->init) f->dg->init(f);
}

void vp_start_game(vp_field_t *f, int unlimited)
{
    f->save_ms = 0;
    f->t_ball_start = f->t_multi_start = f->t_last_launch = f->t_last_mult = -1;
    f->lost_ms = -1;
    f->used_mercy = 0;
    f->total_balls = f->def->numballs;
    f->unlimited = (uint8_t)unlimited;
    f->score = 0; f->ball_number = 1; f->mult = 1; f->extra_balls = 0;
    if (f->nplayers < 1) f->nplayers = 1;
    if (f->nplayers > VP_MAX_PLAYERS) f->nplayers = VP_MAX_PLAYERS;
    f->player = 0;
    for (int p = 0; p < VP_MAX_PLAYERS; p++) { f->pl[p].score = 0; f->pl[p].mult = 1; f->pl[p].ball_number = 1; f->pl[p].extra_balls = 0; f->pl[p].done = 0; }
    f->in_progress = 1;
    f->score_hits = 0;
    f->tilted = 0; f->tilt_level = 0; f->tilt_warnings = 0;
    if (f->dg->game_started) f->dg->game_started(f);
}

/* ---- Takt ---- */
static void check_stuck(vp_field_t *f, float dt)
{
    int maybe = f->nballs > 0 && f->nballs == f->n_last;
    for (int i = 0; maybe && i < f->nballs; i++) {
        const vp_ball_t *b = &f->balls[i];
        float dx = b->x - f->last_x[i], dy = b->y - f->last_y[i];
        if (b->vx * b->vx + b->vy * b->vy > 0.01f || dx * dx + dy * dy > 0.01f) maybe = 0;
    }
    f->n_last = f->nballs;
    for (int i = 0; i < f->nballs; i++) { f->last_x[i] = f->balls[i].x; f->last_y[i] = f->balls[i].y; }
    if (!maybe) { f->stuck_time = 0; return; }
    for (int k = 0; k < f->n_flip; k++) if (vp_flipper_engaged(f, f->flip[k])) return;
    f->stuck_time += dt;
    if (f->stuck_time > STUCK_SECONDS) {
        vp_message(f, "Bump!", 1000, 1);
        for (int i = 0; i < f->nballs; i++) vp_ball_impulse(&f->balls[i], (vp_rand(f) & 1) ? 1.0f : -1.0f, 1.5f);
        f->stuck_time = 0;
    }
}

static void logic_tick(vp_field_t *f, float dt)
{
    const vp_table_def_t *t = f->def;
    f->time += dt;
    if (f->nudge_cool > 0) f->nudge_cool -= dt;
    if (f->save_ms && (int32_t)(f->now_ms - f->save_ms) >= 0) {      /* der gerettete Ball ist durch die Truhe */
        f->save_ms = 0;
        if (f->in_progress) vp_launch_ball(f);
    }
    if (f->tilt_level > 0) {
        static const float DECAY[3] = { 3.5f, 5.0f, 6.5f };            /* Sekunden je Stufe */
        f->tilt_level -= dt / DECAY[f->tilt_sens < 3 ? f->tilt_sens : 0];
        if (f->tilt_level < 0) f->tilt_level = 0;
    }
    for (int e = 0; e < t->nelem; e++) {
        const vp_elem_def_t *d = &t->elems[e];
        vp_estate_t *s = &f->es[e];
        if (s->flash > 0) { s->flash -= dt; if (s->flash < 0) s->flash = 0; }
        if (d->flags & VF_HAS_INACT) {
            float inc = dt / 0.25f;
            if (vp_has_ball_at_layer(f, d->layer)) { s->layer_frac += inc; if (s->layer_frac > 1) s->layer_frac = 1; }
            else { s->layer_frac -= inc; if (s->layer_frac < 0) s->layer_frac = 0; }
        }
        switch (d->type) {
        case VP_ROLL:    roll_tick(f, e); break;
        case VP_SENSOR:  sensor_tick(f, e); break;
        case VP_SPINNER: spinner_tick(f, e, dt); break;
        case VP_FLIPPER: flipper_tick(f, e); break;
        default: break;
        }
    }
    run_actions(f);
    if (f->msg_until && (int32_t)(f->now_ms - f->msg_until) > 0) { f->msg_until = 0; f->msg[0] = 0; }
    check_stuck(f, dt);
    if (f->dg->tick) f->dg->tick(f, dt);
}

void vp_advance(vp_field_t *f, float real_dt, uint32_t now_ms, float speed)
{
    const float h = f->def->ratio / SUBSTEPS_PER_SEC;
    f->now_ms = now_ms;
    if (real_dt > 0.1f) real_dt = 0.1f;                      /* nach einer Pause nicht aufholen */
    f->step_acc += real_dt * f->def->ratio * speed;
    while (f->step_acc >= h) {
        vp_phys_step(f, h);
        f->step_acc -= h;
        if (++f->sub_count >= LOGIC_EVERY) { f->sub_count = 0; logic_tick(f, h * LOGIC_EVERY); }
    }
}
