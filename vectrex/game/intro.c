/*
 * intro.c - Vorspann vor dem Menue: eine Flipperkugel rollt auf einen Flipperfinger zu, wird geschlagen, fliegt gegen eine
 * Wand, die in Stuecke springt, dann steht da "PRESENTED BY ENDPRODUKT".
 *
 * Drei Einstellungen, alle mit der Kamera dicht hinter der Kugel (nicht von oben wie im Spiel):
 *   1  die Kugel rollt ueber den Tisch auf den Finger zu; der Finger schlaegt, die Kugel schiesst ueber die Kamera hinweg
 *   2  Schnitt: wieder hinter der Kugel, die auf eine Wand aus Platten zufliegt
 *   3  Einschlag: die Platten fliegen auseinander und drehen sich - dahinter steht, genau an der Stelle der Wand,
 *      "PRESENTED BY ENDPRODUKT" und bleibt stehen
 *
 * Eigenes kleines 3D (Lochkamera mit Neigung, Abschneiden an der Nahebene) statt des 3D-Teils des Starter Kits: der ganze
 * Vorspann kostet so rund 3 KB, und Speicher ist auf dem UVMC2 knapp. Gezeichnet wird ueber gfx wie alles andere.
 */
/* nicht zeitkritisch: auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp (das Kit baut sonst mit -O3) */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include "intro.h"
#include "gfx.h"
#include "font.h"
#include "sound.h"
#include "vp.h"
#include "fmath.h"
#include <math.h>

typedef struct { float x, y, z; } v3;

#define FOCAL   230.0f               /* Brennweite in Bildschirmeinheiten */
#define NEAR    0.4f
#define HORIZON 12.0f                /* Bildmitte der Kamera liegt etwas ueber der Schirmmitte */

/* Zeitplan (Sekunden) */
#define T_HIT   2.4f                 /* der Finger schlaegt */
#define T_CUT   (T_HIT + 0.24f)      /* Schnitt zur zweiten Einstellung */
#define FLY_V   38.0f
#define WALL_Z  40.0f
#define T_BANG  (T_CUT + (WALL_Z - 1.0f) / FLY_V)
#define T_END   (T_BANG + 4.2f)

static float t_now, t_prev;
static v3    cam;
static float cam_s, cam_c;           /* Neigung der Kamera nach unten */
static float shake_x, shake_y;

/* Platten der Wand */
#define COLS 4
#define ROWS 3
#define NFRAG (COLS * ROWS)
static struct { v3 p, v; float ax, ay, wx, wy; } frag[NFRAG];
#define PANEL_W 4.5f
#define PANEL_H 3.0f

static uint32_t rnd_s;
static float rnd(void) { rnd_s = rnd_s * 1664525u + 1013904223u; return (float)(rnd_s >> 8) * (1.0f / 16777216.0f); }

static void set_cam(float x, float y, float z, float pitch) { cam.x = x; cam.y = y; cam.z = z; cam_s = fm_sinf(pitch); cam_c = fm_cosf(pitch); }

static v3 to_cam(v3 p)
{
    float dx = p.x - cam.x, dy = p.y - cam.y, dz = p.z - cam.z;
    v3 r = { dx, dy * cam_c + dz * cam_s, dz * cam_c - dy * cam_s };
    return r;
}

static int scr(float v) { v *= 16.0f; return (int)(v > 30000.0f ? 30000.0f : (v < -30000.0f ? -30000.0f : v)); }

static void line3(v3 a, v3 b, int br)
{
    if (br < 8) return;
    a = to_cam(a); b = to_cam(b);
    if (a.z < NEAR && b.z < NEAR) return;
    if (a.z < NEAR) { float k = (NEAR - a.z) / (b.z - a.z); a.x += (b.x - a.x) * k; a.y += (b.y - a.y) * k; a.z = NEAR; }
    if (b.z < NEAR) { float k = (NEAR - b.z) / (a.z - b.z); b.x += (a.x - b.x) * k; b.y += (a.y - b.y) * k; b.z = NEAR; }
    gfx_line(scr(FOCAL * a.x / a.z + shake_x), scr(FOCAL * a.y / a.z + HORIZON + shake_y),
             scr(FOCAL * b.x / b.z + shake_x), scr(FOCAL * b.y / b.z + HORIZON + shake_y), br > 127 ? 127 : br);
}

