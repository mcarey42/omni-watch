/* glibc hides tm_gmtoff under the suite's -std=c99 -D_POSIX_C_SOURCE flags (preflight B7). Only this test asks for it;
 * the core stays strict. */
#define _DEFAULT_SOURCE
#include "unity.h"
#include "omni/worldtime.h"
#include "omni/cities.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void setUp(void) {}
void tearDown(void) {}

#define PI_F 3.14159265358979323846f

/* UTC seconds of a civil date (days from civil, Hinnant). */
static int64_t utc(int y, int m, int d, int hh, int mm, int ss)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (era * 146097 + doe - 719468) * 86400 + hh * 3600 + mm * 60 + ss;
}

static void expect_off(const char *tz, int64_t t, int32_t want, bool want_dst)
{
    int32_t off = 12345;
    bool dst = !want_dst;
    TEST_ASSERT_TRUE_MESSAGE(omni_tz_offset(tz, t, &off, &dst), tz);
    TEST_ASSERT_EQUAL_INT_MESSAGE(want, off, tz);
    TEST_ASSERT_EQUAL_INT_MESSAGE(want_dst, dst, tz);
}

void test_tz_transitions(void)
{
    const char *est = "EST5EDT,M3.2.0,M11.1.0";
    expect_off(est, utc(2026, 3, 8, 6, 59, 59), -18000, false);
    expect_off(est, utc(2026, 3, 8, 7, 0, 0), -14400, true);
    expect_off(est, utc(2026, 11, 1, 5, 59, 59), -14400, true);
    expect_off(est, utc(2026, 11, 1, 6, 0, 0), -18000, false);

    const char *gmt = "GMT0BST,M3.5.0/1,M10.5.0";
    expect_off(gmt, utc(2026, 3, 29, 0, 59, 59), 0, false);
    expect_off(gmt, utc(2026, 3, 29, 1, 0, 0), 3600, true);
    expect_off(gmt, utc(2026, 10, 25, 0, 59, 59), 3600, true);
    expect_off(gmt, utc(2026, 10, 25, 1, 0, 0), 0, false);

    /* southern hemisphere: DST spans the new year */
    const char *aest = "AEST-10AEDT,M10.1.0,M4.1.0/3";
    expect_off(aest, utc(2026, 4, 4, 15, 59, 59), 39600, true);
    expect_off(aest, utc(2026, 4, 4, 16, 0, 0), 36000, false);
    expect_off(aest, utc(2026, 10, 3, 15, 59, 59), 36000, false);
    expect_off(aest, utc(2026, 10, 3, 16, 0, 0), 39600, true);
    expect_off(aest, utc(2026, 1, 1, 0, 0, 0), 39600, true);
    expect_off(aest, utc(2026, 12, 31, 23, 59, 59), 39600, true);

    const char *nz = "NZST-12NZDT,M9.5.0,M4.1.0/3";
    expect_off(nz, utc(2026, 9, 26, 13, 59, 59), 43200, false);
    expect_off(nz, utc(2026, 9, 26, 14, 0, 0), 46800, true);

    /* Cairo: the end rule's time is beyond 24 h (M10.5.4/24) */
    const char *eet = "EET-2EEST,M4.5.5/0,M10.5.4/24";
    expect_off(eet, utc(2026, 4, 23, 21, 59, 59), 7200, false);
    expect_off(eet, utc(2026, 4, 23, 22, 0, 0), 10800, true);
    expect_off(eet, utc(2026, 10, 29, 20, 59, 59), 10800, true);
    expect_off(eet, utc(2026, 10, 29, 21, 0, 0), 7200, false);

    /* a southern half-hour zone with DST: 03:00 ACDT ends at 16:30Z the day before */
    const char *acst = "ACST-9:30ACDT,M10.1.0,M4.1.0/3";
    expect_off(acst, utc(2026, 4, 4, 16, 29, 59), 37800, true);
    expect_off(acst, utc(2026, 4, 4, 16, 30, 0), 34200, false);
    expect_off(acst, utc(2026, 10, 3, 16, 29, 59), 34200, false);
    expect_off(acst, utc(2026, 10, 3, 16, 30, 0), 37800, true);

    expect_off("IST-5:30", utc(2026, 6, 1, 0, 0, 0), 19800, false);
    expect_off("<+04>-4", utc(2026, 6, 1, 0, 0, 0), 14400, false);
    expect_off("<-03>3", utc(2026, 6, 1, 0, 0, 0), -10800, false);
    expect_off("NST3:30NDT,M3.2.0,M11.1.0", utc(2026, 7, 1, 0, 0, 0), -9000, true);

    int32_t off = 7;
    bool dst = true;
    TEST_ASSERT_FALSE(omni_tz_offset("", 0, &off, &dst));
    TEST_ASSERT_EQUAL_INT(0, off);
    TEST_ASSERT_FALSE(dst);
    TEST_ASSERT_FALSE(omni_tz_offset(NULL, 0, &off, &dst));
    TEST_ASSERT_FALSE(omni_tz_offset("EST5EDT,M13.2.0,M11.1.0", 0, &off, &dst));
    TEST_ASSERT_FALSE(omni_tz_offset("<+04-4", 0, &off, &dst));
    TEST_ASSERT_FALSE(omni_tz_offset("EST", 0, &off, &dst));
}

