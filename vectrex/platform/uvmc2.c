/*
 * uvmc2.c - Plattformfunktionen fuer das .um2-Image (RP2350 im UVMC2).
 * Einstellungen liegen auf der SD-Karte im Ordner pinball/.
 */
#include <stdio.h>
#include <string.h>
#include "plat.h"
#include "uvm2_sd.h"
#include "uvm2_config.h"
#include "uvm2_jack.h"
#include "uvm2_bus.h"
#include "ff.h"

#define DATA_DIR "pinball/"

/* Einstellungen des Spiels im Kit-Assistenten (Bildwiederholrate); Datei config/PINBALL.CFG */
void uvm2_game_settings(void) { uvm2_config_game("PINBALL", UVM2_SETTING_HZ); }

/* Nachzentrieren (Re-Zero): das Kit zentriert nach Entfernung (uvm2_zero_jump, 24 Einheiten) und - falls gesetzt - nach
 * einer Zahl von Strichen (uvm2_zero_every), immer nur an einem Sprung. Beides ist im Spiel einstellbar (ADJUST). */
extern volatile int32_t uvm2_zero_every;
extern volatile int32_t uvm2_zero_jump;
void plat_beam_init(int zero_every, int zero_jump) { uvm2_zero_every = zero_every; uvm2_zero_jump = zero_jump; }

void plat_bus_stats(unsigned *bus_cycles, unsigned *dropped, unsigned *overrun)
{
    *bus_cycles = uvm2_stats.bus_cycles; *dropped = uvm2_stats.dropped; *overrun = uvm2_stats.overrun;
}

/* ---- Reset-Knopf der Konsole ----
 * Am Modulschacht liegt keine Reset-Leitung, lesen kann man den Knopf also nicht. Aber er setzt auch den VIA-Baustein der
 * Konsole zurueck, und der vergisst dabei seine Einstellungen: das Richtungsregister DDRB, das das Kit zu Beginn jedes
 * Bildes auf $9F setzt, liest sich dann als 0. Geprueft wird einmal je Bild auf Kern 1 (dem gehoert der Bus; der Haken
 * uvm2_core1_gap ist dafuer da), gleich nach dem Lesen der Knoepfe. Gemessen wird die Zeit vom letzten guten Bild bis zum
 * ersten wieder guten - das stimmt auch dann, wenn die Konsole waehrend des Resets keinen Takt liefert und Kern 1 so lange
 * stehen bleibt. Scharf wird die Erkennung erst, wenn DDRB ein paarmal richtig gelesen wurde: liest es sich auf einer
 * Konsole grundsaetzlich anders, bleibt sie einfach aus. */
static volatile uint32_t s_reset_ms;
void uvm2_core1_gap(void)
{
    static uint32_t good_us, good_n;
    static int bad;
    const uint32_t now = plat_micros();
    const uint8_t v = uvm2_via_read(UVM2_VIA_DDRB);
    if (v == 0x9F) {
        if (bad && good_n >= 50) s_reset_ms = (now - good_us) / 1000u;
        bad = 0; good_us = now;
        if (good_n < 50) good_n++;
    } else if (v == 0x00) bad = 1;
}
/* UVM2_REPLAY_LAST zeichnet das letzte Bild noch einmal, sobald Kern 0 nicht rechtzeitig fertig ist. Im Spiel ist das
 * falsch: ist die Logik nur wenig langsamer als der Strahl, wartet das neue Bild jedes Mal ein ganzes Wiederholbild ab und
 * die Bildrate halbiert sich fast. Der Riegel des Kits (uvm2_replay_hold) schaltet es darum ueberall aus, ausser im Vorspann. */
extern volatile uint32_t uvm2_replay_hold;
void plat_redraw_last(int on) { uvm2_replay_hold = on ? 0u : 1u; }

uint32_t plat_clock_us(void) { return plat_micros(); }

int plat_reset_ms(void) { uint32_t v = s_reset_ms; s_reset_ms = 0; return (int)v; }

/* Strahlwerte der Konsole: gehoeren in config/uvm2.cfg und gelten fuer alle Spiele */
int uvm2_config_save(void);
int plat_beam_get(int which)
{
    struct uvm2_config c;
    uvm2_config_current(&c);
    return which == PLAT_BEAM_ZERO ? (int)c.zero : (which == PLAT_BEAM_TAIL ? (int)c.t1_tail_q8 : (int)c.scale);
}
void plat_beam_set(int which, int value)
{
    struct uvm2_config c;
    uvm2_config_current(&c);
    if (which == PLAT_BEAM_ZERO) c.zero = value;
    else if (which == PLAT_BEAM_TAIL) c.t1_tail_q8 = value;
    else if (which == PLAT_BEAM_SCALE) c.scale = value;
    uvm2_config_apply(&c);
}
void plat_beam_save(void) { uvm2_config_save(); }

size_t plat_read_file(const char *name, uint8_t *buf, size_t max)
{
    char path[64];
    snprintf(path, sizeof path, DATA_DIR "%s", name);
    return uvm2_sd_read(path, buf, (uint32_t)max);
}

