/*
 * main.c - Vector Pinball fuer Vectrex + UVMC2: Ablauf (Menue, Spiel, Pause, Bestenliste, Bildeinstellung).
 *
 * Steuerung im Spiel:
 *   Knopf 1 / Knopf 4     linker / rechter Flipper (starten auch den Ball, wie im Original)
 *   Knopf 3               Ball starten
 *   Knopf 2               Pause
 *   Stick links/rechts/hoch   gegen den Tisch stossen (zu oft: TILT)
 * Die Physik laeuft in festen Schritten nach der Echtzeit (480 je Sekunde), unabhaengig von der Bildrate:
 * wird das Bild langsamer, bleibt das Spiel gleich schnell.
 */
/* nicht zeitkritisch: auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp (das Kit baut sonst mit -O3) */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vpy.h>
#include "vp.h"
#include "plat.h"
#include "gfx.h"
#include "font.h"
#include "render.h"
#include "settings.h"
#include "hiscore.h"
#include "sound.h"
#include "intro.h"
#include "fmath.h"

#define PORT_YEAR "2026"

enum { S_MENU, S_PLAY, S_PAUSE, S_OVER, S_ENTRY, S_ADJUST, S_ABOUT, S_INTRO };
static int s_state;
static vp_field_t F;
static uint32_t s_last_us, s_now_ms;
static float s_dt;

/* Tempo des Balls (Option BALL SPEED). MEDIUM ist das Tempo des Originals. FAST ist 1,2-mal so schnell und entspricht einem echten
 * Tisch: 20 Einheiten Breite sind gut 20 Zoll, eine Einheit also etwa ein Zoll, und mit der Schwerkraft des Originals rollt der Ball
 * wie auf 4,5 Grad Neigung - echte Tische stehen bei 6,5 Grad. SLOW ist fuer Anfaenger und Kinder. Die Flugbahnen bleiben gleich. */
static float game_speed(void)
{
    static const float SPEED[SPEED_N] = { 0.8f, 1.0f, 1.2f };
    return SPEED[g_set.speed];
}

/* ---- Eingabe ---- */
typedef struct { int left, right, up, down, b1, b2, b3, b4; } pad_t;
static pad_t pad, prev, edge;
static int rep_t[6];                      /* Wiederholung fuer links/rechts/hoch/runter (Menue), Knopf 1/4 (Initialen) */

static void read_pad(void)
{
    int jx = vpy_j1_x(), jy = vpy_j1_y();
    prev = pad;
    pad.left = jx < -50; pad.right = jx > 50; pad.up = jy > 50; pad.down = jy < -50;
    pad.b1 = vpy_j1_button(1); pad.b2 = vpy_j1_button(2); pad.b3 = vpy_j1_button(3); pad.b4 = vpy_j1_button(4);
    edge.left = pad.left && !prev.left; edge.right = pad.right && !prev.right;
    edge.up = pad.up && !prev.up; edge.down = pad.down && !prev.down;
    edge.b1 = pad.b1 && !prev.b1; edge.b2 = pad.b2 && !prev.b2; edge.b3 = pad.b3 && !prev.b3; edge.b4 = pad.b4 && !prev.b4;
}

/* Tastenwiederholung: 1 beim ersten Druck, dann nach 0,4 s alle 60 ms (fast = alle 25 ms) */
static int repeat(int idx, int held, int fast)
{
    if (!held) { rep_t[idx] = 0; return 0; }
    int t = rep_t[idx];
    rep_t[idx] += (int)(s_dt * 1000);
    if (t == 0) return 1;
    int step = fast ? 25 : 60;
    return t >= 400 && ((t - 400) / step != (rep_t[idx] - 400) / step);
}

/* ---- Anzeigehilfen ---- */
static uint32_t s_fps_t0;
static int s_fps_n, s_fps;

static uint32_t s_audio_us, s_logic_us, s_draw_us, s_flush_us;   /* Zeiten des letzten Bildes auf Kern 0 */
#ifndef __arm__
/* VP_PROF=1 (Host): alle 60 Bilder die mittleren Zeiten je Abschnitt, um Engpaesse zu finden */
static void prof_report(void)
{
    static uint32_t acc[4], n;
    acc[0] += s_logic_us; acc[1] += s_draw_us; acc[2] += s_flush_us; acc[3] += s_audio_us;
    if (++n < 60) return;
    fprintf(stderr, "prof  logic %4u  draw %4u  flush %4u  audio %4u us  (strokes %d, occluders %d, cut %d, tests %d convex + %d concave)\n",
            acc[0] / n, acc[1] / n, acc[2] / n, acc[3] / n, gfx_stats.strokes, gfx_stats.occluders, gfx_stats.cut_lines, gfx_stats.span_tests, gfx_stats.poly_tests);
    memset(acc, 0, sizeof acc); n = 0;
}
#endif
static void draw_info(void)
{
    char buf[64];
    unsigned cyc, drop, over;
    if (!g_set.info) return;
    plat_bus_stats(&cyc, &drop, &over);
    /* A = Zeit fuer Ton und Karte im letzten Bild in 0,1 ms (MP3 dekodieren, mischen, nachladen) */
    /* L = Spiel und Zeichnen (Kern 0) im letzten Bild in 0,1 ms: liegt L nahe 200, bremst Kern 0 und nicht der Strahl */
    snprintf(buf, sizeof buf, "F%d S%d J%d B%u X%u O%u A%u L%u", s_fps, gfx_stats.strokes, gfx_stats.jumps, cyc, drop, over, s_audio_us / 100u,
             (s_logic_us + s_draw_us + s_flush_us) / 100u);
    text_center(0, Q4(-187), buf, 10, 70);
}

/* Stoss gegen den Tisch: STRONG ist die Staerke der ersten Fassung, die sich uebertrieben anfuehlte */
static const float NUDGE_SCALE[NUDGE_N] = { 0.35f, 0.6f, 1.0f, 1.0f };

static void load_table(int t)
{
    vp_load(&F, t, plat_micros() | 1u);
    vp_set_real_physics(&F, g_set.physics == PHYSICS_REALISTIC);
    F.nudge_scale = NUDGE_SCALE[g_set.nudge_power];
    F.tilt_sens = g_set.tilt_sens;
    F.launch_k = t == 4 ? 0.8f : 1.0f;                     /* Tisch 5: der Stoessel schoss zu stark */
    render_default_view(F.def);
    render_zoom_reset(&F);
}

