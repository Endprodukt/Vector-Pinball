/* vp_table1.c - Tische 1 bis 4 (Field1Delegate .. Field4Delegate) */
/* nicht zeitkritisch: auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp (das Kit baut sonst mit -O3) */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include "vp_deleg.h"
#include "fmath.h"

const vp_delegate_t *vp_delegate_for(int n)
{
    static const vp_delegate_t *const all[] = { &vp_delegate1, &vp_delegate2, &vp_delegate3, &vp_delegate4, &vp_delegate5,
                                                 &vp_delegate6, &vp_delegate7, &vp_delegate8, &vp_delegate9, &vp_delegate10 };
    return (n >= 1 && n <= 10) ? all[n - 1] : NULL;
}

static void common_sensor(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    if (vp_is(f, e, "LaunchBarrierSensor")) dg_launch_barrier(f, 1);
    else if (vp_is(f, e, "LaunchBarrierRetract")) dg_launch_barrier(f, 0);
}
static void common_reset_barrier(vp_field_t *f) { dg_launch_barrier(f, 0); }

static void start_multiball_3(vp_field_t *f, int msg_ms)
{
    vp_message(f, "Multiball!", msg_ms, 1);
    dg_restore_saver(f, "BallSaver-left");
    dg_restore_saver(f, "BallSaver-right");
    vp_schedule(f, 1000, dg_act_launch_max3, 0);
    vp_schedule(f, 3500, dg_act_launch_max3, 0);
}

/* ================= Tisch 1: Classic ================= */
static void t1_roll_all(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    vp_roll_set_all(f, e, 0);
    vp_inc_multiplier(f, 1500);
    if (vp_is(f, e, "RampRollovers")) {
        int x = vp_find(f, "ExtraBallRollovers");
        if (x >= 0 && vp_roll_all(f, x)) { vp_roll_set_all(f, x, 0); start_multiball_3(f, 2000); }
    }
}

static void t1_drop_all(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    dg_save_targets(f, e, "DropTargetLeftSave", "DropTargetRightSave");
    int x = vp_find(f, "ExtraBallRollovers");
    if (x >= 0 && !vp_roll_all(f, x)) {
        vp_roll_activate_first(f, x);
        if (vp_roll_all(f, x)) vp_message(f, "Shoot Ramp for Multiball", 1500, 1);
    }
}

const vp_delegate_t vp_delegate1 = { NULL, common_reset_barrier, common_reset_barrier, NULL, NULL, t1_drop_all, t1_roll_all, NULL, common_sensor };

/* ================= Tisch 2: Round and Round ================= */
/* Werte aus "variables" in table2.json */
#define T2_SPEED1 0.75f
#define T2_SPEED2 (-0.5f)
#define T2_CX2    9.35f
#define T2_CY2    13.0f

static struct { int ids[4], n; float cx, cy, radius, angle, speed; } t2g[2];

static void t2_group(vp_field_t *f, int g, const char *const *ids, int n, float cx, float cy, float speed)
{
    t2g[g].n = n; t2g[g].cx = cx; t2g[g].cy = cy; t2g[g].speed = speed;
    for (int i = 0; i < n; i++) t2g[g].ids[i] = vp_find(f, ids[i]);
    const vp_estate_t *s = &f->es[t2g[g].ids[0]];
    t2g[g].radius = hypotf(s->cx - cx, s->cy - cy);
    t2g[g].angle = atan2f(s->cy - cy, s->cx - cx);
}

static void t2_setup(vp_field_t *f)
{
    static const char *const g1[] = { "RotatingBumper1A", "RotatingBumper1B", "RotatingBumper1C", "RotatingBumper1D" };
    static const char *const g2[] = { "RotatingBumper2A", "RotatingBumper2B" };
    int c = vp_find(f, "CenterBumper1");
    t2_group(f, 0, g1, 4, f->es[c].cx, f->es[c].cy, T2_SPEED1);
    t2_group(f, 1, g2, 2, T2_CX2, T2_CY2, T2_SPEED2);
}

static void t2_started(vp_field_t *f) { dg_launch_barrier(f, 0); }

static void t2_tick(vp_field_t *f, float dt)
{
    for (int g = 0; g < 2; g++) {
        t2g[g].angle += dt * t2g[g].speed;
        if (t2g[g].angle > VP_TAU) t2g[g].angle -= VP_TAU;
        if (t2g[g].angle < 0) t2g[g].angle += VP_TAU;
        for (int i = 0; i < t2g[g].n; i++) {
            float a = t2g[g].angle + VP_TAU / t2g[g].n * i;
            vp_estate_t *s = &f->es[t2g[g].ids[i]];
            s->cx = t2g[g].cx + t2g[g].radius * fm_cosf(a);
            s->cy = t2g[g].cy + t2g[g].radius * fm_sinf(a);
        }
    }
}

