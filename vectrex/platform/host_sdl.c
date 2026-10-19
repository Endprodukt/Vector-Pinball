/*
 * host_sdl.c - Host-Simulator fuer Windows: implementiert den libvpy-/PiTrex-Vertrag
 * (vectrexInterface.h) mit einem SDL2-Fenster, das einen Vektorbildschirm nachahmt.
 *
 * Es ist ein Entwicklungswerkzeug, kein Ersatz fuer die echte Vectrex: Es zeigt Geometrie,
 * Helligkeitsstufen, Strichzahl und einen grob geschaetzten Strahlzeit-Bedarf. Flackern,
 * Ueberstrahlung und Strahlverzug entscheidet die Hardware.
 *
 * Das Fenster ist hochkant (3:4) wie die Roehre.
 *   Tasten: Pfeiltasten (oder WASD) = Stick: im Menue waehlen/aendern, im Spiel gegen den Tisch stossen;
 *           linke Umschalt-/Strg-Taste, Y/Z, 1 = Knopf 1 (linker Flipper, zurueck),
 *           rechte Umschalt-/Strg-Taste, Minus, 4, Eingabe = Knopf 4 (rechter Flipper, OK),
 *           Leertaste / C / 3 = Knopf 3 (Ball starten, OK), P / X / 2 = Knopf 2 (Pause), Esc = Ende,
 *           F1 = Statistik, F2 = Bildschirmfoto (shot.bmp), F3 = Nachleuchten an/aus.
 *   Umgebung: VP_FRAMES=n beendet nach n Bildern, VP_SHOT=datei.bmp macht dann ein Bild,
 *           VP_SHOT_AT=n1,n2 Bilder shot_<n>.bmp, VP_HEADLESS=1 rechnet ohne Fenster,
 *           VP_NOSLEEP=1 ohne Bildtakt, VP_STATS=0 ohne Statistikzeile, VP_MUTE=1 ohne Ton.
 */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <dirent.h>

#include "vectrex/vectrexInterface.h"
#include "plat.h"

uint8_t currentButtonState = 0;
int8_t  currentJoy1X = 0;
int8_t  currentJoy1Y = 0;

/* ---- Bildschirm: 3:4 hochkant, PiTrex-Raum x +-18000, y +-24000 ---- */
#define WIN_W 600
#define WIN_H 800
#define PT_X  18000.0f
#define PT_Y  24000.0f

typedef struct { int32_t x0, y0, x1, y1; uint8_t b; } seg_t;
#define MAX_SEG 8192
static seg_t g_seg[MAX_SEG];
static int   g_nseg;
static unsigned g_dropped_seg;

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_accum;
static int g_headless, g_show_stats = 1, g_glow = 1, g_frames_max = -1, g_frame;
static const char *g_shot_path;
static uint32_t g_t0_ms;

/* Strahlmodell (grob): je Strich ~100 Buszyklen (Befehle, Wartezeiten, Rampe), je Sprung ~45 dazu, dazu Laufzeit nach Laenge.
 * Abgeleitet aus einer Messung auf der Konsole (233 Striche + 51 Spruenge = 56 Bilder/s). Nur eine Groessenordnung;
 * die echte Zahl liefert uvm2_stats auf der Konsole. */
static long g_est_cycles;
static int  g_jumps, g_rezero, g_run, g_run_max;
static int32_t g_last_x = 0x7FFFFFFF, g_last_y = 0x7FFFFFFF;
static int  g_zero_every = 0, g_zero_jump = 24;

void vectrexinit(int mode) { (void)mode; }
void v_init(void) {}
static int g_hz = 60;
void v_setRefresh(int hz) { if (hz > 0) g_hz = hz; }
void v_setColour(uint32_t rgb) { (void)rgb; }
uint32_t v_millis(void) { return SDL_GetTicks() - g_t0_ms; }

