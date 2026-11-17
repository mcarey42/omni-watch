#include "unity.h"
#include "omni/globe_motion.h"

void setUp(void) {}
void tearDown(void) {}

static omni_reveal_t r;

static void test_reveal_live_curve(void)
{
    omni_reveal_live(0, false, &r);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -220.0f, r.lon_off_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.fade);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.35f, r.gain);
    TEST_ASSERT_FALSE(r.done);
    omni_reveal_live(350, false, &r);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, r.fade);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, r.gain);
    omni_reveal_live(700, false, &r);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -27.5f, r.lon_off_deg);
    omni_reveal_live(1400, false, &r);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.lon_off_deg);
    TEST_ASSERT_TRUE(r.done);
    omni_reveal_live(0, true, &r);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.lon_off_deg);
    omni_reveal_live(250, true, &r);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.lon_off_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, r.fade);
    TEST_ASSERT_FALSE(r.done);
    omni_reveal_live(500, true, &r);
    TEST_ASSERT_TRUE(r.done);
}

static void test_reveal_analog_curve(void)
{
    omni_reveal_analog(0, false, &r);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -160.0f, r.lon_off_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.fade);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.36f, r.gain);
    omni_reveal_analog(300, false, &r);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, r.fade);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.9f, r.gain);
    omni_reveal_analog(450, false, &r);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, r.settle);
    TEST_ASSERT_FALSE(r.done);
    omni_reveal_analog(1200, false, &r);
    TEST_ASSERT_TRUE(r.done);
    omni_reveal_analog(0, true, &r);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, r.settle);
    TEST_ASSERT_FALSE(r.done);
    omni_reveal_analog(300, true, &r);
    TEST_ASSERT_TRUE(r.done);
}

static void test_reveal_spin_curve(void)
{
    omni_reveal_spin(450, false, &r);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.875f, r.fade);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.906f, r.gain);
    omni_reveal_spin(200, true, &r);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, r.fade);
}

static void test_spin_flywheel(void)
{
    omni_spin_t s = {0};
    omni_spin_wake(&s, 1000, false);
    TEST_ASSERT_EQUAL_FLOAT(520.0f, s.v);
    for (int i = 1; i <= 180; i++) {
        omni_spin_step(&s, 1.0f / 60.0f);
        TEST_ASSERT_TRUE(s.v >= OMNI_SPIN_IDLE);
        if (i == 90) {
            TEST_ASSERT_TRUE(s.v > 38.0f && s.v < 43.0f);
        }
    }
    TEST_ASSERT_TRUE(s.v > 7.0f && s.v < 10.0f);
    omni_spin_wake(&s, 0, true);
    TEST_ASSERT_EQUAL_FLOAT(OMNI_SPIN_IDLE, s.v);
}

static void test_spin_fling_clamped(void)
{
    omni_spin_t s = {0};
    omni_spin_down(&s, 100, 50);
    omni_spin_move(&s, 110, 150);
    TEST_ASSERT_EQUAL_FLOAT(900.0f, s.v);
    omni_spin_up(&s, 112);
    TEST_ASSERT_EQUAL_FLOAT(900.0f, s.v);
}

static void test_spin_still_release_idles(void)
{
    omni_spin_t s = {0};
    omni_spin_down(&s, 100, 50);
    omni_spin_move(&s, 110, 90);
    omni_spin_up(&s, 210);
    TEST_ASSERT_EQUAL_FLOAT(OMNI_SPIN_IDLE, s.v);
}

static void test_spin_tap_shows_time(void)
{
    omni_spin_t s = {0};
    omni_spin_down(&s, 0, 100);
    omni_spin_up(&s, 0);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, omni_spin_loud(&s, 3400));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, omni_spin_loud(&s, 3700));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, omni_spin_loud(&s, 4000));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, omni_spin_loud(&s, 5000));
    /* across the uint32 wrap */
    omni_spin_down(&s, 0xFFFFFF00u, 100);
    omni_spin_up(&s, 0xFFFFFF10u);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, omni_spin_loud(&s, 0xFFFFFF10u + 3400u));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, omni_spin_loud(&s, 0xFFFFFF10u + 5000u));
}

static void test_spin_lon_stays_bounded(void)
{
    omni_spin_t s = {0};
    s.v = 900.0f;
    for (int i = 0; i < 1000000; i++) {
        s.v = 900.0f;
        omni_spin_step(&s, 0.05f);
        TEST_ASSERT_TRUE(s.lon >= -180.0f && s.lon < 180.0f);
    }
}

