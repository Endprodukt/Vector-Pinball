/* vp_table7.c - Tische 7 (Ad Astra, Sternkarte) und 8 (Pool Party): Field7Delegate, Field8Delegate */
/* nicht zeitkritisch: auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp (das Kit baut sonst mit -O3) */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include "vp_deleg.h"
#include "vp_stars.h"
#include "fmath.h"

/* ================= Tisch 7: Ad Astra ================= */
#define T7_MAXSTARS 1536
#define T7_MAXPROJ  320
#define T7_MAX_DOTS 48               /* so viele Sterne gehen hoechstens als Formen an die Anzeige (hellste zuerst) */
enum { SM_WANDERING, SM_CONSTELLATION };
enum { M7_INACTIVE, M7_STARTING, M7_ACTIVE };
#define BALL_LOCK_LAYER 4
#define MINITABLE_LAYER 1

typedef struct { float ra, dec, rad; } target_t;

static struct {
    uint32_t activated[T7_MAXSTARS / 32];
    uint8_t  locked;                 /* Bit je Sternbild */
    int      cur, mode;
    float    wander, anim_elapsed;
    target_t t, from, to;
    int      nproj;
    float    px[T7_MAXPROJ], py[T7_MAXPROJ], pmag[T7_MAXPROJ];
    uint16_t pidx[T7_MAXPROJ];
    int16_t  map[T7_MAXSTARS];

    int   mb, balls_locked, ready;
    int64_t ramp_score;
    float view_x, view_y, view_r, guide_t;
    int   lock_roll[3], guide_l, guide_r, guide_lock, saver_l, saver_r;
    target_t proj;                   /* Blickpunkt, fuer den px/py/map gelten */
    uint32_t last_ms;                /* Echtzeit des letzten Bildes, in dem Projektion und Formen erneuert wurden */
    uint8_t  shapes_due;
} t7;

static int  star_on(int i) { return (t7.activated[i >> 5] >> (i & 31)) & 1; }
static void star_set(int i) { t7.activated[i >> 5] |= 1u << (i & 31); }
static int  in_const(int c, int star) { for (int k = 0; k < vp_consts[c].nidx; k++) if (vp_consts[c].idx[k] == star) return 1; return 0; }

static int all_stars_active(int c)
{
    if (c < 0) return 0;
    for (int k = 0; k < vp_consts[c].nidx; k++) if (!star_on(vp_consts[c].idx[k])) return 0;
    return 1;
}
static int all_locked(void) { return t7.locked == (1u << vp_nconst) - 1; }

static void project(void)
{
    float r2 = t7.t.rad * t7.t.rad;
    float sr = fm_sinf(t7.t.ra), cr = fm_cosf(t7.t.ra), sd = fm_sinf(t7.t.dec), cd = fm_cosf(t7.t.dec);
    t7.nproj = 0;
    for (int i = 0; i < vp_nstars && i < T7_MAXSTARS; i++) {
        const vp_star_t *s = &vp_stars[i];
        /* erst um Z (Rektaszension), dann um Y (Deklination): der Blickpunkt liegt danach bei (1,0,0) */
        const float k = 1.0f / 32767.0f, sx = s->x * k, sy = s->y * k, sz = s->z * k;
        float x1 = sx * cr - sy * sr, y1 = sx * sr + sy * cr;
        float x2 = x1 * cd + sz * sd, z2 = -x1 * sd + sz * cd;
        t7.map[i] = -1;
        if (x2 > 0 && y1 * y1 + z2 * z2 < r2 && t7.nproj < T7_MAXPROJ) {
            int n = t7.nproj++;
            t7.px[n] = y1; t7.py[n] = z2; t7.pmag[n] = s->mag * (1.0f / 256.0f); t7.pidx[n] = (uint16_t)i; t7.map[i] = (int16_t)n;
        }
    }
}

static void animate_from_here(void) { t7.from = t7.t; t7.anim_elapsed = 0; }

static void enter_wandering(void)
{
    if (t7.mode != SM_WANDERING) {
        t7.cur = -1;
        animate_from_here();
        t7.to.ra = 0; t7.to.dec = 0; t7.to.rad = 0.4f;
        t7.mode = SM_WANDERING;
        t7.wander = 0;
    }
}

