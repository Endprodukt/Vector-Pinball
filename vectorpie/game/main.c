/*
 * main.c - Vector Pinball fuer VectorPie, Automatenfassung: Rahmen (Start, Schleife, Steuerung, Zeit) und die Zustandsmaschine.
 * Die Zustaende selbst: attract.c (Titel, Demo, Spielerzahl, Tischwahl), play.c (Spiel), entry.c (Initialen),
 * service.c (Service-Menue); art.c zeichnet die Kulisse. Was sie voneinander brauchen, steht in app.h.
 * Hervorgegangen aus vectrex/game/main.c (Spiel, Bestenliste, Bildeinstellung, Optionen), aber als Muenzautomat gebaut:
 *
 *   ATTRACT   laeuft in Schleife: Titelbild - ein Tisch spielt sich selbst - seine Bestenliste - naechster Tisch.
 *             INSERT COIN; mit Credits (oder FREE PLAY) blinkt START.
 *   START     Zahl der Spieler waehlen (1-4, soviel die Credits hergeben), dann den Tisch (Vorschau und Name), dann das
 *             Spiel. Keine Pause. Nach dem Spiel Initialen, dann wieder ATTRACT.
 *   SERVICE   F2, Tab (ui_configure), immer (aus dem Spiel: es wartet, F2 fuehrt zurueck): alle Einstellungen, Lautstaerken, Belegung aller Knoepfe.
 *
 * Steuerung im Spiel (belegbar, platform/vpie.h): Knopf 1 / Knopf 4 linker / rechter Flipper (starten auch den Ball),
 * Knopf 3 Ball starten, START wechselt die Ansicht (Bildbreite, naeher, ganz nah, ganzer Tisch), Stick links/rechts/hoch gegen den Tisch stossen (zu oft: TILT), Stick unten Stoessel.
 * Die Physik laeuft in festen Schritten nach der Echtzeit (480 je Sekunde), unabhaengig von der Bildrate.
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

int s_service_from;                     /* Service mit F2 aus diesem Zustand geoeffnet (0 = aus Attract/Auswahl) */
int s_state;
vp_field_t F;
uint32_t s_last_us, s_now_ms;
float s_dt;

/* Tempo des Balls (Option BALL SPEED). MEDIUM ist das Tempo des Originals. FAST ist 1,2-mal so schnell und entspricht einem echten
 * Tisch: 20 Einheiten Breite sind gut 20 Zoll, eine Einheit also etwa ein Zoll, und mit der Schwerkraft des Originals rollt der Ball
 * wie auf 4,5 Grad Neigung - echte Tische stehen bei 6,5 Grad. SLOW ist fuer Anfaenger und Kinder. Die Flugbahnen bleiben gleich. */
float game_speed(void)
{
    static const float SPEED[SPEED_N] = { 0.8f, 1.0f, 1.2f };
    return SPEED[g_set.speed];
}

/* ---- Eingabe ---- */
pad_t pad, edge;
static pad_t prev;
static int rep_t[6];                      /* Wiederholung fuer links/rechts/hoch/runter (Menue), Knopf 1/4 (Initialen) */

static void read_pad(void)
{
    int jx = vpie_stick_x(), jy = vpie_stick_y();
    prev = pad;
    pad.left = jx < -50; pad.right = jx > 50; pad.up = jy > 50; pad.down = jy < -50;
    pad.b1 = vpie_button(1); pad.b2 = vpie_button(2); pad.b3 = vpie_button(3); pad.b4 = vpie_button(4);
    edge.left = pad.left && !prev.left; edge.right = pad.right && !prev.right;
    edge.up = pad.up && !prev.up; edge.down = pad.down && !prev.down;
    edge.b1 = pad.b1 && !prev.b1; edge.b2 = pad.b2 && !prev.b2; edge.b3 = pad.b3 && !prev.b3; edge.b4 = pad.b4 && !prev.b4;
}

/* Tastenwiederholung: 1 beim ersten Druck, dann nach 0,4 s alle 60 ms (fast = alle 25 ms) */
int repeat(int idx, int held, int fast)
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

