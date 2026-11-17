#include "unity.h"
#include "omni/sun.h"
#include "omni/timefmt.h"
#include <stdio.h>
#include <string.h>

void setUp(void) { omni_tz_apply("EST5EDT,M3.2.0,M11.1.0"); }
void tearDown(void) {}

#define COL_LAT 39.96
#define COL_LON (-83.0)

/* 2026-10-22 00:00 UTC. */
#define OCT22_UTC 1792627200LL
#define H(h) ((h) * 3600LL)

/* External references for 39.96 N, 83.00 W (controller-computed with the NOAA solar-calculator
 * algorithm, zenith 90.833 deg), as UTC offsets within 2026-10-22 / 23:
 *   Oct 22 rise 7:49:59 EDT = 11:49:59 UTC, Oct 22 set 18:42:16 EDT = 22:42:16 UTC,
 *   Oct 23 rise 7:51:04 EDT = 11:51:04 UTC. */
#define REF_RISE22 (OCT22_UTC + H(11) + 49 * 60 + 59)
#define REF_SET22 (OCT22_UTC + H(22) + 42 * 60 + 16)
#define REF_RISE23 (OCT22_UTC + 86400 + H(11) + 51 * 60 + 4)

/* Minutes encoded in "<h>h <mm>m" found after prefix. */
static int dur_min(const char *s, const char *prefix)
{
    size_t n = strlen(prefix);
    TEST_ASSERT_EQUAL_INT(0, strncmp(s, prefix, n));
    int h = -1, m = -1;
    TEST_ASSERT_EQUAL_INT(2, sscanf(s + n, "%dh %dm", &h, &m));
    return h * 60 + m;
}

/* Minutes of day in "<h>:<mm><a|p>" (12 h) or "<hh>:<mm>" after prefix. */
static int clock_min(const char *s, const char *prefix)
{
    size_t n = strlen(prefix);
    TEST_ASSERT_EQUAL_INT(0, strncmp(s, prefix, n));
    int h = -1, m = -1;
    char c = 0;
    int k = sscanf(s + n, "%d:%d%c", &h, &m, &c);
    TEST_ASSERT_TRUE(k >= 2);
    if (k == 3 && c == 'p' && h != 12) h += 12;
    if (k == 3 && c == 'a' && h == 12) h = 0;
    return h * 60 + m;
}

static void test_fmt_dur(void)
{
    char b[12];
    omni_fmt_dur(59, b);
    TEST_ASSERT_EQUAL_STRING("0h 01m", b);
    omni_fmt_dur(32520, b);
    TEST_ASSERT_EQUAL_STRING("9h 02m", b);
}

static void test_glance_text_before_rise(void)
{
    char b[40];
    omni_sun_glance_text(OCT22_UTC + H(10), COL_LAT, COL_LON, false, b, sizeof b);
    TEST_ASSERT_INT_WITHIN(1, 110, dur_min(b, "sunrise in "));   /* 11:49:59 - 10:00 */
}

static void test_glance_text_day(void)
{
    char b[40];
    omni_sun_glance_text(OCT22_UTC + H(15), COL_LAT, COL_LON, false, b, sizeof b);
    TEST_ASSERT_INT_WITHIN(1, 7 * 60 + 43, dur_min(b, "sunset in "));   /* 22:42:16 - 15:00 */
}

static void test_glance_text_after_set(void)
{
    char b[40];
    omni_sun_glance_text(OCT22_UTC + H(23) + 1800, COL_LAT, COL_LON, false, b, sizeof b);
    TEST_ASSERT_INT_WITHIN(1, 7 * 60 + 51, clock_min(b, "sunrise "));
    TEST_ASSERT_NOT_NULL(strchr(b, 'a'));
}

