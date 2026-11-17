#include "unity.h"
#include <string.h>
#include "omni/clockcal.h"

#define T0 1792780000LL
#define S 1000000ULL /* us per s */

void setUp(void) {}
void tearDown(void) {}

/* A first sync this boot, at uptime 100 s with 0 s asleep. */
static void boot_sync(omni_clockcal_t *c)
{
    omni_clk_init(c);
    omni_clk_on_sync(c, 5000000, T0, 100 * S, 0);
}

static void test_defaults(void)
{
    omni_clockcal_t c;
    omni_clk_init(&c);
    TEST_ASSERT_EQUAL_UINT32(OMNI_CLK_INTERVAL_MIN_S, omni_clk_interval_s(&c));
    TEST_ASSERT_EQUAL_INT32(0, c.saved.rate_ppm);
    TEST_ASSERT_EQUAL_INT64(0, omni_clk_due_us(&c, 1000 * S)); /* no sync yet */
}

static void test_first_sync_this_boot_logs_and_learns_nothing(void)
{
    omni_clockcal_t c;
    boot_sync(&c);
    omni_clk_entry_t e[OMNI_CLK_LOG_N];
    TEST_ASSERT_EQUAL_INT(1, omni_clk_log(&c, e, OMNI_CLK_LOG_N));
    TEST_ASSERT_EQUAL_INT32(5000, e[0].step_ms);
    TEST_ASSERT_EQUAL_UINT32(0, e[0].since_s);
    TEST_ASSERT_EQUAL_UINT32(100, e[0].uptime_s);
    TEST_ASSERT_EQUAL_INT64(T0, e[0].unix_s);
    TEST_ASSERT_EQUAL_INT32(0, c.saved.rate_ppm);
    TEST_ASSERT_EQUAL_UINT32(OMNI_CLK_INTERVAL_MIN_S, omni_clk_interval_s(&c)); /* a boot step says nothing */
}

/* 3000 s asleep, the watch 6 s behind: 2000 ppm, applied between syncs. */
static void test_rate_from_asleep_seconds_and_applied_between_syncs(void)
{
    omni_clockcal_t c;
    boot_sync(&c);
    omni_clk_on_sync(&c, 6000000, T0 + 3600, 3700 * S, 3000 * S);
    TEST_ASSERT_EQUAL_INT32(2000, c.saved.rate_ppm);
    TEST_ASSERT_EQUAL_UINT32(OMNI_CLK_INTERVAL_MIN_S, omni_clk_interval_s(&c)); /* 6 s is a large residual */
    TEST_ASSERT_EQUAL_INT64(0, omni_clk_due_us(&c, 3000 * S + 40 * S));       /* 80 ms: under the minimum */
    int64_t due = omni_clk_due_us(&c, 3000 * S + 1000 * S);
    TEST_ASSERT_EQUAL_INT64(2000000, due);
    omni_clk_applied(&c, due);
    TEST_ASSERT_EQUAL_INT64(0, omni_clk_due_us(&c, 3000 * S + 1000 * S));
    TEST_ASSERT_EQUAL_INT64(1000000, omni_clk_due_us(&c, 3000 * S + 1500 * S));
    /* Next sync: 1000 s more asleep, 2 s applied, the residual +0.1 s: drift 2.1 s / 1000 s = 2100 ppm. */
    omni_clk_on_sync(&c, 100000, T0 + 4600, 4700 * S, 4000 * S);
    TEST_ASSERT_EQUAL_INT32(2050, c.saved.rate_ppm); /* folded: halfway to the new estimate */
    TEST_ASSERT_EQUAL_UINT32(2 * OMNI_CLK_INTERVAL_MIN_S, omni_clk_interval_s(&c));
    omni_clk_entry_t e[OMNI_CLK_LOG_N];
    int n = omni_clk_log(&c, e, OMNI_CLK_LOG_N);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_INT32(2000, e[2].applied_ms);
    TEST_ASSERT_EQUAL_UINT32(1000, e[2].since_s);
    TEST_ASSERT_EQUAL_UINT32(1000, e[2].asleep_s);
    TEST_ASSERT_EQUAL_INT32(2050, e[2].rate_ppm);
}