uint32_t s_audio_us, s_logic_us, s_draw_us, s_flush_us;   /* Zeiten des letzten Bildes */
/* VP_PROF=1: alle 60 Bilder die mittleren Zeiten je Abschnitt, um Engpaesse zu finden */
static void prof_report(void)
{
    static uint32_t acc[4], n;
    acc[0] += s_logic_us; acc[1] += s_draw_us; acc[2] += s_flush_us; acc[3] += s_audio_us;
    if (++n < 60) return;
    fprintf(stderr, "prof  logic %4u  draw %4u  flush %4u  audio %4u us  (strokes %d, occluders %d, cut %d, tests %d convex + %d concave)\n",
            acc[0] / n, acc[1] / n, acc[2] / n, acc[3] / n, gfx_stats.strokes, gfx_stats.occluders, gfx_stats.cut_lines, gfx_stats.span_tests, gfx_stats.poly_tests);
    memset(acc, 0, sizeof acc); n = 0;
}
/* Messzeile (VP_INFO=1): F = Bilder/s, S = Striche, J = Spruenge, LIT / BLK = gezeichnete und unsichtbare Weglaenge des
 * letzten Bildes in Tausend Platineneinheiten (eine Bildhoehe = 4), X = verlorene Striche (muss 0 sein), L = Spiel und
 * Zeichnen in 0,1 ms. Auf der Platine bestimmt die Weglaenge die Zeit je Bild: wird das Bild grob oder dunkel, ist LIT zu gross. */
static void draw_info(void)
{
    char buf[80];
    unsigned lines, lit, blank, drop;
    if (!g_set.info) return;
    vpie_stats(&lines, &lit, &blank, &drop);
    snprintf(buf, sizeof buf, "F%d S%d J%d LIT%u BLK%u X%u L%u", s_fps, gfx_stats.strokes, gfx_stats.jumps, lit / 1000u, blank / 1000u,
             drop + (unsigned)gfx_stats.lost, (s_logic_us + s_draw_us + s_flush_us) / 100u);
    gfx_color(C_DIM);
    text_center(0, Q4(-187), buf, 10, 90);
}

/* Stoss gegen den Tisch: STRONG ist die Staerke der ersten Fassung, die sich uebertrieben anfuehlte */
const float NUDGE_SCALE[NUDGE_N] = { 0.35f, 0.6f, 1.0f, 1.0f };

void load_table(int t)
{
    vp_load(&F, t, plat_micros() | 1u);
    vp_set_real_physics(&F, g_set.physics == PHYSICS_REALISTIC);
    F.nudge_scale = NUDGE_SCALE[g_set.nudge_power];
    F.tilt_sens = g_set.tilt_sens;
    F.launch_k = t == 4 ? 0.8f : 1.0f;                     /* Tisch 5: der Stoessel schoss zu stark */
    render_default_view(F.def);
    render_zoom_reset(&F);
}

/* ---- Hilfen fuer alle Zustaende ---- */
int s_players = 1;                       /* gewaehlte Spielerzahl */
int s_turn_shown;                        /* Spieler, fuer den der grosse Hinweis schon kam (-1 = noch keiner) */
float s_turn_t;
static uint32_t a_rnd = 0x2468ACEu;
float frnd(void) { a_rnd = a_rnd * 1664525u + 1013904223u; return (float)(a_rnd >> 8) * (1.0f / 16777216.0f); }

/* Ton ausserhalb eines Spiels (Muenze, Auswahl). Der Zaehler der Punkte-Toene geht dabei auf 0: sonst holte der Ton nach
 * einem stummen Demospiel alle dessen Treffer auf einmal nach. */
void blip(uint32_t sfx) { sound_stereo(128, 128); sound_events(sfx, 0); }

void start_game(void)
{
    load_table(g_set.table);
    s_turn_shown = -1;
    fx_reset();
    F.nplayers = s_players;
    vp_start_game(&F, g_set.balls_unlimited);
    F.now_ms = s_now_ms;
    vp_message(&F, F.def->name, 1800, 0);
    F.sfx |= VSFX_START;
    sound_game_reset();
    sound_music(g_set.table + 1);
    s_state = S_PLAY;
}