void test_tz_matches_libc_all_cities(void)
{
    const int64_t t0 = utc(2026, 1, 1, 0, 0, 0), t1 = utc(2027, 1, 1, 0, 0, 0);
    TEST_ASSERT_TRUE(omni_city_count() > 100);
    for (size_t i = 0; i < omni_city_count(); i++) {
        const omni_city_t *c = omni_city_at(i);
        setenv("TZ", c->posix_tz, 1);
        tzset();
        for (int64_t t = t0; t < t1; t += 6 * 3600) {
            time_t tt = (time_t)t;
            struct tm tm;
            TEST_ASSERT_NOT_NULL(localtime_r(&tt, &tm));
            int32_t off = 0;
            bool dst = false;
            TEST_ASSERT_TRUE_MESSAGE(omni_tz_offset(c->posix_tz, t, &off, &dst), c->posix_tz);
            if ((int32_t)tm.tm_gmtoff != off) {
                char msg[160];
                snprintf(msg, sizeof msg, "%s (%s) at %lld: libc %ld, ours %ld", c->name, c->posix_tz, (long long)t,
                         (long)tm.tm_gmtoff, (long)off);
                TEST_FAIL_MESSAGE(msg);
            }
            TEST_ASSERT_EQUAL_INT_MESSAGE(tm.tm_isdst > 0, dst, c->posix_tz);
        }
    }
    unsetenv("TZ");   /* not left on the last city */
    tzset();
}

/* m2: every zone in the city table against libc, hourly over 2026-2028 (2028 is a leap year), and each transition
 * libc makes pinned to the second: ours agrees one second before it and at it. Zones are taken once each. */
static int32_t libc_off(int64_t t)
{
    time_t tt = (time_t)t;
    struct tm tm;
    TEST_ASSERT_NOT_NULL(localtime_r(&tt, &tm));
    return (int32_t)tm.tm_gmtoff;
}

static void expect_libc(const char *tz, int64_t t)
{
    int32_t off = 0;
    bool dst = false;
    TEST_ASSERT_TRUE_MESSAGE(omni_tz_offset(tz, t, &off, &dst), tz);
    if (off != libc_off(t)) {
        char msg[160];
        snprintf(msg, sizeof msg, "%s at %lld: libc %ld, ours %ld", tz, (long long)t, (long)libc_off(t), (long)off);
        TEST_FAIL_MESSAGE(msg);
    }
}

