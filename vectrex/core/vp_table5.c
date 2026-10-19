/* vp_table5.c - Tische 5 (Eye of Horus) und 6 (Orbiters): Field5Delegate, Field6Delegate */
/* nicht zeitkritisch: auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp (das Kit baut sonst mit -O3) */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include "vp_deleg.h"
#include "fmath.h"

/* ================= Tisch 5: Eye of Horus ================= */
enum { C_BLUE, C_RED, C_YELLOW, C_GREEN, C_N };
enum { M5_NOT_READY, M5_READY, M5_STARTING, M5_ACTIVE };
static const char *const T5_NAMES[C_N] = { "Blue", "Red", "Yellow", "Green" };
static const uint32_t T5_BALL[C_N]  = { VP_RGB(0x66, 0x88, 0xEE), VP_RGB(0xEE, 0x88, 0x88), VP_RGB(0xCC, 0xCC, 0x77), VP_RGB(0x77, 0xDD, 0x77) };
static const uint32_t T5_BALL2[C_N] = { VP_RGB(0x44, 0x66, 0xCC), VP_RGB(0xCC, 0x66, 0x66), VP_RGB(0xAA, 0xAA, 0x55), VP_RGB(0x55, 0xBB, 0x55) };
static const uint32_t T5_WALL[C_N]  = { VP_RGB(0x00, 0x66, 0xFF), VP_RGB(0xCC, 0x00, 0x00), VP_RGB(0xCC, 0xCC, 0x00), VP_RGB(0x00, 0xCC, 0x00) };
static const uint32_t T5_OFF[C_N]   = { VP_RGB(0x00, 0x33, 0x66), VP_RGB(0x66, 0x00, 0x00), VP_RGB(0x66, 0x66, 0x00), VP_RGB(0x00, 0x66, 0x00) };
#define T5_JACKPOT 50000
#define T5_RAMP    5000

static struct {
    int roll[C_N][3], line[C_N][3], barrier, tri[3], tri_center, extra_roll, extra_bar[C_N];
    int ramp_bonus[C_N], mb, jackpots, ready;
    float tri_mult, tri_angle;
} t5;

static int t5_ball_color(const vp_ball_t *b) { for (int c = 0; c < C_N; c++) if (b->color == T5_BALL[c]) return c; return -1; }
static void t5_set_ball_color(vp_ball_t *b, int c) { if (c >= 0) { b->color = T5_BALL[c]; b->color2 = T5_BALL2[c]; } }
static int t5_has_color(const vp_field_t *f, int c) { for (int i = 0; i < f->nballs; i++) if (t5_ball_color(&f->balls[i]) == c) return 1; return 0; }
static int t5_unused_color(const vp_field_t *f) { for (int c = 0; c < C_N; c++) if (!t5_has_color(f, c)) return c; return -1; }
static int t5_all_for_color(const vp_field_t *f, int c) { for (int i = 0; i < 3; i++) if (!vp_roll_all(f, t5.roll[c][i])) return 0; return 1; }

static void t5_update_rollovers(vp_field_t *f)
{
    for (int c = 0; c < C_N; c++) {
        int has = t5_has_color(f, c);
        for (int i = 0; i < 3; i++) {
            int ro = t5.roll[c][i];
            f->es[ro].ignore_ball = (uint8_t)!has;
            if (has) { f->es[ro].visible = 1; vp_set_color(f, ro, 1, T5_WALL[c]); }
            else {
                int active = vp_roll_all(f, ro);
                f->es[ro].visible = (uint8_t)active;
                if (active) vp_set_color(f, ro, 1, T5_OFF[c]);
            }
        }
    }
}

static void t5_reset_center(vp_field_t *f);

