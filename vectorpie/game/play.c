/*
 * play.c - das Spiel: Steuerung, Stoessel, Stoss, Ton, Zeichnen, Ende der Runde.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "plat.h"
#include "vpie.h"
#include "font.h"
#include "render.h"
#include "hiscore.h"
#include "sound.h"
#include "segdisp.h"
#include "fx.h"
#include "fmath.h"

/* ---- Spiel ---- */
static int s_over_ms;
static float s_shake_t, s_shake_x, s_shake_y;

void autoplay(int *l, int *r)
{
    static int n;
    n++;
    for (int i = 0; i < F.nballs; i++) {
        const vp_ball_t *b = &F.balls[i];
        if (b->y < 5.0f && b->y > 1.0f && (n % 22) < 9) { if (b->x < F.def->width * 0.5f - 0.6f) *l = 1; else *r = 1; }
    }
    if (F.nballs == 0) vp_launch_if_needed(&F);
}

static void nudge_by(int dir, float scale)
{
    if (g_set.nudge_invert) dir = -dir;
    F.nudge_scale = scale;
    F.nudge_tilt = g_set.nudge_power == NUDGE_ANALOG ? 0.4f + 0.6f * scale : 0;   /* analog: ein sanfter Stoss reizt den Tilt weniger */
    if (!vp_nudge(&F, dir)) return;
    const float k = 3.0f * (0.4f + 0.6f * scale);                    /* der Ruck im Bild passt zur Staerke */
    s_shake_t = 0.14f;
    s_shake_x = dir * k; s_shake_y = dir ? 0.0f : k;
}

static void nudge(int dir) { nudge_by(dir, NUDGE_SCALE[g_set.nudge_power]); }

/* NUDGE STRENGTH = ANALOG: wie weit der Stick ausschlaegt, bestimmt die Wucht - bis zur Staerke von STRONG am Anschlag.
 * Der Stick laeuft auf dem Weg zum Anschlag durch alle kleineren Werte, darum wird nicht beim ersten Ueberschreiten
 * gestossen, sondern erst, wenn der Ausschlag nicht mehr waechst (oder nach 70 ms). Neu gespannt wird erst nach der
 * Rueckkehr zur Mitte. Nach unten bleibt der Stoessel. */
static void nudge_analog(void)
{
    static int armed, dir, peak, wait_release;
    static float t;
    int jx = vpie_stick_x(), jy = vpie_stick_y();
    int ax = jx < 0 ? -jx : jx, ay = jy > 0 ? jy : 0, m = ax > ay ? ax : ay;
    if (wait_release) { if (m < 20) wait_release = 0; return; }
    if (!armed) {
        if (m < 30) return;
        armed = 1; peak = m; t = 0;
        dir = ax >= ay ? (jx < 0 ? -1 : 1) : 0;
        return;
    }
    t += s_dt;
    if (m > peak) { peak = m; if (peak < 124 && t < 0.07f) return; }
    else if (m > peak - 4 && t < 0.07f && peak < 124) return;
    {
        float f = (float)(peak - 30) / 94.0f;
        if (f > 1) f = 1;
        nudge_by(dir, 0.25f + 0.75f * f);
    }
    armed = 0; wait_release = 1;
}

/* Tisch, Ball und Punktezeile in der Spielansicht (Zoom, Ruck beim Stoss) */
void draw_game(int dim, int balls, float dt)
{
    float k = s_shake_t > 0 ? s_shake_t / 0.14f : 0;
    if (s_shake_t > 0) s_shake_t -= dt;
    render_game_view(&F, dt, F.in_progress ? 1.0f + 0.2f * g_set.zoom : 1.0f, s_shake_x * k, s_shake_y * k);
    render_table(&F, dim);
    if (balls) { fx_draw_trails(&F); render_balls(&F); fx_draw_top(); }
    render_clip_end();
}

