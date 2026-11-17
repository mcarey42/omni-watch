#include "unity.h"
#include "omni/omni_model.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Expected numbers come from the mockup's own functions: node tools/omni_model_oracle.mjs (faces-omni.js dateVals,
 * handAngles, spinOff, E.outBack/ph and the model() settle). */

#define R2D (180.0f / 3.14159265358979f)

static void check_vals(int y, int m, int d, float a, float b, float c, float e)
{
    float v[OMNI_RINGS];
    omni_dial_vals(y, m, d, v);
    TEST_ASSERT_EQUAL_FLOAT(a, v[0]);
    TEST_ASSERT_EQUAL_FLOAT(b, v[1]);
    TEST_ASSERT_EQUAL_FLOAT(c, v[2]);
    TEST_ASSERT_EQUAL_FLOAT(e, v[3]);
}

static void test_dial_vals_dates(void)
{
    check_vals(2026, 10, 22, 9, 22, 26, 40);
    check_vals(1776, 7, 4, 6, 4, 76, 37);
    check_vals(-44, 3, 15, 2, 15, 44, 19);
    check_vals(-48, 8, 9, 7, 9, 48, 19);
    check_vals(1999, 12, 31, 11, 31, 99, 39);
    check_vals(2000, 1, 1, 0, 1, 0, 40);
    check_vals(100, 1, 1, 0, 1, 0, 21);
    check_vals(-2000, 1, 1, 0, 1, 0, 0);
}

/* The mockup's handAngles, not the brief's 0.5 deg: the minute rounded to 1/720 rad (63.9803, brief 64.0); the hour
 * rounded to the same 1/720 rad step (R90, amending R84's unquantised hour: 305.3388 at 10:10:40). */
static void test_hand_angles(void)
{
    float h, m;
    omni_hand_angles(10, 10, 40, &h, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.329166666666667f, h);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.1166666666666667f, m);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 305.33876f, h * R2D);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 63.98029f, m * R2D);
    omni_hand_angles(3, 0, 0, &h, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1131.0f / 720.0f, h);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0, m * R2D);
    omni_hand_angles(15, 0, 0, &h, &m);   /* 24 h clock */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1131.0f / 720.0f, h);
    omni_hand_angles(11, 59, 59, &h, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 4524.0f / 720.0f, h);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 6.281944444444444f, m);
    omni_hand_angles(7, 30, 30, &h, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2831.0f / 720.0f, h);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.1944444444444446f, m);
}

static void test_spin_off_matches_mockup(void)
{
    static const uint32_t ft[5] = { 760, 1000, 1500, 2500, 4360 };
    static const float fv[5] = { 990, 946.0185344f, 755.6500747f, 305.7887003f, 0 };
    for (int i = 0; i < 5; i++) TEST_ASSERT_FLOAT_WITHIN(1e-3f, fv[i], omni_spin_off(ft[i], 760, 3600, 990));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 990, omni_spin_off(0, 760, 3600, 990));   /* before t0: k */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 470.8067369f, omni_spin_off(500, 150, 2600, 540));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 140.1797908f, omni_spin_off(1500, 150, 2600, 540));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0, omni_spin_off(3000, 150, 2600, 540));
    /* across the u32 wrap */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 140.1797908f, omni_spin_off(0xFFFFFF00u + 1500, 0xFFFFFF00u + 150, 2600, 540));
}

static omni_model_t g_m;
static const struct tm DAY = { .tm_year = 126, .tm_mon = 9, .tm_mday = 22, .tm_hour = 10, .tm_min = 10, .tm_sec = 40 };