static void t5_update_lines(vp_field_t *f, vp_ball_t *b)
{
    int all = 1;
    for (int c = 0; c < C_N; c++)
        for (int i = 0; i < 3; i++) {
            int vis = vp_roll_all(f, t5.roll[c][i]) && vp_roll_all(f, t5.roll[c][i == 2 ? 0 : i + 1]);
            f->es[t5.line[c][i]].visible = (uint8_t)vis;
            all = all && vis;
        }
    if (!all) return;
    if (t5.mb == M5_ACTIVE) {
        t5.jackpots++;
        if (t5.jackpots > 1) vp_messagef(f, 3000, "%dx Jackpot!", t5.jackpots);
        else vp_message(f, "Jackpot!", 3000, 1);
        (void)b;
        vp_add_score(f, (int64_t)T5_JACKPOT * t5.jackpots);
        t5_reset_center(f);
    } else if (t5.mb == M5_NOT_READY) {
        vp_message(f, "Shoot the Pyramid!", 3000, 1);
        t5.mb = M5_READY;
    }
}

static void t5_reset_center(vp_field_t *f)
{
    for (int c = 0; c < C_N; c++) for (int i = 0; i < 3; i++) vp_roll_set(f, t5.roll[c][i], 0, 0);
    t5_update_rollovers(f);
    t5_update_lines(f, NULL);
}

static void t5_check_ramp(vp_field_t *f, vp_ball_t *b, const char *prev_id, int c)
{
    if (!vp_is(f, b->prev_sensor, prev_id)) return;
    if (!t5_has_color(f, c) && !t5_all_for_color(f, c)) { t5_set_ball_color(b, c); t5_update_rollovers(f); }
    int percent = 100 + t5.ramp_bonus[c];
    if (f->nballs > 1) percent *= 2;
    vp_add_score(f, (int64_t)(T5_RAMP / 100) * percent);
    f->sfx |= VSFX_ROLLOVER;
}

static void t5_act_launch(vp_field_t *f, int last)
{
    vp_ball_t *b = vp_launch_ball(f);
    if (b) t5_set_ball_color(b, t5_unused_color(f));
    t5_update_rollovers(f);
    if (last) t5.mb = M5_ACTIVE;
}

static void t5_start_multiball(vp_field_t *f)
{
    for (int i = 0; i < 3; i++) vp_wall_retract(f, t5.tri[i], 0);
    t5.tri_mult = -2.0f;
    t5_reset_center(f);
    if (f->nballs > 0) vp_remove_ball_quiet(f, &f->balls[0]);
    dg_restore_saver(f, "BallSaver-left"); dg_restore_saver(f, "BallSaver-right");
    t5.mb = M5_STARTING; t5.jackpots = 0;
    vp_schedule(f, 1000, t5_act_launch, 0);
    vp_schedule(f, 4000, t5_act_launch, 0);
    vp_schedule(f, 7000, t5_act_launch, 0);
    vp_schedule(f, 10000, t5_act_launch, 1);
}

static void t5_init(vp_field_t *f)
{
    char id[40];
    static const char *const LN[3] = { "1_2", "2_3", "3_1" };
    memset(&t5, 0, sizeof t5);
    t5.barrier = vp_find(f, "LaunchBarrier");
    for (int i = 0; i < 3; i++) { snprintf(id, sizeof id, "TriangleWall%d", i + 1); t5.tri[i] = vp_find(f, id); }
    t5.tri_center = vp_find(f, "TriangleCenter");
    for (int c = 0; c < C_N; c++) {
        for (int i = 0; i < 3; i++) {
            snprintf(id, sizeof id, "CenterRollover_%s_%d", T5_NAMES[c], i + 1); t5.roll[c][i] = vp_find(f, id);
            snprintf(id, sizeof id, "CenterLine_%s_%s", T5_NAMES[c], LN[i]);     t5.line[c][i] = vp_find(f, id);
        }
        snprintf(id, sizeof id, "ExtraBallBarrier_%s", T5_NAMES[c]); t5.extra_bar[c] = vp_find(f, id);
    }
    t5.extra_roll = vp_find(f, "ExtraBallRollover");
    t5.tri_mult = 1; t5.tri_angle = VP_TAU / 4;
    t5.mb = M5_NOT_READY;
    t5.ready = 1;
    t5_update_rollovers(f);
    t5_update_lines(f, NULL);
}