static void reset_and_wander(void) { memset(t7.activated, 0, sizeof t7.activated); t7.locked = 0; enter_wandering(); }
static void lock_current(void) { if (t7.cur >= 0) t7.locked |= (uint8_t)(1u << t7.cur); }

static int switch_to_random(vp_field_t *f)
{
    int cand[8], n = 0;
    for (int c = 0; c < vp_nconst; c++) if (c != t7.cur && !(t7.locked & (1u << c))) cand[n++] = c;
    if (!n) return 0;
    int c = cand[vp_rand(f) % (uint32_t)n];
    t7.cur = c;
    animate_from_here();
    t7.to.ra = vp_consts[c].ra; t7.to.dec = vp_consts[c].dec; t7.to.rad = 1.2f * vp_consts[c].radius;
    if (t7.from.ra > t7.to.ra + VP_TAU / 2) t7.from.ra -= VP_TAU;          /* den kuerzeren Weg drehen */
    else if (t7.from.ra < t7.to.ra - VP_TAU / 2) t7.from.ra += VP_TAU;
    t7.mode = SM_CONSTELLATION;
    return 1;
}

static void stars_tick(float dt)
{
    if (t7.anim_elapsed >= 0) {
        t7.anim_elapsed += dt;
        float fr = t7.anim_elapsed / 10.0f;
        if (fr > 1) fr = 1;
        t7.t.ra = t7.from.ra + fr * (t7.to.ra - t7.from.ra);
        t7.t.dec = t7.from.dec + fr * (t7.to.dec - t7.from.dec);
        t7.t.rad = t7.from.rad + fr * (t7.to.rad - t7.from.rad);
        if (t7.anim_elapsed >= 10.0f) t7.anim_elapsed = -1;
    } else if (t7.mode == SM_WANDERING) {
        t7.wander = fmodf(t7.wander + dt, 40.0f);
        t7.t.ra = VP_TAU * (1 - t7.wander / 40.0f);
        t7.t.dec = 0; t7.t.rad = 0.4f;
    } else if (t7.cur >= 0) {
        t7.t.ra = vp_consts[t7.cur].ra; t7.t.dec = vp_consts[t7.cur].dec; t7.t.rad = 1.2f * vp_consts[t7.cur].radius;
    }
}

/* Die Projektion aller 1536 Sterne und der Aufbau der Formen laufen einmal je BILD, nicht in jedem Logiktakt (120 Hz),
 * und die Projektion nur, wenn der Blickpunkt sich bewegt hat (beim Wandern jedes Bild, sonst gar nicht) */
static void stars_frame(const vp_field_t *f)
{
    if (f->now_ms == t7.last_ms) return;
    t7.last_ms = f->now_ms;
    if (t7.t.ra != t7.proj.ra || t7.t.dec != t7.proj.dec || t7.t.rad != t7.proj.rad) { t7.proj = t7.t; project(); }
    t7.shapes_due = 1;
}

