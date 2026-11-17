#include "unity.h"
#include "omni/almanac.h"
#include "omni/timefmt.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define EST "EST5EDT,M3.2.0,M11.1.0"
#define CET "CET-1CEST,M3.5.0,M10.5.0/3"
#define LAT 39.96
#define LON (-83.00)
#define TLAT 69.65
#define TLON 18.96

void setUp(void) { omni_tz_apply(EST); }
void tearDown(void) {}

/* the unix time of a UTC civil date and time */
static int64_t utc(int y, int m, int d, int hh, int mm)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (era * 146097 + doe - 719468) * 86400 + hh * 3600 + mm * 60;
}

void test_alm_altaz_noon(void)
{
    int64_t rise = 0, set = 0;
    TEST_ASSERT_EQUAL(SUN_NORMAL, omni_sun_rise_set(utc(2026, 6, 21, 12, 0), LAT, LON, &rise, &set));
    double alt, az;
    omni_alm_altaz((rise + set) / 2, LAT, LON, &alt, &az);
    TEST_ASSERT_FLOAT_WITHIN(0.4, 73.5, alt);
    TEST_ASSERT_FLOAT_WITHIN(1.5, 180.0, az);
    /* the subsolar point of any instant: the sun is overhead */
    const int64_t when[] = { utc(2026, 3, 20, 5, 17), utc(2026, 9, 1, 23, 59), utc(2027, 1, 3, 11, 11) };
    for (size_t i = 0; i < sizeof when / sizeof when[0]; i++) {
        omni_sun_t s;
        omni_sun_at(when[i], &s);
        omni_alm_altaz(when[i], s.subsolar_lat, s.subsolar_lon, &alt, &az);
        TEST_ASSERT_TRUE(alt > 89.5);
    }
}

static void check_rgb(int r, int g, int b, double alt)
{
    uint8_t c[3];
    omni_alm_sky_rgb(alt, c);
    TEST_ASSERT_INT_WITHIN(1, r, c[0]);
    TEST_ASSERT_INT_WITHIN(1, g, c[1]);
    TEST_ASSERT_INT_WITHIN(1, b, c[2]);
}

void test_alm_sky_rgb(void)
{
    check_rgb(6, 8, 16, -90);
    check_rgb(8, 11, 24, -18);
    check_rgb(16, 26, 52, -12);
    check_rgb(34, 56, 96, -6);
    check_rgb(120, 92, 60, -0.8);
    check_rgb(150, 114, 58, 6);
    check_rgb(110, 98, 72, 20);
    check_rgb(118, 106, 80, 90);
    check_rgb(12, 18, 38, -15);   /* halfway between the -18 and -12 stops */
    check_rgb(6, 8, 16, -123);    /* beyond the ends: the end stops */
    check_rgb(118, 106, 80, 95);
    /* exact stops come back exactly */
    uint8_t c[3];
    omni_alm_sky_rgb(-90, c);
    TEST_ASSERT_EQUAL_UINT8(6, c[0]);
    omni_alm_sky_rgb(-12, c);
    TEST_ASSERT_EQUAL_UINT8(16, c[0]);
    TEST_ASSERT_EQUAL_UINT8(26, c[1]);
    TEST_ASSERT_EQUAL_UINT8(52, c[2]);
    omni_alm_sky_rgb(90, c);
    TEST_ASSERT_EQUAL_UINT8(118, c[0]);
    TEST_ASSERT_EQUAL_UINT8(106, c[1]);
    TEST_ASSERT_EQUAL_UINT8(80, c[2]);
}

void test_alm_day_summer(void)
{
    omni_alm_day_t d;
    omni_alm_day(utc(2026, 6, 21, 17, 0), LAT, LON, &d);
    TEST_ASSERT_EQUAL(SUN_NORMAL, d.kind);
    TEST_ASSERT_EQUAL_INT32((int32_t)(d.set - d.rise), d.len_s);
    TEST_ASSERT_TRUE(d.len_s >= 14 * 3600 + 50 * 60 && d.len_s <= 15 * 3600 + 5 * 60);
    TEST_ASSERT_TRUE(d.delta_s >= -20 && d.delta_s <= 20);
    TEST_ASSERT_INT64_WITHIN(1, (d.rise + d.set) / 2, d.noon);
}

void test_alm_day_delta_sign(void)
{
    omni_alm_day_t d;
    omni_alm_day(utc(2026, 3, 20, 17, 0), LAT, LON, &d);
    TEST_ASSERT_TRUE(d.delta_s > 100);
    omni_alm_day(utc(2026, 9, 23, 17, 0), LAT, LON, &d);
    TEST_ASSERT_TRUE(d.delta_s < -100);
}

