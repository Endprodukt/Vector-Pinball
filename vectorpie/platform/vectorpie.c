/*
 * vectorpie.c - Plattform VectorPie (Raspberry Pi + USB-DVG/BeamForge, oder ein Fenster auf dem PC): implementiert plat.h
 * und vpie.h mit dem VectorPie SDK. Gegenstueck zu vectrex/platform/host_sdl.c und uvmc2.c.
 *
 * Schleife: NICHT vp_run, sondern dieselben Schritte mit den oeffentlichen Aufrufen des SDK (vpie_run unten). Grund ist
 * der Ton: vp_run oeffnet immer das Audiogeraet von vp_audio, das Spiel mischt aber selbst (vectrex/game/pcm.c: Stereo nach
 * Balllage, Ducking, MP3, nachgebildeter Klangchip) und braucht dafuer ein eigenes Geraet - und ein zweites bekommt man
 * nicht ueberall ("Audio device already open", so gesehen mit SDLs Ersatztreiber). Was vp_run sonst tut, steht hier nach:
 * Ausgang waehlen wie jedes VectorPie-Spiel (VP_OUTPUT, advmame.rc, Schalter --dvg/--window/--windowed/--vertical), auf
 * ui_cancel, geschlossenes Fenster oder Strg+C sauber enden (die Platine bekommt ihre Endefolge), auf der Platine das
 * Marquee auf dem HDMI-Schirm, die START-Lampen zuruecksetzen, VP_STATS=1. Aendert sich vp_run im SDK
 * (sdk/src/app/vp_app.cpp), hier nachziehen.
 * Das Spiel rechnet wie auf der Vectrex einmal je Bild nach der Echtzeit.
 *
 * Dateien: Einstellungen und Bestenliste in save/ (vp_save_dir), Tonaufnahmen in data/ neben dem Programm.
 *
 * Testhilfen (Umgebung, wie beim Vectrex-Simulator): VP_FRAMES=n beendet nach n Bildern, VP_SHOT_AT=n1,n2 schreibt die
 * Striche dieser Bilder nach shot_<n>.txt (tools/shot.py macht daraus ein PNG), VP_KEYS="bild:maske[:x:y],..." spielt
 * Eingaben ab, VP_FIXED_DT=1 rechnet mit fester Bildzeit, VP_MUTE=1 ohne Ton, VP_HZ=n Bilder je Sekunde.
 */
#include <vectorpie/vp.h>
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <signal.h>

#include "plat.h"
#include "vpie.h"
#include "vpbind.h"
#include "pcm.h"
#include "aysynth.h"
#include "settings.h"

#define SCR_HALF_H 189.0f                 /* halbe Bildhoehe in Bildschirmeinheiten (wie auf der Vectrex) */

static vp_dvg   *g_dvg;
static vp_input *g_in;
static vp_dvg   *g_out;                   /* nur waehrend eines Bildes gueltig */
static void (*g_setup)(void), (*g_loop)(void);   /* Spiel: einmal, je Bild */
static int g_vertical, g_frame, g_frames_max = -1;
static float g_kx, g_ky;                  /* Q4 -> Platineneinheiten */
static int g_bx = -1, g_by = -1;          /* wo der Strahl steht */
static FILE *g_shot;

/* ---- Steuerung ----
 * Das Spiel kennt einen Stick und vier Knoepfe (wie auf der Vectrex). Welcher Eingang des Automaten was tut, ist FREI
 * belegbar, unabhaengig von advmame.rc (Service, CONTROLS; Olli, 2026-10-15): jede Taste, jeder Knopf und jedes
 * Steuerkreuz eines Joysticks, eine Achse als Knopf (halber Ausschlag), oder - fuer die beiden analogen Funktionen - eine
 * ganze Achse, etwa die eines Yokes. Alles wird direkt gelesen, nie ueber advmame.rc; bis zu VPIE_BIND_MAX Eingaenge je
 * Funktion, ", " dazwischen, und ein Eingang darf mehrere Funktionen haben. Die Vorgabe (STANDARD) sind die Tasten,
 * Knoepfe und Maustasten, die AdvanceMAME diesen Knoepfen gibt; alte advmame-Namen (p1_button1 ...) in einer Datei werden
 * beim Lesen dazu. Gespeichert wird in save/settings.ini, das geht vor game.ini und bleibt bei einem Update stehen.
 * "none" schaltet eine Funktion ab. Belegt wird wie in allen Vektorspielen des Automaten (game/service.c, CONTROLS).
 *
 * Schreibweise einer Belegung (in settings.ini):
 *   p1_button1        alte Schreibweise: beim Lesen die Eingaenge, die AdvanceMAME ihr gibt
 *   key:Left Ctrl     Taste (SDL-Name)
 *   joy0:b3           Knopf 3 des Joysticks 0 (gezaehlt ab 0; angezeigt ab 1)
 *   joy0:h0:u         Steuerkreuz 0, Richtung u/d/l/r
 *   joy0:a1+          Achse 1 als Knopf: gedrueckt ab halbem Ausschlag nach + (bzw. -)
 *   joy0:a1           Achse 1 ganz, -1..1 (nur NUDGE AXIS und YOKE AXIS); "joy0:a1~" = umgedreht
 *   mouse:b1          Maustaste
 * Fest bleiben: coin1/coin2, start1/start2 und ui_configure aus advmame.rc, die Tab-Taste fuer den Service und Esc
 * (ui_cancel) zum Beenden - die drei Funktionen START, COIN, SERVICE kommen nur hinzu. Ausserhalb des Spiels (Menues,
 * Service, Initialen) gelten nur die festen Eingaenge (vpbind.h): Pfeile, jeder Stick/Hat, START 1, Enter. */
#define BIND_LEN 160
/* Abschuss nur ueber den Stoessel (PLUNGER: Knopf = digital, YOKE AXIS = analog; Olli, 2026-10-19): die Vorgabe des
 * Stoessels hat darum auch die Tasten des frueheren Abschussknopfs (Leertaste, Knopf 3). */
static const char *const BIND_STANDARD[VPIE_F_N + 1] = {
    "", "key:Left Ctrl, joy0:b0, mouse:b1", "none", "key:Left Alt, joy0:b1, mouse:b3",
    "key:Left, joy0:a0-", "key:Right, joy0:a0+", "key:Up, joy0:a1-", "key:Down, key:Space, joy0:a1+, joy0:b2",
    "none", "none", "none", "none", "none" };
static char g_bind[VPIE_F_N + 1][BIND_LEN] = { "", "key:Left Ctrl, joy0:b0, mouse:b1", "none", "key:Left Alt, joy0:b1, mouse:b3",
                                               "key:Left, joy0:a0-", "key:Right, joy0:a0+", "key:Up, joy0:a1-",
                                               "key:Down, key:Space, joy0:a1+, joy0:b2",
                                               "none", "none", "none", "none", "none" };
static const char *const BIND_KEY[VPIE_F_N + 1] = { "", "left_flipper_button", "spare_button", "right_flipper_button",
                                                    "nudge_left_button", "nudge_right_button", "nudge_up_button", "plunger_button",
                                                    "nudge_axis", "yoke_axis", "start_button", "coin_button", "service_button" };
