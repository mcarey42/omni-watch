#include "unity.h"
#include "omni/almanac.h"
#include "omni/blend565.h"
#include "omni/timefmt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EST "EST5EDT,M3.2.0,M11.1.0"
#define LAT 39.96
#define LON (-83.00)
#define TAU 6.28318530717958647692
#define C 219.0                         /* the ring centre in box px (233 - 14) */
#define NPIX (OMNI_ALM_BOX * OMNI_ALM_BOX)

void setUp(void) { omni_tz_apply(EST); }
void tearDown(void) {}

static uint16_t g_box[NPIX];

static int64_t utc(int y, int m, int d, int hh, int mm)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (era * 146097 + doe - 719468) * 86400 + hh * 3600 + mm * 60;
}

/* the box pixel at radius r, hour h on the ring (hourAngle(h) = pi/2 + h/24 tau, canvas radians) */
static uint16_t at(double h, double r)
{
    const double a = TAU / 4 + h / 24 * TAU;
    const int x = (int)floor(C + r * cos(a)), y = (int)floor(C + r * sin(a));
    return g_box[y * OMNI_ALM_BOX + x];
}
static int lum(uint16_t c) { return (omni_ex_r(c) + omni_ex_g(c) + omni_ex_b(c)) / 3; }

/* the sky colour at an instant, through RGB565 as the ring stores it */
static void sky565(int64_t t, double lat, double lon, int out[3])
{
    double alt, az;
    uint8_t c[3];
    omni_alm_altaz(t, lat, lon, &alt, &az);
    omni_alm_sky_rgb(alt, c);
    const uint16_t q = omni_pack565(c[0], c[1], c[2]);
    out[0] = omni_ex_r(q); out[1] = omni_ex_g(q); out[2] = omni_ex_b(q);
}
static void near(uint16_t px, const int want[3], int tol)
{
    TEST_ASSERT_INT_WITHIN(tol, want[0], omni_ex_r(px));
    TEST_ASSERT_INT_WITHIN(tol, want[1], omni_ex_g(px));
    TEST_ASSERT_INT_WITHIN(tol, want[2], omni_ex_b(px));
}

void test_alm_ring_noon_midnight(void)
{
    const int64_t day0 = utc(2026, 6, 21, 4, 0);   /* local midnight, EDT */
    int want[3];
    omni_alm_ring_render(g_box, day0, LAT, LON, -1, -1, true);
    sky565(day0 + 12 * 3600 + 15 * 60, LAT, LON, want);
    near(at(12.25, 202), want, 8);
    sky565(day0 + 15 * 60, LAT, LON, want);
    near(at(0.25, 202), want, 8);
    /* the ring is an annulus: black in the middle and outside it */
    TEST_ASSERT_EQUAL_HEX16(0, g_box[(int)C * OMNI_ALM_BOX + (int)C]);
    TEST_ASSERT_EQUAL_HEX16(0, g_box[0]);
    TEST_ASSERT_EQUAL_HEX16(0, at(12.25, 160));
    TEST_ASSERT_EQUAL_HEX16(0, at(12.25, 217));
}

void test_alm_ring_unset(void)
{
    int night[3];
    uint8_t c[3];
    omni_alm_sky_rgb(-90, c);
    const uint16_t q = omni_pack565(c[0], c[1], c[2]);
    night[0] = omni_ex_r(q); night[1] = omni_ex_g(q); night[2] = omni_ex_b(q);
    memset(g_box, 0x5a, sizeof g_box);   /* every pixel is written */
    omni_alm_ring_render(g_box, utc(2026, 6, 21, 4, 0), LAT, LON, 6.1f, 20.5f, false);
    near(at(12.25, 202), night, 3);
    near(at(18.6, 202), night, 3);
    for (int i = 0; i < NPIX; i++) {
        const int r = omni_ex_r(g_box[i]), g = omni_ex_g(g_box[i]), b = omni_ex_b(g_box[i]);
        TEST_ASSERT_FALSE(abs(r - 236) <= 10 && abs(g - 210) <= 10 && abs(b - 154) <= 10);
    }
}

