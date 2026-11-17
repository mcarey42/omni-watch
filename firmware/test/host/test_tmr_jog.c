/* R144: the timer face's rim is a relative jog dial, and a drag that starts on the rim is classified (omni/tmr_jog.h):
 * the rim takes it only after OMNI_TMR_JOG_CLAIM_DEG of travel round the centre inside the rim band; before that a
 * mostly horizontal flick is a swipe (navigation, from anywhere on the face). Taps and long presses never reach it. */
#include "unity.h"
#include "omni/input.h"
#include "omni/timer.h"
#include "omni/tmr_jog.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define PI_F 3.14159265358979f

static omni_tmr_t T;
static omni_tmr_drag_t D;
static int16_t X0, Y0;

static omni_gesture_t ev(omni_gesture_kind_t k, int16_t x, int16_t y)
{
    omni_gesture_t g;
    memset(&g, 0, sizeof g);
    g.kind = k;
    g.x = x;
    g.y = y;
    g.x0 = X0;
    g.y0 = Y0;
    return g;
}

static int16_t px(float r, float deg) { return (int16_t)lroundf(OMNI_TMR_CX + r * sinf(deg * PI_F / 180.0f)); }
static int16_t py(float r, float deg) { return (int16_t)lroundf(OMNI_TMR_CY - r * cosf(deg * PI_F / 180.0f)); }

/* One touch: an arc at radius r from a0 to a1 degrees (clockwise from 12) in 3 degree samples, as the input task reports
 * it (G_DRAG_START at the first sample past 12 px, G_DRAG_MOVE, G_DRAG_END at the lift). Returns what the face does. */
static omni_gesture_kind_t arc(float r, float a0, float a1)
{
    X0 = px(r, a0);
    Y0 = py(r, a0);
    const float step = a1 > a0 ? 3.0f : -3.0f;
    const int n = (int)ceilf(fabsf(a1 - a0) / 3.0f);
    omni_gesture_kind_t k = G_NONE;
    for (int i = 1; i <= n; i++) {
        const float a = i == n ? a1 : a0 + step * (float)i;
        omni_gesture_t g = ev(i == 1 ? G_DRAG_START : i == n ? G_DRAG_END : G_DRAG_MOVE, px(r, a), py(r, a));
        if (n == 1) {   /* start and end on one sample */
            g.kind = G_DRAG_START;
            omni_tmr_drag(&D, &T, &g);
            g.kind = G_DRAG_END;
        }
        k = omni_tmr_drag(&D, &T, &g);
    }
    return k;
}

/* A straight drag from (x0, y0) to (x1, y1) in n samples. */
static omni_gesture_kind_t line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int n)
{
    X0 = x0;
    Y0 = y0;
    omni_gesture_kind_t k = G_NONE;
    for (int i = 1; i <= n; i++) {
        const int16_t x = (int16_t)(x0 + (x1 - x0) * i / n), y = (int16_t)(y0 + (y1 - y0) * i / n);
        omni_gesture_t g = ev(i == 1 ? G_DRAG_START : i == n ? G_DRAG_END : G_DRAG_MOVE, x, y);
        k = omni_tmr_drag(&D, &T, &g);
    }
    return k;
}

static void fresh(void)
{
    omni_tmr_init(&T);
    memset(&D, 0, sizeof D);
}

void test_jog_one_turn_is_30_min(void)
{
    fresh();
    TEST_ASSERT_EQUAL(G_NONE, arc(200, 0, 360));
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, T.mode);   /* set, never started (R135) */
    TEST_ASSERT_EQUAL_UINT8(30, T.set_min);
    /* from any start angle: only the travel counts */
    fresh();
    arc(170, 135, 495);
    TEST_ASSERT_EQUAL_UINT8(30, T.set_min);
}

void test_jog_two_turns_is_60_and_clamps(void)
{
    fresh();
    arc(200, 90, 90 + 720);
    TEST_ASSERT_EQUAL_UINT8(60, T.set_min);
    fresh();
    arc(200, 0, 1000);   /* past two turns: held at 60 */
    TEST_ASSERT_EQUAL_UINT8(60, T.set_min);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, T.mode);
}

