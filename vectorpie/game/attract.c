/*
 * attract.c - Attract (Titel, Demo, Bestenliste), Spielerzahl und Tischwahl.
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

/* ---- Titelbild ----
 * Ein Flipper im Kleinen: oben drei Bumper, die reihum aufblitzen, unten zwei Finger, die abwechselnd schlagen,
 * dazwischen der Schriftzug. (Eine Kugel, die hier herumsprang, ist wieder draussen: sie stoerte - Olli, 2026-10-10.) */
static struct { float t, flash[3], flip[2]; int bump, side; } tb;

static void title_frame(void)
{
    const float W = gfx_half_w(), k = W / 252.0f;
    const float bx[3] = { -0.62f * W, 0.0f, 0.62f * W }, by = 142.0f, br = 15.0f, fy = -150.0f, px = 92.0f;
    const float dt = s_dt > 0.03f ? 0.03f : s_dt;
    tb.t += dt;
    for (int i = 0; i < 3; i++) if (tb.flash[i] > 0) tb.flash[i] -= dt * 2.4f;
    for (int i = 0; i < 2; i++) if (tb.flip[i] > 0) tb.flip[i] -= dt;
    if (tb.t > 0.55f) {                                              /* im Takt: ein Bumper blitzt, jeder zweite Takt ein Fingerschlag */
        tb.t = 0;
        tb.bump = (tb.bump + 1 + (int)(frnd() * 2.0f)) % 3;           /* nie zweimal derselbe */
        tb.flash[tb.bump] = 1.0f;
        if (vpie_opt(VPIE_OPT_ATTRACT_SOUND)) blip(VSFX_KICK);
        if ((tb.side++ & 1) == 0) {
            tb.flip[(tb.side >> 1) & 1] = 0.16f;
            if (vpie_opt(VPIE_OPT_ATTRACT_SOUND)) blip(VSFX_FLIPPER);
        }
    }

    chaser();
    for (int i = 0; i < 3; i++) {
        const int lit = tb.flash[i] > 0;
        gfx_color(lit ? C_LIT : C_BUMPER);
        gfx_circle(Q4(bx[i]), Q4(by), Q4(br), lit ? 127 : 104, 24, 0);
        gfx_circle(Q4(bx[i]), Q4(by), Q4(br) * 6 / 10, lit ? 127 : 70, 16, 0);
        if (lit) {
            gfx_color(C_WHITE);
            for (int r = 0; r < 8; r++) {
                const float a = r * 0.785398f + 0.39f, c = fm_cosf(a), sn = fm_sinf(a), r0 = br * 1.35f, r1 = br * (1.35f + 0.9f * tb.flash[i]);
                gfx_line(Q4(bx[i] + c * r0), Q4(by + sn * r0), Q4(bx[i] + c * r1), Q4(by + sn * r1), 127);
            }
        }
    }
    gfx_color(C_FRAME);                                              /* Einlaeufe zu den Fingern */
    gfx_line(Q4(-W + 12.0f), Q4(-98), Q4(-px), Q4(fy + 4.0f), 96);
    gfx_line(Q4(W - 12.0f), Q4(-98), Q4(px), Q4(fy + 4.0f), 96);
    {   /* Kopfbogen ueber den Bumpern und die beiden Schleudern ueber den Einlaeufen: der Umriss eines Tisches */
        int ax = Q4(-W + 12.0f), ay = Q4(96);
        for (int i = 1; i <= 16; i++) {
            const float a = 3.14159265f * (1.0f - i / 16.0f);
            const int qx = (int)(Q4(W - 12.0f) * fm_cosf(a)), qy = Q4(96) + (int)(Q4(80) * fm_sinf(a));
            gfx_line(ax, ay, qx, qy, 96);
            ax = qx; ay = qy;
        }
        for (int sd = -1; sd <= 1; sd += 2) {
            const int x1 = Q4(sd * (W - 34.0f)), x2 = Q4(sd * (px + 46.0f));
            gfx_color(C_FRAME);
            gfx_line(x1, Q4(-40), x1, Q4(-92), 96);
            gfx_line(x1, Q4(-92), x2, Q4(-124), 96);
            gfx_color(0x3050FFu);
            gfx_line(x2, Q4(-124), x1, Q4(-40), 118);
        }
    }
    for (int i = 0; i < 2; i++) {
        const float up = tb.flip[i] > 0 ? 24.0f : -28.0f;
        wedge(i ? Q4(px) : Q4(-px), Q4(fy), Q4(78), i ? 180.0f - up : up, 127);
    }
    {
        static const char V[] = "VECTOR";
        const int u = (int)(W * 16.0f * 2.0f * 0.72f / 40.0f), w = text_width("PINBALL", u), x0 = -w / 2 - u * 6 * 60 / 512;
        gfx_color(C_ACCENT);
        for (int i = 0; i < 6; i++) { char c[2] = { V[i], 0 }; text_draw(-w / 2 + i * (w - 4 * 24) / 5, Q4(86), c, 24, 118); }
        /* "PINBALL" mit Tiefe: drei Schichten nach hinten, von dunkelrot nach orange, vorn gelb */
        gfx_color(0xC01810u); text_draw_slant(x0 + 132, Q4(14) - 108, "PINBALL", u, 60, 60);
        gfx_color(0xFF3018u); text_draw_slant(x0 + 88, Q4(14) - 72, "PINBALL", u, 82, 60);
        gfx_color(0xFF8020u); text_draw_slant(x0 + 44, Q4(14) - 36, "PINBALL", u, 104, 60);
        gfx_color(C_TITLE);   text_draw_slant(x0, Q4(14), "PINBALL", u, 127, 60);
    }
    coin_line(-50, (int)(27.0f * k));
    gfx_color(C_DIM);
    text_center(0, Q4(-118), "DOZINGCAT / ENDPRODUKT " PORT_YEAR, (int)(15.0f * k), 84);
}

