/* nicht zeitkritisch: auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp (das Kit baut sonst mit -O3) */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
/*
 * vp_table10.c - Tisch 10 "Spinball": der Tisch des Vectrex-Flippers von 1983 (GCE).
 * Das Spielfeld stammt aus dem ROM (tools/spinball_map.py liest die Tabellen und schreibt tables/table10.json).
 * Die Regeln folgen Punkt fuer Punkt der Anleitung des Originals (liegt VIDE bei); der Code des ROMs ist dafuer NICHT
 * zerlegt worden - wo die Anleitung etwas offen laesst, steht hier, wie es gedeutet ist. Orte und Formen der blinkenden
 * Pfeile und des Sterns stammen aus den Zeichentabellen des ROMs ($13E9, $158E.., $1550).
 *
 *   Rauten    Die drei Rauten in den Kanaelen oben sind anfangs an. Durch einen Kanal mit Raute: 500 Bonus, die Raute geht
 *             aus, ein Bumperlicht geht an. Durch einen Kanal ohne Raute: 1500, kein Licht. Sind alle drei aus, gehen sie
 *             wieder an, und ein Ballretter-Pfeil leuchtet eine Weile (abwechselnd links und rechts).
 *   Bumper    15 Punkte; brennt sein Licht, 500 Bonus dazu und das Licht geht aus. Brennen alle vier Lichter zugleich:
 *             Multiplikator + 1 (hoechstens 6), die Lichter gehen aus.
 *   Spinner   300 Punkte je Durchfahrt. Mit blinkendem Bonuspfeil: 2000 Bonus, der Ballteiler wird unbefristet scharf,
 *             der Pfeil geht aus.
 *   Ziele     Fuenf Reihen. Reihe ganz gefallen: 100 Bonus je Ziel; der Reihe nach getroffen (von links nach rechts oder
 *             umgekehrt): 200 je Ziel, die Bonuspfeile der Spinner gehen an, und der Ballteiler wird fuer eine Weile scharf.
 *   Teiler    Der blinkende Stern in der Tischmitte. Ball darauf: aus einem Ball werden zwei (nie mehr als zwei), und
 *             zwischen den unteren Flippern erscheint der dreieckige Bumper. War der Teiler unbefristet scharf:
 *             Multiplikator + 1.
 *   Retter    In den beiden Aussenbahnen. Normal ist der Ball dort verloren; leuchtet der Pfeil, wird er zurueckgeworfen:
 *             1000 Bonus.
 *   Paare     Zielreihe (der Reihe nach) und danach der dazugehoerige Spinner mit Pfeil: 5000 dazu. Oben rechts - Spinner
 *             unten links, oben links - unten rechts, unten links - oben links, unten rechts - rechts Mitte. Alle fuenf
 *             Reihen bei diesem Ball gefallen und der dreieckige Bumper getroffen: 10 000.
 *
 * "Bonus" wird nach der Anleitung mit dem Multiplikator malgenommen, die einfachen Punkte (15, 300) nicht. Gedeutet: der
 * Bonus wird hier sofort gutgeschrieben (mal Multiplikator) statt gesammelt; die fuenfte Zielreihe (rechts unter dem
 * Dreieck an der Wand) gehoert zum Spinner rechts Mitte; "eine Weile" sind rund 12 Sekunden.
 */
#include "vp_deleg.h"

#define RK (20.0f / 192.0f)                  /* Feldeinheiten je ROM-Einheit, wie tools/spinball_map.py */
#define FX(x) (((x) + 96.0f) * RK)
#define FY(y) (((y) + 116.0f) * RK)
#define WHILE_S 28.0f                        /* "eine Weile" in Spielzeit, gut zwoelf Sekunden Echtzeit */
#define WHITE VP_RGB(255, 255, 255)

static const char *const DROP_ID[5] = { "DropTopRight", "DropTopLeft", "DropMidLeft", "DropMidRight", "DropLowRight" };
static const int8_t PARTNER[5] = { 1, 2, 0, 3, 3 };          /* Zielreihe -> Spinner (0 oben links, 1 unten links, 2 unten rechts, 3 rechts Mitte) */

