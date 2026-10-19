/*
 * sound.c - Ton.
 *
 * Lautsprecher der Vectrex (Klangchip, drei Rechtecktoene und Rauschen): die Effekte sind neu gesetzt, halten sich
 * aber an das Original:
 *   Punkte      eine von sechs Glocken (A, C, C', D, E, G), nie zweimal dieselbe hintereinander
 *   Rollover    bis zu drei Toene der Fuenftonleiter um E
 *   Flipper     kurzes Klacken (Rauschen)
 *   Bumper      dumpfer Schlag (tiefer Ton + Rauschen) - im Original ohne eigenen Klang
 *   Ballstart   aufsteigender Ton, Meldung: zwei Toene, Spielstart/-ende: kleiner Dreiklang, Ballverlust: fallender Ton
 * Kanal A: Glocken und Rollover, Kanal B: Meldungen und Laeufe, Kanal C: Geraeusche. 60 Schritte je Sekunde Echtzeit.
 *
 * Buchse des UVMC2 und Simulator: die Aufnahmen des Originals mit dessen Lautstaerken und Tonhoehen (VPSoundpool.java):
 * Glocke 0.5, Rollover 0.3 in bis zu drei Tonhoehen, Start und Flipper 1.0, Meldung 0.66, Spielstart 0.5.
 * Musik wie dort: nach je 20 Punkte-Treffern setzt das Schlagzeug ein (falls es nicht laeuft), der Flaechenklang nach 10,
 * danach in wachsenden Abstaenden (+42). Fehlen die WAV-Dateien auf der Karte, kommen die Chip-Effekte als Nachbildung.
 */
/* nicht zeitkritisch (60 Schritte je Sekunde): auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include "sound.h"
#include "aysynth.h"
#include "pcm.h"
#include "settings.h"
#include "plat.h"
#include "vp.h"
#include "vectrex/vectrexInterface.h"
#include <string.h>
#include <stdio.h>

typedef struct { int16_t period, slide; uint8_t vol, decay, frames, noise; } step_t;   /* decay: 1/16 Lautstaerke je Schritt */
typedef struct { const step_t *seq; int n, i, t, period, vol; step_t custom[3]; } voice_t;

static voice_t voice[3];
static int pcm_on, chip_on, pcm_started;
static uint32_t last_us, acc_us, rnd = 0xC0FFEEu;
static int last_ding = -1, last_sample = -1;
static uint32_t last_hits, c_score, pad_mod = 10;
static int song_n = -1;                      /* Nummer des laufenden eigenen Lieds, -1 = keins */

/* Tonperioden: 93750 / Frequenz */
#define P_A5 107
#define P_C6 90
#define P_D6 80
#define P_E6 71
#define P_G6 60
#define P_A6 53
#define P_C7 45
static const int16_t DING[6] = { P_A5, P_C6, P_C7, P_D6, P_E6, P_G6 };
static const int16_t PENTA[6] = { P_C6, P_D6, P_E6, P_G6, P_A6, P_C7 };
/* Tonhoehen des Rollover-Klangs im Original (E, dazu C, D, G, A, C) */
static const uint32_t PITCH[6] = { 52016, 58386, 65536, 77936, 87480, 104032 };

static const step_t SEQ_FLIPPER[] = { { 0, 0, 12, 56, 4, 5 } };
static const step_t SEQ_KICK[]    = { { 520, 70, 14, 34, 7, 12 } };
static const step_t SEQ_BALL[]    = { { 700, -32, 11, 4, 18, 0 } };
static const step_t SEQ_MESSAGE[] = { { P_G6, 0, 11, 8, 5, 0 }, { P_C7, 0, 11, 18, 10, 0 } };
static const step_t SEQ_START[]   = { { P_C6, 0, 11, 6, 6, 0 }, { P_E6, 0, 11, 6, 6, 0 }, { P_G6, 0, 11, 6, 6, 0 }, { P_C7, 0, 12, 12, 16, 0 } };
static const step_t SEQ_DRAIN[]   = { { 160, 22, 12, 6, 30, 0 } };
/* Tilt, nach dem Vorbild der Williams-Flipper der 90er - beides vom Chip erzeugt, ohne Aufnahme, und mit nichts sonst im
 * Spiel zu verwechseln, weil nur diese beiden den Saegezahn benutzen (Lautstaerke 16 = Huellkurve, siehe write_all).
 *   Warnung  zwei kurze, harte Brummer, jeder sackt in der Tonhoehe ab (133 -> 100 Hz), dazu ein Rechteck eine Oktave hoeher
 *   Tilt     ein langer Saegezahn, der von 325 Hz bis in den Keller faellt, ein Rechteck faellt mit, am Ende ein Schlag */