/* fruehere Funktionen in einer alten settings.ini: ihre Eingaenge kommen zu der, die es jetzt tut */
static const struct { const char *key; int to; } BIND_OLD[] = {
    { "left_flipper_button_2", VPIE_F_LEFT_FLIP }, { "right_flipper_button_2", VPIE_F_RIGHT_FLIP }, { "launch_button", VPIE_F_PLUNGER }
};

static int g_held[5], g_jx, g_jy;
static int g_play;                        /* 1 = im Spiel: nur die belegten Eingaenge zaehlen */
static int g_start, g_service, g_coins;   /* in diesem Bild gedrueckt / eingeworfen */
static int g_opt[VPIE_OPT_N] = { 0, 0, 10, 10 };   /* Einstellungen des Automaten (save/settings.ini) */
static const char *const OPT_KEY[VPIE_OPT_N] = { "free_play", "attract_sound", "master_volume", "effects_volume" };
static int opt_is_volume(int n) { return n == VPIE_OPT_MASTER_VOL || n == VPIE_OPT_EFFECTS_VOL; }   /* 0..10, sonst yes/no */
static vp_credits g_cred;

int vpie_func_is_axis(int n) { return n == VPIE_F_NUDGE_AXIS || n == VPIE_F_YOKE_AXIS; }

/* ---- rohe Eingaenge: Tastatur, Joysticks, Maus (SDL) ---- */
#define MAXJOY 8
#define MAXAX  16
static SDL_Joystick *g_joy[MAXJOY];
static int g_njoy;

/* Testhilfe VP_VJOY="bild:a0=v:a1=v:b0=1,bild:...": ein angesteckter Ersatz-Joystick (SDL, 2 Achsen, 4 Knoepfe), dessen
 * Achsen (-32767..32767) und Knoepfe ab dem genannten Bild so stehen - fuer Yoke und freie Belegung ohne Hardware.
 * Er wird nach den echten gezaehlt (am PC mit vier Geraeten: joy4). */
static void vjoy_update(void)
{
    static int dev = -2;
    const char *e = getenv("VP_VJOY");
    char tmp[512];
    if (!e) return;
    if (dev == -2) dev = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN, 2, 4, 0);
    if (dev < 0) return;
    SDL_Joystick *j = SDL_JoystickFromInstanceID(SDL_JoystickGetDeviceInstanceID(dev));
    if (!j) return;
    int ax[2] = { 0, 0 }, bt[4] = { 0, 0, 0, 0 };
    strncpy(tmp, e, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
    for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) {
        if (g_frame < atoi(t)) continue;
        ax[0] = ax[1] = 0; bt[0] = bt[1] = bt[2] = bt[3] = 0;
        for (char *q = strchr(t, ':'); q; q = strchr(q + 1, ':')) {
            int k, v;
            if (sscanf(q + 1, "a%d=%d", &k, &v) == 2 && k >= 0 && k < 2) ax[k] = v;
            if (sscanf(q + 1, "b%d=%d", &k, &v) == 2 && k >= 0 && k < 4) bt[k] = v;
        }
    }
    for (int k = 0; k < 2; k++) SDL_JoystickSetVirtualAxis(j, k, (Sint16)ax[k]);
    for (int k = 0; k < 4; k++) SDL_JoystickSetVirtualButton(j, k, (Uint8)bt[k]);
    SDL_JoystickUpdate();
}

/* Joysticks oeffnen (vp_input hat SDL dafuer schon gestartet); einmal je Bild nachsehen, ob einer dazukam oder ging */
static void joys_refresh(void)
{
    int n = SDL_NumJoysticks();
    if (n > MAXJOY) n = MAXJOY;
    if (n == g_njoy) return;
    for (int i = 0; i < g_njoy; i++) if (g_joy[i]) SDL_JoystickClose(g_joy[i]);
    for (int i = 0; i < n; i++) g_joy[i] = SDL_JoystickOpen(i);
    g_njoy = n;
}

static float joy_axis(int d, int a)
{
    if (d < 0 || d >= g_njoy || !g_joy[d] || a < 0 || a >= SDL_JoystickNumAxes(g_joy[d])) return 0;
    return SDL_JoystickGetAxis(g_joy[d], a) / 32767.0f;
}

/* Eine Belegung zerlegt: kind 0 nichts, 1 Aktion, 2 Taste, 3 Knopf, 4 halbe Achse, 5 ganze Achse, 6 Steuerkreuz, 7 Maus */
typedef struct { int kind, dev, idx, dir; } src_t;

static src_t src_of(const char *b)
{
    src_t s = { 0, 0, 0, 0 };
    char c = 0;
    int n = 0;
    if (!b[0] || !strcmp(b, "none")) return s;
    if (!strncmp(b, "key:", 4)) { s.kind = 2; s.idx = SDL_GetScancodeFromName(b + 4); if (s.idx == SDL_SCANCODE_UNKNOWN) s.kind = 0; return s; }
    if (sscanf(b, "mouse:b%d", &s.idx) == 1) { s.kind = 7; return s; }
    if (sscanf(b, "joy%d:b%d", &s.dev, &s.idx) == 2) { s.kind = 3; return s; }
    if (sscanf(b, "joy%d:h%d:%c", &s.dev, &s.idx, &c) == 3) {
        s.kind = 6;
        s.dir = c == 'u' ? SDL_HAT_UP : c == 'd' ? SDL_HAT_DOWN : c == 'l' ? SDL_HAT_LEFT : SDL_HAT_RIGHT;
        return s;
    }
    if (sscanf(b, "joy%d:a%d%n", &s.dev, &s.idx, &n) == 2) {
        c = b[n];
        if (c == '+' || c == '-') { s.kind = 4; s.dir = c == '+' ? 1 : -1; }
        else { s.kind = 5; s.dir = c == '~' ? -1 : 1; }
        return s;
    }
    s.kind = 1;
    return s;
}

static int src_held_one(const char *b)
{
    const src_t s = src_of(b);
    switch (s.kind) {
    case 2: { const Uint8 *k = SDL_GetKeyboardState(NULL); return k && k[s.idx]; }
    case 3: return s.dev < g_njoy && g_joy[s.dev] && SDL_JoystickGetButton(g_joy[s.dev], s.idx);
    case 4: return joy_axis(s.dev, s.idx) * s.dir > 0.5f;
    case 5: { const float v = joy_axis(s.dev, s.idx); return v > 0.5f || v < -0.5f; }
    case 6: return s.dev < g_njoy && g_joy[s.dev] && (SDL_JoystickGetHat(g_joy[s.dev], s.idx) & s.dir);
    case 7: return (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(s.idx)) != 0;
    default: return 0;
    }
}

/* Jeder Eingang einer Liste ("a, b, c") der Reihe nach in `one`. */
static const char *next_src(const char *p, char *one, size_t size)
{
    while (*p == ' ' || *p == ',') p++;
    if (!*p) return NULL;
    size_t n = strcspn(p, ",");
    while (n && p[n - 1] == ' ') n--;
    snprintf(one, size, "%.*s", (int)(n < size ? n : size - 1), p);
    p += n;
    while (*p == ' ') p++;
    return p;
}

/* irgendeiner der Eingaenge der Liste gedrueckt */
static int src_held(const char *list)
{
    char one[64];
    for (const char *p = list; (p = next_src(p, one, sizeof one)) != NULL; )
        if (src_held_one(one)) return 1;
    return 0;
}

