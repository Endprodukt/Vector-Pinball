/* vp_table9.c - Tisch 9 (Royal Flush, Poker): Field9Delegate */
/* nicht zeitkritisch: auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp (das Kit baut sonst mit -O3) */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include "vp_deleg.h"

/* Karte = Rang (2..14) * 4 + Farbe; -1 = keine. Farben in der Reihenfolge des Originals: S, H, D, C */
enum { SUIT_S, SUIT_H, SUIT_D, SUIT_C };
#define CARD(rank, suit) ((rank) * 4 + (suit))
#define RANK(c) ((c) >> 2)
#define SUIT(c) ((c) & 3)
enum { M9_INACTIVE, M9_STARTING, M9_ACTIVE };

static const uint32_t SUIT_COLOR[4] = { VP_RGB(225, 225, 235), VP_RGB(235, 60, 70), VP_RGB(90, 140, 255), VP_RGB(80, 210, 100) };
static const char *const SUIT_NAME[4] = { "Spades", "Hearts", "Diamonds", "Clubs" };

static struct {
    int hand[5], nhand, left, right, mb, barrier, ready;
    int64_t hand_bonus;
} t9;

/* ---- Karten ziehen ---- */
static int in_list(const int *l, int n, int v) { for (int i = 0; i < n; i++) if (l[i] == v) return 1; return 0; }

static void draw_random(vp_field_t *f, int n, const int *used, int nused, int *out)
{
    int deck[52], k = 0;
    for (int r = 2; r <= 14; r++) for (int s = 0; s < 4; s++) deck[k++] = CARD(r, s);
    for (int i = 51; i > 0; i--) { int j = (int)(vp_rand(f) % (uint32_t)(i + 1)); int t = deck[i]; deck[i] = deck[j]; deck[j] = t; }
    for (int i = 0, o = 0; i < 52 && o < n; i++) if (!in_list(used, nused, deck[i])) out[o++] = deck[i];
}

static int draw_one(vp_field_t *f, const int *used, int nused) { int c = -1; draw_random(f, 1, used, nused, &c); return c; }

static void sort_desc(int *a, int n) { for (int i = 1; i < n; i++) { int v = a[i], j = i; while (j > 0 && a[j - 1] < v) { a[j] = a[j - 1]; j--; } a[j] = v; } }

/* Rang, der die gegebenen (1 bis 4) Raenge Richtung Strasse erweitert, oder -1 */
static int best_rank_for_straight(const int *unsorted, int n)
{
    int r[5];
    memcpy(r, unsorted, sizeof(int) * (size_t)n);
    sort_desc(r, n);
    for (int i = 1; i < n; i++) if (r[i] == r[i - 1]) return -1;             /* Paar: keine Strasse moeglich */
    if (n == 1) return r[0] >= 11 ? r[0] - 1 : r[0] + 1;
    int wheel = 1;
    for (int i = 0; i < n; i++) if (!(r[i] == 14 || r[i] <= 5)) wheel = 0;
    if (in_list(r, n, 14) && wheel) {
        static const int cand[5] = { 14, 5, 4, 3, 2 };
        for (int i = 0; i < 5; i++) if (!in_list(r, n, cand[i])) return cand[i];
    }
    int max = r[0], min = r[n - 1], diff = max - min;
    if (diff > 4) return -1;
    if (diff == n - 1) return max == 14 ? min - 1 : max + 1;
    for (int v = max; v >= min; v--) if (!in_list(r, n, v)) return v;
    return -1;
}

/* Gruppen gleicher Raenge: g[k] = erster Rang (in absteigender Reihenfolge), der k-mal vorkommt, sonst -1 */
static void groups(const int *ranks_desc, int n, int g[5])
{
    for (int k = 0; k < 5; k++) g[k] = -1;
    for (int i = 0; i < n; i++) {
        if (i > 0 && ranks_desc[i] == ranks_desc[i - 1]) continue;
        int cnt = 0;
        for (int j = 0; j < n; j++) if (ranks_desc[j] == ranks_desc[i]) cnt++;
        if (g[cnt] < 0) g[cnt] = ranks_desc[i];
    }
}