static void test_negative_rate(void)
{
    omni_clockcal_t c;
    boot_sync(&c);
    omni_clk_on_sync(&c, -3000000, T0 + 2000, 2100 * S, 1500 * S); /* 3 s ahead after 1500 s asleep */
    TEST_ASSERT_EQUAL_INT32(-2000, c.saved.rate_ppm);
    TEST_ASSERT_EQUAL_INT64(-1000000, omni_clk_due_us(&c, 2000 * S));
}

static void test_interval_doubles_to_the_cap_then_tightens(void)
{
    omni_clockcal_t c;
    boot_sync(&c);
    const uint32_t want[] = {1800, 3600, 7200, 14400, 21600, 21600};
    uint64_t up = 100 * S;
    for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++) {
        up += (uint64_t)omni_clk_interval_s(&c) * S;
        omni_clk_on_sync(&c, 200000, T0 + (int64_t)(up / S), up, 0); /* on USB: no sleep, small residual */
        TEST_ASSERT_EQUAL_UINT32(want[i], omni_clk_interval_s(&c));
    }
    up += 21600 * S;
    omni_clk_on_sync(&c, 2000000, T0 + (int64_t)(up / S), up, 0); /* medium: kept */
    TEST_ASSERT_EQUAL_UINT32(21600, omni_clk_interval_s(&c));
    up += 21600 * S;
    omni_clk_on_sync(&c, -4500000, T0 + (int64_t)(up / S), up, 0); /* large: dense again */
    TEST_ASSERT_EQUAL_UINT32(OMNI_CLK_INTERVAL_MIN_S, omni_clk_interval_s(&c));
    TEST_ASSERT_EQUAL_INT32(0, c.saved.rate_ppm); /* never enough sleep to estimate */
}

static void test_outlier_rate_ignored(void)
{
    omni_clockcal_t c;
    boot_sync(&c);
    omni_clk_on_sync(&c, 3600000000LL, T0 + 7200, 7300 * S, 3600 * S); /* an hour off: a settime, not drift */
    TEST_ASSERT_EQUAL_INT32(0, c.saved.rate_ppm);
    TEST_ASSERT_EQUAL_UINT8(0, c.saved.n_est);
}

static void test_log_keeps_the_last_entries_oldest_first(void)
{
    omni_clockcal_t c;
    omni_clk_init(&c);
    for (int i = 0; i < OMNI_CLK_LOG_N + 3; i++)
        omni_clk_on_sync(&c, i * 1000, T0 + i, (uint64_t)(100 + i) * S, 0);
    omni_clk_entry_t e[OMNI_CLK_LOG_N];
    TEST_ASSERT_EQUAL_INT(OMNI_CLK_LOG_N, omni_clk_log(&c, e, OMNI_CLK_LOG_N));
    TEST_ASSERT_EQUAL_INT32(3, e[0].step_ms);
    TEST_ASSERT_EQUAL_INT32(OMNI_CLK_LOG_N + 2, e[OMNI_CLK_LOG_N - 1].step_ms);
    TEST_ASSERT_EQUAL_INT(2, omni_clk_log(&c, e, 2));
    TEST_ASSERT_EQUAL_INT32(3, e[0].step_ms);
}

/* The rate and interval survive a reboot; the reference does not (the first sync after it learns nothing). */
static void test_restore(void)
{
    omni_clockcal_t a, b;
    boot_sync(&a);
    omni_clk_on_sync(&a, 6000000, T0 + 3600, 3700 * S, 3000 * S);
    omni_clk_saved_t saved = a.saved;
    TEST_ASSERT_TRUE(omni_clk_restore(&b, &saved));
    TEST_ASSERT_EQUAL_INT32(2000, b.saved.rate_ppm);
    TEST_ASSERT_EQUAL_INT64(0, omni_clk_due_us(&b, 5000 * S));
    omni_clk_on_sync(&b, 50000000, T0 + 9000, 50 * S, 0);
    TEST_ASSERT_EQUAL_INT32(2000, b.saved.rate_ppm);
    omni_clk_entry_t e[OMNI_CLK_LOG_N];
    TEST_ASSERT_EQUAL_INT(3, omni_clk_log(&b, e, OMNI_CLK_LOG_N));

    saved.interval_s = 5; /* out of range */
    TEST_ASSERT_FALSE(omni_clk_restore(&b, &saved));
    TEST_ASSERT_EQUAL_UINT32(OMNI_CLK_INTERVAL_MIN_S, omni_clk_interval_s(&b));
    TEST_ASSERT_EQUAL_INT32(0, b.saved.rate_ppm);
    memset(&saved, 0xff, sizeof saved);
    TEST_ASSERT_FALSE(omni_clk_restore(&b, &saved));
    TEST_ASSERT_EQUAL_INT(0, omni_clk_log(&b, e, OMNI_CLK_LOG_N));
}