static const step_t SEQ_WARN_SAW[] = { { 44, 2, 16, 0, 8, 0 }, { 0, 0, 0, 0, 5, 0 }, { 44, 2, 16, 0, 10, 0 } };
static const step_t SEQ_WARN_SQ[]  = { { 352, 16, 13, 0, 8, 0 }, { 0, 0, 0, 0, 5, 0 }, { 352, 16, 13, 0, 10, 0 } };
static const step_t SEQ_TILT_SAW[] = { { 18, 2, 16, 0, 84, 0 } };
static const step_t SEQ_TILT_SQ[]  = { { 288, 32, 13, 2, 84, 0 } };
static const step_t SEQ_TILT_END[] = { { 0, 0, 0, 0, 62, 0 }, { 0, 0, 14, 7, 34, 30 } };

static uint32_t rnd_next(void) { rnd = rnd * 1664525u + 1013904223u; return rnd >> 8; }

static int16_t shadow[16];                   /* zuletzt geschriebene Werte (-1 = unbekannt): nichts doppelt an den Chip schicken */

/* Die Chip-Effekte gehen an den Lautsprecher der Konsole und - nur wenn es keine Originalaufnahmen gibt - als Nachbildung an die Buchse */
static int synth_force;                      /* Vorspann ohne eigene Aufnahme: Nachbildung auch dann an die Buchse */
static int mus_to_pcm;                       /* Menuemusik vom Chip laeuft: auch sie geht an die Buchse */
static int synth_hold;                       /* so viele Schritte lang geht der Chip auch an die Buchse (Tilt-Toene gibt es nur vom Chip) */
static int music_muted;                      /* nach einem Tilt: keine Musik bis zur naechsten Runde */
static int synth_to_pcm(void) { return pcm_on && (synth_force || mus_to_pcm || synth_hold > 0 || !pcm_have()); }

static void ay(int r, int v)
{
    if (shadow[r] == v) return;
    shadow[r] = (int16_t)v;
    if (chip_on) v_writePSG((uint8_t)r, (uint8_t)v);
    aysynth_write(r, (uint8_t)v);
}

static void start(int ch, const step_t *seq, int n)
{
    voice_t *v = &voice[ch];
    v->seq = seq; v->n = n; v->i = 0; v->t = 0;
    v->period = seq[0].period; v->vol = seq[0].vol * 16;
}

/* ---- Menuemusik auf dem Klangchip ----
 * Eine kleine eigene Schleife (a-Moll: Am - F - C - G, acht Takte): Kanal A zupft Akkordtoene, Kanal B den Bass, Kanal C
 * schlaegt den Takt mit Rauschen. Sie laeuft nur im Menue und nur, wenn dort kein eigenes Lied (snd/0_*) spielt. Ein Effekt
 * hat Vorrang: solange er klingt, schweigt die Musik auf seinem Kanal. Noten entstehen aus Akkord und Muster - das kostet
 * ein paar Dutzend Byte statt einer Notentabelle. */
#define MUS_TICKS 7                          /* Schritte (1/60 s) je Sechzehntel: etwa 128 Schlaege je Minute */
static int mus_on, mus_t, mus_step;
static int mus_p[3], mus_v[3], mus_d[3], mus_noise;

static int note_period(int semis)            /* Halbtoene ueber C2 */
{
    static const uint16_t OCT[12] = { 1433, 1353, 1277, 1205, 1138, 1074, 1014, 957, 903, 852, 804, 759 };
    return OCT[semis % 12] >> (semis / 12);
}