/* ---- Menue ----
 * Eine Liste mit Flipper-Gesicht: der Rahmen ist der Umriss eines Tisches, unten sitzen zwei Flipper (sie schlagen, wenn man
 * waehlt), oben blinken drei Bumper, und der Zeiger ist eine Kugel, die von Zeile zu Zeile rollt.
 *   Stick hoch/runter   Punkt waehlen
 *   Stick links/rechts  Wert aendern (Zeilen mit Wert zeigen ihn dann zwischen < >)
 *   Knopf 4 (oder 3)    ausfuehren: spielen, Seite wechseln - bei einer Zeile mit Wert: naechster Wert
 *   Knopf 1             zurueck
 * Kein Tisch im Hintergrund: die Liste soll ruhig stehen, und auf der Roehre kostet jeder Strich Bildrate. */
enum { PAGE_MAIN, PAGE_OPTIONS };
enum { M_PLAY, M_TABLE, M_OPTIONS, M_MAIN_N };
enum { O_BALLS, O_SPEED, O_PHYSICS, O_NUDGE_POWER, O_NUDGE_INVERT, O_TILT, O_PLUNGER, O_ZOOM, O_3D, O_SOUND, O_MUSIC, O_MUSIC_VOL,
       O_ADJUST, O_ABOUT, O_BACK, O_N };
#define O_VISIBLE 7                      /* so viele Zeilen der Seite OPTIONS sind zugleich zu sehen */
static int m_page, m_item;
static float m_ball_y = 1e9f, m_flip[2], m_time;
static const char *const SOUND_NAMES[SOUND_N] = { "OFF", "CONSOLE", "JACK", "BOTH" };
static const char *const SPEED_NAMES[SPEED_N] = { "SLOW", "MEDIUM", "FAST" };
static const char *const NUDGE_NAMES[NUDGE_N] = { "WEAK", "NORMAL", "STRONG", "ANALOG" };
static const char *const TILT_NAMES[TILT_N] = { "LOW", "MEDIUM", "HIGH" };
static const char *const V3D_NAMES[V3D_N] = { "OFF", "FLIPPERS", "OBJECTS", "EVERYTHING" };

static void adjust_open(void);

static int s_turn_shown;                 /* Spieler, fuer den der grosse Hinweis schon kam (-1 = noch keiner) */
static float s_turn_t;

static void start_game(void)
{
    load_table(g_set.table);
    s_turn_shown = -1;
    F.nplayers = g_set.players;
    vp_start_game(&F, g_set.balls_unlimited);
    F.now_ms = s_now_ms;
    vp_message(&F, F.def->name, 1800, 0);
    F.sfx |= VSFX_START;
    sound_game_reset();
    sound_music(g_set.table + 1);
    s_state = S_PLAY;
}

static void menu_open(int page, int item)
{
    s_state = S_MENU; m_page = page; m_item = item;
    sound_music(0);
}

/* 1 = die Zeile hat einen Wert, den links/rechts aendern */
static int menu_has_value(void)
{
    if (m_page == PAGE_MAIN) return m_item == M_TABLE || m_item == M_PLAY;
    return m_item < O_ADJUST;
}

static void menu_change(int d)
{
    if (m_page == PAGE_MAIN) {
        if (m_item == M_TABLE) g_set.table = (uint8_t)((g_set.table + vp_num_tables + d) % vp_num_tables);
        else if (m_item == M_PLAY) g_set.players = (uint8_t)((g_set.players - 1 + VP_MAX_PLAYERS + d) % VP_MAX_PLAYERS + 1);
        return;
    }
    switch (m_item) {
    case O_BALLS:     g_set.balls_unlimited ^= 1; break;
    case O_SPEED:     g_set.speed = (uint8_t)((g_set.speed + SPEED_N + d) % SPEED_N); break;
    case O_PHYSICS:   g_set.physics = (uint8_t)((g_set.physics + 1) % PHYSICS_N); break;
    case O_NUDGE_POWER:  g_set.nudge_power = (uint8_t)((g_set.nudge_power + NUDGE_N + d) % NUDGE_N); break;
    case O_NUDGE_INVERT: g_set.nudge_invert ^= 1; break;
    case O_TILT:      g_set.tilt_sens = (uint8_t)((g_set.tilt_sens + TILT_N + d) % TILT_N); break;
    case O_PLUNGER:   g_set.plunger ^= 1; break;
    case O_ZOOM: {                                           /* nicht unter die Stufe, die das gewaehlte 3D braucht */
        const int lo = settings_min_zoom(), n = ZOOM_STEPS + 1 - lo;
        g_set.zoom = (uint8_t)(lo + (g_set.zoom - lo + n + d) % n);
        break;
    }
    case O_3D:
        g_set.view3d = (uint8_t)((g_set.view3d + V3D_N + d) % V3D_N);
        if (g_set.zoom < settings_min_zoom()) g_set.zoom = (uint8_t)settings_min_zoom();
        break;
    case O_SOUND:     g_set.sound = (uint8_t)((g_set.sound + SOUND_N + d) % SOUND_N); sound_apply(g_set.sound); sound_music(-1); sound_music(0); break;   /* Musik sofort neu ansetzen, auf dem neuen Ausgang */
    case O_MUSIC:     g_set.music ^= 1; if (!g_set.music) sound_game_reset(); sound_music(0); break;
    case O_MUSIC_VOL: g_set.music_vol = (uint8_t)((g_set.music_vol + 11 + d) % 11); break;
    default: break;
    }
}