/* Kugel: Umriss (immer rund) doppelt gezogen, dazu zwei Ringe ueber die Pole der Rollachse - an ihnen sieht man das Rollen */
static void ball3(v3 c, float roll)
{
    v3 q = to_cam(c);
    if (q.z < 1.6f) return;
    int cx = scr(FOCAL * q.x / q.z + shake_x), cy = scr(FOCAL * q.y / q.z + HORIZON + shake_y), r = scr(FOCAL / q.z);
    if (r > 2600) return;
    gfx_circle(cx, cy, r, 127, 16, 0);
    gfx_circle(cx, cy, r, 127, 16, 0);
    for (int ring = 0; ring < 2; ring++) {
        float ph = roll + ring * 1.5707963f, cp = fm_cosf(ph), sp = fm_sinf(ph);
        v3 prev = { c.x + 1.0f, c.y, c.z };
        for (int k = 1; k <= 8; k++) {
            float a = k * 0.78539816f, ca = fm_cosf(a), sa = fm_sinf(a);
            v3 p = { c.x + ca, c.y + sa * cp, c.z + sa * sp };
            line3(prev, p, 96);
            prev = p;
        }
    }
    gfx_occ_circle(cx, cy, r, 16, 0);                                       /* die Kugel ist das Naechste: sie verdeckt alles danach */
}

/* Punkt auf den Schirm. 0 = hinter der Nahebene */
static int proj(v3 p, pt_t *o)
{
    p = to_cam(p);
    if (p.z < NEAR) return 0;
    o->x = (int16_t)scr(FOCAL * p.x / p.z + shake_x); o->y = (int16_t)scr(FOCAL * p.y / p.z + HORIZON + shake_y);
    return 1;
}

static void occ3(const v3 *c, int n)
{
    pt_t q[8];
    for (int i = 0; i < n; i++) if (!proj(c[i], &q[i])) return;
    gfx_occ_add(q, n);
}

/* Tischboden: zwei Schienen und Querstriche im festen Abstand, die auf die Kamera zulaufen */
static void floor3(float from, float to)
{
    v3 a = { -5.0f, 0, from }, b = { -5.0f, 0, to };
    line3(a, b, 46);
    a.x = b.x = 5.0f;
    line3(b, a, 46);
    for (float z = ceilf(from / 5.0f) * 5.0f; z < to; z += 5.0f) {
        v3 l = { -5.0f, 0, z }, r = { 5.0f, 0, z };
        line3(l, r, 40);
    }
}

/* Flipperfinger als Koerper: Keil mit runder Spitze, oben und unten ein Umriss, dazwischen Kanten */
#define FL_PX (-3.4f)
#define N_OUT 8
#define FL_PZ 31.0f
static void flipper3(float ang)
{
    /* dickes rundes Ende am Drehpunkt, schmale runde Spitze */
    static const float OUT[N_OUT][2] = { { -1.25f, 0 }, { -0.65f, -1.1f }, { 0.5f, -1.25f }, { 6.2f, -0.5f }, { 6.8f, 0 },
                                         { 6.2f, 0.5f }, { 0.5f, 1.25f }, { -0.65f, 1.1f } };
    float c = fm_cosf(ang), s = fm_sinf(ang);
    v3 top[N_OUT], bot[N_OUT];
    for (int i = 0; i < N_OUT; i++) {
        float x = FL_PX + OUT[i][0] * c - OUT[i][1] * s, z = FL_PZ + OUT[i][0] * s + OUT[i][1] * c;
        top[i] = (v3){ x, 1.35f, z }; bot[i] = (v3){ x, 0.1f, z };
    }
    /* Der Finger ist ein gewoelbter Koerper: zu sehen sind die Deckflaeche und die Seitenwaende, die zur Kamera zeigen.
     * Alles andere liegt hinter ihm und wird gar nicht erst gezeichnet. Danach verdeckt er, was hinter ihm kommt. */
    int vis[N_OUT];
    for (int i = 0; i < N_OUT; i++) {
        const v3 *p = &top[i], *q = &top[(i + 1) % N_OUT];
        float nx = q->z - p->z, nz = -(q->x - p->x);                 /* Normale der Seitenwand in der Tischebene ... */
        float mx = (p->x + q->x) * 0.5f, mz = (p->z + q->z) * 0.5f;
        if (nx * (mx - FL_PX - 2.8f * c) + nz * (mz - FL_PZ - 2.8f * s) < 0) { nx = -nx; nz = -nz; }   /* ... nach aussen */
        vis[i] = nx * (cam.x - mx) + nz * (cam.z - mz) > 0;
    }
    for (int i = 0; i < N_OUT; i++) line3(top[i], top[(i + 1) % N_OUT], 120);
    for (int i = 0; i < N_OUT; i++) {
        int j = (i + 1) % N_OUT, h = (i + N_OUT - 1) % N_OUT;
        if (vis[i] || vis[h]) line3(top[i], bot[i], 84);             /* Kante zwischen zwei Waenden, von denen eine zu sehen ist */
        if (vis[i]) line3(bot[i], bot[j], 84);
    }
    occ3(top, N_OUT);
    for (int i = 0; i < N_OUT; i++)
        if (vis[i]) { v3 q[4] = { top[i], top[(i + 1) % N_OUT], bot[(i + 1) % N_OUT], bot[i] }; occ3(q, 4); }
}