static void music_tick(void)
{
    static const uint8_t CHORD[4][3] = { { 9, 12, 16 }, { 5, 9, 12 }, { 0, 4, 7 }, { 7, 11, 14 } };
    static const int8_t ARP_A[16] = { 0, 1, 2, 3, 2, 1, 2, 3, 0, 1, 2, 3, 4, 3, 2, 1 };
    static const int8_t ARP_B[16] = { 3, -1, 2, -1, 3, 4, -1, 3, 2, -1, 1, -1, 2, 3, -1, -1 };
    for (int c = 0; c < 3; c++) { mus_v[c] -= mus_d[c]; if (mus_v[c] < 0) mus_v[c] = 0; }
    if (mus_t++ % MUS_TICKS) return;
    int pos = mus_step & 15, bar = (mus_step >> 4) & 7, ch = bar & 3;
    mus_step++;
    int idx = (bar < 4 ? ARP_A : ARP_B)[pos];
    if (idx >= 0) { mus_p[0] = note_period(CHORD[ch][idx % 3] + 12 * (idx / 3) + 24); mus_v[0] = 9 * 16; mus_d[0] = 9; }
    if (!(pos & 1)) { mus_p[1] = note_period(CHORD[ch][0] + 12 + ((pos & 3) == 2 ? 12 : 0));   /* ab C3: tiefer traegt der kleine Lautsprecher nicht */ mus_v[1] = 10 * 16; mus_d[1] = 7; }
    if (!(pos & 7)) { mus_noise = 22; mus_v[2] = 9 * 16; mus_d[2] = 30; }             /* schwerer Schlag */
    else if ((pos & 3) == 2) { mus_noise = 3; mus_v[2] = 6 * 16; mus_d[2] = 34; }     /* leichter Schlag */
}

static void music_set(int on)
{
    if (on == mus_on) return;
    mus_on = on; mus_t = mus_step = 0;
    for (int c = 0; c < 3; c++) mus_v[c] = 0;
}

static void write_all(void)
{
    int mixer = 0x3F;
    for (int c = 0; c < 3; c++) {
        voice_t *v = &voice[c];
        int vol = 0;
        if (!v->seq && mus_on) {
            vol = mus_v[c] / 16;
            if (c < 2) { ay(2 * c, mus_p[c] & 255); ay(2 * c + 1, mus_p[c] >> 8); mixer &= ~(1 << c); }
            else if (vol) { ay(6, mus_noise); mixer &= ~(8 << c); }
        }
        if (v->seq) {
            const step_t *s = &v->seq[v->i];
            vol = v->vol / 16;
            if (s->vol >= 16) {
                /* Saegezahn ueber die Huellkurve des Chips: period ist hier die Huellkurvenperiode (Frequenz 5859 / period) */
                int p = v->period < 1 ? 1 : v->period;
                ay(11, p & 255); ay(12, p >> 8); ay(13, 8);
                vol = 16;
            } else if (v->period > 0) {
                int p = v->period > 4095 ? 4095 : v->period;
                ay(2 * c, p & 255); ay(2 * c + 1, p >> 8);
                mixer &= ~(1 << c);
            }
            if (s->noise) { ay(6, s->noise); mixer &= ~(8 << c); }
        }
        ay(8 + c, vol);
    }
    ay(7, mixer);
}

static void tick(void)
{
    if (synth_hold > 0) synth_hold--;
    for (int c = 0; c < 3; c++) {
        voice_t *v = &voice[c];
        if (!v->seq) continue;
        const step_t *s = &v->seq[v->i];
        v->period += s->slide;
        if (v->period < 1 && s->period) v->period = 1;
        v->vol -= s->decay;
        if (v->vol < 0) v->vol = 0;
        if (++v->t >= s->frames) {
            if (++v->i >= v->n) { v->seq = NULL; continue; }
            v->t = 0; v->period = v->seq[v->i].period; v->vol = v->seq[v->i].vol * 16;
        }
    }
    if (mus_on) music_tick();
    write_all();
}

static void render_cb(int16_t *left, int16_t *right, int n)
{
    /* Buchse abgeschaltet (SOUND auf CONSOLE oder OFF): Stille schreiben. Wird der Ring gar nicht mehr gefuettert, spielt
     * die Buchse seinen letzten Inhalt in Schleife - das Lied "haengt und rattert". */
    if (pcm_on && synth_to_pcm()) aysynth_render(left, n);
    else memset(left, 0, sizeof(int16_t) * (size_t)n);
    memcpy(right, left, sizeof(int16_t) * (size_t)n);                /* der Chip ist mono: auf beide Seiten */
    if (pcm_on) pcm_mix(left, right, n);
}

static int pan_ball = 128, pan_flip = 128;
void sound_stereo(int ball, int flipper) { pan_ball = ball; pan_flip = flipper; }

void sound_silence(void)
{
    for (int c = 0; c < 3; c++) voice[c].seq = NULL;
    for (int r = 0; r < 16; r++) shadow[r] = -1;
    ay(7, 0x3F); ay(8, 0); ay(9, 0); ay(10, 0);
}

