/*
 * service.c - das Service-Menue (Tab): alle Einstellungen, Lautstaerken, Belegung der Knoepfe (CONTROLS), ABOUT.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "plat.h"
#include "vpie.h"
#include "vpbind.h"

#include <SDL.h>
#include "font.h"
#include "render.h"
#include "hiscore.h"
#include "sound.h"
#include "segdisp.h"
#include "fx.h"
#include "fmath.h"

/* ---- Service-Menue ----
 * Eine Liste mit Flipper-Gesicht: der Rahmen ist der Umriss eines Tisches, unten sitzen zwei Flipper (sie schlagen, wenn man
 * waehlt), und der Zeiger ist eine Kugel, die von Zeile zu Zeile rollt.
 *   Stick hoch/runter   Punkt waehlen
 *   Stick links/rechts  Wert aendern (Zeilen mit Wert zeigen ihn dann zwischen < >)
 *   START (oder OK)     ausfuehren: Seite wechseln - bei einer Zeile mit Wert: naechster Wert
 *   Tab, EXIT           speichern und zurueck in den Attract */
enum { O_FREEPLAY, O_ATTRACT, O_MASTER, O_EFFECTS, O_MUSIC_VOL, O_BALLS, O_SPEED, O_PHYSICS, O_NUDGE_POWER, O_NUDGE_INVERT, O_TILT, O_PLUNGER,
       O_ZOOM, O_3D, O_CONTROLS, O_ABOUT, O_BACK, O_N };
#define O_VISIBLE 7                      /* so viele Zeilen der Seite sind zugleich zu sehen */
/* Masse der Service-Seiten. Im Querformat steht die Liste gross ueber die ganze Bildbreite (die kleine Schrift der
 * Vectrex-Fassung war auf der Roehre zu klein: Olli, 2026-10-10); hochkant bleibt das schmale Bild mit dem Tischrahmen. */
typedef struct { int xl, xv, y0, step, lab, val, rows, title_y, title_u, note_y, note_u, hint_y, hint_u, ball; } mlay_t;
static mlay_t m_lay(void)
{
    static const mlay_t WIDE = { -226, 22, 98, 31, 25, 22, 8, 142, 36, -148, 15, -172, 16, 7 };
    static const mlay_t TALL = { -96, 14, 78, 23, 19, 16, 7, 118, 30, -88, 11, -108, 11, 5 };
    return vpie_vertical() ? TALL : WIDE;
}
/* Hintergrund der Service-Seiten: hochkant der Tischrahmen mit den Fingern, quer das Lauflicht um das Bild */
void menu_back(void) { if (vpie_vertical()) menu_frame_art(0); else chaser(); }
static int m_page, m_item, m_song;       /* m_song: im Service laeuft gerade ein Lied zum Einstellen der Lautstaerke */
float m_ball_y = 1e9f;
static const char *const SPEED_NAMES[SPEED_N] = { "SLOW", "MEDIUM", "FAST" };
static const char *const NUDGE_NAMES[NUDGE_N] = { "WEAK", "NORMAL", "STRONG", "ANALOG" };
static const char *const TILT_NAMES[TILT_N] = { "LOW", "MEDIUM", "HIGH" };
static const char *const V3D_NAMES[V3D_N] = { "OFF", "FLIPPERS", "OBJECTS", "EVERYTHING" };



void menu_open(int page, int item)
{
    s_state = S_MENU; m_page = page; m_item = item;
    m_song = 0;
    sound_music(-1);
}

/* Service verlassen: alles speichern, zurueck in den Attract */
void service_close(void)
{
    settings_save();
    vpie_settings_save();
    m_ball_y = 1e9f;
    if (s_service_from) {                                            /* mit F2 aus dem Spiel gekommen: dorthin zurueck */
        s_state = s_service_from; s_service_from = 0;
        if (s_state == S_PLAY && !F.tilted) sound_music(g_set.table + 1);
        return;
    }
    load_table(g_set.table);
    attract_open();
}

/* 1 = die Zeile hat einen Wert, den links/rechts aendern */
static int menu_has_value(void) { return m_item < O_CONTROLS; }