static struct {
    int chutes, lights, splitter, bump[4], barrier[3], post[3], spin[4], drop[5], saver[2], ready;
    uint8_t in_chute;                        /* Bit je Kanal: gerade ein Ball darin */
    uint8_t arrows;                          /* Bit je Spinner: Bonuspfeil blinkt */
    uint8_t dir[5], sets_done;               /* je Reihe: 0 noch nichts, 1 aufsteigend, 2 absteigend, 3 durcheinander; gefallene Reihen */
    int8_t  last_set;                        /* Reihe, die die Pfeile angemacht hat */
    uint8_t split_on, split_forever, post_on, next_saver;
    float   split_left, saver_left[2];
} t10;

/* ---- Punkte ---- */
static void bonus(vp_field_t *f, int n, const char *what)
{
    vp_add_score(f, n);                                              /* mal Multiplikator */
    f->sfx |= VSFX_SCORE;
    if (what) vp_messagef(f, 1500, "%s %d", what, n);
}
static void points(vp_field_t *f, int n) { f->score += n; f->sfx |= VSFX_SCORE; }
static void mult_up(vp_field_t *f) { if (f->mult < 6) vp_inc_multiplier(f, 1500); }

/* ---- Schalter ---- */
static void t10_barrier(vp_field_t *f, int on) { for (int i = 0; i < 3; i++) vp_wall_retract(f, t10.barrier[i], !on); }
static void t10_post(vp_field_t *f, int on) { t10.post_on = (uint8_t)on; for (int i = 0; i < 3; i++) vp_wall_retract(f, t10.post[i], !on); }
static void t10_saver(vp_field_t *f, int k, int on)
{
    t10.saver_left[k] = on ? WHILE_S : 0;
    vp_wall_retract(f, t10.saver[k], !on);
}
static void t10_splitter(vp_field_t *f, int on, int forever)
{
    t10.split_on = (uint8_t)on; t10.split_forever = (uint8_t)(on && forever);
    t10.split_left = on && !forever ? WHILE_S : 0;
    f->es[t10.splitter].ignore_ball = (uint8_t)!on;
    vp_roll_set_all(f, t10.splitter, 0);
}

static void t10_reset_ball(vp_field_t *f)
{
    t10.in_chute = 0; t10.arrows = 0; t10.sets_done = 0; t10.last_set = -1;
    memset(t10.dir, 0, sizeof t10.dir);
    vp_roll_set_all(f, t10.chutes, 1);                               /* Rauten an */
    vp_roll_set_all(f, t10.lights, 0);
    t10_splitter(f, 0, 0);
    t10_post(f, 0);
    t10_saver(f, 0, 0); t10_saver(f, 1, 0);
    t10_barrier(f, 0);
}

static void t10_init(vp_field_t *f)
{
    char id[20];
    memset(&t10, 0, sizeof t10);
    t10.chutes = vp_find(f, "Chutes"); t10.lights = vp_find(f, "BumperLights"); t10.splitter = vp_find(f, "Splitter");
    t10.saver[0] = vp_find(f, "BallSaver-left"); t10.saver[1] = vp_find(f, "BallSaver-right");
    for (int i = 0; i < 4; i++) {
        snprintf(id, sizeof id, "Bumper%d", i + 1); t10.bump[i] = vp_find(f, id);
        snprintf(id, sizeof id, "Spinner%d", i + 1); t10.spin[i] = vp_find(f, id);
    }
    for (int i = 0; i < 5; i++) t10.drop[i] = vp_find(f, DROP_ID[i]);
    for (int i = 0; i < 3; i++) {
        snprintf(id, sizeof id, "LaunchBarrier%d", i + 1); t10.barrier[i] = vp_find(f, id);
        snprintf(id, sizeof id, "CenterPost%d", i + 1); t10.post[i] = vp_find(f, id);
    }
    f->es[t10.splitter].visible = 0;                                 /* gezeichnet wird der Stern (t10_shapes) */
    t10_reset_ball(f);
    t10.ready = 1;
}

static void t10_started(vp_field_t *f) { t10_init(f); }
static void t10_ball_lost(vp_field_t *f) { t10_reset_ball(f); }

static void t10_sensor(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    if (vp_is(f, e, "LaunchBarrierSensor")) t10_barrier(f, 1);
    else if (vp_is(f, e, "LaunchBarrierRetract")) t10_barrier(f, 0);
}

