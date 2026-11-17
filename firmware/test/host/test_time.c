#include "unity.h"
#include "omni/timefmt.h"
#include "omni/sync.h"
#include <stdio.h>
#include <string.h>

#define NY_TZ "EST5EDT,M3.2.0,M11.1.0"

#define T_2026_10_22_00Z 1792627200LL
#define T_NY_1458        1792695480LL /* 2026-10-22 18:58 UTC */
#define T_NY_1842        1792708920LL /* 2026-10-22 22:42 UTC */
#define T_SPRING_0659Z   1772953140LL /* 2026-03-08 06:59 UTC */
#define T_SPRING_0700Z   1772953200LL

void setUp(void) { omni_tz_apply(NY_TZ); }
void tearDown(void) {}

static void test_time_unset_before_2025(void)
{
    TEST_ASSERT_FALSE(omni_time_is_set(0));
    TEST_ASSERT_TRUE(omni_time_is_set(T_2026_10_22_00Z));
}

static void test_fmt_clock_12_24(void)
{
    char hhmm[6], ampm[3];
    omni_fmt_clock(T_NY_1458, false, hhmm, ampm);
    TEST_ASSERT_EQUAL_STRING("2:58", hhmm);
    TEST_ASSERT_EQUAL_STRING("PM", ampm);
    omni_fmt_clock(T_NY_1458, true, hhmm, ampm);
    TEST_ASSERT_EQUAL_STRING("14:58", hhmm);
    TEST_ASSERT_EQUAL_STRING("", ampm);
}

static void test_fmt_clock_zero_padding(void)
{
    char hhmm[6], ampm[3];
    /* 09:05 local EDT = 13:05 UTC */
    omni_fmt_clock(T_2026_10_22_00Z + 13 * 3600 + 5 * 60, true, hhmm, ampm);
    TEST_ASSERT_EQUAL_STRING("09:05", hhmm);
    omni_fmt_clock(T_2026_10_22_00Z + 13 * 3600 + 5 * 60, false, hhmm, ampm);
    TEST_ASSERT_EQUAL_STRING("9:05", hhmm);
    TEST_ASSERT_EQUAL_STRING("AM", ampm);
}

static void test_fmt_short_time(void)
{
    char out[8];
    omni_fmt_short_time(T_NY_1842, false, out);
    TEST_ASSERT_EQUAL_STRING("6:42p", out);
    omni_fmt_short_time(T_NY_1842, true, out);
    TEST_ASSERT_EQUAL_STRING("18:42", out);
    /* 00:05 EDT = 04:05 UTC */
    omni_fmt_short_time(T_2026_10_22_00Z + 4 * 3600 + 5 * 60, false, out);
    TEST_ASSERT_EQUAL_STRING("12:05a", out);
    omni_fmt_short_time(T_2026_10_22_00Z + 4 * 3600 + 5 * 60, true, out);
    TEST_ASSERT_EQUAL_STRING("00:05", out);
}

static void test_tz_dst_spring_forward(void)
{
    char hhmm[6], ampm[3];
    omni_fmt_clock(T_SPRING_0659Z, false, hhmm, ampm);
    TEST_ASSERT_EQUAL_STRING("1:59", hhmm);
    TEST_ASSERT_EQUAL_STRING("AM", ampm);
    omni_fmt_clock(T_SPRING_0700Z, false, hhmm, ampm);
    TEST_ASSERT_EQUAL_STRING("3:00", hhmm);
    TEST_ASSERT_EQUAL_STRING("AM", ampm);
}

static void test_tz_far_zone(void)
{
    char hhmm[6], ampm[3];
    omni_tz_apply("IST-5:30");
    omni_fmt_clock(T_2026_10_22_00Z, false, hhmm, ampm);
    TEST_ASSERT_EQUAL_STRING("5:30", hhmm);
    TEST_ASSERT_EQUAL_STRING("AM", ampm);
}