static void t5_started(vp_field_t *f) { t5_init(f); }

static void t5_ball_lost(vp_field_t *f)
{
    t5_update_rollovers(f);
    for (int i = 0; i < f->nballs; i++) f->balls[i].prev_sensor = -1;
}

static void t5_sensor(vp_field_t *f, int e, vp_ball_t *b)
{
    if (vp_is(f, e, "LaunchBarrierSensor")) vp_wall_retract(f, t5.barrier, 0);
    else if (vp_is(f, e, "LaunchBarrierRetract")) vp_wall_retract(f, t5.barrier, 1);
    else if (vp_is(f, e, "RampSensor_OuterLeftTop")) t5_check_ramp(f, b, "RampSensor_OuterRightTop", C_GREEN);
    else if (vp_is(f, e, "RampSensor_OuterRightTop")) t5_check_ramp(f, b, "RampSensor_OuterLeftTop", C_BLUE);
    else if (vp_is(f, e, "RampSensor_LeftTop")) t5_check_ramp(f, b, "RampSensor_LeftMiddle", C_RED);
    else if (vp_is(f, e, "RampSensor_RightTop")) t5_check_ramp(f, b, "RampSensor_RightMiddle", C_YELLOW);
    else if (vp_is(f, e, "Sensor_ExtraBallExit")) {
        if (vp_roll_all(f, t5.extra_roll)) {
            vp_roll_set(f, t5.extra_roll, 0, 0);
            for (int c = 0; c < C_N; c++) vp_wall_retract(f, t5.extra_bar[c], 0);
        }
    }
    b->prev_sensor = (int16_t)e;
}

static void t5_ramp_bonus(vp_field_t *f, int c, const char *name)
{
    t5.ramp_bonus[c] += 10;
    vp_messagef(f, 1500, "%s ramp +%d%%", name, t5.ramp_bonus[c]);
}

static void t5_drop_all(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    if (dg_save_targets(f, e, "DropTargetLeftSave", "DropTargetRightSave")) return;
    /* die Rampen heissen im Original nach ihrer Farbe; die Roehre hat keine, darum nach der Lage */
    if (vp_is(f, e, "DropTargets_BlueRamp")) t5_ramp_bonus(f, C_BLUE, "Outer Left");
    else if (vp_is(f, e, "DropTargets_RedRamp")) t5_ramp_bonus(f, C_RED, "Inner Left");
    else if (vp_is(f, e, "DropTargets_YellowRamp")) t5_ramp_bonus(f, C_YELLOW, "Inner Right");
    else if (vp_is(f, e, "DropTargets_GreenRamp")) t5_ramp_bonus(f, C_GREEN, "Outer Right");
}

static void t5_roll_all(vp_field_t *f, int e, vp_ball_t *b)
{
    uint16_t serial = b->serial;
    if (vp_is(f, e, "FlipperRollovers")) { vp_inc_multiplier(f, 1500); vp_roll_set_all(f, e, 0); }
    else if (e == t5.tri_center) { if (t5.mb == M5_READY) t5_start_multiball(f); }
    else if (e == t5.extra_roll) { f->extra_balls++; vp_message(f, "Extra Ball!", 3000, 1); }
    t5_update_lines(f, vp_ball_by_serial(f, serial));
}

static void t5_collision(vp_field_t *f, int e, int sub, vp_ball_t *b)
{
    (void)sub; (void)b;
    if (t5.mb == M5_READY && (e == t5.tri[0] || e == t5.tri[1] || e == t5.tri[2])) vp_wall_retract(f, e, 1);
}