/* Farben in Zufallsreihenfolge ohne die in "remove" */
static int suits_minus(const int *rand_suits, const int *remove, int nrem, int *out)
{
    int n = 0;
    for (int i = 0; i < 4; i++) if (!in_list(remove, nrem, rand_suits[i])) out[n++] = rand_suits[i];
    for (int i = n; i < 4; i++) out[i] = rand_suits[i % 4];                   /* Sicherheit: nie ins Leere greifen */
    return n;
}

static int suits_of_rank(const int *h, int n, int rank, int *out) { int k = 0; for (int i = 0; i < n; i++) if (RANK(h[i]) == rank) out[k++] = SUIT(h[i]); return k; }

static int all_same_suit(const int *h, int n) { for (int i = 1; i < n; i++) if (SUIT(h[i]) != SUIT(h[0])) return 0; return 1; }

/* Zwei "Glueckskarten" (linke, rechte Rampe), die die Hand (1 bis 4 Karten) moeglichst verbessern */
static void draw_lucky(vp_field_t *f, const int *h, int n, int *left, int *right)
{
    int rs[4] = { 0, 1, 2, 3 }, ranks[5], hs[5], rem[4], tmp[5], g[5];
    for (int i = 3; i > 0; i--) { int j = (int)(vp_rand(f) % (uint32_t)(i + 1)); int t = rs[i]; rs[i] = rs[j]; rs[j] = t; }
    for (int i = 0; i < n; i++) { ranks[i] = RANK(h[i]); hs[i] = SUIT(h[i]); }
    sort_desc(ranks, n);
    *left = *right = -1;
    if (n == 1) {
        int other = rs[0] != hs[0] ? rs[0] : rs[1];
        *left = CARD(RANK(h[0]), other);
        *right = CARD(RANK(h[0]) > 10 ? RANK(h[0]) - 1 : RANK(h[0]) + 1, hs[0]);
    } else if (n == 2) {
        suits_minus(rs, hs, n, rem);
        if (RANK(h[0]) == RANK(h[1])) { *left = CARD(ranks[0], rem[0]); *right = CARD(ranks[0], rem[1]); }
        else {
            int sr = best_rank_for_straight(ranks, n);
            *left = CARD(ranks[0], rem[0]);
            *right = sr >= 0 ? CARD(sr, hs[0]) : CARD(ranks[1], rem[1]);
        }
    } else if (n == 3) {
        groups(ranks, n, g);
        if (g[3] >= 0) { suits_minus(rs, hs, n, rem); *left = *right = CARD(RANK(h[0]), rem[0]); }
        else if (g[2] >= 0) { suits_minus(rs, tmp, suits_of_rank(h, n, g[2], tmp), rem); *left = CARD(g[2], rem[0]); *right = CARD(g[2], rem[1]); }
        else {
            suits_minus(rs, tmp, suits_of_rank(h, n, g[1], tmp), rem);
            int sr = best_rank_for_straight(ranks, n);
            *left = CARD(g[1], rem[0]);
            *right = sr >= 0 ? CARD(sr, hs[0]) : CARD(g[1], rem[1]);
        }
    } else if (n == 4) {
        groups(ranks, n, g);
        if (g[4] >= 0) { int r = RANK(h[0]) == 14 ? 13 : 14; *left = CARD(r, rs[0]); *right = CARD(r, rs[1]); }
        else if (g[3] >= 0) { suits_minus(rs, hs, n, rem); *left = *right = CARD(g[3], rem[0]); }
        else if (g[2] >= 0) { suits_minus(rs, tmp, suits_of_rank(h, n, g[2], tmp), rem); *left = CARD(g[2], rem[0]); *right = CARD(g[2], rem[1]); }
        else {
            suits_minus(rs, tmp, suits_of_rank(h, n, g[1], tmp), rem);
            int sr = best_rank_for_straight(ranks, n);
            if (sr >= 0) {
                *left = CARD(sr, hs[0]);
                if (sr > ranks[0]) *right = CARD(ranks[n - 1] == 2 ? 14 : ranks[n - 1] - 1, hs[0]);   /* nach beiden Seiten offen */
                else *right = CARD(sr, hs[0]);
            } else if (all_same_suit(h, n)) {
                int k = 0, pick[2] = { 14, 13 };
                for (int r = 14; r >= 2 && k < 2; r--) if (!in_list(ranks, n, r)) pick[k++] = r;
                *left = CARD(pick[0], hs[0]); *right = CARD(pick[1], hs[0]);
            } else { *left = CARD(g[1], rem[0]); *right = CARD(g[1], rem[1]); }
        }
    }
}