#define T7_OTHER_STARS 12
static void t7_shapes(vp_field_t *f)
{
    float scale = t7.view_r / t7.t.rad, base = t7.view_r * 0.015f;
    int dots = 0;
    f->nshapes = 0;
    for (int pass = 0; pass < 2; pass++) {                                 /* erst das aktive Sternbild, dann der Rest (hellste zuerst) */
        int others = 0;
        for (int i = 0; i < t7.nproj && dots < T7_MAX_DOTS; i++) {
            int star = t7.pidx[i];
            int in_cur = t7.cur >= 0 && in_const(t7.cur, star);
            if (in_cur != (pass == 0)) continue;
            /* das aktive Sternbild immer ganz, vom Rest nur die hellsten (weniger Punkte = weniger Flimmern), ausser er gehoert
             * zu einem schon aufgeleuchteten Sternbild */
            if (pass == 1 && !star_on(star) && ++others > T7_OTHER_STARS) continue;
            float mag = t7.pmag[i];
            int alpha = mag <= 0 ? 255 : (int)(255 - 30 * mag);
            if (alpha < 0) alpha = 0;
            int act = star_on(star);
            uint32_t c = in_cur ? (act ? VP_RGB(240, 240, 0) : VP_RGB(0, 240, 0)) : (act ? VP_RGB(192, 192, 0) : VP_RGB(240, 240, 240));
            float rmul = mag <= 0 ? 1.5f : (mag >= 4 ? 0.75f : 1.0f);
            vp_shape_circle(f, t7.view_x + t7.px[i] * scale, t7.view_y + t7.py[i] * scale, rmul * base, 1, vp_with_alpha(c, alpha));
            dots++;
        }
    }
    for (int c = 0; c < vp_nconst; c++)
        for (int k = 0; k < vp_consts[c].nseg; k++) {
            int a = vp_consts[c].seg[2 * k], b = vp_consts[c].seg[2 * k + 1];
            if (a == b || !star_on(a) || !star_on(b)) continue;
            int pa = t7.map[a], pb = t7.map[b];
            if (pa < 0 || pb < 0) continue;
            vp_shape_line(f, t7.view_x + t7.px[pa] * scale, t7.view_y + t7.py[pa] * scale,
                          t7.view_x + t7.px[pb] * scale, t7.view_y + t7.py[pb] * scale, VP_RGBA(240, 240, 240, 192));
        }
}

static void t7_init(vp_field_t *f)
{
    char id[24];
    memset(&t7, 0, sizeof t7);
    t7.cur = -1; t7.mode = SM_WANDERING; t7.anim_elapsed = -1;
    t7.t.rad = 0.4f;
    int bnd = vp_find(f, "StarViewBoundary");
    t7.view_x = vp_roll(f, bnd, 0)->x; t7.view_y = vp_roll(f, bnd, 0)->y; t7.view_r = vp_roll(f, bnd, 0)->r;
    for (int i = 0; i < 3; i++) { snprintf(id, sizeof id, "BallLockRollover%d", i + 1); t7.lock_roll[i] = vp_find(f, id); }
    t7.guide_l = vp_find(f, "LeftLoopGuide"); t7.guide_r = vp_find(f, "RightLoopGuide"); t7.guide_lock = vp_find(f, "LockAndJackpotGuide");
    t7.saver_l = vp_find(f, "BallSaver-left"); t7.saver_r = vp_find(f, "BallSaver-right");
    t7.mb = M7_INACTIVE; t7.ramp_score = 5000;
    t7.ready = 1;
    t7.last_ms = 0xFFFFFFFFu; t7.proj.rad = -1;                       /* erstes Bild: projizieren und Formen bauen */
}

static void t7_started(vp_field_t *f) { t7_init(f); }

static void t7_launch_for_multiball(vp_field_t *f, vp_ball_t *b)
{
    if (!b) {
        if (t7.balls_locked < 1) return;
        const vp_roll_t *r = vp_roll(f, t7.lock_roll[t7.balls_locked - 1], 0);
        b = vp_create_ball(f, r->x, r->y);
        if (!b) return;
    }
    b->layer = BALL_LOCK_LAYER;
    b->vx = 0; b->vy = -(5.0f + vp_randf(f));
    f->sfx |= VSFX_BALL;
    vp_update_launch_times(f);
    t7.balls_locked--;
}
static void t7_act_release(vp_field_t *f, int serial)
{
    vp_ball_t *b = vp_ball_by_serial(f, serial);
    if (b) b->gscale = 1;
    t7_launch_for_multiball(f, b);
}
static void t7_act_new(vp_field_t *f, int last) { t7_launch_for_multiball(f, NULL); if (last) t7.mb = M7_ACTIVE; }

static void t7_start_multiball(vp_field_t *f)
{
    if (f->nballs < 1) return;
    vp_ball_t *b = &f->balls[0];
    const vp_roll_t *r = vp_roll(f, t7.lock_roll[2], 0);
    b->x = r->x; b->y = r->y; b->vx = b->vy = 0; b->angvel = 0; b->gscale = 0;   /* ueber dem letzten Fach festhalten */
    vp_message(f, "Multiball!", 3000, 1);
    t7.mb = M7_STARTING;
    vp_wall_retract(f, t7.saver_l, 0); vp_wall_retract(f, t7.saver_r, 0);
    vp_schedule(f, 1000, t7_act_release, b->serial);
    vp_schedule(f, 3500, t7_act_new, 0);
    vp_schedule(f, 6000, t7_act_new, 1);
}