/* Flipper als schmaler Keil: Drehpunkt, Laenge, Winkel in Grad (0 = nach rechts) */
static void wedge(int px, int py, int len, float deg, int b)
{
    /* wie im Spiel als Koerper: helle Deckflaeche, der Fuss zur Bildmitte hin versetzt */
    float a = deg * 0.01745329f, c = fm_cosf(a), s = fm_sinf(a);
    int tx = px + (int)(len * c), ty = py + (int)(len * s), ox = (int)(-s * Q4(4)), oy = (int)(c * Q4(4));
    const int ux[4] = { px + ox, tx + ox / 3, tx - ox / 3, px - ox }, uy[4] = { py + oy, ty + oy / 3, ty - oy / 3, py - oy };
    int bx[4], by[4];
    for (int i = 0; i < 4; i++) { bx[i] = ux[i] - ux[i] * 5 / 100; by[i] = uy[i] - uy[i] * 5 / 100; }
    for (int i = 0; i < 4; i++) gfx_line(ux[i], uy[i], ux[(i + 1) & 3], uy[(i + 1) & 3], b);
    {
        const pt_t top[4] = { { (int16_t)ux[0], (int16_t)uy[0] }, { (int16_t)ux[1], (int16_t)uy[1] }, { (int16_t)ux[2], (int16_t)uy[2] }, { (int16_t)ux[3], (int16_t)uy[3] } };
        const int mark = gfx_occ_mark();
        gfx_occ_add(top, 4);                                         /* die Deckflaeche verdeckt den Fuss unter sich */
        for (int i = 0; i < 4; i++) {
            gfx_line(ux[i], uy[i], bx[i], by[i], b * 2 / 3);
            gfx_line(bx[i], by[i], bx[(i + 1) & 3], by[(i + 1) & 3], b * 2 / 3);
        }
        gfx_occ_cut(mark);
    }
}

/* Kugel wie im Vorspann: Umriss und zwei Ringe ueber die Pole der Rollachse, an denen man die Drehung sieht */
static void roll_ball(int cx, int cy, int r, float roll)
{
    gfx_circle(cx, cy, r, 127, 12, 0);
    gfx_circle(cx, cy, r, 127, 12, 0);
    for (int ring = 0; ring < 2; ring++) {
        const float k = fm_cosf(roll + ring * 1.5707963f) * r;
        int px = cx + r, py = cy;
        for (int i = 1; i <= 8; i++) {
            float a = i * 0.78539816f;
            int qx = cx + (int)(fm_cosf(a) * r), qy = cy + (int)(fm_sinf(a) * k);
            gfx_line(px, py, qx, qy, 100);
            px = qx; py = qy;
        }
    }
}

/* Rahmen in Tischform mit zwei Flippern; bumpers = 1 zeichnet oben drei blinkende Bumper */
static void menu_frame_art(int bumpers)
{
    pt_t p[20];
    int n = 0;
    p[n++] = (pt_t){ (int16_t)Q4(-46), (int16_t)Q4(-130) };
    p[n++] = (pt_t){ (int16_t)Q4(-124), (int16_t)Q4(-98) };
    for (int i = 0; i <= 12; i++) {
        float a = 3.14159265f * (1.0f - i / 12.0f);
        p[n++] = (pt_t){ (int16_t)(Q4(124) * fm_cosf(a)), (int16_t)(Q4(90) + Q4(78) * fm_sinf(a)) };
    }
    p[n++] = (pt_t){ (int16_t)Q4(124), (int16_t)Q4(-98) };
    p[n++] = (pt_t){ (int16_t)Q4(46), (int16_t)Q4(-130) };
    gfx_poly(p, n, 62, 0);
    for (int k = 0; k < 2; k++) {
        if (m_flip[k] > 0) m_flip[k] -= s_dt;
        float up = m_flip[k] > 0 ? 24.0f : -28.0f;
        wedge(k ? Q4(46) : Q4(-46), Q4(-130), Q4(36), k ? 180.0f - up : up, 127);    /* Spitzen bis -147: bleibt im Bild */
    }
    if (bumpers) {
        static const int BX[3] = { -46, 0, 46 }, BY[3] = { 147, 155, 147 };
        int lit = (int)(m_time * 2.5f) % 3;
        for (int k = 0; k < 3; k++) {
            gfx_circle(Q4(BX[k]), Q4(BY[k]), Q4(7), k == lit ? 127 : 78, 8, 16);
            if (k == lit) gfx_circle(Q4(BX[k]), Q4(BY[k]), Q4(3), 127, 4, 0);
        }
    }
}

static void menu_title(void)
{
    /* "VECTOR" klein und gesperrt */
    static const char V[] = "VECTOR";
    for (int i = 0; i < 6; i++) { char c[2] = { V[i], 0 }; text_draw(Q4(-55 + 20 * i), Q4(124), c, 18, 105); }
    /* darunter "PINBALL" gross, schraeg und doppelt gezogen (die Fassungen mit Tiefe liefen auf der Roehre nicht ruhig) */
    int w = text_width("PINBALL", 44);
    text_draw_slant(-w / 2 - Q4(4), Q4(96), "PINBALL", 44, 127, 60);
    text_draw_slant(-w / 2 - Q4(4) + 14, Q4(96) + 10, "PINBALL", 44, 110, 60);
}

/* Zeile: Name links, Wert rechts; die gewaehlte ist hell. Der Zeiger (Kugel) rollt weich zur gewaehlten Zeile. */
static void menu_row(int sel, int y, int xl, int xv, const char *label, const char *value, int unit_v)
{
    char buf[48];
    int b = sel ? 127 : 66;
    text_draw(Q4(xl), y, label, 19, b);
    if (sel) {
        float target = (float)(y + 19 * 3);
        if (m_ball_y > 1e8f) m_ball_y = target;
        m_ball_y += (target - m_ball_y) * (s_dt * 14.0f > 1.0f ? 1.0f : s_dt * 14.0f);
        /* rollt gemaechlich auf der Stelle, und schneller, solange sie zur naechsten Zeile laeuft */
        static float roll;
        float gap = target - m_ball_y;
        roll += s_dt * 2.2f + (gap < 0 ? -gap : gap) * 0.004f;
        roll_ball(Q4(xl - 13), (int)m_ball_y, Q4(5), roll);
    }
    if (!value) return;
    if (sel) snprintf(buf, sizeof buf, "< %s >", value); else snprintf(buf, sizeof buf, "  %s", value);
    text_draw(Q4(xv), y + (19 - unit_v) * 3, buf, unit_v, b);
}

static void about_open(void);

/* Menue: jede Bewegung schlaegt einen der beiden Flipper im Bild und klingt auch so (Olli, 2026-10-10: Flipperton als Menueton) */
static void menu_flip(int side)
{
    m_flip[side] = 0.14f;
    sound_stereo(128, side ? 256 : 0);
    sound_events(VSFX_FLIPPER, 0);
}