typedef struct { const char *name; int64_t bonus; int multiball; } hand_result_t;

static hand_result_t evaluate_hand(void)
{
    int r[5], cnt[15] = { 0 }, top = 0, second = 0;
    for (int i = 0; i < 5; i++) { r[i] = RANK(t9.hand[i]); cnt[r[i]]++; }
    sort_desc(r, 5);
    for (int k = 2; k <= 14; k++) {
        if (cnt[k] > top) { second = top; top = cnt[k]; }
        else if (cnt[k] > second) second = cnt[k];
    }
    int flush = all_same_suit(t9.hand, 5);
    int wheel = r[0] == 14 && r[1] == 5 && r[2] == 4 && r[3] == 3 && r[4] == 2;
    int straight = top == 1 && (r[4] + 4 == r[0] || wheel);
    hand_result_t h;
    if (flush && r[0] == 14 && r[4] == 10 && top == 1) { h.name = "Royal Flush"; h.bonus = 100000; h.multiball = 1; }
    else if (straight && flush) { h.name = "Straight Flush"; h.bonus = 75000; h.multiball = 1; }
    else if (top == 4) { h.name = "Four of a Kind"; h.bonus = 60000; h.multiball = 1; }
    else if (top == 3 && second == 2) { h.name = "Full House"; h.bonus = 50000; h.multiball = 1; }
    else if (flush) { h.name = "Flush"; h.bonus = 40000; h.multiball = 1; }
    else if (straight) { h.name = "Straight"; h.bonus = 35000; h.multiball = 1; }
    else if (top == 3) { h.name = "Three of a Kind"; h.bonus = 30000; h.multiball = 1; }
    else if (top == 2 && second == 2) { h.name = "Two Pair"; h.bonus = 20000; h.multiball = 0; }
    else if (top == 2) { h.name = "Pair"; h.bonus = 10000; h.multiball = 0; }
    else { h.name = "High Card"; h.bonus = 5000; h.multiball = 0; }
    return h;
}

/* ---- Kartenbilder (14-Segment-Anzeige fuer den Rang, Symbol fuer die Farbe) ---- */
enum { SG_A, SG_B, SG_C, SG_D, SG_E, SG_F, SG_G1, SG_G2, SG_I, SG_J, SG_L, SG_M };
static const float SEG[12][4] = {
    { -0.44f, 0.8f, 0.44f, 0.8f }, { 0.5f, 0.74f, 0.5f, 0.06f }, { 0.5f, -0.06f, 0.5f, -0.74f }, { -0.44f, -0.8f, 0.44f, -0.8f },
    { -0.5f, -0.74f, -0.5f, -0.06f }, { -0.5f, 0.06f, -0.5f, 0.74f }, { -0.44f, 0, -0.06f, 0 }, { 0.06f, 0, 0.44f, 0 },
    { 0, 0.74f, 0, 0.06f }, { 0.4f, 0.7f, 0.1f, 0.1f }, { 0, -0.74f, 0, -0.06f }, { 0.4f, -0.7f, 0.1f, -0.1f },
};
#define B(x) (1u << (x))
static const uint16_t RANK_SEGS[15] = {
    0, 0,
    B(SG_A) | B(SG_B) | B(SG_G1) | B(SG_G2) | B(SG_E) | B(SG_D),                         /* 2 */
    B(SG_A) | B(SG_B) | B(SG_G1) | B(SG_G2) | B(SG_C) | B(SG_D),                         /* 3 */
    B(SG_F) | B(SG_G1) | B(SG_G2) | B(SG_B) | B(SG_C),                                   /* 4 */
    B(SG_A) | B(SG_F) | B(SG_G1) | B(SG_G2) | B(SG_C) | B(SG_D),                         /* 5 */
    B(SG_A) | B(SG_F) | B(SG_G1) | B(SG_G2) | B(SG_C) | B(SG_D) | B(SG_E),               /* 6 */
    B(SG_A) | B(SG_B) | B(SG_C),                                                         /* 7 */
    B(SG_A) | B(SG_B) | B(SG_C) | B(SG_D) | B(SG_E) | B(SG_F) | B(SG_G1) | B(SG_G2),     /* 8 */
    B(SG_A) | B(SG_B) | B(SG_C) | B(SG_D) | B(SG_F) | B(SG_G1) | B(SG_G2),               /* 9 */
    B(SG_A) | B(SG_I) | B(SG_L),                                                         /* 10 = T */
    B(SG_B) | B(SG_C) | B(SG_D) | B(SG_E),                                               /* Bube = J */
    B(SG_A) | B(SG_B) | B(SG_C) | B(SG_D) | B(SG_E) | B(SG_F) | B(SG_M),                 /* Dame = Q */
    B(SG_F) | B(SG_E) | B(SG_G1) | B(SG_J) | B(SG_M),                                    /* Koenig = K */
    B(SG_A) | B(SG_B) | B(SG_C) | B(SG_E) | B(SG_F) | B(SG_G1) | B(SG_G2),               /* As = A */
};