static void t7_roll_all(vp_field_t *f, int e, vp_ball_t *b)
{
    if (vp_is(f, e, "FlipperRollovers") || vp_is(f, e, "TopRollovers")) {
        vp_roll_set_all(f, e, 0);
        vp_inc_multiplier(f, 1500);
    } else if (e == t7.lock_roll[0] || e == t7.lock_roll[1] || e == t7.lock_roll[2]) {
        t7.balls_locked++;
        if (t7.balls_locked == 3) t7_start_multiball(f);
        else { vp_remove_ball_quiet(f, b); vp_messagef(f, 3000, "Ball %d Locked", t7.balls_locked); }
        lock_current();
        enter_wandering();
    }
}

static void t7_drop_all(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    if (dg_save_targets(f, e, "DropTargetLeftSave", "DropTargetRightSave")) return;
    if (vp_is(f, e, "MiniFieldTopTargets") || vp_is(f, e, "MiniFieldLeftTargets")) {
        t7.ramp_score += 1000;
        vp_message(f, "Ramp bonus increased", 1500, 1);
    }
}

static void t7_loop(vp_field_t *f)
{
    vp_add_score(f, t7.ramp_score);
    if (!all_stars_active(t7.cur) && switch_to_random(f)) vp_message(f, vp_consts[t7.cur].name, 3000, 1);
}

static void t7_sensor(vp_field_t *f, int e, vp_ball_t *b)
{
    int prev = b->last_sensor;
    if (vp_is(f, e, "LaunchBarrierSensor")) dg_launch_barrier(f, 1);
    else if (vp_is(f, e, "LaunchBarrierRetract")) dg_launch_barrier(f, 0);
    else if (vp_is(f, e, "LeftFlipperDropSensor") || vp_is(f, e, "RightFlipperDropSensor")) { b->vx = 0; b->vy = 0; }
    else if (vp_is(f, e, "MiniTableOrBallLockSensor"))
        b->layer = (t7.mb == M7_INACTIVE && all_stars_active(t7.cur)) ? BALL_LOCK_LAYER : MINITABLE_LAYER;
    else if ((vp_is(f, e, "LeftLoopDetector") || vp_is(f, e, "RightLoopDetector")) && e != prev) t7_loop(f);
    else if (vp_is(f, e, "MiniFieldDetector")) {
        if (all_locked()) { vp_message(f, "Jackpot!", 3000, 1); vp_add_score(f, 100000); reset_and_wander(); }
    } else if ((vp_is(f, e, "InnerOrbitLeftTrigger") && vp_is(f, prev, "InnerOrbitRightTrigger")) ||
               (vp_is(f, e, "InnerOrbitRightTrigger") && vp_is(f, prev, "InnerOrbitLeftTrigger")))
        vp_add_score(f, t7.ramp_score);
}

static int guide_alpha(void) { return (int)(75 + 180 * (1 + fm_sinf(VP_TAU * t7.guide_t / 5.0f)) / 2); }