void test_alm_ring_dst_day(void)
{
    /* 2026-03-08: the clock jumps at 02:00 (07:00Z). Local midnight is 05:00Z; 12:15 EDT is 16:15Z. */
    const int64_t day0 = utc(2026, 3, 8, 5, 0);
    int want[3], wrong[3];
    omni_alm_ring_render(g_box, day0, LAT, LON, -1, -1, true);
    sky565(utc(2026, 3, 8, 16, 15), LAT, LON, want);
    near(at(12.25, 202), want, 8);
    /* a steep part of the sky (the twilight before sunrise): 07:29 on the dial is the bin of 07:15 EDT (11:15Z). Read
     * as 24 h from midnight it would be the colour of 06:15 EDT, an hour earlier, which differs by far more than 8 */
    sky565(utc(2026, 3, 8, 11, 15), LAT, LON, want);
    sky565(utc(2026, 3, 8, 11, 15) - 3600, LAT, LON, wrong);
    TEST_ASSERT_TRUE(abs(want[2] - wrong[2]) > 20);
    near(at(7.29, 202), want, 8);
    /* 03:29 is past the gap: its bin is 03:25 EDT, 07:25Z */
    sky565(utc(2026, 3, 8, 7, 25), LAT, LON, want);
    near(at(3.46, 202), want, 8);
    /* and the fall-back day, 2026-11-01 (25 h): 12:15 EST is 17:15Z, local midnight 04:00Z */
    omni_alm_ring_render(g_box, utc(2026, 11, 1, 4, 0), LAT, LON, -1, -1, true);
    sky565(utc(2026, 11, 1, 17, 15), LAT, LON, want);
    near(at(12.25, 202), want, 8);
    sky565(utc(2026, 11, 1, 11, 45), LAT, LON, want);   /* 06:45 EST */
    sky565(utc(2026, 11, 1, 11, 45) + 3600, LAT, LON, wrong);
    TEST_ASSERT_TRUE(abs(want[2] - wrong[2]) > 20);
    near(at(6.79, 202), want, 8);
}

void test_alm_ring_ticks(void)
{
    omni_alm_ring_render(g_box, utc(2026, 6, 21, 4, 0), LAT, LON, 6.1f, -1, true);
    const uint16_t t = at(6.1, 216);
    TEST_ASSERT_INT_WITHIN(10, 236, omni_ex_r(t));
    TEST_ASSERT_INT_WITHIN(10, 210, omni_ex_g(t));
    TEST_ASSERT_INT_WITHIN(10, 154, omni_ex_b(t));
    /* the rise tick reaches 186 and stops at 218 */
    TEST_ASSERT_TRUE(lum(at(6.1, 187)) > 120);
    TEST_ASSERT_TRUE(lum(at(6.1, 222)) == 0);
    /* a major tick (hour 6) is darker than the sky beside it; a minor tick (hour 7) shows only to r 197 */
    omni_alm_ring_render(g_box, utc(2026, 6, 21, 4, 0), LAT, LON, -1, -1, true);
    TEST_ASSERT_TRUE(lum(at(6, 205)) < lum(at(5.8, 205)));
    TEST_ASSERT_TRUE(lum(at(6, 205)) < lum(at(6.2, 205)));
    TEST_ASSERT_TRUE(lum(at(7, 193)) < lum(at(7.2, 193)));
    TEST_ASSERT_TRUE(lum(at(7, 205)) >= lum(at(7.2, 205)) - 4);   /* no tick out there */
    /* the brass hairlines, at the band's edges */
    const uint16_t hl = g_box[(int)(C + 189.2) * OMNI_ALM_BOX + (int)C];
    TEST_ASSERT_TRUE(omni_ex_r(hl) > omni_ex_b(hl) + 20);
}