static void test_settle_matches_mockup(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1, omni_settle(80, 80, 1100));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.283450558f, omni_settle(300, 80, 1100));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.0659625634f, omni_settle(600, 80, 1100));   /* overshoot */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, omni_settle(1100, 80, 1100));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, omni_settle(5000, 80, 1100));
    /* through the model, the DIAL entry: val = today - offset, k = (1.4, -2.2, 3.5, -1.2) */
    static const float want300[4] = { 0.3968307815f, -0.6235912281f, 0.9920769538f, -0.3401406699f };
    static const float want600[4] = { -0.0923475888f, 0.1451176395f, -0.2308689720f, 0.0791550761f };
    static const float today[4] = { 9, 22, 26, 40 };
    omni_model_init(&g_m, 40, -83);
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_DIAL, false);
    omni_dial_state_t s;
    omni_model_step(&g_m, 1000, true, &DAY, OMNI_LENS_OFF, &s);
    for (int i = 0; i < 4; i++) TEST_ASSERT_FLOAT_WITHIN(1e-4f, today[i] - 1.0f * (i == 0 ? 1.4f : i == 1 ? -2.2f : i == 2 ? 3.5f : -1.2f), s.val[i]);
    omni_model_step(&g_m, 1300, true, &DAY, OMNI_LENS_OFF, &s);
    for (int i = 0; i < 4; i++) TEST_ASSERT_FLOAT_WITHIN(1e-4f, today[i] - want300[i], s.val[i]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 8.6032f, s.val[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1, s.mark_old);
    omni_model_step(&g_m, 1600, true, &DAY, OMNI_LENS_OFF, &s);
    for (int i = 0; i < 4; i++) TEST_ASSERT_FLOAT_WITHIN(1e-4f, today[i] - want600[i], s.val[i]);
    omni_model_step(&g_m, 2100, true, &DAY, OMNI_LENS_OFF, &s);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(today[i], s.val[i]);
    /* the other entries, reduced motion and a skip do not settle */
    omni_model_wake(&g_m, 0, &OMNI_ENTRY_FLIP, false);
    omni_model_step(&g_m, 300, true, &DAY, OMNI_LENS_OFF, &s);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(today[i], s.val[i]);
    omni_model_wake(&g_m, 0, &OMNI_ENTRY_DIAL, true);
    omni_model_step(&g_m, 300, true, &DAY, OMNI_LENS_OFF, &s);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(today[i], s.val[i]);
    omni_model_wake(&g_m, 0, &OMNI_ENTRY_DIAL, false);
    omni_model_step(&g_m, 100, true, &DAY, OMNI_LENS_OFF, &s);
    omni_model_skip(&g_m, 100);
    omni_model_step(&g_m, 300, true, &DAY, OMNI_LENS_OFF, &s);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(today[i], s.val[i]);
}

static void test_ring_change_forward(void)
{
    const float from[4] = { 11, 31, 99, 41 }, to[4] = { 0, 1, 0, 40 };
    float out[4];
    omni_ring_change(from, to, true, 0, out);
    TEST_ASSERT_EQUAL_FLOAT(12, out[0]);    /* 11 -> 0 (month, n 12) ends at 12 */
    TEST_ASSERT_EQUAL_FLOAT(33, out[1]);    /* 31 -> 1 (day, n 32) ends at 33, passing 32 */
    TEST_ASSERT_EQUAL_FLOAT(100, out[2]);   /* 99 -> 0 (year) ends at 100 */
    TEST_ASSERT_EQUAL_FLOAT(82, out[3]);    /* 41 -> 40 forward: the long way round (n 42) */
    omni_ring_change(from, to, true, 1, out);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(from[i], out[i]);
    omni_ring_change(from, to, true, 0.5f, out);
    TEST_ASSERT_EQUAL_FLOAT(11.5f, out[0]);
    TEST_ASSERT_EQUAL_FLOAT(32, out[1]);
    omni_ring_change(from, to, true, -0.1f, out);   /* the overshoot passes the target */
    TEST_ASSERT_TRUE(out[1] > 33);
    /* a step back: the shortest way */
    omni_ring_change(from, to, false, 0, out);
    TEST_ASSERT_EQUAL_FLOAT(12, out[0]);    /* 11 -> 0 is +1 */
    TEST_ASSERT_EQUAL_FLOAT(33, out[1]);    /* 31 -> 1 is +2 */
    TEST_ASSERT_EQUAL_FLOAT(100, out[2]);
    TEST_ASSERT_EQUAL_FLOAT(40, out[3]);    /* 41 -> 40 is -1 */
    const float f2[4] = { 0, 1, 0, 40 }, t2[4] = { 11, 31, 99, 41 };
    omni_ring_change(f2, t2, false, 0, out);
    TEST_ASSERT_EQUAL_FLOAT(-1, out[0]);
    TEST_ASSERT_EQUAL_FLOAT(-1, out[1]);
    TEST_ASSERT_EQUAL_FLOAT(-1, out[2]);
    TEST_ASSERT_EQUAL_FLOAT(41, out[3]);
}