/* ---- Attract: Titel - ein Tisch spielt sich selbst - seine Bestenliste - der naechste Tisch ---- */
int at_page, at_table;
static float at_t;

void attract_page(int page)
{
    at_page = page; at_t = 0;
    if (page == AT_DEMO) {
        load_table(at_table);
        F.nplayers = 1;
        vp_start_game(&F, 0);
        F.now_ms = s_now_ms;
        vp_message(&F, F.def->name, 2500, 0);
        F.sfx = 0;
        fx_reset();
    }
    sound_music(page == AT_TITLE && vpie_opt(VPIE_OPT_ATTRACT_SOUND) ? 0 : -1);
}

void attract_open(void)
{
    s_state = S_ATTRACT;
    sound_silence();
    sound_game_reset();
    attract_page(AT_TITLE);
}

static void scores_frame(void)
{
    char buf[40];
    menu_side();                                                     /* Querformat: rechts der Tisch, der eben gespielt hat */
    gfx_color(C_TITLE);
    text_center(0, Q4(136), "HIGH SCORES", 24, 120);
    gfx_color(C_ACCENT);
    text_center(0, Q4(104), vp_tables[at_table].name, 22, 116);
    for (int p = 0; p < HS_PLACES; p++) {
        const hs_entry_t *e = hs_get(at_table, p);
        snprintf(buf, sizeof buf, "%d %s %7lu", p + 1, e->score ? e->name : "---", (unsigned long)e->score);
        gfx_color(p == 0 ? C_PLASMA : C_VFD);
        {
            int h = Q4(21), w = seg_width(buf, h);
            if (w > Q4(236)) h = h * Q4(236) / w;                    /* neunstellige Ergebnisse: etwas kleiner statt ueber den Rand */
            seg_center(0, Q4(60 - 36 * p), buf, h, p == 0 ? 127 : 108, 14);
        }
    }
    coin_line(-98, 21);
    gfx_shift(0);
}