/* ganze Achse, -1..1, mit totem Bereich in der Mitte (ein Yoke steht selten genau auf 0); 0 fuer alles andere. Die erste
 * Achse der Liste. */
static float src_axis(const char *list)
{
    char b[64];
    if (!next_src(list, b, sizeof b)) return 0;
    const src_t s = src_of(b);
    const float dead = 0.10f;
    if (s.kind != 5) return 0;
    float v = joy_axis(s.dev, s.idx) * s.dir;
    if (v > -dead && v < dead) return 0;
    v = (v > 0 ? v - dead : v + dead) / (1.0f - dead);
    return v > 1 ? 1 : (v < -1 ? -1 : v);
}

static int bound(int n) { return src_held(g_bind[n]); }

/* Eine Belegung aus einer Datei: alte advmame-Namen werden zu den Eingaengen, die AdvanceMAME ihnen gibt. */
static void bind_read(char *to, const char *value)
{
    static const char *const OLD[][2] = {
        { "p1_button1", "key:Left Ctrl, joy0:b0, mouse:b1" }, { "p1_button2", "key:Left Alt, joy0:b1, mouse:b3" },
        { "p1_button3", "key:Space, joy0:b2, mouse:b2" }, { "p1_button4", "key:Left Shift, joy0:b3" },
        { "p1_button5", "key:Z, joy0:b4" }, { "p1_button6", "key:X, joy0:b5" },
        { "p1_left", "key:Left, joy0:a0-" }, { "p1_right", "key:Right, joy0:a0+" },
        { "p1_up", "key:Up, joy0:a1-" }, { "p1_down", "key:Down, joy0:a1+" },
        { "start1", "key:1" }, { "start2", "key:2" }, { "coin1", "key:5" }, { "coin2", "key:6" },
    };
    char out[BIND_LEN] = "", one[64];
    size_t o = 0;
    for (const char *p = value; (p = next_src(p, one, sizeof one)) != NULL; ) {
        const char *as = one;
        if (!strcmp(one, "none")) continue;
        for (size_t i = 0; i < sizeof OLD / sizeof OLD[0]; i++) if (!strcmp(one, OLD[i][0])) as = OLD[i][1];
        o += (size_t)snprintf(out + o, o < sizeof out ? sizeof out - o : 0, "%s%s", o ? ", " : "", as);
    }
    snprintf(to, BIND_LEN, "%s", o ? out : "none");
}

/* Einen Eingang zu den Eingaengen der Funktion n: hat sie ihn schon, bleibt es; sind es VPIE_BIND_MAX, geht der aelteste */
static void bind_add(int n, const char *hit)
{
    char one[64], out[BIND_LEN] = "";
    const char *p = g_bind[n];
    int count = 0;
    size_t o = 0;
    if (!strcmp(hit, "none")) return;
    if (!strcmp(g_bind[n], "none")) { snprintf(g_bind[n], sizeof g_bind[n], "%s", hit); return; }
    for (const char *q = g_bind[n]; (q = next_src(q, one, sizeof one)) != NULL; count++)
        if (!strcmp(one, hit)) return;
    if (count >= VPIE_BIND_MAX) p = next_src(p, one, sizeof one);
    while ((p = next_src(p, one, sizeof one)) != NULL)
        o += (size_t)snprintf(out + o, o < sizeof out ? sizeof out - o : 0, "%s%s", o ? ", " : "", one);
    snprintf(out + o, o < sizeof out ? sizeof out - o : 0, "%s%s", o ? ", " : "", hit);
    snprintf(g_bind[n], sizeof g_bind[n], "%s", out);
}

/* ---- Einstellungen (game.ini, save/settings.ini) ---- */
static void settings_read(const char *path)
{
    char line[512], key[64], value[BIND_LEN];
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        /* Wert bis zum Kommentar oder Zeilenende, Leerzeichen erlaubt (Tastennamen wie "Left Ctrl") */
        if (sscanf(line, " %63[A-Za-z_0-9] = %159[^;#\r\n]", key, value) != 2) continue;
        for (size_t k = strlen(value); k > 0 && value[k - 1] == ' '; k--) value[k - 1] = 0;
        for (int n = 1; n <= VPIE_F_N; n++) if (!strcmp(key, BIND_KEY[n])) bind_read(g_bind[n], value);
        for (size_t i = 0; i < sizeof BIND_OLD / sizeof BIND_OLD[0]; i++) {
            char add[BIND_LEN], one[64];
            if (strcmp(key, BIND_OLD[i].key)) continue;
            bind_read(add, value);
            for (const char *p = add; (p = next_src(p, one, sizeof one)) != NULL; ) bind_add(BIND_OLD[i].to, one);
        }
        for (int n = 0; n < VPIE_OPT_N; n++) {
            if (strcmp(key, OPT_KEY[n])) continue;
            if (opt_is_volume(n)) { g_opt[n] = atoi(value); if (g_opt[n] < 0) g_opt[n] = 0; if (g_opt[n] > 10) g_opt[n] = 10; }
            else g_opt[n] = !strcmp(value, "yes") || !strcmp(value, "1");
        }
    }
    fclose(f);
}

static void settings_ini_load(void)
{
    char path[1200];
    settings_read(vp_asset_path("game.ini", path, sizeof path));
    snprintf(path, sizeof path, "%s/settings.ini", vp_save_dir());
    settings_read(path);
    vp_credits_init(&g_cred);
    g_cred.free_play = g_opt[VPIE_OPT_FREE_PLAY];
}

void vpie_settings_save(void)
{
    char text[4096];
    int len = snprintf(text, sizeof text, "; Written by the service menu. What stands here goes before game.ini.\n");
    for (int n = 0; n < VPIE_OPT_N; n++) {
        if (opt_is_volume(n)) len += snprintf(text + len, sizeof text - (size_t)len, "%s = %d\n", OPT_KEY[n], g_opt[n]);
        else len += snprintf(text + len, sizeof text - (size_t)len, "%s = %s\n", OPT_KEY[n], g_opt[n] ? "yes" : "no");
    }
    for (int n = 1; n <= VPIE_F_N; n++) len += snprintf(text + len, sizeof text - (size_t)len, "%s = %s\n", BIND_KEY[n], g_bind[n]);
    plat_write_file("settings.ini", (const uint8_t *)text, (size_t)len);
}

int  vpie_opt(int which) { return which >= 0 && which < VPIE_OPT_N ? g_opt[which] : 0; }
void vpie_opt_set(int which, int on)
{
    if (which < 0 || which >= VPIE_OPT_N) return;
    g_opt[which] = opt_is_volume(which) ? (on < 0 ? 0 : (on > 10 ? 10 : on)) : on != 0;
    g_cred.free_play = g_opt[VPIE_OPT_FREE_PLAY];
}

const char *vpie_binding(int n) { return n >= 1 && n <= VPIE_F_N ? g_bind[n] : ""; }
void vpie_binding_clear(int n) { if (n >= 1 && n <= VPIE_F_N) snprintf(g_bind[n], sizeof g_bind[n], "none"); }
void vpie_binding_standard(int n) { if (n >= 1 && n <= VPIE_F_N) snprintf(g_bind[n], sizeof g_bind[n], "%s", BIND_STANDARD[n]); }
void vpie_binding_toggle(int n)
{
    if (n < 1 || n > VPIE_F_N) return;
    if (strcmp(g_bind[n], "none")) vpie_binding_clear(n);
    else if (strcmp(BIND_STANDARD[n], "none")) vpie_binding_standard(n);
}
void vpie_play_mode(int on) { g_play = on; }