void v_directDraw32(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t b)
{
    if (b == 0) return;
    if (g_nseg >= MAX_SEG) { g_dropped_seg++; return; }
    seg_t *s = &g_seg[g_nseg++];
    s->x0 = x0; s->y0 = y0; s->x1 = x1; s->y1 = y1; s->b = b;
    double jd = (g_last_x == 0x7FFFFFFF) ? 1000.0 : fmax(fabs((x0 - g_last_x) / 127.0), fabs((y0 - g_last_y) / 127.0));
    int jumped = jd > 0.06;
    if (jumped) {
        if ((g_zero_jump > 0 && jd >= g_zero_jump) || (g_zero_every > 0 && g_run >= g_zero_every)) { g_rezero++; g_run = 0; }
        g_jumps++;
        g_est_cycles += 45 + (long)(jd * 0.6);
    }
    g_run++;
    if (g_run > g_run_max) g_run_max = g_run;
    g_est_cycles += 100 + (long)(hypot((x1 - x0) / 127.0, (y1 - y0) / 127.0) * 1.2);
    g_last_x = x1; g_last_y = y1;
}

uint8_t v_readButtons(void) { return currentButtonState; }
void v_readJoystick1Analog(void) {}
void v_playSample(int idx, int voice, int loop) { (void)idx; (void)voice; (void)loop; }
void v_stopSample(int voice) { (void)voice; }
int  v_samplePlaying(int voice) { (void)voice; return 0; }
void v_beamNewStroke(void) {}

/* ---- Ton: der Klangchip wird im Spiel nachgebildet (game/aysynth.c) und kommt hier als PCM an ---- */
void v_writePSG(uint8_t reg, uint8_t val) { (void)reg; (void)val; }
void v_setSoundAY(uint8_t reg, uint8_t val) { (void)reg; (void)val; }
static plat_render_t s_render;
static SDL_AudioDeviceID s_adev;
static void audio_cb(void *ud, Uint8 *stream, int len)
{
    static int16_t l[2048], r[2048];
    int16_t *out = (int16_t *)stream;
    int n = len / 4;
    (void)ud;
    while (n > 0) {
        int c = n > 2048 ? 2048 : n;
        s_render(l, r, c);
        for (int i = 0; i < c; i++) { *out++ = l[i]; *out++ = r[i]; }
        n -= c;
    }
}

int plat_audio_has_chip(void) { return 0; }
int plat_reset_ms(void) { return 0; }
uint32_t plat_clock_us(void) { return (uint32_t)(SDL_GetPerformanceCounter() * 1000000ull / SDL_GetPerformanceFrequency()); }
void plat_redraw_last(int on) { (void)on; }

int plat_audio_start(plat_render_t render, int rate)
{
    if (g_headless || getenv("VP_MUTE")) return 1;                   /* stumm, aber der Synth-Pfad bleibt */
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return 0;
    SDL_AudioSpec want; SDL_zero(want);
    want.freq = rate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512; want.callback = audio_cb;
    s_render = render;
    s_adev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (!s_adev) return 0;
    SDL_PauseAudioDevice(s_adev, 0);
    return 1;
}
void plat_audio_pump(plat_render_t render) { (void)render; }

/* ---- Plattformfunktionen ---- */
static const char *const DATA_DIRS[] = { "pinball/", "", "../pinball/", "sdcard/pinball/", "../sdcard/pinball/" };

#define ARENA_SIZE (8u * 1024u * 1024u)
static uint8_t *g_arena;
static FILE *g_stream[2];

const uint8_t *plat_arena(uint32_t *size)
{
    if (getenv("VP_NOPCM")) return NULL;
    if (!g_arena) g_arena = malloc(ARENA_SIZE);
    *size = ARENA_SIZE;
    return g_arena;
}

int plat_stream_open(int h, const char *name, uint32_t *size)
{
    char path[512];
    if (g_stream[h]) { fclose(g_stream[h]); g_stream[h] = NULL; }
    for (unsigned i = 0; i < sizeof DATA_DIRS / sizeof DATA_DIRS[0] && !g_stream[h]; i++) {
        snprintf(path, sizeof path, "%s%s", DATA_DIRS[i], name);
        g_stream[h] = fopen(path, "rb");
    }
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
    if (h == 1 && max > 65536u) max = 65536u;                         /* wie auf der Konsole: das Lied kommt in Stuecken */
    return (uint32_t)fread(g_arena + offset, 1, max, g_stream[h]);
}