static void menu_frame(void)
{
    char val[40];
    int n = m_page == PAGE_MAIN ? M_MAIN_N : O_N;
    m_time += s_dt;
    if (repeat(2, pad.up, 0)) { m_item = (m_item + n - 1) % n; menu_flip(0); }
    if (repeat(3, pad.down, 0)) { m_item = (m_item + 1) % n; menu_flip(1); }
    if (menu_has_value()) {
        if (repeat(0, pad.left, 0)) { menu_change(-1); menu_flip(0); }
        if (repeat(1, pad.right, 0)) { menu_change(1); menu_flip(1); }
    }
    if (edge.b4 || edge.b3) {
        menu_flip(0); menu_flip(1);                                    /* Auswahl: beide Flipper */
        m_flip[0] = m_flip[1] = 0.18f;
        if (m_page == PAGE_MAIN) {
            if (m_item == M_PLAY) { settings_save(); start_game(); return; }
            if (m_item == M_TABLE) menu_change(1);
            else { m_ball_y = 1e9f; menu_open(PAGE_OPTIONS, 0); }
        } else if (menu_has_value()) menu_change(1);
        else if (m_item == O_ADJUST) { settings_save(); adjust_open(); return; }
        else if (m_item == O_ABOUT) { about_open(); return; }
        else { settings_save(); m_ball_y = 1e9f; menu_open(PAGE_MAIN, M_OPTIONS); }
    } else if (edge.b1 && m_page == PAGE_OPTIONS) { settings_save(); m_ball_y = 1e9f; menu_open(PAGE_MAIN, M_OPTIONS); }

    if (m_page == PAGE_MAIN) {
        menu_frame_art(1);
        menu_title();
        snprintf(val, sizeof val, "%d PLAYER%s", g_set.players, g_set.players > 1 ? "S" : "");
        menu_row(m_item == M_PLAY, Q4(60), -88, -30, "PLAY", val, 17);
        snprintf(val, sizeof val, "%d %s", g_set.table + 1, vp_tables[g_set.table].name);
        menu_row(m_item == M_TABLE, Q4(36), -88, -30, "TABLE", val, 15);
        menu_row(m_item == M_OPTIONS, Q4(12), -88, -30, "OPTIONS", NULL, 17);
        /* die vier besten Ergebnisse des gewaehlten Tisches */
        {   /* als Block in der Mitte: linksbuendig untereinander, die breiteste Zeile bestimmt den Rand */
            char line[HS_PLACES][40];
            int wmax = 0;
            for (int p = 0; p < HS_PLACES; p++) {
                const hs_entry_t *e = hs_get(g_set.table, p);
                if (e->score) snprintf(line[p], sizeof line[p], "%d %s %lu", p + 1, e->name, (unsigned long)e->score);
                else snprintf(line[p], sizeof line[p], "%d ...", p + 1);
                int w = text_width(line[p], 15);
                if (w > wmax) wmax = w;
            }
            for (int p = 0; p < HS_PLACES; p++)
                text_draw(-wmax / 2, Q4(-22 - 19 * p), line[p], 15, p == 0 && hs_get(g_set.table, p)->score ? 100 : 66);
        }
        text_center(0, Q4(-108), "STICK: CHOOSE   4: OK", 11, 55);
    } else {
        static const char *const ONOFF[2] = { "OFF", "ON" };
        static const char *const NAMES[O_N] = { "BALLS", "BALL SPEED", "PHYSICS", "NUDGE STRENGTH", "INVERT NUDGE", "TILT SENSOR", "PLUNGER", "ZOOM", "3D", "SOUND", "MUSIC", "MUSIC VOLUME",
                                                 "ADJUST SCREEN", "ABOUT", "BACK" };
        menu_frame_art(0);
        text_center(0, Q4(118), "OPTIONS", 30, 110);
        int first = m_item - O_VISIBLE / 2;
        if (first > O_N - O_VISIBLE) first = O_N - O_VISIBLE;
        if (first < 0) first = 0;
        for (int i = first; i < first + O_VISIBLE; i++) {
            const char *v = NULL;
            switch (i) {
            case O_BALLS:     v = g_set.balls_unlimited ? "FREE PLAY" : "3"; break;
            case O_SPEED:     v = SPEED_NAMES[g_set.speed]; break;
            case O_PHYSICS:   v = g_set.physics == PHYSICS_REALISTIC ? "REALISTIC" : "ARCADE"; break;
            case O_NUDGE_POWER:  v = NUDGE_NAMES[g_set.nudge_power]; break;
            case O_NUDGE_INVERT: v = ONOFF[g_set.nudge_invert]; break;
            case O_TILT:      v = TILT_NAMES[g_set.tilt_sens]; break;
            case O_PLUNGER:   v = g_set.plunger == PLUNGER_DIGITAL ? "DIGITAL" : "ANALOG"; break;
            case O_ZOOM:      if (g_set.zoom) snprintf(val, sizeof val, "%d.%dX", 1 + g_set.zoom / 5, g_set.zoom * 2 % 10); else snprintf(val, sizeof val, "OFF"); v = val; break;
            case O_3D:        v = V3D_NAMES[g_set.view3d]; break;
            case O_SOUND:     v = SOUND_NAMES[g_set.sound]; break;
            case O_MUSIC:     v = ONOFF[g_set.music]; break;
            case O_MUSIC_VOL: snprintf(val, sizeof val, "%d", g_set.music_vol); v = val; break;
            default: break;
            }
            menu_row(i == m_item, Q4(78 - 23 * (i - first)), -96, 14, NAMES[i], v, 16);
        }
        /* es geht nach oben bzw. unten weiter */
        if (first > 0) { gfx_line(Q4(104), Q4(86), Q4(110), Q4(94), 90); gfx_line(Q4(110), Q4(94), Q4(116), Q4(86), 90); }
        if (first + O_VISIBLE < O_N) { gfx_line(Q4(104), Q4(-58), Q4(110), Q4(-66), 90); gfx_line(Q4(110), Q4(-66), Q4(116), Q4(-58), 90); }
        {   /* Zustand der Originalaufnahmen fuer die Buchse - damit "kein Ton" nicht raten heisst */
            int loading, memory, k = sound_status(&loading, &memory);
            if (k >= 0) {
                if (!memory) snprintf(val, sizeof val, "JACK: NO SOUND MEMORY");
                else if (loading) snprintf(val, sizeof val, "JACK: LOADING SOUNDS %d/%d", k, sound_files());
                else if (k == 0) snprintf(val, sizeof val, "JACK: NO SOUND FILES FOUND");
                else snprintf(val, sizeof val, "JACK: %d/%d SOUND FILES", k, sound_files());
                text_center(0, Q4(-88), val, 11, 70);
            }
        }
        text_center(0, Q4(-108), "STICK: CHOOSE  4: OK  1: BACK", 11, 55);
    }
}