void test_alm_ring_digest(void)
{
    omni_alm_ring_render(g_box, utc(2026, 6, 21, 4, 0), LAT, LON, 6.25f, 20.9f, true);
    uint64_t h = 1469598103934665603ull;
    const uint8_t *p = (const uint8_t *)g_box;
    for (size_t i = 0; i < sizeof g_box; i++) h = (h ^ p[i]) * 1099511628211ull;
    printf("alm_ring digest %016llx\n", (unsigned long long)h);
    TEST_ASSERT_EQUAL_HEX64(0xc6a267814fa4cacaull, h);   /* host regression pin, set on the first green run */
}

/* The gap and overlap hours, pinned (m2). A TZ whose transitions fall in the morning twilight makes them visible:
 * EST5EDT,M3.2.0/6,M11.1.0/8 jumps 06:00 EST -> 07:00 EDT (gap 06:xx) and 08:00 EDT -> 07:00 EST (overlap 07:xx). */
void test_alm_ring_gap_overlap(void)
{
    omni_tz_apply("EST5EDT,M3.2.0/6,M11.1.0/8");
    int want[3], other[3];
    /* spring: the clock never reads 06:30; the sample uses the offset before the jump, 06:30 EST = 11:30Z */
    omni_alm_ring_render(g_box, utc(2026, 3, 8, 5, 0), LAT, LON, -1, -1, true);
    sky565(utc(2026, 3, 8, 11, 30), LAT, LON, want);
    sky565(utc(2026, 3, 8, 10, 30), LAT, LON, other);   /* the other reading, 06:30 EDT */
    TEST_ASSERT_TRUE(abs(want[2] - other[2]) > 20);
    near(at(6.54, 202), want, 8);
    sky565(utc(2026, 3, 8, 11, 25), LAT, LON, want);    /* 07:25 EDT, after the jump */
    near(at(7.46, 202), want, 8);                /* bin of 07:25 (centre 7.46) */
    /* fall: the clock reads 07:30 twice; the sample is the first, 07:30 EDT = 11:30Z, not 07:30 EST = 12:30Z */
    omni_alm_ring_render(g_box, utc(2026, 11, 1, 4, 0), LAT, LON, -1, -1, true);
    sky565(utc(2026, 11, 1, 11, 30), LAT, LON, want);
    sky565(utc(2026, 11, 1, 12, 30), LAT, LON, other);
    TEST_ASSERT_TRUE(abs(want[2] - other[2]) > 20);
    near(at(7.54, 202), want, 8);
    sky565(utc(2026, 11, 1, 13, 25), LAT, LON, want);   /* 08:25 EST, after the overlap */
    near(at(8.46, 202), want, 8);
}

/* n3: the same rules in a southern, half-hour zone (Adelaide, ACST-9:30ACDT; the change hours moved into dawn twilight
 * so a wrong reading shows), where DST starts in October and ends in April. */
void test_alm_ring_gap_overlap_south(void)
{
    omni_tz_apply("ACST-9:30ACDT,M10.1.0/5,M4.1.0/7");
    const double lat = -34.93, lon = 138.60;
    int want[3], other[3];
    /* spring, Sun 4 Oct: the clock never reads 05:30; the sample uses ACST, 05:30 ACST = 20:00Z on the 3rd */
    omni_alm_ring_render(g_box, utc(2026, 10, 3, 15, 0), lat, lon, -1, -1, true);
    sky565(utc(2026, 10, 3, 20, 0), lat, lon, want);
    sky565(utc(2026, 10, 3, 19, 0), lat, lon, other);   /* the other reading, 05:30 ACDT */
    TEST_ASSERT_TRUE(abs(want[2] - other[2]) > 20);
    near(at(5.54, 202), want, 8);
    sky565(utc(2026, 10, 3, 19, 55), lat, lon, want);   /* 06:25 ACDT, after the jump */
    near(at(6.46, 202), want, 8);
    /* fall, Sun 5 Apr: the clock reads 06:30 twice; the sample is the first, 06:30 ACDT = 20:00Z on the 4th */
    omni_alm_ring_render(g_box, utc(2026, 4, 4, 15, 0), lat, lon, -1, -1, true);
    sky565(utc(2026, 4, 4, 20, 0), lat, lon, want);
    sky565(utc(2026, 4, 4, 21, 0), lat, lon, other);    /* 06:30 ACST: sunrise's red, against twilight's dark */
    TEST_ASSERT_TRUE(abs(want[0] - other[0]) > 20);
    near(at(6.54, 202), want, 8);
    sky565(utc(2026, 4, 4, 21, 55), lat, lon, want);    /* 07:25 ACST, after the overlap */
    near(at(7.46, 202), want, 8);
}