void plat_arena_get(uint32_t offset, uint8_t *dst, uint32_t n) { if (g_arena && offset + n <= ARENA_SIZE) memcpy(dst, g_arena + offset, n); }
void plat_arena_put(uint32_t offset, const uint8_t *src, uint32_t n) { if (g_arena && offset + n <= ARENA_SIZE) memcpy(g_arena + offset, src, n); }

int plat_find_file(const char *dir, const char *prefix, const char *ext, char *out, size_t n)
{
    char path[512];
    size_t pl = strlen(prefix);
    for (unsigned i = 0; i < sizeof DATA_DIRS / sizeof DATA_DIRS[0]; i++) {
        snprintf(path, sizeof path, "%s%s", DATA_DIRS[i], dir);
        DIR *d = opendir(path);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            size_t l = strlen(e->d_name);
            if (l < pl + 4 || strncmp(e->d_name, prefix, pl)) continue;
            const char *x = e->d_name + l - 4;
            if (x[0] != '.' || (x[1] | 32) != ext[1] || (x[2] | 32) != ext[2] || (x[3] | 32) != ext[3]) continue;
            snprintf(out, n, "%s/%s", dir, e->d_name);
            closedir(d);
            return 1;
        }
        closedir(d);
    }
    return 0;
}

size_t plat_read_file(const char *name, uint8_t *buf, size_t max)
{
    static const char *dirs[] = { "pinball/", "", "../pinball/" };
    char path[512];
    for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        snprintf(path, sizeof path, "%s%s", dirs[i], name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        size_t n = fread(buf, 1, max, f);
        fclose(f);
        return n;
    }
    return 0;
}

int plat_write_file(const char *name, const uint8_t *data, size_t n)
{
    char path[512];
    if (getenv("VP_NOSAVE")) return 1;
#ifdef _WIN32
    _mkdir("pinball");
#endif
    snprintf(path, sizeof path, "pinball/%s", name);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t w = fwrite(data, 1, n, f);
    fclose(f);
    return w == n;
}

void plat_bus_stats(unsigned *bus_cycles, unsigned *dropped, unsigned *overrun) { *bus_cycles = 0; *dropped = 0; *overrun = 0; }
void plat_beam_init(int zero_every, int zero_jump) { g_zero_every = zero_every; g_zero_jump = zero_jump; }
static int g_beam[PLAT_BEAM_N] = { 35, 640, 160 };
int  plat_beam_get(int which) { return which >= 0 && which < PLAT_BEAM_N ? g_beam[which] : 0; }
void plat_beam_set(int which, int value) { if (which >= 0 && which < PLAT_BEAM_N) g_beam[which] = value; }
void plat_beam_save(void) {}
void plat_log(const char *msg) { fputs(msg, stderr); fputc('\n', stderr); }
uint32_t plat_micros(void)
{
    if (getenv("VP_FIXED_DT")) return (uint32_t)g_frame * 16667u;      /* Testlaeufe: feste Bildzeit, wiederholbar */
    return (uint32_t)(SDL_GetPerformanceCounter() * 1000000ULL / SDL_GetPerformanceFrequency());
}
int plat_test_int(const char *name, int def)
{
    char key[64];
    snprintf(key, sizeof key, "VP_%s", name);
    const char *e = getenv(key);
    return e ? atoi(e) : def;
}

/* ---- Fenster und Darstellung ---- */
static int px_x(int32_t x) { return (int)((x / PT_X * 0.5f + 0.5f) * WIN_W); }
static int px_y(int32_t y) { return (int)((0.5f - y / PT_Y * 0.5f) * WIN_H); }