static void t2_roll_all(vp_field_t *f, int e, vp_ball_t *b) { (void)b; vp_roll_set_all(f, e, 0); vp_inc_multiplier(f, 1500); }

static void t2_collision(vp_field_t *f, int e, int sub, vp_ball_t *b)
{
    (void)sub; (void)b;
    if (vp_is(f, e, "CenterBumper1")) {
        int x = vp_find(f, "ExtraBallRollovers");
        if (x >= 0 && vp_roll_all(f, x)) { start_multiball_3(f, 2000); vp_roll_set_all(f, x, 0); }
    }
}

static void t2_drop_all(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    int start = -1;
    if (vp_is(f, e, "DropTargetLeft")) { dg_restore_saver(f, "BallSaver-left"); vp_message(f, "Left Save Enabled", 1500, 1); start = 0; }
    else if (vp_is(f, e, "DropTargetRight")) { dg_restore_saver(f, "BallSaver-right"); vp_message(f, "Right Save Enabled", 1500, 1); start = 2; }
    else if (vp_is(f, e, "DropTargetTopLeft")) start = 1;
    if (start >= 0) {
        int x = vp_find(f, "ExtraBallRollovers");
        int n = x >= 0 ? f->def->elems[x].count : 0;
        while (start < n) {
            if (!vp_roll_active(f, x, start)) {
                vp_roll_set(f, x, start, 1);
                if (vp_roll_all(f, x)) vp_message(f, "Shoot Top Left Bumper", 1500, 1);   /* im Original "Red": die Roehre hat keine Farbe */
                break;
            }
            start += 3;
        }
    }
}

static void t2_load_reset(vp_field_t *f) { dg_launch_barrier(f, 0); }
const vp_delegate_t vp_delegate2 = { t2_setup, t2_started, t2_load_reset, t2_tick, t2_collision, t2_drop_all, t2_roll_all, NULL, common_sensor };

/* ================= Tisch 3: Ringmaster ================= */
#define T3_BASE_BUMPER_SCORE 500
enum { MB_INACTIVE, MB_PENDING, MB_ACTIVE, MB_STARTING, MB_READY, MB_NOT_READY };

static uint32_t t3_temp_color(float ratio)
{
    /* Blau -> Tuerkis (16er), Tuerkis -> Gruen (32er), Gruen -> Gelb (32er), Gelb -> Rot (12er): 53 Stufen wie im Original */
    int i;
    if (ratio <= 0) i = 0; else if (ratio >= 1) i = 52; else i = (int)lroundf(52 * ratio);
    if (i < 16) return VP_RGB(0, 16 * i, 255);
    if (i < 24) return VP_RGB(0, 255, 240 - 32 * (i - 16));
    if (i < 32) return VP_RGB(16 + 32 * (i - 24), 255, 0);
    return VP_RGB(255, 240 - 12 * (i - 32), 0);
}

static struct {
    float bonus_dur, base_bonus_dur, bonus_elapsed, energy, max_energy;
    int bonus_active, bonus_mult, base_bonus_mult, upper_done, lower_done, max_upper, max_lower, mb;
    float flash_val[3], flash_inc[3];
    int upper_ind, lower_ind, bumper_ind;
} t3;

static void t3_bumper_colors(vp_field_t *f, int has, uint32_t c)
{
    for (int k = 0; k < f->def->nelem; k++)
        if (f->def->elems[k].type == VP_BUMPER) { vp_set_color(f, k, has, c); vp_set_outer(f, k, has, vp_with_alpha(c, 128)); }
}

static void t3_reset_bonuses(vp_field_t *f)
{
    t3.bonus_dur = t3.base_bonus_dur; t3.bonus_mult = t3.base_bonus_mult;
    t3.energy = 0; t3.upper_done = t3.lower_done = 0;
    vp_set_color(f, t3.upper_ind, 0, 0); vp_set_color(f, t3.lower_ind, 0, 0); vp_set_color(f, t3.bumper_ind, 0, 0);
    vp_roll_set_all(f, t3.bumper_ind, 0);
}

static void t3_check_multiball(vp_field_t *f)
{
    if (t3.energy >= t3.max_energy && t3.upper_done >= t3.max_upper && t3.lower_done >= t3.max_lower) vp_roll_set_all(f, t3.bumper_ind, 1);
}

