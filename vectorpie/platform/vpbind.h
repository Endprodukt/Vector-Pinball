/* vpbind.h - the controls as they are, and laying them out on a game's functions.
 *
 * The same in every vector game of this cabinet: the master copy is the
 * Tempest 2000 port's src/vpbind.[ch], every game has a copy kept the same.
 * Everything a player can lay out is read directly, not through advmame.rc:
 * the keyboard, every joystick (yokes and sticks: buttons, hats, axes) and
 * the mouse's buttons.
 * A function's controls are text, '+' between them, up to VPB_MAX:
 *
 *   key_Left_Ctrl          a key: "key_" and SDL's name, '_' for spaces
 *   joy1_button3           a joystick's button (joysticks and buttons from 1)
 *   yoke_button3           the same of the yoke: the first joystick whose
 *                          name says "yoke", whatever its number
 *   joy1_hat1_up           a hat's way: up, down, left, right
 *   joy1_axis2_neg         an axis past half its travel: neg or pos
 *   mouse_left             a mouse button: left, middle, right, x1, x2
 *   none                   no control
 *
 * advmame.rc's names (p1_button1 ...) in an old file are read as the
 * controls AdvanceMAME gives them by default (vpb_normalize).
 *
 * The menus never go by what is laid out: they have their own controls,
 * there whatever a function has - the arrow keys, any joystick's hat or
 * stick, START 1, Enter and the keypad's Enter (vpb_menu_*). The spinner is
 * the mouse, whose moves only the SDK reads (vp_input_delta).
 *
 * Laying out (vpb_layout): START 1 or Enter on a function's row, then one
 * control. The first since the cursor came to the row replaces what the
 * function had, every further one on the row is added (up to VPB_MAX, the
 * oldest goes). While laying out START 1 / Enter leave it as it was, START 2
 * makes it none, Delete (also the keypad's) its standard controls. Left and
 * right on the row switch between none and the standard. A control may be
 * on several functions at once. START, COIN, the way out, F2, Esc, Enter
 * and Delete are never laid out.
 */
#ifndef VPBIND_H
#define VPBIND_H

#include <stddef.h>
#include <vectorpie/vp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VPB_LENGTH 128                  /* a function's controls as text, at most */
#define VPB_MAX    4                    /* controls one function may have */

/* Once every game step, after the SDK has read its input: the keyboard, the
 * joysticks and the mouse's buttons as they are now. */
void vpb_update(void);

/* Any of a function's controls held, or down since the last step. */
int vpb_held(const char *controls);
int vpb_pressed(const char *controls);

/* advmame.rc names in `controls` turned into the controls they stand for. */
void vpb_normalize(char *controls, size_t size);

/* The menus' own controls. */
int vpb_menu_confirm(vp_input *in);     /* START 1, Enter, keypad Enter: down since the last step */
int vpb_menu_confirm_held(vp_input *in);
int vpb_menu_up(void);                  /* down since the last step */
int vpb_menu_down(void);
int vpb_menu_left(void);                /* held */
int vpb_menu_right(void);
int vpb_menu_left_pressed(void);
int vpb_menu_right_pressed(void);
int vpb_menu_way_x(void);               /* held: -1 left, 1 right, 0 */
int vpb_menu_way_y(void);               /* held: -1 up, 1 down, 0 */
int vpb_key_pressed(int scancode);      /* an SDL scancode, down since the last step */
int vpb_key_held(int scancode);

/* A key a game keeps for itself besides the usual (TAB for its menu ...):
 * never laid out. */
void vpb_reserve_key(int scancode);

/* The joysticks as they are: the yoke's number (from 0) or -1; the first
 * other one with a stick, or -1; an axis, -1..1. */
int   vpb_yoke(void);
int   vpb_stick(void);
float vpb_joy_axis(int joystick, int axis);

/* "LEFT CTRL + JOY1 BUTTON 3": what the controls are, for the screen. */
const char *vpb_spelled(const char *controls, char *buf, size_t size);

/* Laying out the functions of a menu. All zero to begin with. */
typedef struct vpb_layout {
    int   capturing, armed, adding, let_go;
    char  taken[VPB_LENGTH];
    int   count;
    int   said_frames;                  /* frames left of saying `said` */
    char  said[64];
    short rest[8][16];                  /* the joysticks' axes when it began */
} vpb_layout;

enum { VPB_WAITING, VPB_CHANGED, VPB_LEFT };

/* The cursor went to another row (or page): the next control replaces. */
void vpb_layout_moved(vpb_layout *l);
/* START 1 / Enter on a function's row whose controls are `has`. */
void vpb_layout_begin(vpb_layout *l, const char *has);
/* Each step while l->capturing: VPB_CHANGED with `has` new, VPB_LEFT when
 * it was left as it was, else VPB_WAITING. */
int  vpb_layout_step(vpb_layout *l, vp_input *in, char *has, size_t size, const char *standard);
/* Delete on the row: its standard controls. */
void vpb_layout_standard(vpb_layout *l, char *has, size_t size, const char *standard);
/* Left or right on the row: none, or its standard controls. */
void vpb_layout_toggle(vpb_layout *l, char *has, size_t size, const char *standard);
/* Left and right as the menu should take them: after a control was laid
 * out they count only once they have been let go. */
void vpb_layout_filter(vpb_layout *l, int *left, int *right);
/* What the menu should say under its rows now, or NULL. Once a step. */
const char *vpb_layout_said(vpb_layout *l);
/* Help for the bottom of the screen, two lines, while laying out / on a
 * function's row. */
void vpb_layout_help(const vpb_layout *l, const char **one, const char **two);

#ifdef __cplusplus
}
#endif

#endif /* VPBIND_H */