/* Lesbarer Name einer Belegung fuer den Service: "LEFT CTRL + JOY1 BUTTON 1 + MOUSE LEFT", "JOY1 AXIS 2 INV" ... */
const char *vpie_binding_name(int n, char *buf, size_t size)
{
    static const char *const HAT[9] = { "", "UP", "RIGHT", "", "DOWN", "", "", "", "LEFT" };
    static const char *const MOUSE[6] = { "", "LEFT", "MIDDLE", "RIGHT", "X1", "X2" };
    const char *list = vpie_binding(n);
    char one[64], word[64];
    size_t o = 0;
    buf[0] = 0;
    for (const char *p = list; (p = next_src(p, one, sizeof one)) != NULL; ) {
        const src_t s = src_of(one);
        switch (s.kind) {
        case 0: continue;
        case 2: snprintf(word, sizeof word, "%s", one + 4); break;
        case 3: snprintf(word, sizeof word, "JOY%d BUTTON %d", s.dev + 1, s.idx + 1); break;
        case 4: snprintf(word, sizeof word, "JOY%d AXIS %d %c", s.dev + 1, s.idx + 1, s.dir > 0 ? '+' : '-'); break;
        case 5: snprintf(word, sizeof word, "JOY%d AXIS %d%s", s.dev + 1, s.idx + 1, s.dir < 0 ? " INV" : ""); break;
        case 6: snprintf(word, sizeof word, "JOY%d HAT %s", s.dev + 1, HAT[s.dir & 15]); break;
        case 7: snprintf(word, sizeof word, "MOUSE %s", s.idx >= 1 && s.idx <= 5 ? MOUSE[s.idx] : "?"); break;
        default: snprintf(word, sizeof word, "%s", one); break;
        }
        o += (size_t)snprintf(buf + o, o < size ? size - o : 0, "%s%s", o ? " + " : "", word);
        if (o >= size) break;
    }
    if (!buf[0]) snprintf(buf, size, "NONE");
    for (char *p = buf; *p; p++) *p = *p == '_' ? ' ' : (char)toupper((unsigned char)*p);
    return buf;
}

/* ---- Belegen ----
 * Die Funktion n bekommt den ersten Eingang, der bewegt wird, nachdem alles losgelassen ist. Die Achsen werden beim Losgehen
 * in ihrer Ruhelage gemerkt (Hebel, die in Ruhe am Anschlag stehen, zaehlen so erst, wenn sie bewegt werden). Fuer die
 * analogen Funktionen zaehlt nur eine Achse, und die Richtung, in die man sie bewegt, legt fest, wie herum sie wirkt:
 * NUDGE AXIS nach RECHTS, YOKE AXIS ZURUECKZIEHEN (ziehen = Stoessel, druecken = Stoss nach oben). Wie in allen
 * Vektorspielen des Automaten: der erste Eingang, seit der Cursor auf der Zeile steht, ersetzt, jeder weitere kommt dazu
 * (bis VPIE_BIND_MAX; eine Achse ersetzt immer); andere Funktionen behalten ihre Eingaenge. Enter (und START 1, ausser bei
 * START, COIN, SERVICE) laesst es, wie es ist; START 2 schaltet ab (none), Entf gibt die Vorgabe. Nie belegt werden Tab, F2,
 * Esc, Enter, Entf, Ruecktaste und, ausser bei START/COIN/SERVICE, die START- und COIN-Tasten (1, 2, 5, 6). */
static int g_cap_armed, g_cap_frames, g_cap_replace;
static Uint8 g_cap_keys[SDL_NUM_SCANCODES];
static Uint8 g_cap_btn[MAXJOY][32], g_cap_hat[MAXJOY][4];
static float g_cap_rest[MAXJOY][MAXAX];
static Uint32 g_cap_mouse;

static int raw_any_held(void)
{
    int nk = 0;
    const Uint8 *k = SDL_GetKeyboardState(&nk);
    for (int i = 0; k && i < nk; i++) if (k[i]) return 1;
    for (int d = 0; d < g_njoy; d++) {
        if (!g_joy[d]) continue;
        for (int b = 0; b < SDL_JoystickNumButtons(g_joy[d]); b++) if (SDL_JoystickGetButton(g_joy[d], b)) return 1;
        for (int h = 0; h < SDL_JoystickNumHats(g_joy[d]); h++) if (SDL_JoystickGetHat(g_joy[d], h)) return 1;
    }
    return SDL_GetMouseState(NULL, NULL) != 0;
}

/* Zustand merken, gegen den "neu gedrueckt" und "bewegt" gemessen wird */
static void cap_snapshot(int with_rest)
{
    int nk = 0;
    const Uint8 *k = SDL_GetKeyboardState(&nk);
    memset(g_cap_keys, 0, sizeof g_cap_keys);
    if (k) memcpy(g_cap_keys, k, (size_t)(nk < SDL_NUM_SCANCODES ? nk : SDL_NUM_SCANCODES));
    for (int d = 0; d < g_njoy && d < MAXJOY; d++) {
        if (!g_joy[d]) continue;
        for (int b = 0; b < 32 && b < SDL_JoystickNumButtons(g_joy[d]); b++) g_cap_btn[d][b] = SDL_JoystickGetButton(g_joy[d], b);
        for (int h = 0; h < 4 && h < SDL_JoystickNumHats(g_joy[d]); h++) g_cap_hat[d][h] = SDL_JoystickGetHat(g_joy[d], h);
        if (with_rest) for (int a = 0; a < MAXAX; a++) g_cap_rest[d][a] = joy_axis(d, a);
    }
    g_cap_mouse = SDL_GetMouseState(NULL, NULL);
}

void vpie_capture_begin(int replace) { g_cap_armed = 0; g_cap_frames = 0; g_cap_replace = replace; }

static int cabinet_func(int n) { return n == VPIE_F_START || n == VPIE_F_COIN || n == VPIE_F_SERVICE; }

static int kept_key(int n, int sc)
{
    if (sc == SDL_SCANCODE_TAB || sc == SDL_SCANCODE_F2 || sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_RETURN
        || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_KP_PERIOD || sc == SDL_SCANCODE_BACKSPACE)
        return 1;
    return !cabinet_func(n) && (sc == SDL_SCANCODE_1 || sc == SDL_SCANCODE_2 || sc == SDL_SCANCODE_5 || sc == SDL_SCANCODE_6);
}

int vpie_controls_idle(void)
{
    if (getenv("VP_KEYS")) return !(g_held[1] || g_held[2] || g_held[3] || g_held[4]);
    return !raw_any_held() && !vp_input_held(g_in, "start1") && !vp_input_held(g_in, "start2");
}