void test_tz_transitions_to_the_second_all_zones(void)
{
    const char *saved = getenv("TZ");
    char keep[64] = "";
    if (saved) snprintf(keep, sizeof keep, "%s", saved);
    int zones = 0, ruled = 0, transitions = 0;
    for (size_t i = 0; i < omni_city_count(); i++) {
        const char *tz = omni_city_at(i)->posix_tz;
        bool dup = false;
        for (size_t j = 0; j < i && !dup; j++) dup = !strcmp(omni_city_at(j)->posix_tz, tz);
        if (dup) continue;
        zones++;
        setenv("TZ", tz, 1);
        tzset();
        int here = 0;
        for (int y = 2026; y <= 2028; y++) {
            const int64_t t0 = utc(y, 1, 1, 0, 0, 0), t1 = utc(y + 1, 1, 1, 0, 0, 0);
            int32_t prev = libc_off(t0);
            for (int64_t t = t0; t < t1; t += 3600) {
                expect_libc(tz, t);
                const int32_t o = libc_off(t + 3600);
                if (o == prev) continue;
                int64_t lo = t, hi = t + 3600;   /* libc: prev at lo, o at hi; the first second of o */
                while (hi - lo > 1) {
                    const int64_t mid = lo + (hi - lo) / 2;
                    if (libc_off(mid) == prev) lo = mid;
                    else hi = mid;
                }
                expect_libc(tz, hi - 1);
                expect_libc(tz, hi);
                prev = o;
                here++;
            }
        }
        if (here) {
            ruled++;
            TEST_ASSERT_EQUAL_INT_MESSAGE(6, here, tz);   /* two a year */
        }
        transitions += here;
    }
    if (keep[0]) setenv("TZ", keep, 1);
    else unsetenv("TZ");
    tzset();
    TEST_ASSERT_TRUE(zones >= 20);
    TEST_ASSERT_TRUE(ruled >= 8);
    TEST_ASSERT_EQUAL_INT(6 * ruled, transitions);
}

/* m1: no signed overflow at extreme times (UBSan run): beyond +-1e12 s (about 31 700 years) the evaluator refuses, and
 * the cartouche and glance show an unset clock rather than a wrapped one. */
void test_tz_extreme_times(void)
{
    static const int64_t T[] = { INT64_MAX, INT64_MAX - 3600, INT64_MIN, INT64_MIN + 3600, 1000000000001LL,
                                 -1000000000001LL };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        int32_t off = 1;
        bool dst = true;
        TEST_ASSERT_FALSE(omni_tz_offset("EST5EDT,M3.2.0,M11.1.0", T[i], &off, &dst));
        TEST_ASSERT_EQUAL_INT(0, off);
        TEST_ASSERT_FALSE(dst);
        TEST_ASSERT_FALSE(omni_tz_offset("<+1345>-13:45<+1445>,M9.5.0/2:45,M4.1.0/3:45", T[i], &off, &dst));
    }
    for (int s = -1; s <= 1; s += 2) {   /* the limits themselves are evaluated */
        int32_t off = 1;
        bool dst = false;
        TEST_ASSERT_TRUE(omni_tz_offset("EST5EDT,M3.2.0,M11.1.0", s * OMNI_TZ_UTC_LIMIT, &off, &dst));
        TEST_ASSERT_EQUAL_INT(dst ? -14400 : -18000, off);
    }
    omni_wt_city_t l[OMNI_WT_MAX];
    const int n = omni_wt_list((uint16_t)omni_city_find_key("Columbus", "US"), l);
    char big[8], top[4], sub[24], g[24];
    omni_wt_cartouche(&l[0], &l[n - 1], INT64_MAX, false, big, top, sub);
    TEST_ASSERT_EQUAL_STRING("--:--", big);
    omni_wt_glance_text(&l[0], INT64_MAX, false, g);
    TEST_ASSERT_EQUAL_STRING("LON  --:--", g);
}

void test_wt_list_columbus(void)
{
    omni_wt_city_t l[OMNI_WT_MAX];
    int home = omni_city_find_key("Columbus", "US");
    TEST_ASSERT_TRUE(home >= 0);
    TEST_ASSERT_EQUAL_INT(13, omni_wt_list((uint16_t)home, l));
    TEST_ASSERT_EQUAL_STRING("LON", l[0].code);
    TEST_ASSERT_EQUAL_STRING("TYO", l[4].code);   /* mockup order: LON NYC LAX HNL TYO SYD ... */
    TEST_ASSERT_EQUAL_STRING("AKL", l[11].code);
    TEST_ASSERT_EQUAL_STRING("CMH", l[12].code);
    TEST_ASSERT_TRUE(l[12].home);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)home, l[12].city);
    for (int i = 0; i < 12; i++) TEST_ASSERT_FALSE(l[i].home);
}

void test_wt_list_home_in_twelve(void)
{
    omni_wt_city_t l[OMNI_WT_MAX];
    int home = omni_city_find_key("London", "GB");
    TEST_ASSERT_EQUAL_INT(12, omni_wt_list((uint16_t)home, l));
    TEST_ASSERT_EQUAL_STRING("LON", l[0].code);
    int marked = 0;
    for (int i = 0; i < 12; i++) marked += l[i].home;
    TEST_ASSERT_EQUAL_INT(1, marked);
    TEST_ASSERT_TRUE(l[0].home);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)home, l[0].city);
}

