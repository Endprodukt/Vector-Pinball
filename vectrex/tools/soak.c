/*
 * soak.c - Dauerlauf des Spielkerns ohne Grafik: jeder Tisch wird mit einem einfachen Automaten gespielt
 * (Flipper schlagen, wenn ein Ball in ihrer Naehe ist). Prueft, dass kein Ball das Feld verlaesst oder
 * ungueltige Werte annimmt, und zeigt Punkte und Ereignisse.
 *
 *   tools/soak.exe [sekunden je tisch] [tisch 1..9] [seed]
 */
#include "vp.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

static vp_field_t F;

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 120, only = argc > 2 ? atoi(argv[2]) : 0;
    uint32_t seed = argc > 3 ? (uint32_t)atoi(argv[3]) : 12345;
    int bad = 0;
    printf("sizeof(vp_field_t) = %u\n", (unsigned)sizeof F);
    for (int t = 0; t < vp_num_tables; t++) {
        if (only && only != t + 1) continue;
        vp_load(&F, t, seed + (uint32_t)t);
        vp_set_real_physics(&F, getenv("SOAK_REAL") != NULL);
        vp_start_game(&F, 1);
        const float dt = 1.0f / 60.0f;
        uint32_t ms = 0;
        long steps = 0, escapes = 0, nans = 0, launches = 0, msgs = 0, kicks = 0;
        float vmax = 0;
        char last[48] = "";
        int maxballs = 0;
        clock_t c0 = clock();
        for (int fr = 0; fr < secs * 60; fr++) {
            int l = 0, r = 0;
            for (int i = 0; i < F.nballs; i++) {
                const vp_ball_t *b = &F.balls[i];
                int pulse = (fr % 22) < 9;                                         /* schlagen und wieder loslassen */
                if (b->y < 5.0f && b->y > 1.0f && pulse) { if (b->x < F.def->width * 0.5f - 0.6f) l = 1; else r = 1; }
                if (b->y > 10 && ((fr / 7) % 9) == 0) { l = r = 1; }               /* obere Flipper */
            }
            vp_set_flippers(&F, l, r);
            if (F.nballs == 0 && vp_launch_if_needed(&F)) launches++;
            vp_advance(&F, dt, ms, 1.0f);
            ms += 16; steps++;
            if (getenv("SOAK_POS") && fr % 300 == 0 && F.nballs) printf("   [%5.1fs] x %.2f y %.2f v %.2f %.2f score %lld\n", fr / 60.0, F.balls[0].x, F.balls[0].y, F.balls[0].vx, F.balls[0].vy, (long long)F.score);
            if (F.nballs > maxballs) maxballs = F.nballs;
            if (F.sfx & VSFX_KICK) kicks++;
            F.sfx = 0;
            if (F.msg[0] && strcmp(last, F.msg)) { msgs++; snprintf(last, sizeof last, "%s", F.msg); if (getenv("SOAK_MSG")) printf("   [%5.1fs] %s\n", fr / 60.0, F.msg); }
            for (int i = 0; i < F.nballs; i++) {
                vp_ball_t *b = &F.balls[i];
                float v = hypotf(b->vx, b->vy);
                if (v > vmax) vmax = v;
                if (!(b->x == b->x) || !(b->y == b->y) || !(v == v)) { nans++; vp_remove_ball_quiet(&F, b); break; }
                if (b->x < -1 || b->x > F.def->width + 1 || b->y < -1 || b->y > F.def->height + 1) {
                    escapes++;
                    if (getenv("SOAK_ESC")) printf("   escape at %.2f,%.2f v %.2f,%.2f layer %d\n", b->x, b->y, b->vx, b->vy, b->layer);
                    vp_remove_ball(&F, b); break;
                }
            }
            if (!F.in_progress) vp_start_game(&F, 1);
        }
        double cpu = (double)(clock() - c0) / CLOCKS_PER_SEC;
        printf("table %d %-18s score %12lld  ball# %3d  launches %3ld  maxballs %d  msgs %3ld  kicks %4ld  vmax %5.1f  escapes %ld  nan %ld  cpu %.0f us/frame\n",
               t + 1, F.def->name, (long long)F.score, F.ball_number, launches, maxballs, msgs, kicks, vmax, escapes, nans, cpu * 1e6 / steps);
        bad += escapes + nans;
    }
    return bad ? 1 : 0;
}
