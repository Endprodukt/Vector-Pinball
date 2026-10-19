#include "pcm.h"
#include "plat.h"
#include "minimp3.h"
#include <stdio.h>
#include <string.h>

#define SFX_VOICES 8
#define VOICES     (SFX_VOICES + 2)          /* 8 Effekte + Schlagzeug + Flaeche */
#define MASTER     150                       /* von 256: Luft nach oben, wenn mehrere Glocken und die Musik zusammenkommen */

/* Aufteilung des grossen Speichers: vorn die fest geladenen Aufnahmen, hinten ein Ringpuffer fuer das Lied, das gerade von der
 * Karte gestreamt wird (eigene Lieder koennen beliebig lang sein), dazwischen ein kleiner Platz fuer den Dateikopf. */
#define RING_SIZE  (512u * 1024u)            /* Zweierpotenz; 512 KB = 4 s bei 32 kHz stereo, knapp 3 s bei 44,1 kHz stereo */
#define HEAD_SIZE  4096u

static const char *const FILES[SND_N] = {
    "flipper.wav", "launch.wav", "message.wav", "start.wav", "rollover.wav",
    "ding1.wav", "ding2.wav", "ding3.wav", "ding4.wav", "ding5.wav", "ding6.wav",
    "pad.wav", "drums.wav",
};

typedef struct { uint32_t data_off, frames, rate; uint8_t ch; } wav_t;
typedef struct { const uint8_t *data; uint32_t frames, rate; uint8_t ch; } snd_t;
static snd_t snd[SND_N];

typedef struct {
    const uint8_t *pcm;      /* 16 Bit little endian; byteweise gelesen (die Daten muessen nicht ausgerichtet liegen) */
    uint32_t len, pos;       /* in Abtastschritten (Frames) */
    uint32_t frac, step;     /* Q16 */
    int32_t  vol;            /* 0..256 */
    int32_t  gl, gr;         /* Anteil links/rechts, 0..256 */
    uint8_t  ch, loop;
    volatile int live;
    uint32_t age;
} voice_t;

static voice_t v[VOICES];
/* Ducking: mit jedem Effekt geht die Musik (Lied, Schlagzeug, Flaeche) kurz ein Stueck zurueck und kommt weich wieder -
 * wie ein Kompressor mit Seitenkette. So ueberlaedt die Summe nicht, und die Effekte bleiben vorn. */
#define DUCK_DEPTH   165                     /* von 256: etwa -4 dB */
#define DUCK_HOLD    (PCM_RATE / 6)          /* so lange unten bleiben (Abtastwerte) */
#define DUCK_ATTACK  24                      /* je Mischblock nach unten: in ~30 ms unten, ohne Knack */
#define DUCK_RELEASE (PCM_RATE * 6 / 10)     /* in 0,6 s wieder oben */
static volatile int duck_hold;
static int duck = 256;
static const uint8_t *arena;
static uint32_t arena_size, sfx_limit, head_off, ring_off, arena_off, cur_size, cur_got, age;
static int cur, opened, loaded;              /* cur: Datei, die gerade geladen wird (SND_N = fertig) */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

void pcm_init(void)
{
    arena = plat_arena(&arena_size);
    if (arena && arena_size < RING_SIZE + HEAD_SIZE + 65536u) arena = NULL;
    if (arena) { ring_off = arena_size - RING_SIZE; head_off = ring_off - HEAD_SIZE; sfx_limit = head_off; }
    arena_off = 0; cur = arena ? 0 : SND_N; opened = 0; loaded = 0;
    memset(snd, 0, sizeof snd);
}

/* WAV-Kopf lesen: nur unkomprimiert, 16 Bit, 1 oder 2 Kanaele. p = die ersten n Bytes der Datei, size = Dateilaenge.
 * Alles andere wird abgelehnt und nicht gespielt - falsch gedeutete Daten waeren auf einem 16-Bit-Ausgang lautes Rauschen. */