static void check_dst_day(int64_t noon_utc, int mon, int mday)
{
    omni_alm_day_t d, p, n;
    omni_alm_day(noon_utc, LAT, LON, &d);
    omni_alm_day(noon_utc - 86400, LAT, LON, &p);
    omni_alm_day(noon_utc + 86400, LAT, LON, &n);
    TEST_ASSERT_EQUAL(SUN_NORMAL, d.kind);
    time_t t = (time_t)d.rise;
    struct tm tm;
    TEST_ASSERT_NOT_NULL(localtime_r(&t, &tm));
    TEST_ASSERT_EQUAL_INT(mon, tm.tm_mon + 1);
    TEST_ASSERT_EQUAL_INT(mday, tm.tm_mday);
    TEST_ASSERT_INT_WITHIN(300, p.len_s, d.len_s);
    TEST_ASSERT_INT_WITHIN(300, n.len_s, d.len_s);
    TEST_ASSERT_TRUE(d.delta_s >= -300 && d.delta_s <= 300);
}

void test_alm_day_dst(void)
{
    check_dst_day(utc(2026, 3, 8, 17, 0), 3, 8);    /* spring forward: a 23 h day */
    check_dst_day(utc(2026, 11, 1, 17, 0), 11, 1);  /* fall back: 25 h */
    /* the same local day, asked at its first and its last minute */
    omni_alm_day_t a, b;
    omni_alm_day(utc(2026, 3, 8, 5, 5), LAT, LON, &a);     /* 00:05 EST */
    omni_alm_day(utc(2026, 3, 9, 3, 55), LAT, LON, &b);    /* 23:55 EDT */
    TEST_ASSERT_EQUAL_INT64(a.rise, b.rise);
    TEST_ASSERT_EQUAL_INT64(a.set, b.set);
}

void test_alm_polar_day(void)
{
    omni_tz_apply(CET);
    omni_alm_day_t d;
    char t[12];
    omni_alm_day(utc(2026, 6, 21, 10, 0), TLAT, TLON, &d);
    TEST_ASSERT_EQUAL(SUN_ALWAYS_UP, d.kind);
    TEST_ASSERT_EQUAL_INT32(86400, d.len_s);
    TEST_ASSERT_EQUAL_INT32(0, d.delta_s);
    omni_alm_daylen_text(&d, true, t);
    TEST_ASSERT_EQUAL_STRING("24h  00m", t);
}

void test_alm_polar_night(void)
{
    omni_tz_apply(CET);
    omni_alm_day_t d;
    char t[12];
    omni_alm_day(utc(2026, 12, 21, 10, 0), TLAT, TLON, &d);
    TEST_ASSERT_EQUAL(SUN_ALWAYS_DOWN, d.kind);
    TEST_ASSERT_EQUAL_INT32(0, d.len_s);
    omni_alm_daylen_text(&d, true, t);
    TEST_ASSERT_EQUAL_STRING("0h  00m", t);
    /* the polar transition: the day the sun first rises again has a positive delta from zero */
    omni_alm_day(utc(2027, 1, 20, 10, 0), TLAT, TLON, &d);
    TEST_ASSERT_EQUAL(SUN_NORMAL, d.kind);
    TEST_ASSERT_TRUE(d.delta_s > 0);
}

/* the four published phase times, checked against USNO's API (aa.usno.navy.mil/api/moon/phases/year, 2026-11-01) */
void test_alm_moon_phases(void)
{
    TEST_ASSERT_INT64_WITHIN(600, utc(2024, 1, 11, 11, 57), omni_alm_phase_unix(297));
    TEST_ASSERT_INT64_WITHIN(600, utc(2024, 1, 25, 17, 54), omni_alm_phase_unix(297.5));
    TEST_ASSERT_INT64_WITHIN(600, utc(2025, 1, 13, 22, 27), omni_alm_phase_unix(309.5));
    TEST_ASSERT_INT64_WITHIN(600, utc(2025, 1, 29, 12, 36), omni_alm_phase_unix(310));
    /* a lunation is about 29.5 days, new to full about half */
    const int64_t n = omni_alm_phase_unix(297), f = omni_alm_phase_unix(297.5), n2 = omni_alm_phase_unix(298);
    TEST_ASSERT_TRUE(f - n > 13 * 86400 && f - n < 16 * 86400);
    TEST_ASSERT_TRUE(n2 - f > 13 * 86400 && n2 - f < 16 * 86400);
}

