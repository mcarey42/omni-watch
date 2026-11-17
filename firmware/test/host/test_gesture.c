/* The recogniser's appended events (G_DOWN, G_HOLD, G_LIFT_STILL, asked for per claim) and the proof that a want of 0
 * is today's recogniser: the same streams through main 4ef03a7's gesture.c (gesture_oracle.c) give identical output. */
#include "unity.h"
#include "omni/input.h"
#include "gesture_oracle.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static bool claim_true(int16_t x0, int16_t y0, void *ctx) { (void)x0; (void)y0; (void)ctx; return true; }
static bool claim_false(int16_t x0, int16_t y0, void *ctx) { (void)x0; (void)y0; (void)ctx; return false; }

static omni_gesture_sm_t sm;
static omni_gesture_t out[2];
static int feed_w(uint32_t t, bool p, int x, int y, uint8_t want, bool (*c)(int16_t, int16_t, void *))
{
    memset(out, 0, sizeof out);
    return omni_gesture_feed_w(&sm, t, p, (int16_t)x, (int16_t)y, c, NULL, want, out);
}

/* a still press at (233,233) fed every 16 ms up to and including end_ms; counts what comes out */
typedef struct {
    int down, hold, lift, tap, longp, other;
    int hold_n;
    uint16_t ages[200];
    uint32_t hold_t[200];
} run_t;

static void tally(run_t *r, int n, uint32_t t)
{
    for (int i = 0; i < n; i++) {
        switch (out[i].kind) {
        case G_DOWN: r->down++; break;
        case G_HOLD:
            if (r->hold_n < 200) { r->ages[r->hold_n] = out[i].hold_ms; r->hold_t[r->hold_n] = t; }
            r->hold_n++;
            r->hold++;
            break;
        case G_LIFT_STILL: r->lift++; break;
        case G_TAP: r->tap++; break;
        case G_LONG_PRESS: r->longp++; break;
        default: r->other++; break;
        }
    }
}

static run_t still(uint8_t want, uint32_t hold_end, uint32_t release_at, bool do_release)
{
    run_t r;
    memset(&r, 0, sizeof r);
    omni_gesture_reset(&sm);
    for (uint32_t t = 0; t <= hold_end; t += 16) tally(&r, feed_w(t, true, 233, 233, want, NULL), t);
    if (do_release) tally(&r, feed_w(release_at, false, 233, 233, want, NULL), release_at);
    return r;
}

static void test_gesture_down_at_contact(void)
{
    omni_gesture_reset(&sm);
    TEST_ASSERT_EQUAL(1, feed_w(0, true, 120, 340, OMNI_GW_DOWN, NULL));
    TEST_ASSERT_EQUAL(G_DOWN, out[0].kind);
    TEST_ASSERT_EQUAL(120, out[0].x);
    TEST_ASSERT_EQUAL(340, out[0].y);
    TEST_ASSERT_EQUAL(120, out[0].x0);
    TEST_ASSERT_EQUAL(340, out[0].y0);
    TEST_ASSERT_EQUAL(0, out[0].hold_ms);
    TEST_ASSERT_EQUAL(0, feed_w(16, true, 121, 340, OMNI_GW_DOWN, NULL));   /* once only */
    TEST_ASSERT_EQUAL(1, feed_w(32, false, 121, 340, OMNI_GW_DOWN, NULL));
    TEST_ASSERT_EQUAL(G_TAP, out[0].kind);
    omni_gesture_reset(&sm);
    TEST_ASSERT_EQUAL(0, feed_w(0, true, 120, 340, 0, NULL));
}

static void test_gesture_hold_schedule(void)
{
    run_t r = still(OMNI_GW_HOLD, 1600, 0, false);
    int seen1000 = 0, seen1500 = 0;
    TEST_ASSERT_EQUAL(0, r.longp);
    TEST_ASSERT_TRUE(r.hold_n >= 14);
    for (int k = 0; k < r.hold_n && k < 200; k++) {
        const uint32_t due = 450u + 85u * (uint32_t)k;
        /* the first sample at or after the due age, one tick per feed */
        TEST_ASSERT_TRUE(r.hold_t[k] >= due);
        TEST_ASSERT_TRUE(r.hold_t[k] < due + 16);
        TEST_ASSERT_EQUAL_UINT32(r.hold_t[k], r.ages[k]);
        if (r.ages[k] >= 1000 && r.ages[k] < 1085) seen1000++;
        if (r.ages[k] >= 1500 && r.ages[k] < 1585) seen1500++;
    }
    TEST_ASSERT_EQUAL(1, seen1000);
    TEST_ASSERT_EQUAL(1, seen1500);
    TEST_ASSERT_EQUAL(0, r.other);
}