void test_jog_lift_and_replace_accumulates(void)
{
    fresh();
    arc(200, 300, 300 + 183);   /* half a turn (and 3 degrees, short of the next step): 15 */
    TEST_ASSERT_EQUAL_UINT8(15, T.set_min);
    arc(160, 45, 45 + 123);     /* the thumb back down anywhere else: +10 */
    TEST_ASSERT_EQUAL_UINT8(25, T.set_min);
    arc(215, 200, 200 + 63);    /* +5 */
    TEST_ASSERT_EQUAL_UINT8(30, T.set_min);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, T.mode);
}

void test_jog_anticlockwise_subtracts_and_clamps_at_0(void)
{
    fresh();
    arc(200, 0, 360);
    TEST_ASSERT_EQUAL_UINT8(30, T.set_min);
    arc(200, 90, 90 - 123);   /* -10 */
    TEST_ASSERT_EQUAL_UINT8(20, T.set_min);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, T.mode);
    arc(200, 0, -400);   /* past 0: held at 0; released at 0 the arming is cancelled */
    TEST_ASSERT_EQUAL_UINT8(0, T.set_min);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_IDLE, T.mode);
    /* from 0, back the other way in one touch: the overshoot below 0 is not banked */
    fresh();
    arc(200, 0, 123);
    TEST_ASSERT_EQUAL_UINT8(10, T.set_min);
    X0 = px(200, 0);
    Y0 = py(200, 0);
    omni_gesture_t g = ev(G_DRAG_START, px(200, -3), py(200, -3));
    omni_tmr_drag(&D, &T, &g);
    for (int a = -6; a >= -180; a -= 3) {   /* -15 min of travel: clamps at 0 */
        g = ev(G_DRAG_MOVE, px(200, (float)a), py(200, (float)a));
        omni_tmr_drag(&D, &T, &g);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, omni_tmr_set_ms(&T));
    for (int a = -177; a <= -150; a += 3) {   /* 30 degrees back: +2 at once (whole 12 degree steps), not after -5 */
        g = ev(G_DRAG_MOVE, px(200, (float)a), py(200, (float)a));
        omni_tmr_drag(&D, &T, &g);
    }
    g = ev(G_DRAG_END, px(200, -150), py(200, -150));
    omni_tmr_drag(&D, &T, &g);
    TEST_ASSERT_EQUAL_UINT8(2, T.set_min);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, T.mode);
}

void test_flick_from_rim_navigates(void)
{
    omni_tmr_t before;
    /* from the 9 o'clock rim, rightward across the face: the previous face (G_SWIPE_R), nothing set */
    fresh();
    before = T;
    TEST_ASSERT_EQUAL(G_SWIPE_R, line(25, 233, 205, 245, 6));
    TEST_ASSERT_EQUAL_MEMORY(&before, &T, sizeof T);
    /* from the 3 o'clock rim, leftward: the next face */
    TEST_ASSERT_EQUAL(G_SWIPE_L, line(445, 220, 300, 240, 4));
    TEST_ASSERT_EQUAL_MEMORY(&before, &T, sizeof T);
    /* a short flick that never leaves the band (70 px in from the 9 o'clock edge) */
    TEST_ASSERT_EQUAL(G_SWIPE_R, line(8, 233, 78, 233, 3));
    /* from an armed timer, a flick from the rim near 8 o'clock: navigates, the duration untouched */
    fresh();
    arc(200, 0, 123);
    before = T;
    TEST_ASSERT_EQUAL(G_SWIPE_R, line(60, 330, 260, 300, 5));
    TEST_ASSERT_EQUAL_MEMORY(&before, &T, sizeof T);
    /* mostly vertical (an upward one: swipe up does nothing, R144), or too short: neither a swipe nor a jog */
    fresh();
    TEST_ASSERT_EQUAL(G_NONE, line(233, 446, 240, 356, 4));
    TEST_ASSERT_EQUAL(G_NONE, line(30, 233, 75, 233, 3));
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_IDLE, T.mode);
}