static void panel3(int i, int br, v3 *c)
{
    float cx = fm_cosf(frag[i].ax), sx = fm_sinf(frag[i].ax), cy = fm_cosf(frag[i].ay), sy = fm_sinf(frag[i].ay);
    for (int k = 0; k < 4; k++) {
        float lx = (k == 0 || k == 3) ? -PANEL_W * 0.5f : PANEL_W * 0.5f, ly = k < 2 ? -PANEL_H * 0.5f : PANEL_H * 0.5f;
        /* erst um die waagerechte, dann um die senkrechte Achse der Platte */
        float y1 = ly * cx, z1 = ly * sx;
        c[k] = (v3){ frag[i].p.x + lx * cy + z1 * sy, frag[i].p.y + y1, frag[i].p.z - lx * sy + z1 * cy };
    }
    if (br > 8) for (int k = 0; k < 4; k++) line3(c[k], c[(k + 1) & 3], br);
}

void intro_start(void)
{
    t_now = t_prev = 0; shake_x = shake_y = 0;
    rnd_s = 20261002u;
    for (int i = 0; i < NFRAG; i++) {
        int col = i % COLS, row = i / COLS;
        frag[i].p = (v3){ (col - (COLS - 1) * 0.5f) * PANEL_W, (row + 0.5f) * PANEL_H, WALL_Z };
        /* weg von der Einschlagstelle und auf die Kamera zu (so liegen die Platten VOR der Schrift und geben sie frei), mit Drall */
        float dx = frag[i].p.x, dy = frag[i].p.y - 2.4f, l = sqrtf(dx * dx + dy * dy) + 0.5f, k = 3.5f + 5.0f * rnd();
        frag[i].v = (v3){ dx / l * k, dy / l * k + 3.0f, -(1.2f + 4.0f * rnd()) };
        frag[i].ax = frag[i].ay = 0;
        frag[i].wx = (rnd() - 0.5f) * 9.0f; frag[i].wy = (rnd() - 0.5f) * 9.0f;
    }
    sound_stereo(128, 128);
    sound_intro(INTRO_ROLL);
}

static int crossed(float t) { return t_prev < t && t_now >= t; }