/* Stoessel am Stick / Yoke: nach unten ziehen spannt (je weiter, desto staerker), loslassen schiesst ab. Der Stoessel
 * bleibt auf dem tiefsten Punkt stehen, bis der Hebel ganz losgelassen ist (Olli, 2026-10-15: "yoke unten muss den
 * plunger auch unten halten bis man loslaesst") - auch ein Yoke, der beim Loslassen ueber ein paar Bilder zur Mitte
 * zurueckfaehrt, schiesst so mit dem vollen Zug. Die Knoepfe schiessen wie bisher mit voller Staerke. */
static void plunger(void)
{
    static float deepest, charge;
    static int armed;
    int jy = vpie_stick_y();
    float pull;
    if (g_set.plunger == PLUNGER_DIGITAL) {
        /* DIGITAL: solange der Stick unten ist, faehrt der Stoessel langsam herunter (voll nach 1,3 s); loslassen schiesst
         * mit dem, was bis dahin gespannt war */
        if (!vp_launch_ready(&F)) { armed = 0; charge = 0; }
        else if (jy < -50) { armed = 1; charge += s_dt / 1.3f; if (charge > 1) charge = 1; }
        else if (armed) { armed = 0; vp_launch_power(&F, charge); charge = 0; }
        render_plunger(charge);
        return;
    }
    pull = jy < -24 ? (float)(-jy - 24) / 96.0f : 0;
    if (pull > 1) pull = 1;
    if (!vp_launch_ready(&F)) { armed = 0; deepest = 0; }
    else if (pull > 0) {
        if (!armed) deepest = 0;
        armed = 1;
        if (pull > deepest) deepest = pull;
    } else if (armed) {
        armed = 0;
        vp_launch_power(&F, deepest);
        deepest = 0;
    }
    render_plunger(armed ? deepest : 0);
}

/* Stereobild: Effekte dort, wo der Ball ist (halb zur Seite am Tischrand), Flipperschlag auf der Seite des Flippers */
static void stereo(int l, int r)
{
    static int pl, pr;
    int ball = 128, flip = 128;
    const vp_table_def_t *t = F.def;
    float x = F.nballs ? F.balls[0].x : t->launch_x;
    ball = 128 + (int)((x / t->width - 0.5f) * 2.0f * 90.0f);
    if (l && !pl && !(r && !pr)) flip = 128 - 80;
    else if (r && !pr && !(l && !pl)) flip = 128 + 80;
    pl = l; pr = r;
    sound_stereo(ball, flip);
}

/* START im Spiel: naechste Ansicht. Querformat: Bildbreite, naeher, ganz nah, ganzer Tisch; hochkant die Zoomstufen
 * (ab der kleinsten zur 3D-Stufe) und der ganze Tisch. Bleibt als Einstellung (ZOOM im Service). */
static float s_view_t;
static void view_next(void)
{
    if (!vpie_vertical()) g_set.zoom = (uint8_t)(g_set.zoom == 0 ? 3 : (g_set.zoom >= ZOOM_STEPS ? 0 : (g_set.zoom < 3 ? 4 : g_set.zoom + 1)));
    else if (g_set.zoom == 0) g_set.zoom = (uint8_t)(settings_min_zoom() > 1 ? settings_min_zoom() : 1);
    else g_set.zoom = (uint8_t)(g_set.zoom >= ZOOM_STEPS ? 0 : g_set.zoom + 1);
    s_view_t = 1.2f;
}

static void view_label(void)
{
    static const char *const WIDE[ZOOM_STEPS + 1] = { "WHOLE TABLE", "FULL WIDTH", "FULL WIDTH", "FULL WIDTH", "CLOSER", "CLOSEST" };
    char buf[24];
    if (s_view_t <= 0) return;
    s_view_t -= s_dt;
    if (!vpie_vertical()) snprintf(buf, sizeof buf, "%s", WIDE[g_set.zoom]);
    else if (g_set.zoom) snprintf(buf, sizeof buf, "ZOOM %d.%dX", 1 + g_set.zoom / 5, g_set.zoom * 2 % 10);
    else snprintf(buf, sizeof buf, "WHOLE TABLE");
    gfx_color(C_SHADOW); text_center(12, Q4(-60) + 10, buf, 44, 110);
    gfx_color(C_VALUE);  text_center(0, Q4(-60), buf, 44, 127);
}

