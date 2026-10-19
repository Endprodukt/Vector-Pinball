/* trace.c - Ballbahn eines Tisches als Text: tools/trace.exe tisch sekunden [flip: 0 nie, 1 Automat] */
#include "vp.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
static vp_field_t F;
int main(int argc, char **argv)
{
    int t = argc > 1 ? atoi(argv[1]) - 1 : 0, secs = argc > 2 ? atoi(argv[2]) : 20, mode = argc > 3 ? atoi(argv[3]) : 0;
    int every = argc > 4 ? atoi(argv[4]) : 6;
    vp_load(&F, t, 777); vp_set_real_physics(&F, getenv("TRACE_REAL") != NULL); vp_start_game(&F, 1);
    uint32_t ms = 0;
    for (int fr = 0; fr < secs * 60; fr++) {
        int l = 0, r = 0;
        if (mode) for (int i = 0; i < F.nballs; i++) {
            const vp_ball_t *b = &F.balls[i];
            if (b->y < 5.0f && b->y > 1.0f && (fr % 22) < 9) { if (b->x < F.def->width * 0.5f - 0.6f) l = 1; else r = 1; }
        }
        vp_set_flippers(&F, l, r);
        if (F.nballs == 0) vp_launch_if_needed(&F);
        vp_advance(&F, 1.0f / 60, ms, 1.0f); ms += 16;
        if (fr % every == 0 && F.nballs) {
            const vp_ball_t *b = &F.balls[0];
            printf("%6.2f  x %6.2f y %6.2f  v %6.2f %6.2f  w %6.2f  L%d  n%d  score %lld %s\n", fr / 60.0, b->x, b->y, b->vx, b->vy, b->angvel, b->layer, F.nballs, (long long)F.score, F.msg);
        }
    }
    return 0;
}
