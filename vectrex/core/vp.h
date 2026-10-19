/*
 * vp.h - Vector Pinball, plattformneutraler Spielkern in C.
 *
 * Portiert aus dem Java-Original (com.dozingcatsoftware.vectorpinball.model / .elements / .fields).
 * Der Kern kennt weder Grafik noch Dateien noch Uhr: Tischdaten kommen als Konstanten
 * (core/gen/table_data.c, erzeugt von tools/tables2c.py), die Zeit als Parameter von vp_advance().
 *
 * Einheiten wie im Original: Feldeinheiten (Tisch 20 x 30), Sekunden Spielzeit (= Echtzeit mal
 * targetTimeRatio des Tisches), Winkel im Bogenmass, Farben als ARGB.
 */
#ifndef VP_H
#define VP_H

#include <stdint.h>
#include <stddef.h>

#define VP_MAX_BALLS    6
#define VP_MAX_ELEMS    128
#define VP_MAX_ROLLS    40
#define VP_MAX_ACTIONS  64
#define VP_MAX_SHAPES   160
#define VP_MAX_EVENTS   16
#define VP_MAX_TOUCH    6
#define VP_MAX_PLAYERS  4
#define VP_FLASH_TIME   0.7f        /* Sekunden Spielzeit, die ein getroffener Bumper/Kicker nachleuchtet (klingt ab) */

/* ---- Tischbeschreibung (Konstanten) ---- */
enum { VP_WALL, VP_ARC, VP_PATH, VP_BUMPER, VP_DROP, VP_ROLL, VP_SENSOR, VP_SPINNER, VP_FLIPPER };

enum {
    VF_KILL = 1, VF_RETRACT_HIT = 2, VF_DISABLED = 4, VF_IGNORE_BALL = 8, VF_TOGGLE_OFF = 16, VF_CYCLE = 32,
    VF_HAS_COLOR = 64, VF_HAS_INACT = 128, VF_HAS_OUTER = 256, VF_HAS_INACT_OUTER = 512,
    VF_LAYER_TO = 1024, VF_LAYER_FROM = 2048
};

typedef struct { float x1, y1, x2, y2; } vp_seg_t;
/* In den Tischdaten liegen Strecken und Bogenpunkte als int16 in 1/1024 Feldeinheit: das Abbild laeuft auf dem RP2350
 * komplett aus dem RAM, und die ~5000 Strecken aller Tische kosten so 36 KB weniger. Zugriff ueber vp_seg / VP_CQ. */
typedef struct { int16_t x1, y1, x2, y2; } vp_cseg_t;
#define VP_CQ (1.0f / 1024.0f)
typedef struct { float x, y, r; uint32_t color; int32_t score; float reset; uint8_t has_color; } vp_roll_def_t;

/* p[] je Typ:
 *   WALL    kick, restitution, friction
 *   ARC     restitution, friction, cx, cy, rx, ry, a0, a1
 *   PATH    restitution, friction
 *   BUMPER  cx, cy, radius, outerRadius, kick
 *   DROP    reset (s)
 *   SENSOR  xmin, ymin, xmax, ymax, layerTo, layerFrom
 *   SPINNER cx, cy, radius, minSpeed, baseCps, minCps, decay
 *   FLIPPER cx, cy, length (negativ = dreht um das rechte Ende), minangle, maxangle, upspeed, downspeed */
typedef struct {
    uint8_t  type;
    int8_t   layer;
    uint16_t flags;
    const char *id;
    uint32_t color, inact_color, outer_color, inact_outer_color;
    int32_t  score;
    uint16_t first, count;      /* Strecken (Wand, Bogen, Pfad, Ziele) bzw. Rollover */
    uint16_t dfirst, dcount;    /* Zeichenpunkte (nur Bogen) */
    float    p[8];
} vp_elem_def_t;

typedef struct {
    const char *name;
    uint8_t  delegate, numballs;
    float    width, height, gravity, ratio, ball_radius;
    uint32_t ball_color, ball_color2;
    float    launch_x, launch_y, launch_vx, launch_vy, launch_dvx, launch_dvy;
    float    dead[4];
    uint8_t  has_dead;
    int32_t  mercy_ms, saver_ms, preserve_ms;
    const vp_elem_def_t *elems;   uint16_t nelem;
    const vp_cseg_t *segs;        uint16_t nseg;
    const vp_roll_def_t *rolls;   uint16_t nroll;
    const int16_t (*dpts)[2];     uint16_t ndpt;
    const uint16_t *order;        uint16_t norder;   /* Zeichenreihenfolge: Element | 0x8000 = rueckwaerts */
} vp_table_def_t;