int plat_write_file(const char *name, const uint8_t *data, size_t n)
{
    char path[64];
    snprintf(path, sizeof path, DATA_DIR "%s", name);
    return uvm2_sd_write(path, data, (uint32_t)n);
}

/* Tonaufnahmen: von der Karte in das PSRAM (8 MB). Geschrieben wird durch das ungepufferte Fenster, gelesen durch das
 * gepufferte - so macht es auch das Beispiel des Kits (game/tacscan/src/ts_jack.c), dort stehen die Messungen dazu.
 * Das PSRAM wird nur hochgefahren, wenn das Image mit UVM2_PSRAM_START=1 gebaut ist (Makefile) - ohne das bleibt
 * uvm2_psram_ready auf -1 und es gibt keinen Speicher fuer Aufnahmen. */
extern int uvm2_psram_ready;
#define PSRAM_WRITE ((unsigned char *)(uintptr_t)0x15000000u)
#define PSRAM_READ  ((const unsigned char *)(uintptr_t)0x11000000u)
#define PSRAM_SIZE  (7u * 1024u * 1024u + 512u * 1024u)
static uvm2_sd_file s_stream[2];
static int s_stream_open[2];

const uint8_t *plat_arena(uint32_t *size)
{
    if (uvm2_psram_ready != 1) return NULL;
    *size = PSRAM_SIZE;
    return PSRAM_READ;
}

int plat_stream_open(int h, const char *name, uint32_t *size)
{
    char path[96];
    if (s_stream_open[h]) { uvm2_sd_close(&s_stream[h]); s_stream_open[h] = 0; }
    snprintf(path, sizeof path, DATA_DIR "%s", name);
    if (!uvm2_sd_open(path, &s_stream[h])) return 0;
    s_stream_open[h] = 1;
    *size = s_stream[h].len;
    return 1;
}

uint32_t plat_stream_read(int h, uint32_t offset, uint32_t max, unsigned budget_us)
{
    uint32_t got = 0;
    if (!s_stream_open[h] || offset + max > PSRAM_SIZE) return 0;
    const uint32_t t0 = plat_micros();
    while (got < max) {
        uint32_t want = max - got > 1024u ? 1024u : max - got;
        uint32_t n = uvm2_sd_next(&s_stream[h], PSRAM_WRITE + offset + got, want);
        got += n;
        if (n < want) { uvm2_sd_close(&s_stream[h]); s_stream_open[h] = 0; break; }
        if (plat_micros() - t0 >= budget_us) break;
    }
    return got;
}

/* Durch das ungepufferte Fenster: liefert auch an mehrfach beschriebenen Stellen den aktuellen Inhalt */
void plat_arena_get(uint32_t offset, uint8_t *dst, uint32_t n) { if (offset + n <= PSRAM_SIZE) memcpy(dst, PSRAM_WRITE + offset, n); }
void plat_arena_put(uint32_t offset, const uint8_t *src, uint32_t n) { if (offset + n <= PSRAM_SIZE) memcpy(PSRAM_WRITE + offset, src, n); }

/* Verzeichnis lesen: das Kit bietet dafuer nichts an, FatFs (im Kit enthalten, vom Kartenleser des Kits eingehaengt) schon */
int plat_find_file(const char *dir, const char *prefix, const char *ext, char *out, size_t n)
{
    DIR d;
    FILINFO fi;
    char path[64];
    size_t pl = strlen(prefix);
    int found = 0;
    snprintf(path, sizeof path, DATA_DIR "%s", dir);
    if (f_opendir(&d, path) != FR_OK) return 0;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        size_t l = strlen(fi.fname);
        if ((fi.fattrib & AM_DIR) || l < pl + 4 || strncmp(fi.fname, prefix, pl)) continue;
        const char *x = fi.fname + l - 4;
        if (x[0] != '.' || (x[1] | 32) != ext[1] || (x[2] | 32) != ext[2] || (x[3] | 32) != ext[3]) continue;
        snprintf(out, n, "%s/%s", dir, fi.fname);
        found = 1;
        break;
    }
    f_closedir(&d);
    return found;
}

void plat_log(const char *msg) { (void)msg; }
uint32_t plat_micros(void) { return *(volatile uint32_t *)0x400B000CU; }   /* TIMER0, 1 MHz (wie im Kit) */
int plat_test_int(const char *name, int def) { (void)name; return def; }

int plat_audio_has_chip(void) { return 1; }

int plat_audio_start(plat_render_t render, int rate)
{
    (void)render;
    if (rate != UVM2_JACK_RATE) return 0;
    return uvm2_jack_init();
}

void plat_audio_pump(plat_render_t render)
{
    int16_t l[256], r[256];
    int n = uvm2_jack_space();
    while (n > 0) {
        int c = n > 256 ? 256 : n;
        render(l, r, c);
        uvm2_jack_write_lr(l, r, c);                 /* ein Wort des Rings = ein Stereo-Abtastwert */
        n -= c;
    }
}
