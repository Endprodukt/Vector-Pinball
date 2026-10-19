/*
 * fliptest.c - misst, was die Flipper koennen (Tisch 1, linker Flipper; Pfosten der Schleuder bei 5.95 / 4.0):
 *
 *   A  Zielen: der Ball rollt die Fuehrung herunter auf den ruhenden Flipper; geschlagen wird, wenn er einen bestimmten
 *      Bruchteil der Flipperlaenge erreicht hat (0 = Drehpunkt, 1 = Spitze). Gezeigt: Abflugrichtung (Grad von der
 *      Senkrechten, minus = zur eigenen Seite = Rueckhand) und die Hoehe, die der Ball erreicht.
 *   B  Halten (Cradle): Flipper oben, der Ball faellt darauf und soll in der Mulde am Drehpunkt liegen bleiben.
 *   C  Post Pass: aus dem Halten kurz loslassen und wieder druecken; der Ball soll an den Pfosten unter der Schleuder
 *      tippen und zum anderen Flipper hinueberlaufen.
 *   D  Dead Bounce: der Ball kommt die Fuehrung herunter auf den RUHENDEN Flipper und soll vom Gummi zum anderen
 *      Flipper hinueberspringen.
 *   E  Tip Pass: der Ball rollt bis zur Spitze, ein kurzer Tipp wirft ihn zum anderen Flipper hinueber.
 *   F  Drop Catch: Flipper oben, der Ball faellt darauf; im Augenblick der Beruehrung loslassen - der Ball soll tot
 *      auf dem Flipper liegen bleiben (kein Zurueckspringen).
 *   G  Live Catch: Flipper unten, der Ball kommt die Fuehrung herunter; bei der Beruehrung druecken und halten - der
 *      Ball soll in der Mulde zur Ruhe kommen.
 *
 *   tools/fliptest.exe [real 0|1]        (FT_DBG=1: Ballbahn im Live-Catch-Test mitschreiben)
 */
#include "vp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static vp_field_t F;
static uint32_t ms;
#define DT (1.0f / 240.0f)
#define OTHER_X 9.35f                 /* ab hier ist der Ball beim rechten Flipper */

static void step(int l, int r)
{
    vp_set_flippers(&F, l, r);
    vp_advance(&F, DT, ms / 1000, 1.0f);
    ms += 4167;
    F.sfx = 0;
}

static void setup(int real)
{
    vp_load(&F, 0, 7);
    vp_set_real_physics(&F, real);
    vp_start_game(&F, 1);
    ms = 0;
    for (int i = 0; i < 240; i++) step(0, 0);
    while (F.nballs) vp_remove_ball_quiet(&F, &F.balls[0]);
}

/* Ball in die Mulde des gehaltenen Flippers legen; 0 = liegt nicht still */
static int cradle(int real)
{
    setup(real);
    if (!vp_create_ball(&F, 7.6f, 5.0f)) return 0;
    for (int i = 0; i < 240 * 4 && F.nballs; i++) step(1, 0);
    return F.nballs && hypotf(F.balls[0].vx, F.balls[0].vy) < 0.3f;
}

/* ab jetzt: wohin kommt der Ball? l/r = Flipperstellung waehrend des Wartens. Ein Pass ist ein flacher Bogen zum anderen
 * Finger: Mitte (x 9,35) unter y 5 ueberquert, Gipfel unter 8. Hoeher ist ein Schuss, der irgendwann rechts herunterkommt. */
static const char *outcome(int l, int r, float secs, float *top, float *ycross)
{
    int crossed = 0;
    *top = 0; *ycross = 0;
    for (int i = 0; i < (int)(240 * secs) && F.nballs; i++) {
        step(l, r);
        if (!F.nballs) break;
        const vp_ball_t *b = &F.balls[0];
        if (b->y > *top) *top = b->y;
        if (!crossed && b->x > OTHER_X) { crossed = 1; *ycross = b->y; }
    }
    if (crossed && *ycross < 5.0f && *top < 8.0f) return "-> PASS";     /* auch wenn er danach vom unteren rechten Finger rollt */
    if (crossed) return "hinueber (als Schuss)";
    if (!F.nballs) return "verloren";
    return "bleibt links";
}

