/* vpbind.c - the controls as they are, and laying them out: see vpbind.h. */
#include "vpbind.h"

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JOYS      8
#define BUTTONS   32
#define HATS      4
#define AXES      16
#define PAST_HALF 16384                 /* an axis this far out is a way pressed */
#define CENTRED   8000                  /* an axis seen this near the middle is a stick's (not a pedal's) */
#define MOVED     20000                 /* how far from where it rested an axis must go to be laid out */

static const char *const hat_ways[4] = { "up", "right", "down", "left" };
static const Uint8 hat_bits[4] = { SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT };
static const char *const mouse_names[5] = { "left", "middle", "right", "x1", "x2" };

/* Everything there is, this step [0] and the last [1]. */
static struct {
    Uint8  key[2][SDL_NUM_SCANCODES];
    Uint8  button[2][JOYS][BUTTONS];
    Uint8  hat[2][JOYS][HATS];
    Sint16 axis[2][JOYS][AXES];
    Uint8  centred[JOYS][AXES];         /* the axis has been near its middle: a stick's */
    Uint32 mouse[2];
    SDL_Joystick *joy[JOYS];
    int    joys;
    int    yoke;                        /* the first joystick whose name says "yoke", or -1 */
    int    opened;                      /* the joysticks have been looked for */
    int    reserved[8], nreserved;      /* keys a game keeps for itself besides the usual */
} I;

static void open_joysticks(void)
{
    int i, n = SDL_NumJoysticks();

    for (i = 0; i < JOYS; i++) if (I.joy[i]) SDL_JoystickClose(I.joy[i]);
    memset(I.joy, 0, sizeof I.joy);
    memset(I.centred, 0, sizeof I.centred);
    I.joys = n < 0 ? 0 : n > JOYS ? JOYS : n;
    I.yoke = -1;
    I.opened = 1;
    for (i = 0; i < I.joys; i++) {
        const char *name;
        char low[128];
        size_t c;
        I.joy[i] = SDL_JoystickOpen(i);
        name = I.joy[i] ? SDL_JoystickName(I.joy[i]) : NULL;
        for (c = 0; name && name[c] && c + 1 < sizeof low; c++) low[c] = (char)tolower((unsigned char)name[c]);
        low[name ? c : 0] = 0;
        if (I.yoke < 0 && strstr(low, "yoke")) I.yoke = i;
    }
}

/* "joy2_..." or "yoke_...": which joystick (from 0, -1 if not there), and
 * what follows the device's part. */
static int device(const char *one, const char **rest)
{
    int j, n = 0;
    if (!strncmp(one, "yoke_", 5)) { *rest = one + 5; return I.yoke; }
    if (sscanf(one, "joy%d_%n", &j, &n) == 1 && n > 0 && j >= 1 && j <= JOYS) { *rest = one + n; return j - 1; }
    *rest = one;
    return -1;
}

/* How a control of joystick `j` is written: "yoke_" for the yoke. */
static void device_name(int j, char *out, size_t size)
{
    if (j == I.yoke) snprintf(out, size, "yoke_");
    else snprintf(out, size, "joy%d_", j + 1);
}