/* Learning happens in light sleep (on battery): an unplug before the rate is learned makes the syncs dense again. */
static void test_unplug_before_learning_goes_dense(void)
{
    omni_clockcal_t c;
    boot_sync(&c);
    c.saved.interval_s = OMNI_CLK_INTERVAL_MAX_S; /* backed off on USB */
    omni_clk_on_battery(&c);
    TEST_ASSERT_EQUAL_UINT32(OMNI_CLK_INTERVAL_MIN_S, omni_clk_interval_s(&c));
    c.saved.n_est = 2; /* learned */
    c.saved.interval_s = 7200;
    omni_clk_on_battery(&c);
    TEST_ASSERT_EQUAL_UINT32(7200, omni_clk_interval_s(&c));
}

/* A manual settime between syncs: the app invalidates the reference, so SNTP undoing it teaches nothing. */
static void test_settime_invalidates_the_reference(void)
{
    omni_clockcal_t c;
    boot_sync(&c);
    omni_clk_invalidate_ref(&c);
    TEST_ASSERT_EQUAL_INT64(0, omni_clk_due_us(&c, 20000 * S));
    /* 60 s off after 6 h asleep (2778 ppm: inside the cap) */
    omni_clk_on_sync(&c, 60000000, T0 + 21700, 21800 * S, 21600 * S);
    TEST_ASSERT_EQUAL_INT32(0, c.saved.rate_ppm);
    TEST_ASSERT_EQUAL_UINT8(0, c.saved.n_est);
    omni_clk_entry_t e[OMNI_CLK_LOG_N];
    TEST_ASSERT_EQUAL_INT(2, omni_clk_log(&c, e, OMNI_CLK_LOG_N));
    TEST_ASSERT_EQUAL_UINT32(0, e[1].since_s); /* logged as a fresh reference */
    /* the following interval learns normally */
    omni_clk_on_sync(&c, 2000000, T0 + 23000, 23100 * S, 22600 * S);
    TEST_ASSERT_EQUAL_INT32(2000, c.saved.rate_ppm);
}

static void learned(omni_clockcal_t *c, int32_t rate)
{
    boot_sync(c);
    c->saved.rate_ppm = rate;
    c->saved.n_est = OMNI_CLK_LEARNED;
}

/* Once learned, a non-drift step inside the cap but far from the rate is not folded in. */
static void test_learned_rejects_a_far_estimate(void)
{
    omni_clockcal_t c;
    learned(&c, 1000);
    omni_clk_on_sync(&c, 9000000, T0 + 1100, 1200 * S, 1000 * S); /* 9000 ppm */
    TEST_ASSERT_EQUAL_INT32(1000, c.saved.rate_ppm);
    TEST_ASSERT_EQUAL_UINT8(OMNI_CLK_LEARNED, c.saved.n_est);
    /* a close one still folds */
    omni_clk_on_sync(&c, 3000000, T0 + 2100, 2200 * S, 2000 * S); /* 3000 ppm */
    TEST_ASSERT_EQUAL_INT32(2000, c.saved.rate_ppm);
}

/* A real change of rate: two consecutive far estimates that agree replace the rate. */
static void test_learned_relearns_after_two_agreeing_far_estimates(void)
{
    omni_clockcal_t c;
    learned(&c, 1000);
    omni_clk_on_sync(&c, 8000000, T0 + 1100, 1200 * S, 1000 * S);  /* 8000 ppm: held */
    TEST_ASSERT_EQUAL_INT32(1000, c.saved.rate_ppm);
    omni_clk_on_sync(&c, 8400000, T0 + 2100, 2200 * S, 2000 * S);  /* 8400 ppm: agrees */
    TEST_ASSERT_EQUAL_INT32(8400, c.saved.rate_ppm);
    TEST_ASSERT_EQUAL_UINT8(1, c.saved.n_est);
}

static void test_asleep_gate_boundary(void)
{
    omni_clockcal_t c;
    boot_sync(&c);
    omni_clk_on_sync(&c, 1200000, T0 + 700, 800 * S, OMNI_CLK_MIN_ASLEEP_S * S - 1);
    TEST_ASSERT_EQUAL_UINT8(0, c.saved.n_est);
    boot_sync(&c);
    omni_clk_on_sync(&c, 1200000, T0 + 700, 800 * S, OMNI_CLK_MIN_ASLEEP_S * S);
    TEST_ASSERT_EQUAL_UINT8(1, c.saved.n_est);
    TEST_ASSERT_EQUAL_INT32(2000, c.saved.rate_ppm);
}

