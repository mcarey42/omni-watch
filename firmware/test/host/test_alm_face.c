/* The almanac face's pure helpers (omni/alm_face.h, Task 7 fix m7): the ring's local-date key across DST changes
 * (US and southern), midnights and a year end, and the hour numerals in 12 h and 24 h. */
#include "unity.h"
#include "omni/alm_face.h"
#include "omni/timefmt.h"

#define EST "EST5EDT,M3.2.0,M11.1.0"
#define AEST "AEST-10AEDT,M10.1.0,M4.1.0/3"

void setUp(void) { omni_tz_apply(EST); }
void tearDown(void) {}

static int64_t utc(int y, int m, int d, int hh, int mm, int ss)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (era * 146097 + doe - 719468) * 86400 + hh * 3600 + mm * 60 + ss;
}

/* [m0, m1) is one local day: one key inside, the day before and after one less and one more */
static void check_day(int64_t m0, int64_t m1)
{
    const int32_t k = omni_alm_date_key(m0);
    TEST_ASSERT_EQUAL_INT32(k, omni_alm_date_key(m1 - 1));
    TEST_ASSERT_EQUAL_INT32(k, omni_alm_date_key((m0 + m1) / 2));
    TEST_ASSERT_EQUAL_INT32(k - 1, omni_alm_date_key(m0 - 1));
    TEST_ASSERT_EQUAL_INT32(k + 1, omni_alm_date_key(m1));
}

void test_alm_date_key_us_dst(void)
{
    check_day(utc(2026, 3, 7, 5, 0, 0), utc(2026, 3, 8, 5, 0, 0));    /* an ordinary 24 h EST day */
    check_day(utc(2026, 3, 8, 5, 0, 0), utc(2026, 3, 9, 4, 0, 0));    /* spring forward: 23 h */
    check_day(utc(2026, 11, 1, 4, 0, 0), utc(2026, 11, 2, 5, 0, 0));  /* fall back: 25 h */
}

void test_alm_date_key_southern_dst(void)
{
    omni_tz_apply(AEST);
    check_day(utc(2026, 10, 3, 14, 0, 0), utc(2026, 10, 4, 13, 0, 0));  /* Sydney spring forward, 4 Oct: 23 h */
    check_day(utc(2026, 4, 4, 13, 0, 0), utc(2026, 4, 5, 14, 0, 0));    /* fall back, 5 Apr: 25 h */
}

void test_alm_date_key_year_and_zone(void)
{
    const int64_t ny = utc(2027, 1, 1, 5, 0, 0);   /* local midnight, New Year, EST */
    TEST_ASSERT_TRUE(omni_alm_date_key(ny) > omni_alm_date_key(ny - 1));
    TEST_ASSERT_EQUAL_INT32(omni_alm_date_key(ny), omni_alm_date_key(ny + 86399));
    /* the same instant in another zone is another local date: the key follows the TZ as applied */
    const int64_t t = utc(2026, 6, 21, 15, 0, 0);   /* 11:00 EDT on the 21st, 01:00 AEST on the 22nd */
    const int32_t k_est = omni_alm_date_key(t);
    omni_tz_apply(AEST);
    TEST_ASSERT_EQUAL_INT32(k_est + 1, omni_alm_date_key(t));
}

void test_alm_hour_labels(void)
{
    static const char *const H12[8] = {"12a", "3", "6", "9", "12p", "3", "6", "9"};
    static const char *const H24[8] = {"00", "03", "06", "09", "12", "15", "18", "21"};
    char s[4];
    for (int i = 0; i < 8; i++) {
        omni_alm_hour_label(i * 3, false, s);
        TEST_ASSERT_EQUAL_STRING(H12[i], s);
        omni_alm_hour_label(i * 3, true, s);
        TEST_ASSERT_EQUAL_STRING(H24[i], s);
    }
}

/* R140: the ring cache's key (what omni_alm_ring_render bakes): the local date, the zone, the city, the set state
 * and the rise/set ticks to the minute. The worker renders a ring only for a key the cache does not hold. */
static omni_alm_ring_key_t key(int64_t now, const char *tz, int city, bool set, float rise, float sset)
{
    omni_alm_ring_key_t k;
    omni_alm_ring_key(&k, now, tz, city, set, rise, sset);
    return k;
}

