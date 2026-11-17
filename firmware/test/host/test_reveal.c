#include "unity.h"
#include "omni/reveal.h"
#include "omni/globe_motion.h"
#include "omni/omni_model.h"
#include "omni/settings.h"
#include "omni/travel.h"
#include <math.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

/* D1: the expected table is written out here, not re-derived from the rule. Plan D2: the flip or iris only on a wake
 * from sleep or a BOOT double (with reveal true); the first wake since create (a swipe, the picker, boot) plays the
 * plain dial entry; reveal false (a re-lift within 10 s, a touch from the glance) is at rest; Instant is the plain
 * entry on a wake too. */
static void test_reveal_for(void)
{
    static const struct {
        omni_wake_cause_t cause;
        bool reveal;
        uint8_t style;
        omni_rv_t want;
    } T[18] = {
        { OMNI_WAKE_FIRST, false, OMNI_REVEAL_FLIP, OMNI_RV_NONE },
        { OMNI_WAKE_FIRST, false, OMNI_REVEAL_IRIS, OMNI_RV_NONE },
        { OMNI_WAKE_FIRST, false, OMNI_REVEAL_NONE, OMNI_RV_NONE },
        { OMNI_WAKE_FIRST, true, OMNI_REVEAL_FLIP, OMNI_RV_DIAL },
        { OMNI_WAKE_FIRST, true, OMNI_REVEAL_IRIS, OMNI_RV_DIAL },
        { OMNI_WAKE_FIRST, true, OMNI_REVEAL_NONE, OMNI_RV_DIAL },
        { OMNI_WAKE_AFTER_SLEEP, false, OMNI_REVEAL_FLIP, OMNI_RV_NONE },
        { OMNI_WAKE_AFTER_SLEEP, false, OMNI_REVEAL_IRIS, OMNI_RV_NONE },
        { OMNI_WAKE_AFTER_SLEEP, false, OMNI_REVEAL_NONE, OMNI_RV_NONE },
        { OMNI_WAKE_AFTER_SLEEP, true, OMNI_REVEAL_FLIP, OMNI_RV_FLIP },
        { OMNI_WAKE_AFTER_SLEEP, true, OMNI_REVEAL_IRIS, OMNI_RV_IRIS },
        { OMNI_WAKE_AFTER_SLEEP, true, OMNI_REVEAL_NONE, OMNI_RV_DIAL },
        { OMNI_WAKE_REPLAY, false, OMNI_REVEAL_FLIP, OMNI_RV_NONE },
        { OMNI_WAKE_REPLAY, false, OMNI_REVEAL_IRIS, OMNI_RV_NONE },
        { OMNI_WAKE_REPLAY, false, OMNI_REVEAL_NONE, OMNI_RV_NONE },
        { OMNI_WAKE_REPLAY, true, OMNI_REVEAL_FLIP, OMNI_RV_FLIP },
        { OMNI_WAKE_REPLAY, true, OMNI_REVEAL_IRIS, OMNI_RV_IRIS },
        { OMNI_WAKE_REPLAY, true, OMNI_REVEAL_NONE, OMNI_RV_DIAL },
    };
    for (int i = 0; i < 18; i++) {
        char msg[16];
        snprintf(msg, sizeof msg, "row %d", i);
        TEST_ASSERT_EQUAL_MESSAGE(T[i].want, omni_reveal_for(T[i].cause, T[i].reveal, T[i].style), msg);
    }
}

/* m3: a style out of range (a corrupt or future setting) is the default, FLIP; reveal and the cause rule still apply */
static void test_reveal_for_out_of_range(void)
{
    static const uint8_t BAD[] = { 3, 7, 255 };
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(OMNI_RV_FLIP, omni_reveal_for(OMNI_WAKE_AFTER_SLEEP, true, BAD[i]));
        TEST_ASSERT_EQUAL(OMNI_RV_FLIP, omni_reveal_for(OMNI_WAKE_REPLAY, true, BAD[i]));
        TEST_ASSERT_EQUAL(OMNI_RV_DIAL, omni_reveal_for(OMNI_WAKE_FIRST, true, BAD[i]));
        TEST_ASSERT_EQUAL(OMNI_RV_NONE, omni_reveal_for(OMNI_WAKE_AFTER_SLEEP, false, BAD[i]));
    }
}