static void t7_tick(vp_field_t *f, float dt)
{
    if (!t7.ready) return;
    stars_tick(dt);
    stars_frame(f);
    {   /* Sterne des aktiven Bildes einsammeln */
        int new_stars = 0;
        for (int i = 0; i < f->nballs && t7.cur >= 0; i++) {
            const vp_ball_t *b = &f->balls[i];
            if (b->layer != 0) continue;
            float bx = (b->x - t7.view_x) / t7.view_r, by = (b->y - t7.view_y) / t7.view_r;
            if (bx * bx + by * by >= 1) continue;
            for (int k = 0; k < vp_consts[t7.cur].nidx; k++) {
                int star = vp_consts[t7.cur].idx[k], pi = t7.map[star];
                if (pi < 0 || star_on(star)) continue;
                float dx = bx - t7.px[pi] / t7.t.rad, dy = by - t7.py[pi] / t7.t.rad;
                if (dx * dx + dy * dy < 0.01f) { star_set(star); new_stars++; }
            }
        }
        if (new_stars > 0) {
            t7.shapes_due = 1;
            vp_add_score(f, (int64_t)new_stars * 500);
            f->sfx |= VSFX_ROLLOVER;
            if (all_stars_active(t7.cur)) {
                if (t7.mb == M7_INACTIVE) vp_message(f, t7.balls_locked == 2 ? "Multiball Ready" : "Ball Lock Ready", 2000, 1);
                else {
                    lock_current();
                    if (all_locked()) vp_message(f, "Shoot ramp for jackpot", 2000, 1);
                    else vp_messagef(f, 2000, "%s complete", vp_consts[t7.cur].name);
                    enter_wandering();
                }
            }
        }
    }
    for (int i = 0; i < 3; i++) {
        vp_roll_set_all(f, t7.lock_roll[i], t7.balls_locked > i);
        f->es[t7.lock_roll[i]].ignore_ball = (uint8_t)!(t7.mb == M7_INACTIVE && t7.balls_locked == i);
    }
    {   /* Wegweiser: gruen = Schleife schiessen, gelb = Ball einlochen, rot = Jackpot */
        int loop_a = 0, lock_a = 0;
        uint32_t lock_c = VP_RGB(0xCC, 0xCC, 0);
        t7.guide_t = fmodf(t7.guide_t + dt, 5.0f);
        if (f->in_progress) {
            if (t7.mb == M7_ACTIVE && all_locked()) { lock_c = VP_RGB(0xAA, 0, 0); lock_a = guide_alpha(); }
            else if (t7.mb == M7_INACTIVE && all_stars_active(t7.cur)) lock_a = guide_alpha();
            else if (t7.mode == SM_WANDERING) loop_a = guide_alpha();
        }
        vp_set_color(f, t7.guide_l, 1, vp_with_alpha(VP_RGB(0, 0xAA, 0x66), loop_a));
        vp_set_color(f, t7.guide_r, 1, vp_with_alpha(VP_RGB(0, 0xAA, 0x66), loop_a));
        vp_set_color(f, t7.guide_lock, 1, vp_with_alpha(lock_c, lock_a));
    }
    if (t7.shapes_due) { t7.shapes_due = 0; t7_shapes(f); }
    if (t7.mb == M7_ACTIVE && f->nballs <= 1) { t7.mb = M7_INACTIVE; reset_and_wander(); }
}

const vp_delegate_t vp_delegate7 = { t7_init, t7_started, NULL, t7_tick, NULL, t7_drop_all, t7_roll_all, NULL, t7_sensor };

/* ================= Tisch 8: Pool Party ================= */
static struct {
    int launch_gate, right_gate, left_gate, lanes, saucer, left_out, right_out;
    int bank_ins, eight_ins, bonus_ins, mult_ins, orbit_ins, eight_val_ins;
    int bank_targets, eight_target, inline_targets, spinner, left_flipper;
    int balls_collected, racks, bonus_mult, eight_value;
    int bank_done, collect_lit, skill_shot, ready;
} t8;

static const char *commas(char *buf, size_t n, long v)
{
    char t[24];
    int len = snprintf(t, sizeof t, "%ld", v), o = 0;
    for (int i = 0; i < len && o < (int)n - 2; i++) {
        buf[o++] = t[i];
        if ((len - 1 - i) % 3 == 0 && i != len - 1 && t[i] != '-') buf[o++] = ',';
    }
    buf[o] = 0;
    return buf;
}

static int t8_count_collected(vp_field_t *f)
{
    int n = 0;
    for (int i = 0; i < f->def->elems[t8.bonus_ins].count; i++) if (vp_roll_active(f, t8.bonus_ins, i)) n++;
    return n;
}

static void t8_act_insert(vp_field_t *f, int i) { vp_roll_set(f, t8.bank_ins, i, 1); }
static void t8_act_targets(vp_field_t *f, int a) { (void)a; vp_drop_reset(f, t8.bank_targets); }
static void t8_act_left_gate(vp_field_t *f, int a) { (void)a; vp_wall_retract(f, t8.left_gate, 0); }
static void t8_act_launch_gate(vp_field_t *f, int a) { (void)a; vp_wall_retract(f, t8.launch_gate, 0); }
static void t8_act_right_gate(vp_field_t *f, int a) { (void)a; vp_wall_retract(f, t8.right_gate, 0); }