static void test_gesture_lift_still(void)
{
    run_t r = still(OMNI_GW_LIFT_STILL, 0, 299, true);
    TEST_ASSERT_EQUAL(1, r.tap);
    TEST_ASSERT_EQUAL(0, r.lift);
    r = still(OMNI_GW_LIFT_STILL, 0, 300, true);
    TEST_ASSERT_EQUAL(0, r.tap);
    TEST_ASSERT_EQUAL(1, r.lift);
    r = still(OMNI_GW_LIFT_STILL, 0, 2000, true);
    TEST_ASSERT_EQUAL(1, r.lift);
    TEST_ASSERT_EQUAL(0, r.tap);
    omni_gesture_reset(&sm);
    feed_w(0, true, 233, 233, OMNI_GW_LIFT_STILL, NULL);
    TEST_ASSERT_EQUAL(1, feed_w(2000, false, 240, 236, OMNI_GW_LIFT_STILL, NULL));
    TEST_ASSERT_EQUAL(G_LIFT_STILL, out[0].kind);
    TEST_ASSERT_EQUAL(240, out[0].x);
    TEST_ASSERT_EQUAL(233, out[0].x0);
}

static void test_gesture_hold_then_drag(void)
{
    run_t r;
    memset(&r, 0, sizeof r);
    omni_gesture_reset(&sm);
    for (uint32_t t = 0; t <= 500; t += 16) tally(&r, feed_w(t, true, 233, 233, OMNI_GW_HOLD, claim_true), t);
    TEST_ASSERT_EQUAL(1, r.hold);
    TEST_ASSERT_EQUAL(1, feed_w(516, true, 246, 233, OMNI_GW_HOLD, claim_true));   /* 13 px */
    TEST_ASSERT_EQUAL(G_DRAG_START, out[0].kind);
    memset(&r, 0, sizeof r);
    for (uint32_t t = 532; t <= 1500; t += 16) tally(&r, feed_w(t, true, 246, 233, OMNI_GW_HOLD, claim_true), t);
    TEST_ASSERT_EQUAL(0, r.hold);
    TEST_ASSERT_EQUAL(0, r.longp);
    /* unclaimed: the move ends the hold, no more ticks, today's swipe path */
    omni_gesture_reset(&sm);
    for (uint32_t t = 0; t <= 500; t += 16) feed_w(t, true, 233, 233, OMNI_GW_HOLD, claim_false);
    TEST_ASSERT_EQUAL(0, feed_w(516, true, 246, 233, OMNI_GW_HOLD, claim_false));
    memset(&r, 0, sizeof r);
    for (uint32_t t = 532; t <= 1500; t += 16) tally(&r, feed_w(t, true, 246, 233, OMNI_GW_HOLD, claim_false), t);
    TEST_ASSERT_EQUAL(0, r.hold);
}

static void test_gesture_wants_isolated(void)
{
    run_t r = still(OMNI_GW_LIFT_STILL, 2000, 2016, true);
    TEST_ASSERT_EQUAL(0, r.longp);
    TEST_ASSERT_EQUAL(0, r.hold);
    TEST_ASSERT_EQUAL(0, r.down);
    TEST_ASSERT_EQUAL(1, r.lift);
    r = still(OMNI_GW_HOLD, 2000, 2016, true);
    TEST_ASSERT_EQUAL(0, r.longp);
    TEST_ASSERT_EQUAL(0, r.down);
    TEST_ASSERT_EQUAL(0, r.lift);
    TEST_ASSERT_EQUAL(0, r.tap);
    TEST_ASSERT_TRUE(r.hold > 0);
    r = still(OMNI_GW_DOWN, 700, 716, true);
    TEST_ASSERT_EQUAL(1, r.down);
    TEST_ASSERT_EQUAL(1, r.longp);
    TEST_ASSERT_EQUAL(0, r.hold);
    TEST_ASSERT_EQUAL(0, r.lift);
    TEST_ASSERT_EQUAL(0, r.tap);
    r = still(OMNI_GW_DOWN | OMNI_GW_HOLD | OMNI_GW_LIFT_STILL, 700, 716, true);
    TEST_ASSERT_EQUAL(1, r.down);
    TEST_ASSERT_EQUAL(0, r.longp);
    TEST_ASSERT_EQUAL(1, r.lift);
    TEST_ASSERT_TRUE(r.hold > 0);
    r = still(0, 700, 716, true);
    TEST_ASSERT_EQUAL(0, r.down + r.hold + r.lift);
    TEST_ASSERT_EQUAL(1, r.longp);
}