void test_wt_list_twelve_cities(void)
{
    static const char *const want[12][2] = {
        {"London", "GB"}, {"New York", "US"}, {"Los Angeles", "US"}, {"Honolulu", "US"}, {"Tokyo", "JP"},
        {"Sydney", "AU"}, {"Singapore", "SG"}, {"Delhi", "IN"}, {"Dubai", "AE"}, {"Cairo", "EG"},
        {"Sao Paulo", "BR"}, {"Auckland", "NZ"}};
    static const char *const codes[12] = {"LON", "NYC", "LAX", "HNL", "TYO", "SYD", "SIN", "DEL", "DXB", "CAI", "SAO", "AKL"};
    omni_wt_city_t l[OMNI_WT_MAX];
    TEST_ASSERT_EQUAL_INT(13, omni_wt_list((uint16_t)omni_city_find_key("Columbus", "US"), l));
    for (int i = 0; i < 12; i++) {
        TEST_ASSERT_EQUAL_STRING(codes[i], l[i].code);
        TEST_ASSERT_EQUAL_INT(omni_city_find_key(want[i][0], want[i][1]), l[i].city);
    }
}

void test_wt_list_home_code(void)
{
    char c[4];
    omni_wt_code("Rio de Janeiro", c);
    TEST_ASSERT_EQUAL_STRING("RIO", c);
    omni_wt_code("Columbus", c);
    TEST_ASSERT_EQUAL_STRING("CMH", c);
    omni_wt_code("La Paz", c);
    TEST_ASSERT_EQUAL_STRING("LAP", c);
    omni_wt_code("St. John's", c);
    TEST_ASSERT_EQUAL_STRING("STJ", c);
    omni_wt_code("Tokyo", c);
    TEST_ASSERT_EQUAL_STRING("TYO", c);
    omni_wt_code("Ho", c);   /* fewer than three letters: what there is */
    TEST_ASSERT_EQUAL_STRING("HO", c);
    omni_wt_code("", c);
    TEST_ASSERT_EQUAL_STRING("", c);
}

void test_wt_angle_flip(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -PI_F / 2, omni_wt_angle(40.0f, 40.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -PI_F, omni_wt_angle(130.0f, 40.0f));
    TEST_ASSERT_TRUE(omni_wt_flip(PI_F / 2));
    TEST_ASSERT_FALSE(omni_wt_flip(-PI_F / 2));
    TEST_ASSERT_FALSE(omni_wt_flip(0.0f));   /* sin 0 = 0, under 0.15 */
}

static void tap_at(float a, float r, int16_t *x, int16_t *y)
{
    *x = (int16_t)lroundf(233.0f + r * cosf(a));
    *y = (int16_t)lroundf(233.0f + r * sinf(a));
}

void test_wt_hit(void)
{
    omni_wt_city_t l[OMNI_WT_MAX];
    int n = omni_wt_list((uint16_t)omni_city_find_key("Columbus", "US"), l);
    const float lon_c = -83.0f;
    int tyo = -1;
    for (int i = 0; i < n; i++)
        if (!strcmp(l[i].code, "TYO")) tyo = i;
    TEST_ASSERT_TRUE(tyo >= 0);
    int16_t x, y;
    tap_at(omni_wt_angle(omni_city_at(l[tyo].city)->lon, lon_c), 185.0f, &x, &y);
    TEST_ASSERT_EQUAL_INT(tyo, omni_wt_hit(l, n, x, y, lon_c));
    TEST_ASSERT_EQUAL_INT(-2, omni_wt_hit(l, n, 233 + 50, 233, lon_c));
    TEST_ASSERT_EQUAL_INT(-2, omni_wt_hit(l, n, 233, 233, lon_c));
    TEST_ASSERT_EQUAL_INT(-1, omni_wt_hit(l, n, 233 + 100, 233, lon_c));
    TEST_ASSERT_EQUAL_INT(-1, omni_wt_hit(l, n, 233 + 100, 233, lon_c));

    /* 0.3 rad from a city is a miss wherever no other city is nearer than 0.25 */
    int misses = 0;
    for (int i = 0; i < n; i++) {
        const float a = omni_wt_angle(omni_city_at(l[i].city)->lon, lon_c);
        for (int s = -1; s <= 1; s += 2) {
            const float ta = a + (float)s * 0.3f;
            float best = 9.0f;
            for (int j = 0; j < n; j++) {
                float d = fabsf(remainderf(ta - omni_wt_angle(omni_city_at(l[j].city)->lon, lon_c), 2 * PI_F));
                if (d < best) best = d;
            }
            if (best < 0.27f) continue;   /* another city is within reach (and the tap is rounded to a pixel) */
            tap_at(ta, 185.0f, &x, &y);
            TEST_ASSERT_EQUAL_INT(-1, omni_wt_hit(l, n, x, y, lon_c));
            misses++;
        }
    }
    TEST_ASSERT_TRUE(misses > 0);
}