static void t8_reset_elements(vp_field_t *f, int first)
{
    if (t8.bank_done || first) {
        vp_roll_set_all(f, t8.bonus_ins, 0);
        t8.bank_done = 0;
        for (int i = 0; i < 7; i++) vp_schedule(f, 500 + 100 * i, t8_act_insert, i);     /* Lichterlauf */
        vp_schedule(f, 1200, t8_act_targets, 0);
    }
    vp_roll_set_all(f, t8.lanes, 0);
    vp_roll_set_all(f, t8.saucer, 0);
    f->es[t8.saucer].visible = 0; f->es[t8.left_out].visible = 0; f->es[t8.right_out].visible = 0;
    vp_roll_set_all(f, t8.eight_ins, 0);
    vp_roll_set_all(f, t8.mult_ins, 0);
    vp_roll_set_all(f, t8.orbit_ins, 0);     vp_roll_activate_first(f, t8.orbit_ins);
    vp_roll_set_all(f, t8.eight_val_ins, 0); vp_roll_activate_first(f, t8.eight_val_ins);
    vp_drop_reset(f, t8.eight_target);
    vp_drop_reset(f, t8.inline_targets);
    f->es[t8.spinner].score = 500;
}

static void t8_reset_vars(int first)
{
    if (first) t8.racks = 0;
    t8.balls_collected = 0; t8.bonus_mult = 1; t8.eight_value = 20000;
    t8.bank_done = 0; t8.collect_lit = 0; t8.skill_shot = 0;
}

static void t8_init(vp_field_t *f)
{
    memset(&t8, 0, sizeof t8);
    t8.launch_gate = vp_find(f, "LaunchGate"); t8.right_gate = vp_find(f, "RightGate"); t8.left_gate = vp_find(f, "LeftGate");
    t8.lanes = vp_find(f, "LaneRollovers"); t8.saucer = vp_find(f, "BonusCollectSaucer");
    t8.left_out = vp_find(f, "LeftOutlane"); t8.right_out = vp_find(f, "RightOutlane");
    t8.bank_ins = vp_find(f, "RightBankInserts"); t8.eight_ins = vp_find(f, "EightBallInsert"); t8.bonus_ins = vp_find(f, "BonusInserts");
    t8.mult_ins = vp_find(f, "BonusMultiplierInserts"); t8.orbit_ins = vp_find(f, "LeftOrbitValueInserts"); t8.eight_val_ins = vp_find(f, "EightBallValueInserts");
    t8.bank_targets = vp_find(f, "RightBankTargets"); t8.eight_target = vp_find(f, "EightBallTarget"); t8.inline_targets = vp_find(f, "InlineTargets");
    t8.spinner = vp_find(f, "LeftOrbitSpinner"); t8.left_flipper = vp_find(f, "LeftFlipper");
    t8.bonus_mult = 1; t8.eight_value = 20000;
    t8.ready = 1;
}

static void t8_started(vp_field_t *f) { t8_init(f); t8_reset_elements(f, 1); t8_reset_vars(1); }

static void t8_ball_lost(vp_field_t *f)
{
    char b[24], m[48];
    t8.balls_collected = t8_count_collected(f);
    long bonus = ((long)t8.racks * 56000 + (long)t8.balls_collected * 7000) * t8.bonus_mult;
    vp_add_score(f, bonus);
    snprintf(m, sizeof m, "Bonus: %s", commas(b, sizeof b, bonus));
    vp_message(f, m, 3000, 1);
    t8_reset_elements(f, 0);
    t8_reset_vars(0);
}

static void t8_collect_rack(vp_field_t *f)
{
    int down = 0;
    vp_roll_set_all(f, t8.bonus_ins, 1);
    vp_roll_set_all(f, t8.bank_ins, 0);
    for (int i = 0; i < 7; i++)
        if (!(f->es[t8.bank_targets].mask & (1u << i))) { down++; f->es[t8.bank_targets].mask |= 1u << i; }
    vp_add_score(f, (int64_t)3000 * down);
    vp_add_score(f, t8.eight_value);
    t8.skill_shot = 0; t8.collect_lit = 1; t8.bank_done = 1;
    f->es[t8.saucer].visible = 1;
    t8.racks++;
}