static void open_window(void)
{
    if (g_headless) return;
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO);
    g_win = SDL_CreateWindow("Vector Pinball - Vectrex-Simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    g_accum = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, WIN_W, WIN_H);
    SDL_SetTextureBlendMode(g_accum, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(g_ren, g_accum);
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    SDL_SetRenderTarget(g_ren, NULL);
}

static void poll_input(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) exit(0);
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            if (e.key.keysym.sym == SDLK_ESCAPE) exit(0);
            if (e.key.keysym.sym == SDLK_F1) g_show_stats ^= 1;
            if (e.key.keysym.sym == SDLK_F3) g_glow ^= 1;
            if (e.key.keysym.sym == SDLK_F2) g_shot_path = "shot.bmp";
        }
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    int jx = 0, jy = 0;
    /* Die Pfeiltasten sind der Stick (Menue; im Spiel: Stoss gegen den Tisch). Die Flipper liegen wie bei PC-Flippern auf den Umschalttasten. */
    int kl = k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A], kr = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D];
    if (kl) jx -= 127;
    if (kr) jx += 127;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) jy += 127;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) jy -= 127;
    int b1 = k[SDL_SCANCODE_1] || k[SDL_SCANCODE_Y] || k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_BACKSPACE];
    int b2 = k[SDL_SCANCODE_2] || k[SDL_SCANCODE_X] || k[SDL_SCANCODE_P];
    int b3 = k[SDL_SCANCODE_3] || k[SDL_SCANCODE_C] || k[SDL_SCANCODE_SPACE];
    int b4 = k[SDL_SCANCODE_4] || k[SDL_SCANCODE_V] || k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_RCTRL] || k[SDL_SCANCODE_RETURN] ||
             k[SDL_SCANCODE_SLASH] || k[SDL_SCANCODE_MINUS];
    static SDL_GameController *pad;
    if (!pad && SDL_NumJoysticks() > 0 && SDL_IsGameController(0)) pad = SDL_GameControllerOpen(0);
    if (pad) {
        int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 258;
        int ay = -SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 258;
        if (abs(ax) > abs(jx)) jx = ax;
        if (abs(ay) > abs(jy)) jy = ay;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) jx = -127;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) jx = 127;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) jy = 127;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) jy = -127;
        b1 |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000;
        b4 |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
        b4 |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
        b2 |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X) || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START);
        b3 |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
    }
    currentJoy1X = (int8_t)jx;
    currentJoy1Y = (int8_t)jy;
    currentButtonState = (uint8_t)(b1 | (b2 << 1) | (b3 << 2) | (b4 << 3));
}

/* Testhilfe VP_KEYS="bild:maske[:x:y],...": Knoepfe (Bit 0-3) und Stick ab dem genannten Bild */
static void scripted_input(void)
{
    const char *sc = getenv("VP_KEYS");
    if (!sc) return;
    char tmp[512];
    strncpy(tmp, sc, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
    int mask = 0, jx = 0, jy = 0;
    for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) {
        int f = 0, m = 0, x = 0, y = 0;
        int n = sscanf(t, "%d:%d:%d:%d", &f, &m, &x, &y);
        if (n >= 2 && g_frame >= f) { mask = m; jx = n >= 3 ? x : 0; jy = n >= 4 ? y : 0; }
    }
    currentButtonState = (uint8_t)mask; currentJoy1X = (int8_t)jx; currentJoy1Y = (int8_t)jy;
}

