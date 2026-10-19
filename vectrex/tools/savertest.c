/*
 * savertest.c - prueft die Ballretter in den Aussenbahnen: ein Ball faellt in die linke bzw. rechte Aussenbahn
 * und muss vom Retter (Wand mit "kick", zieht sich nach dem Treffer zurueck) wieder nach oben geworfen werden.
 * Zeigt je Tisch die groesste Hoehe nach dem Treffer; "LOST" = der Ball ging trotzdem verloren.
 *
 *   tools/savertest.exe [real 0|1]
 */
#include "vp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static vp_field_t F;

static float run(int table, const char *id, int real, int *lost)
{
    vp_load(&F, table, 99);
    vp_set_real_physics(&F, real);
    vp_start_game(&F, 1);
    int e = vp_find(&F, id);
    *lost = 0;
    if (e < 0) return -1;
    const vp_seg_t *s = &F.es[e].seg;
    vp_ball_t *b = vp_create_ball(&F, (s->x1 + s->x2) * 0.5f, s->y1 + 4.0f);
    b->vy = -6.0f;
    F.t_ball_start = -1000;                       /* kein Gnadenball */
    F.used_mercy = 1;
    float top = -1;
    int hit = 0;
    uint32_t ms = 0;
    for (int fr = 0; fr < 240; fr++) {
        vp_advance(&F, 1.0f / 60, ms, 1.0f); ms += 16;
        if (F.es[e].retracted) hit = 1;
        if (F.nballs == 0) { *lost = 1; break; }
        if (hit && F.balls[0].y > top) top = F.balls[0].y;
    }
    return top - s->y1;
}

int main(int argc, char **argv)
{
    int real = argc > 1 ? atoi(argv[1]) : 0, bad = 0;
    for (int t = 0; t < vp_num_tables; t++) {
        int ll, lr;
        float l = run(t, "BallSaver-left", real, &ll), r = run(t, "BallSaver-right", real, &lr);
        printf("table %d  left: rise %5.2f %s   right: rise %5.2f %s\n", t + 1, l, ll ? "LOST" : "ok", r, lr ? "LOST" : "ok");
        if (l >= 0 && (ll || l < 3)) bad++;
        if (r >= 0 && (lr || r < 3)) bad++;
    }
    return bad != 0;
}