/* An outlier does not count as an estimate: the next good one is taken as it is. */
static void test_outlier_then_good_estimate(void)
{
    omni_clockcal_t c;
    boot_sync(&c);
    omni_clk_on_sync(&c, 100000000, T0 + 1100, 1200 * S, 1000 * S); /* 100000 ppm */
    omni_clk_on_sync(&c, 3000000, T0 + 2100, 2200 * S, 2000 * S);
    TEST_ASSERT_EQUAL_INT32(3000, c.saved.rate_ppm);
    TEST_ASSERT_EQUAL_UINT8(1, c.saved.n_est);
}

static void test_overflow_sized_steps(void)
{
    omni_clockcal_t c;
    boot_sync(&c);
    omni_clk_on_sync(&c, INT64_MAX / 2, T0 + 1100, 1200 * S, 1000 * S);
    omni_clk_on_sync(&c, INT64_MIN / 2, T0 + 2100, 2200 * S, 2000 * S);
    TEST_ASSERT_EQUAL_INT32(0, c.saved.rate_ppm);
    TEST_ASSERT_EQUAL_UINT8(0, c.saved.n_est);
    omni_clk_entry_t e[OMNI_CLK_LOG_N];
    TEST_ASSERT_EQUAL_INT(3, omni_clk_log(&c, e, OMNI_CLK_LOG_N));
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, e[1].step_ms);
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, e[2].step_ms);
}

/* Unplug, then a sync at once (no sleep yet): no estimate, so the battery schedule stays at 15 min. On USB a
 * small residual still backs off (nothing to learn there: the crystal keeps the time). */
static void test_unplug_then_immediate_sync_stays_dense(void)
{
    omni_clockcal_t c;
    boot_sync(&c);
    omni_clk_on_battery(&c);
    omni_clk_on_sync(&c, 50000, T0 + 30, 130 * S, 0);
    TEST_ASSERT_EQUAL_UINT32(OMNI_CLK_INTERVAL_MIN_S, omni_clk_interval_s(&c));
    c.saved.interval_s = 3600; /* a large residual without an estimate still tightens */
    omni_clk_on_sync(&c, 5000000, T0 + 60, 160 * S, 0);
    TEST_ASSERT_EQUAL_UINT32(OMNI_CLK_INTERVAL_MIN_S, omni_clk_interval_s(&c));
    omni_clk_on_sync(&c, 1200000, T0 + 1000, 1030 * S, 600 * S); /* an estimate, but 1.2 s: kept */
    TEST_ASSERT_EQUAL_UINT32(OMNI_CLK_INTERVAL_MIN_S, omni_clk_interval_s(&c));
    omni_clk_on_sync(&c, 100000, T0 + 2000, 2030 * S, 1500 * S); /* an estimate and small: doubles */
    TEST_ASSERT_EQUAL_UINT32(2 * OMNI_CLK_INTERVAL_MIN_S, omni_clk_interval_s(&c));
    omni_clk_on_usb(&c);
    omni_clk_on_sync(&c, 10000, T0 + 4000, 4030 * S, 1500 * S);
    TEST_ASSERT_EQUAL_UINT32(4 * OMNI_CLK_INTERVAL_MIN_S, omni_clk_interval_s(&c));
}

/* A correction never sets the clock before the start of a minute already shown (held, applied later). */
static void test_hold_back_at_the_shown_minute(void)
{
    const int64_t m = 60 * 1000000LL, now = 1000 * m + 2 * 1000000LL; /* 2 s into minute 1000 */
    TEST_ASSERT_EQUAL_INT64(-1500000, omni_clk_hold_back(now, -1500000, 1000 * m));
    TEST_ASSERT_EQUAL_INT64(-2000000, omni_clk_hold_back(now, -5000000, 1000 * m));
    TEST_ASSERT_EQUAL_INT64(3000000, omni_clk_hold_back(now, 3000000, 1000 * m));
    TEST_ASSERT_EQUAL_INT64(-5000000, omni_clk_hold_back(now, -5000000, 999 * m)); /* screen was off since 999 */
    TEST_ASSERT_EQUAL_INT64(0, omni_clk_hold_back(now, -5000000, now + 1000000)); /* never forwards by itself */
}