static void draw_frame_to(SDL_Renderer *ren)
{
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, g_glow ? 96 : 255);          /* Nachleuchten: alten Inhalt abdunkeln */
    SDL_RenderFillRect(ren, NULL);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
    for (int i = 0; i < g_nseg; i++) {
        const seg_t *s = &g_seg[i];
        int c = (int)(255.0 * pow(s->b / 127.0, 1.6));               /* Helligkeitsstufen deutlich unterscheidbar */
        if (c > 255) c = 255;
        if (c < 12) c = 12;
        int x0 = px_x(s->x0), y0 = px_y(s->y0), x1 = px_x(s->x1), y1 = px_y(s->y1);
        if (s->b >= 124) {                                            /* volle Helligkeit ueberstrahlt: breiter */
            SDL_SetRenderDrawColor(ren, (Uint8)(c / 3), (Uint8)(c * 2 / 3), (Uint8)(c / 2), 255);
            SDL_RenderDrawLine(ren, x0 + 1, y0, x1 + 1, y1);
            SDL_RenderDrawLine(ren, x0, y0 + 1, x1, y1 + 1);
        }
        if (g_glow && s->b > 60) {
            int h = (s->b - 60) * 2;
            SDL_SetRenderDrawColor(ren, 0, (Uint8)(h / 3), (Uint8)(h / 4), 255);
            SDL_RenderDrawLine(ren, x0 + 1, y0, x1 + 1, y1);
            SDL_RenderDrawLine(ren, x0 - 1, y0, x1 - 1, y1);
            SDL_RenderDrawLine(ren, x0, y0 + 1, x1, y1 + 1);
            SDL_RenderDrawLine(ren, x0, y0 - 1, x1, y1 - 1);
        }
        SDL_SetRenderDrawColor(ren, (Uint8)(c / 2), (Uint8)c, (Uint8)(c * 3 / 4), 255);
        SDL_RenderDrawLine(ren, x0, y0, x1, y1);
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

static void save_shot(const char *path)
{
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, WIN_W, WIN_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (g_headless) {                                                 /* ohne Fenster: in eine Software-Flaeche zeichnen */
        SDL_Renderer *sr = SDL_CreateSoftwareRenderer(sf);
        SDL_SetRenderDrawColor(sr, 0, 0, 0, 255);
        SDL_RenderClear(sr);
        int glow = g_glow; g_glow = 0;
        draw_frame_to(sr);
        g_glow = glow;
        SDL_RenderPresent(sr);
        SDL_DestroyRenderer(sr);
    } else {
        SDL_SetRenderTarget(g_ren, g_accum);
        SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
        SDL_SetRenderTarget(g_ren, NULL);
    }
    SDL_SaveBMP(sf, path);
    SDL_FreeSurface(sf);
}

static void present(void)
{
    if (!g_headless) {
        SDL_SetRenderTarget(g_ren, g_accum);
        draw_frame_to(g_ren);
        SDL_SetRenderTarget(g_ren, NULL);
        SDL_RenderCopy(g_ren, g_accum, NULL, NULL);
    }
    if (g_shot_path) { save_shot(g_shot_path); g_shot_path = NULL; }
    if (!g_headless) SDL_RenderPresent(g_ren);
}

void v_WaitRecal(void)
{
    static uint32_t next;
    if (g_frame == 0) { const char *e = getenv("VP_FRAMES"); if (e) g_frames_max = atoi(e); }

    if (g_show_stats && (g_frame % 60 == 0)) {
        long cyc = g_est_cycles > 0 ? g_est_cycles : 1;
        fprintf(stderr, "frame %5d  strokes %4d  jumps %3d  recentre %3d  longest run %3d  ~%ld bus cycles = ~%ld fps unlocked%s\n", g_frame, g_nseg, g_jumps,
                g_rezero, g_run_max, g_est_cycles, 1500000 / cyc, g_dropped_seg ? "  SEGMENTS LOST" : "");
    }
    {   /* VP_SHOT_AT=n1,n2,...: Bildschirmfotos shot_<n>.bmp zu den genannten Bildern */
        const char *at = getenv("VP_SHOT_AT");
        if (at && !g_shot_path) {
            char tmp[256]; strncpy(tmp, at, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
            for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) if (atoi(t) == g_frame) { static char nm[64]; snprintf(nm, sizeof nm, "shot_%d.bmp", g_frame); g_shot_path = nm; }
        }
    }
    if (g_frames_max >= 0 && g_frame + 1 >= g_frames_max && getenv("VP_SHOT")) g_shot_path = getenv("VP_SHOT");
    present();
    g_nseg = 0; g_est_cycles = 0; g_jumps = 0; g_rezero = 0; g_run = 0; g_run_max = 0; g_last_x = g_last_y = 0x7FFFFFFF;
    g_frame++;
    if (g_frames_max >= 0 && g_frame >= g_frames_max) exit(0);
    if (!g_headless) {
        poll_input();
        uint32_t now = SDL_GetTicks();
        if (next < now) next = now;
        next += 1000 / g_hz;
        if (next > now && !getenv("VP_NOSLEEP")) SDL_Delay(next - now);
    }
    scripted_input();
}

/* Vor main(): Umgebung lesen, Fenster oeffnen */
__attribute__((constructor)) static void host_boot(void)
{
    g_headless = getenv("VP_HEADLESS") != NULL;
    if (g_headless) SDL_Init(SDL_INIT_TIMER);
    g_t0_ms = SDL_GetTicks();
    open_window();
    const char *k = getenv("VP_STATS");
    if (k) g_show_stats = atoi(k);
}