static void test_glance_text_composition(void)
{
    int64_t now = OCT22_UTC + H(10), rise = 0, set = 0, noon = OCT22_UTC + H(12);
    TEST_ASSERT_EQUAL(SUN_NORMAL, omni_sun_rise_set(noon, COL_LAT, COL_LON, &rise, &set));
    char d[12], want[40], got[40];
    omni_fmt_dur(rise - now, d);
    snprintf(want, sizeof want, "sunrise in %s", d);
    omni_sun_glance_text(now, COL_LAT, COL_LON, false, got, sizeof got);
    TEST_ASSERT_EQUAL_STRING(want, got);
}

/* Final fix (review finding 2): the text moves only with the clock's minute, never mid-minute (the glance redraws
 * when it changes: N6 0 frames). Unfloored, "set - now" rounded to the minute changes at second (set + 30) mod 60. */
static void test_glance_text_changes_on_the_minute(void)
{
    static const int64_t FROM[] = { OCT22_UTC + H(9), OCT22_UTC + H(21) };   /* before rise; before set */
    for (int f = 0; f < 2; f++) {
        char prev[40] = "", got[40];
        int changes = 0;
        for (int64_t t = FROM[f]; t < FROM[f] + H(2); t++) {
            omni_sun_glance_text(t, COL_LAT, COL_LON, false, got, sizeof got);
            if (t != FROM[f] && strcmp(got, prev) != 0) {
                TEST_ASSERT_EQUAL_INT64(0, t % 60);
                changes++;
            }
            memcpy(prev, got, sizeof prev);
        }
        TEST_ASSERT_TRUE(changes >= 100);   /* it still counts down, once a minute */
    }
}

static void test_glance_text_polar(void)
{
    char b[40] = "x";
    omni_sun_glance_text(1797854400LL, 78.22, 15.65, false, b, sizeof b);   /* 2026-12-21 12:00 UTC */
    TEST_ASSERT_EQUAL_STRING("", b);
}

static void test_margins_columbus(void)
{
    char r[8], s[8];
    int64_t now = OCT22_UTC + H(15);
    omni_sun_margins(now, COL_LAT, COL_LON, false, r, s);
    TEST_ASSERT_INT_WITHIN(1, 7 * 60 + 50, clock_min(r, ""));
    TEST_ASSERT_INT_WITHIN(1, 18 * 60 + 42, clock_min(s, ""));
    TEST_ASSERT_EQUAL_CHAR('p', s[strlen(s) - 1]);
    omni_sun_margins(now, COL_LAT, COL_LON, true, r, s);
    TEST_ASSERT_INT_WITHIN(1, 18 * 60 + 42, clock_min(s, ""));
    TEST_ASSERT_EQUAL_INT(5, (int)strlen(s));
}

static void test_margins_polar(void)
{
    char r[8], s[8];
    omni_sun_margins(1797854400LL, 78.22, 15.65, false, r, s);
    TEST_ASSERT_EQUAL_STRING("\xe2\x80\x94", r);
    TEST_ASSERT_EQUAL_STRING("\xe2\x80\x94", s);
}

static void test_rise_set_vs_noaa_refs(void)
{
    int64_t r = 0, st = 0;
    TEST_ASSERT_EQUAL(SUN_NORMAL, omni_sun_rise_set(OCT22_UTC + H(12), COL_LAT, COL_LON, &r, &st));
    TEST_ASSERT_INT64_WITHIN(60, REF_RISE22, r);
    TEST_ASSERT_INT64_WITHIN(60, REF_SET22, st);
    TEST_ASSERT_EQUAL(SUN_NORMAL, omni_sun_rise_set(OCT22_UTC + 86400 + H(12), COL_LAT, COL_LON, &r, &st));
    TEST_ASSERT_INT64_WITHIN(60, REF_RISE23, r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fmt_dur);
    RUN_TEST(test_glance_text_before_rise);
    RUN_TEST(test_glance_text_day);
    RUN_TEST(test_glance_text_after_set);
    RUN_TEST(test_glance_text_composition);
    RUN_TEST(test_glance_text_changes_on_the_minute);
    RUN_TEST(test_glance_text_polar);
    RUN_TEST(test_margins_columbus);
    RUN_TEST(test_margins_polar);
    RUN_TEST(test_rise_set_vs_noaa_refs);
    return UNITY_END();
}