void vpb_update(void)
{
    int count = 0, j, k;
    const Uint8 *keys = SDL_GetKeyboardState(&count);
    int changed = !I.opened || SDL_NumJoysticks() != I.joys;

    memcpy(I.key[1], I.key[0], sizeof I.key[0]);
    memcpy(I.button[1], I.button[0], sizeof I.button[0]);
    memcpy(I.hat[1], I.hat[0], sizeof I.hat[0]);
    memcpy(I.axis[1], I.axis[0], sizeof I.axis[0]);
    I.mouse[1] = I.mouse[0];

    memset(I.key[0], 0, sizeof I.key[0]);
    if (keys) memcpy(I.key[0], keys, (size_t)(count < SDL_NUM_SCANCODES ? count : SDL_NUM_SCANCODES));
    for (j = 0; j < I.joys && !changed; j++) changed = I.joy[j] && !SDL_JoystickGetAttached(I.joy[j]);
    if (changed) open_joysticks();
    memset(I.button[0], 0, sizeof I.button[0]);
    memset(I.hat[0], 0, sizeof I.hat[0]);
    memset(I.axis[0], 0, sizeof I.axis[0]);
    for (j = 0; j < I.joys; j++) {
        SDL_Joystick *s = I.joy[j];
        int n;
        if (!s) continue;
        n = SDL_JoystickNumButtons(s);
        for (k = 0; k < n && k < BUTTONS; k++) I.button[0][j][k] = SDL_JoystickGetButton(s, k) ? 1 : 0;
        n = SDL_JoystickNumHats(s);
        for (k = 0; k < n && k < HATS; k++) I.hat[0][j][k] = SDL_JoystickGetHat(s, k);
        n = SDL_JoystickNumAxes(s);
        for (k = 0; k < n && k < AXES; k++) {
            Sint16 v = SDL_JoystickGetAxis(s, k);
            I.axis[0][j][k] = v;
            if (v > -CENTRED && v < CENTRED) I.centred[j][k] = 1;
        }
    }
    I.mouse[0] = SDL_GetMouseState(NULL, NULL);
}

/* --- one control ------------------------------------------------------------------ */

static SDL_Scancode key_of(const char *name)
{
    char spaced[48];
    size_t i;
    for (i = 0; name[i] && i + 1 < sizeof spaced; i++) spaced[i] = name[i] == '_' ? ' ' : name[i];
    spaced[i] = 0;
    return SDL_GetScancodeFromName(spaced);
}

/* Is the control `one` down, this step (then 0) or the last (1)? */
static int down(const char *one, int then)
{
    int j, k, w;
    char way[8];
    const char *rest;

    if (!strncmp(one, "key_", 4)) {
        SDL_Scancode s = key_of(one + 4);
        return s > SDL_SCANCODE_UNKNOWN && s < SDL_NUM_SCANCODES && I.key[then][s];
    }
    j = device(one, &rest);
    if (rest != one) {
        if (j < 0) return 0;
        if (sscanf(rest, "button%d", &k) == 1) return k >= 1 && k <= BUTTONS && I.button[then][j][k - 1];
        if (sscanf(rest, "hat%d_%7s", &k, way) == 2 && k >= 1 && k <= HATS) {
            for (w = 0; w < 4; w++)
                if (!strcmp(way, hat_ways[w])) return (I.hat[then][j][k - 1] & hat_bits[w]) != 0;
            return 0;
        }
        if (sscanf(rest, "axis%d_%7s", &k, way) == 2 && k >= 1 && k <= AXES) {
            int v = I.axis[then][j][k - 1];
            return strcmp(way, "neg") ? v > PAST_HALF : v < -PAST_HALF;
        }
        return 0;
    }
    if (!strncmp(one, "mouse_", 6)) {
        for (w = 0; w < 5; w++)
            if (!strcmp(one + 6, mouse_names[w])) return (I.mouse[then] & SDL_BUTTON(w + 1)) != 0;
    }
    return 0;
}

/* Each control of a '+' list in turn: `test` on any of them. */
static int any(const char *controls, int pressed)
{
    char one[VPB_LENGTH];
    const char *p = controls;

    while (p && *p) {
        size_t n = strcspn(p, "+");
        if (n && n < sizeof one) {
            memcpy(one, p, n);
            one[n] = 0;
            if (down(one, 0) && (!pressed || !down(one, 1))) return 1;
        }
        p += n;
        if (*p == '+') p++;
    }
    return 0;
}

int vpb_held(const char *controls)    { return any(controls, 0); }
int vpb_pressed(const char *controls) { return any(controls, 1); }

/* --- '+' lists ---------------------------------------------------------------------- */

static int has_one(const char *controls, const char *one)
{
    size_t n = strlen(one);
    const char *p = controls;

    while ((p = strstr(p, one)) != NULL) {
        if ((p == controls || p[-1] == '+') && (p[n] == 0 || p[n] == '+')) return 1;
        p += n;
    }
    return 0;
}