int vpie_capture(int n)
{
    char hit[40] = "";
    const char *e = getenv("VP_CAP");                                /* Testhilfe "bild:eingang" */
    if (n < 1 || n > VPIE_F_N) return -1;
    if (e) {
        if (atoi(e) != g_frame) return 0;
        snprintf(hit, sizeof hit, "%.39s", strchr(e, ':') ? strchr(e, ':') + 1 : "none");
    } else {
        if (!g_cap_armed) {
            if (!vpie_controls_idle()) return 0;
            g_cap_armed = 1;
            cap_snapshot(1);
            return 0;
        }
        g_cap_frames++;
        if (vpb_key_pressed(SDL_SCANCODE_RETURN) || vpb_key_pressed(SDL_SCANCODE_KP_ENTER) || vpb_key_pressed(SDL_SCANCODE_ESCAPE)
            || (!cabinet_func(n) && vp_input_pressed(g_in, "start1"))) return -1;
        if (!cabinet_func(n) && vp_input_pressed(g_in, "start2")) { vpie_binding_clear(n); return 1; }
        if (vpb_key_pressed(SDL_SCANCODE_DELETE) || vpb_key_pressed(SDL_SCANCODE_KP_PERIOD)) { vpie_binding_standard(n); return 1; }
        for (int d = 0; d < g_njoy && d < MAXJOY && !hit[0]; d++) {
            if (!g_joy[d]) continue;
            const int na = SDL_JoystickNumAxes(g_joy[d]);
            for (int a = 0; a < na && a < MAXAX && !hit[0]; a++) {
                const float m = joy_axis(d, a) - g_cap_rest[d][a];
                if (m < 0.6f && m > -0.6f) continue;
                if (n == VPIE_F_NUDGE_AXIS) snprintf(hit, sizeof hit, "joy%d:a%d%s", d, a, m > 0 ? "" : "~");          /* nach rechts */
                else if (n == VPIE_F_YOKE_AXIS) snprintf(hit, sizeof hit, "joy%d:a%d%s", d, a, m > 0 ? "~" : "");      /* zurueckgezogen */
                else snprintf(hit, sizeof hit, "joy%d:a%d%c", d, a, joy_axis(d, a) > 0 ? '+' : '-');
            }
            if (vpie_func_is_axis(n)) continue;                       /* analoge Funktionen: nur Achsen */
            for (int b = 0; b < 32 && b < SDL_JoystickNumButtons(g_joy[d]) && !hit[0]; b++)
                if (SDL_JoystickGetButton(g_joy[d], b) && !g_cap_btn[d][b]) snprintf(hit, sizeof hit, "joy%d:b%d", d, b);
            for (int h = 0; h < 4 && h < SDL_JoystickNumHats(g_joy[d]) && !hit[0]; h++) {
                const int v = SDL_JoystickGetHat(g_joy[d], h) & ~g_cap_hat[d][h];
                if (v & SDL_HAT_UP) snprintf(hit, sizeof hit, "joy%d:h%d:u", d, h);
                else if (v & SDL_HAT_DOWN) snprintf(hit, sizeof hit, "joy%d:h%d:d", d, h);
                else if (v & SDL_HAT_LEFT) snprintf(hit, sizeof hit, "joy%d:h%d:l", d, h);
                else if (v & SDL_HAT_RIGHT) snprintf(hit, sizeof hit, "joy%d:h%d:r", d, h);
            }
        }
        if (!hit[0] && !vpie_func_is_axis(n)) {
            int nk = 0;
            const Uint8 *k = SDL_GetKeyboardState(&nk);
            for (int i = 0; k && i < nk && i < SDL_NUM_SCANCODES && !hit[0]; i++) {
                if (!k[i] || g_cap_keys[i] || kept_key(n, i)) continue;
                const char *name = SDL_GetScancodeName((SDL_Scancode)i);
                if (name && name[0]) snprintf(hit, sizeof hit, "key:%.34s", name);
            }
            const Uint32 ms = SDL_GetMouseState(NULL, NULL) & ~g_cap_mouse;
            for (int b = 1; b <= 5 && !hit[0]; b++) if (ms & SDL_BUTTON(b)) snprintf(hit, sizeof hit, "mouse:b%d", b);
        }
        cap_snapshot(0);                                              /* "neu gedrueckt" gilt gegenueber dem letzten Bild */
    }
    if (!hit[0]) return 0;
    if (g_cap_replace || vpie_func_is_axis(n) || !strcmp(g_bind[n], "none")) {
        snprintf(g_bind[n], sizeof g_bind[n], "%s", hit);
        return 1;
    }
    bind_add(n, hit);                                                 /* dazu */
    return 1;
}

/* Testhilfe VP_KEYS="bild:maske[:x:y],...": Bit 0-3 Knopf 1-4, Bit 4 Muenze, Bit 5 START, Bit 6 Service; dazu der Stick */
static int scripted_input(void)
{
    static int was;
    const char *sc = getenv("VP_KEYS");
    char tmp[512];
    int mask = 0, jx = 0, jy = 0;
    if (!sc) return 0;
    strncpy(tmp, sc, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
    for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) {
        int f = 0, m = 0, x = 0, y = 0;
        int n = sscanf(t, "%d:%d:%d:%d", &f, &m, &x, &y);
        if (n >= 2 && g_frame >= f) { mask = m; jx = n >= 3 ? x : 0; jy = n >= 4 ? y : 0; }
    }
    for (int i = 1; i <= 4; i++) g_held[i] = (mask >> (i - 1)) & 1;
    g_jx = jx; g_jy = jy;
    g_coins = (mask & 16) && !(was & 16);
    if (g_coins && g_cred.credits < 99) g_cred.credits++;
    g_start = (mask & 32) && !(was & 32);
    if ((mask & 32) && !g_play) g_held[3] = 1;
    g_service = (mask & 64) && !(was & 64);
    was = mask;
    return 1;
}