static inline vp_seg_t vp_seg(const vp_table_def_t *t, int i)
{
    const vp_cseg_t *c = &t->segs[i];
    const vp_seg_t s = { c->x1 * VP_CQ, c->y1 * VP_CQ, c->x2 * VP_CQ, c->y2 * VP_CQ };
    return s;
}

extern const vp_table_def_t vp_tables[];
/* Deckel (tools/lids.py -> core/gen/lid_data.c): Flaechen eines Tisches, in die der Ball nie kommt - massive Bloecke.
 * vp_lid_first[t] .. vp_lid_first[t + 1] - 1 sind die Deckel von Tisch t. */
typedef struct { uint16_t first; uint8_t n; int16_t kick; } vp_lid_t;
extern const int16_t  vp_lid_pts[];
extern const vp_lid_t vp_lids[];
extern const uint8_t  vp_lid_first[];
extern const int vp_num_tables;

/* ---- Laufzeitzustand ---- */
typedef struct {
    float    x, y, vx, vy;
    float    ang, angvel;
    float    gscale;                 /* Schwerkraftfaktor (Tisch 7 haelt den Ball kurz fest) */
    uint32_t color, color2;
    int16_t  last_sensor;            /* Element des zuletzt beruehrten Sensors, -1 = keiner */
    int16_t  prev_sensor;            /* Tisch 5 fuehrt eine eigene Liste (wird bei Ballverlust geleert) */
    uint16_t serial;
    int8_t   layer;
    uint8_t  ntouch;
    uint16_t touch[VP_MAX_TOUCH];    /* Schluessel der im letzten Schritt beruehrten Koerper */
    uint8_t  touch_e[VP_MAX_TOUCH], touch_sub[VP_MAX_TOUCH];   /* dazu Element und Teil (fuer die Meldung beim Loslassen) */
} vp_ball_t;

typedef struct {
    vp_seg_t seg;                    /* WALL: aktuelle Lage (Waende koennen bewegt werden) */
    uint32_t new_color, new_outer;
    uint32_t mask;                   /* DROP: Bit = Ziel getroffen; ROLL: Bit = Rollover aktiv */
    uint32_t prev_hit;               /* ROLL: im letzten Takt beruehrt; DROP: zuletzt getroffenes Ziel (blitzt) */
    int32_t  score;
    float    flash;                  /* Sekunden Spielzeit, in denen das Element aufblitzt */
    float    layer_frac;
    float    a, w;                   /* FLIPPER: Gelenkwinkel, Winkelgeschwindigkeit; SPINNER: cps, Zeit bis zum naechsten Wechsel */
    float    motor, macc;            /* FLIPPER: Sollgeschwindigkeit des Motors (im Gelenksinn), verbrauchter Motorimpuls */
    float    cx, cy;                 /* BUMPER: aktuelle Mitte; FLIPPER: cx = Gelenkwinkel, bei dem der Schlag begann */
    float    t;                      /* SPINNER: Zeit seit der letzten Punkteanzeige; FLIPPER: Impuls des Anschlags im laufenden Schritt */
    uint8_t  retracted, visible, ignore_ball, has_new_color, has_new_outer, st;
} vp_estate_t;

typedef struct { float x, y, r; uint32_t color; uint8_t has_color; } vp_roll_t;

enum { VS_LINE, VS_CIRCLE, VS_DISC, VS_ARC };
typedef struct { uint8_t kind; uint32_t color; float v[6]; } vp_shape_t;   /* LINE x1,y1,x2,y2; CIRCLE/DISC cx,cy,r; ARC cx,cy,rx,ry,a0,a1 */

enum { VSFX_START = 1, VSFX_BALL = 2, VSFX_FLIPPER = 4, VSFX_SCORE = 8, VSFX_MESSAGE = 16, VSFX_ROLLOVER = 32, VSFX_DRAIN = 64, VSFX_KICK = 128,
       VSFX_WARN = 256, VSFX_TILT = 512 };   /* Tilt-Warnung, Tilt */

struct vp_field;
typedef void (*vp_action_fn)(struct vp_field *f, int arg);
typedef struct { double at; vp_action_fn fn; int arg; uint32_t seq; } vp_action_t;