static void t10_collision(vp_field_t *f, int e, int sub, vp_ball_t *b)
{
    (void)b;
    for (int i = 0; i < 4; i++)
        if (e == t10.bump[i]) {
            points(f, 15);
            if (vp_roll_active(f, t10.lights, i)) { vp_roll_set(f, t10.lights, i, 0); bonus(f, 500, NULL); }
            return;
        }
    for (int k = 0; k < 5; k++)
        if (e == t10.drop[k]) {
            /* der Reihe nach? Das erste Ziel muss ein Ende der Reihe sein, jedes weitere das naechste daneben. */
            const int n = f->def->elems[e].count;
            int down = 0;
            for (int i = 0; i < n; i++) if (f->es[e].mask & (1u << i)) down++;
            down--;                                                  /* dieses Ziel ist schon mitgezaehlt */
            if (down == 0) t10.dir[k] = (uint8_t)(sub == 0 ? 1 : (sub == n - 1 ? 2 : 3));
            else if ((t10.dir[k] == 1 && sub != down) || (t10.dir[k] == 2 && sub != n - 1 - down)) t10.dir[k] = 3;
            return;
        }
    for (int k = 0; k < 2; k++)
        if (e == t10.saver[k]) { t10.saver_left[k] = 0; bonus(f, 1000, "Ball Saver"); return; }
    for (int i = 0; i < 3; i++)
        if (e == t10.post[i] && t10.sets_done == 0x1F) { t10.sets_done = 0; bonus(f, 10000, "All Targets"); return; }
}

static void t10_drop_all(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    for (int k = 0; k < 5; k++) {
        if (e != t10.drop[k]) continue;
        const int n = f->def->elems[e].count, in_order = t10.dir[k] == 1 || t10.dir[k] == 2;
        t10.dir[k] = 0;
        t10.sets_done |= (uint8_t)(1u << k);
        bonus(f, (in_order ? 200 : 100) * n, in_order ? "In Order" : "Targets");
        if (in_order) {
            t10.arrows = 0x0F; t10.last_set = (int8_t)k;             /* Bonuspfeile der Spinner an */
            if (!t10.split_forever) t10_splitter(f, 1, 0);
        }
    }
}

static void t10_spinner(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    for (int i = 0; i < 4; i++) {
        if (e != t10.spin[i]) continue;
        points(f, 300);
        if (t10.arrows & (1u << i)) {
            t10.arrows &= (uint8_t)~(1u << i);
            bonus(f, 2000, "Spinner Bonus");
            if (t10.last_set >= 0 && PARTNER[t10.last_set] == i) { t10.last_set = -1; bonus(f, 5000, "Combination"); }
            t10_splitter(f, 1, 1);
        }
    }
}

static void t10_roll_all(vp_field_t *f, int e, vp_ball_t *b)
{
    if (e != t10.splitter || !t10.split_on) return;
    const int forever = t10.split_forever;
    if (f->nballs < 2) {
        vp_ball_t *n = vp_create_ball(f, b->x, b->y);
        if (n) {
            n->vx = -b->vx - 1.5f; n->vy = b->vy + 2.0f;
            b->vx += 1.5f;
            vp_update_launch_times(f);
            f->sfx |= VSFX_BALL;
            vp_message(f, "Ball Split!", 2000, 1);
            t10_post(f, 1);
        }
    }
    t10_splitter(f, 0, 0);
    if (forever) mult_up(f);
}

/* ---- Anzeigen: blinkende Pfeile und der Stern, Formen wie im ROM ---- */
/* Umriss eines Pfeils ab der Spitze: sieben Schritte (y, x) im Massstab 6 des Originals */
static const int8_t ARROW[4][7][2] = {
    { { -84, -42 }, { 0, 21 }, { -126, 0 }, { 0, 42 }, { 126, 0 }, { 0, 21 }, { 84, -42 } },          /* $158E: zeigt nach oben */
    { { 84, -42 }, { 0, 21 }, { 126, 0 }, { 0, 42 }, { -126, 0 }, { 0, 21 }, { -84, -42 } },          /* $159C: nach unten */
    { { 0, 84 }, { -21, -21 }, { -105, 105 }, { -42, -42 }, { 105, -105 }, { -21, -21 }, { 84, 0 } }, /* $15AA: nach links oben */
    { { -84, 0 }, { 21, -21 }, { 105, 105 }, { 42, -42 }, { -105, -105 }, { 21, -21 }, { 0, 84 } },   /* $15B8: nach links unten */
};
static void arrow(vp_field_t *f, int shape, float ry, float rx)
{
    const float k = 7.5f / 128.5f;                                   /* ROM-Einheiten je Schritt bei Massstab 6 */
    float y = ry, x = rx;
    for (int i = 0; i < 7; i++) {
        float ny = y + ARROW[shape][i][0] * k, nx = x + ARROW[shape][i][1] * k;
        vp_shape_line(f, FX(x), FY(y), FX(nx), FY(ny), WHITE);
        y = ny; x = nx;
    }
}