/* m3: the hit's edges. Off-centre within 0.25 rad is still that city, just past it is not; the atan2 seam (a city at
 * canvas angle pi, taps either side of it) and the date line (lon_c 180: Auckland and Honolulu either side) wrap. */
static float nearest_other(const omni_wt_city_t *l, int n, int self, float a, float lon_c)
{
    float best = 9.0f;
    for (int j = 0; j < n; j++) {
        if (j == self) continue;
        const float d = fabsf(remainderf(a - omni_wt_angle(omni_city_at(l[j].city)->lon, lon_c), 2 * PI_F));
        if (d < best) best = d;
    }
    return best;
}

static int index_of(const omni_wt_city_t *l, int n, const char *code)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(l[i].code, code)) return i;
    TEST_FAIL_MESSAGE(code);
    return -1;
}

void test_wt_hit_edges(void)
{
    omni_wt_city_t l[OMNI_WT_MAX];
    const int n = omni_wt_list((uint16_t)omni_city_find_key("Columbus", "US"), l);
    int16_t x, y;
    /* off-centre, at r 220 (a pixel is 0.0045 rad there): 0.2 and 0.235 hit, 0.265 misses, both sides */
    const int hnl = index_of(l, n, "HNL"), akl = index_of(l, n, "AKL"), lon = index_of(l, n, "LON");
    const float lon_c = -83.0f, a = omni_wt_angle(omni_city_at(l[lon].city)->lon, lon_c);
    for (int s = -1; s <= 1; s += 2) {   /* London: Cairo 0.55 rad east, Sao Paulo 0.81 west */
        TEST_ASSERT_TRUE(nearest_other(l, n, lon, a + s * 0.265f, lon_c) > 0.27f);
        tap_at(a + s * 0.2f, 220.0f, &x, &y);
        TEST_ASSERT_EQUAL_INT(lon, omni_wt_hit(l, n, x, y, lon_c));
        tap_at(a + s * 0.235f, 220.0f, &x, &y);
        TEST_ASSERT_EQUAL_INT(lon, omni_wt_hit(l, n, x, y, lon_c));
        tap_at(a + s * 0.265f, 220.0f, &x, &y);
        TEST_ASSERT_EQUAL_INT(-1, omni_wt_hit(l, n, x, y, lon_c));
    }
    /* the band starts at r 120 */
    tap_at(a, 119.0f, &x, &y);
    TEST_ASSERT_EQUAL_INT(-1, omni_wt_hit(l, n, x, y, lon_c));
    tap_at(a, 122.0f, &x, &y);
    TEST_ASSERT_EQUAL_INT(lon, omni_wt_hit(l, n, x, y, lon_c));
    /* the atan2 seam: lon_c puts Honolulu at canvas angle pi (left); a tap 0.1 rad either side wraps to it */
    const float seam_c = omni_city_at(l[hnl].city)->lon - 90.0f + 360.0f;   /* 112.14 */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, PI_F, fabsf(remainderf(omni_wt_angle(omni_city_at(l[hnl].city)->lon, seam_c),
                                                             2 * PI_F)));
    TEST_ASSERT_TRUE(nearest_other(l, n, hnl, PI_F, seam_c) > 0.35f);
    tap_at(PI_F - 0.1f, 220.0f, &x, &y);
    TEST_ASSERT_TRUE(y > 233);   /* atan2 near +pi */
    TEST_ASSERT_EQUAL_INT(hnl, omni_wt_hit(l, n, x, y, seam_c));
    tap_at(PI_F + 0.1f, 220.0f, &x, &y);
    TEST_ASSERT_TRUE(y < 233);   /* atan2 near -pi */
    TEST_ASSERT_EQUAL_INT(hnl, omni_wt_hit(l, n, x, y, seam_c));
    /* the date line: from lon_c 180, Auckland and Honolulu on either side of the top */
    const float a_akl = omni_wt_angle(omni_city_at(l[akl].city)->lon, 180.0f);
    const float a_hnl = omni_wt_angle(omni_city_at(l[hnl].city)->lon, 180.0f);
    tap_at(a_akl, 200.0f, &x, &y);
    TEST_ASSERT_EQUAL_INT(akl, omni_wt_hit(l, n, x, y, 180.0f));
    tap_at(a_hnl, 200.0f, &x, &y);
    TEST_ASSERT_EQUAL_INT(hnl, omni_wt_hit(l, n, x, y, 180.0f));
    tap_at(a_hnl - 2 * PI_F, 200.0f, &x, &y);   /* the same place, a turn away */
    TEST_ASSERT_EQUAL_INT(hnl, omni_wt_hit(l, n, x, y, -180.0f));
}