static void read_controls(void)
{
    vp_input *in = g_in;
    static int was_start, was_coin, was_service;
    joys_refresh();
    vjoy_update();
    vpb_update();                                                    /* die festen Eingaenge der Menues (vpbind.h) */
    if (scripted_input()) return;
    {   /* START, COIN, SERVICE: die festen Eingaenge aus advmame.rc (und Tab), dazu was im Service belegt ist */
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        const int start = bound(VPIE_F_START), coin = bound(VPIE_F_COIN);
        /* Service: F2 (wie an den anderen Automaten), Tab, ui_configure und was belegt ist */
        const int service = (keys && (keys[SDL_SCANCODE_F2] || keys[SDL_SCANCODE_TAB])) || vp_input_held(in, "ui_configure") || bound(VPIE_F_SERVICE);
        g_coins = vp_credits_update(&g_cred, in);
        if (coin && !was_coin && g_cred.credits < g_cred.max) { g_cred.credits++; g_coins++; }
        g_start = vp_input_pressed(in, "start1") || vp_input_pressed(in, "start2") || (start && !was_start)
               || (!g_play && (vpb_key_pressed(SDL_SCANCODE_RETURN) || vpb_key_pressed(SDL_SCANCODE_KP_ENTER)));
        g_service = service && !was_service;
        was_start = start; was_coin = coin; was_service = service;
    }
    if (!g_play) {
        /* ausserhalb des Spiels (Menues, Service, Initialen): nur die festen Eingaenge, was auch immer belegt ist - die
         * Pfeile, jeder Stick oder Hat; START 1, Enter (OK) */
        const int wx = vpb_menu_way_x(), wy = vpb_menu_way_y();
        /* ... und die Flipper: Spielerzahl, Tisch und Initialen waehlt man mit ihnen (Olli, 2026-10-19) */
        g_held[1] = bound(VPIE_F_LEFT_FLIP);
        g_held[4] = bound(VPIE_F_RIGHT_FLIP);
        g_held[2] = 0;
        g_held[3] = vpb_menu_confirm_held(in) || bound(VPIE_F_START);
        g_jx = wx * 127;
        g_jy = -wy * 127;
        return;
    }
    g_held[1] = bound(VPIE_F_LEFT_FLIP);
    g_held[4] = bound(VPIE_F_RIGHT_FLIP);
    /* kein Abschussknopf: der Ball geht mit dem Stoessel (PLUNGER, YOKE AXIS); START wechselt die Ansicht (play.c) */
    g_held[3] = 0;
    g_held[2] = bound(VPIE_F_SPARE) || vp_input_held(in, "start2");
    {
        int l = bound(VPIE_F_NUDGE_L), r = bound(VPIE_F_NUDGE_R), u = bound(VPIE_F_NUDGE_UP), d = bound(VPIE_F_PLUNGER);
        const int dx = (r ? 127 : 0) - (l ? 127 : 0), dy = (u ? 127 : 0) - (d ? 127 : 0);
        /* analog nur die belegten Achsen (Yoke: X stoesst seitlich, Y ziehen = Stoessel, druecken = hoch) - nicht p1_stickx/y
         * aus advmame.rc: die bewegt auch ein digitaler Stick, und liegt darauf ein Flipper, stiesse jedes Flippen */
        const int ax = (int)(src_axis(g_bind[VPIE_F_NUDGE_AXIS]) * 127.0f), ay = (int)(src_axis(g_bind[VPIE_F_YOKE_AXIS]) * 127.0f);
        g_jx = abs(ax) > abs(dx) ? ax : dx;
        g_jy = abs(ay) > abs(dy) ? ay : dy;
    }
}

int vpie_stick_x(void) { return g_jx; }
int vpie_stick_y(void) { return g_jy; }
int vpie_button(int n) { return n >= 1 && n <= 4 && g_held[n]; }

/* ---- Muenzen ---- */
int vpie_coins(void) { return g_coins; }
int vpie_credits(void) { return g_cred.credits; }
int vpie_start(void) { return g_start; }
int vpie_service(void) { return g_service; }
int vpie_can_start(int players) { return vp_credits_enough(&g_cred, players); }
int vpie_take_credits(int players) { return vp_credits_take(&g_cred, players); }
/* Lampen der START-Knoepfe: wait = ein Spiel kann gestartet werden (blinken, wenn Credits da sind), sonst aus */
void vpie_lamps(int wait)
{
    if (wait) vp_credits_blink(&g_cred, (double)vp_ticks_ms() / 1000.0);
    else vp_lamps_set(0);
}

/* ---- Bild ---- */
int vpie_vertical(void) { return g_vertical; }
int vpie_on_board(void) { return g_dvg && vp_dvg_get_output(g_dvg) == VP_DVG_OUTPUT_BOARD; }

void vpie_line(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    const int ax = 2048 + (int)lroundf(x0 * g_kx), ay = 2048 + (int)lroundf(y0 * g_ky);
    const int bx = 2048 + (int)lroundf(x1 * g_kx), by = 2048 + (int)lroundf(y1 * g_ky);
    if (!g_out) return;
    vp_color(g_out, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
    if (ax != g_bx || ay != g_by) vp_moveto(g_out, ax, ay);
    vp_lineto(g_out, bx, by);
    g_bx = bx; g_by = by;
    if (g_shot) fprintf(g_shot, "%d %d %d %d %06x\n", ax, ay, bx, by, (unsigned)rgb);
}

void vpie_stats(unsigned *lines, unsigned *lit, unsigned *blank, unsigned *dropped)
{
    vp_dvg_stats s;
    memset(&s, 0, sizeof s);
    if (g_dvg) vp_dvg_get_stats(g_dvg, &s);
    *lines = s.vectors; *lit = s.lit_length; *blank = s.blank_length; *dropped = s.dropped;
}


static int shot_wanted(void)
{
    const char *at = getenv("VP_SHOT_AT");
    char tmp[256];
    if (!at) return 0;
    strncpy(tmp, at, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
    for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) if (atoi(t) == g_frame) return 1;
    return 0;
}

/* Ein Bild: Steuerung lesen, das Spiel zeichnen lassen, senden. 0 = das Bild ging nicht hinaus */
static int one_frame(void)
{
    read_controls();
    vp_dvg_begin_frame(g_dvg);
    g_out = g_dvg; g_bx = g_by = -1;
    if (shot_wanted()) {
        char nm[64];
        snprintf(nm, sizeof nm, "shot_%d.txt", g_frame);
        g_shot = fopen(nm, "w");
        if (g_shot) fprintf(g_shot, "# %s\n", g_vertical ? "3:4" : "4:3");
    }
    g_loop();
    if (g_shot) { fclose(g_shot); g_shot = NULL; }
    g_out = NULL;
    g_frame++;
    return vp_dvg_end_frame(g_dvg, 0) == VP_OK;
}

/* ---- Ton ---- */
static plat_render_t s_render;
static SDL_AudioDeviceID s_adev;

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    static int16_t l[2048], r[2048];
    int16_t *out = (int16_t *)stream;
    int n = len / 4;
    (void)ud;
    const int master = g_opt[VPIE_OPT_MASTER_VOL] * g_opt[VPIE_OPT_MASTER_VOL] * 256 / 100;   /* quadratisch: 5 klingt halb so laut */
    while (n > 0) {
        int c = n > 2048 ? 2048 : n;
        s_render(l, r, c);
        for (int i = 0; i < c; i++) { *out++ = (int16_t)(l[i] * master >> 8); *out++ = (int16_t)(r[i] * master >> 8); }
        n -= c;
    }
}

/* ---- Lautstaerken: MASTER, EFFECTS, MUSIC ----
 * Der Mischer (vectrex/game/pcm.c) und der Ton (sound.c) werden unveraendert mit der Vectrex-Fassung geteilt und kennen
 * nur die Lautstaerke eigener Lieder. Darum laufen drei Aufrufe von sound.c ueber die Huellen hier (CMakeLists.txt benennt
 * sie beim Uebersetzen von sound.c um): Effekte und der nachgebildete Klangchip gehen mit EFFECTS, die Musik des Originals
 * (Schlagzeug, Flaeche) mit MUSIC wie die eigenen Lieder (g_set.music_vol). MASTER wirkt ganz am Ende auf alles (audio_cb).
 * Alle drei 0..10, quadratisch auf den Pegel. */
static int vol_gain(int v) { return v * v * 256 / 100; }
void vpie_pcm_play(int idx, int vol, uint32_t pitch, int pan) { pcm_play(idx, vol * vol_gain(g_opt[VPIE_OPT_EFFECTS_VOL]) >> 8, pitch, pan); }
void vpie_pcm_music_start(int idx, int vol, int loop) { pcm_music_start(idx, vol * vol_gain(g_set.music_vol > 10 ? 10 : g_set.music_vol) >> 8, loop); }
void vpie_aysynth_render(int16_t *out, int n)
{
    const int g = vol_gain(g_opt[VPIE_OPT_EFFECTS_VOL]);
    aysynth_render(out, n);
    for (int i = 0; i < n; i++) out[i] = (int16_t)(out[i] * g >> 8);
}