static int count_of(const char *controls)
{
    int n = 1;
    if (!controls[0] || !strcmp(controls, "none")) return 0;
    for (; *controls; controls++) n += *controls == '+';
    return n;
}

/* `one` added to `controls` unless it has it; the first goes if that makes
 * more than `max`. */
static void add_one(char *controls, size_t size, const char *one, int max)
{
    if (!strcmp(controls, "none")) controls[0] = 0;
    if (has_one(controls, one)) return;
    while (max > 0 && count_of(controls) >= max) {
        const char *rest = strchr(controls, '+');
        if (rest) memmove(controls, rest + 1, strlen(rest + 1) + 1);
        else controls[0] = 0;
    }
    if (strlen(controls) + strlen(one) + 2 > size) return;
    if (controls[0]) strcat(controls, "+");
    strcat(controls, one);
}

/* AdvanceMAME's defaults, as the controls they are. */
static const char *const legacy[][2] = {
    { "p1_button1", "key_Left_Ctrl+joy1_button1+mouse_left" },
    { "p1_button2", "key_Left_Alt+joy1_button2+mouse_right" },
    { "p1_button3", "key_Space+joy1_button3+mouse_middle" },
    { "p1_button4", "key_Left_Shift+joy1_button4" },
    { "p1_button5", "key_Z+joy1_button5" },
    { "p1_button6", "key_X+joy1_button6" },
    { "p1_up",      "key_Up+joy1_axis2_neg" },
    { "p1_down",    "key_Down+joy1_axis2_pos" },
    { "p1_left",    "key_Left+joy1_axis1_neg" },
    { "p1_right",   "key_Right+joy1_axis1_pos" },
    { "p2_button1", "key_A+joy2_button1" },
    { "p2_button2", "key_S+joy2_button2" },
    { "p2_button3", "key_Q+joy2_button3" },
    { "p2_button4", "key_W+joy2_button4" },
    { "p2_up",      "key_R+joy2_axis2_neg" },
    { "p2_down",    "key_F+joy2_axis2_pos" },
    { "p2_left",    "key_D+joy2_axis1_neg" },
    { "p2_right",   "key_G+joy2_axis1_pos" },
};

void vpb_normalize(char *controls, size_t size)
{
    char out[VPB_LENGTH * 2] = "", one[VPB_LENGTH];
    const char *p = controls;
    int i, any_left = 0;

    while (*p) {
        size_t n = strcspn(p, "+");
        if (n && n < sizeof one) {
            const char *as = one, *q;
            memcpy(one, p, n);
            one[n] = 0;
            for (i = 0; i < (int)(sizeof legacy / sizeof legacy[0]); i++)
                if (!strcmp(one, legacy[i][0])) as = legacy[i][1];
            for (q = as; *q; ) {        /* each of what it stands for */
                char part[VPB_LENGTH];
                size_t m = strcspn(q, "+");
                memcpy(part, q, m);
                part[m] = 0;
                if (strcmp(part, "none")) { add_one(out, sizeof out, part, 0); any_left = 1; }
                q += m;
                if (*q == '+') q++;
            }
        }
        p += n;
        if (*p == '+') p++;
    }
    snprintf(controls, size, "%s", any_left ? out : "none");
}

/* --- the menus' own ----------------------------------------------------------------- */

static int key_now(int s)      { return s > 0 && s < SDL_NUM_SCANCODES && I.key[0][s]; }
int vpb_key_held(int s)        { return key_now(s); }
int vpb_key_pressed(int s)     { return key_now(s) && !I.key[1][s]; }

int vpb_menu_confirm(vp_input *in)
{
    return vp_input_pressed(in, "start1") || vpb_key_pressed(SDL_SCANCODE_RETURN)
           || vpb_key_pressed(SDL_SCANCODE_KP_ENTER);
}

int vpb_menu_confirm_held(vp_input *in)
{
    return vp_input_held(in, "start1") || key_now(SDL_SCANCODE_RETURN) || key_now(SDL_SCANCODE_KP_ENTER);
}