static void menu_change(int d)
{
    switch (m_item) {
    case O_FREEPLAY:  vpie_opt_set(VPIE_OPT_FREE_PLAY, !vpie_opt(VPIE_OPT_FREE_PLAY)); break;
    case O_ATTRACT:   vpie_opt_set(VPIE_OPT_ATTRACT_SOUND, !vpie_opt(VPIE_OPT_ATTRACT_SOUND)); break;
    case O_BALLS:     g_set.balls_unlimited ^= 1; break;
    case O_SPEED:     g_set.speed = (uint8_t)((g_set.speed + SPEED_N + d) % SPEED_N); break;
    case O_PHYSICS:   g_set.physics = (uint8_t)((g_set.physics + 1) % PHYSICS_N); break;
    case O_NUDGE_POWER:  g_set.nudge_power = (uint8_t)((g_set.nudge_power + NUDGE_N + d) % NUDGE_N); break;
    case O_NUDGE_INVERT: g_set.nudge_invert ^= 1; break;
    case O_TILT:      g_set.tilt_sens = (uint8_t)((g_set.tilt_sens + TILT_N + d) % TILT_N); break;
    case O_PLUNGER:   g_set.plunger ^= 1; break;
    case O_ZOOM: {                                           /* nicht unter die Stufe, die das gewaehlte 3D braucht */
        if (!vpie_vertical()) { g_set.zoom = g_set.zoom ? 0 : 3; break; }   /* Querformat: nur an (Bildbreite) oder aus */
        const int lo = settings_min_zoom(), n = ZOOM_STEPS + 1 - lo;
        g_set.zoom = (uint8_t)(lo + (g_set.zoom - lo + n + d) % n);
        break;
    }
    case O_3D:
        g_set.view3d = (uint8_t)((g_set.view3d + V3D_N + d) % V3D_N);
        if (g_set.zoom < settings_min_zoom()) g_set.zoom = (uint8_t)settings_min_zoom();
        break;
    /* Lautstaerken 0..10, an den Enden ist Schluss (kein Sprung von 10 auf 0); man hoert sofort, was man einstellt */
    case O_MASTER:    vpie_opt_set(VPIE_OPT_MASTER_VOL, vpie_opt(VPIE_OPT_MASTER_VOL) + d); blip(VSFX_SCORE); break;
    case O_EFFECTS:   vpie_opt_set(VPIE_OPT_EFFECTS_VOL, vpie_opt(VPIE_OPT_EFFECTS_VOL) + d); blip(VSFX_SCORE); break;
    case O_MUSIC_VOL: if (g_set.music_vol + d >= 0 && g_set.music_vol + d <= 10) g_set.music_vol = (uint8_t)(g_set.music_vol + d); break;
    default: break;
    }
}

void menu_row(int sel, int y, const char *label, const char *value)
{
    const mlay_t L = m_lay();
    char buf[192];
    int b = sel ? 127 : 88;
    gfx_color(sel ? C_WHITE : C_DIM);
    text_draw(Q4(L.xl), y, label, L.lab, b);
    if (sel) {
        float target = (float)(y + L.lab * 3);
        if (m_ball_y > 1e8f) m_ball_y = target;
        m_ball_y += (target - m_ball_y) * (s_dt * 14.0f > 1.0f ? 1.0f : s_dt * 14.0f);
        /* rollt gemaechlich auf der Stelle, und schneller, solange sie zur naechsten Zeile laeuft */
        static float roll;
        float gap = target - m_ball_y;
        roll += s_dt * 2.2f + (gap < 0 ? -gap : gap) * 0.004f;
        roll_ball(Q4(L.xl) - Q4(L.ball) * 5 / 2, (int)m_ball_y, Q4(L.ball), roll);
    }
    if (!value) return;
    if (sel) snprintf(buf, sizeof buf, "< %s >", value); else snprintf(buf, sizeof buf, "  %s", value);
    gfx_color(sel ? C_VALUE : C_DIM);
    {   /* ein langer Wert (mehrere Eingaenge) kleiner, statt ueber den Rand */
        int u = L.val;
        const int room = Q4(-L.xl) - Q4(L.xv), w = text_width(buf, u);
        if (w > room && w > 0) u = (int)((long)u * room / w);
        text_draw(Q4(L.xv), y + (L.lab - u) * 3, buf, u, b);
    }
}

/* ---- Seite CONTROLS: welcher Eingang des Automaten was tut ----
 * Wie in allen Vektorspielen des Automaten: Zeile waehlen, START (oder Enter), dann den Knopf druecken - der erste, seit
 * der Cursor auf der Zeile steht, ersetzt, jedes weitere START fuegt einen hinzu. Links / rechts: NONE oder die Vorgabe;
 * Entf: die Vorgabe. Ein Eingang darf mehrere Funktionen haben. Gespeichert wird beim Verlassen der Seite. */