int intro_frame(float dt, int skip)
{
    if (dt > 0.05f) dt = 0.05f;                                      /* ein Haenger soll keine Szene ueberspringen */
    t_prev = t_now; t_now += dt;
    if (skip || t_now >= T_END) { gfx_clip_off(); sound_intro(INTRO_END); return 1; }
    if (crossed(T_HIT)) sound_intro(INTRO_FLIP);
    if (crossed(T_BANG)) sound_intro(INTRO_CRASH);

    gfx_clip(Q4(-136), Q4(-176), Q4(136), Q4(176));
    if (t_now < T_CUT) {
        /* 1: auf den Finger zu */
        const float t = t_now < T_HIT ? t_now : T_HIT, zb = 3.0f + 10.6f * t;       /* bei T_HIT kurz vor dem Finger */
        set_cam(0, 4.6f, zb - 7.5f, 0.36f);
        /* Reihenfolge = Tiefe: erst die Kugel (vorn), dann der Finger, zuletzt der Boden - jedes verdeckt, was danach kommt */
        if (t_now < T_HIT) {
            v3 b = { 0, 1.0f, zb };
            ball3(b, zb);                                             /* Rollwinkel = Weg / Radius */
        } else {
            float u = t_now - T_HIT;
            v3 b = { 0.5f * u, 1.0f + 9.0f * u, zb - 46.0f * u };
            ball3(b, zb - 46.0f * u);
            if (u < 0.1f) {                                           /* Funken am Treffpunkt */
                for (int k = 0; k < 4; k++) {
                    float a = 0.4f + k * 1.5707963f, r0 = 1.3f, r1 = 1.3f + 16.0f * u;
                    v3 p0 = { fm_cosf(a) * r0, 1.0f + fm_sinf(a) * r0, zb + 1.0f }, p1 = { fm_cosf(a) * r1, 1.0f + fm_sinf(a) * r1, zb + 1.0f };
                    line3(p0, p1, 127);
                }
            }
        }
        {   /* Finger: wartet schraeg nach hinten, schlaegt in 70 ms nach vorn */
            float k = (t_now - T_HIT) / 0.07f;
            k = k < 0 ? 0 : (k > 1 ? 1 : k);
            flipper3(0.42f - 0.9f * k);
        }
        floor3(cam.z + 0.5f, FL_PZ + 16.0f);
    } else {
        /* 2 und 3: auf die Wand zu, Einschlag */
        const float u = t_now - T_CUT, zb = 1.0f + FLY_V * u, after = t_now - T_BANG;
        float cz = zb - 6.0f;
        if (after > 0) {                                              /* Kamera haelt vor der Wand */
            cz = WALL_Z - 7.0f;
            float amp = 6.0f * expf(-after * 6.0f);
            shake_x = (rnd() - 0.5f) * amp; shake_y = (rnd() - 0.5f) * amp;
        }
        set_cam(0, 3.2f, cz, 0.1f);
        if (after <= 0) {
            v3 b = { 0, 2.4f, zb };
            ball3(b, zb);                                             /* vorn: verdeckt Wand und Boden hinter sich */
        }
        if (after <= 0) {
            /* Wand heil: Gitter aus Platten */
            for (int c = 0; c <= COLS; c++) {
                float x = (c - COLS * 0.5f) * PANEL_W;
                v3 a = { x, 0, WALL_Z }, b = { x, ROWS * PANEL_H, WALL_Z };
                if (c & 1) line3(b, a, 96); else line3(a, b, 96);
            }
            for (int r = 0; r <= ROWS; r++) {
                v3 a = { -COLS * 0.5f * PANEL_W, r * PANEL_H, WALL_Z }, b = { COLS * 0.5f * PANEL_W, r * PANEL_H, WALL_Z };
                if (r & 1) line3(b, a, 96); else line3(a, b, 96);
            }
            floor3(cam.z + 0.5f, WALL_Z + 14.0f);
        } else {
            /* Die Platten fliegen auf die Kamera zu und liegen damit VOR der Schrift: erst alle zeichnen, dann alle als
             * Verdecker anmelden - die Schrift dahinter ist nur dort zu sehen, wo keine Platte mehr davor ist. */
            v3 c[NFRAG][4];
            int br = (int)(112.0f * (1.0f - after / 1.9f));
            for (int i = 0; i < NFRAG; i++) {
                frag[i].v.y -= 13.0f * dt;
                frag[i].p.x += frag[i].v.x * dt; frag[i].p.y += frag[i].v.y * dt; frag[i].p.z += frag[i].v.z * dt;
                frag[i].ax += frag[i].wx * dt; frag[i].ay += frag[i].wy * dt;
                panel3(i, br, c[i]);
            }
            if (br > 8) for (int i = 0; i < NFRAG; i++) occ3(c[i], 4);
        }
        if (after > 0) {
            /* Die Schrift steht in der Ebene der Wand, um die Einschlagstelle: sie ist im Augenblick des Einschlags voll da,
             * die Platten fliegen von ihr weg. Sie steht vollkommen still - kein Wackeln, kein Wachsen (Schriftgroessen
             * gibt es nur in ganzen Schritten, jede Bewegung der Kamera liesse die Zeilen springen). */
            v3 q = to_cam((v3){ 0, 2.4f, WALL_Z });
            int cy = scr(FOCAL * q.y / q.z + HORIZON);
            gfx_clip_off();
            text_center(0, cy + Q4(30), "PRESENTED BY", 15, 100);
            text_center(0, cy - Q4(24), "ENDPRODUKT", 35, 127);
            text_center(10, cy - Q4(24) + 8, "ENDPRODUKT", 35, 112);                /* doppelt gezogen: fett */
        }
    }
    gfx_clip_off();
    return 0;
}