static void t8_collision(vp_field_t *f, int e, int sub, vp_ball_t *b)
{
    (void)sub; (void)b;
    if (e == t8.bank_targets) {
        for (int i = 0; i < 7; i++)
            if (f->es[t8.bank_targets].mask & (1u << i)) { vp_roll_set(f, t8.bank_ins, 6 - i, 0); vp_roll_set(f, t8.bonus_ins, 6 - i, 1); }
    } else if (e == t8.inline_targets) {
        if (t8.bonus_mult != 5) t8.bonus_mult++;
        vp_roll_activate_first(f, t8.mult_ins);
    }
}

static void t8_roll_all(vp_field_t *f, int e, vp_ball_t *b)
{
    char num[24], m[48];
    (void)b;
    if (e == t8.lanes) {
        vp_message(f, "Lanes Completed", 3000, 1);
        vp_roll_set_all(f, t8.lanes, 0);
        if (f->es[t8.spinner].score < 1500) {
            f->es[t8.spinner].score += 500;
            vp_roll_activate_first(f, t8.orbit_ins);
            t8.eight_value += 20000;
            vp_roll_activate_first(f, t8.eight_val_ins);
        }
    } else if (e == t8.saucer) {
        t8.collect_lit = 0;
        t8.balls_collected = t8_count_collected(f);
        long bonus = ((long)t8.racks * 56000 + (long)t8.balls_collected * 7000) * t8.bonus_mult;
        vp_add_score(f, bonus);
        snprintf(m, sizeof m, "Bonus Collected: %s", commas(num, sizeof num, bonus));
        vp_message(f, m, 3000, 1);
    } else if (e == t8.left_out || e == t8.right_out) vp_message(f, "Scratch!", 3000, 1);
}

static void t8_spinner(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    if (e == t8.spinner) { vp_wall_retract(f, t8.left_gate, 1); vp_schedule(f, 2000, t8_act_left_gate, 0); }
}

static void t8_drop_all(vp_field_t *f, int e, vp_ball_t *b)
{
    char num[24], m[48];
    (void)b;
    if (e == t8.eight_target) {
        if (t8.bank_done) { t8_collect_rack(f); vp_message(f, "Bonus Collect Lit", 3000, 1); }
        else if (t8.skill_shot) { t8_collect_rack(f); vp_message(f, "Breakshot!", 3000, 1); }
        else {
            vp_drop_reset(f, t8.eight_target);
            vp_add_score(f, t8.eight_value);
            snprintf(m, sizeof m, "Corner Pocket: %s", commas(num, sizeof num, t8.eight_value));
            vp_message(f, m, 3000, 1);
        }
    } else if (e == t8.bank_targets && !t8.bank_done) {
        t8.bank_done = 1;
        vp_roll_set(f, t8.eight_ins, 0, 1);
        vp_message(f, "Shoot the Eight Ball!", 3000, 1);
    }
}

static void t8_sensor(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    if (vp_is(f, e, "LaunchGateRetract")) {
        vp_wall_retract(f, t8.launch_gate, 1);
        vp_schedule(f, 1000, t8_act_launch_gate, 0);
        if (vp_flipper_engaged(f, t8.left_flipper)) {                       /* linker Flipper beim Abschuss gehalten: Breakshot */
            t8.skill_shot = 1;
            vp_wall_retract(f, t8.left_gate, 1);
            vp_schedule(f, 2000, t8_act_left_gate, 0);
        }
    } else if (vp_is(f, e, "RightGateRetract")) {
        vp_wall_retract(f, t8.right_gate, 1);
        vp_schedule(f, 1000, t8_act_right_gate, 0);
    } else if (vp_is(f, e, "EndSkillShot")) t8.skill_shot = 0;
}

const vp_delegate_t vp_delegate8 = { t8_init, t8_started, t8_ball_lost, NULL, t8_collision, t8_drop_all, t8_roll_all, t8_spinner, t8_sensor };