static int wav_info(const uint8_t *p, uint32_t n, uint32_t size, wav_t *o)
{
    uint32_t at = 12, rate = 0, ch = 0, bits = 0, fmt = 0;
    if (n < 44 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return 0;
    while (at + 8 <= n) {
        uint32_t len = rd32(p + at + 4);
        if (!memcmp(p + at, "fmt ", 4) && len >= 16 && at + 24 <= n) {
            fmt = rd16(p + at + 8); ch = rd16(p + at + 10); rate = rd32(p + at + 12); bits = rd16(p + at + 22);
        } else if (!memcmp(p + at, "data", 4)) {
            if (at + 8 > size) return 0;
            if (len > size - at - 8) len = size - at - 8;
            if ((fmt != 1 && fmt != 0xFFFE) || bits != 16 || (ch != 1 && ch != 2) || rate < 4000 || rate > 96000) return 0;
            o->data_off = at + 8; o->frames = len / (2 * ch); o->rate = rate; o->ch = (uint8_t)ch;
            return o->frames > 0;
        }
        at += 8 + len + (len & 1);
    }
    return 0;
}

void pcm_load_step(unsigned budget_us)
{
    char path[40];
    if (cur >= SND_N) return;
    if (!opened) {
        snprintf(path, sizeof path, "snd/%s", FILES[cur]);
        if (!plat_stream_open(0, path, &cur_size) || cur_size < 44 || arena_off + cur_size > sfx_limit) { cur++; return; }   /* fehlt oder passt nicht mehr */
        opened = 1; cur_got = 0;
    }
    uint32_t n = plat_stream_read(0, arena_off + cur_got, cur_size - cur_got, budget_us);
    cur_got += n;
    if (cur_got >= cur_size) {
        wav_t w;
        if (wav_info(arena + arena_off, cur_size, cur_size, &w)) {
            snd[cur].data = arena + arena_off + w.data_off; snd[cur].frames = w.frames; snd[cur].rate = w.rate; snd[cur].ch = w.ch;
            loaded++;
        }
        arena_off += (cur_size + 3u) & ~3u;
        opened = 0; cur++;
    } else if (n == 0) { opened = 0; cur++; }                         /* Lesefehler: diese Aufnahme auslassen */
}

int pcm_status(int *loading, int *memory)
{
    *loading = cur < SND_N;
    *memory = arena != NULL;
    return loaded;
}

int pcm_have(void) { return arena && (loaded > 0 || cur < SND_N); }
int pcm_ready(int idx) { return idx >= 0 && idx < SND_N && snd[idx].data != NULL; }

static void start(voice_t *o, int idx, int vol, uint32_t pitch, int loop, int pan)
{
    if (!pcm_ready(idx)) return;
    o->live = 0;
    o->pcm = snd[idx].data; o->len = snd[idx].frames; o->ch = snd[idx].ch; o->pos = 0; o->frac = 0;
    o->step = (uint32_t)(((uint64_t)snd[idx].rate * pitch) / PCM_RATE);
    o->vol = vol; o->age = ++age; o->loop = (uint8_t)loop;
    if (pan < 0) pan = 0;
    if (pan > 256) pan = 256;
    o->gl = pan <= 128 ? 256 : 2 * (256 - pan);
    o->gr = pan >= 128 ? 256 : 2 * pan;
    o->live = 1;
}

void pcm_play(int idx, int vol, uint32_t pitch, int pan)
{
    int best = 0;
    for (int i = 0; i < SFX_VOICES; i++) {
        if (!v[i].live) { best = i; break; }
        if (v[i].age < v[best].age) best = i;                        /* sonst die aelteste Stimme */
    }
    start(&v[best], idx, vol, pitch, 0, pan);
    duck_hold = DUCK_HOLD;
}

static voice_t *music_voice(int idx) { return &v[SFX_VOICES + (idx == SND_PAD ? 1 : 0)]; }
int  pcm_music_playing(int idx) { return music_voice(idx)->live; }
void pcm_music_start(int idx, int vol, int loop) { if (!music_voice(idx)->live) start(music_voice(idx), idx, vol, 65536, loop, 128); }
void pcm_music_stop(void) { v[SFX_VOICES].live = 0; v[SFX_VOICES + 1].live = 0; }
void pcm_stop_all(void) { for (int i = 0; i < VOICES; i++) v[i].live = 0; pcm_song_stop(); }

static int16_t clip16(int32_t m) { return (int16_t)(m > 32767 ? 32767 : (m < -32768 ? -32768 : m)); }

/* ---- Lied von der Karte (eigene Musik je Tisch und fuer das Menue) ---- */
static struct {
    volatile int on;
    int loop, vol;
    char path[64];
    uint32_t rate, ch, frame;            /* frame = Bytes je Abtastschritt */
    uint32_t data_len, left;             /* Bytes der Aufnahme, davon in diesem Durchlauf noch nicht gelesen */
    volatile uint32_t wr, rd;            /* je in Bytes, laufen durch; der Ring-Index ist der Rest modulo RING_SIZE */
    uint32_t frac, step;
    uint8_t  blk[512];                   /* ein Stueck aus dem Ring, zum Mischen in den normalen Speicher geholt */
    uint32_t blk_frames, blk_pos;
    /* MP3: im Ring liegen die Bytes der Datei; dekodiert wird Rahmen fuer Rahmen beim Mischen */
    int      mp3;
    volatile uint32_t skip_at, skip_left;   /* ID3-Kopf: ab dieser Ringstelle so viele Bytes ueberspringen */
    uint32_t in_len;
} song;

#define MP3_IN 2048u                     /* groesster Rahmen 1441 Byte + der Kopf des naechsten */
static mp3dec_t mp3_dec;
static uint8_t  mp3_in[MP3_IN];
static int16_t  mp3_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

static int is_mp3_name(const char *p)
{
    size_t l = strlen(p);
    return l > 4 && p[l - 4] == '.' && (p[l - 3] | 32) == 'm' && (p[l - 2] | 32) == 'p' && p[l - 1] == '3';
}

/* Datei (neu) oeffnen, Kopf lesen, die ersten Daten in den Ring legen. Auch der Sprung an den Anfang beim Wiederholen. */
static int song_pass(void)
{
    uint8_t hdr[1024];
    uint32_t size, n;
    wav_t w;
    if (!plat_stream_open(1, song.path, &size)) return 0;
    n = plat_stream_read(1, head_off, size < HEAD_SIZE ? size : HEAD_SIZE, 2500u);   /* kurz: der Rest kommt mit pcm_song_step */
    if (n < 44) return 0;
    plat_arena_get(head_off, hdr, n < sizeof hdr ? n : (uint32_t)sizeof hdr);
    if (song.mp3) {
        /* Die ganze Datei geht in den Ring. Ein ID3v2-Kopf (Titel, oft ein Bild von einigen 100 KB) wird beim Lesen
         * uebersprungen statt durchsucht - in Bilddaten faende der Dekoder sonst falsche Rahmen. */
        uint32_t skip = 0;
        if (!memcmp(hdr, "ID3", 3))
            skip = 10u + (((uint32_t)hdr[6] & 127u) << 21 | ((uint32_t)hdr[7] & 127u) << 14 | ((uint32_t)hdr[8] & 127u) << 7 | ((uint32_t)hdr[9] & 127u));
        if (skip >= size) return 0;
        song.skip_at = song.wr; song.skip_left = skip;
        w.data_off = 0; w.frames = size;
        song.frame = 1;
        song.data_len = size;
    } else {
        if (!wav_info(hdr, n < sizeof hdr ? n : (uint32_t)sizeof hdr, size, &w)) return 0;
        song.rate = w.rate; song.ch = w.ch; song.frame = 2u * w.ch;
        song.data_len = w.frames * song.frame;
        song.step = (uint32_t)(((uint64_t)w.rate << 16) / PCM_RATE);
    }
    {   /* was vom Kopfstueck schon Daten sind, in den Ring umziehen */
        uint32_t have = n - w.data_off, at = w.data_off;
        if (have > song.data_len) have = song.data_len;
        have -= have % song.frame;
        song.left = song.data_len - have;
        while (have) {
            uint8_t tmp[256];
            uint32_t idx = song.wr & (RING_SIZE - 1), c = have > sizeof tmp ? (uint32_t)sizeof tmp : have;
            if (c > RING_SIZE - idx) c = RING_SIZE - idx;
            plat_arena_get(head_off + at, tmp, c);
            plat_arena_put(ring_off + idx, tmp, c);
            song.wr += c; at += c; have -= c;
        }
    }
    return 1;
}

int pcm_song_start(const char *path, int vol, int loop)
{
    pcm_song_stop();
    if (!arena) return 0;
    snprintf(song.path, sizeof song.path, "%s", path);
    song.loop = loop; song.vol = vol;
    song.wr = song.rd = 0; song.frac = 0; song.blk_frames = song.blk_pos = 0;
    song.mp3 = is_mp3_name(path); song.in_len = 0; song.skip_left = 0;
    if (song.mp3) { mp3dec_init(&mp3_dec); song.ch = 2; song.step = 1u << 16; }
    if (!song_pass()) return 0;
    song.on = 1;
    return 1;
}

void pcm_song_stop(void) { song.on = 0; }
void pcm_song_volume(int vol) { song.vol = vol; }
int  pcm_song_playing(void) { return song.on; }

int pcm_song_step(unsigned budget_us)
{
    if (!song.on) return 0;
    uint32_t fill = song.wr - song.rd;
    if (fill > RING_SIZE * 3u / 4u) return 0;
    if (song.left == 0) {
        if (song.loop) { if (!song_pass()) song.on = 0; return 1; }
        else if (fill < song.frame && song.in_len < 16) song.on = 0;
        return 0;
    }
    uint32_t idx = song.wr & (RING_SIZE - 1), want = RING_SIZE - fill - 64u;
    if (want > song.left) want = song.left;
    if (want > RING_SIZE - idx) want = RING_SIZE - idx;
    uint32_t n = plat_stream_read(1, ring_off + idx, want, budget_us);
    song.wr += n; song.left -= n;
    if (n == 0) song.left = 0;                                       /* Lesefehler: wie Dateiende behandeln */
    return 1;
}

/* Naechsten MP3-Rahmen dekodieren. 1 = mp3_pcm hat song.blk_frames neue Abtastwerte, 0 = gerade keine Daten da */
static int mp3_next(void)
{
    for (int tries = 0; tries < 8; tries++) {
        /* Eingang aus dem Ring nachfuellen */
        for (;;) {
            uint32_t avail = song.wr - song.rd;
            if (song.skip_left && song.rd == song.skip_at) {         /* ID3-Kopf an dieser Stelle */
                uint32_t d = song.skip_left < avail ? song.skip_left : avail;
                if (!d) break;
                song.rd += d; song.skip_at += d; song.skip_left -= d;
                continue;
            }
            uint32_t idx = song.rd & (RING_SIZE - 1), take = MP3_IN - song.in_len;
            if (take > avail) take = avail;
            if (take > RING_SIZE - idx) take = RING_SIZE - idx;
            if (song.skip_left && song.skip_at - song.rd < take) take = song.skip_at - song.rd;   /* nicht ueber den naechsten Kopf */
            if (!take) break;
            plat_arena_get(ring_off + idx, mp3_in + song.in_len, take);
            song.in_len += take; song.rd += take;
        }
        if (song.in_len < 16) return 0;
        mp3dec_frame_info_t info;
        int got = mp3dec_decode_frame(&mp3_dec, mp3_in, (int)song.in_len, mp3_pcm, &info);
        uint32_t used = (uint32_t)info.frame_bytes;
        if (!used) {                                                 /* kein ganzer Rahmen im Puffer */
            if (song.in_len < MP3_IN) return 0;                      /* es fehlt nur Nachschub */
            used = MP3_IN / 2;                                       /* Muell: halben Puffer verwerfen */
        }
        if (used > song.in_len) used = song.in_len;
        memmove(mp3_in, mp3_in + used, song.in_len - used);
        song.in_len -= used;
        if (got > 0 && info.hz >= 8000) {
            song.blk_frames = (uint32_t)got; song.ch = (uint32_t)info.channels;
            song.step = (uint32_t)(((uint64_t)info.hz << 16) / PCM_RATE);
            return 1;
        }
    }
    return 0;
}

static void song_mix_mp3(int16_t *left, int16_t *right, int n)
{
    const int32_t vol = song.vol * MASTER / 256 * duck / 256;
    for (int i = 0; i < n; i++) {
        while (song.blk_pos >= song.blk_frames) {
            song.blk_pos -= song.blk_frames;
            song.blk_frames = 0;
            if (!mp3_next()) { song.blk_pos = 0; return; }           /* Karte zu langsam oder Lied zu Ende: Stille */
        }
        const int16_t *p = mp3_pcm + song.blk_pos * song.ch;
        int32_t sl = p[0], sr = song.ch == 2 ? p[1] : sl;
        left[i] = clip16(left[i] + ((sl * vol) >> 8));
        right[i] = clip16(right[i] + ((sr * vol) >> 8));
        song.frac += song.step;
        song.blk_pos += song.frac >> 16; song.frac &= 0xFFFF;
    }
}

static void song_mix(int16_t *left, int16_t *right, int n)
{
    if (song.mp3) { song_mix_mp3(left, right, n); return; }

    const int32_t vol = song.vol * MASTER / 256 * duck / 256;
    for (int i = 0; i < n; i++) {
        while (song.blk_pos >= song.blk_frames) {                    /* naechstes Stueck aus dem Ring holen */
            song.blk_pos -= song.blk_frames;
            song.rd += song.blk_frames * song.frame;
            uint32_t avail = (song.wr - song.rd) / song.frame, idx = song.rd & (RING_SIZE - 1);
            uint32_t take = (uint32_t)sizeof song.blk / song.frame;
            if (take > avail) take = avail;
            if (take * song.frame > RING_SIZE - idx) take = (RING_SIZE - idx) / song.frame;
            song.blk_frames = take;
            if (!take) { song.blk_pos = 0; return; }                 /* Ring leer (Karte zu langsam): Stille statt Geknatter */
            plat_arena_get(ring_off + idx, song.blk, take * song.frame);
        }
        const uint8_t *p = song.blk + song.blk_pos * song.frame;
        int32_t sl = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        int32_t sr = song.ch == 2 ? (int16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8)) : sl;
        left[i] = clip16(left[i] + ((sl * vol) >> 8));
        right[i] = clip16(right[i] + ((sr * vol) >> 8));
        song.frac += song.step;
        song.blk_pos += song.frac >> 16; song.frac &= 0xFFFF;
    }
}