static void check_finite(const omni_dial_state_t *s)
{
    const float *f = (const float *)s->val;
    for (int i = 0; i < 12; i++) TEST_ASSERT_TRUE(isfinite(f[i]));
    TEST_ASSERT_TRUE(isfinite(s->mark_old) && isfinite(s->mark_new) && isfinite(s->hour_rad) && isfinite(s->min_rad));
    TEST_ASSERT_TRUE(isfinite(s->lon_deg) && isfinite(s->red) && isfinite(s->green) && isfinite(s->flash));
    TEST_ASSERT_TRUE(isfinite(s->loupe) && isfinite(s->marker_a) && isfinite(s->lon_speed));
    for (int i = 0; i < 4; i++) TEST_ASSERT_TRUE(isfinite(s->speed[i]));
}

static void test_unset_model(void)
{
    omni_model_init(&g_m, 40, -83);
    omni_model_wake(&g_m, 5000, &OMNI_ENTRY_DIAL, false);
    float lo = 9, hi = -9;
    for (uint32_t t = 5000; t < 5000 + 7200; t += 33) {
        omni_dial_state_t s;
        omni_model_step(&g_m, t, false, NULL, OMNI_LENS_UNSET, &s);
        check_finite(&s);
        const float want[4] = { 0, 1, 0, 40 };
        for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(want[i], s.val[i]);
        TEST_ASSERT_EQUAL_FLOAT(0, s.mark_old);
        TEST_ASSERT_EQUAL_FLOAT(0, s.mark_new);
        TEST_ASSERT_EQUAL_FLOAT(0, s.marker_a);
        TEST_ASSERT_EQUAL_FLOAT(0, s.hour_rad);
        TEST_ASSERT_EQUAL_FLOAT(0, s.min_rad);
        TEST_ASSERT_EQUAL_FLOAT(0, s.green);
        if (s.red < lo) lo = s.red;
        if (s.red > hi) hi = s.red;
        TEST_ASSERT_TRUE(s.red >= 0.25f - 1e-6f && s.red <= 1 + 1e-6f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.25f, lo);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1, hi);
    /* the mockup's lit: 0.25 + 0.75 smooth(sin(T / 380) / 2 + 0.5) */
    omni_dial_state_t s;
    omni_model_step(&g_m, 5000, false, NULL, OMNI_LENS_UNSET, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.625f, s.red);
    omni_model_step(&g_m, 5300, false, NULL, OMNI_LENS_UNSET, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.9572618698f, s.red);
    omni_model_step(&g_m, 6194, false, NULL, OMNI_LENS_UNSET, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.6247116571f, s.red);
}

