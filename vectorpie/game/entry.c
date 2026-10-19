/*
 * entry.c - Initialen nach einem Platz in der Bestenliste, und die Liste fuer das Menue des Automaten.
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

/* ---- Bestenliste: Initialen eingeben (bei mehreren Spielern der Reihe nach fuer jeden, der einen Platz erreicht hat) ---- */
/* Das Zeichenrad: Buchstaben, Ziffern, Leerzeichen und der Pfeil zurueck (E_BACK; loescht die Stelle davor). */
#define E_BACK '<'
static const char LETTERS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 <";
static char e_name[4];
static int e_pos, e_place, e_player;
static uint32_t e_score;

/* Fuer das Menue des Automaten (Hinweiszeile): die zehn besten Ergebnisse ueber alle Tische nach save/highscores.txt.
 * Die eigene Liste je Tisch (scores.bin) bleibt die, die das Spiel zeigt. */
static void export_scores(void)
{
    char names[10][4];
    uint32_t scores[10];
    int n = 0;
    for (int t = 0; t < vp_num_tables && t < NUM_TABLES_MAX; t++)
        for (int p = 0; p < HS_PLACES; p++) {
            const hs_entry_t *e = hs_get(t, p);
            int at = n;
            if (!e->score) continue;
            while (at > 0 && scores[at - 1] < e->score) at--;
            if (at >= 10) continue;
            if (n < 10) n++;
            for (int k = n - 1; k > at; k--) { scores[k] = scores[k - 1]; memcpy(names[k], names[k - 1], 4); }
            scores[at] = e->score; memcpy(names[at], e->name, 4);
        }
    vpie_export_scores((const char (*)[4])names, scores, n);
}

static int letter_index(char c) { const char *p = strchr(LETTERS, c); return p ? (int)(p - LETTERS) : 0; }
uint32_t final_score(int p) { int64_t s = vp_player_score(&F, p); return s > 0xFFFFFFFFLL ? 0xFFFFFFFFu : (uint32_t)(s < 0 ? 0 : s); }

/* naechster Spieler ab from, dessen Ergebnis in die Liste gehoert; keiner mehr: zurueck ins Menue */
void entry_next(int from)
{
    if (!g_set.balls_unlimited)                              /* freies Spiel zaehlt nicht fuer die Liste */
        for (int p = from; p < F.nplayers; p++) {
            int place = hs_rank(g_set.table, final_score(p));
            if (place < 0) continue;
            memcpy(e_name, "A  ", 4);                            /* immer leer anfangen, nichts gemerkt (Olli, 2026-10-10) */
            e_pos = 0; e_place = place; e_score = final_score(p); e_player = p;
            s_state = S_ENTRY;
            return;
        }
    load_table(g_set.table);
    attract_open();
}

static float e_t;                        /* Sekunden ohne Eingabe: nach 40 gelten die Initialen, wie sie stehen */
void entry_frame(void)
{
    char buf[64];
    const int n = (int)sizeof LETTERS - 1;
    int i = letter_index(e_name[e_pos]);
    int done = 0;
    e_t = (pad.up || pad.down || pad.left || pad.right || pad.b1 || pad.b3 || pad.b4) ? 0 : e_t + s_dt;
    /* Zeichen waehlen: Flipper (links = vorheriges, rechts = naechstes) oder Stick; START nimmt es und geht zur naechsten
     * Stelle. Vertippt: den Pfeil zurueck waehlen und START - die Stelle davor wird wieder frei. Der Stick bewegt NICHT
     * die Stelle (Olli, 2026-10-10: Korrektur nur ueber den Pfeil). */
    if (repeat(1, pad.b4 || pad.right || pad.up, 0)) e_name[e_pos] = LETTERS[(i + 1) % n];
    if (repeat(0, pad.b1 || pad.left || pad.down, 0)) e_name[e_pos] = LETTERS[(i + n - 1) % n];
    if (vpie_start() || edge.b3) {
        if (e_name[e_pos] == E_BACK) {
            e_name[e_pos] = ' ';
            if (e_pos > 0) e_pos--;                                  /* das Zeichen davor bleibt stehen, zum Aendern */
            else e_name[0] = 'A';
            blip(VSFX_FLIPPER);
        } else if (e_pos < 2) {
            e_pos++;
            if (e_name[e_pos] == ' ') e_name[e_pos] = 'A';
            blip(VSFX_ROLLOVER);
        } else done = 1;
    }
    if (done || e_t > 40.0f) {
        e_t = 0;
        for (int k = 0; k < 3; k++) if (e_name[k] == E_BACK) e_name[k] = ' ';
        hs_insert(g_set.table, e_place, e_score, e_name); export_scores(); entry_next(e_player + 1);
        return;
    }
    gfx_color(C_TITLE);
    text_center(0, Q4(120), "NEW HIGH SCORE", 30, 120);
    if (F.nplayers > 1) snprintf(buf, sizeof buf, "PLAYER %d   PLACE %d", e_player + 1, e_place + 1);
    else snprintf(buf, sizeof buf, "PLACE %d", e_place + 1);
    gfx_color(C_DIM);
    text_center(0, Q4(84), buf, 23, 108);
    snprintf(buf, sizeof buf, "%lu", (unsigned long)e_score);
    gfx_color(C_WHITE);
    text_center(0, Q4(54), buf, 27, 120);
    for (int k = 0; k < 3; k++) {
        char one[2] = { e_name[k], 0 };
        int x = Q4(-66 + 48 * k), blink = ((s_now_ms / 250) & 1);
        gfx_color(k == e_pos ? C_VALUE : C_DIM);
        if (e_name[k] == E_BACK) {                                   /* der Pfeil zurueck: Schaft und Spitze nach links */
            const int cy = Q4(-10) + 80 * 3, x0 = x + 80 / 2, x1 = x + 4 * 80 - 80 / 2;
            gfx_line(x1, cy, x0, cy, 127);
            gfx_line(x0, cy, x0 + 80, cy + 80, 127);
            gfx_line(x0, cy, x0 + 80, cy - 80, 127);
        } else if (k <= e_pos) text_draw(x, Q4(-10), one, 80, k == e_pos ? 127 : 96);
        if (k == e_pos && blink) gfx_line(x, Q4(-20), x + 4 * 80, Q4(-20), 127);       /* Strich unter dem aktiven Zeichen */
        else gfx_line(x, Q4(-20), x + 4 * 80, Q4(-20), 50);
    }
    gfx_color(C_VALUE);                                              /* was das gewaehlte Zeichen ist, wenn man es nicht sieht */
    if (e_name[e_pos] == E_BACK) text_center(0, Q4(-54), "BACK", 16, 110);
    else if (e_name[e_pos] == ' ') text_center(0, Q4(-54), "SPACE", 16, 110);
    gfx_color(C_ACCENT);
    text_center(0, Q4(-82), "FLIPPERS OR STICK: LETTER", 18, 96);
    text_center(0, Q4(-106), "START: NEXT   ARROW: BACK", 18, 96);
}