/* Zeilen der Seite CONTROLS: alle Funktionen, die man belegen kann (platform/vpie.h), und BACK */
static const struct { const char *name; int func; } CROW[] = {
    { "LEFT FLIPPER", VPIE_F_LEFT_FLIP }, { "RIGHT FLIPPER", VPIE_F_RIGHT_FLIP },
    { "NUDGE LEFT", VPIE_F_NUDGE_L }, { "NUDGE RIGHT", VPIE_F_NUDGE_R }, { "NUDGE UP", VPIE_F_NUDGE_UP },
    { "PLUNGER", VPIE_F_PLUNGER }, { "NUDGE AXIS", VPIE_F_NUDGE_AXIS }, { "YOKE AXIS", VPIE_F_YOKE_AXIS },
    { "START", VPIE_F_START }, { "COIN", VPIE_F_COIN }, { "SERVICE", VPIE_F_SERVICE }, { "BACK", 0 }
};
#define C_N ((int)(sizeof CROW / sizeof CROW[0]))
static int c_cap;                        /* 0 = nichts, 1 = fuer die Zeile wird ein Eingang erwartet, 2 = danach: bis alles losgelassen ist */
static int c_adding = -1;                /* die Zeile, die seit der Cursor dort steht einen Eingang bekam: der naechste kommt dazu */

/* Was waehrend des Belegens in der Zeile steht: bei den Achsen, in welche Richtung man sie bewegen soll */
static const char *capture_prompt(int func)
{
    if (func == VPIE_F_NUDGE_AXIS) return "MOVE RIGHT";
    if (func == VPIE_F_YOKE_AXIS) return "PULL BACK";
    return "PRESS THE BUTTON";
}

static void controls_frame(void)
{
    const mlay_t L = m_lay();
    const int rows = vpie_vertical() ? 9 : L.rows, step = vpie_vertical() ? 21 : L.step, y0 = vpie_vertical() ? 92 : L.y0 + 4;
    char val[160];
    int leave = 0;
    m_time += s_dt;
    if (c_cap == 1) {
        const int r = vpie_capture(CROW[m_item].func);
        if (r) c_cap = 2;
        if (r > 0 && vpie_func_is_axis(CROW[m_item].func)) {
            /* eine Achse belegt: Stoss und Stoessel arbeiten dann analog (so ist ein Yoke gemeint) */
            g_set.nudge_power = NUDGE_ANALOG;
            if (CROW[m_item].func == VPIE_F_YOKE_AXIS) g_set.plunger = PLUNGER_ANALOG;
        }
    }
    else if (c_cap == 2) { if (vpie_controls_idle()) c_cap = 0; }
    else {
        if (repeat(2, pad.up, 0)) { m_item = (m_item + C_N - 1) % C_N; menu_flip(0); c_adding = -1; }
        if (repeat(3, pad.down, 0)) { m_item = (m_item + 1) % C_N; menu_flip(1); c_adding = -1; }
        if ((edge.left || edge.right) && CROW[m_item].func) {                       /* NONE, oder die Vorgabe */
            vpie_binding_toggle(CROW[m_item].func); menu_flip(0); c_adding = -1;
        }
        if (CROW[m_item].func && (vpb_key_pressed(SDL_SCANCODE_DELETE) || vpb_key_pressed(SDL_SCANCODE_KP_PERIOD))) {
            vpie_binding_standard(CROW[m_item].func); menu_flip(0); c_adding = -1;
        }
        if (vpie_start()) {
            m_flip[0] = m_flip[1] = 0.18f;
            if (!CROW[m_item].func) leave = 1;
            else { c_cap = 1; vpie_capture_begin(c_adding != m_item); c_adding = m_item; }
        }
    }
    if (leave) { settings_save(); vpie_settings_save(); m_ball_y = 1e9f; menu_open(PAGE_OPTIONS, O_CONTROLS); return; }
    menu_back();
    gfx_color(C_TITLE);
    text_center(0, Q4(L.title_y), "CONTROLS", L.title_u, 110);
    int first = m_item - rows / 2;
    if (first > C_N - rows) first = C_N - rows;
    if (first < 0) first = 0;
    for (int i = first; i < first + rows && i < C_N; i++) {
        const char *v = NULL;
        if (CROW[i].func) {
            if (c_cap == 1 && i == m_item) v = ((s_now_ms / 250) & 1) ? capture_prompt(CROW[i].func) : "";
            else v = vpie_binding_name(CROW[i].func, val, sizeof val);
        }
        menu_row(i == m_item, Q4(y0 - step * (i - first)), CROW[i].name, v);
    }
    {   /* es geht nach oben bzw. unten weiter */
        const int ax = -L.xl - 12, top = y0 + 12, bot = y0 - step * (rows - 1) - 6;
        gfx_color(C_ACCENT);
        if (first > 0) { gfx_line(Q4(ax - 7), Q4(top - 8), Q4(ax), Q4(top), 100); gfx_line(Q4(ax), Q4(top), Q4(ax + 7), Q4(top - 8), 100); }
        if (first + rows < C_N) { gfx_line(Q4(ax - 7), Q4(bot + 8), Q4(ax), Q4(bot), 100); gfx_line(Q4(ax), Q4(bot), Q4(ax + 7), Q4(bot + 8), 100); }
    }
    gfx_color(C_ACCENT);
    text_center(0, Q4(L.note_y), c_cap == 1 ? (c_adding == m_item && m_item >= 0 ? "PRESS THE BUTTON TO ADD" : "PRESS THE BUTTON")
                                            : "LEFT RIGHT: NONE OR STANDARD   DEL: STANDARD", L.note_u, 80);
    text_center(0, Q4(L.hint_y), c_cap == 1 ? "START 1: BACK   DEL: STANDARD   START 2: NONE" : "STICK: ROW   START: ADD BUTTON", L.hint_u, 96);
}