void test_slow_arc_under_threshold_changes_nothing(void)
{
    omni_tmr_t before;
    fresh();
    arc(200, 0, 123);
    before = T;
    /* 19 degrees of slow travel round the rim, at 12 o'clock (horizontal) and at 3 o'clock (vertical): no value, no
     * mode change, and no swipe */
    TEST_ASSERT_EQUAL(G_NONE, arc(200, -9, 10));
    TEST_ASSERT_EQUAL_MEMORY(&before, &T, sizeof T);
    TEST_ASSERT_EQUAL(G_NONE, arc(220, 80, 99));
    TEST_ASSERT_EQUAL_MEMORY(&before, &T, sizeof T);
    TEST_ASSERT_EQUAL(G_NONE, arc(150, 200, 181));
    TEST_ASSERT_EQUAL_MEMORY(&before, &T, sizeof T);
    /* a wiggle that goes back and forth stays under the threshold however long it lasts */
    X0 = px(200, 0);
    Y0 = py(200, 0);
    omni_gesture_t g = ev(G_DRAG_START, px(200, 4), py(200, 4));
    omni_tmr_drag(&D, &T, &g);
    for (int i = 0; i < 40; i++) {
        const float a = (i & 1) ? 15.0f : -3.0f;
        g = ev(G_DRAG_MOVE, px(200, a), py(200, a));
        omni_tmr_drag(&D, &T, &g);
    }
    g = ev(G_DRAG_END, px(200, 0), py(200, 0));
    TEST_ASSERT_EQUAL(G_NONE, omni_tmr_drag(&D, &T, &g));
    TEST_ASSERT_EQUAL_MEMORY(&before, &T, sizeof T);
}

/* Travel only counts inside the rim band: a drag that dips inside r 140 before the threshold is never a jog. */
void test_band_left_is_never_a_jog(void)
{
    fresh();
    X0 = px(200, 0);
    Y0 = py(200, 0);
    omni_gesture_t g = ev(G_DRAG_START, px(120, 0), py(120, 0));   /* inside the band at once */
    omni_tmr_drag(&D, &T, &g);
    for (int a = 3; a <= 90; a += 3) {
        g = ev(G_DRAG_MOVE, px(200, (float)a), py(200, (float)a));
        omni_tmr_drag(&D, &T, &g);
    }
    g = ev(G_DRAG_END, px(200, 90), py(200, 90));
    omni_tmr_drag(&D, &T, &g);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_IDLE, T.mode);
    TEST_ASSERT_EQUAL_UINT8(0, T.set_min);
    /* once the rim has it, the thumb may wander inward: the value follows the angle (outside the dead centre) */
    fresh();
    g = ev(G_DRAG_START, px(200, 3), py(200, 3));
    omni_tmr_drag(&D, &T, &g);
    for (int a = 6; a <= 24; a += 3) {
        g = ev(G_DRAG_MOVE, px(200, (float)a), py(200, (float)a));
        omni_tmr_drag(&D, &T, &g);
    }
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SET, T.mode);
    for (int a = 27; a <= 63; a += 3) {   /* r 100: inside the band, outside the dead centre */
        g = ev(G_DRAG_MOVE, px(100, (float)a), py(100, (float)a));
        omni_tmr_drag(&D, &T, &g);
    }
    g = ev(G_DRAG_END, px(100, 63), py(100, 63));
    omni_tmr_drag(&D, &T, &g);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, T.mode);
    TEST_ASSERT_EQUAL_UINT8(5, T.set_min);   /* 63 degrees: 5.25 min, whole steps */
}

/* M2: a pre-claim dip just inside r 140 (to r 128) keeps the drag a candidate jog; only below r 128 is it FREE. */
static omni_gesture_kind_t dip_arc(int dip_r)
{
    X0 = px(150, 0);
    Y0 = py(150, 0);
    omni_gesture_t g = ev(G_DRAG_START, px(150, 3), py(150, 3));
    omni_tmr_drag(&D, &T, &g);
    for (int a = 6; a <= 27; a += 3) {
        const float r = a == 9 ? (float)dip_r : 150.0f;
        g = ev(G_DRAG_MOVE, px(r, (float)a), py(r, (float)a));
        omni_tmr_drag(&D, &T, &g);
    }
    g = ev(G_DRAG_END, px(150, 27), py(150, 27));
    return omni_tmr_drag(&D, &T, &g);
}