static void t5_tick(vp_field_t *f, float dt)
{
    if (!t5.ready) return;
    if (t5.tri_mult != 0) {
        const float cx = 10.0f, cy = 18.86f, r = 1.1547005f;            /* gleichseitiges Dreieck, Seite 2 */
        t5.tri_angle += t5.tri_mult * 1.0f * dt;
        if (t5.tri_angle < 0) t5.tri_angle += VP_TAU;
        if (t5.tri_angle >= VP_TAU) t5.tri_angle -= VP_TAU;
        float a0 = t5.tri_angle, a1 = a0 + VP_TAU / 3, a2 = a0 - VP_TAU / 3;
        float x0 = cx + r * fm_cosf(a0), y0 = cy + r * fm_sinf(a0);
        float x1 = cx + r * fm_cosf(a1), y1 = cy + r * fm_sinf(a1);
        float x2 = cx + r * fm_cosf(a2), y2 = cy + r * fm_sinf(a2);
        vp_wall_set(f, t5.tri[0], x0, y0, x1, y1);
        vp_wall_set(f, t5.tri[1], x1, y1, x2, y2);
        vp_wall_set(f, t5.tri[2], x2, y2, x0, y0);
    }
    if (f->nballs <= 1 && t5.mb == M5_ACTIVE) {
        t5.tri_mult = 1.0f;
        vp_roll_set(f, t5.tri_center, 0, 0);
        t5_reset_center(f);
        t5.mb = M5_NOT_READY;
    }
}

const vp_delegate_t vp_delegate5 = { t5_init, t5_started, t5_ball_lost, t5_tick, t5_collision, t5_drop_all, t5_roll_all, NULL, t5_sensor };

/* ================= Tisch 6: Orbiters ================= */
enum { P_OFF, P_IN_PROGRESS, P_ON };
enum { M6_INACTIVE, M6_STARTING, M6_ACTIVE };
#define T6_MAXP 8
static const uint32_t T6_PLANET[5] = { VP_RGB(0xFF, 0x99, 0x00), VP_RGB(0x00, 0x99, 0xFF), VP_RGB(0xAA, 0x00, 0x00), VP_RGB(0x00, 0xAA, 0x66), VP_RGB(0xAA, 0x22, 0xCC) };
static const uint32_t T6_BALL[5]   = { VP_RGB(0xFF, 0xBB, 0x44), VP_RGB(0x66, 0xBB, 0xFF), VP_RGB(0xDD, 0x44, 0x44), VP_RGB(0x77, 0xDD, 0xAA), VP_RGB(0xCC, 0x88, 0xEE) };
static const uint32_t T6_BALL2[5]  = { VP_RGB(0xDD, 0x99, 0x22), VP_RGB(0x44, 0x99, 0xDD), VP_RGB(0xBB, 0x22, 0x22), VP_RGB(0x55, 0xBB, 0x88), VP_RGB(0xAA, 0x66, 0xCC) };

static struct {
    int n, elem[T6_MAXP], status[T6_MAXP], barrier, orbits, sun, mb, jackpot_mult, ramp_mult, ready;
    float radius[T6_MAXP], angle[T6_MAXP], angvel[T6_MAXP], phase, ramp_left;
} t6;

static int t6_ball_color_index(const vp_ball_t *b) { for (int i = 0; i < 5; i++) if (b->color == T6_BALL[i]) return i; return -1; }
static int t6_all_on(void) { for (int i = 0; i < t6.n; i++) if (t6.status[i] != P_ON) return 0; return 1; }

static void t6_act_launch(vp_field_t *f, int last) { vp_launch_ball(f); if (last) t6.mb = M6_ACTIVE; }