static void test_wake_track(void)
{
    omni_wake_track_t w = { 0 };   /* zeroed at create */
    TEST_ASSERT_EQUAL(OMNI_WAKE_FIRST, omni_wake_track_wake(&w));
    TEST_ASSERT_EQUAL(OMNI_WAKE_REPLAY, omni_wake_track_wake(&w));
    omni_wake_track_sleep(&w);
    TEST_ASSERT_EQUAL(OMNI_WAKE_AFTER_SLEEP, omni_wake_track_wake(&w));
    TEST_ASSERT_EQUAL(OMNI_WAKE_REPLAY, omni_wake_track_wake(&w));
    omni_wake_track_sleep(&w);
    omni_wake_track_sleep(&w);   /* a sleep twice over (a screen, then the glance) is still one */
    TEST_ASSERT_EQUAL(OMNI_WAKE_AFTER_SLEEP, omni_wake_track_wake(&w));
    /* a sleep before the first wake (created under a screen, the glance) leaves it the first */
    omni_wake_track_t f = { 0 };
    omni_wake_track_sleep(&f);
    TEST_ASSERT_EQUAL(OMNI_WAKE_FIRST, omni_wake_track_wake(&f));
    TEST_ASSERT_EQUAL(OMNI_WAKE_REPLAY, omni_wake_track_wake(&f));
}

static const struct tm DAY = { .tm_year = 2026 - 1900, .tm_mon = 9, .tm_mday = 26, .tm_hour = 10, .tm_min = 8 };

/* T5 m7: after a skip at ts, with lon_ts shown then and `base` the globe's home less the drift at ts: the remaining
 * offset (the short way) eases out by outCubic over 500 ms while the drift runs on from ts */
static float skip_lon(uint32_t t, uint32_t ts, float lon_ts, double base)
{
    const double off = omni_wrap180((double)lon_ts - base), k = (t - ts) >= 500 ? 1 : (double)(t - ts) / 500;
    const double u = 1 - k;
    return (float)omni_wrap180(base + off * (u * u * u) - 2.2 * (double)(t - ts) / 1000);
}