int main(int argc, char **argv)
{
    int real = argc > 1 ? atoi(argv[1]) : 1;
    float px = 6.35f, py = 2.2f;
    const float len = 2.5f;
    float top;
    setup(real);
    vp_flipper_pivot(&F, F.flip[0], &px, &py);
    printf("physik: %s  (Drehpunkt links bei %.2f / %.2f, im Tisch 6.35 / 2.20)\n\nA  Zielen\n  Bruchteil  Richtung  Tempo  Gipfel\n", real ? "REALISTIC" : "ARCADE", px, py);
    for (int k = 1; k <= 9; k++) {
        float frac = k * 0.1f, dir = 0, sp = 0;
        int fired = 0, left = 0;
        top = 0;
        setup(real);
        vp_ball_t *b = vp_create_ball(&F, 3.4f, 4.4f);
        if (!b) return 1;
        for (int i = 0; i < 240 * 6 && F.nballs; i++) {
            b = &F.balls[0];
            if (!fired && b->x >= px + frac * len) fired = 1;
            step(fired, 0);
            if (!F.nballs) break;
            b = &F.balls[0];
            if (fired && !left && b->y > 3.6f && b->vy > 0) { left = 1; dir = atan2f(b->vx, b->vy) * 57.29578f; sp = hypotf(b->vx, b->vy); }
            if (fired && b->y > top) top = b->y;
        }
        if (left) printf("  %.1f       %+6.1f   %5.1f  %5.1f\n", frac, dir, sp, top);
        else printf("  %.1f       nicht getroffen (Gipfel %.1f)\n", frac, top);
    }

    printf("\nB  Halten\n");
    if (cradle(real)) printf("  liegt bei x %.2f y %.2f  (ruht)\n", F.balls[0].x, F.balls[0].y);
    else printf("  %s\n", F.nballs ? "ruht nicht" : "Ball verloren");

    printf("\nC  Post Pass (loslassen, dann wieder druecken)\n  Loslassen  naechster Abstand zum Pfosten  Gipfel  Ergebnis\n");
    for (int tap = 40; tap <= 400; tap += 20) {
        if (!cradle(real)) continue;
        float near = 99;
        const int n = tap * 240 / 1000;
        for (int i = 0; i < n && F.nballs; i++) step(0, 0);
        int crossed = 0;
        float yc = 0;
        top = 0;
        for (int i = 0; i < 240 * 4 && F.nballs; i++) {
            step(1, 0);
            if (!F.nballs) break;
            const vp_ball_t *b = &F.balls[0];
            float d = hypotf(b->x - 5.95f, b->y - 4.0f) - F.def->ball_radius;
            if (d < near) near = d;
            if (b->y > top) top = b->y;
            if (!crossed && b->x > OTHER_X) { crossed = 1; yc = b->y; }
        }
        printf("  %3d ms     %5.2f                         %5.2f   %s\n", tap, near, top, crossed ? (yc < 5.0f && top < 8.0f ? "-> PASS" : "hinueber (als Schuss)") : !F.nballs ? "verloren" : "bleibt links");
    }

    printf("\nD  Dead Bounce (Flipper unten, Ball faellt von oben auf die Mitte des Fingers)\n  Fallhoehe  Tempo beim Aufprall  Gipfel  Mitte bei y  Ergebnis\n");
    for (int h = 0; h < 5; h++) {
        setup(real);
        const float y0 = 3.5f + h * 1.5f;
        vp_ball_t *b = vp_create_ball(&F, 7.9f, y0);
        if (!b) continue;
        float sp = 0;
        for (int i = 0; i < 240 * 6 && F.nballs; i++) {
            b = &F.balls[0];
            sp = hypotf(b->vx, b->vy);
            if (b->ntouch) break;
            step(0, 0);
        }
        if (!F.nballs) continue;
        float yc;
        const char *res = outcome(0, 0, 4.0f, &top, &yc);
        printf("  y %.1f        %5.1f              %5.1f   %5.1f       %s\n", y0, sp, top, yc, res);
    }

    printf("\nE  Tip Pass (Ball rollt zur Spitze, kurzer Tipp)\n  Tipp bei  Dauer   Gipfel  Mitte bei y  Ergebnis\n");
    for (int k = 0; k < 3; k++) for (int tap = 20; tap <= 100; tap += 20) {
        const float frac = 0.80f + k * 0.08f;
        setup(real);
        vp_ball_t *b = vp_create_ball(&F, 3.4f, 4.4f);
        if (!b) continue;
        int fired = 0;
        for (int i = 0; i < 240 * 6 && F.nballs && !fired; i++) {
            b = &F.balls[0];
            if (b->x >= px + frac * len) fired = 1; else step(0, 0);
        }
        if (!fired || !F.nballs) { printf("  %.2f      %3d ms  nicht erreicht\n", frac, tap); continue; }
        for (int i = 0; i < tap * 240 / 1000 && F.nballs; i++) step(1, 0);
        float yc;
        const char *res = outcome(0, 0, 4.0f, &top, &yc);
        printf("  %.2f      %3d ms  %5.2f   %5.1f       %s\n", frac, tap, top, yc, res);
    }

    printf("\nF  Drop Catch (Flipper oben, bei der Beruehrung loslassen, 0,35 s spaeter wieder halten)\n  Fallhoehe  Tempo beim Aufprall  Tempo 0,35 s danach  Ergebnis\n");
    for (int h = 0; h < 4; h++) {
        setup(real);
        for (int i = 0; i < 120; i++) step(1, 0);
        const float y0 = 5.0f + h * 2.0f;
        vp_ball_t *b = vp_create_ball(&F, 7.6f, y0);
        if (!b) continue;
        float sp = 0;
        for (int i = 0; i < 240 * 6 && F.nballs; i++) {
            b = &F.balls[0];
            sp = hypotf(b->vx, b->vy);
            if (b->ntouch) break;
            step(1, 0);
        }
        if (!F.nballs) continue;
        for (int i = 0; i < 84 && F.nballs; i++) step(0, 0);
        if (!F.nballs) { printf("  %.1f        %5.1f               -       verloren\n", y0, sp); continue; }
        b = &F.balls[0];
        const float v1 = hypotf(b->vx, b->vy);
        for (int i = 0; i < 240 * 3 && F.nballs; i++) step(1, 0);
        printf("  %.1f        %5.1f               %5.2f   %s\n", y0, sp, v1, !F.nballs ? "verloren" : hypotf(F.balls[0].vx, F.balls[0].vy) < 0.3f && F.balls[0].x < OTHER_X ? "ruht in der Mulde" : "weg");
    }

    printf("\nG  Live Catch (Ball kommt die Fuehrung herunter; so viele ms VOR der Beruehrung druecken und halten)\n  vorher    Tempo nach 1 s  Ergebnis\n");
    for (int pre = 0; pre <= 400; pre += 50) {
        setup(real);
        vp_ball_t *b = vp_create_ball(&F, 3.4f, 4.4f);
        if (!b) continue;
        /* erst ohne Flipper laufen lassen und die Beruehrung finden */
        int hit_at = -1;
        for (int i = 0; i < 240 * 6 && F.nballs; i++) {
            b = &F.balls[0];
            if (getenv("FT_DBG") && i % 24 == 0) printf("    t %.2f x %.2f y %.2f touch %d\n", i / 240.0, b->x, b->y, b->ntouch);
            if (b->ntouch && b->x > px && b->y < 3.6f) { hit_at = i; break; }
            step(0, 0);
        }
        if (hit_at < 0) { printf("  %3d ms    -       keine Beruehrung\n", pre); continue; }
        setup(real);
        b = vp_create_ball(&F, 3.4f, 4.4f);
        const int at = hit_at - pre * 240 / 1000;
        for (int i = 0; i < 240 * 4 && F.nballs; i++) step(i >= at, 0);
        if (!F.nballs) { printf("  %3d ms    -       verloren\n", pre); continue; }
        b = &F.balls[0];
        printf("  %3d ms   %5.2f   %s\n", pre, hypotf(b->vx, b->vy), hypotf(b->vx, b->vy) < 0.3f && b->x < OTHER_X && b->y < 4.0f ? "ruht in der Mulde" : "weg");
    }
    return 0;
}