static void suit_shape(vp_field_t *f, int suit, float gx, float gy, float s, uint32_t c)
{
    switch (suit) {
    case SUIT_H:
        vp_shape_arc(f, gx - 0.23f * s, gy + 0.17f * s, 0.25f * s, 0.25f * s, 0, 180, c);
        vp_shape_arc(f, gx + 0.23f * s, gy + 0.17f * s, 0.25f * s, 0.25f * s, 0, 180, c);
        vp_shape_line(f, gx - 0.48f * s, gy + 0.13f * s, gx, gy - 0.5f * s, c);
        vp_shape_line(f, gx, gy - 0.5f * s, gx + 0.48f * s, gy + 0.13f * s, c);
        break;
    case SUIT_D:
        vp_shape_line(f, gx, gy + 0.5f * s, gx + 0.36f * s, gy, c);
        vp_shape_line(f, gx + 0.36f * s, gy, gx, gy - 0.5f * s, c);
        vp_shape_line(f, gx, gy - 0.5f * s, gx - 0.36f * s, gy, c);
        vp_shape_line(f, gx - 0.36f * s, gy, gx, gy + 0.5f * s, c);
        break;
    case SUIT_C:
        vp_shape_circle(f, gx, gy + 0.26f * s, 0.21f * s, 1, c);
        vp_shape_circle(f, gx - 0.23f * s, gy - 0.06f * s, 0.21f * s, 1, c);
        vp_shape_circle(f, gx + 0.23f * s, gy - 0.06f * s, 0.21f * s, 1, c);
        vp_shape_line(f, gx, gy - 0.1f * s, gx, gy - 0.5f * s, c);
        break;
    default:
        vp_shape_line(f, gx - 0.4f * s, gy - 0.08f * s, gx, gy + 0.5f * s, c);
        vp_shape_line(f, gx, gy + 0.5f * s, gx + 0.4f * s, gy - 0.08f * s, c);
        vp_shape_arc(f, gx - 0.2f * s, gy - 0.14f * s, 0.2f * s, 0.2f * s, 180, 360, c);
        vp_shape_arc(f, gx + 0.2f * s, gy - 0.14f * s, 0.2f * s, 0.2f * s, 180, 360, c);
        vp_shape_line(f, gx, gy - 0.16f * s, gx, gy - 0.5f * s, c);
        break;
    }
}