static void cart(const char *home_name, const char *home_country, const char *code, bool h24, int64_t now,
                 char big[8], char top[4], char sub[24])
{
    omni_wt_city_t l[OMNI_WT_MAX];
    int n = omni_wt_list((uint16_t)omni_city_find_key(home_name, home_country), l);
    const omni_wt_city_t *home = NULL, *sel = NULL;
    for (int i = 0; i < n; i++)
        if (l[i].home) home = &l[i];
    TEST_ASSERT_NOT_NULL(home);
    for (int i = 0; code && i < n; i++)
        if (!strcmp(l[i].code, code)) sel = &l[i];
    TEST_ASSERT_TRUE(!code || sel);
    omni_wt_cartouche(sel, home, now, h24, big, top, sub);
}

void test_wt_cartouche(void)
{
    const int64_t now = utc(2026, 11, 1, 15, 0, 0);
    char big[8], top[4], sub[24];
    cart("Columbus", "US", NULL, false, now, big, top, sub);
    TEST_ASSERT_EQUAL_STRING("10:00", big);
    TEST_ASSERT_EQUAL_STRING("CMH", top);
    TEST_ASSERT_EQUAL_STRING("SUN AM", sub);
    cart("Columbus", "US", "TYO", false, now, big, top, sub);
    TEST_ASSERT_EQUAL_STRING("12:00", big);
    TEST_ASSERT_EQUAL_STRING("TYO", top);
    TEST_ASSERT_EQUAL_STRING("MON AM +14h", sub);
    cart("Columbus", "US", "DEL", false, now, big, top, sub);
    TEST_ASSERT_EQUAL_STRING("8:30", big);
    TEST_ASSERT_EQUAL_STRING("DEL", top);
    TEST_ASSERT_EQUAL_STRING("SUN PM +10:30h", sub);
    cart("Columbus", "US", "TYO", true, now, big, top, sub);
    TEST_ASSERT_EQUAL_STRING("00:00", big);
    TEST_ASSERT_EQUAL_STRING("MON +14h", sub);
    cart("Columbus", "US", NULL, true, now, big, top, sub);
    TEST_ASSERT_EQUAL_STRING("10:00", big);
    TEST_ASSERT_EQUAL_STRING("SUN", sub);
    /* behind home: an en dash (U+2013), whole and half hours */
    cart("Columbus", "US", "HNL", false, now, big, top, sub);
    TEST_ASSERT_EQUAL_STRING("5:00", big);
    TEST_ASSERT_EQUAL_STRING("SUN AM \xe2\x80\x93" "5h", sub);
    cart("Tokyo", "JP", "DEL", false, now, big, top, sub);
    TEST_ASSERT_EQUAL_STRING("SUN PM \xe2\x80\x93" "3:30h", sub);
    /* the home city selected among the twelve reads as home */
    cart("London", "GB", "LON", false, now, big, top, sub);
    TEST_ASSERT_EQUAL_STRING("3:00", big);
    TEST_ASSERT_EQUAL_STRING("LON", top);
    TEST_ASSERT_EQUAL_STRING("SUN PM", sub);
}

