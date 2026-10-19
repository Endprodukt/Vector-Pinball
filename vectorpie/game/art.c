/*
 * art.c - die Kulisse: Flipper-Rahmen mit Fingern und Kugel-Zeiger (Service), Lauflicht, Muenzzeile, Ablaufbalken,
 * Tischvorschau neben der Bestenliste.
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

float m_flip[2], m_time;                 /* Fingerschlag und Zeit der Menue-Kulisse */

/* Flipper als schmaler Keil: Drehpunkt, Laenge, Winkel in Grad (0 = nach rechts) */
void wedge(int px, int py, int len, float deg, int b)
{
    /* wie im Spiel als Koerper: helle Deckflaeche, der Fuss zur Bildmitte hin versetzt */
    float a = deg * 0.01745329f, c = fm_cosf(a), s = fm_sinf(a);
    int tx = px + (int)(len * c), ty = py + (int)(len * s), ox = (int)(-s * Q4(4)), oy = (int)(c * Q4(4));
    const int ux[4] = { px + ox, tx + ox / 3, tx - ox / 3, px - ox }, uy[4] = { py + oy, ty + oy / 3, ty - oy / 3, py - oy };
    int bx[4], by[4];
    for (int i = 0; i < 4; i++) { bx[i] = ux[i] - ux[i] * 5 / 100; by[i] = uy[i] - uy[i] * 5 / 100; }
    gfx_color(C_FLIPPER);
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
void roll_ball(int cx, int cy, int r, float roll)
{
    gfx_color(C_BALL);
    gfx_circle(cx, cy, r, 127, 20, 0);
    gfx_circle(cx, cy, r, 127, 20, 0);
    gfx_color(C_RING);
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
void menu_frame_art(int bumpers)
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
    gfx_color(C_FRAME);
    gfx_poly(p, n, 90, 0);
    for (int k = 0; k < 2; k++) {
        if (m_flip[k] > 0) m_flip[k] -= s_dt;
        float up = m_flip[k] > 0 ? 24.0f : -28.0f;
        wedge(k ? Q4(46) : Q4(-46), Q4(-130), Q4(36), k ? 180.0f - up : up, 127);    /* Spitzen bis -147: bleibt im Bild */
    }
    if (bumpers) {
        static const int BX[3] = { -46, 0, 46 }, BY[3] = { 147, 155, 147 };
        int lit = (int)(m_time * 2.5f) % 3;
        for (int k = 0; k < 3; k++) {
            gfx_color(k == lit ? C_LIT : C_BUMPER);
            gfx_circle(Q4(BX[k]), Q4(BY[k]), Q4(7), k == lit ? 127 : 100, 16, 0);
            if (k == lit) gfx_circle(Q4(BX[k]), Q4(BY[k]), Q4(3), 127, 8, 0);
        }
    }
}

/* Zeile: Name links, Wert rechts; die gewaehlte ist hell. Der Zeiger (Kugel) rollt weich zur gewaehlten Zeile. */
/* Jede Bewegung im Service schlaegt einen Finger und klingt auch so (wie in der Vectrex-Fassung seit 2026-10-10) */
void menu_flip(int side)
{
    m_flip[side] = 0.14f;
    sound_stereo(128, side ? 256 : 0);
    sound_events(VSFX_FLIPPER, 0);
}

/* Lauflicht um das ganze Bild, wie die Lampenkette um die Kopfscheibe eines Flippers */
void chaser(void)
{
    const int W = (int)gfx_half_w() - 7, H = 182, step = 18, phase = (int)(s_now_ms / 110);
    int k = 0;
#define LAMP(x, y) do { const int lit = ((k++ + phase) & 3) == 0; gfx_color(lit ? 0xFFE060u : 0xC03020u); \
        if (lit) gfx_circle(Q4(x), Q4(y), 28, 127, 6, 0); else gfx_dot(Q4(x), Q4(y), 60); } while (0)
    for (int x = -W; x < W; x += step) LAMP(x, H);
    for (int y = H; y > -H; y -= step) LAMP(W, y);
    for (int x = W; x > -W; x -= step) LAMP(x, -H);
    for (int y = -H; y < H; y += step) LAMP(-W, y);
#undef LAMP
}

/* Die Muenzzeile als Segmentanzeige: INSERT COIN, oder PRESS START, sobald ein Spiel bezahlt ist; darunter die Credits */
void coin_line(int y, int h)
{
    char buf[24];
    const int free_play = vpie_opt(VPIE_OPT_FREE_PLAY), ready = free_play || vpie_credits() > 0;
    gfx_color(C_PLASMA);
    if ((s_now_ms / 400) & 1) seg_center(0, Q4(y), ready ? "PRESS START" : "INSERT COIN", Q4(h), 127, 0);
    if (free_play) snprintf(buf, sizeof buf, "FREE PLAY"); else snprintf(buf, sizeof buf, "CREDITS %2d", vpie_credits());
    gfx_color(C_VFD);
    seg_center(0, Q4(y) - Q4(h) * 21 / 20, buf, Q4(h) * 72 / 100, 112, free_play ? 0 : 16);
}

/* Ablaufbalken: so viel Zeit bleibt, bis die Auswahl von selbst gilt (kein Bild soll ewig stehen und einbrennen) */
void time_bar(int y, float left)
{
    const int len = (int)(Q4(110) * (left < 0 ? 0 : left));
    gfx_color(C_DIM);
    if (len > 8) gfx_line(-len, Q4(y), len, Q4(y), 70);
}

/* Querformat, Bestenliste im Attract: die Liste rueckt nach links, rechts daneben steht der Tisch, der eben gespielt hat
 * (gedimmt, ganz zu sehen). Hochkant bleibt alles mittig. */
#define MENU_SHIFT (-122)
void menu_side(void)
{
    if (vpie_vertical()) return;
    render_view(F.def, 124.0f, 10.0f, 315.0f / F.def->height);
    render_table(&F, 35);
    render_default_view(F.def);
    gfx_shift(Q4(MENU_SHIFT));
}