int plat_audio_has_chip(void) { return 0; }

int plat_audio_start(plat_render_t render, int rate)
{
    SDL_AudioSpec want;
    if (getenv("VP_MUTE")) return 1;                                 /* stumm, aber der Mischer laeuft */
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "pinball: no sound: %s\n", SDL_GetError());
        return 0;
    }
    SDL_zero(want);
    want.freq = rate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512; want.callback = audio_cb;
    s_render = render;
    s_adev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);           /* SDL wandelt, falls das Geraet die Rate nicht kann */
    if (!s_adev) { fprintf(stderr, "pinball: no sound: %s\n", SDL_GetError()); return 0; }
    SDL_PauseAudioDevice(s_adev, 0);
    return 1;
}
void plat_audio_pump(plat_render_t render) { (void)render; }

/* ---- Dateien ---- */
#define ARENA_SIZE (8u * 1024u * 1024u)   /* wie im Vectrex-Simulator: pcm.c rechnet mit dem PSRAM des UVMC2 */
static uint8_t *g_arena;
static FILE *g_stream[2];

static const char *data_path(const char *name, char *buf, size_t n)
{
    char rel[400];
    snprintf(rel, sizeof rel, "data/%s", name);
    return vp_asset_path(rel, buf, n);
}
static const char *save_path(const char *name, char *buf, size_t n) { snprintf(buf, n, "%s/%s", vp_save_dir(), name); return buf; }

const uint8_t *plat_arena(uint32_t *size)
{
    if (!g_arena) g_arena = malloc(ARENA_SIZE);
    *size = ARENA_SIZE;
    return g_arena;
}

int plat_stream_open(int h, const char *name, uint32_t *size)
{
    char path[1200];
    if (g_stream[h]) { fclose(g_stream[h]); g_stream[h] = NULL; }
    g_stream[h] = fopen(data_path(name, path, sizeof path), "rb");
    if (!g_stream[h]) return 0;
    fseek(g_stream[h], 0, SEEK_END);
    *size = (uint32_t)ftell(g_stream[h]);
    fseek(g_stream[h], 0, SEEK_SET);
    return 1;
}

uint32_t plat_stream_read(int h, uint32_t offset, uint32_t max, unsigned budget_us)
{
    (void)budget_us;
    if (!g_stream[h] || !g_arena || offset + max > ARENA_SIZE) return 0;
    if (h == 1 && max > 65536u) max = 65536u;                         /* das Lied kommt in Stuecken, wie auf der Konsole */
    return (uint32_t)fread(g_arena + offset, 1, max, g_stream[h]);
}

void plat_arena_get(uint32_t offset, uint8_t *dst, uint32_t n) { if (g_arena && offset + n <= ARENA_SIZE) memcpy(dst, g_arena + offset, n); }
void plat_arena_put(uint32_t offset, const uint8_t *src, uint32_t n) { if (g_arena && offset + n <= ARENA_SIZE) memcpy(g_arena + offset, src, n); }

int plat_find_file(const char *dir, const char *prefix, const char *ext, char *out, size_t n)
{
    char path[1200];
    const size_t pl = strlen(prefix);
    DIR *d = opendir(data_path(dir, path, sizeof path));
    struct dirent *e;
    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {
        const size_t l = strlen(e->d_name);
        if (l < pl + 4 || strncmp(e->d_name, prefix, pl)) continue;
        const char *x = e->d_name + l - 4;
        if (x[0] != '.' || (x[1] | 32) != ext[1] || (x[2] | 32) != ext[2] || (x[3] | 32) != ext[3]) continue;
        snprintf(out, n, "%s/%s", dir, e->d_name);
        closedir(d);
        return 1;
    }
    closedir(d);
    return 0;
}

size_t plat_read_file(const char *name, uint8_t *buf, size_t max)
{
    char path[1200];
    FILE *f = fopen(save_path(name, path, sizeof path), "rb");
    size_t n;
    if (!f) f = fopen(data_path(name, path, sizeof path), "rb");
    if (!f) return 0;
    n = fread(buf, 1, max, f);
    fclose(f);
    return n;
}

/* erst daneben schreiben, dann umbenennen: ein Automat, der beim Speichern ausgeht, behaelt die alte Datei */
int plat_write_file(const char *name, const uint8_t *data, size_t n)
{
    char path[1200], tmp[1210];
    FILE *f;
    size_t w;
    if (getenv("VP_NOSAVE")) return 1;
    save_path(name, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) return 0;
    w = fwrite(data, 1, n, f);
    if (fclose(f) != 0 || w != n) { remove(tmp); return 0; }
#ifdef _WIN32
    remove(path);                                                    /* rename ersetzt unter Windows keine vorhandene Datei */
#endif
    return rename(tmp, path) == 0;
}

int vpie_file_exists(const char *name)
{
    char path[1200];
    FILE *f = fopen(save_path(name, path, sizeof path), "rb");
    if (f) fclose(f);
    return f != NULL;
}

void vpie_export_scores(const char (*names)[4], const uint32_t *scores, int n)
{
    char text[VP_HISCORE_MAX * 24];
    int len = 0;
    if (n > VP_HISCORE_MAX) n = VP_HISCORE_MAX;
    for (int i = 0; i < n; i++) {
        char nm[4];
        int k = 0;
        for (int c = 0; c < 3 && names[i][c]; c++) if (names[i][c] != ' ') nm[k++] = names[i][c];
        nm[k] = 0;
        len += snprintf(text + len, sizeof text - (size_t)len, "%s %lu\n", k ? nm : "---",
                        (unsigned long)(scores[i] > 0x7FFFFFFFu ? 0x7FFFFFFFu : scores[i]));
    }
    plat_write_file("highscores.txt", (const uint8_t *)text, (size_t)len);
}

/* ---- Rest von plat.h ---- */
void plat_log(const char *msg) { fputs(msg, stderr); fputc('\n', stderr); }
uint32_t plat_clock_us(void) { return (uint32_t)(SDL_GetPerformanceCounter() * 1000000ull / SDL_GetPerformanceFrequency()); }
uint32_t plat_micros(void)
{
    if (getenv("VP_FIXED_DT")) return (uint32_t)g_frame * 16667u;      /* Testlaeufe: feste Bildzeit, wiederholbar */
    return plat_clock_us();
}
int plat_test_int(const char *name, int def)
{
    char key[64];
    const char *e;
    snprintf(key, sizeof key, "VP_%s", name);
    e = getenv(key);
    return e ? atoi(e) : def;
}
/* gibt es nur auf der Vectrex: Strahlwerte der Konsole, Reset-Knopf, Wiederholbild */
void plat_bus_stats(unsigned *bus_cycles, unsigned *dropped, unsigned *overrun) { *bus_cycles = 0; *dropped = 0; *overrun = 0; }
void plat_beam_init(int zero_every, int zero_jump) { (void)zero_every; (void)zero_jump; }
int  plat_beam_get(int which) { (void)which; return 0; }
void plat_beam_set(int which, int value) { (void)which; (void)value; }
void plat_beam_save(void) {}
int  plat_reset_ms(void) { return 0; }
void plat_redraw_last(int on) { (void)on; }