void test_band_hysteresis(void)
{
    fresh();
    /* a wobble to r 136: still the rim's, and it winds (27 degrees: +2); its 68 px chord never navigates */
    TEST_ASSERT_EQUAL(G_NONE, dip_arc(136));
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, T.mode);
    TEST_ASSERT_EQUAL_UINT8(2, T.set_min);
    fresh();
    TEST_ASSERT_EQUAL(G_NONE, dip_arc(129));
    TEST_ASSERT_EQUAL_UINT8(2, T.set_min);
    fresh();
    dip_arc(120);   /* well inside r 128: left the band, never a jog (the 68 px chord is then a swipe, as designed) */
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_IDLE, T.mode);
    TEST_ASSERT_EQUAL_UINT8(0, T.set_min);
}

/* The threshold itself: just under it nothing moves; once past it, the whole travel counts (1 min per 12 degrees). */
void test_claim_threshold(void)
{
    fresh();
    X0 = px(200, 0);
    Y0 = py(200, 0);
    omni_gesture_t g = ev(G_DRAG_START, px(200, 5), py(200, 5));
    omni_tmr_drag(&D, &T, &g);
    g = ev(G_DRAG_MOVE, px(200, 18), py(200, 18));
    omni_tmr_drag(&D, &T, &g);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_IDLE, T.mode);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_DRAG_PENDING, D.phase);
    g = ev(G_DRAG_MOVE, px(200, 21), py(200, 21));
    omni_tmr_drag(&D, &T, &g);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_DRAG_JOG, D.phase);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SET, T.mode);
    TEST_ASSERT_EQUAL_UINT32(60000u, omni_tmr_set_ms(&T));   /* the claim: exactly one minute */
    g = ev(G_DRAG_MOVE, px(200, 23), py(200, 23));
    omni_tmr_drag(&D, &T, &g);
    TEST_ASSERT_EQUAL_UINT32(60000u, omni_tmr_set_ms(&T));
    g = ev(G_DRAG_MOVE, px(200, 27), py(200, 27));
    omni_tmr_drag(&D, &T, &g);
    TEST_ASSERT_EQUAL_UINT32(2u * 60000u, omni_tmr_set_ms(&T));
    /* a lift mid-wind with no G_DRAG_END (touch cancel, sleep): wind_cancel restores the value before the touch */
    omni_tmr_wind_cancel(&T);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_IDLE, T.mode);
    TEST_ASSERT_EQUAL_UINT8(0, T.set_min);
}

/* R144 choice: a jog sets in IDLE, ARMED and PAUSED; a running countdown ignores it (and does not claim the rim, so its
 * rim drags are the shell's swipes); the stopwatch and DONE never claim it. */
void test_jog_by_mode(void)
{
    omni_tmr_t before;
    /* PAUSED: the remaining time moves by whole minutes, the seconds kept, still PAUSED */
    fresh();
    arc(200, 0, 123);
    omni_tmr_tap(&T, 1000);
    omni_tmr_tap(&T, 16000);   /* 9:45 left */
    arc(200, 10, 10 + 360);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_PAUSED, T.mode);
    TEST_ASSERT_EQUAL_UINT32(585000u + 30u * 60000u, omni_tmr_remaining_ms(&T, 0));
    /* COUNT: nothing, and the alarm stays where it was */
    omni_tmr_tap(&T, 20000);
    before = T;
    TEST_ASSERT_FALSE(omni_tmr_claims(&T, 233, 30));
    arc(200, 0, 360);
    TEST_ASSERT_EQUAL_MEMORY(&before, &T, sizeof T);
    /* the stopwatch: no claim, and a stray drag changes nothing */
    fresh();
    omni_tmr_tap(&T, 0);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SW, T.mode);
    TEST_ASSERT_FALSE(omni_tmr_claims(&T, 233, 30));
    before = T;
    arc(200, 0, 360);
    TEST_ASSERT_EQUAL_MEMORY(&before, &T, sizeof T);
    omni_tmr_tap(&T, 5000);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SWPAUSE, T.mode);
    TEST_ASSERT_FALSE(omni_tmr_claims(&T, 233, 30));
    TEST_ASSERT_EQUAL(G_SWIPE_R, line(25, 233, 205, 245, 6));   /* the classifier still reads a flick */
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SWPAUSE, T.mode);
}