/* A way (hat bit) on any joystick's hats or first stick, this step or the last. */
static int stick_way(int bit, int then)
{
    int j, k;
    for (j = 0; j < I.joys; j++) {
        for (k = 0; k < HATS; k++) if (I.hat[then][j][k] & bit) return 1;
        if (bit == SDL_HAT_LEFT && I.centred[j][0] && I.axis[then][j][0] < -PAST_HALF) return 1;
        if (bit == SDL_HAT_RIGHT && I.centred[j][0] && I.axis[then][j][0] > PAST_HALF) return 1;
        if (bit == SDL_HAT_UP && I.centred[j][1] && I.axis[then][j][1] < -PAST_HALF) return 1;
        if (bit == SDL_HAT_DOWN && I.centred[j][1] && I.axis[then][j][1] > PAST_HALF) return 1;
    }
    return 0;
}

static int way(int key, int bit, int then) { return I.key[then][key] || stick_way(bit, then); }

int vpb_menu_up(void)             { return way(SDL_SCANCODE_UP, SDL_HAT_UP, 0) && !way(SDL_SCANCODE_UP, SDL_HAT_UP, 1); }
int vpb_menu_down(void)           { return way(SDL_SCANCODE_DOWN, SDL_HAT_DOWN, 0) && !way(SDL_SCANCODE_DOWN, SDL_HAT_DOWN, 1); }
int vpb_menu_left(void)           { return way(SDL_SCANCODE_LEFT, SDL_HAT_LEFT, 0); }
int vpb_menu_right(void)          { return way(SDL_SCANCODE_RIGHT, SDL_HAT_RIGHT, 0); }
int vpb_menu_way_x(void)          { return vpb_menu_right() - vpb_menu_left(); }
int vpb_menu_way_y(void)
{
    return way(SDL_SCANCODE_DOWN, SDL_HAT_DOWN, 0) - way(SDL_SCANCODE_UP, SDL_HAT_UP, 0);
}

int vpb_yoke(void) { return I.yoke; }

float vpb_joy_axis(int j, int axis)
{
    if (j < 0 || j >= I.joys || axis < 0 || axis >= AXES) return 0.0f;
    return (float)I.axis[0][j][axis] / 32767.0f;
}

/* The first joystick that is no yoke and has a stick (axes that rest in
 * their middle), or -1. */
int vpb_stick(void)
{
    int j;
    for (j = 0; j < I.joys; j++)
        if (j != I.yoke && I.centred[j][0] && I.centred[j][1]) return j;
    return -1;
}

int vpb_menu_left_pressed(void)   { return vpb_menu_left() && !way(SDL_SCANCODE_LEFT, SDL_HAT_LEFT, 1); }
int vpb_menu_right_pressed(void)  { return vpb_menu_right() && !way(SDL_SCANCODE_RIGHT, SDL_HAT_RIGHT, 1); }

/* --- on the screen ------------------------------------------------------------------ */

const char *vpb_spelled(const char *controls, char *buf, size_t size)
{
    char one[VPB_LENGTH], word[VPB_LENGTH];
    const char *p = controls;
    size_t o = 0;
    int j, k;

    buf[0] = 0;
    while (*p) {
        size_t n = strcspn(p, "+"), i;
        char way[8];
        memcpy(one, p, n < sizeof one ? n : sizeof one - 1);
        one[n < sizeof one ? n : sizeof one - 1] = 0;
        char dev[16] = "";
        const char *rest = one;
        if (!strncmp(one, "yoke_", 5)) { snprintf(dev, sizeof dev, "YOKE"); rest = one + 5; }
        else if (sscanf(one, "joy%d_%n", &j, &k) == 1 && k > 0) { snprintf(dev, sizeof dev, "JOY%d", j); rest = one + k; }
        if (!strncmp(one, "key_", 4)) snprintf(word, sizeof word, "%s", one + 4);
        else if (dev[0] && sscanf(rest, "button%d", &k) == 1) snprintf(word, sizeof word, "%s BUTTON %d", dev, k);
        else if (dev[0] && sscanf(rest, "hat%d_%7s", &k, way) == 2) {
            if (k == 1) snprintf(word, sizeof word, "%s HAT %s", dev, way);
            else snprintf(word, sizeof word, "%s HAT%d %s", dev, k, way);
        } else if (dev[0] && sscanf(rest, "axis%d_%7s", &k, way) == 2)
            snprintf(word, sizeof word, "%s AXIS %d %s", dev, k, strcmp(way, "neg") ? "+" : "-");
        else if (!strncmp(one, "mouse_", 6)) snprintf(word, sizeof word, "MOUSE %s", one + 6);
        else snprintf(word, sizeof word, "%s", one);
        if (o && o + 3 < size) { memcpy(buf + o, " + ", 3); o += 3; }
        for (i = 0; word[i] && o + 1 < size; i++)
            buf[o++] = word[i] == '_' ? ' ' : (char)toupper((unsigned char)word[i]);
        buf[o] = 0;
        p += n;
        if (*p == '+') p++;
    }
    if (!o) snprintf(buf, size, "NONE");
    return buf;
}