/* ---- Rahmen ---- */
static void setup(void)
{
    settings_load();
    hs_load();
    if (g_set.table >= vp_num_tables) g_set.table = 0;
    if (!vpie_file_exists("settings.bin")) {
        /* Vorgaben fuer den Automaten, solange noch nichts gespeichert ist: am Bedienpult sitzt meist ein Schaltknueppel
         * (der Stoessel zieht dann beim Halten auf), und die Platine zeichnet mehr als die Vectrex */
        g_set.plunger = PLUNGER_DIGITAL;
    }
    /* keine Wahl mehr: volle Zeichnung (die Platine schafft sie), Ton und Musik an - leise stellt man sie ueber die Lautstaerken */
    g_set.detail = DETAIL_HIGH;
    g_set.sound = SOUND_BOTH;
    g_set.music = 1;
    if (g_set.music_vol > 10) g_set.music_vol = 10;
    sound_init();
    s_last_us = plat_micros();
    {
        int t = plat_test_int("TABLE", 0);
        if (t >= 1 && t <= vp_num_tables) g_set.table = (uint8_t)(t - 1);
        g_set.detail = (uint8_t)plat_test_int("DETAIL", g_set.detail);
        if (g_set.detail >= DETAIL_N) g_set.detail = DETAIL_MED;
        g_set.zoom = (uint8_t)plat_test_int("ZOOM", g_set.zoom);
        g_set.view3d = (uint8_t)plat_test_int("3D", g_set.view3d);
        if (g_set.view3d >= V3D_N) g_set.view3d = V3D_ALL;
        g_set.info = (uint8_t)(plat_test_int("INFO", 0) != 0);
        if (g_set.zoom > ZOOM_STEPS) g_set.zoom = ZOOM_STEPS;
        g_set.physics = (uint8_t)(plat_test_int("REAL", g_set.physics) != 0);
        g_set.players = (uint8_t)plat_test_int("PLAYERS", g_set.players);
        if (g_set.players < 1 || g_set.players > VP_MAX_PLAYERS) g_set.players = 1;
        s_players = g_set.players;
        at_table = plat_test_int("ATTRACT_TABLE", 1) - 1;
        if (at_table < 0 || at_table >= vp_num_tables) at_table = 0;
        load_table(g_set.table);
        if (plat_test_int("ENTRY", 0)) { F.nplayers = 1; F.score = 1234567; entry_next(0); }
        else if (plat_test_int("ABOUT", 0)) { about_open(); ab_page = plat_test_int("ABOUT", 1) - 1; }
        else if (t) {
            start_game();
            if (plat_test_int("BL", -1) >= 0) {                      /* Testbild: Ball an eine Stelle setzen (VP_BX/VP_BY in Zehnteln, VP_BL = Ebene) */
                vp_ball_t *b = vp_create_ball(&F, plat_test_int("BX", 0) / 10.0f, plat_test_int("BY", 0) / 10.0f);
                if (b) b->layer = (int8_t)plat_test_int("BL", 0);
            }
        }
        else if (plat_test_int("PAGE", 0)) menu_open(PAGE_OPTIONS, plat_test_int("ITEM", 0));     /* Service */
        else if (plat_test_int("SELECT", 0) == 1) players_open();
        else if (plat_test_int("SELECT", 0) == 2) table_open();
        else { attract_open(); if (plat_test_int("ATTRACT", 0)) attract_page(plat_test_int("ATTRACT", 0)); }
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
    {
        const int idle = s_state == S_ATTRACT || s_state == S_PLAYERS || s_state == S_TABLE;
        if (vpie_coins()) {                                          /* Muenze: immer hoerbar; im Attract zurueck zum Titel */
            if (s_state == S_PLAY) F.sfx |= VSFX_MESSAGE; else blip(VSFX_MESSAGE);
            if (s_state == S_ATTRACT && at_page != AT_TITLE) attract_page(AT_TITLE);
        }
        if (vpie_service()) {                                        /* Service: F2, immer */
            if (idle) { m_ball_y = 1e9f; load_table(g_set.table); menu_open(PAGE_OPTIONS, 0); }
            else if (s_state == S_MENU || s_state == S_ABOUT) service_close();
            else {                                                   /* aus dem laufenden Spiel: es wartet, bis der Service zu ist */
                s_service_from = s_state; m_ball_y = 1e9f; menu_open(PAGE_OPTIONS, 0);
            }
        }
        vpie_play_mode(s_state == S_PLAY);
        vpie_lamps(s_state == S_ATTRACT);                            /* START blinkt, sobald ein Spiel bezahlt ist */
    }
    gfx_begin();
    gfx_occ_enable(plat_test_int("OCCLUSION", 1));                   /* immer an; nur zum Messen abschaltbar */
    switch (s_state) {
    case S_ATTRACT: attract_frame(); break;
    case S_PLAYERS: players_frame(); break;
    case S_TABLE:   table_frame(); break;
    case S_MENU:    menu_frame(); break;
    case S_PLAY:    play_frame(); break;
    case S_OVER:    over_frame(); break;
    case S_ENTRY:   entry_frame(); break;
    default:        about_frame(); break;
    }
    draw_info();
    {
        uint32_t t0 = plat_clock_us();
        gfx_flush();
        s_flush_us = plat_clock_us() - t0;
        t0 = plat_clock_us();
        sound_frame(1);
        s_audio_us = plat_clock_us() - t0;
    }
    if (plat_test_int("PROF", 0)) prof_report();
}

int main(int argc, char **argv)
{
    return vpie_run(setup, loop, argc, argv);
}