/* m14: clock_correct's decision. A learned rate of 2000 ppm, 1000 s asleep since the sync: 2 s due. */
static void test_correction_now(void)
{
    omni_clockcal_t c;
    boot_sync(&c);
    omni_clk_on_sync(&c, 6000000, T0 + 3600, 3700 * S, 3000 * S); /* rate 2000 ppm, reference at 3000 s asleep */
    const int64_t now = (int64_t)(T0 + 5000) * 1000000;
    const int64_t floor = now - now % 60000000;
    TEST_ASSERT_EQUAL_INT64(2000000, omni_clk_correction(&c, false, now, 4000 * S, floor));
    /* a sync in flight measures its step as the change of the clock: no correction then */
    TEST_ASSERT_EQUAL_INT64(0, omni_clk_correction(&c, true, now, 4000 * S, floor));
    /* under the minimum step */
    TEST_ASSERT_EQUAL_INT64(0, omni_clk_correction(&c, false, now, 3000 * S + 40 * S, floor));
    /* a backward correction stops at the start of the minute last shown */
    omni_clockcal_t n;
    boot_sync(&n);
    omni_clk_on_sync(&n, -6000000, T0 + 3600, 3700 * S, 3000 * S); /* rate -2000 ppm */
    const int64_t at = floor + 1500000;                             /* 1.5 s into the shown minute */
    TEST_ASSERT_EQUAL_INT64(-1500000, omni_clk_correction(&n, false, at, 4000 * S, floor));
    TEST_ASSERT_EQUAL_INT64(-2000000, omni_clk_correction(&n, false, at, 4000 * S, floor - 60000000));
}

/* m10: SNTP steps add up until the event loop takes them, so a dropped EV_NET does not lose one. */
static void test_steps_accumulate_until_taken(void)
{
    omni_clk_steps_t st = {0};
    int64_t v = 123;
    TEST_ASSERT_FALSE(omni_clk_steps_take(&st, &v));
    TEST_ASSERT_EQUAL_INT64(0, v);
    omni_clk_steps_add(&st, 250000);
    omni_clk_steps_add(&st, -50000); /* the second sync's EV_NET carries both */
    TEST_ASSERT_TRUE(omni_clk_steps_take(&st, &v));
    TEST_ASSERT_EQUAL_INT64(200000, v);
    TEST_ASSERT_FALSE(omni_clk_steps_take(&st, &v)); /* taken once */
    TEST_ASSERT_EQUAL_INT64(0, v);
    omni_clk_steps_add(&st, INT64_MAX - 5);
    omni_clk_steps_add(&st, 100); /* saturates instead of wrapping */
    TEST_ASSERT_TRUE(omni_clk_steps_take(&st, &v));
    TEST_ASSERT_EQUAL_INT64(INT64_MAX, v);
    omni_clk_steps_add(&st, INT64_MIN + 5);
    omni_clk_steps_add(&st, -100);
    TEST_ASSERT_TRUE(omni_clk_steps_take(&st, &v));
    TEST_ASSERT_EQUAL_INT64(INT64_MIN, v);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_first_sync_this_boot_logs_and_learns_nothing);
    RUN_TEST(test_rate_from_asleep_seconds_and_applied_between_syncs);
    RUN_TEST(test_negative_rate);
    RUN_TEST(test_interval_doubles_to_the_cap_then_tightens);
    RUN_TEST(test_outlier_rate_ignored);
    RUN_TEST(test_log_keeps_the_last_entries_oldest_first);
    RUN_TEST(test_restore);
    RUN_TEST(test_unplug_before_learning_goes_dense);
    RUN_TEST(test_settime_invalidates_the_reference);
    RUN_TEST(test_learned_rejects_a_far_estimate);
    RUN_TEST(test_learned_relearns_after_two_agreeing_far_estimates);
    RUN_TEST(test_asleep_gate_boundary);
    RUN_TEST(test_outlier_then_good_estimate);
    RUN_TEST(test_overflow_sized_steps);
    RUN_TEST(test_unplug_then_immediate_sync_stays_dense);
    RUN_TEST(test_hold_back_at_the_shown_minute);
    RUN_TEST(test_correction_now);
    RUN_TEST(test_steps_accumulate_until_taken);
    return UNITY_END();
}