void attract_frame(void)
{
    at_t += s_dt;
    if (vpie_start() && vpie_can_start(1)) { players_open(); return; }
    switch (at_page) {
    case AT_TITLE:
        title_frame();
        if (at_t > 12.0f) attract_page(AT_DEMO);
        break;
    case AT_DEMO: {
        int l = 0, r = 0;
        autoplay(&l, &r);
        vp_catch_returned_ball(&F);
        render_plunger(0);
        vp_set_flippers(&F, l, r);
        vp_advance(&F, s_dt, s_now_ms, game_speed());
        fx_update(&F, s_dt);
        if (vpie_opt(VPIE_OPT_ATTRACT_SOUND)) sound_events(F.sfx, F.score_hits);
        F.sfx = 0;
        draw_game(0, 1, s_dt);
        render_hud(&F);
        coin_line(-120, 22);
        if (at_t > 28.0f || !F.in_progress) attract_page(AT_SCORES);
        break;
    }
    default:
        scores_frame();
        if (at_t > 8.0f) { at_table = (at_table + 1) % vp_num_tables; attract_page(AT_TITLE); }
        break;
    }
}

/* ---- START gedrueckt: wie viele spielen? ---- */
static float sel_t;

static int players_max(void)
{
    const int c = vpie_credits();
    if (vpie_opt(VPIE_OPT_FREE_PLAY)) return VP_MAX_PLAYERS;
    return c > VP_MAX_PLAYERS ? VP_MAX_PLAYERS : (c < 1 ? 1 : c);
}

void players_open(void)
{
    s_state = S_PLAYERS; sel_t = 0;
    sound_music(-1);
    if (s_players < 1) s_players = 1;
    blip(VSFX_START);
}

void players_frame(void)
{
    char buf[24];
    const float W = gfx_half_w();
    const int max = players_max();
    sel_t += s_dt;
    if (s_players > max) s_players = max;
    if ((edge.left || edge.b1) && s_players > 1) { s_players--; sel_t = 0; blip(VSFX_FLIPPER); }
    if ((edge.right || edge.b4) && s_players < max) { s_players++; sel_t = 0; blip(VSFX_FLIPPER); }
    if (vpie_start() || sel_t > 20.0f) { table_open(); return; }

    chaser();
    gfx_color(C_SHADOW); text_center(12, Q4(118) + 10, "PLAYERS", 34, 100);
    gfx_color(C_TITLE);  text_center(0, Q4(118), "PLAYERS", 34, 127);
    for (int i = 1; i <= VP_MAX_PLAYERS; i++) {
        const int sel = i == s_players, h = sel ? 78 : 46;
        const char d[2] = { (char)('0' + i), 0 };
        gfx_color(sel ? C_PLASMA : (i <= max ? C_VFD : 0xA02020u));
        seg_center(Q4((i - 2.5f) * W * 0.36f), Q4(14 - h / 2), d, Q4(h), sel ? 127 : (i <= max ? 92 : 60), 16);
    }
    snprintf(buf, sizeof buf, s_players == 1 ? "1 PLAYER" : "%d PLAYERS", s_players);
    gfx_color(C_VFD);
    seg_center(0, Q4(-76), buf, Q4(23), 120, 0);
    if (vpie_opt(VPIE_OPT_FREE_PLAY)) snprintf(buf, sizeof buf, "FREE PLAY"); else snprintf(buf, sizeof buf, "CREDITS %2d", vpie_credits());
    gfx_color(C_PLASMA);
    seg_center(0, Q4(-112), buf, Q4(18), 108, 0);
    gfx_color(C_ACCENT);
    text_center(0, Q4(-150), "FLIPPERS OR STICK: CHOOSE   START: OK", (int)(17.0f * W / 252.0f), 100);
    time_bar(-168, 1.0f - sel_t / 20.0f);
}

/* ---- ... und auf welchem Tisch? Vorschau des ganzen Tisches, daneben Name und bestes Ergebnis ---- */
void table_open(void)
{
    s_state = S_TABLE; sel_t = 0;
    g_set.table = 0;                                                 /* immer mit Tisch 1 beginnen (Olli, 2026-10-10) */
    load_table(g_set.table);
    blip(VSFX_MESSAGE);
}