/* I1: a claimed turn of 20-23 degrees is exactly one minute, either way. */
void test_claim_gives_one_minute(void)
{
    static const float ends[] = { 20.5f, 21.0f, 22.0f, 23.0f, 23.5f };
    for (unsigned i = 0; i < sizeof ends / sizeof ends[0]; i++) {
        fresh();
        TEST_ASSERT_EQUAL(G_NONE, arc(200, 0, ends[i]));
        TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, T.mode);
        TEST_ASSERT_EQUAL_UINT8(1, T.set_min);
        arc(200, 0, 123);   /* 11 */
        TEST_ASSERT_EQUAL_UINT8(11, T.set_min);
        arc(200, 90, 90 - ends[i]);   /* anticlockwise from ARMED: exactly one less */
        TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, T.mode);
        TEST_ASSERT_EQUAL_UINT8(10, T.set_min);
    }
}

/* M1: a thumb resting on a step boundary (a 1 px wobble) never toggles the value: a step back needs a whole 12 degrees. */
void test_boundary_rest_does_not_toggle(void)
{
    /* 24 and 36 degrees: whole-minute boundaries; 30: where half-minute rounding would put one */
    static const int rest[] = { 24, 30, 36 };
    fresh();
    X0 = px(200, 0);
    Y0 = py(200, 0);
    omni_gesture_t g = ev(G_DRAG_START, px(200, 3), py(200, 3));
    omni_tmr_drag(&D, &T, &g);
    int at = 3;
    for (unsigned k = 0; k < sizeof rest / sizeof rest[0]; k++) {
        for (; at < rest[k]; at += 1) {
            g = ev(G_DRAG_MOVE, px(200, (float)at + 1), py(200, (float)at + 1));
            omni_tmr_drag(&D, &T, &g);
        }
        uint32_t seen = 0;
        for (int i = 0; i < 60; i++) {   /* +-0.5 degree wobble (about 1.7 px at r 200) */
            const float a = (float)rest[k] + ((i & 1) ? 0.5f : -0.5f);
            g = ev(G_DRAG_MOVE, px(200, a), py(200, a));
            omni_tmr_drag(&D, &T, &g);
            if (i < 2) seen = omni_tmr_set_ms(&T);   /* the first crossing may take the step once */
            else TEST_ASSERT_EQUAL_UINT32(seen, omni_tmr_set_ms(&T));
        }
        g = ev(G_DRAG_MOVE, px(200, (float)rest[k]), py(200, (float)rest[k]));
        omni_tmr_drag(&D, &T, &g);
    }
    g = ev(G_DRAG_END, px(200, 36), py(200, 36));
    omni_tmr_drag(&D, &T, &g);
    TEST_ASSERT_EQUAL_UINT8(3, T.set_min);   /* 36 degrees, after all that wobbling: 3 */
}

/* M3: a swipe down from the top edge reaches quick settings on the timer face in every mode: handed back by the
 * classifier where the rim claims the drag, the shell's own swipe where it does not. */
static void to_mode(omni_tmr_mode_t m);
static omni_gesture_kind_t touch_nav(const int16_t (*pts)[2], int n, int *taps, int *longs);
void test_swipe_down_from_top_every_mode(void)
{
    const omni_tmr_mode_t modes[] = { OMNI_TMR_IDLE, OMNI_TMR_ARMED, OMNI_TMR_PAUSED, OMNI_TMR_COUNT,
                                      OMNI_TMR_SW, OMNI_TMR_SWPAUSE, OMNI_TMR_DONE };
    for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        to_mode(modes[i]);
        TEST_ASSERT_EQUAL_INT(modes[i], T.mode);
        omni_tmr_t before = T;
        if (omni_tmr_claims(&T, 233, 20)) {   /* the classifier path */
            TEST_ASSERT_EQUAL(G_SWIPE_DOWN_FROM_TOP, line(233, 20, 238, 120, 5));
            TEST_ASSERT_EQUAL_MEMORY(&before, &T, sizeof T);
            TEST_ASSERT_EQUAL(G_NONE, line(233, 70, 238, 170, 5));   /* not from the top edge: nothing */
        }
        static const int16_t down[5][2] = { { 233, 20 }, { 234, 45 }, { 235, 70 }, { 236, 95 }, { 238, 120 } };
        int taps = 0, longs = 0;
        TEST_ASSERT_EQUAL(G_SWIPE_DOWN_FROM_TOP, touch_nav(down, 5, &taps, &longs));
        TEST_ASSERT_EQUAL_MEMORY(&before, &T, sizeof T);
    }
}