/* a skip at 300 ms into a flip: lid gone, green 1 and no spin-up on the next step */
static void test_reveal_skip(void)
{
    omni_model_t m;
    omni_dial_state_t s;
    omni_model_init(&m, 40, -83);
    omni_model_wake(&m, 1000, &OMNI_ENTRY_FLIP, false);
    omni_model_step(&m, 1300, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_TRUE(s.lid);
    TEST_ASSERT_TRUE(omni_model_revealing(&m, 1300));
    TEST_ASSERT_FALSE(omni_model_reveal_done(&m, 1300));
    omni_model_skip(&m, 1300);
    TEST_ASSERT_TRUE(omni_model_reveal_done(&m, 1300));
    TEST_ASSERT_FALSE(omni_model_revealing(&m, 1300));
    const float lon_skip = s.lon_deg;
    omni_model_step(&m, 1332, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FALSE(s.lid);
    TEST_ASSERT_FALSE(s.iris);
    TEST_ASSERT_EQUAL_FLOAT(1, s.green);
    TEST_ASSERT_EQUAL_FLOAT(0, s.fade);
    /* T5 m7: the spin-up's offset (990 deg: -90 the short way) eases out over 500 ms, from the skip */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, skip_lon(1332, 1300, lon_skip, -83), s.lon_deg);
    omni_model_step(&m, 1800, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)(-83 - 2.2 * 0.5), s.lon_deg);   /* home, less the drift from the skip */
    /* the same at 300 ms into the iris */
    omni_model_wake(&m, 5000, &OMNI_ENTRY_IRIS, false);
    omni_model_step(&m, 5300, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_TRUE(s.iris);
    omni_model_skip(&m, 5300);
    omni_model_step(&m, 5332, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FALSE(s.iris);
    TEST_ASSERT_EQUAL_FLOAT(1, s.green);
    TEST_ASSERT_EQUAL_FLOAT(0, s.fade);
    omni_model_step(&m, 5800, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)(-83 - 2.2 * 0.5), s.lon_deg);   /* the spin had not started: drift from the skip */
}

/* m2: a skip mid-spin (the usual tap, 1-3 s in): the spin offset eases out over 500 ms (T5 m7: no jump); then the globe
 * is home less the drift since the spin's start, drifting only; the iris's lon at a time its spin has started. */
static void test_reveal_skip_mid_spin(void)
{
    omni_model_t m;
    omni_dial_state_t s;
    omni_model_init(&m, 40, -83);
    omni_model_wake(&m, 1000, &OMNI_ENTRY_FLIP, false);
    omni_model_step(&m, 3000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_TRUE(fabsf(s.lon_deg - (float)(-83 - 2.2 * 1.67)) > 1);   /* still spinning: well off home */
    omni_model_skip(&m, 3000);
    const float l3000 = s.lon_deg;
    omni_model_step(&m, 3032, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, skip_lon(3032, 3000, l3000, -83 - 2.2 * 1.67), s.lon_deg);
    TEST_ASSERT_TRUE(s.lon_speed < 35);   /* at most 3 x 180 deg / 500 ms over the step: motion, not a jump */
    TEST_ASSERT_EQUAL_FLOAT(1, s.green);
    omni_model_step(&m, 3500, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)(-83 - 2.2 * (3500 - 1330) / 1000.0), s.lon_deg);
    omni_model_step(&m, 3532, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)(-83 - 2.2 * (3532 - 1330) / 1000.0), s.lon_deg);
    TEST_ASSERT_TRUE(s.lon_speed < 0.1f);   /* drift only: 0.07 deg a step */
    /* the iris, 1 s in (its spin started at 620) */
    omni_model_wake(&m, 5000, &OMNI_ENTRY_IRIS, false);
    omni_model_step(&m, 6000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_TRUE(fabsf(s.lon_deg + 83) > 1);
    omni_model_skip(&m, 6000);
    omni_model_step(&m, 6032, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FALSE(s.iris);
    omni_model_step(&m, 6500, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)(-83 - 2.2 * (6500 - 5620) / 1000.0), s.lon_deg);
}

/* m2: a skip of the plain dial entry 500 ms in, mid-settle: the rings at today's values at once, the globe home 500 ms
 * on (T5 m7) */
static void test_reveal_skip_dial(void)
{
    omni_model_t m;
    omni_dial_state_t s;
    float today[OMNI_RINGS];
    omni_dial_vals(2026, 10, 26, today);
    omni_model_init(&m, 40, -83);
    omni_model_wake(&m, 1000, &OMNI_ENTRY_DIAL, false);
    omni_model_step(&m, 1500, true, &DAY, OMNI_LENS_OFF, &s);
    int off = 0;
    for (int i = 0; i < OMNI_RINGS; i++) off += fabsf(s.val[i] - today[i]) > 1e-3f;
    TEST_ASSERT_TRUE(off > 0);   /* settling */
    omni_model_skip(&m, 1500);
    omni_model_step(&m, 1532, true, &DAY, OMNI_LENS_OFF, &s);
    for (int i = 0; i < OMNI_RINGS; i++) TEST_ASSERT_EQUAL_FLOAT(today[i], s.val[i]);
    omni_model_step(&m, 2000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)(-83 - 2.2 * (2000 - 1150) / 1000.0), s.lon_deg);
    TEST_ASSERT_EQUAL_FLOAT(1, s.green);
}