void sound_apply(int mode)
{
    int has_chip = plat_audio_has_chip();
    int want_chip = has_chip && (mode == SOUND_CONSOLE || mode == SOUND_BOTH);
    int want_pcm = has_chip ? (mode == SOUND_JACK || mode == SOUND_BOTH) : (mode != SOUND_OFF);
    chip_on = has_chip;                                   /* damit das Stummschalten unten den Chip erreicht */
    music_set(0); mus_to_pcm = 0;                         /* die Menuemusik setzt danach auf dem neuen Ausgang neu an */
    sound_silence();
    if (want_pcm && !pcm_started) {
        aysynth_init(PCM_RATE);
        pcm_init();
        pcm_started = plat_audio_start(render_cb, PCM_RATE) ? 1 : -1;
    }
    chip_on = want_chip;
    pcm_on = want_pcm && pcm_started > 0;
    if (!pcm_on) pcm_stop_all();
}

static void find_paths(void);

void sound_init(void)
{
    last_us = plat_micros();
    sound_apply(g_set.sound);
    find_paths();
}

int sound_files(void) { return SND_N; }
int sound_status(int *loading, int *memory)
{
    *loading = 0; *memory = 0;
    if (!pcm_on) return -1;
    return pcm_status(loading, memory);
}

/* Die Karte nur EINMAL durchsuchen (beim Start): ein Verzeichnislauf oder ein vergebliches Oeffnen kostet auf dem UVMC2
 * mehrere Millisekunden, und mitten im Vorspann oder beim Betreten des Menues liess das das Bild kurz aussetzen.
 * Gemerkt wird je Lied 0..10 und fuer die beiden Vorspann-Aufnahmen der Pfad, oder leer, wenn es keine Datei gibt. */
#define SONGS 11
static char song_path[SONGS + 2][48];        /* 0..10: Lieder, 11: intro_roll, 12: intro_crash */
static int  paths_known;
static void find_paths(void)
{
    char prefix[16];
    paths_known = 1;
    for (int n = 0; n < SONGS + 2; n++) {
        char *p = song_path[n];
        p[0] = 0;
        if (n < SONGS) snprintf(prefix, sizeof prefix, "%d_", n);
        else snprintf(prefix, sizeof prefix, n == SONGS ? "intro_roll" : "intro_crash");
        if (!plat_find_file("snd", prefix, ".wav", p, sizeof song_path[n])) plat_find_file("snd", prefix, ".mp3", p, sizeof song_path[n]);
    }
}

static void song_select(int n);

void sound_music(int n)
{
    const int menu = n == 0;
    music_muted = 0;
    song_select(n);
    /* Menue ohne eigenes Lied: die Schleife vom Klangchip (Lautsprecher der Konsole, und nachgebildet an der Buchse) */
    music_set(menu && g_set.music && song_n < 0 && (chip_on || pcm_on));
    mus_to_pcm = mus_on;
}

static void song_select(int n)
{
    char path[80], prefix[8];
    if (!pcm_on || !g_set.music) n = -1;
    if (n == song_n && (n < 0 || pcm_song_playing())) return;
    pcm_song_stop();
    song_n = -1;
    if (n < 0) return;
    (void)path; (void)prefix;
    if (!paths_known) find_paths();
    /* WAV zuerst (kostet keine Rechenzeit), sonst MP3 - gesucht wurde beim Start */
    if (n < SONGS && song_path[n][0] && pcm_song_start(song_path[n], 256 * g_set.music_vol / 10, 1)) {
        song_n = n;
        pcm_music_stop();
    }
}

/* ---- Vorspann ----
 * Nachbildung auf dem Klangchip. Rollen: tiefes Brummen (Kanal A) und Rauschen (Kanal B), dessen Lautstaerke im Takt der
 * Umdrehungen schwankt - der Takt wird schneller und alles lauter, bis die Kugel einschlaegt. Einschlag: ein Krach aus
 * Rauschen (Kanal C), ein dumpfer Schlag (Kanal B) und klappernde kurze Toene wie fallende Kegel (Kanal A). */