void test_alm_ring_key_unset_is_one_key(void)
{
    const omni_alm_ring_key_t a = key(utc(2026, 6, 21, 12, 0, 0), EST, 3, false, 6.2f, 20.9f);
    const omni_alm_ring_key_t b = key(utc(2027, 1, 2, 3, 0, 0), AEST, 9, false, -1, -1);
    TEST_ASSERT_TRUE(omni_alm_ring_key_eq(&a, &b));   /* the night ring, whatever else */
    const omni_alm_ring_key_t c = key(utc(2026, 6, 21, 12, 0, 0), EST, 3, true, 6.2f, 20.9f);
    TEST_ASSERT_FALSE(omni_alm_ring_key_eq(&a, &c));
}

void test_alm_ring_key_day_and_midnight(void)
{
    /* 21 Jun 2026 EDT: local midnight is 04:00 UTC */
    const omni_alm_ring_key_t a = key(utc(2026, 6, 21, 4, 0, 0), EST, 3, true, 6.25f, 20.9f);
    const omni_alm_ring_key_t b = key(utc(2026, 6, 22, 3, 59, 59), EST, 3, true, 6.25f, 20.9f);
    const omni_alm_ring_key_t c = key(utc(2026, 6, 22, 4, 0, 0), EST, 3, true, 6.25f, 20.9f);
    TEST_ASSERT_TRUE(omni_alm_ring_key_eq(&a, &b));
    TEST_ASSERT_FALSE(omni_alm_ring_key_eq(&a, &c));
}

void test_alm_ring_key_dst_day_renders_once(void)
{
    /* 8 Mar 2026: 01:30 EST and 03:30 EDT are the same local date in the same zone: no second render at 2 am */
    const omni_alm_ring_key_t a = key(utc(2026, 3, 8, 6, 30, 0), EST, 3, true, 6.8f, 18.6f);
    const omni_alm_ring_key_t b = key(utc(2026, 3, 8, 7, 30, 0), EST, 3, true, 6.8f, 18.6f);
    TEST_ASSERT_TRUE(omni_alm_ring_key_eq(&a, &b));
}

void test_alm_ring_key_zone_city_ticks(void)
{
    const int64_t t = utc(2026, 6, 21, 15, 0, 0);
    const omni_alm_ring_key_t a = key(t, EST, 3, true, 6.25f, 20.9f);
    omni_tz_apply(AEST);   /* the zone is the string given, not the one applied */
    const omni_alm_ring_key_t a2 = key(t, EST, 3, true, 6.25f, 20.9f);
    omni_tz_apply(EST);
    TEST_ASSERT_EQUAL_INT32(a.date, omni_alm_date_key(t));
    TEST_ASSERT_TRUE(a2.tz == a.tz);
    const omni_alm_ring_key_t z = key(t, "CST6CDT,M3.2.0,M11.1.0", 3, true, 6.25f, 20.9f);
    const omni_alm_ring_key_t c = key(t, EST, 4, true, 6.25f, 20.9f);              /* same zone, another city */
    const omni_alm_ring_key_t r1 = key(t, EST, 3, true, 6.251f, 20.9f);            /* 375.06 min: the same tick */
    const omni_alm_ring_key_t r2 = key(t, EST, 3, true, 6.2584f, 20.9f);           /* 375.5 min: the next */
    const omni_alm_ring_key_t s1 = key(t, EST, 3, true, 6.25f, -1);                /* no set tick (polar) */
    const omni_alm_ring_key_t s0 = key(t, EST, 3, true, 6.25f, 0);                 /* a set tick at midnight */
    TEST_ASSERT_FALSE(omni_alm_ring_key_eq(&a, &z));
    TEST_ASSERT_FALSE(omni_alm_ring_key_eq(&a, &c));
    TEST_ASSERT_TRUE(omni_alm_ring_key_eq(&a, &r1));
    TEST_ASSERT_FALSE(omni_alm_ring_key_eq(&a, &r2));
    TEST_ASSERT_FALSE(omni_alm_ring_key_eq(&s1, &s0));
    const omni_alm_ring_key_t n = key(t, NULL, 3, true, 6.25f, 20.9f);             /* no TZ: the empty string's */
    const omni_alm_ring_key_t e = key(t, "", 3, true, 6.25f, 20.9f);
    TEST_ASSERT_TRUE(omni_alm_ring_key_eq(&n, &e));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_alm_date_key_us_dst);
    RUN_TEST(test_alm_date_key_southern_dst);
    RUN_TEST(test_alm_date_key_year_and_zone);
    RUN_TEST(test_alm_hour_labels);
    RUN_TEST(test_alm_ring_key_unset_is_one_key);
    RUN_TEST(test_alm_ring_key_day_and_midnight);
    RUN_TEST(test_alm_ring_key_dst_day_renders_once);
    RUN_TEST(test_alm_ring_key_zone_city_ticks);
    return UNITY_END();
}