/* Final review M1: a wake without a reveal (a screen closed over the dial, a re-lift or a touch from the glance) goes
 * on from the longitude last shown, drifting, with no jump back to home; the face's sleep (omni_travel_abort_home) in
 * between does not change that. A first wake with nothing shown yet, and a sleep that aborted a travel, are at home. */
static void test_wake_at_rest_continues(void)
{
    omni_model_t m;
    omni_dial_state_t s;
    omni_model_init(&m, 40, -83);
    omni_model_wake(&m, 1000, NULL, false);   /* nothing shown before: home, drifting */
    omni_model_step(&m, 1000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -83, s.lon_deg);
    omni_model_step(&m, 9000, true, &DAY, OMNI_LENS_OFF, &s);
    const float shown = s.lon_deg;
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)(-83 - 2.2 * 8), shown);
    omni_travel_abort_home(&m);   /* the face's sleep as the screen opens */
    omni_model_wake(&m, 12000, NULL, false);   /* the screen closes 3 s later */
    omni_model_step(&m, 12000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, shown, s.lon_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0, s.lon_speed);
    omni_model_step(&m, 13000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)(shown - 2.2), s.lon_deg);
    /* mid-spin-up (a screen opened 1.5 s into the flip's spin): on from there, the spin's rest not replayed */
    omni_model_wake(&m, 20000, &OMNI_ENTRY_FLIP, false);
    for (uint32_t t = 20000; t <= 22500; t += 32) omni_model_step(&m, t, true, &DAY, OMNI_LENS_OFF, &s);
    const float mid = s.lon_deg;
    omni_travel_abort_home(&m);
    omni_model_wake(&m, 24000, NULL, false);
    omni_model_step(&m, 24000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, mid, s.lon_deg);
    TEST_ASSERT_FALSE(s.lid);
    omni_model_step(&m, 25000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)omni_wrap180((double)mid - 2.2), s.lon_deg);
    /* reduced motion: no drift, the globe stays where it was */
    omni_model_wake(&m, 30000, NULL, true);
    omni_model_step(&m, 30000, true, &DAY, OMNI_LENS_OFF, &s);
    const float still = s.lon_deg;
    omni_model_wake(&m, 34000, NULL, true);
    omni_model_step(&m, 36000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, still, s.lon_deg);
    /* re-review N2: a new home city (settings) re-centres the globe on the next wake, even without a reveal */
    omni_model_wake(&m, 36500, NULL, false);
    omni_model_step(&m, 36500, true, &DAY, OMNI_LENS_OFF, &s);
    omni_model_set_home(&m, 51.5, -0.13);
    omni_model_wake(&m, 37000, NULL, false);
    omni_model_step(&m, 37000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.13f, s.lon_deg);
    omni_model_set_home(&m, 40, -83);
    /* a wake with a reveal starts from home again: as a new model's */
    omni_model_t fresh;
    omni_dial_state_t fs;
    omni_model_init(&fresh, 40, -83);
    omni_model_wake(&fresh, 40000, &OMNI_ENTRY_DIAL, false);
    omni_model_step(&fresh, 40000, true, &DAY, OMNI_LENS_OFF, &fs);
    omni_model_wake(&m, 40000, &OMNI_ENTRY_DIAL, false);
    omni_model_step(&m, 40000, true, &DAY, OMNI_LENS_OFF, &s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, fs.lon_deg, s.lon_deg);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reveal_for);
    RUN_TEST(test_wake_track);
    RUN_TEST(test_reveal_skip);
    RUN_TEST(test_reveal_for_out_of_range);
    RUN_TEST(test_reveal_skip_mid_spin);
    RUN_TEST(test_reveal_skip_dial);
    RUN_TEST(test_wake_at_rest_continues);
    return UNITY_END();
}