static void test_lens_levels(void)
{
    omni_dial_state_t s;
    omni_model_init(&g_m, 40, -83);
    omni_model_wake(&g_m, 0, &OMNI_ENTRY_DIAL, false);
    omni_model_step(&g_m, 2300, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(0, s.green);
    omni_model_step(&g_m, 2475, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, s.green);
    omni_model_step(&g_m, 2650, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(1, s.green);
    TEST_ASSERT_EQUAL_FLOAT(0, s.red);
    omni_model_step(&g_m, 9000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(1, s.green);
    TEST_ASSERT_EQUAL_FLOAT(0, s.red);
    omni_model_step(&g_m, 9033, true, &DAY, OMNI_LENS_STALE, &s);
    TEST_ASSERT_EQUAL_FLOAT(0.35f, s.red);
    TEST_ASSERT_EQUAL_FLOAT(1, s.green);
    TEST_ASSERT_TRUE(s.marker);
    TEST_ASSERT_EQUAL_FLOAT(0.9f, s.marker_a);
    TEST_ASSERT_EQUAL_FLOAT(0, s.marker_pulse);
    TEST_ASSERT_EQUAL_FLOAT(1, s.mark_old);
    TEST_ASSERT_EQUAL_FLOAT(0, s.mark_new);
    /* the FLIP and IRIS greens, and a skip, and no entry (at rest) */
    omni_model_wake(&g_m, 0, &OMNI_ENTRY_FLIP, false);
    omni_model_step(&g_m, 3570, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(0, s.green);
    omni_model_step(&g_m, 3920, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(1, s.green);
    omni_model_wake(&g_m, 0, &OMNI_ENTRY_IRIS, false);
    omni_model_step(&g_m, 3900, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(0, s.green);
    omni_model_skip(&g_m, 3900);
    omni_model_step(&g_m, 3933, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(1, s.green);
    omni_model_wake(&g_m, 100, NULL, false);
    omni_model_step(&g_m, 100, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(1, s.green);
}

static void test_globe_spin_and_reduced(void)
{
    omni_dial_state_t s;
    omni_model_init(&g_m, 40, -83);
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_DIAL, false);
    omni_model_step(&g_m, 1000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -83 + 540 - 360, s.lon_deg);   /* wrapped to [-180, 180) */
    omni_model_step(&g_m, 1000 + 1500, true, &DAY, OMNI_LENS_OFF, &s);
    const double want = -83 + 140.1797908 - 2.2 * 1.35;
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)want, s.lon_deg);
    omni_model_step(&g_m, 1000 + 2750, true, &DAY, OMNI_LENS_OFF, &s);   /* spin done: drift only */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)(-83 - 2.2 * 2.6), s.lon_deg);
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_DIAL, true);
    omni_model_step(&g_m, 1000 + 2750, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -83, s.lon_deg);
    omni_model_wake(&g_m, 1000, NULL, false);   /* at rest: drift from the wake */
    omni_model_step(&g_m, 11000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -83 - 22, s.lon_deg);
}

static void test_rollover(void)
{
    omni_dial_state_t s;
    struct tm a = DAY, b = DAY;
    a.tm_mday = 31; a.tm_mon = 9;
    b.tm_mday = 1; b.tm_mon = 10;
    omni_model_init(&g_m, 40, -83);
    omni_model_wake(&g_m, 0, NULL, false);
    omni_model_step(&g_m, 0, true, &a, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(31, s.val[1]);
    TEST_ASSERT_EQUAL_FLOAT(0, s.speed[1]);
    omni_model_step(&g_m, 1000, true, &b, OMNI_LENS_OFF, &s);   /* the change: starts at the old day */
    TEST_ASSERT_EQUAL_FLOAT(31, s.val[1]);
    TEST_ASSERT_EQUAL_FLOAT(9, s.val[0]);    /* month 9 -> 10 follows */
    omni_model_step(&g_m, 1000 + 590, true, &b, OMNI_LENS_OFF, &s);   /* mid-settle: between 31 and 33 */
    TEST_ASSERT_TRUE(s.val[1] > 31 && s.val[1] < 34);
    TEST_ASSERT_TRUE(s.speed[1] > 0);
    omni_model_step(&g_m, 1000 + 1200, true, &b, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(1, s.val[1]);   /* done: today's value, not 33 */
    TEST_ASSERT_EQUAL_FLOAT(10, s.val[0]);
    TEST_ASSERT_TRUE(s.speed[1] < 0.5f);    /* 33 -> 1 is no motion (cyclic) */
    /* the clock set from unset does not animate */
    omni_model_step(&g_m, 3000, false, NULL, OMNI_LENS_UNSET, &s);
    omni_model_step(&g_m, 3033, true, &a, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(31, s.val[1]);
    /* a step back takes the short way (1 -> 31 through 0) */
    omni_model_step(&g_m, 3066, true, &b, OMNI_LENS_OFF, &s);
    omni_model_step(&g_m, 3066 + 1200, true, &b, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(1, s.val[1]);
    omni_model_step(&g_m, 4300, true, &a, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(1, s.val[1]);
    omni_model_step(&g_m, 4300 + 590, true, &a, OMNI_LENS_OFF, &s);
    TEST_ASSERT_TRUE(s.val[1] < 1 && s.val[1] > -1.5f);
    omni_model_step(&g_m, 4300 + 1200, true, &a, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(31, s.val[1]);
    /* T5 m6: a change inside another's settle starts from the displayed rings (a step back mid-roll: no jump) */
    omni_model_step(&g_m, 6000, true, &b, OMNI_LENS_OFF, &s);
    omni_model_step(&g_m, 6000 + 400, true, &b, OMNI_LENS_OFF, &s);
    const float shown = s.val[1];
    TEST_ASSERT_TRUE(shown > 31.5f && shown < 33.5f);
    omni_model_step(&g_m, 6432, true, &a, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, shown, s.val[1]);
    omni_model_step(&g_m, 6432 + 1200, true, &a, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(31, s.val[1]);
}

static void test_loupe(void)
{
    omni_dial_state_t s;
    omni_model_init(&g_m, 40, -83);
    omni_model_wake(&g_m, 0, NULL, false);
    omni_model_step(&g_m, 100, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(0, s.loupe);
    omni_model_loupe(&g_m, 1000);
    static const uint32_t dt[7] = { 0, 150, 300, 3000, 3600, 3750, 3900 };
    static const float want[7] = { 0, 0.5f, 1, 1, 1, 0.5f, 0 };
    for (int i = 0; i < 7; i++) {
        omni_model_step(&g_m, 1000 + dt[i], true, &DAY, OMNI_LENS_OFF, &s);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, want[i], s.loupe);
    }
    omni_model_step(&g_m, 6000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(0, s.loupe);
    /* a tap while open starts the close at once (the mockup: loupeT = t - 3600) */
    omni_model_loupe(&g_m, 10000);
    omni_model_step(&g_m, 11000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(1, s.loupe);
    omni_model_loupe(&g_m, 11000);
    omni_model_step(&g_m, 11150, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, s.loupe);
    omni_model_step(&g_m, 11400, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_EQUAL_FLOAT(0, s.loupe);
}

static void test_hits(void)
{
    TEST_ASSERT_EQUAL(OMNI_HIT_RED, omni_hit(125, 47, false));
    TEST_ASSERT_EQUAL(OMNI_HIT_GREEN, omni_hit(340, 60, false));
    TEST_ASSERT_EQUAL(OMNI_HIT_MAG, omni_hit(233, 70, false));
    TEST_ASSERT_EQUAL(OMNI_HIT_MAG, omni_hit(233, 70, true));
    TEST_ASSERT_EQUAL(OMNI_HIT_LOUPE_OFF, omni_hit(233, 300, true));
    TEST_ASSERT_EQUAL(OMNI_HIT_NONE, omni_hit(233, 300, false));
    TEST_ASSERT_EQUAL(OMNI_HIT_NONE, omni_hit(233, 400, false));
    TEST_ASSERT_EQUAL(OMNI_HIT_NONE, omni_hit(233, 400, true));
    /* edges: 38 px from the lens, the magnifier box, the loupe box */
    TEST_ASSERT_EQUAL(OMNI_HIT_RED, omni_hit(125 + 37, 47, false));
    TEST_ASSERT_EQUAL(OMNI_HIT_NONE, omni_hit(125 + 39, 47 + 20, false));
    TEST_ASSERT_EQUAL(OMNI_HIT_MAG, omni_hit(196, 9, false));
    TEST_ASSERT_EQUAL(OMNI_HIT_NONE, omni_hit(195, 70, false));
    TEST_ASSERT_EQUAL(OMNI_HIT_NONE, omni_hit(271, 70, false));
    TEST_ASSERT_EQUAL(OMNI_HIT_NONE, omni_hit(233, 130, false));
    TEST_ASSERT_EQUAL(OMNI_HIT_LOUPE_OFF, omni_hit(233 + 49, 233 + 99, true));
    TEST_ASSERT_EQUAL(OMNI_HIT_NONE, omni_hit(233 + 50, 233, true));
}

static void test_model_long_run(void)
{
    /* 10 h of 33 ms frames from 20:00 on 2026-10-22 (a midnight rollover in it), a loupe tap on the way; a start at
     * 0xFFFF0000 (the u32 wraps 10 h in) gives the same states as a start at 0 */
    static omni_model_t ma, mb;
    const uint32_t base_b = 0xFFFF0000u;
    omni_model_init(&ma, 40, -83);
    omni_model_init(&mb, 40, -83);
    omni_model_wake(&ma, 0, &OMNI_ENTRY_DIAL, false);
    omni_model_wake(&mb, base_b, &OMNI_ENTRY_DIAL, false);
    const uint32_t frames = 10u * 3600u * 1000u / 33u;
    unsigned moved = 0, mism = 0;
    for (uint32_t f = 0; f < frames; f++) {
        const uint32_t el = f * 33u;
        struct tm tmv = DAY;
        tmv.tm_hour = 20 + (int)(el / 3600000u);   /* 20..29: crosses midnight into the 23rd */
        tmv.tm_min = (int)(el / 60000u % 60u);
        tmv.tm_sec = (int)(el / 1000u % 60u);
        if (tmv.tm_hour >= 24) { tmv.tm_hour -= 24; tmv.tm_mday = 23; }
        if (f == 5000) { omni_model_loupe(&ma, el); omni_model_loupe(&mb, base_b + el); }
        omni_dial_state_t a, b;
        omni_model_step(&ma, el, true, &tmv, OMNI_LENS_OFF, &a);
        omni_model_step(&mb, base_b + el, true, &tmv, OMNI_LENS_OFF, &b);
        if (a.lon_deg < -180 || a.lon_deg >= 180 || !isfinite(a.lon_deg)) TEST_FAIL_MESSAGE("lon out of [-180, 180)");
        if (memcmp(&a, &b, sizeof a)) mism++;
        if (a.speed[1] > 0.01f) moved++;
        if (f % 1000 == 0) check_finite(&a);
    }
    TEST_ASSERT_EQUAL_UINT(0, mism);
    TEST_ASSERT_TRUE(moved > 10);   /* the midnight roll was seen */
}

/* Task 11: omni_model_reveal_done (lid gone / iris open; at once with no lid or iris, or once skipped), the red lens's
 * readiness (reveal done + 400 ms, the mockup's REVEAL_END + 400), and omni_model_revealing (a tap skips): the entry's
 * lid or iris, spin-up, settle and green, whichever ends last. Reduced motion (the mockup): the lid and iris at t x 2.2,
 * no spin, no drift, no settle. */
static void test_reveal_done(void)
{
    omni_dial_state_t s;
    omni_model_init(&g_m, 40, -83);
    /* the lid leaves the screen at 73.37 deg, about 485 ms on the device's timeline */
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_FLIP, false);
    TEST_ASSERT_FALSE(omni_model_reveal_done(&g_m, 1000));
    TEST_ASSERT_FALSE(omni_model_reveal_done(&g_m, 1000 + 470));
    TEST_ASSERT_TRUE(omni_model_reveal_done(&g_m, 1000 + 500));
    TEST_ASSERT_FALSE(omni_model_reveal_done(&g_m, 1000 - 400));   /* before the wake */
    omni_model_step(&g_m, 1000 + 470, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_TRUE(s.lid);
    omni_model_step(&g_m, 1000 + 500, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FALSE(s.lid);
    /* red: 400 ms after it */
    TEST_ASSERT_FALSE(omni_model_red_ready(&g_m, 1000 + 870));
    TEST_ASSERT_TRUE(omni_model_red_ready(&g_m, 1000 + 900));
    /* reduced: t x 2.2, gone at about 220 ms */
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_FLIP, true);
    TEST_ASSERT_FALSE(omni_model_reveal_done(&g_m, 1000 + 210));
    TEST_ASSERT_TRUE(omni_model_reveal_done(&g_m, 1000 + 230));
    /* the iris is open at 1420 ms (645.5 ms reduced) */
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_IRIS, false);
    TEST_ASSERT_FALSE(omni_model_reveal_done(&g_m, 1000 + 1410));
    TEST_ASSERT_TRUE(omni_model_reveal_done(&g_m, 1000 + 1420));
    TEST_ASSERT_FALSE(omni_model_red_ready(&g_m, 1000 + 1810));
    TEST_ASSERT_TRUE(omni_model_red_ready(&g_m, 1000 + 1820));
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_IRIS, true);
    TEST_ASSERT_FALSE(omni_model_reveal_done(&g_m, 1000 + 640));
    TEST_ASSERT_TRUE(omni_model_reveal_done(&g_m, 1000 + 650));
    /* no lid or iris: done at once, red 400 ms after the wake */
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_DIAL, false);
    TEST_ASSERT_TRUE(omni_model_reveal_done(&g_m, 1000));
    TEST_ASSERT_FALSE(omni_model_red_ready(&g_m, 1000 + 390));
    TEST_ASSERT_TRUE(omni_model_red_ready(&g_m, 1000 + 400));
    omni_model_wake(&g_m, 1000, NULL, false);
    TEST_ASSERT_TRUE(omni_model_reveal_done(&g_m, 1000));
    /* a skip: done from the skip, red 400 ms after it */
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_IRIS, false);
    omni_model_skip(&g_m, 1300);
    TEST_ASSERT_TRUE(omni_model_reveal_done(&g_m, 1300));
    TEST_ASSERT_FALSE(omni_model_red_ready(&g_m, 1690));
    TEST_ASSERT_TRUE(omni_model_red_ready(&g_m, 1700));
    /* across the u32 wrap */
    omni_model_wake(&g_m, 0xFFFFFF00u, &OMNI_ENTRY_FLIP, false);
    TEST_ASSERT_FALSE(omni_model_reveal_done(&g_m, 0xFFFFFF00u + 470u));
    TEST_ASSERT_TRUE(omni_model_reveal_done(&g_m, 0xFFFFFF00u + 500u));
}

static void test_revealing(void)
{
    omni_model_init(&g_m, 40, -83);
    /* m1: only while something visibly moves (lid or iris, spin-up, settle), not green's ramp */
    /* FLIP: the spin 330 + 3600 = 3930 (green ends 3920) */
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_FLIP, false);
    TEST_ASSERT_TRUE(omni_model_revealing(&g_m, 1000));
    TEST_ASSERT_TRUE(omni_model_revealing(&g_m, 1000 + 3929));
    TEST_ASSERT_FALSE(omni_model_revealing(&g_m, 1000 + 3930));
    /* reduced: the lid alone (gone at about 220 ms); green still ramps to 3920, but a tap at 1 s is the magnifier's */
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_FLIP, true);
    TEST_ASSERT_TRUE(omni_model_revealing(&g_m, 1000 + 210));
    TEST_ASSERT_FALSE(omni_model_revealing(&g_m, 1000 + 230));
    TEST_ASSERT_FALSE(omni_model_revealing(&g_m, 1000 + 1000));
    /* IRIS: the spin 620 + 3600 = 4220 (green's 4250 does not count); reduced: open at 645.5 ms */
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_IRIS, false);
    TEST_ASSERT_TRUE(omni_model_revealing(&g_m, 1000 + 4219));
    TEST_ASSERT_FALSE(omni_model_revealing(&g_m, 1000 + 4220));
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_IRIS, true);
    TEST_ASSERT_TRUE(omni_model_revealing(&g_m, 1000 + 640));
    TEST_ASSERT_FALSE(omni_model_revealing(&g_m, 1000 + 650));
    /* DIAL: the spin 150 + 2600 = 2750; reduced: nothing moves at all */
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_DIAL, false);
    TEST_ASSERT_TRUE(omni_model_revealing(&g_m, 1000 + 2749));
    TEST_ASSERT_FALSE(omni_model_revealing(&g_m, 1000 + 2750));
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_DIAL, true);
    TEST_ASSERT_FALSE(omni_model_revealing(&g_m, 1000));
    /* at rest, and once skipped: nothing to skip */
    omni_model_wake(&g_m, 1000, NULL, false);
    TEST_ASSERT_FALSE(omni_model_revealing(&g_m, 1000));
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_FLIP, false);
    omni_model_skip(&g_m, 1100);
    TEST_ASSERT_FALSE(omni_model_revealing(&g_m, 1100));
}

static void test_reduced_motion_reveal(void)
{
    omni_dial_state_t s;
    omni_model_init(&g_m, 40, -83);
    for (int e = 0; e < 3; e++) {
        const omni_entry_t *en = e == 0 ? &OMNI_ENTRY_DIAL : e == 1 ? &OMNI_ENTRY_FLIP : &OMNI_ENTRY_IRIS;
        omni_model_wake(&g_m, 1000, en, true);
        float today[OMNI_RINGS];
        omni_dial_vals(2026, 10, 22, today);
        for (uint32_t t = 0; t <= 6000; t += 100) {
            omni_model_step(&g_m, 1000 + t, true, &DAY, OMNI_LENS_OFF, &s);
            TEST_ASSERT_EQUAL_FLOAT(-83, s.lon_deg);           /* no spin, no drift */
            for (int i = 0; i < OMNI_RINGS; i++) TEST_ASSERT_EQUAL_FLOAT(today[i], s.val[i]);   /* no settle */
        }
    }
    /* the lid at t x 2.2: on at 200 ms, gone by 230; the iris at 640, open by 650 */
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_FLIP, true);
    omni_model_step(&g_m, 1200, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_TRUE(s.lid);
    omni_model_step(&g_m, 1230, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FALSE(s.lid);
    omni_model_wake(&g_m, 1000, &OMNI_ENTRY_IRIS, true);
    omni_model_step(&g_m, 1640, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_TRUE(s.iris);
    omni_model_step(&g_m, 1650, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FALSE(s.iris);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dial_vals_dates);
    RUN_TEST(test_hand_angles);
    RUN_TEST(test_spin_off_matches_mockup);
    RUN_TEST(test_settle_matches_mockup);
    RUN_TEST(test_ring_change_forward);
    RUN_TEST(test_unset_model);
    RUN_TEST(test_lens_levels);
    RUN_TEST(test_globe_spin_and_reduced);
    RUN_TEST(test_rollover);
    RUN_TEST(test_loupe);
    RUN_TEST(test_hits);
    RUN_TEST(test_model_long_run);
    RUN_TEST(test_reveal_done);
    RUN_TEST(test_revealing);
    RUN_TEST(test_reduced_motion_reveal);
    return UNITY_END();
}