static void test_drift_returns_home(void)
{
    omni_drift_t d = {0};
    uint32_t t = 1000;
    omni_drift_down(&d, t, 200);
    int x = 200;
    for (int i = 0; i < 10; i++) {
        t += 16;
        x -= 10;
        omni_drift_move(&d, t, (int16_t)x);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, d.off_lon);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 281.0f, d.v);
    omni_drift_up(&d, t);
    uint32_t t0 = t;
    int n = 0;
    for (; (int32_t)(t - t0) < 3900; n++) {
        t = t0 + (uint32_t)((n + 1) * 1000 / 60);
        omni_drift_step(&d, t, 1.0f / 60.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 112.0f, d.off_lon);
    for (; (int32_t)(t - t0) < 5000; n++) {
        t = t0 + (uint32_t)((n + 1) * 1000 / 60);
        omni_drift_step(&d, t, 1.0f / 60.0f);
    }
    /* pins the 0.94 per-frame return rate (0.96 would leave about 9.7 here) */
    TEST_ASSERT_FLOAT_WITHIN(0.4f, 2.73f, d.off_lon);
    for (; (int32_t)(t - t0) < 6000; n++) {
        t = t0 + (uint32_t)((n + 1) * 1000 / 60);
        omni_drift_step(&d, t, 1.0f / 60.0f);
    }
    TEST_ASSERT_TRUE(d.off_lon < 0.2f && d.off_lon > -0.2f);
    for (; (int32_t)(t - t0) < 8000; n++) {
        t = t0 + (uint32_t)((n + 1) * 1000 / 60);
        omni_drift_step(&d, t, 1.0f / 60.0f);
    }
    TEST_ASSERT_FALSE(omni_drift_moving(&d));
}

/* Signed casts: a timestamp that is "earlier" than the stored one is a negative difference, not a huge one. */
static void test_signed_time_casts(void)
{
    omni_drift_t d = {0};
    d.off_lon = 100.0f;
    d.release_t = 1000;
    omni_drift_step(&d, 500, 1.0f / 60.0f); /* before the release: no return-home decay yet */
    TEST_ASSERT_EQUAL_FLOAT(100.0f, d.off_lon);
    d.release_t = 0xFFFFFFF0u;
    omni_drift_step(&d, 0xFFFFFFE0u, 1.0f / 60.0f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, d.off_lon);
    omni_drift_step(&d, 0xFFFFFFF0u + 4100u, 1.0f / 60.0f); /* 4.1 s after, across the wrap */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 94.0f, d.off_lon);

    omni_spin_t s = {0};
    omni_spin_down(&s, 1000, 100);
    omni_spin_up(&s, 995); /* elapsed is negative, so still < 350: a tap */
    TEST_ASSERT_EQUAL_UINT32(995u + 4000u, s.show_until);

    omni_spin_down(&s, 1000, 100);
    omni_spin_move(&s, 1010, 300); /* fling, moved */
    s.last_t = 1100;
    omni_spin_up(&s, 1090); /* 10 ms before the last move: still not "held still" */
    TEST_ASSERT_EQUAL_FLOAT(900.0f, s.v);

    omni_spin_down(&s, 0xFFFFFFF0u, 100); /* a real fling straddling the wrap */
    omni_spin_move(&s, 0xFFFFFFF8u, 300);
    omni_spin_up(&s, 0x00000004u);
    TEST_ASSERT_EQUAL_FLOAT(900.0f, s.v);
}

static void test_dt_clamps(void)
{
    omni_spin_t s = {0};
    s.v = 520.0f;
    omni_spin_step(&s, 1.0f); /* clamped to 0.05 s */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 475.42f, s.v);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -23.77f, s.lon);

    omni_drift_t d = {0};
    d.v = 281.0f;
    d.release_t = 0;
    omni_drift_step(&d, 100, 3600.0f); /* an hour asleep: one clamped frame, not 1e6 degrees */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 14.05f, d.off_lon);
}

static void test_ease_lon_shortest(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 171.6f, omni_ease_lon(170.0f, -170.0f, 1.0f / 60.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -171.6f, omni_ease_lon(-170.0f, 170.0f, 1.0f / 60.0f));
}

static void test_cadence(void)
{
    TEST_ASSERT_EQUAL_UINT32(33, omni_globe_period_ms(OMNI_CAD_LIVE, true, 0));
    TEST_ASSERT_EQUAL_UINT32(30000, omni_globe_period_ms(OMNI_CAD_LIVE, false, 0));
    TEST_ASSERT_EQUAL_UINT32(30000, omni_globe_period_ms(OMNI_CAD_ANALOG, false, 0));
    TEST_ASSERT_EQUAL_UINT32(33, omni_globe_period_ms(OMNI_CAD_SPIN, false, 19999));
    TEST_ASSERT_EQUAL_UINT32(66, omni_globe_period_ms(OMNI_CAD_SPIN, true, 20000));
    TEST_ASSERT_EQUAL_UINT32(33, omni_globe_period_ms(OMNI_CAD_SPIN, true, 0));
    TEST_ASSERT_EQUAL_UINT32(66, omni_globe_period_ms(OMNI_CAD_SPIN, false, 20000));
}

