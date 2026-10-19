/* nicht zeitkritisch: auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp (das Kit baut sonst mit -O3) */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include "settings.h"
#include "plat.h"
#include <string.h>
#include <stddef.h>

#define SET_FILE  "settings.bin"
#define SET_MAGIC 0x56503034u        /* "VP04" */

settings_t g_set;

void settings_defaults(void)
{
    memset(&g_set, 0, sizeof g_set);
    g_set.sound = SOUND_BOTH;
    g_set.detail = DETAIL_HIGH;      /* immer HIGH - die Option DETAIL ist weg (Olli, 2026-10-09) */
    g_set.zoom = 3;                  /* 1,6-fach */
    g_set.physics = PHYSICS_REALISTIC;
    g_set.music = 1;
    g_set.music_vol = 7;
    g_set.players = 1;
    g_set.speed = SPEED_FAST;
    g_set.zero_every = 0;
    g_set.zero_jump = 24;            /* Vorgabe des Kits (aus Mitschnitten gemessen) */
    g_set.nudge_power = NUDGE_NORMAL;
    g_set.tilt_sens = TILT_MEDIUM;
    g_set.view3d = V3D_ALL;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* VERSUCH (Olli, 2026-10-04): 3D auch ohne Zoom, um zu sehen, was die Roehre schafft. Vorher: EVERYTHING ab 1,4-fach, OBJECTS und
 * FLIPPERS ab 1,2-fach (`return g_set.view3d == V3D_ALL ? 2 : (g_set.view3d != V3D_OFF ? 1 : 0);`) */
int settings_min_zoom(void) { return 0; }

void settings_load(void)
{
    uint8_t buf[4 + sizeof(settings_t)];
    uint32_t magic;
    settings_defaults();
    /* Eine aeltere, kuerzere Datei gilt weiter: was sie nicht enthaelt, behaelt seine Vorgabe (die Bildanpassung soll bei
     * einem Update nicht verloren gehen). Kuerzer als der erste Stand mit diesem Kennwort ist sie ungueltig. */
    size_t n = plat_read_file(SET_FILE, buf, sizeof buf);
    if (n < 4 + offsetof(settings_t, nudge_invert)) return;
    memcpy(&magic, buf, 4);
    if (magic != SET_MAGIC) return;
    memcpy(&g_set, buf + 4, n - 4);
    g_set.nudge_invert = g_set.nudge_invert != 0;
    if (g_set.nudge_power >= NUDGE_N) g_set.nudge_power = NUDGE_NORMAL;
    if (g_set.tilt_sens >= TILT_N) g_set.tilt_sens = TILT_LOW;
    if (g_set.view3d >= V3D_N) g_set.view3d = V3D_ALL;
    g_set.info = 0;
    if (g_set.zoom < settings_min_zoom()) g_set.zoom = (uint8_t)settings_min_zoom();
    if (g_set.plunger >= PLUNGER_N) g_set.plunger = PLUNGER_ANALOG;
    if (g_set.table >= NUM_TABLES_MAX) g_set.table = 0;
    if (g_set.sound >= SOUND_N) g_set.sound = SOUND_BOTH;
    g_set.detail = DETAIL_HIGH;      /* gespeicherte Werte zaehlen nicht mehr */
    if (g_set.physics >= PHYSICS_N) g_set.physics = PHYSICS_REALISTIC;
    if (g_set.music_vol > 10) g_set.music_vol = 7;
    if (g_set.players < 1 || g_set.players > 4) g_set.players = 1;
    if (g_set.speed >= SPEED_N) g_set.speed = SPEED_FAST;
    if (g_set.zoom > ZOOM_STEPS) g_set.zoom = 3;
    g_set.music = g_set.music != 0;
    g_set.cal_dx = (int8_t)clampi(g_set.cal_dx, -40, 40);
    g_set.cal_dy = (int8_t)clampi(g_set.cal_dy, -40, 40);
    g_set.cal_size = (int8_t)clampi(g_set.cal_size, -40, 25);
    g_set.cal_bright = (int8_t)clampi(g_set.cal_bright, -50, 50);
    g_set.cal_ball = (int8_t)clampi(g_set.cal_ball, -60, 30);
    g_set.cal_hud_dy = (int8_t)clampi(g_set.cal_hud_dy, -30, 30);
    g_set.zero_every = 0;
    g_set.zero_jump = (uint8_t)clampi(g_set.zero_jump, 0, 80);
}

void settings_save(void)
{
    uint8_t buf[4 + sizeof(settings_t)];
    uint32_t magic = SET_MAGIC;
    memcpy(buf, &magic, 4);
    memcpy(buf + 4, &g_set, sizeof g_set);
    plat_write_file(SET_FILE, buf, sizeof buf);
}