/* Taps and long presses never reach the classifier's state, and the press rules hold as before (R135, F1). */
void test_tap_and_long_press_unchanged(void)
{
    fresh();
    arc(200, 0, 123);   /* ARMED 10 */
    omni_tmr_t before = T;
    omni_tmr_drag_t dbefore = D;
    X0 = 233;
    Y0 = 233;
    const omni_gesture_kind_t K[] = { G_TAP, G_LONG_PRESS, G_SWIPE_L, G_SWIPE_R, G_SWIPE_UP };
    for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) {
        omni_gesture_t g = ev(K[i], 233, 233);
        TEST_ASSERT_EQUAL(G_NONE, omni_tmr_drag(&D, &T, &g));
        TEST_ASSERT_EQUAL_MEMORY(&before, &T, sizeof T);
        TEST_ASSERT_EQUAL_MEMORY(&dbefore, &D, sizeof D);
    }
    /* a centre long press is the timer's reset (no picker); a rim one is not; IDLE and DONE never hold */
    TEST_ASSERT_TRUE(omni_tmr_holds(&T, 233, 233));
    TEST_ASSERT_FALSE(omni_tmr_holds(&T, 233, 30));
    /* the centre tap starts, a second pauses; a drag on the rim never does */
    omni_tmr_tap(&T, 1000);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_COUNT, T.mode);
    omni_tmr_tap(&T, 2000);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_PAUSED, T.mode);
    TEST_ASSERT_TRUE(omni_tmr_holds(&T, 233, 233));
    arc(200, 0, 720);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_PAUSED, T.mode);
    fresh();
    TEST_ASSERT_FALSE(omni_tmr_holds(&T, 233, 233));
}

/* The whole path the device takes: the input task's gesture machine (omni_gesture_feed) with the face's claim, then
 * the face's drag handling. A still press is a tap or a long press as before; a rim turn winds; a flick navigates. */
static bool claim_cb(int16_t x0, int16_t y0, void *ctx) { return omni_tmr_claims((const omni_tmr_t *)ctx, x0, y0); }

static omni_gesture_kind_t touch(const int16_t (*pts)[2], int n, uint32_t dt, uint32_t hold_ms, int *taps, int *longs)
{
    omni_gesture_sm_t sm;
    omni_gesture_reset(&sm);
    omni_gesture_kind_t nav = G_NONE;
    uint32_t t = 0;
    const int held = (int)(hold_ms / dt);   /* still samples at the last point before the lift */
    for (int i = 0; i <= n + held; i++) {
        const bool pressed = i < n + held;
        const int16_t x = pts[i < n ? i : n - 1][0], y = pts[i < n ? i : n - 1][1];
        omni_gesture_t out[2];
        const int k = omni_gesture_feed(&sm, t, pressed, x, y, claim_cb, &T, out);
        for (int j = 0; j < k; j++) {
            if (out[j].kind == G_TAP) (*taps)++;
            else if (out[j].kind == G_LONG_PRESS) (*longs)++;
            else if (out[j].kind == G_SWIPE_L || out[j].kind == G_SWIPE_R || out[j].kind == G_SWIPE_DOWN_FROM_TOP)
                nav = out[j].kind;   /* the shell's own */
            else {
                const omni_gesture_kind_t s = omni_tmr_drag(&D, &T, &out[j]);
                if (s != G_NONE) nav = s;
            }
        }
        t += dt;
    }
    return nav;
}

static omni_gesture_kind_t touch_nav(const int16_t (*pts)[2], int n, int *taps, int *longs)
{
    return touch(pts, n, 16, 0, taps, longs);
}