static void t6_activate(vp_field_t *f, vp_ball_t *b, int p)
{
    if (p >= t6.n || t6.status[p] == P_ON) return;
    if (t6_ball_color_index(b) != p) return;
    t6.status[p] = P_ON;
    int64_t total = 5000;
    if (t6_all_on()) {
        if (t6.mb == M6_INACTIVE) {
            dg_restore_saver(f, "BallSaver-left"); dg_restore_saver(f, "BallSaver-right");
            for (int i = 0; i < t6.n; i++) t6.status[i] = P_OFF;
            t6.mb = M6_STARTING; t6.jackpot_mult = 1;
            vp_message(f, "Multiball!", 4000, 1);
            vp_schedule(f, 1000, t6_act_launch, 0);
            vp_schedule(f, 4000, t6_act_launch, 1);
        } else {
            if (t6.jackpot_mult > 1) vp_messagef(f, 2000, "%dx Jackpot!", t6.jackpot_mult);
            else vp_message(f, "Jackpot!", 2000, 1);
            total += (int64_t)100000 * t6.jackpot_mult;
            t6.jackpot_mult++;
            for (int i = 0; i < t6.n; i++) t6.status[i] = P_OFF;
        }
    } else vp_messagef(f, 1500, "Planet %d Activated!", p + 1);
    vp_add_score(f, total);
}

static void t6_check_ramp(vp_field_t *f, vp_ball_t *b, const char *prev, int planet)
{
    if (!vp_is(f, b->last_sensor, prev)) return;
    if (t6.ramp_mult > 1) vp_messagef(f, 1000, "%dx Ramp", t6.ramp_mult);
    t6.ramp_left = 12.0f;
    vp_add_score(f, (int64_t)2500 * t6.ramp_mult);
    t6.ramp_mult++;
    if (planet >= 0) t6_activate(f, b, planet);
}

static void t6_init(vp_field_t *f)
{
    char id[16];
    memset(&t6, 0, sizeof t6);
    t6.barrier = vp_find(f, "LaunchBarrier");
    t6.sun = vp_find(f, "Sun"); t6.orbits = vp_find(f, "Orbits");
    vp_roll_set_all(f, t6.sun, 1);
    t6.n = f->def->elems[t6.orbits].count;
    if (t6.n > 5) t6.n = 5;
    for (int i = 0; i < t6.n; i++) {
        snprintf(id, sizeof id, "Planet%d", i + 1);
        t6.elem[i] = vp_find(f, id);
        t6.radius[i] = vp_roll(f, t6.elem[i], 0)->r;
        t6.angle[i] = vp_randf(f) * VP_TAU;
        t6.angvel[i] = (0.9f + 0.2f * vp_randf(f)) / (i + 1);       /* innere Planeten laufen schneller */
        t6.status[i] = P_OFF;
    }
    t6.ramp_mult = 1; t6.jackpot_mult = 1;
    t6.ready = 1;
}

static void t6_started(vp_field_t *f) { t6_init(f); vp_wall_retract(f, t6.barrier, 1); }
static void t6_ball_lost(vp_field_t *f) { vp_wall_retract(f, t6.barrier, 0); }

static void t6_sensor(vp_field_t *f, int e, vp_ball_t *b)
{
    if (vp_is(f, e, "LaunchBarrierSensor")) vp_wall_retract(f, t6.barrier, 0);
    else if (vp_is(f, e, "LaunchBarrierRetract")) vp_wall_retract(f, t6.barrier, 1);
    else if (vp_is(f, e, "LeftLoopDetector_Trigger")) t6_check_ramp(f, b, "LeftLoopDetector_Enter", 3);
    else if (vp_is(f, e, "RightLoopDetector_Trigger")) t6_check_ramp(f, b, "RightLoopDetector_Enter", 2);
    else if (vp_is(f, e, "OrbitDetector_Left")) t6_check_ramp(f, b, "OrbitDetector_Right", 4);
    else if (vp_is(f, e, "OrbitDetector_Right")) t6_check_ramp(f, b, "OrbitDetector_Left", 4);
}

static void t6_drop_all(vp_field_t *f, int e, vp_ball_t *b)
{
    if (dg_save_targets(f, e, "DropTargetLeftSave", "DropTargetRightSave")) return;
    if (vp_is(f, e, "Planet1Targets")) { vp_add_score(f, 5000); t6_activate(f, b, 0); }
}