typedef struct {
    void (*init)(struct vp_field *f);                    /* nach dem Laden des Tisches (noch kein Spiel) */
    void (*game_started)(struct vp_field *f);
    void (*ball_lost)(struct vp_field *f);
    void (*tick)(struct vp_field *f, float dt);
    void (*collision)(struct vp_field *f, int e, int sub, vp_ball_t *b);
    void (*drop_all)(struct vp_field *f, int e, vp_ball_t *b);
    void (*roll_all)(struct vp_field *f, int e, vp_ball_t *b);
    void (*spinner)(struct vp_field *f, int e, vp_ball_t *b);
    void (*sensor)(struct vp_field *f, int e, vp_ball_t *b);
} vp_delegate_t;

typedef struct { uint16_t serial; uint8_t e, sub; } vp_event_t;

#define VP_GRID_W 12
#define VP_GRID_H 16
#define VP_GRID_CELL 2.0f
#define VP_GRID_ITEMS 3400      /* groesster Bedarf: Tisch 9 mit 3307 (wird beim Aufbau begrenzt) */

typedef struct vp_field {
    const vp_table_def_t *def;
    const vp_delegate_t *dg;
    int table;

    vp_estate_t es[VP_MAX_ELEMS];
    vp_roll_t   rolls[VP_MAX_ROLLS];
    vp_ball_t   balls[VP_MAX_BALLS];
    int nballs;
    uint16_t next_serial;

    /* Raster fuer die Strecken: je Zelle eine Liste (Element << 6 | Teil ist zu knapp, darum zwei Felder) */
    uint16_t cell_start[VP_GRID_W * VP_GRID_H + 1];
    uint16_t cell_seg[VP_GRID_ITEMS];      /* Index der Strecke in def->segs */
    uint8_t  cell_elem[VP_GRID_ITEMS];     /* zugehoeriges Element */
    uint8_t  n_flip, n_bump, n_wall;
    uint8_t  flip[8], bump[16], walls[80];   /* Waende werden immer geprueft (sie koennen wandern) */
    float    flip_drop[8];           /* REALISTIC: um so viel sitzt der Drehpunkt tiefer (Mulde fuer den Post Pass, vp_phys_build) */
    int16_t  flip_apron[8];          /* dazu die Wand, deren Ende neben dem Drehpunkt mit nach unten rueckt (Auslauf bleibt offen), -1 = keine */
    uint8_t  flip_apron_end[8];      /* 0 = Anfang, 1 = Ende der Wand */
    vp_seg_t flip_step[8];           /* REALISTIC: Wandstueck vom Ende der Einlaufwand hinunter zum tieferen Drehpunkt (schliesst die Stufe) */
    uint8_t  n_step;                 /* so viele davon sind aktiv (0 in ARCADE) */

    /* Spielstand (GameState) */
    uint8_t in_progress, unlimited, used_mercy;
    int     ball_number, extra_balls, total_balls;
    int64_t score;
    double  mult;

    double  time;                    /* Spielzeit in Sekunden */
    float   step_acc;                /* noch nicht simulierte Spielzeit */
    int     sub_count;
    uint32_t now_ms;                 /* Echtzeit, vom Aufrufer */
    double  t_ball_start, t_multi_start, t_last_launch, t_last_mult;   /* < 0 = nie */
    int32_t lost_ms;                 /* Echtzeit des letzten Ballverlusts, -1 = keiner */

    vp_action_t actions[VP_MAX_ACTIONS];
    int nactions;
    uint32_t action_seq;

    char     msg[48];
    uint32_t msg_until;              /* Echtzeit; 0 = keine Meldung */

    float   last_x[VP_MAX_BALLS], last_y[VP_MAX_BALLS];
    int     n_last;
    float   stuck_time;

    vp_shape_t shapes[VP_MAX_SHAPES];
    int nshapes;

    vp_event_t events[VP_MAX_EVENTS];
    int nevents;

    uint32_t sfx;                    /* VSFX_*, sammelt sich an, der Aufrufer loescht */
    uint32_t score_hits;             /* Zaehler der Punkte-Toene (fuer die Musik wie im Original) */
    uint32_t rng;

    uint8_t  flip_left, flip_right;  /* gewuenschter Zustand der Flipper */

    /* Mehrere Spieler abwechselnd, wie am echten Flipper: jeder hat Punkte, Ballnummer, Multiplikator und Freibaelle fuer sich;
     * score/mult/ball_number/extra_balls oben gelten fuer den Spieler, der gerade dran ist. Der Tisch selbst wird geteilt. */
    uint8_t  nplayers, player;
    struct { int64_t score; double mult; int ball_number, extra_balls; uint8_t done; } pl[VP_MAX_PLAYERS];

    /* Erweiterungen gegenueber dem Original */
    uint8_t  real_physics;           /* 1 = Rueckprall nimmt mit der Aufprallgeschwindigkeit ab, Bumper streuen leicht */
    uint8_t  tilted;                 /* 1 = Tisch gekippt: Flipper tot, bis der Ball verloren ist */
    float    tilt_level;             /* steigt mit jedem Stoss gegen den Tisch, klingt ab */
    float    launch_scale;           /* Staerke des naechsten Abschusses (Stoessel), 0 = voll */
    float    nudge_scale;            /* Staerke des Stosses gegen den Tisch, 0 = voll (1,0) */
    float    nudge_tilt;             /* wie stark der naechste Stoss den Tilt reizt, 0 = voll (1,0) */
    float    flip_scale;             /* Schlagkraft der Flipper (Faktor auf die Schlaggeschwindigkeit), 0 = 1,0 */
    float    slope_scale;            /* Neigung des Tisches (Faktor auf die Schwerkraft), 0 = 1,0 - kommt zur festen Abstimmung des
                                      * Tisches (vp_table_slope) noch dazu */
    float    launch_k;               /* Staerke des Stoessels auf diesem Tisch, 0 = 1,0 */
    uint8_t  tilt_sens;              /* wie leicht ein Stoss das Tilt-Pendel ausloest: 0 = nur bei schnellen Stoessen, 2 = jeder Stoss */
    uint8_t  tilt_warnings;          /* Warnungen bei diesem Ball: zwei sind frei, die dritte Ausloesung ist der Tilt */
    uint32_t save_ms;                /* Ball gerettet: zu dieser Zeit (now_ms) wird er wieder eingeschossen, 0 = nichts steht aus */
    float    nudge_cool;             /* Sekunden Spielzeit bis zum naechsten erlaubten Stoss */

} vp_field_t;   /* der Zustand der Tischlogik liegt in den Dateien vp_tableN.c (es gibt nur ein Feld) */