void test_wt_cartouche_unset(void)
{
    char big[8], top[4], sub[24] = "x";
    cart("Columbus", "US", "TYO", false, 0, big, top, sub);
    TEST_ASSERT_EQUAL_STRING("--:--", big);
    TEST_ASSERT_EQUAL_STRING("TYO", top);
    TEST_ASSERT_EQUAL_STRING("", sub);
    cart("Columbus", "US", NULL, true, 0, big, top, sub);
    TEST_ASSERT_EQUAL_STRING("--:--", big);
    TEST_ASSERT_EQUAL_STRING("CMH", top);
    TEST_ASSERT_EQUAL_STRING("", sub);
}

void test_wt_glance_text(void)
{
    omni_wt_city_t l[OMNI_WT_MAX];
    omni_wt_list((uint16_t)omni_city_find_key("Columbus", "US"), l);
    char out[24];
    omni_wt_glance_text(&l[0], utc(2026, 11, 1, 15, 42, 0), false, out);
    TEST_ASSERT_EQUAL_STRING("LON  3:42 pm", out);
    omni_wt_glance_text(&l[0], utc(2026, 11, 1, 15, 42, 0), true, out);
    TEST_ASSERT_EQUAL_STRING("LON  15:42", out);
    omni_wt_glance_text(&l[0], utc(2026, 11, 1, 0, 5, 0), false, out);
    TEST_ASSERT_EQUAL_STRING("LON  12:05 am", out);
    omni_wt_glance_text(&l[0], 0, false, out);
    TEST_ASSERT_EQUAL_STRING("LON  --:--", out);
}

/* R138: the reveal is fade-only: the map brightens in place over 300 ms and never turns (no lon offset at any t), so
 * the face needs no globe frame per step; reduced motion is the same curve. */
void test_wt_reveal(void)
{
    omni_reveal_t r;
    for (uint32_t t = 0; t <= 2000; t += 50) {
        omni_wt_reveal(t, false, &r);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, r.lon_off_deg);
        TEST_ASSERT_EQUAL_FLOAT(1.0f, r.gain);
        TEST_ASSERT_EQUAL_FLOAT(1.0f, r.settle);
        TEST_ASSERT_EQUAL(t >= 300, r.done);
        omni_reveal_t q;
        omni_wt_reveal(t, true, &q);
        TEST_ASSERT_EQUAL_FLOAT(r.fade, q.fade);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, q.lon_off_deg);
        TEST_ASSERT_EQUAL(r.done, q.done);
    }
    omni_wt_reveal(0, false, &r);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.fade);
    omni_wt_reveal(150, false, &r);   /* fade t/300 */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, r.fade);
    omni_wt_reveal(300, false, &r);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, r.fade);
}

/* R141: the band's codes placed glyph by glyph on the arc (the face's own draw, replacing lv_arclabel's whole-degree
 * steps): centres an arc length (adv_i + adv_i+1) / 2 + spacing apart along radius r, the run centred on the label's
 * angle, each glyph's baseline point mid px inside r (clockwise, tops out) or outside (flipped, read anticlockwise
 * along the bottom), turned to the tangent. */
static float ang_of(const omni_wt_glyph_t *g) { return atan2f(g->y, g->x); }
static float rad_of(const omni_wt_glyph_t *g) { return sqrtf(g->x * g->x + g->y * g->y); }

void test_wt_glyphs_even_and_centred(void)
{
    const float adv[3] = { 7, 7, 7 };
    omni_wt_glyph_t g[3];
    const float a = -1.5707963f;   /* 12 o'clock */
    TEST_ASSERT_EQUAL_INT(3, omni_wt_glyphs(adv, 3, 1.0f, 182.0f, 3.0f, a, false, 233, 233, g));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, a, ang_of(&g[1]));                 /* the middle glyph on the label's angle */
    /* review I1: the screen point is the baseline centre itself, (233, 233 - 179), not moved by a pivot */
    TEST_ASSERT_EQUAL_INT32(233, g[1].px);
    TEST_ASSERT_EQUAL_INT32(54, g[1].py);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -8.0f / 182.0f, ang_of(&g[0]) - a); /* 7/2 + 7/2 + 1 = 8 px of arc */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 8.0f / 182.0f, ang_of(&g[2]) - a);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 179.0f, rad_of(&g[i]));        /* the baseline 3 px inside r */
        /* clockwise: the glyph's up is outward, so its rotation is the angle + 90 deg */
        float d = g[i].rot_deg - (ang_of(&g[i]) * 57.2957795f + 90.0f);
        d = remainderf(d, 360.0f);
        TEST_ASSERT_FLOAT_WITHIN(1e-2f, 0.0f, d);
    }
}