static void t10_shapes(vp_field_t *f)
{
    /* Spinner: oben links, unten links, unten rechts, rechts Mitte - Spitze des Pfeils (ROM y, x) und Form */
    static const float TIP[4][2] = { { 82.5f, -77.5f }, { -47.4f, -78.0f }, { -47.4f, 66.0f }, { -10.3f, 65.1f } };
    static const uint8_t FORM[4] = { 2, 1, 1, 3 };
    const int blink = (f->now_ms / 170) & 1;
    f->nshapes = 0;
    if (blink) {
        for (int i = 0; i < 4; i++) if (t10.arrows & (1u << i)) arrow(f, FORM[i], TIP[i][0], TIP[i][1]);
        if (t10.saver_left[0] > 0) arrow(f, 0, -88.0f, -90.0f);
        if (t10.saver_left[1] > 0) arrow(f, 0, -88.0f, 78.0f);
    }
    if (t10.split_on && (t10.split_forever || blink || t10.split_left > 6.0f)) {
        /* Stern in der Tischmitte ($1550): vier Striche durch (-10, -6), Halbmesser 5,9 */
        const float cy = -10.0f, cx = -6.0f, r = 5.9f, d = r * 0.7071f;
        vp_shape_line(f, FX(cx), FY(cy - r), FX(cx), FY(cy + r), WHITE);
        vp_shape_line(f, FX(cx - r), FY(cy), FX(cx + r), FY(cy), WHITE);
        vp_shape_line(f, FX(cx - d), FY(cy - d), FX(cx + d), FY(cy + d), WHITE);
        vp_shape_line(f, FX(cx - d), FY(cy + d), FX(cx + d), FY(cy - d), WHITE);
    }
}

static void t10_tick(vp_field_t *f, float dt)
{
    if (!t10.ready) return;
    {   /* Kanaele: zaehlt, wenn ein Ball neu in den Kreis um die Raute kommt */
        uint8_t now = 0;
        for (int i = 0; i < 3; i++) {
            const vp_roll_t *r = vp_roll(f, t10.chutes, i);
            for (int k = 0; k < f->nballs; k++) {
                float dx = f->balls[k].x - r->x, dy = f->balls[k].y - r->y;
                if (dx * dx + dy * dy < 0.6f * 0.6f) now |= (uint8_t)(1u << i);
            }
        }
        const uint8_t fresh = now & (uint8_t)~t10.in_chute;
        t10.in_chute = now;
        for (int i = 0; i < 3; i++) {
            if (!(fresh & (1u << i))) continue;
            if (vp_roll_active(f, t10.chutes, i)) {
                vp_roll_set(f, t10.chutes, i, 0);
                vp_roll_activate_first(f, t10.lights);
                bonus(f, 500, NULL);
            } else bonus(f, 1500, NULL);
            f->sfx |= VSFX_ROLLOVER;
        }
        if (!vp_roll_active(f, t10.chutes, 0) && !vp_roll_active(f, t10.chutes, 1) && !vp_roll_active(f, t10.chutes, 2)) {
            vp_roll_set_all(f, t10.chutes, 1);                       /* alle aus: Rauten wieder an, ein Retter fuer eine Weile */
            t10_saver(f, t10.next_saver, 1);
            vp_message(f, t10.next_saver ? "Right Saver On" : "Left Saver On", 1500, 1);
            t10.next_saver ^= 1;
        }
    }
    if (vp_roll_all(f, t10.lights)) { vp_roll_set_all(f, t10.lights, 0); mult_up(f); }
    for (int k = 0; k < 2; k++)
        if (t10.saver_left[k] > 0 && (t10.saver_left[k] -= dt) <= 0) t10_saver(f, k, 0);
    if (t10.split_on && !t10.split_forever && (t10.split_left -= dt) <= 0) t10_splitter(f, 0, 0);
    if (t10.post_on && f->nballs < 2 && f->nballs > 0 && !t10.split_on) { /* bleibt, solange der Ball lebt */ }
    t10_shapes(f);
}

const vp_delegate_t vp_delegate10 = { t10_init, t10_started, t10_ball_lost, t10_tick, t10_collision, t10_drop_all, t10_roll_all, t10_spinner, t10_sensor };