static void card_shape(vp_field_t *f, float cx, float cy, float w, int card)
{
    float hw = 0.5f * w, hh = 0.7f * w;
    uint32_t border = card >= 0 ? VP_RGB(210, 210, 220) : VP_RGB(70, 70, 85);
    vp_shape_line(f, cx - hw, cy - hh, cx - hw, cy + hh, border);
    vp_shape_line(f, cx - hw, cy + hh, cx + hw, cy + hh, border);
    vp_shape_line(f, cx + hw, cy + hh, cx + hw, cy - hh, border);
    vp_shape_line(f, cx + hw, cy - hh, cx - hw, cy - hh, border);
    if (card < 0) return;
    uint32_t c = SUIT_COLOR[SUIT(card)];
    float ds = 0.42f * w, dy = cy + 0.27f * w;
    for (int k = 0; k < 12; k++)
        if (RANK_SEGS[RANK(card)] & B(k))
            vp_shape_line(f, cx + SEG[k][0] * ds, dy + SEG[k][1] * ds, cx + SEG[k][2] * ds, dy + SEG[k][3] * ds, c);
    suit_shape(f, SUIT(card), cx, cy - 0.38f * w, 0.55f * w, c);
}

static void rebuild_shapes(vp_field_t *f)
{
    static const float XS[5] = { 5.9f, 7.65f, 9.4f, 11.15f, 12.9f };
    f->nshapes = 0;
    if (t9.left >= 0) card_shape(f, 2.8f, 17.5f, 1.6f, t9.left);
    if (t9.right >= 0) card_shape(f, 16.5f, 16.6f, 1.6f, t9.right);
    for (int i = 0; i < 5; i++) card_shape(f, XS[i], 11.0f, 1.5f, i < t9.nhand ? t9.hand[i] : -1);
}

/* ---- Ablauf ---- */
static void new_ramp_cards(vp_field_t *f)
{
    int c[2] = { -1, -1 };
    draw_random(f, 2, NULL, 0, c);
    t9.left = c[0]; t9.right = c[1];
}

static void t9_act_launch(vp_field_t *f, int last)
{
    if (f->nballs < 3) vp_launch_ball(f);
    if (last) t9.mb = M9_ACTIVE;
}

static void jackpot(vp_field_t *f)
{
    vp_add_score(f, t9.hand_bonus + evaluate_hand().bonus);
    vp_message(f, "Jackpot!", 1500, 1);
}

static const char *rank_name(int r, char *buf, size_t n)
{
    if (r <= 10) { snprintf(buf, n, "%d", r); return buf; }
    return r == 11 ? "Jack" : (r == 12 ? "Queen" : (r == 13 ? "King" : "Ace"));
}

static void collect_card(vp_field_t *f, int card)
{
    char rb[8];
    if (card < 0 || t9.nhand >= 5) return;
    t9.hand[t9.nhand++] = card;
    if (t9.nhand < 5) {
        int used[8], n;
        vp_messagef(f, 1500, "%s of %s", rank_name(RANK(card), rb, sizeof rb), SUIT_NAME[SUIT(card)]);
        vp_add_score(f, 2500);
        if (card == t9.left)  { n = t9.nhand; memcpy(used, t9.hand, sizeof(int) * (size_t)n); used[n++] = t9.right; t9.left = draw_one(f, used, n); }
        if (card == t9.right) { n = t9.nhand; memcpy(used, t9.hand, sizeof(int) * (size_t)n); used[n++] = t9.left; t9.right = draw_one(f, used, n); }
    } else {
        hand_result_t r = evaluate_hand();
        int64_t jp = r.bonus + t9.hand_bonus;
        vp_add_score(f, jp);
        vp_messagef(f, 2500, "%s! +%ld", r.name, (long)jp);
        if (r.multiball) {
            t9.mb = M9_STARTING;
            vp_schedule(f, 1000, t9_act_launch, 0);
            vp_schedule(f, 3500, t9_act_launch, 1);
            t9.left = t9.right = -1;
        } else { t9.nhand = 0; new_ramp_cards(f); }
    }
    rebuild_shapes(f);
}

static void t9_init(vp_field_t *f)
{
    memset(&t9, 0, sizeof t9);
    t9.barrier = vp_find(f, "LaunchBarrier");
    t9.mb = M9_INACTIVE;
    new_ramp_cards(f);
    rebuild_shapes(f);
    t9.ready = 1;
}

static void t9_started(vp_field_t *f) { t9_init(f); vp_wall_retract(f, t9.barrier, 1); }
static void t9_ball_lost(vp_field_t *f) { vp_wall_retract(f, t9.barrier, 0); }