void test_wt_glyphs_uneven_and_flipped(void)
{
    const float adv[3] = { 6, 9, 5 };
    omni_wt_glyph_t g[3];
    const float a = 1.2f;   /* lower right: flipped text */
    TEST_ASSERT_EQUAL_INT(3, omni_wt_glyphs(adv, 3, 1.0f, 186.0f, 3.0f, a, true, 233, 233, g));
    /* reading order runs anticlockwise (angles fall): arc gaps (6+9)/2+1 = 8.5 and (9+5)/2+1 = 8 */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -8.5f / 186.0f, ang_of(&g[1]) - ang_of(&g[0]));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -8.0f / 186.0f, ang_of(&g[2]) - ang_of(&g[1]));
    /* centred: the run is 6 + 9 + 5 + 2 = 22 px; its middle (11 px in) is on the label's angle */
    const float s1 = 6.0f + 1.0f + 4.5f;   /* glyph 1's centre, px along the run */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, a - (s1 - 11.0f) / 186.0f, ang_of(&g[1]));
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 189.0f, rad_of(&g[i]));        /* the baseline 3 px outside r: tops inward */
        float d = remainderf(g[i].rot_deg - (ang_of(&g[i]) * 57.2957795f - 90.0f), 360.0f);
        TEST_ASSERT_FLOAT_WITHIN(1e-2f, 0.0f, d);
    }
    TEST_ASSERT_EQUAL_INT(0, omni_wt_glyphs(adv, 0, 1.0f, 186.0f, 3.0f, a, true, 233, 233, g));
    TEST_ASSERT_EQUAL_INT(0, omni_wt_glyphs(adv, OMNI_WT_GLYPHS + 1, 1.0f, 186.0f, 3.0f, a, true, 233, 233, g));
}

/* review I1: the point handed to lv_draw_letter, which LVGL 9.5 takes as the glyph's baseline centre (it moves the
 * glyph by its pivot (adv / 2, ascent) itself and turns it there): one glyph at 3 o'clock and one flipped at 6. */
void test_wt_glyph_screen_points(void)
{
    const float adv[1] = { 7 };
    omni_wt_glyph_t g[1];
    TEST_ASSERT_EQUAL_INT(1, omni_wt_glyphs(adv, 1, 1.0f, 182.0f, 3.0f, 0.0f, false, 233, 233, g));
    TEST_ASSERT_EQUAL_INT32(233 + 179, g[0].px);   /* baseline 3 px inside r 182, tops outward */
    TEST_ASSERT_EQUAL_INT32(233, g[0].py);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, g[0].rot_deg);
    TEST_ASSERT_EQUAL_INT(1, omni_wt_glyphs(adv, 1, 1.0f, 186.0f, 3.0f, 1.5707963f, true, 233, 233, g));
    TEST_ASSERT_EQUAL_INT32(233, g[0].px);
    TEST_ASSERT_EQUAL_INT32(233 + 189, g[0].py);   /* baseline 3 px outside r 186, tops inward: upright */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, g[0].rot_deg);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tz_transitions);
    RUN_TEST(test_tz_matches_libc_all_cities);
    RUN_TEST(test_tz_transitions_to_the_second_all_zones);
    RUN_TEST(test_tz_extreme_times);
    RUN_TEST(test_wt_list_columbus);
    RUN_TEST(test_wt_list_home_in_twelve);
    RUN_TEST(test_wt_list_twelve_cities);
    RUN_TEST(test_wt_list_home_code);
    RUN_TEST(test_wt_angle_flip);
    RUN_TEST(test_wt_hit);
    RUN_TEST(test_wt_hit_edges);
    RUN_TEST(test_wt_cartouche);
    RUN_TEST(test_wt_cartouche_unset);
    RUN_TEST(test_wt_glance_text);
    RUN_TEST(test_wt_reveal);
    RUN_TEST(test_wt_glyphs_even_and_centred);
    RUN_TEST(test_wt_glyphs_uneven_and_flipped);
    RUN_TEST(test_wt_glyph_screen_points);
    return UNITY_END();
}