/* --- laying out ----------------------------------------------------------------- */

/* What the cabinet and the menus keep for themselves. */
void vpb_reserve_key(int scancode)
{
    if (I.nreserved < (int)(sizeof I.reserved / sizeof I.reserved[0])) I.reserved[I.nreserved++] = scancode;
}

static int reserved_key(int s)
{
    int i;
    for (i = 0; i < I.nreserved; i++) if (s == I.reserved[i]) return 1;
    return s == SDL_SCANCODE_F2 || s == SDL_SCANCODE_ESCAPE || s == SDL_SCANCODE_RETURN
           || s == SDL_SCANCODE_KP_ENTER || s == SDL_SCANCODE_DELETE || s == SDL_SCANCODE_KP_PERIOD;
}

static int reserved_held(vp_input *in)
{
    static const char *const cabinet[] = { "start1", "start2", "coin1", "coin2", "ui_cancel" };
    int i;
    for (i = 0; i < 5; i++) if (vp_input_held(in, cabinet[i])) return 1;
    for (i = 1; i < SDL_NUM_SCANCODES; i++) if (I.key[0][i] && reserved_key(i)) return 1;
    return 0;
}

static void rest_axes(vpb_layout *l)
{
    int j, k;
    for (j = 0; j < JOYS; j++)
        for (k = 0; k < AXES; k++) l->rest[j][k] = I.axis[0][j][k];
}

/* The control that has just gone down, into `one`; 0 if none. */
static int fresh(const vpb_layout *l, char *one, size_t size)
{
    int i, j, k, w;

    for (i = SDL_SCANCODE_A; i < SDL_NUM_SCANCODES; i++) {
        const char *name;
        size_t c;
        if (!I.key[0][i] || I.key[1][i] || reserved_key(i)) continue;
        name = SDL_GetScancodeName((SDL_Scancode)i);
        if (!name || !name[0]) continue;
        snprintf(one, size, "key_%s", name);
        for (c = 0; one[c]; c++) if (one[c] == ' ') one[c] = '_';
        return 1;
    }
    for (j = 0; j < I.joys; j++) {
        char dev[16];
        device_name(j, dev, sizeof dev);
        for (k = 0; k < BUTTONS; k++)
            if (I.button[0][j][k] && !I.button[1][j][k]) { snprintf(one, size, "%sbutton%d", dev, k + 1); return 1; }
        for (k = 0; k < HATS; k++)
            for (w = 0; w < 4; w++)
                if ((I.hat[0][j][k] & hat_bits[w]) && !(I.hat[1][j][k] & hat_bits[w])) {
                    snprintf(one, size, "%shat%d_%s", dev, k + 1, hat_ways[w]);
                    return 1;
                }
        for (k = 0; k < AXES; k++) {
            int v = I.axis[0][j][k], from = l->rest[j][k];
            if (abs(v - from) < MOVED || (v < PAST_HALF && v > -PAST_HALF)) continue;
            snprintf(one, size, "%saxis%d_%s", dev, k + 1, v < 0 ? "neg" : "pos");
            return 1;
        }
    }
    for (w = 0; w < 5; w++)
        if ((I.mouse[0] & SDL_BUTTON(w + 1)) && !(I.mouse[1] & SDL_BUTTON(w + 1))) {
            snprintf(one, size, "mouse_%s", mouse_names[w]);
            return 1;
        }
    return 0;
}