static void t6_roll_all(vp_field_t *f, int e, vp_ball_t *b)
{
    if (vp_is(f, e, "FlipperRollovers")) { vp_roll_set_all(f, e, 0); vp_inc_multiplier(f, 1500); }
    else if (vp_is(f, e, "Planet2Rollovers")) { vp_roll_set_all(f, e, 0); vp_add_score(f, 5000); t6_activate(f, b, 1); }
    else {
        for (int i = 0; i < t6.n; i++)
            if (e == t6.elem[i]) { b->color = T6_BALL[i]; b->color2 = T6_BALL2[i]; break; }
    }
}

static void t6_tick(vp_field_t *f, float dt)
{
    if (!t6.ready) return;
    if (f->in_progress && f->nballs <= 1 && t6.mb == M6_ACTIVE) {
        t6.mb = M6_INACTIVE;
        for (int i = 0; i < t6.n; i++) t6.status[i] = P_OFF;
    }
    /* Planeten: Zustand an die Ballfarben anpassen, laufende blinken */
    t6.phase += VP_TAU * dt / 4.0f;
    while (t6.phase > VP_TAU) t6.phase -= VP_TAU;
    for (int i = 0; i < t6.n; i++) {
        if (t6.status[i] != P_ON) {
            int any = 0;
            for (int k = 0; k < f->nballs; k++) if (f->balls[k].color == T6_BALL[i]) any = 1;
            t6.status[i] = any ? P_IN_PROGRESS : P_OFF;
        }
        vp_roll_set_all(f, t6.elem[i], t6.status[i] != P_OFF);
        float ph = (t6.status[i] == P_IN_PROGRESS) ? (1 + fm_sinf(t6.phase)) * 0.35f : 0;
        vp_roll_t *r = vp_roll(f, t6.elem[i], 0);
        r->color = vp_blend(T6_PLANET[i], VP_RGB(0, 0, 0), ph); r->has_color = 1;
    }
    if (t6.ramp_left > 0) t6.ramp_left -= dt;
    if (t6.ramp_left <= 0) t6.ramp_mult = 1;
    for (int i = 0; i < t6.n; i++) {
        t6.angle[i] += dt * t6.angvel[i];
        while (t6.angle[i] > VP_TAU) t6.angle[i] -= VP_TAU;
        const vp_roll_t *o = vp_roll(f, t6.orbits, i);
        vp_roll_t *r = vp_roll(f, t6.elem[i], 0);
        r->x = o->x + o->r * fm_cosf(t6.angle[i]); r->y = o->y + o->r * fm_sinf(t6.angle[i]);
    }
    /* Anziehung von Sonne und Planeten (sie liegen "unter" dem Ball: Tiefe 2) */
    if (f->in_progress) {
        const vp_roll_t *sun = vp_roll(f, t6.sun, 0);
        for (int k = 0; k < f->nballs; k++) {
            vp_ball_t *b = &f->balls[k];
            float sdx = sun->x - b->x, sdy = sun->y - b->y, sd2 = sdx * sdx + sdy * sdy;
            if (sd2 > 64.0f) continue;
            float sd = sqrtf(sd2), force = 8.0f / (4.0f + sd2);
            float ix = sd > 1e-6f ? dt * force * sdx / sd : 0, iy = sd > 1e-6f ? dt * force * sdy / sd : 0;
            for (int i = 0; i < t6.n; i++) {
                const vp_roll_t *p = vp_roll(f, t6.elem[i], 0);
                float pdx = p->x - b->x, pdy = p->y - b->y, pd2 = pdx * pdx + pdy * pdy, pd = sqrtf(pd2);
                float mass = t6.radius[i] * t6.radius[i] * t6.radius[i];
                float pf = 15.0f * mass / (4.0f + pd2);
                if (pd > 1e-6f) { ix += dt * pf * pdx / pd; iy += dt * pf * pdy / pd; }
            }
            vp_ball_impulse(b, ix, iy);
        }
    }
}

const vp_delegate_t vp_delegate6 = { t6_init, t6_started, t6_ball_lost, t6_tick, NULL, t6_drop_all, t6_roll_all, NULL, t6_sensor };