void play_frame(void)
{
    int l = pad.b1, r = pad.b4;
    if (vpie_start()) view_next();
    if (plat_test_int("AUTOPLAY", 0)) autoplay(&l, &r);
    if (edge.b1 || edge.b4 || edge.b3) vp_launch_if_needed(&F);
    vp_catch_returned_ball(&F);
    plunger();
    if (g_set.nudge_power == NUDGE_ANALOG) nudge_analog();
    else {
        if (edge.left) nudge(-1);
        if (edge.right) nudge(1);
        if (edge.up) nudge(0);
    }
    vp_set_flippers(&F, l, r);
    {
        const uint32_t t0 = plat_clock_us();
        vp_advance(&F, s_dt, s_now_ms, game_speed());
        s_logic_us = plat_clock_us() - t0;
    }
    stereo(l, r);
    fx_update(&F, s_dt);
    sound_events(F.sfx, F.score_hits);
    F.sfx = 0;
    {   /* nach einem Tilt schweigt die Musik bis zur naechsten Runde */
        static int was_tilted;
        if (was_tilted && !F.tilted && F.in_progress) sound_music(g_set.table + 1);
        was_tilted = F.tilted;
    }
    if (!F.in_progress) { sound_game_reset(); s_state = S_OVER; s_over_ms = 0; }
    {
        const uint32_t t0 = plat_clock_us();
        draw_game(0, 1, s_dt);
        render_hud(&F);
        s_draw_us = plat_clock_us() - t0;
    }
    view_label();
    if (F.save_ms) {                                                 /* der Ball laeuft gerade durch die Truhe: die Runde ist NICHT vorbei */
        gfx_color(C_VALUE);
        if ((s_now_ms / 180) & 1) { text_center(0, Q4(6), "BALL SAVE", 44, 127); gfx_color(C_SHADOW); text_center(12, Q4(6) + 10, "BALL SAVE", 44, 110); }
        else text_center(0, Q4(6), "BALL SAVE", 44, 96);
    }
    if (F.nplayers > 1) {
        /* Wer ist dran? Bei jedem Wechsel gross mitten im Bild, knapp zwei Sekunden lang */
        if (F.player != s_turn_shown) { s_turn_shown = F.player; s_turn_t = 1.8f; }
        if (s_turn_t > 0) {
            char who[12];
            s_turn_t -= s_dt;
            snprintf(who, sizeof who, "PLAYER %d", F.player + 1);
            gfx_color(C_ACCENT);
            text_center(0, Q4(6), who, 46, 127);
            text_center(12, Q4(6) + 10, who, 46, 110);
        }
    }
}

void over_frame(void)
{
    char buf[32];
    s_over_ms += (int)(s_dt * 1000);
    vp_set_flippers(&F, pad.b1, pad.b4);
    vp_advance(&F, s_dt, s_now_ms, game_speed());
    sound_events(F.sfx, F.score_hits);
    F.sfx = 0;
    draw_game(F.nplayers > 1 ? 50 : 0, 0, s_dt);
    if (F.nplayers > 1) {                                    /* Endstand aller Spieler */
        gfx_color(C_ALERT);
        text_center(0, Q4(70), "GAME OVER", 34, 127);
        gfx_color(C_WHITE);
        for (int p = 0; p < F.nplayers; p++) {
            snprintf(buf, sizeof buf, "P%d  %lu", p + 1, (unsigned long)final_score(p));
            text_center(0, Q4(30 - 30 * p), buf, 26, 118);
        }
    } else {
        render_hud(&F);
        gfx_color(C_ALERT);
        if (!F.msg[0]) text_center(0, Q4(20), "GAME OVER", 34, 127);
    }
    if ((s_over_ms > 1200 && (edge.b1 || edge.b2 || edge.b3 || edge.b4)) || s_over_ms > (F.nplayers > 1 ? 12000 : 6000)) entry_next(0);
}