static void test_fmt_age(void)
{
    char out[8];
    omni_fmt_age(59, out);
    TEST_ASSERT_EQUAL_STRING("now", out);
    omni_fmt_age(60, out);
    TEST_ASSERT_EQUAL_STRING("1m", out);
    omni_fmt_age(840, out);
    TEST_ASSERT_EQUAL_STRING("14m", out);
    omni_fmt_age(3 * 3600, out);
    TEST_ASSERT_EQUAL_STRING("3h", out);
    omni_fmt_age(2 * 86400, out);
    TEST_ASSERT_EQUAL_STRING("2d", out);
    omni_fmt_age(-5, out);
    TEST_ASSERT_EQUAL_STRING("now", out);
    omni_fmt_age(86400LL * 99999, out);
    TEST_ASSERT_EQUAL_STRING("99999d", out);
}

static void test_sync_init_due_and_stale(void)
{
    omni_sync_t s;
    omni_sync_init(&s);
    TEST_ASSERT_TRUE(omni_sync_due(&s, 1000));
    TEST_ASSERT_TRUE(omni_sync_stale(&s, 1000));
}

static void test_sync_success_schedule(void)
{
    omni_sync_t s;
    omni_sync_init(&s);
    omni_sync_result(&s, 1000, false);
    omni_sync_result(&s, 2000, true);
    TEST_ASSERT_EQUAL(0, s.failures);
    TEST_ASSERT_EQUAL_INT64(2000, s.last_ok);
    TEST_ASSERT_EQUAL_INT64(2000 + 21600, s.next_try);
    TEST_ASSERT_FALSE(omni_sync_due(&s, 2000 + 21599));
    TEST_ASSERT_TRUE(omni_sync_due(&s, 2000 + 21600));
    TEST_ASSERT_FALSE(omni_sync_stale(&s, 2000 + 86400));
}

static void test_sync_backoff_gives_up(void)
{
    omni_sync_t s;
    omni_sync_init(&s);
    const int64_t t0 = 1000000;
    omni_sync_result(&s, t0, true);
    const int64_t now = t0 + 100;
    const int64_t want[] = {60, 300, 1800, 21600, 21600};
    for (int i = 0; i < 5; i++) {
        omni_sync_result(&s, now, false);
        TEST_ASSERT_EQUAL_INT64(now + want[i], s.next_try);
    }
    TEST_ASSERT_EQUAL_INT64(t0, s.last_ok);
    TEST_ASSERT_FALSE(omni_sync_stale(&s, t0 + 86400));
    TEST_ASSERT_TRUE(omni_sync_stale(&s, t0 + 86401));
}

/* The interval comes from the clock calibration (dense while learning); failures keep their backoff. */
static void test_sync_period_is_settable(void)
{
    omni_sync_t s;
    omni_sync_init(&s);
    TEST_ASSERT_EQUAL_INT32(21600, s.period_s);
    s.period_s = 900;
    omni_sync_result(&s, 2000, true);
    TEST_ASSERT_FALSE(omni_sync_due(&s, 2000 + 899));
    TEST_ASSERT_TRUE(omni_sync_due(&s, 2000 + 900));
    omni_sync_result(&s, 3000, false);
    TEST_ASSERT_TRUE(omni_sync_due(&s, 3000 + 60));
}

/* A shorter period pulls a scheduled success retry in; a longer one or a failure backoff is left alone. */
static void test_sync_set_period(void)
{
    omni_sync_t s;
    omni_sync_init(&s);
    omni_sync_result(&s, 1000, true);
    omni_sync_set_period(&s, 900);
    TEST_ASSERT_EQUAL_INT64(1900, s.next_try);
    omni_sync_set_period(&s, 21600);
    TEST_ASSERT_EQUAL_INT64(1900, s.next_try);
    omni_sync_result(&s, 2000, false); /* retry in 60 s */
    omni_sync_set_period(&s, 900);
    TEST_ASSERT_EQUAL_INT64(2060, s.next_try);
    TEST_ASSERT_EQUAL_INT32(900, s.period_s);
}