static void t3_act_launch(vp_field_t *f, int arg)
{
    (void)arg;
    if (f->nballs < 3) vp_launch_ball(f);
    if (t3.mb != MB_ACTIVE) {
        t3.mb = MB_ACTIVE;
        for (int i = 0; i < 3; i++) { t3.flash_val[i] = 0; t3.flash_inc[i] = 0.2f + 0.3f * vp_randf(f); }
    }
}

static void t3_roll_all(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    if (vp_is(f, e, "LeftRampRollover") || vp_is(f, e, "RightRampRollover")) {
        t3.bonus_active = 1; t3.bonus_elapsed = 0;
        if (t3.mb == MB_INACTIVE && vp_roll_active(f, t3.bumper_ind, 0)) {
            vp_message(f, "Multiball!", 2000, 1);
            t3.mb = MB_PENDING;
            dg_restore_saver(f, "BallSaver-left"); dg_restore_saver(f, "BallSaver-right");
            vp_schedule(f, 1000, t3_act_launch, 0);
            vp_schedule(f, 3500, t3_act_launch, 0);
            t3.bonus_mult += 1; t3.bonus_dur += 1.0f;
        }
        if (t3.mb == MB_ACTIVE) vp_add_score(f, f->es[e].score);
    } else {
        vp_roll_set_all(f, e, 0);
        vp_inc_multiplier(f, 1500);
    }
}

static void t3_drop_all(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    if (dg_save_targets(f, e, "DropTargetLeftSave", "DropTargetRightSave")) return;
    if (vp_is(f, e, "LowerMultiballTargets")) {
        if (t3.lower_done < t3.max_lower) {
            t3.bonus_dur += 1.0f; t3.lower_done++;
            vp_set_color(f, t3.lower_ind, 1, t3_temp_color((float)t3.lower_done / t3.max_lower));
            t3_check_multiball(f);
        }
    } else if (vp_is(f, e, "UpperMultiballTargets")) {
        if (t3.upper_done < t3.max_upper) {
            t3.bonus_mult += 1; t3.upper_done++;
            vp_set_color(f, t3.upper_ind, 1, t3_temp_color((float)t3.upper_done / t3.max_upper));
            t3_check_multiball(f);
        }
    }
}

static void t3_tick(vp_field_t *f, float dt)
{
    if (t3.bonus_active) {
        t3.bonus_elapsed += dt;
        if (t3.bonus_elapsed >= t3.bonus_dur) { t3.bonus_active = 0; t3_bumper_colors(f, 0, 0); }
        else t3_bumper_colors(f, 1, t3_temp_color(1 - t3.bonus_elapsed / t3.bonus_dur));
    }
    if (t3.mb == MB_ACTIVE) {
        if (f->nballs <= 1) { t3_reset_bonuses(f); t3.mb = MB_INACTIVE; }
        else {
            const int ind[3] = { t3.upper_ind, t3.lower_ind, t3.bumper_ind };
            for (int i = 0; i < 3; i++) {
                float v = t3.flash_val[i] + dt * t3.flash_inc[i];
                if (v > 2) v -= 2;
                t3.flash_val[i] = v;
                vp_set_color(f, ind[i], 1, t3_temp_color(fabsf(v - 1)));
            }
        }
    }
}

static void t3_collision(vp_field_t *f, int e, int sub, vp_ball_t *b)
{
    (void)sub; (void)b;
    if (f->def->elems[e].type != VP_BUMPER) return;
    float extra = 0;
    int64_t score = T3_BASE_BUMPER_SCORE;
    if (t3.bonus_active) {
        extra = (1 - t3.bonus_elapsed / t3.bonus_dur) * t3.bonus_mult;
        score += (int64_t)lroundf(T3_BASE_BUMPER_SCORE * extra / 10) * 10;
    }
    vp_add_score(f, score);
    f->sfx |= VSFX_SCORE; f->score_hits++;
    t3.energy += 1 + extra;
    if (t3.energy > t3.max_energy) t3.energy = t3.max_energy;
    vp_set_color(f, t3.bumper_ind, 1, t3_temp_color(t3.energy / t3.max_energy));
    t3_check_multiball(f);
}