static void test_gesture_hold_saturates(void)
{
    run_t r;
    int last_age = 0, ticks = 0, bad = 0;
    memset(&r, 0, sizeof r);
    omni_gesture_reset(&sm);
    for (uint32_t t = 0; t <= 70000; t += 16) {
        const int n = feed_w(t, true, 233, 233, OMNI_GW_HOLD, NULL);
        for (int i = 0; i < n; i++) {
            if (out[i].kind != G_HOLD) { bad++; continue; }
            ticks++;
            if (out[i].hold_ms < last_age) bad++;   /* a wrap */
            last_age = out[i].hold_ms;
        }
    }
    TEST_ASSERT_EQUAL(0, bad);
    /* due ages 450 + 85 k < 65 535 hold for k = 0..765: exactly 766 ticks, the last at the first sample at or after 65 475 */
    TEST_ASSERT_EQUAL(766, ticks);
    TEST_ASSERT_TRUE(last_age >= 65475 && last_age <= 65535);
    /* a late sample far past the cap saturates, and the schedule is then exhausted */
    omni_gesture_reset(&sm);
    feed_w(0, true, 233, 233, OMNI_GW_HOLD, NULL);
    TEST_ASSERT_EQUAL(1, feed_w(90000, true, 233, 233, OMNI_GW_HOLD, NULL));
    TEST_ASSERT_EQUAL(G_HOLD, out[0].kind);
    TEST_ASSERT_EQUAL(65535, out[0].hold_ms);
    ticks = 0;
    for (uint32_t t = 90016; t < 95000; t += 16) ticks += feed_w(t, true, 233, 233, OMNI_GW_HOLD, NULL);
    TEST_ASSERT_EQUAL(0, ticks);
}

/* ---- the oracle ---- */

typedef struct { uint32_t t; bool p; int x, y; } smp_t;
enum { CL_NULL, CL_FALSE, CL_TRUE };

/* a recording claim: every call's count and (x0, y0) are compared between the two recognisers */
typedef struct { int calls; int16_t x0, y0; int ret; } rec_t;
static rec_t rec_a, rec_b;
static bool claim_rec(int16_t x0, int16_t y0, void *ctx)
{
    rec_t *r = ctx;
    r->calls++;
    r->x0 = x0;
    r->y0 = y0;
    return r->ret != 0;
}
static bool (*rec_fn(int m))(int16_t, int16_t, void *) { return m == CL_NULL ? NULL : claim_rec; }

static int same_out(int na, const omni_gesture_t *a, int nb, const omni_gesture_t *b)
{
    if (na != nb) return 0;
    for (int i = 0; i < na; i++)
        if (a[i].kind != b[i].kind || a[i].x != b[i].x || a[i].y != b[i].y || a[i].x0 != b[i].x0 ||
            a[i].y0 != b[i].y0 || a[i].edge_start != b[i].edge_start || a[i].hold_ms != 0)
            return 0;
    return 1;
}

/* one feed through both; the out arrays are zeroed first (the oracle never writes hold_ms). Returns 1 when identical. */
static int both(omni_gesture_sm_t *a, omni_gesture_sm_t *b, const smp_t *s, int cl, int *emitted)
{
    omni_gesture_t oa[2], ob[2];
    memset(oa, 0, sizeof oa);
    memset(ob, 0, sizeof ob);
    rec_a.ret = rec_b.ret = (cl == CL_TRUE);
    const int before = rec_a.calls;
    int na = omni_gesture_feed(a, s->t, s->p, (int16_t)s->x, (int16_t)s->y, rec_fn(cl), &rec_a, oa);
    int nb = oracle_gesture_feed(b, s->t, s->p, (int16_t)s->x, (int16_t)s->y, rec_fn(cl), &rec_b, ob);
    if (emitted) *emitted += na;
    if (rec_a.calls != rec_b.calls || rec_a.x0 != rec_b.x0 || rec_a.y0 != rec_b.y0) return 0;
    if (cl == CL_NULL && rec_a.calls != before) return 0; /* no claim function: nothing is asked */
    return same_out(na, oa, nb, ob);
}