/* ---- About ---- */
static int ab_page;
static void about_open(void) { s_state = S_ABOUT; ab_page = 0; }

static void about_frame(void)
{
    static const char *const TEXT[3][5] = {
        { "VECTOR PINBALL", "BY BRIAN NENNINGER", "(DOZINGCAT) 2010-2026", "GITHUB.COM/DOZINGCAT", "/VECTOR-PINBALL" },
        { "SOUND AND MUSIC: PDX", "TWITTERING MACHINE", "TABLE 8: JACK MORGAN", "TABLE 10 FROM SPINBALL", "GCE 1983" },
        { "VECTREX VERSION", "OLIVER CARELL", "(ENDPRODUKT) " PORT_YEAR, "FREE SOFTWARE", "GNU GPL VERSION 3" },
    };
    m_time += s_dt;
    if (edge.b4 || edge.b3 || edge.right) { ab_page++; m_flip[1] = 0.18f; }
    if (edge.left && ab_page > 0) { ab_page--; m_flip[0] = 0.18f; }
    if (edge.b1 || ab_page > 2) { m_ball_y = 1e9f; menu_open(PAGE_OPTIONS, O_ABOUT); return; }
    menu_frame_art(0);
    text_center(0, Q4(118), "ABOUT", 30, 110);
    for (int i = 0; i < 5; i++) text_center(0, Q4(66 - 26 * i), TEXT[ab_page][i], i == 0 ? 17 : 15, i == 0 ? 127 : 96);
    text_center(0, Q4(-108), ab_page < 2 ? "4: NEXT  1: BACK" : "4: BACK", 11, 55);
}

/* ---- Spiel ---- */
static int s_over_ms;
static float s_shake_t, s_shake_x, s_shake_y;

static void autoplay(int *l, int *r)
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
    int jx = vpy_j1_x(), jy = vpy_j1_y();
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
static void draw_game(int dim, int balls, float dt)
{
    float k = s_shake_t > 0 ? s_shake_t / 0.14f : 0;
    if (s_shake_t > 0) s_shake_t -= dt;
    render_game_view(&F, dt, F.in_progress ? 1.0f + 0.2f * g_set.zoom : 1.0f, s_shake_x * k, s_shake_y * k);
    render_table(&F, dim);
    if (balls) render_balls(&F);
    render_clip_end();
}

/* Stoessel am Stick: nach unten ziehen spannt (je weiter, desto staerker), loslassen schiesst ab. Der Stick schnellt beim
 * Loslassen ueber ein paar Bilder zur Mitte, darum zaehlt der groesste Zug der letzten Bilder. Die Knoepfe schiessen wie
 * bisher mit voller Staerke. */