#define ROLL_STEPS 44
static step_t roll_hum[4], roll_noise[ROLL_STEPS];
static const step_t SEQ_CRASH[] = { { 0, 0, 15, 4, 60, 13 } };
static const step_t SEQ_THUD[]  = { { 1500, 70, 15, 16, 15, 0 } };
static const step_t SEQ_PINS[]  = { { 190, 5, 14, 44, 5, 0 }, { 262, 4, 13, 44, 5, 0 }, { 150, 6, 13, 40, 5, 0 }, { 310, 3, 12, 36, 6, 0 },
                                    { 215, 5, 12, 36, 5, 0 }, { 172, 4, 11, 30, 6, 0 }, { 280, 3, 10, 26, 7, 0 }, { 236, 2, 10, 14, 12, 0 } };

static int intro_file(const char *name, int loop)
{
    if (!pcm_on) return 0;
    if (!paths_known) find_paths();
    const char *p = song_path[name[6] == 'r' ? SONGS : SONGS + 1];      /* "intro_roll" / "intro_crash" */
    return p[0] && pcm_song_start(p, 230, loop);
}

void sound_intro(int what)
{
    switch (what) {
    case INTRO_ROLL: {
        pcm_song_stop(); song_n = -1;
        music_set(0); mus_to_pcm = 0;
        synth_force = !intro_file("intro_roll", 1);
        /* gut 3,6 s bis zum Einschlag: Brummen steigt, das Rauschen pulsiert immer schneller */
        for (int i = 0; i < 4; i++) roll_hum[i] = (step_t){ (int16_t)(1500 - 110 * i), -1, (uint8_t)(7 + 2 * i), 0, 60, 0 };
        for (int i = 0; i < ROLL_STEPS; i++) {
            int len = 9 - i / 7, loud = 7 + i / 5;
            if (loud > 14) loud = 14;
            roll_noise[i] = (step_t){ 0, 0, (uint8_t)((i & 1) ? loud : loud / 2 + 1), 0, (uint8_t)(len < 3 ? 3 : len), (uint8_t)(31 - i / 4) };
        }
        if (chip_on || synth_to_pcm()) { start(0, roll_hum, 4); start(1, roll_noise, ROLL_STEPS); }
        break;
    }
    case INTRO_FLIP:
        sound_events(VSFX_FLIPPER | VSFX_KICK, 0);
        break;
    case INTRO_CRASH:
        pcm_song_stop(); song_n = -1;
        synth_force = !intro_file("intro_crash", 0);
        if (chip_on || synth_to_pcm()) { start(0, SEQ_PINS, 8); start(1, SEQ_THUD, 1); start(2, SEQ_CRASH, 1); }
        break;
    default:
        if (synth_force) { synth_force = 0; sound_silence(); }
        break;                               /* eine laufende Aufnahme des Einschlags darf im Menue ausklingen, bis dessen Lied sie abloest */
    }
}

void sound_game_reset(void)
{
    pcm_music_stop();
    c_score = 0; pad_mod = 10;
}

static void score_hit(void)
{
    if (!pcm_on || !pcm_have()) return;
    int d;
    do d = (int)(rnd_next() % 6); while (d == last_sample);
    last_sample = d;
    pcm_play(SND_DING0 + d, 128, 65536, pan_ball);
    c_score++;
    if (!g_set.music || song_n >= 0 || music_muted) return;  /* eigenes Lied laeuft: die Musik des Originals schweigt */
    /* Das Original startet das Schlagzeug ohne Wiederholung: nach 72 s war Stille, bis wieder ein 20. Treffer kam. Hier laeuft es,
     * einmal gestartet, in Schleife bis zum Spielende (Wunsch von Olli). Der Flaechenklang kommt wie dort einzeln. */
    if (c_score % 20 == 0) pcm_music_start(SND_DRUM, 256, 1);
    if (c_score % pad_mod == 0) { pcm_music_start(SND_PAD, 128, 0); pad_mod += 42; }
}