static int run_both(const smp_t *s, int n, int cl, int *emitted)
{
    omni_gesture_sm_t a, b;
    omni_gesture_reset(&a);
    oracle_gesture_reset(&b);
    memset(&rec_a, 0, sizeof rec_a);
    memset(&rec_b, 0, sizeof rec_b);
    for (int i = 0; i < n; i++)
        if (!both(&a, &b, &s[i], cl, emitted)) return 0;
    return 1;
}

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define ROW(nm, cl, arr) { nm, cl, arr, N(arr), 0, 0, NULL }
typedef struct {
    const char *src;      /* the source test */
    int claim;
    const smp_t *s;       /* explicit samples (test_input.c) */
    int n;
    uint32_t dt, hold_ms; /* touch-style (test_tmr_jog.c) */
    const int16_t (*pts)[2];
} row_t;

/* test_input.c */
static const smp_t R_TAP[] = { {0,1,233,233}, {100,1,236,234}, {200,0,236,234} };
static const smp_t R_REL450[] = { {0,1,233,233}, {450,0,233,233} };
static const smp_t R_LONG[] = { {0,1,233,233}, {599,1,233,233}, {600,1,233,233}, {700,1,233,233}, {800,0,233,233} };
static const smp_t R_LONG_MOVE[] = { {0,1,233,233}, {100,1,260,233}, {700,1,260,233} };
static const smp_t R_SWL[] = { {0,1,300,233}, {50,1,270,235}, {100,1,220,235}, {120,0,220,235} };
static const smp_t R_SWR[] = { {0,1,150,233}, {60,1,230,240}, {80,0,230,240} };
static const smp_t R_SWU[] = { {0,1,233,300}, {60,1,233,200}, {80,0,233,200} };
static const smp_t R_SHORT[] = { {0,1,233,233}, {60,1,273,233}, {80,0,273,233} };
static const smp_t R_TOP[] = { {0,1,233,20}, {60,1,233,120}, {80,0,233,120} };
static const smp_t R_NOTOP[] = { {0,1,233,200}, {60,1,233,300}, {80,0,233,300} };
static const smp_t R_DRAG[] = { {0,1,100,233}, {50,1,120,233}, {70,1,180,233}, {90,1,180,233}, {110,0,180,233} };
static const smp_t R_NOCLAIM[] = { {0,1,100,233}, {50,1,200,233}, {60,0,200,233} };
static const smp_t R_EDGE223[] = { {0,1,10,233}, {50,0,10,233} };
static const smp_t R_EDGE192[] = { {0,1,233,41}, {50,0,233,41} };
static const smp_t R_EDGE193[] = { {0,1,233,40}, {50,0,233,40} };

/* test_tmr_jog.c: touch(pts, n, dt, hold_ms) */
static const int16_t P_CENTRE[1][2] = { { 233, 233 } };
static const int16_t P_RIM[1][2] = { { 233, 30 } };
static const int16_t P_FLICK[6][2] = { { 25, 233 }, { 45, 234 }, { 85, 236 }, { 125, 238 }, { 165, 240 }, { 205, 242 } };
static const int16_t P_MID[4][2] = { { 280, 233 }, { 240, 233 }, { 200, 233 }, { 160, 233 } };
static const int16_t P_DOWN[5][2] = { { 233, 20 }, { 234, 45 }, { 235, 70 }, { 236, 95 }, { 238, 120 } };

static int expand(const row_t *r, smp_t *buf, int cap)
{
    int n = 0;
    if (r->s) {
        for (int i = 0; i < r->n && n < cap; i++) buf[n++] = r->s[i];
        return n;
    }
    const int held = (int)(r->hold_ms / r->dt);
    uint32_t t = 0;
    for (int i = 0; i <= r->n + held && n < cap; i++) {
        const bool p = i < r->n + held;
        const int j = i < r->n ? i : r->n - 1;
        buf[n++] = (smp_t){ t, p, r->pts[j][0], r->pts[j][1] };
        t += r->dt;
    }
    return n;
}