void menu_frame(void)
{
    static const char *const ONOFF[2] = { "OFF", "ON" };
    static const char *const NAMES[O_N] = { "FREE PLAY", "ATTRACT SOUND", "MASTER VOLUME", "EFFECTS VOLUME", "MUSIC VOLUME", "BALLS", "BALL SPEED",
                                             "PHYSICS", "NUDGE STRENGTH", "INVERT NUDGE", "TILT SENSOR", "PLUNGER", "ZOOM", "3D",
                                             "CONTROLS", "ABOUT", "EXIT" };
    char val[40];
    if (m_page == PAGE_CONTROLS) { controls_frame(); return; }
    m_time += s_dt;
    {   /* in den Zeilen MASTER und MUSIC laeuft das Lied des gewaehlten Tisches: man hoert, was man einstellt */
        const int want = m_item == O_MASTER || m_item == O_MUSIC_VOL;
        if (want != m_song) { m_song = want; sound_music(want ? g_set.table + 1 : -1); }
    }
    if (repeat(2, pad.up, 0)) { m_item = (m_item + O_N - 1) % O_N; menu_flip(0); }
    if (repeat(3, pad.down, 0)) { m_item = (m_item + 1) % O_N; menu_flip(1); }
    if (menu_has_value()) {
        if (repeat(0, pad.left, 0)) { menu_change(-1); menu_flip(0); }
        if (repeat(1, pad.right, 0)) { menu_change(1); menu_flip(1); }
    }
    if (vpie_start()) {
        m_flip[0] = m_flip[1] = 0.18f;
        if (menu_has_value()) menu_change(1);
        else if (m_item == O_CONTROLS) { settings_save(); m_ball_y = 1e9f; c_cap = 0; menu_open(PAGE_CONTROLS, 0); return; }
        else if (m_item == O_ABOUT) { about_open(); return; }
        else { service_close(); return; }
    }

    const mlay_t L = m_lay();
    menu_back();
    gfx_color(C_TITLE);
    text_center(0, Q4(L.title_y), "SERVICE", L.title_u, 110);
    int first = m_item - L.rows / 2;
    if (first > O_N - L.rows) first = O_N - L.rows;
    if (first < 0) first = 0;
    for (int i = first; i < first + L.rows; i++) {
        const char *v = NULL;
        switch (i) {
        case O_FREEPLAY:  v = ONOFF[vpie_opt(VPIE_OPT_FREE_PLAY)]; break;
        case O_ATTRACT:   v = ONOFF[vpie_opt(VPIE_OPT_ATTRACT_SOUND)]; break;
        case O_BALLS:     v = g_set.balls_unlimited ? "UNLIMITED" : "3"; break;
        case O_SPEED:     v = SPEED_NAMES[g_set.speed]; break;
        case O_PHYSICS:   v = g_set.physics == PHYSICS_REALISTIC ? "REALISTIC" : "ARCADE"; break;
        case O_NUDGE_POWER:  v = NUDGE_NAMES[g_set.nudge_power]; break;
        case O_NUDGE_INVERT: v = ONOFF[g_set.nudge_invert]; break;
        case O_TILT:      v = TILT_NAMES[g_set.tilt_sens]; break;
        case O_PLUNGER:   v = g_set.plunger == PLUNGER_DIGITAL ? "DIGITAL" : "ANALOG"; break;
        case O_ZOOM:      if (g_set.zoom && !vpie_vertical()) snprintf(val, sizeof val, "FULL WIDTH"); else if (g_set.zoom) snprintf(val, sizeof val, "%d.%dX", 1 + g_set.zoom / 5, g_set.zoom * 2 % 10); else snprintf(val, sizeof val, "OFF"); v = val; break;
        case O_3D:        v = V3D_NAMES[g_set.view3d]; break;
        case O_MASTER:    snprintf(val, sizeof val, "%d", vpie_opt(VPIE_OPT_MASTER_VOL)); v = val; break;
        case O_EFFECTS:   snprintf(val, sizeof val, "%d", vpie_opt(VPIE_OPT_EFFECTS_VOL)); v = val; break;
        case O_MUSIC_VOL: snprintf(val, sizeof val, "%d", g_set.music_vol); v = val; break;
        default: break;
        }
        menu_row(i == m_item, Q4(L.y0 - L.step * (i - first)), NAMES[i], v);
    }
    {   /* es geht nach oben bzw. unten weiter */
        const int ax = -L.xl - 12, top = L.y0 + 12, bot = L.y0 - L.step * (L.rows - 1) - 6;
        gfx_color(C_ACCENT);
        if (first > 0) { gfx_line(Q4(ax - 7), Q4(top - 8), Q4(ax), Q4(top), 100); gfx_line(Q4(ax), Q4(top), Q4(ax + 7), Q4(top - 8), 100); }
        if (first + L.rows < O_N) { gfx_line(Q4(ax - 7), Q4(bot + 8), Q4(ax), Q4(bot), 100); gfx_line(Q4(ax), Q4(bot), Q4(ax + 7), Q4(bot + 8), 100); }
    }
    {   /* Zustand der Tonaufnahmen - damit "kein Ton" nicht raten heisst */
        int loading, memory, k = sound_status(&loading, &memory);
        if (k >= 0) {
            if (!memory) snprintf(val, sizeof val, "NO SOUND MEMORY");
            else if (loading) snprintf(val, sizeof val, "LOADING SOUNDS %d/%d", k, sound_files());
            else if (k == 0) snprintf(val, sizeof val, "NO SOUND FILES FOUND");
            else snprintf(val, sizeof val, "%d/%d SOUND FILES", k, sound_files());
            gfx_color(C_DIM);
            text_center(0, Q4(L.note_y), val, L.note_u, 84);
        }
    }
    gfx_color(C_ACCENT);
    text_center(0, Q4(L.hint_y), "STICK: CHOOSE   START: OK   F2: EXIT", L.hint_u, 96);
}