static void test_lat0_for_home(void)
{
    TEST_ASSERT_EQUAL_INT8(22, omni_globe_lat0_for(39.96));
    TEST_ASSERT_EQUAL_INT8(43, omni_globe_lat0_for(78.22));
    TEST_ASSERT_EQUAL_INT8(-19, omni_globe_lat0_for(-33.87));
    TEST_ASSERT_EQUAL_INT8(-30, omni_globe_lat0_for(-54.8));
    TEST_ASSERT_EQUAL_INT8(45, omni_globe_lat0_for(90.0));
}

/* D6/m3: the one wrap the faces share; [-180, 180) even where float rounding lands on 180 */
static void test_wrap180(void)
{
    TEST_ASSERT_EQUAL_FLOAT(-180.0f, omni_wrap180f(180.0f));
    TEST_ASSERT_EQUAL_FLOAT(-180.0f, omni_wrap180f(-180.0f));
    TEST_ASSERT_EQUAL_FLOAT(-170.0f, omni_wrap180f(190.0f));
    TEST_ASSERT_EQUAL_FLOAT(170.0f, omni_wrap180f(-190.0f));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, omni_wrap180f(370.0f));
    const float w = omni_wrap180f(-180.00001f);   /* -1e-5 + 360 rounds to 360 in float */
    TEST_ASSERT_TRUE(w >= -180.0f && w < 180.0f);
    TEST_ASSERT_TRUE(omni_wrap180(180.0) == -180.0);
    TEST_ASSERT_TRUE(omni_wrap180(190.0) == -170.0);
    TEST_ASSERT_TRUE(omni_wrap180(-190.0) == 170.0);
    const double d = omni_wrap180(-180.0 - 1e-14);
    TEST_ASSERT_TRUE(d >= -180.0 && d < 180.0);
}

/* Task 9 (D): the flywheel advanced per tick in 50 ms pieces, at most 0.25 s a tick (a stall must not jump it) */
static void test_spin_advance_pieces(void)
{
    omni_spin_t a = {.v = 520.0f}, b = a;
    omni_spin_advance(&a, 0.066f);
    omni_spin_step(&b, 0.05f);
    omni_spin_step(&b, 0.066f - 0.05f);
    TEST_ASSERT_EQUAL_FLOAT(b.lon, a.lon);
    TEST_ASSERT_EQUAL_FLOAT(b.v, a.v);
    omni_spin_t c = {.v = 520.0f}, d = c;
    omni_spin_advance(&c, 1.0f);
    omni_spin_advance(&d, 0.25f);
    TEST_ASSERT_EQUAL_FLOAT(d.lon, c.lon);
    TEST_ASSERT_EQUAL_FLOAT(d.v, c.v);
    omni_spin_t e = {.v = 520.0f};
    omni_spin_advance(&e, 0);
    omni_spin_advance(&e, -1);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e.lon);
}

/* 15 fps and 60 fps ticks keep the same pace: the coast-down from a wake and the idle turn */
static void test_spin_advance_rate_independent(void)
{
    omni_spin_t a = {.v = 520.0f}, b = a;
    double ta = 0, tb = 0;
    float turned_a = 0, turned_b = 0;
    for (int i = 0; i < 30; i++) {   /* 30 x 66 ms = 1.98 s */
        const float before = a.lon;
        omni_spin_advance(&a, 0.066f);
        turned_a += omni_wrap180f(before - a.lon);
        ta += 0.066;
    }
    for (int i = 0; i < 120; i++) {   /* 120 x 16.5 ms = 1.98 s */
        const float before = b.lon;
        omni_spin_advance(&b, 0.0165f);
        turned_b += omni_wrap180f(before - b.lon);
        tb += 0.0165;
    }
    TEST_ASSERT_TRUE(ta - tb < 1e-6 && tb - ta < 1e-6);
    /* omni_spin_step moves lon by the already-decayed v, so 50 ms pieces turn a little less in the 520 deg/s
     * coast than 16.5 ms ones: 278 vs 284 deg over 2 s (2.2 %), invisible in a spin-up. */
    TEST_ASSERT_FLOAT_WITHIN(0.03f * turned_b, turned_b, turned_a);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, b.v, a.v);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reveal_live_curve);
    RUN_TEST(test_reveal_analog_curve);
    RUN_TEST(test_reveal_spin_curve);
    RUN_TEST(test_spin_flywheel);
    RUN_TEST(test_spin_fling_clamped);
    RUN_TEST(test_spin_still_release_idles);
    RUN_TEST(test_spin_tap_shows_time);
    RUN_TEST(test_spin_lon_stays_bounded);
    RUN_TEST(test_drift_returns_home);
    RUN_TEST(test_signed_time_casts);
    RUN_TEST(test_dt_clamps);
    RUN_TEST(test_ease_lon_shortest);
    RUN_TEST(test_cadence);
    RUN_TEST(test_lat0_for_home);
    RUN_TEST(test_wrap180);
    RUN_TEST(test_spin_advance_pieces);
    RUN_TEST(test_spin_advance_rate_independent);
    return UNITY_END();
}