static void t3_init(vp_field_t *f)
{
    memset(&t3, 0, sizeof t3);
    t3.upper_ind = vp_find(f, "UpperTargetIndicator"); t3.lower_ind = vp_find(f, "LowerTargetIndicator"); t3.bumper_ind = vp_find(f, "BumperIndicator");
    t3.max_energy = 200; t3.max_upper = 2; t3.max_lower = 5;
    t3.base_bonus_dur = 15; t3.base_bonus_mult = 5;
    t3_reset_bonuses(f);
    t3_bumper_colors(f, 0, 0);
    t3.mb = MB_INACTIVE;
}

static void t3_started(vp_field_t *f) { dg_launch_barrier(f, 0); t3_init(f); }

static void t3_ball_lost(vp_field_t *f)
{
    dg_launch_barrier(f, 0);
    t3.bonus_active = 0; t3_bumper_colors(f, 0, 0);
    if (t3.mb == MB_ACTIVE) { t3_reset_bonuses(f); t3.mb = MB_INACTIVE; }
}

const vp_delegate_t vp_delegate3 = { t3_init, t3_started, t3_ball_lost, t3_tick, t3_collision, t3_drop_all, t3_roll_all, NULL, common_sensor };

/* ================= Tisch 4: Compound Interest ================= */
static const char *const T4_COLORS[6] = { "Blue", "Cyan", "Green", "Yellow", "Red", "Magenta" };
static struct {
    int barrier, top_blocker, right_kicker, lock_roll[3], lock_kick[3], status[6];
    int in_multiball, starting, bumper_inc, balls_locked;
    int64_t jackpot;
    int ready;
} t4;

static void t4_status_all(vp_field_t *f, int active) { for (int i = 0; i < 6; i++) for (int k = 0; k < 3; k++) vp_roll_set(f, t4.status[i], k, active); }
static void t4_status_ignore(vp_field_t *f, int ign) { for (int i = 0; i < 6; i++) f->es[t4.status[i]].ignore_ball = (uint8_t)ign; }
static void t4_lock_show(vp_field_t *f, int i, int on) { f->es[t4.lock_roll[i]].ignore_ball = (uint8_t)!on; f->es[t4.lock_roll[i]].visible = (uint8_t)on; }

static void t4_clear_multiball(vp_field_t *f)
{
    t4_status_all(f, 0); t4_status_ignore(f, 1);
    t4.balls_locked = 0; t4.in_multiball = 0; t4.starting = 0;
}

static void t4_bumper_inc(vp_field_t *f, int percent)
{
    t4.bumper_inc += percent;
    vp_messagef(f, 1500, "Bumper Multiplier %d%%", t4.bumper_inc);
}

static void t4_act_mb1(vp_field_t *f, int arg)
{
    (void)arg;
    vp_wall_retract(f, t4.lock_kick[1], 1); vp_wall_retract(f, t4.lock_kick[2], 1);
    t4_lock_show(f, 1, 0);
    if (f->nballs < 3) vp_launch_ball(f);
}
static void t4_act_mb2(vp_field_t *f, int arg)
{
    (void)arg;
    t4.starting = 0;
    t4_lock_show(f, 0, 0);
    if (f->nballs < 3) vp_launch_ball(f);
}

static void t4_start_multiball(vp_field_t *f)
{
    vp_message(f, "Multiball!", 3000, 1);
    dg_restore_saver(f, "BallSaver-left"); dg_restore_saver(f, "BallSaver-right");
    t4_lock_show(f, 2, 0);
    t4.balls_locked = 3; t4.in_multiball = 1; t4.starting = 1;
    t4_status_all(f, 0); t4_status_ignore(f, 0);
    t4.jackpot = 25;
    vp_schedule(f, 1000, t4_act_mb1, 0);
    vp_schedule(f, 3500, t4_act_mb2, 0);
}

static void t4_started(vp_field_t *f)
{
    char id[32];
    memset(&t4, 0, sizeof t4);
    t4.barrier = vp_find(f, "LaunchBarrier"); t4.top_blocker = vp_find(f, "TopRampBlocker"); t4.right_kicker = vp_find(f, "RightRampKicker");
    for (int i = 0; i < 3; i++) {
        snprintf(id, sizeof id, "LockedBallRollover%d", i + 1); t4.lock_roll[i] = vp_find(f, id);
        snprintf(id, sizeof id, "LockedBallKicker%d", i + 1);   t4.lock_kick[i] = vp_find(f, id);
        vp_roll_set(f, t4.lock_roll[i], 0, 0);
        t4_lock_show(f, i, 0);
    }
    vp_wall_retract(f, t4.lock_kick[0], 0);
    vp_wall_retract(f, t4.lock_kick[1], 1); vp_wall_retract(f, t4.lock_kick[2], 1);
    for (int i = 0; i < 6; i++) { snprintf(id, sizeof id, "Rollovers.%s", T4_COLORS[i]); t4.status[i] = vp_find(f, id); }
    t4.bumper_inc = 1;
    t4.ready = 1;
    t4_clear_multiball(f);
    vp_wall_retract(f, t4.barrier, 1);
    vp_wall_retract(f, t4.right_kicker, 1);
}