/* Dense intervals are for successful syncs: once the backoff runs out, failures retry at 6 h at most often. */
static void test_sync_backoff_with_a_short_period(void)
{
    omni_sync_t s;
    omni_sync_init(&s);
    s.period_s = 900;
    omni_sync_result(&s, 1000, true);
    int64_t now = 2000;
    for (int i = 0; i < 5; i++) omni_sync_result(&s, now, false);
    TEST_ASSERT_EQUAL_INT64(now + 21600, s.next_try);
    s.period_s = 43200; /* a longer period still applies */
    omni_sync_result(&s, now, false);
    TEST_ASSERT_EQUAL_INT64(now + 43200, s.next_try);
}

static void test_sync_failures_saturate(void)
{
    omni_sync_t s;
    omni_sync_init(&s);
    for (int i = 0; i < 1000; i++) omni_sync_result(&s, 5, false);
    TEST_ASSERT_EQUAL_INT64(5 + 21600, s.next_try);
}

/* The 11 o'clock lens (R48/C16): unset clock blinks, stale sync is steady and dim, otherwise off. */
static void test_sync_lens(void)
{
    const int64_t now = 1767225600; /* 2026-01-01, a set clock */
    omni_sync_t s;
    omni_sync_init(&s);
    TEST_ASSERT_EQUAL_INT(OMNI_LENS_UNSET, omni_sync_lens(0, &s));
    TEST_ASSERT_EQUAL_INT(OMNI_LENS_STALE, omni_sync_lens(now, &s)); /* never synced, clock set */
    omni_sync_result(&s, now - 3600, true);
    TEST_ASSERT_EQUAL_INT(OMNI_LENS_OFF, omni_sync_lens(now, &s));
    omni_sync_result(&s, now - 86401, true);
    TEST_ASSERT_EQUAL_INT(OMNI_LENS_STALE, omni_sync_lens(now, &s));
}

/* m3: the day and month names the faces share (they were five copies) */
static void test_day_month_names(void)
{
    TEST_ASSERT_EQUAL_STRING("Sunday", omni_day_name(0));
    TEST_ASSERT_EQUAL_STRING("Saturday", omni_day_name(6));
    TEST_ASSERT_EQUAL_STRING("Sunday", omni_day_name(7));
    TEST_ASSERT_EQUAL_STRING("Saturday", omni_day_name(-1));
    TEST_ASSERT_EQUAL_STRING("January", omni_month_name(0));
    TEST_ASSERT_EQUAL_STRING("September", omni_month_name(8));
    TEST_ASSERT_EQUAL_STRING("December", omni_month_name(11));
    TEST_ASSERT_EQUAL_STRING("January", omni_month_name(12));
    char b[16];
    snprintf(b, sizeof b, "%.3s", omni_month_name(8));   /* the short forms are the first three letters */
    TEST_ASSERT_EQUAL_STRING("Sep", b);
    snprintf(b, sizeof b, "%s 5", omni_day_name(3));
    omni_ascii_upper(b);
    TEST_ASSERT_EQUAL_STRING("WEDNESDAY 5", b);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_time_unset_before_2025);
    RUN_TEST(test_fmt_clock_12_24);
    RUN_TEST(test_fmt_clock_zero_padding);
    RUN_TEST(test_fmt_short_time);
    RUN_TEST(test_tz_dst_spring_forward);
    RUN_TEST(test_tz_far_zone);
    RUN_TEST(test_fmt_age);
    RUN_TEST(test_sync_init_due_and_stale);
    RUN_TEST(test_sync_success_schedule);
    RUN_TEST(test_sync_backoff_gives_up);
    RUN_TEST(test_sync_period_is_settable);
    RUN_TEST(test_sync_set_period);
    RUN_TEST(test_sync_backoff_with_a_short_period);
    RUN_TEST(test_sync_failures_saturate);
    RUN_TEST(test_sync_lens);
    RUN_TEST(test_day_month_names);
    return UNITY_END();
}