void pcm_mix(int16_t *left, int16_t *right, int n)
{
    if (duck_hold > 0) { duck_hold -= n; duck -= DUCK_ATTACK; if (duck < DUCK_DEPTH) duck = DUCK_DEPTH; }
    else if (duck < 256) { duck += n * (256 - DUCK_DEPTH) / DUCK_RELEASE + 1; if (duck > 256) duck = 256; }
    for (int k = 0; k < VOICES; k++) {
        voice_t *o = &v[k];
        if (!o->live) continue;
        const int32_t g = k >= SFX_VOICES ? MASTER * duck / 256 : MASTER;        /* nur die Musikstimmen ducken sich */
        const int32_t vl = o->vol * g / 256 * o->gl / 256, vr = o->vol * g / 256 * o->gr / 256;
        uint32_t pos = o->pos, frac = o->frac;
        for (int i = 0; i < n; i++) {
            if (pos >= o->len) {
                if (!o->loop) { o->live = 0; break; }
                pos = 0;
            }
            const uint8_t *p = o->pcm + 2 * o->ch * pos;
            int32_t sl = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
            int32_t sr = o->ch == 2 ? (int16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8)) : sl;
            left[i] = clip16(left[i] + ((sl * vl) >> 8));
            right[i] = clip16(right[i] + ((sr * vr) >> 8));
            frac += o->step;
            pos += frac >> 16; frac &= 0xFFFF;
        }
        o->pos = pos; o->frac = frac;
    }
    if (song.on) song_mix(left, right, n);
}