static void plunger(void)
{
    static float hist[6], charge;
    static int armed;
    int jy = vpy_j1_y();
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
    if (!vp_launch_ready(&F)) { armed = 0; pull = 0; }
    else if (pull > 0) {
        if (!armed) memset(hist, 0, sizeof hist);
        armed = 1;
        memmove(hist + 1, hist, sizeof hist - sizeof hist[0]);
        hist[0] = pull;
    } else if (armed) {
        float m = 0;
        for (int i = 0; i < 6; i++) if (hist[i] > m) m = hist[i];
        armed = 0;
        vp_launch_power(&F, m);
    }
    render_plunger(pull);
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

static void play_frame(void)
{
    int l = pad.b1, r = pad.b4;
    if (plat_test_int("AUTOPLAY", 0)) autoplay(&l, &r);
    if (edge.b2) { s_state = S_PAUSE; sound_silence(); return; }
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
    if (F.save_ms) {                                                 /* der Ball laeuft gerade durch die Truhe: die Runde ist NICHT vorbei */
        if ((s_now_ms / 180) & 1) { text_center(0, Q4(6), "BALL SAVE", 44, 127); text_center(12, Q4(6) + 10, "BALL SAVE", 44, 110); }
        else text_center(0, Q4(6), "BALL SAVE", 44, 96);
    }
    if (F.nplayers > 1) {
        /* Wer ist dran? Bei jedem Wechsel gross mitten im Bild, knapp zwei Sekunden lang */
        if (F.player != s_turn_shown) { s_turn_shown = F.player; s_turn_t = 1.8f; }
        if (s_turn_t > 0) {
            char who[12];
            s_turn_t -= s_dt;
            snprintf(who, sizeof who, "PLAYER %d", F.player + 1);
            text_center(0, Q4(6), who, 46, 127);
            text_center(12, Q4(6) + 10, who, 46, 110);
        }
    }
}

/* ---- Bestenliste: Initialen eingeben (bei mehreren Spielern der Reihe nach fuer jeden, der einen Platz erreicht hat) ---- */
static const char LETTERS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.- ";
static char e_name[4];
static int e_pos, e_place, e_player;
static uint32_t e_score;

static int letter_index(char c) { const char *p = strchr(LETTERS, c); return p ? (int)(p - LETTERS) : 0; }
static uint32_t final_score(int p) { int64_t s = vp_player_score(&F, p); return s > 0xFFFFFFFFLL ? 0xFFFFFFFFu : (uint32_t)(s < 0 ? 0 : s); }

/* naechster Spieler ab from, dessen Ergebnis in die Liste gehoert; keiner mehr: zurueck ins Menue */
static void entry_next(int from)
{
    if (!g_set.balls_unlimited)                              /* freies Spiel zaehlt nicht fuer die Liste */
        for (int p = from; p < F.nplayers; p++) {
            int place = hs_rank(g_set.table, final_score(p));
            if (place < 0) continue;
            if (F.nplayers > 1) memcpy(e_name, "   ", 4);      /* mehrere Spieler: jeder faengt leer an */
            else memcpy(e_name, hs_last_name(), 4);
            e_pos = 0; e_place = place; e_score = final_score(p); e_player = p;
            s_state = S_ENTRY;
            return;
        }
    load_table(g_set.table);
    m_ball_y = 1e9f;
    menu_open(PAGE_MAIN, M_PLAY);
}

static void entry_frame(void)
{
    char buf[64];
    const int n = (int)sizeof LETTERS - 1;
    int i = letter_index(e_name[e_pos]);
    /* Buchstabe: Stick hoch/runter oder die Flipperknoepfe (rechts = weiter, links = zurueck); Knopf 3 bestaetigt */
    if (repeat(2, pad.up, 0) || repeat(5, pad.b4, 0)) e_name[e_pos] = LETTERS[(i + 1) % n];
    if (repeat(3, pad.down, 0) || repeat(4, pad.b1, 0)) e_name[e_pos] = LETTERS[(i + n - 1) % n];
    if (edge.left && e_pos > 0) e_pos--;
    else if (edge.right && e_pos < 2) e_pos++;
    else if (edge.b3) {
        if (e_pos < 2) e_pos++;
        else { hs_insert(g_set.table, e_place, e_score, e_name); entry_next(e_player + 1); return; }
    }
    text_center(0, Q4(120), "NEW HIGH SCORE", 30, 120);
    if (F.nplayers > 1) snprintf(buf, sizeof buf, "PLAYER %d   PLACE %d", e_player + 1, e_place + 1);
    else snprintf(buf, sizeof buf, "PLACE %d", e_place + 1);
    text_center(0, Q4(86), buf, 18, 96);
    snprintf(buf, sizeof buf, "%lu", (unsigned long)e_score);
    text_center(0, Q4(60), buf, 22, 110);
    for (int k = 0; k < 3; k++) {
        char one[2] = { e_name[k], 0 };
        int x = Q4(-66 + 48 * k), blink = ((s_now_ms / 250) & 1);
        text_draw(x, Q4(-10), one, 80, k == e_pos ? 127 : 80);
        if (k == e_pos && blink) gfx_line(x, Q4(-20), x + 4 * 80, Q4(-20), 127);       /* Strich unter dem aktiven Buchstaben */
        else gfx_line(x, Q4(-20), x + 4 * 80, Q4(-20), 50);
    }
    text_center(0, Q4(-80), "FLIPPERS OR STICK: LETTER", 14, 60);
    text_center(0, Q4(-100), "BUTTON 3: OK   STICK LEFT: BACK", 14, 60);
}

static void over_frame(void)
{
    char buf[32];
    s_over_ms += (int)(s_dt * 1000);
    vp_set_flippers(&F, pad.b1, pad.b4);
    vp_advance(&F, s_dt, s_now_ms, game_speed());
    sound_events(F.sfx, F.score_hits);
    F.sfx = 0;
    draw_game(F.nplayers > 1 ? 50 : 0, 0, s_dt);
    if (F.nplayers > 1) {                                    /* Endstand aller Spieler */
        text_center(0, Q4(70), "GAME OVER", 34, 127);
        for (int p = 0; p < F.nplayers; p++) {
            snprintf(buf, sizeof buf, "P%d  %lu", p + 1, (unsigned long)final_score(p));
            text_center(0, Q4(30 - 26 * p), buf, 22, 115);
        }
    } else {
        render_hud(&F);
        if (!F.msg[0]) text_center(0, Q4(20), "GAME OVER", 34, 127);
    }
    if ((s_over_ms > 1200 && (edge.b1 || edge.b2 || edge.b3 || edge.b4)) || s_over_ms > (F.nplayers > 1 ? 12000 : 6000)) entry_next(0);
}

static void pause_frame(void)
{
    /* Stick hoch/runter waehlt, links/rechts aendert, wirkt sofort:
     *   ZOOM, 3D MODE  die Ansicht (wie im Menue OPTIONS), zum Ausprobieren mitten im Spiel
     * Schlagkraft und Neigung sind je Tisch fest abgestimmt (vp_phys.c, vp_slope) und stehen nicht mehr zur Wahl. */
    enum { R_ZOOM, R_3D, R_N };
    static int row;
    char buf[40], val[16];
    if (edge.up) row = (row + R_N - 1) % R_N;
    if (edge.down) row = (row + 1) % R_N;
    switch (row) {
    case R_ZOOM:
        if (edge.left) g_set.zoom = (uint8_t)((g_set.zoom + ZOOM_STEPS) % (ZOOM_STEPS + 1));
        if (edge.right) g_set.zoom = (uint8_t)((g_set.zoom + 1) % (ZOOM_STEPS + 1));
        break;
    default:
        if (edge.left) g_set.view3d = (uint8_t)((g_set.view3d + V3D_N - 1) % V3D_N);
        if (edge.right) g_set.view3d = (uint8_t)((g_set.view3d + 1) % V3D_N);
        break;
    }
    draw_game(45, 1, 0);
    text_center(0, Q4(66), "PAUSED", 34, 127);
#define PAUSE_ROW(r, y, name, fmt, ...) do {         snprintf(buf, sizeof buf, row == (r) ? name " < " fmt " >" : name "   " fmt, __VA_ARGS__);         text_center(0, Q4(y), buf, 17, row == (r) ? 127 : 70); } while (0)
    if (g_set.zoom) snprintf(val, sizeof val, "%d.%dX", 1 + g_set.zoom / 5, g_set.zoom * 2 % 10); else snprintf(val, sizeof val, "OFF");
    PAUSE_ROW(R_ZOOM, 30, "ZOOM", "%s", val);
    PAUSE_ROW(R_3D, 12, "3D MODE", "%s", V3D_NAMES[g_set.view3d]);
#undef PAUSE_ROW
    text_center(0, Q4(-22), "2 OR 4 RESUME  1 QUIT", 20, 100);
    if (edge.b4 || edge.b2) { settings_save(); s_state = S_PLAY; }
    else if (edge.b1) { settings_save(); sound_game_reset(); load_table(g_set.table); m_ball_y = 1e9f; menu_open(PAGE_MAIN, M_PLAY); }
}

/* ---- Bild einstellen ----
 * Alles wird am echten Tisch eingestellt (ruhender Ball, Punktezeile wie im Spiel) - kein Pruefbild. Jeder Punkt sagt in
 * Klartext, was er tut und worauf man am Tisch achten soll. Der Text steht MITTEN IM TISCH und waechst mit ihm: so bleibt
 * er sichtbar, auch wenn das Bild gross gezogen ist.
 *
 * CLOSE SHAPES und STRAIGHTEN sind Strahlwerte der KONSOLE (config/uvm2.cfg, gelten fuer alle Spiele), der Rest gehoert dem
 * Spiel (pinball/settings.bin). Zwei fruehere Punkte gibt es nicht mehr: LINE LENGTH (Strichlaenge der Roehre) machte auf der
 * Konsole einfach alles groesser oder kleiner, also dasselbe wie PICTURE SIZE; RECENTER COUNT liess das Bild zucken. */
enum { A_X, A_Y, A_SIZE, A_BRIGHT, A_BALL, A_SCORE_Y, A_BEAM_TAIL, A_BEAM_ZERO, A_REZERO_D, A_N };
typedef struct { const char *name, *hint; int console, lo, hi, step; } adj_t;
static const adj_t ADJ[A_N] = {
    { "MOVE LEFT/RIGHT",   "CENTER THE TABLE",              0, -40, 40, 1 },
    { "MOVE UP/DOWN",      "CENTER THE TABLE",              0, -40, 40, 1 },
    { "PICTURE SIZE",      "BIG, BUT NOTHING CUT OFF",      0, -40, 25, 1 },
    { "BRIGHTNESS",        "WALLS DIM, BALL BRIGHTEST",     0, -50, 30, 1 },
    { "BALL BRIGHTNESS",   "BRIGHT, BUT MUST NOT SMEAR",    0, -60, 30, 1 },
    { "SCORE HEIGHT",      "MOVE THE SCORE LINE",           0, -30, 30, 1 },
    { "CLOSE SHAPES",      "BALL AND BUMPERS MUST CLOSE",   1, 0, 2000, 8 },
    { "STRAIGHTEN",        "SCORE DIGITS IN A LEVEL ROW",   1, 0, 127, 1 },
    { "RECENTER DISTANCE", "LOWER IF PARTS SHIFT APART",    0, 0, 80, 1 },
};
static int a_item, a_beam0[PLAT_BEAM_N];
static settings_t a_saved;

static int a_get(int i)
{
    switch (i) {
    case A_X: return g_set.cal_dx;
    case A_Y: return g_set.cal_dy;
    case A_SIZE: return g_set.cal_size;
    case A_BRIGHT: return g_set.cal_bright;
    case A_BALL: return g_set.cal_ball;
    case A_SCORE_Y: return g_set.cal_hud_dy;
    case A_REZERO_D: return g_set.zero_jump;
    case A_BEAM_ZERO: return plat_beam_get(PLAT_BEAM_ZERO);
    default: return plat_beam_get(PLAT_BEAM_TAIL);
    }
}

static void a_set(int i, int v)
{
    if (v < ADJ[i].lo) v = ADJ[i].lo;
    if (v > ADJ[i].hi) v = ADJ[i].hi;
    switch (i) {
    case A_X: g_set.cal_dx = (int8_t)v; break;
    case A_Y: g_set.cal_dy = (int8_t)v; break;
    case A_SIZE: g_set.cal_size = (int8_t)v; break;
    case A_BRIGHT: g_set.cal_bright = (int8_t)v; break;
    case A_BALL: g_set.cal_ball = (int8_t)v; break;
    case A_SCORE_Y: g_set.cal_hud_dy = (int8_t)v; break;
    case A_REZERO_D: g_set.zero_jump = (uint8_t)v; break;
    case A_BEAM_ZERO: plat_beam_set(PLAT_BEAM_ZERO, v); break;
    default: plat_beam_set(PLAT_BEAM_TAIL, v); break;
    }
    plat_beam_init(g_set.zero_every, g_set.zero_jump);
    render_default_view(F.def);
}

static int a_default(int i)
{
    settings_t keep = g_set;
    int v;
    if (i == A_BEAM_ZERO) return a_beam0[PLAT_BEAM_ZERO];
    if (i == A_BEAM_TAIL) return a_beam0[PLAT_BEAM_TAIL];
    settings_defaults();
    v = a_get(i);
    g_set = keep;
    return v;
}

static void adjust_open(void)
{
    a_saved = g_set;
    for (int i = 0; i < PLAT_BEAM_N; i++) a_beam0[i] = plat_beam_get(i);
    a_item = plat_test_int("ADJUST_ITEM", 0);
    if (a_item < 0 || a_item >= A_N) a_item = 0;
    load_table(g_set.table);
    F.nplayers = 1;
    vp_start_game(&F, 1);                       /* echter Tisch im Spielzustand, damit das Bild dem Spiel entspricht */
    F.score = 1234567;
    s_state = S_ADJUST;
}

static void adjust_frame(void)
{
    char buf[48];
    if (repeat(2, pad.up, 0)) a_item = (a_item + A_N - 1) % A_N;
    if (repeat(3, pad.down, 0)) a_item = (a_item + 1) % A_N;
    const adj_t *a = &ADJ[a_item];
    if (repeat(0, pad.left, a->step > 1)) a_set(a_item, a_get(a_item) - a->step);
    if (repeat(1, pad.right, a->step > 1)) a_set(a_item, a_get(a_item) + a->step);
    if (edge.b3) a_set(a_item, a_default(a_item));
    if (edge.b4) {                                              /* speichern */
        settings_save();
        if (plat_beam_get(PLAT_BEAM_ZERO) != a_beam0[PLAT_BEAM_ZERO] || plat_beam_get(PLAT_BEAM_TAIL) != a_beam0[PLAT_BEAM_TAIL]) plat_beam_save();
        load_table(g_set.table); m_ball_y = 1e9f; menu_open(PAGE_OPTIONS, O_ADJUST); return;
    }
    if (edge.b1) {                                              /* verwerfen */
        g_set = a_saved;
        for (int i = 0; i < PLAT_BEAM_N; i++) plat_beam_set(i, a_beam0[i]);
        plat_beam_init(g_set.zero_every, g_set.zero_jump);
        load_table(g_set.table); m_ball_y = 1e9f; menu_open(PAGE_OPTIONS, O_ADJUST); return;
    }
    /* der echte Tisch: ein ruhender Ball etwas ueber der Mitte, Punktezeile wie im Spiel */
    render_default_view(F.def);
    if (F.nballs == 0) { vp_ball_t *b = vp_create_ball(&F, F.def->width * 0.5f, F.def->height * 0.5f + 1.2f); if (b) b->gscale = 0; }
    render_table(&F, 0);
    render_balls(&F);
    F.msg[0] = 0;
    render_hud(&F);
    {   /* Textblock mitten im Tisch; die Schrift waechst und schrumpft mit dem Bild */
        const int cx = render_x(F.def->width * 0.5f), cy = render_y(F.def->height * 0.5f);
        const int u = (int)(render_units() * 16);              /* eine Feldeinheit in Q4 */
        const int big = u / 10 + 1, mid = u / 15 + 1, small = u / 18 + 1;
        snprintf(buf, sizeof buf, "%s %d", a->name, a_get(a_item));
        text_center(cx, cy - u * 1, buf, big, 127);
        text_center(cx, cy - u * 5 / 2, a->hint, mid, 110);
        if (a->console) text_center(cx, cy - u * 15 / 4, "FOR ALL GAMES ON THIS CONSOLE", small, 80);
        snprintf(buf, sizeof buf, "%d/%d  STICK: PICK, CHANGE", a_item + 1, A_N);
        text_center(cx, cy - u * 21 / 4, buf, small, 70);
        text_center(cx, cy - u * 25 / 4, "4 SAVE  1 CANCEL  3 RESET", small, 70);
    }
}

/* ---- Rahmen ---- */
static void setup(void)
{
    settings_load();
    hs_load();
    if (g_set.table >= vp_num_tables) g_set.table = 0;
    plat_beam_init(g_set.zero_every, g_set.zero_jump);
    sound_init();
    s_last_us = plat_micros();
    {
        int t = plat_test_int("TABLE", 0);
        if (t >= 1 && t <= vp_num_tables) g_set.table = (uint8_t)(t - 1);
        g_set.zoom = (uint8_t)plat_test_int("ZOOM", g_set.zoom);
        g_set.view3d = (uint8_t)plat_test_int("3D", g_set.view3d);
        if (g_set.view3d >= V3D_N) g_set.view3d = V3D_ALL;
        g_set.info = (uint8_t)(plat_test_int("INFO", 0) != 0);
        if (g_set.zoom > ZOOM_STEPS) g_set.zoom = ZOOM_STEPS;
        g_set.physics = (uint8_t)(plat_test_int("REAL", g_set.physics) != 0);
        g_set.players = (uint8_t)plat_test_int("PLAYERS", g_set.players);
        if (g_set.players < 1 || g_set.players > VP_MAX_PLAYERS) g_set.players = 1;
        load_table(g_set.table);
        if (plat_test_int("ADJUST", 0)) adjust_open();
        else if (plat_test_int("ENTRY", 0)) { F.nplayers = 1; F.score = 1234567; entry_next(0); }
        else if (plat_test_int("ABOUT", 0)) { about_open(); ab_page = plat_test_int("ABOUT", 1) - 1; }
        else if (t) {
            start_game();
            if (plat_test_int("BL", -1) >= 0) {                      /* Testbild: Ball an eine Stelle setzen (VP_BX/VP_BY in Zehnteln, VP_BL = Ebene) */
                vp_ball_t *b = vp_create_ball(&F, plat_test_int("BX", 0) / 10.0f, plat_test_int("BY", 0) / 10.0f);
                if (b) b->layer = (int8_t)plat_test_int("BL", 0);
            }
        }
        else if (plat_test_int("INTRO", 1)) { intro_start(); s_state = S_INTRO; }
        else menu_open(plat_test_int("PAGE", 0) ? PAGE_OPTIONS : PAGE_MAIN, plat_test_int("ITEM", M_PLAY));
    }
}

static void loop(void)
{
    uint32_t now = plat_micros();
    uint32_t d = now - s_last_us;
    s_last_us = now;
    if (d > 200000) d = 200000;
    s_dt = d / 1e6f;
    s_now_ms += d / 1000;
    if (++s_fps_n >= 30) { uint32_t el = now - s_fps_t0; s_fps = el ? (int)(30000000u / el) : 0; s_fps_t0 = now; s_fps_n = 0; }

    read_pad();
    {   /* Reset-Knopf der Konsole lange gedrueckt: zurueck ins Menue, von ueberall */
        int held = plat_reset_ms();
        if (held >= 500 && s_state != S_INTRO) {
            sound_silence(); sound_game_reset();
            load_table(g_set.table);
            m_ball_y = 1e9f;
            menu_open(PAGE_MAIN, M_PLAY);
        }
    }
    gfx_begin();
    gfx_occ_enable(plat_test_int("OCCLUSION", 1));                   /* immer an; nur der Simulator kann sie zum Messen abschalten */
    switch (s_state) {
    case S_MENU:   menu_frame(); break;
    case S_PLAY:   play_frame(); break;
    case S_PAUSE:  pause_frame(); break;
    case S_OVER:   over_frame(); break;
    case S_ENTRY:  entry_frame(); break;
    case S_ABOUT:  about_frame(); break;
    case S_INTRO:
        if (intro_frame(s_dt, edge.b1 || edge.b2 || edge.b3 || edge.b4)) {
            menu_open(PAGE_MAIN, M_PLAY);
            edge.b1 = edge.b2 = edge.b3 = edge.b4 = 0;
        }
        break;
    default:       adjust_frame(); break;
    }
    draw_info();
    {
        uint32_t t0 = plat_clock_us();
        gfx_flush();
        s_flush_us = plat_clock_us() - t0;
        t0 = plat_clock_us();
        plat_redraw_last(s_state == S_INTRO);
        sound_frame(s_state != S_INTRO);
        s_audio_us = plat_clock_us() - t0;
    }
#ifndef __arm__
    if (plat_test_int("PROF", 0)) prof_report();
#endif
}

int main(void)
{
    vpy_run(setup, loop);
    return 0;
}