static void t4_ball_lost(vp_field_t *f) { vp_wall_retract(f, t4.barrier, 0); t4.bumper_inc = 1; }

static void t4_sensor(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    if (vp_is(f, e, "LaunchBarrierRetract")) vp_wall_retract(f, t4.barrier, 1);
    else if (vp_is(f, e, "TopRampSensor")) { vp_wall_retract(f, t4.barrier, 0); vp_wall_retract(f, t4.top_blocker, 1); vp_wall_retract(f, t4.right_kicker, 0); }
    else if (vp_is(f, e, "RightRampSensor")) { vp_wall_retract(f, t4.top_blocker, 0); vp_wall_retract(f, t4.right_kicker, 1); }
    else if (vp_is(f, e, "AfterRampKickerSensor")) { vp_wall_retract(f, t4.top_blocker, 0); vp_wall_retract(f, t4.right_kicker, 0); }
}

static void t4_drop_all(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    if (dg_save_targets(f, e, "DropTargetLeftSave", "DropTargetRightSave")) return;
    if (vp_is(f, e, "DropTargetTop")) t4_bumper_inc(f, 1);
}

static void t4_collision(vp_field_t *f, int e, int sub, vp_ball_t *b)
{
    (void)sub; (void)b;
    if (!vp_starts(f, e, "Bumper.")) return;
    const char *suffix = f->def->elems[e].id + 7;
    long m = lround(f->mult * 100) + t4.bumper_inc;
    f->mult = m / 100.0;
    if (t4.balls_locked < 3) {
        int all = 1;
        for (int i = 0; i < 6; i++) {
            if (!strcmp(suffix, T4_COLORS[i])) vp_roll_set(f, t4.status[i], t4.balls_locked, 1);
        }
        for (int i = 0; i < 6; i++) if (!vp_roll_active(f, t4.status[i], t4.balls_locked)) all = 0;
        if (all && f->es[t4.lock_roll[t4.balls_locked]].ignore_ball) {
            t4_lock_show(f, t4.balls_locked, 1);
            vp_roll_set(f, t4.lock_roll[t4.balls_locked], 0, 0);
            vp_message(f, t4.balls_locked == 2 ? "Multiball Ready" : "Ball Lock Ready", 3000, 1);
        }
    }
}

static void t4_roll_all(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    int is_status = 0;
    for (int i = 0; i < 6; i++) if (e == t4.status[i]) is_status = 1;
    if (e == t4.lock_roll[0] || e == t4.lock_roll[1]) {
        int n = (e == t4.lock_roll[0]) ? 1 : 2;
        if (f->nballs > 0) vp_remove_ball_quiet(f, &f->balls[0]);
        vp_wall_retract(f, t4.lock_kick[n], 0);
        vp_messagef(f, 3000, "Ball %d Locked", n);
        t4.balls_locked = n;
    } else if (e == t4.lock_roll[2]) {
        t4_start_multiball(f);
    } else if (t4.in_multiball && is_status) {
        int jack = 1;
        for (int i = 0; i < 6; i++) if (!vp_roll_all(f, t4.status[i])) jack = 0;
        if (jack) {
            vp_add_score(f, t4.jackpot * 1000);
            vp_message(f, "Jackpot!", 3000, 1);
            f->mult = (double)(int64_t)(f->mult * (100 + t4.jackpot)) / 100.0;
            t4.jackpot += 5;
            t4_status_all(f, 0);
        }
    } else if (vp_is(f, e, "FlipperRollovers")) {
        t4_bumper_inc(f, 2);
        vp_roll_set_all(f, e, 0);
    }
}

static void t4_tick(vp_field_t *f, float dt)
{
    (void)dt;
    if (t4.ready && t4.in_multiball && !t4.starting && f->nballs <= 1) t4_clear_multiball(f);
}

static void t4_init(vp_field_t *f) { (void)f; memset(&t4, 0, sizeof t4); }
const vp_delegate_t vp_delegate4 = { t4_init, t4_started, t4_ball_lost, t4_tick, t4_collision, t4_drop_all, t4_roll_all, NULL, t4_sensor };