void test_alm_moon_state(void)
{
    omni_alm_moon_t m;
    omni_alm_moon(utc(2024, 1, 25, 17, 54), &m);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.5, m.f);
    TEST_ASSERT_TRUE(m.illum > 0.99);
    TEST_ASSERT_EQUAL_STRING("Full moon", omni_alm_moon_name(m.name));
    omni_alm_moon(utc(2024, 1, 14, 11, 57), &m);   /* 3 days after new: f ~ 0.10 */
    TEST_ASSERT_FLOAT_WITHIN(0.02, 0.10, m.f);
    TEST_ASSERT_EQUAL_STRING("Waxing crescent", omni_alm_moon_name(m.name));
    omni_alm_moon(utc(2024, 1, 20, 0, 0), &m);
    TEST_ASSERT_INT64_WITHIN(600, utc(2024, 1, 25, 17, 54), m.next_full);
    TEST_ASSERT_TRUE(m.next_new > m.next_full);
    TEST_ASSERT_INT64_WITHIN(600, utc(2024, 2, 9, 22, 59), m.next_new);
    /* after full: f above a half, the next full is a lunation on */
    omni_alm_moon(utc(2024, 1, 28, 0, 0), &m);
    TEST_ASSERT_TRUE(m.f > 0.5 && m.f < 0.7);
    TEST_ASSERT_TRUE(m.next_full > utc(2024, 2, 20, 0, 0));
    TEST_ASSERT_INT64_WITHIN(600, utc(2024, 2, 9, 22, 59), m.next_new);
    /* f never jumps backwards or by more than a day's worth across a month, and stays in [0, 1) */
    double last = -1;
    for (int64_t t = utc(2024, 1, 1, 0, 0); t < utc(2024, 3, 1, 0, 0); t += 3600) {
        omni_alm_moon(t, &m);
        TEST_ASSERT_TRUE(m.f >= 0 && m.f < 1);
        if (last >= 0 && m.f < last) TEST_ASSERT_TRUE(last > 0.97 && m.f < 0.03);   /* only the wrap at new moon */
        else if (last >= 0) TEST_ASSERT_TRUE(m.f - last < 0.01);
        last = m.f;
    }
    TEST_ASSERT_EQUAL_STRING("New moon", omni_alm_moon_name(0));
    TEST_ASSERT_EQUAL_STRING("Waning crescent", omni_alm_moon_name(7));
    TEST_ASSERT_EQUAL_STRING("Full moon", omni_alm_moon_name(12));   /* taken mod 8 */
}

void test_alm_texts(void)
{
    char s[40];
    omni_alm_day_t d = { SUN_NORMAL, 0, 0, 0, 32520, 118 };
    omni_alm_daylen_text(&d, true, s);
    TEST_ASSERT_EQUAL_STRING("9h  02m", s);
    omni_alm_delta_text(&d, true, s);
    TEST_ASSERT_EQUAL_STRING("+1m 58s vs yesterday", s);
    d.delta_s = -7;
    omni_alm_delta_text(&d, true, s);
    TEST_ASSERT_EQUAL_STRING("-0m 07s vs yesterday", s);
    d.delta_s = 0;
    omni_alm_delta_text(&d, true, s);
    TEST_ASSERT_EQUAL_STRING("+0m 00s vs yesterday", s);
    omni_alm_alt_text(-12.4, s);
    TEST_ASSERT_EQUAL_STRING("\xe2\x88\x92" "12\xc2\xb0", s);
    omni_alm_alt_text(34.4, s);
    TEST_ASSERT_EQUAL_STRING("34\xc2\xb0", s);
    omni_alm_alt_text(-0.3, s);
    TEST_ASSERT_EQUAL_STRING("0\xc2\xb0", s);   /* no minus zero */
    omni_alm_moon_t m = { 0.3, 0.62, 3, utc(2026, 11, 25, 17, 0), utc(2026, 12, 10, 12, 0) };
    omni_alm_moon_text(&m, s);
    TEST_ASSERT_EQUAL_STRING("62% \xc2\xb7 Full Wed 25 Nov", s);
    m.f = 0.7;
    omni_alm_moon_text(&m, s);
    TEST_ASSERT_EQUAL_STRING("62% \xc2\xb7 New Thu 10 Dec", s);
}