static void test_gesture_oracle_replay(void)
{
    static int16_t ring[121][2];
    for (int i = 0; i <= 120; i++) { /* test_tmr_jog.c's full turn round the rim: px/py(200, 3 i degrees) */
        ring[i][0] = (int16_t)lroundf(233.0f + 200.0f * sinf((float)i * 3.0f * 3.14159265358979f / 180.0f));
        ring[i][1] = (int16_t)lroundf(233.0f - 200.0f * cosf((float)i * 3.0f * 3.14159265358979f / 180.0f));
    }
    const row_t rows[] = {
        /* test_input.c, claim as that file's start(): a claim function returning false unless the test sets true */
        { "test_input:test_tap", CL_FALSE, R_TAP, N(R_TAP), 0, 0, NULL },
        { "test_input:test_tap (300-600 ms release)", CL_FALSE, R_REL450, N(R_REL450), 0, 0, NULL },
        { "test_input:test_long_press_600ms", CL_FALSE, R_LONG, N(R_LONG), 0, 0, NULL },
        { "test_input:test_long_press_600ms (moved first)", CL_FALSE, R_LONG_MOVE, N(R_LONG_MOVE), 0, 0, NULL },
        { "test_input:test_swipe_left_right (left)", CL_FALSE, R_SWL, N(R_SWL), 0, 0, NULL },
        { "test_input:test_swipe_left_right (right)", CL_FALSE, R_SWR, N(R_SWR), 0, 0, NULL },
        { "test_input:test_swipe_left_right (up)", CL_FALSE, R_SWU, N(R_SWU), 0, 0, NULL },
        { "test_input:test_swipe_left_right (too short)", CL_FALSE, R_SHORT, N(R_SHORT), 0, 0, NULL },
        { "test_input:test_swipe_down_needs_top_start (top)", CL_FALSE, R_TOP, N(R_TOP), 0, 0, NULL },
        { "test_input:test_swipe_down_needs_top_start (not top)", CL_FALSE, R_NOTOP, N(R_NOTOP), 0, 0, NULL },
        { "test_input:test_drag_claimed_suppresses_swipe", CL_TRUE, R_DRAG, N(R_DRAG), 0, 0, NULL },
        { "test_input:test_drag_claimed_suppresses_swipe (NULL claim)", CL_NULL, R_NOCLAIM, N(R_NOCLAIM), 0, 0, NULL },
        { "test_input:test_edge_band_flag (223)", CL_FALSE, R_EDGE223, N(R_EDGE223), 0, 0, NULL },
        { "test_input:test_edge_band_flag (192)", CL_FALSE, R_EDGE192, N(R_EDGE192), 0, 0, NULL },
        { "test_input:test_edge_band_flag (193)", CL_FALSE, R_EDGE193, N(R_EDGE193), 0, 0, NULL },
        /* test_tmr_jog.c; its claim is the timer's (rim start and a winding mode), so both answers are replayed */
        { "test_tmr_jog:test_end_to_end (centre tap)", CL_FALSE, NULL, 1, 16, 100, P_CENTRE },
        { "test_tmr_jog:test_end_to_end (rim long press)", CL_FALSE, NULL, 1, 16, 700, P_RIM },
        { "test_tmr_jog:test_end_to_end (rim long press, claim)", CL_TRUE, NULL, 1, 16, 700, P_RIM },
        { "test_tmr_jog:test_end_to_end (ring)", CL_TRUE, NULL, 121, 16, 0, (const int16_t(*)[2])ring },
        { "test_tmr_jog:test_end_to_end (ring, no claim)", CL_FALSE, NULL, 121, 16, 0, (const int16_t(*)[2])ring },
        { "test_tmr_jog:test_end_to_end (flick)", CL_TRUE, NULL, 6, 16, 0, P_FLICK },
        { "test_tmr_jog:test_end_to_end (flick, no claim)", CL_FALSE, NULL, 6, 16, 0, P_FLICK },
        { "test_tmr_jog:test_end_to_end (mid flick)", CL_FALSE, NULL, 4, 16, 0, P_MID },
        { "test_tmr_jog:test_swipe_down_from_top_every_mode", CL_FALSE, NULL, 5, 16, 0, P_DOWN },
        { "test_tmr_jog:test_swipe_down_from_top_every_mode (claimed)", CL_TRUE, NULL, 5, 16, 0, P_DOWN },
    };
    static smp_t buf[400];
    for (int i = 0; i < N(rows); i++) {
        const int n = expand(&rows[i], buf, 400);
        int emitted = 0;
        TEST_ASSERT_TRUE_MESSAGE(n > 0, rows[i].src);
        TEST_ASSERT_TRUE_MESSAGE(run_both(buf, n, rows[i].claim, &emitted), rows[i].src);
    }
}

/* xorshift32, seeded */
static uint32_t rng_s;
static uint32_t rnd(void)
{
    rng_s ^= rng_s << 13;
    rng_s ^= rng_s >> 17;
    rng_s ^= rng_s << 5;
    return rng_s;
}