void sound_events(uint32_t sfx, uint32_t score_hits)
{
    if (score_hits < last_hits) last_hits = 0;                    /* neuer Tisch: der Zaehler begann von vorn */
    uint32_t hits = score_hits - last_hits;
    last_hits = score_hits;
    if (!chip_on && !pcm_on) return;
    if (sfx & VSFX_TILT) {                                           /* Tilt: die Musik geht aus und kommt erst mit der naechsten Runde wieder */
        pcm_music_stop(); pcm_song_stop(); song_n = -1; music_muted = 1;
    }
    for (uint32_t i = 0; i < hits; i++) score_hit();

    if (pcm_on && pcm_have()) {
        if (sfx & VSFX_ROLLOVER) {
            int a = (int)(rnd_next() % 6), b = (int)(rnd_next() % 6), c = (int)(rnd_next() % 6);
            pcm_play(SND_ROLLOVER, 77, PITCH[a], pan_ball);
            if (b != a) pcm_play(SND_ROLLOVER, 77, PITCH[b], pan_ball);
            if (c != a && c != b) pcm_play(SND_ROLLOVER, 77, PITCH[c], pan_ball);
        }
        if (sfx & VSFX_FLIPPER) pcm_play(SND_FLIPPER, 256, 65536, pan_flip);
        if (sfx & VSFX_BALL) pcm_play(SND_LAUNCH, 256, 65536, pan_ball);
        if (sfx & VSFX_MESSAGE) pcm_play(SND_MESSAGE, 169, 65536, 128);
        if (sfx & VSFX_START) pcm_play(SND_START, 128, 65536, 128);
    }

    if (sfx & (VSFX_TILT | VSFX_WARN)) synth_hold = 1;               /* damit der Chip-Teil unten auch fuer die Buchse laeuft */
    if (!chip_on && !synth_to_pcm()) return;
    if (sfx & VSFX_SCORE) {
        int d;
        do d = (int)(rnd_next() % 6); while (d == last_ding);
        last_ding = d;
        voice_t *v = &voice[0];
        v->custom[0] = (step_t){ DING[d], 0, 12, 12, 16, 0 };
        start(0, v->custom, 1);
    }
    if (sfx & VSFX_ROLLOVER) {
        voice_t *v = &voice[0];
        int a = (int)(rnd_next() % 6), b = (int)(rnd_next() % 6), c = (int)(rnd_next() % 6), n = 0;
        v->custom[n++] = (step_t){ PENTA[a], 0, 11, 10, 4, 0 };
        if (b != a) v->custom[n++] = (step_t){ PENTA[b], 0, 11, 10, 4, 0 };
        if (c != a && c != b) v->custom[n++] = (step_t){ PENTA[c], 0, 11, 10, 4, 0 };
        v->custom[n - 1].frames = 12;
        start(0, v->custom, n);
    }
    if (sfx & VSFX_FLIPPER) start(2, SEQ_FLIPPER, 1);
    if (sfx & VSFX_KICK) start(2, SEQ_KICK, 1);
    if (sfx & VSFX_BALL) start(1, SEQ_BALL, 1);
    if (sfx & VSFX_MESSAGE) start(1, SEQ_MESSAGE, 2);
    if (sfx & VSFX_DRAIN) start(1, SEQ_DRAIN, 1);
    if (sfx & VSFX_START) start(1, SEQ_START, 4);
    /* zuletzt, damit nichts sie ueberdeckt (der Stoss selbst hat gerade Kanal C belegt) */
    if (sfx & VSFX_TILT) { start(1, SEQ_TILT_SAW, 1); start(0, SEQ_TILT_SQ, 1); start(2, SEQ_TILT_END, 2); synth_hold = 100; }
    else if (sfx & VSFX_WARN) { start(1, SEQ_WARN_SAW, 3); start(0, SEQ_WARN_SQ, 3); synth_hold = 28; }
}

void sound_frame(int load_ok)
{
    uint32_t now = plat_micros();
    acc_us += now - last_us;
    last_us = now;
    if (acc_us > 100000) acc_us = 100000;
    while (acc_us >= 16667) { acc_us -= 16667; tick(); }
    if (pcm_on) {
        /* Die Karte bekommt je Bild hoechstens 4 ms, und nur EINER liest: erst das laufende Lied, sonst das Nachladen der
         * Aufnahmen. Mehr (frueher im Menue 15 + 5 ms) halbierte die Bildrate, solange geladen wurde - das Menue flackerte,
         * "bis es sich eingegroovt hatte". */
        if (song_n >= 0) pcm_song_volume(256 * g_set.music_vol / 10);   /* nicht fuer die Aufnahmen des Vorspanns */
        /* load_ok = 0 (Vorspann): die Aufnahmen werden erst danach geladen - ein Kartenzugriff, der laenger dauert als sein
         * Budget, liess dort das Bild kurz aussetzen */
        if (!pcm_song_step(4000u) && load_ok) pcm_load_step(4000u);
    }
    if (pcm_started > 0) plat_audio_pump(render_cb);                 /* auch stumm weiter fuettern, siehe render_cb */
}