/* ---- Schleife ---- */
static volatile sig_atomic_t g_signal;
static void on_signal(int sig) { (void)sig; g_signal = 1; }

/* Ausgang wie vp_run: die Schalter der Befehlszeile gehen vor, sonst entscheidet vp_dvg_auto_options. 0 = unbekannter Schalter */
static int choose_output(int argc, char **argv, const char *title, vp_dvg_options *o, const char **reason)
{
    int forced = 0, fullscreen = 0, vertical = 0;
    const char *port = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dvg")) { forced = VP_DVG_OUTPUT_BOARD; if (i + 1 < argc && argv[i + 1][0] != '-') port = argv[++i]; }
        else if (!strcmp(argv[i], "--window")) { forced = VP_DVG_OUTPUT_WINDOW; fullscreen = 1; }
        else if (!strcmp(argv[i], "--windowed")) { forced = VP_DVG_OUTPUT_WINDOW; fullscreen = 0; }
        else if (!strcmp(argv[i], "--vertical")) vertical = 1;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: %s [--dvg [PORT] | --window | --windowed] [--vertical]\n", argc > 0 ? argv[0] : "pinball");
            return 0;
        }
    }
    if (forced) {
        memset(o, 0, sizeof *o);
        o->output = (vp_dvg_output)forced; o->port = port; o->fullscreen = fullscreen; o->window_title = title;
        *reason = "command line";
    } else vp_dvg_auto_options(o, title, reason);
    if (vertical) o->vertical = 1;
    return 1;
}

/* VP_STATS=1: einmal je Sekunde, was ein Bild kostet (Zaehler des SDK, Mittel und Spitze) */
static void stats_report(const char *title)
{
    static uint64_t since;
    static unsigned n, drops;
    static double sum[4], peak[4];
    vp_dvg_stats st;
    const uint64_t now = vp_ticks_ms();
    vp_dvg_get_stats(g_dvg, &st);
    const double v[4] = { st.vectors, st.blank_moves, st.lit_length, st.blank_length };
    for (int i = 0; i < 4; i++) { sum[i] += v[i]; if (v[i] > peak[i]) peak[i] = v[i]; }
    n++; if (st.dropped) drops++;
    if (!since) since = now;
    if (now - since < 1000) return;
    printf("%s stats: %.1f fps | avg/peak: vectors %.0f/%.0f, moves %.0f/%.0f, lit length %.0f/%.0f, blank length %.0f/%.0f | "
           "frames with dropped lines %u\n", title, n * 1000.0 / (double)(now - since), sum[0] / n, peak[0], sum[1] / n, peak[1],
           sum[2] / n, peak[2], sum[3] / n, peak[3], drops);
    fflush(stdout);
    since = now; n = drops = 0;
    memset(sum, 0, sizeof sum); memset(peak, 0, sizeof peak);
}

int vpie_run(void (*setup)(void), void (*loop)(void), int argc, char **argv)
{
    static const char title[] = "Vector Pinball";
    vp_dvg_options o;
    const char *reason = "", *e;
    char path[1200];
    int status = 0, hz, stats;
    uint64_t next;

    g_setup = setup; g_loop = loop;
    /* Das Menue des Automaten startet ein Spiel als <name>_dvg oder <name>_sdl (Verweise auf das Programm) und sagt damit,
     * wohin gezeichnet wird; neuere Menues setzen stattdessen VP_OUTPUT. */
    if (!getenv("VP_OUTPUT") && argc > 0) {
        const size_t n = strlen(argv[0]);
        const char *out = n >= 4 && !strcmp(argv[0] + n - 4, "_sdl") ? "sdl" : (n >= 4 && !strcmp(argv[0] + n - 4, "_dvg") ? "dvg" : NULL);
        if (out) SDL_setenv("VP_OUTPUT", out, 1);
    }
    if (!choose_output(argc, argv, title, &o, &reason)) return 1;
    if ((e = getenv("VP_FRAMES")) != NULL) g_frames_max = atoi(e);
    if (vp_dvg_open_ex(&o, &g_dvg) != VP_OK) { fprintf(stderr, "%s: cannot open the output: %s\n", title, vp_last_error()); return 1; }
    if (vp_input_open(&g_in) != VP_OK) {
        fprintf(stderr, "%s: cannot open the controls: %s\n", title, vp_last_error());
        vp_dvg_close(g_dvg);
        return 1;
    }
    g_vertical = o.vertical;
    g_kx = 2047.0f / ((g_vertical ? 141.75f : 252.0f) * 16.0f);
    g_ky = 2047.0f / (SCR_HALF_H * 16.0f);
    /* Bilder je Sekunde: Fenster 60; Platine 40 (Vorgabe des SDK - ein Bild, das die Platine nicht schafft, zeichnet sie
     * groeber und dunkler). Auf dem Automaten mit VP_HZ ausprobieren. */
    hz = vpie_on_board() ? 40 : 60;
    if ((e = getenv("VP_HZ")) != NULL && atoi(e) > 0) hz = atoi(e);
    stats = (e = getenv("VP_STATS")) != NULL && *e && strcmp(e, "0");
    /* auf der Platine bliebe der HDMI-Schirm des Automaten schwarz: dort das Marquee des Pakets, wenn es eins gibt */
    if (vpie_on_board()) {
        FILE *f = fopen(vp_asset_path("marquee.png", path, sizeof path), "rb");
        if (f) { fclose(f); if (vp_marquee_show(path) != VP_OK) fprintf(stderr, "%s: marquee: %s\n", title, vp_last_error()); }
    }
    printf("%s: VectorPie SDK %s, %s%s%s (%s), %d joystick(s), %d frames/s\n", title, vp_version(), vp_hardware_name(vp_dvg_hardware(g_dvg)),
           vpie_on_board() ? " on " : "", vp_dvg_port(g_dvg), reason, vp_input_joystick_count(g_in), hz);
    fflush(stdout);
    signal(SIGINT, on_signal);                                       /* nach SDL, damit diese gelten */
    signal(SIGTERM, on_signal);

    settings_ini_load();
    g_setup();
    next = vp_ticks_ms();
    while (!g_signal) {
        uint64_t now;
        /* die Schleife besitzt SDLs Ereignisliste: nur das geschlossene Fenster zaehlt, die Steuerung liest vp_input */
        SDL_PumpEvents();
        if (SDL_HasEvent(SDL_QUIT)) break;
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        vp_input_update(g_in);
        if (vp_input_pressed(g_in, "ui_cancel")) break;              /* zurueck ins Menue des Automaten */
        if (!one_frame()) { fprintf(stderr, "%s: frame not sent: %s\n", title, vp_last_error()); status = 1; break; }
        if (stats) stats_report(title);
        if (g_frames_max >= 0 && g_frame >= g_frames_max) break;
        next += (uint64_t)(1000 / hz);
        now = vp_ticks_ms();
        if (next > now) vp_sleep_ms((int)(next - now));
        else next = now;                                             /* zu spaet: nichts aufholen */
    }

    if (s_adev) { SDL_CloseAudioDevice(s_adev); s_adev = 0; }
    vp_lamps_close();                                                /* die START-Lampen wie vorher */
    vp_marquee_clear();
    vp_input_close(g_in);
    vp_dvg_close(g_dvg);                                             /* die Platine geht auf ihren Startschirm zurueck */
    g_dvg = NULL;
    return status;
}