static void test_gesture_oracle_random(void)
{
    static const int scale[3] = { 0, 3, 30 };
    int emitted = 0, longs = 0;
    rng_s = 0x9E3779B9u;
    for (int seq = 0; seq < 20000; seq++) {
        omni_gesture_sm_t a, b;
        omni_gesture_reset(&a);
        oracle_gesture_reset(&b);
        const int sc = scale[rnd() % 3], cl = (int)(rnd() % 3), n = (int)(rnd() % 41);
        uint32_t t = rnd() % 1000;
        int x = 40 + (int)(rnd() % 400), y = 40 + (int)(rnd() % 400);
        bool p = true;
        for (int i = 0; i < n; i++) {
            if (rnd() % 8 == 0) p = !p;
            if (sc) {
                x += (int)(rnd() % (uint32_t)(2 * sc + 1)) - sc;
                y += (int)(rnd() % (uint32_t)(2 * sc + 1)) - sc;
            }
            t += rnd() % 41;
            smp_t s = { t, p, x, y };
            TEST_ASSERT_TRUE(both(&a, &b, &s, cl, &emitted));
        }
    }
    TEST_ASSERT_TRUE(emitted > 1000);
    /* long presses occur in the long-sample sequences: still presses of 600 ms and more, steps up to 40 ms */
    for (int seq = 0; seq < 200; seq++) {
        omni_gesture_sm_t a, b;
        omni_gesture_reset(&a);
        oracle_gesture_reset(&b);
        uint32_t t = 0;
        for (int i = 0; i < 40; i++) {
            smp_t s = { t, i < 39, 200, 200 };
            int e = 0;
            TEST_ASSERT_TRUE(both(&a, &b, &s, CL_FALSE, &e));
            longs += e;
            t += 20 + rnd() % 21;
        }
    }
    TEST_ASSERT_TRUE(longs > 0);
    /* one long stream, no resets between sequences */
    {
        omni_gesture_sm_t a, b;
        omni_gesture_reset(&a);
        oracle_gesture_reset(&b);
        uint32_t t = 0;
        int x = 233, y = 233, ev = 0;
        bool p = false;
        for (int i = 0; i < 400000; i++) {
            if (rnd() % 12 == 0) p = !p;
            if (rnd() % 3 == 0) {
                x += (int)(rnd() % 61) - 30;
                y += (int)(rnd() % 61) - 30;
            }
            t += rnd() % 41;
            smp_t s = { t, p, x, y };
            TEST_ASSERT_TRUE(both(&a, &b, &s, (int)(rnd() % 3), &ev));
        }
        TEST_ASSERT_TRUE(ev > 1000);
    }
    /* the wrap: t0 = 0xFFFFF000, the clock rolls over mid-press */
    {
        omni_gesture_sm_t a, b;
        omni_gesture_reset(&a);
        oracle_gesture_reset(&b);
        uint32_t t = 0xFFFFF000u;
        int x = 233, y = 233, ev = 0;
        bool p = false;
        for (int i = 0; i < 100000; i++) {
            if (rnd() % 40 == 0) p = !p;
            if (rnd() % 5 == 0) {
                x += (int)(rnd() % 7) - 3;
                y += (int)(rnd() % 7) - 3;
            }
            t += rnd() % 41;
            smp_t s = { t, p, x, y };
            TEST_ASSERT_TRUE(both(&a, &b, &s, CL_NULL, &ev));
        }
        TEST_ASSERT_TRUE(ev > 100);
        /* and a single still press that straddles the wrap */
        omni_gesture_reset(&a);
        oracle_gesture_reset(&b);
        ev = 0;
        for (uint32_t k = 0; k < 80; k++) {
            smp_t s = { 0xFFFFFD00u + k * 16u, k < 79, 233, 233 };
            TEST_ASSERT_TRUE(both(&a, &b, &s, CL_NULL, &ev));
        }
        TEST_ASSERT_EQUAL(1, ev);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_gesture_down_at_contact);
    RUN_TEST(test_gesture_hold_schedule);
    RUN_TEST(test_gesture_lift_still);
    RUN_TEST(test_gesture_hold_then_drag);
    RUN_TEST(test_gesture_wants_isolated);
    RUN_TEST(test_gesture_hold_saturates);
    RUN_TEST(test_gesture_oracle_replay);
    RUN_TEST(test_gesture_oracle_random);
    return UNITY_END();
}