static void say(vpb_layout *l, int frames, const char *what)
{
    l->said_frames = frames;
    snprintf(l->said, sizeof l->said, "%s", what);
}

void vpb_layout_moved(vpb_layout *l) { l->adding = 0; }

void vpb_layout_begin(vpb_layout *l, const char *has)
{
    l->capturing = 1;
    l->armed = 0;
    l->taken[0] = 0;
    if (l->adding && strcmp(has, "none")) snprintf(l->taken, sizeof l->taken, "%s", has);   /* else the first replaces */
    l->count = count_of(l->taken);
    rest_axes(l);
}

static void done(vpb_layout *l, char *has, size_t size)
{
    l->capturing = 0;
    l->let_go = 1;                      /* what was pressed may be left or right as well */
    if (!l->taken[0]) snprintf(l->taken, sizeof l->taken, "none");
    snprintf(has, size, "%s", l->taken);
    l->adding = strcmp(l->taken, "none") != 0;
}

void vpb_layout_standard(vpb_layout *l, char *has, size_t size, const char *standard)
{
    snprintf(l->taken, sizeof l->taken, "%s", standard);
    done(l, has, size);
    l->adding = 0;                      /* the next replaces it again */
}

void vpb_layout_toggle(vpb_layout *l, char *has, size_t size, const char *standard)
{
    if (!strcmp(has, "none")) { vpb_layout_standard(l, has, size, standard); return; }
    snprintf(has, size, "none");
    l->adding = 0;
}

int vpb_layout_step(vpb_layout *l, vp_input *in, char *has, size_t size, const char *standard)
{
    char one[VPB_LENGTH];

    if (!l->armed) {                    /* the press that took the row must end first */
        l->armed = !vpb_menu_confirm_held(in);
        rest_axes(l);
        return VPB_WAITING;
    }
    if (vpb_menu_confirm(in)) { l->capturing = 0; return VPB_LEFT; }
    if (vp_input_pressed(in, "start2")) {
        snprintf(l->taken, sizeof l->taken, "none");
        done(l, has, size);
        return VPB_CHANGED;
    }
    if (vpb_key_pressed(SDL_SCANCODE_DELETE) || vpb_key_pressed(SDL_SCANCODE_KP_PERIOD)) {
        vpb_layout_standard(l, has, size, standard);
        return VPB_CHANGED;
    }
    if (reserved_held(in)) {            /* a start, a coin, the way out ... is not taken */
        if (fresh(l, one, sizeof one)) say(l, 90, "START, COIN, ENTER AND EXIT STAY THE MENU'S");
        return VPB_WAITING;
    }
    if (!fresh(l, one, sizeof one)) return VPB_WAITING;
    add_one(l->taken, sizeof l->taken, one, VPB_MAX);
    done(l, has, size);
    return VPB_CHANGED;
}

void vpb_layout_filter(vpb_layout *l, int *left, int *right)
{
    if (!l->let_go) return;
    l->let_go = *left || *right;
    *left = *right = 0;
}

const char *vpb_layout_said(vpb_layout *l)
{
    if (l->said_frames <= 0) return NULL;
    l->said_frames--;
    return l->said;
}

void vpb_layout_help(const vpb_layout *l, const char **one, const char **two)
{
    if (l->capturing) {
        *one = l->count ? "PRESS THE BUTTON TO ADD" : "PRESS THE BUTTON";
        *two = "START 1 - BACK  DEL - STANDARD  START 2 - NONE";
    } else {
        *one = "LEFT, RIGHT - NONE / STANDARD";
        *two = "START, ENTER - ADD BUTTON   DEL - STANDARD";
    }
}