void table_frame(void)
{
    char buf[40];
    const int vertical = vpie_vertical();
    int d = 0;
    sel_t += s_dt;
    if (repeat(0, pad.left || pad.b1, 0)) d = -1;
    if (repeat(1, pad.right || pad.b4, 0)) d = 1;
    if (d) {
        g_set.table = (uint8_t)((g_set.table + vp_num_tables + d) % vp_num_tables);
        load_table(g_set.table);
        sel_t = 0;
        blip(VSFX_FLIPPER);
    }
    if (vpie_start() || sel_t > 30.0f) {
        if (!vpie_take_credits(s_players)) { attract_open(); return; }
        g_set.players = (uint8_t)s_players;
        settings_save();
        start_game();
        return;
    }

    if (vertical) render_view(F.def, 0.0f, -22.0f, 296.0f / F.def->height);
    else render_view(F.def, 122.0f, -2.0f, 340.0f / F.def->height);
    render_table(&F, 0);
    render_default_view(F.def);

    if (vertical) {
        int h = Q4(17), w = seg_width(F.def->name, h);
        if (w > Q4(270)) h = h * Q4(270) / w;
        gfx_color(C_PLASMA);
        seg_center(0, Q4(166), F.def->name, h, 127, 0);
        gfx_color(C_ACCENT);
        text_center(0, Q4(-187), "FLIPPERS: TABLE   START: PLAY", 13, 100);
        return;
    }
    gfx_shift(Q4(-126));
    gfx_color(C_SHADOW); text_center(8, Q4(144) + 7, "SELECT TABLE", 24, 96);
    gfx_color(C_TITLE);  text_center(0, Q4(144), "SELECT TABLE", 24, 127);
    {
        int h = Q4(25), w = seg_width(F.def->name, h);              /* muss zwischen die Pfeile passen */
        if (w > Q4(188)) h = h * Q4(188) / w;
        gfx_color(C_PLASMA);
        seg_center(0, Q4(84), F.def->name, h, 127, 0);
    }
    snprintf(buf, sizeof buf, "TABLE %d OF %d", g_set.table + 1, vp_num_tables);
    gfx_color(C_DIM);
    text_center(0, Q4(50), buf, 18, 104);
    gfx_color(C_ACCENT);                                             /* Pfeile links und rechts: es geht in beide Richtungen weiter */
    gfx_line(Q4(-104), Q4(96), Q4(-114), Q4(90), 110); gfx_line(Q4(-114), Q4(90), Q4(-104), Q4(84), 110);
    gfx_line(Q4(104), Q4(96), Q4(114), Q4(90), 110); gfx_line(Q4(114), Q4(90), Q4(104), Q4(84), 110);
    {
        const hs_entry_t *e = hs_get(g_set.table, 0);
        gfx_color(C_DIM);
        text_center(0, Q4(12), "BEST", 18, 104);
        if (e->score) snprintf(buf, sizeof buf, "%s %7lu", e->name, (unsigned long)e->score); else snprintf(buf, sizeof buf, "NONE YET");
        gfx_color(C_VFD);
        {
            int h = Q4(21), w = seg_width(buf, h);
            if (w > Q4(236)) h = h * Q4(236) / w;
            seg_center(0, Q4(-22), buf, h, 116, e->score ? 14 : 0);
        }
    }
    snprintf(buf, sizeof buf, s_players == 1 ? "1 PLAYER" : "%d PLAYERS", s_players);
    gfx_color(C_PLASMA);
    seg_center(0, Q4(-74), buf, Q4(19), 110, 0);
    gfx_color(C_ACCENT);
    text_center(0, Q4(-126), "FLIPPERS OR STICK: TABLE", 16, 100);
    text_center(0, Q4(-148), "START: PLAY", 16, 100);
    time_bar(-168, 1.0f - sel_t / 30.0f);
    gfx_shift(0);
}