/* ---- Schnittstelle fuer das Spiel ---- */
void vp_load(vp_field_t *f, int table, uint32_t seed);     /* Tisch laden, noch kein Spiel */
void vp_start_game(vp_field_t *f, int unlimited);          /* Spielerzahl vorher in f->nplayers (0 = 1) */
int64_t vp_player_score(const vp_field_t *f, int player);
void vp_end_game(vp_field_t *f);
/* Spiel um real_dt Sekunden Echtzeit weiterfuehren (fester Simulationsschritt, Rest wird aufgehoben).
 * speed = Faktor auf die Spielgeschwindigkeit des Tisches (1.0 = wie im Original). */
void vp_advance(vp_field_t *f, float real_dt, uint32_t now_ms, float speed);
void vp_set_flippers(vp_field_t *f, int left, int right);
int  vp_launch_if_needed(vp_field_t *f);                   /* Knopfdruck: Ball starten, falls keiner im Spiel ist */
float vp_slope(const vp_field_t *f);                       /* wirksame Neigung: feste Abstimmung des Tisches mal slope_scale */
float vp_table_flip(const vp_field_t *f);                  /* feste Schlagkraft des Tisches (REALISTIC), Faktor auf die Nenndrehzahl */
int  vp_launch_ready(const vp_field_t *f);                 /* 1 = ein Abschuss wuerde jetzt einen Ball starten */
int  vp_launch_power(vp_field_t *f, float power);          /* wie vp_launch_if_needed, Staerke 0..1 (Stoessel) */
int  vp_catch_returned_ball(vp_field_t *f);                /* zu schwach geschossen: Ball faellt zurueck auf den Stoessel; 1 = gefangen */
/* Stoss gegen den Tisch (nicht im Original): dir -1 = nach links, +1 = nach rechts, 0 = nach oben.
 * Liefert 1, wenn gestossen wurde. Zu viele Stoesse kurz hintereinander kippen den Tisch (TILT). */