static void to_mode(omni_tmr_mode_t m)
{
    fresh();
    switch (m) {
    case OMNI_TMR_ARMED: arc(200, 0, 123); break;
    case OMNI_TMR_PAUSED: arc(200, 0, 123); omni_tmr_tap(&T, 1000); omni_tmr_tap(&T, 2000); break;
    case OMNI_TMR_COUNT: arc(200, 0, 123); omni_tmr_tap(&T, 1000); break;
    case OMNI_TMR_SW: omni_tmr_tap(&T, 0); break;
    case OMNI_TMR_SWPAUSE: omni_tmr_tap(&T, 0); omni_tmr_tap(&T, 10); break;
    case OMNI_TMR_DONE: omni_tmr_start_seconds(&T, 0, 1); omni_tmr_tick(&T, 1000); break;
    default: break;
    }
}

void test_end_to_end_with_gesture_machine(void)
{
    int taps = 0, longs = 0;
    fresh();
    /* a still centre press: a tap (IDLE -> the stopwatch is the face's business, the classifier sees nothing) */
    static const int16_t centre[1][2] = { { 233, 233 } };
    TEST_ASSERT_EQUAL(G_NONE, touch(centre, 1, 16, 100, &taps, &longs));
    TEST_ASSERT_EQUAL_INT(1, taps);
    TEST_ASSERT_EQUAL_INT(0, longs);
    /* a still rim press held 700 ms: a long press, no drag */
    static const int16_t rim[1][2] = { { 233, 30 } };
    TEST_ASSERT_EQUAL(G_NONE, touch(rim, 1, 16, 700, &taps, &longs));
    TEST_ASSERT_EQUAL_INT(1, longs);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_IDLE, T.mode);
    /* a full turn round the rim: 30 min, armed */
    int16_t ring[121][2];
    for (int i = 0; i <= 120; i++) {
        ring[i][0] = px(200, (float)i * 3.0f);
        ring[i][1] = py(200, (float)i * 3.0f);
    }
    TEST_ASSERT_EQUAL(G_NONE, touch((const int16_t(*)[2])ring, 121, 16, 0, &taps, &longs));
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, T.mode);
    TEST_ASSERT_EQUAL_UINT8(30, T.set_min);
    /* a flick from the 9 o'clock rim: the rim claimed it, the classifier hands back the swipe */
    static const int16_t flick[6][2] = { { 25, 233 }, { 45, 234 }, { 85, 236 }, { 125, 238 }, { 165, 240 }, { 205, 242 } };
    TEST_ASSERT_EQUAL(G_SWIPE_R, touch(flick, 6, 16, 0, &taps, &longs));
    TEST_ASSERT_EQUAL_UINT8(30, T.set_min);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, T.mode);
    /* a flick from the centre is the shell's swipe, as everywhere */
    static const int16_t mid[4][2] = { { 280, 233 }, { 240, 233 }, { 200, 233 }, { 160, 233 } };
    TEST_ASSERT_EQUAL(G_SWIPE_L, touch(mid, 4, 16, 0, &taps, &longs));
    TEST_ASSERT_EQUAL_INT(1, taps);
    TEST_ASSERT_EQUAL_INT(1, longs);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_jog_one_turn_is_30_min);
    RUN_TEST(test_jog_two_turns_is_60_and_clamps);
    RUN_TEST(test_jog_lift_and_replace_accumulates);
    RUN_TEST(test_jog_anticlockwise_subtracts_and_clamps_at_0);
    RUN_TEST(test_flick_from_rim_navigates);
    RUN_TEST(test_slow_arc_under_threshold_changes_nothing);
    RUN_TEST(test_band_left_is_never_a_jog);
    RUN_TEST(test_claim_threshold);
    RUN_TEST(test_jog_by_mode);
    RUN_TEST(test_band_hysteresis);
    RUN_TEST(test_claim_gives_one_minute);
    RUN_TEST(test_boundary_rest_does_not_toggle);
    RUN_TEST(test_swipe_down_from_top_every_mode);
    RUN_TEST(test_tap_and_long_press_unchanged);
    RUN_TEST(test_end_to_end_with_gesture_machine);
    return UNITY_END();
}