/* ---- About ---- */
int ab_page;
void about_open(void) { s_state = S_ABOUT; ab_page = 0; }

void about_frame(void)
{
    static const char *const TEXT[3][5] = {
        { "VECTOR PINBALL", "BY BRIAN NENNINGER", "(DOZINGCAT) 2010-2026", "GITHUB.COM/DOZINGCAT", "/VECTOR-PINBALL" },
        { "SOUND AND MUSIC: PDX", "TWITTERING MACHINE", "TABLE 8: JACK MORGAN", "TABLE 10 FROM SPINBALL", "GCE 1983" },
        { "VECTORPIE VERSION", "OLIVER CARELL", "(ENDPRODUKT) " PORT_YEAR, "FREE SOFTWARE", "GNU GPL VERSION 3" },
    };
    m_time += s_dt;
    if (vpie_start() || edge.right) { ab_page++; m_flip[1] = 0.18f; }
    if (edge.left && ab_page > 0) { ab_page--; m_flip[0] = 0.18f; }
    if (ab_page > 2) { m_ball_y = 1e9f; menu_open(PAGE_OPTIONS, O_ABOUT); return; }
    {
        const mlay_t L = m_lay();
        const int big = vpie_vertical() ? 17 : 26, mid = vpie_vertical() ? 15 : 22, gap = vpie_vertical() ? 26 : 38;
        menu_back();
        gfx_color(C_TITLE);
        text_center(0, Q4(L.title_y), "ABOUT", L.title_u, 110);
        for (int i = 0; i < 5; i++) {
            gfx_color(i == 0 ? C_WHITE : C_DIM);
            text_center(0, Q4((vpie_vertical() ? 66 : 84) - gap * i), TEXT[ab_page][i], i == 0 ? big : mid, i == 0 ? 127 : 108);
        }
        gfx_color(C_ACCENT);
        text_center(0, Q4(L.hint_y), ab_page < 2 ? "START: NEXT" : "START: BACK", L.hint_u, 96);
    }
}

