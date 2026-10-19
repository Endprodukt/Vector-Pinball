/*
 * vp_deleg.h - gemeinsame Helfer der Tischlogik (vp_table1.c .. vp_table9.c).
 * Jede Datei entspricht einem FieldNDelegate.java des Originals; Texte sind die englischen des Originals.
 */
#ifndef VP_DELEG_H
#define VP_DELEG_H

#include "vp.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define VP_TAU 6.2831853f

static inline void dg_restore_saver(vp_field_t *f, const char *id) { vp_wall_retract(f, vp_find(f, id), 0); }
/* Startrampe: enabled = Sperre steht (Ball kann nicht zurueck in die Rampe) */
static inline void dg_launch_barrier(vp_field_t *f, int enabled) { vp_wall_retract(f, vp_find(f, "LaunchBarrier"), !enabled); }

/* "Left/Right Save Enabled": liefert 1, wenn es eine der beiden Zielgruppen war */
static inline int dg_save_targets(vp_field_t *f, int e, const char *left_id, const char *right_id)
{
    if (vp_is(f, e, left_id))  { dg_restore_saver(f, "BallSaver-left");  vp_message(f, "Left Save Enabled", 1500, 1);  return 1; }
    if (vp_is(f, e, right_id)) { dg_restore_saver(f, "BallSaver-right"); vp_message(f, "Right Save Enabled", 1500, 1); return 1; }
    return 0;
}

static inline void dg_act_launch_max3(vp_field_t *f, int arg) { (void)arg; if (f->nballs < 3) vp_launch_ball(f); }

extern const vp_delegate_t vp_delegate1, vp_delegate2, vp_delegate3, vp_delegate4, vp_delegate5,
                           vp_delegate6, vp_delegate7, vp_delegate8, vp_delegate9, vp_delegate10;

#endif