/* the first minute of 2026-06-21 (UTC, from 12:00) at which pred holds */
typedef bool (*pred_t)(double alt, double az);
static bool alt_235(double alt, double az) { (void)az; return alt >= 23.45 && alt < 23.55; }
static bool az_212(double alt, double az) { (void)alt; return az >= 211.6 && az < 212.4; }
static int64_t find_sun(pred_t p)
{
    for (int64_t t = utc(2026, 6, 21, 12, 0); t < utc(2026, 6, 22, 0, 0); t += 30) {
        double alt, az;
        omni_alm_altaz(t, LAT, LON, &alt, &az);
        if (p(alt, az)) return t;
    }
    return 0;
}

void test_alm_sun_lines(void)
{
    char az[24], noon[24], shadow[32];
    int64_t t = find_sun(alt_235);
    TEST_ASSERT_NOT_EQUAL(0, t);
    omni_alm_sun_lines(t, LAT, LON, false, az, noon, shadow);
    TEST_ASSERT_EQUAL_STRING("1 m stick casts 2.3 m", shadow);
    t = find_sun(az_212);
    TEST_ASSERT_NOT_EQUAL(0, t);
    omni_alm_sun_lines(t, LAT, LON, false, az, noon, shadow);
    TEST_ASSERT_EQUAL_STRING("azimuth 212\xc2\xb0 SW", az);
    /* solar noon: the local date's, in 12 h and 24 h; at Columbus in June it is 13:33 EDT */
    TEST_ASSERT_EQUAL_STRING("solar noon 1:33p", noon);
    omni_alm_sun_lines(t, LAT, LON, true, az, noon, shadow);
    TEST_ASSERT_EQUAL_STRING("solar noon 13:33", noon);
    /* it agrees with the day's rise/set midpoint */
    omni_alm_day_t d;
    char hm[8], want[24];
    omni_alm_day(t, LAT, LON, &d);
    omni_fmt_short_time(d.noon, true, hm);
    snprintf(want, sizeof want, "solar noon %s", hm);
    TEST_ASSERT_EQUAL_STRING(want, noon);
    /* night: the sun is below the horizon */
    omni_alm_sun_lines(utc(2026, 6, 21, 6, 0), LAT, LON, true, az, noon, shadow);
    TEST_ASSERT_EQUAL_STRING("sun below horizon", shadow);
    /* the compass points: the first half minute at which the sun is past each azimuth */
    const struct { double a; const char *tail; } dirs[] = { { 90, " E" }, { 135, " SE" }, { 180, " S" },
                                                            { 225, " SW" }, { 270, " W" } };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        int64_t tt = utc(2026, 6, 21, 12, 0);
        double alt, a;
        do { tt += 30; omni_alm_altaz(tt, LAT, LON, &alt, &a); } while (a < dirs[i].a && tt < utc(2026, 6, 22, 0, 0));
        omni_alm_sun_lines(tt, LAT, LON, true, az, noon, shadow);
        TEST_ASSERT_NOT_NULL(strstr(az, "\xc2\xb0"));
        TEST_ASSERT_EQUAL_STRING(dirs[i].tail, strstr(az, "\xc2\xb0") + 2);
    }
}

void test_alm_polar_sun_lines(void)
{
    omni_tz_apply(CET);
    char az[24], noon[24], shadow[32];
    omni_alm_sun_lines(utc(2026, 12, 21, 10, 0), TLAT, TLON, true, az, noon, shadow);
    TEST_ASSERT_EQUAL_STRING("sun below horizon", shadow);
    TEST_ASSERT_EQUAL_MEMORY("solar noon ", noon, 11);   /* computed even where the sun does not rise */
}

void test_alm_unset_texts(void)
{
    char s[40];
    omni_alm_day_t d = { SUN_NORMAL, 0, 0, 0, 32520, 118 };
    omni_alm_daylen_text(&d, false, s);
    TEST_ASSERT_EQUAL_STRING("--h  --m", s);
    omni_alm_delta_text(&d, false, s);
    TEST_ASSERT_EQUAL_STRING("", s);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_alm_altaz_noon);
    RUN_TEST(test_alm_sky_rgb);
    RUN_TEST(test_alm_day_summer);
    RUN_TEST(test_alm_day_delta_sign);
    RUN_TEST(test_alm_day_dst);
    RUN_TEST(test_alm_polar_day);
    RUN_TEST(test_alm_polar_night);
    RUN_TEST(test_alm_moon_phases);
    RUN_TEST(test_alm_moon_state);
    RUN_TEST(test_alm_texts);
    RUN_TEST(test_alm_sun_lines);
    RUN_TEST(test_alm_polar_sun_lines);
    RUN_TEST(test_alm_unset_texts);
    return UNITY_END();
}