int  vp_nudge(vp_field_t *f, int dir);
/* Ballposition fuer die Anzeige: um die noch nicht simulierte Restzeit vorausgerechnet */
void vp_ball_draw_pos(const vp_field_t *f, const vp_ball_t *b, float *x, float *y);
int  vp_flipper_engaged(const vp_field_t *f, int e);
/* Lage eines Flippers als Strecke (Drehpunkt -> Spitze) */
void vp_flipper_line(const vp_field_t *f, int e, float *x1, float *y1, float *x2, float *y2);
float vp_flipper_top(const vp_field_t *f, int e);          /* Gelenkwinkel am oberen Anschlag (gespiegelte Flipper: negativ) */
void  vp_flipper_pivot(const vp_field_t *f, int e, float *x, float *y);   /* Drehpunkt (in REALISTIC ggf. tiefer als im Tisch) */
void  vp_set_real_physics(vp_field_t *f, int on);         /* ARCADE/REALISTIC waehlen (nach vp_load; passt die Flippermulde an) */

/* ---- Schnittstelle fuer die Tischlogik (vp_tableN.c) ---- */
int   vp_find(const vp_field_t *f, const char *id);        /* Elementindex oder -1 */
int   vp_is(const vp_field_t *f, int e, const char *id);
int   vp_starts(const vp_field_t *f, int e, const char *prefix);
void  vp_schedule(vp_field_t *f, int ms, vp_action_fn fn, int arg);
void  vp_message(vp_field_t *f, const char *text, int ms, int sound);
void  vp_messagef(vp_field_t *f, int ms, const char *fmt, ...);
int64_t vp_add_score(vp_field_t *f, int64_t points);       /* mal Multiplikator; liefert die addierten Punkte */
void  vp_inc_multiplier(vp_field_t *f, int ms);
vp_ball_t *vp_launch_ball(vp_field_t *f);
vp_ball_t *vp_create_ball(vp_field_t *f, float x, float y);
void  vp_update_launch_times(vp_field_t *f);
void  vp_remove_ball(vp_field_t *f, vp_ball_t *b);         /* mit Ballverlust-Behandlung */
void  vp_remove_ball_quiet(vp_field_t *f, vp_ball_t *b);   /* ohne */
vp_ball_t *vp_ball_by_serial(vp_field_t *f, int serial);
void  vp_ball_impulse(vp_ball_t *b, float ix, float iy);
uint32_t vp_rand(vp_field_t *f);
float vp_randf(vp_field_t *f);                             /* 0 <= x < 1 */

void  vp_wall_retract(vp_field_t *f, int e, int retracted);
void  vp_wall_set(vp_field_t *f, int e, float x1, float y1, float x2, float y2);   /* Start + Richtung, Laenge bleibt */
int   vp_drop_all_hit(const vp_field_t *f, int e);
void  vp_drop_reset(vp_field_t *f, int e);
int   vp_roll_all(const vp_field_t *f, int e);
int   vp_roll_active(const vp_field_t *f, int e, int i);
void  vp_roll_set(vp_field_t *f, int e, int i, int active);
void  vp_roll_set_all(vp_field_t *f, int e, int active);
void  vp_roll_activate_first(vp_field_t *f, int e);
vp_roll_t *vp_roll(vp_field_t *f, int e, int i);
void  vp_set_color(vp_field_t *f, int e, int has, uint32_t c);
void  vp_set_outer(vp_field_t *f, int e, int has, uint32_t c);
void  vp_shape_line(vp_field_t *f, float x1, float y1, float x2, float y2, uint32_t c);
void  vp_shape_circle(vp_field_t *f, float cx, float cy, float r, int solid, uint32_t c);
void  vp_shape_arc(vp_field_t *f, float cx, float cy, float rx, float ry, float a0_deg, float a1_deg, uint32_t c);

/* Farben wie im Original (ARGB) */
#define VP_RGB(r, g, b)      (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define VP_RGBA(r, g, b, a)  (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
uint32_t vp_blend(uint32_t a, uint32_t b, float frac);
uint32_t vp_with_alpha(uint32_t c, int a);
/* Farbe, mit der ein Element gerade zu zeichnen ist (Ebenenwechsel, Aufblitzen eingerechnet);
 * *flash = 1, wenn es aufblitzt (das Original invertiert dann die Farbe). */
uint32_t vp_elem_color(const vp_field_t *f, int e, uint32_t def_color, int *flash);
uint32_t vp_elem_outer_color(const vp_field_t *f, int e, int *flash);
int  vp_has_ball_at_layer(const vp_field_t *f, int layer);

/* intern: Physik (vp_phys.c) */
void vp_phys_build(vp_field_t *f);
void vp_phys_step(vp_field_t *f, float h);
/* intern: Ereignis nach einem Stoss (vp_field.c) */
void vp_on_collision(vp_field_t *f, vp_ball_t *b, int e, int sub);

const vp_delegate_t *vp_delegate_for(int n);

#endif