static uint16_t moon_px(const uint8_t *m, int d, int x, int y) { return ((const uint16_t *)(const void *)m)[y * d + x]; }
static uint8_t moon_a(const uint8_t *m, int d, int x, int y) { return m[d * d * 2 + y * d + x]; }

#define MD 88
static uint16_t g_moon16[MD * MD * 3 / 2 + 2];

void test_alm_moon_render(void)
{
    uint8_t *m = (uint8_t *)g_moon16;
    omni_alm_moon_render(m, MD, 0.5);
    TEST_ASSERT_TRUE(lum(moon_px(m, MD, MD / 2, MD / 2)) > 150);
    TEST_ASSERT_EQUAL_UINT8(0, moon_a(m, MD, 0, 0));
    TEST_ASSERT_EQUAL_UINT8(0, moon_a(m, MD, MD - 1, MD - 1));
    TEST_ASSERT_EQUAL_UINT8(255, moon_a(m, MD, MD / 2, MD / 2));
    omni_alm_moon_render(m, MD, 0.0);
    TEST_ASSERT_TRUE(lum(moon_px(m, MD, MD / 2, MD / 2)) < 30);
    TEST_ASSERT_EQUAL_UINT8(255, moon_a(m, MD, MD / 2, MD / 2));
    omni_alm_moon_render(m, MD, 0.25);   /* a waxing moon is lit on the right */
    long l = 0, r = 0;
    for (int y = 0; y < MD; y++)
        for (int x = 0; x < MD / 2; x++) {
            l += lum(moon_px(m, MD, x, y));
            r += lum(moon_px(m, MD, MD - 1 - x, y));
        }
    TEST_ASSERT_TRUE(r > 3 * l);
    omni_alm_moon_render(m, MD, 0.75);   /* and a waning one on the left */
    l = r = 0;
    for (int y = 0; y < MD; y++)
        for (int x = 0; x < MD / 2; x++) {
            l += lum(moon_px(m, MD, x, y));
            r += lum(moon_px(m, MD, MD - 1 - x, y));
        }
    TEST_ASSERT_TRUE(l > 3 * r);
    /* the limb is anti-aliased: some edge pixels are partly transparent */
    int partial = 0;
    for (int i = 0; i < MD * MD; i++) if (m[MD * MD * 2 + i] > 0 && m[MD * MD * 2 + i] < 255) partial++;
    TEST_ASSERT_TRUE(partial > 100);
    /* the digest of the full moon is pinned */
    omni_alm_moon_render(m, MD, 0.5);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < (size_t)MD * MD * 3; i++) h = (h ^ m[i]) * 1099511628211ull;
    printf("alm_moon digest %016llx\n", (unsigned long long)h);
    TEST_ASSERT_EQUAL_HEX64(0x9c98c4117d428c16ull, h);   /* pinned on the first green run (no fused multiply-add) */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_alm_ring_noon_midnight);
    RUN_TEST(test_alm_ring_unset);
    RUN_TEST(test_alm_ring_dst_day);
    RUN_TEST(test_alm_ring_gap_overlap);
    RUN_TEST(test_alm_ring_gap_overlap_south);
    RUN_TEST(test_alm_ring_ticks);
    RUN_TEST(test_alm_moon_render);
    RUN_TEST(test_alm_ring_digest);
    return UNITY_END();
}