static void t9_sensor(vp_field_t *f, int e, vp_ball_t *b)
{
    int prev = b->last_sensor;
    if (vp_is(f, e, "LaunchBarrierSensor")) vp_wall_retract(f, t9.barrier, 0);
    else if (vp_is(f, e, "LaunchBarrierRetract")) vp_wall_retract(f, t9.barrier, 1);
    else if (vp_is(f, e, "LeftRampSensor_Trigger") && vp_is(f, prev, "LeftRampSensor_Enter")) {
        if (t9.mb == M9_INACTIVE) collect_card(f, t9.left); else jackpot(f);
    } else if (vp_is(f, e, "RightRampSensor_Trigger") && vp_is(f, prev, "RightRampSensor_Enter")) {
        if (t9.mb == M9_INACTIVE) collect_card(f, t9.right); else jackpot(f);
    } else if (vp_is(f, e, "CenterRampSensor_Trigger") && vp_is(f, prev, "CenterRampSensor_Enter")) {
        if (t9.mb == M9_INACTIVE) {
            if (t9.nhand == 0 && t9.left >= 0) t9.hand[t9.nhand++] = t9.left;
            if (t9.nhand >= 1 && t9.nhand <= 4) draw_lucky(f, t9.hand, t9.nhand, &t9.left, &t9.right);
            vp_message(f, "Lucky draw!", 2500, 1);
            vp_add_score(f, 2500);
            rebuild_shapes(f);
        } else jackpot(f);
    } else if (vp_starts(f, e, "RampDropSensor")) {
        /* am Ende einer Rampe: faellt auf die Hauptebene, sobald der Ball fast steht */
        if (b->layer != 0 && hypotf(b->vx, b->vy) < 0.05f) b->layer = 0;
    }
}

static void t9_collision(vp_field_t *f, int e, int sub, vp_ball_t *b)
{
    (void)sub;
    if (vp_starts(f, e, "RampEndCap")) { b->vx *= 0.2f; b->vy *= 0.2f; return; }   /* der Ball soll in der Tasche liegen bleiben */
    int left = vp_is(f, e, "LeftCycleTargets"), right = vp_is(f, e, "RightCycleTargets");
    if (!left && !right) return;
    int *card = left ? &t9.left : &t9.right;
    if (*card >= 0 && !vp_drop_all_hit(f, e)) {
        int used[8], n = t9.nhand;
        memcpy(used, t9.hand, sizeof(int) * (size_t)n);
        used[n++] = t9.left; used[n++] = t9.right;
        *card = draw_one(f, used, n);
    }
    rebuild_shapes(f);
}

static void t9_drop_all(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    if (dg_save_targets(f, e, "DropTargetLeftSave", "DropTargetRightSave")) return;
    int left = vp_is(f, e, "LeftCycleTargets"), right = vp_is(f, e, "RightCycleTargets");
    if ((left || right) && t9.nhand > 0 && t9.nhand < 5) {
        int l, r;
        draw_lucky(f, t9.hand, t9.nhand, &l, &r);
        if (left) t9.left = l; else t9.right = r;
        rebuild_shapes(f);
        vp_message(f, "Lucky card!", 2000, 1);
    }
}

static void t9_roll_all(vp_field_t *f, int e, vp_ball_t *b)
{
    (void)b;
    if (vp_is(f, e, "FlipperRollovers")) { vp_roll_set_all(f, e, 0); vp_inc_multiplier(f, 1500); }
    else if (vp_is(f, e, "TopRollovers")) {
        vp_roll_set_all(f, e, 0);
        t9.hand_bonus += 1000;
        vp_message(f, "Hand bonus increased", 1500, 1);
    }
}

static void t9_tick(vp_field_t *f, float dt)
{
    (void)dt;
    if (t9.ready && t9.mb == M9_ACTIVE && f->nballs <= 1) {
        t9.mb = M9_INACTIVE;
        t9.nhand = 0;
        new_ramp_cards(f);
        rebuild_shapes(f);
    }
}

const vp_delegate_t vp_delegate9 = { t9_init, t9_started, t9_ball_lost, t9_tick, t9_collision, t9_drop_all, t9_roll_all, NULL, t9_sensor };
