#include "unity.h"
#include "omni/trig.h"
#include <math.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

#define PI 3.14159265358979323846

static void test_sin_table(void)
{
    for (int i = 0; i < OMNI_TRIG_N + OMNI_TRIG_N / 4; i++) TEST_ASSERT_TRUE(fabs(OMNI_SIN_TAB[i] - sin(2 * PI * i / 1024)) < 1.2e-7);
    for (unsigned i = 0; i < 4096; i += 7) {
        TEST_ASSERT_TRUE(fabs(omni_sin_tab(i) - sin(2 * PI * (i & 1023) / 1024)) < 1.2e-7);
        TEST_ASSERT_TRUE(fabs(omni_cos_tab(i) - cos(2 * PI * (i & 1023) / 1024)) < 1.2e-7);
    }
}

/* 1/x in Q16 within 1 of floor(2^32 / x) over the domain x >= 0.5 (Q16 32768). */
static void test_recip_q16(void)
{
    uint32_t worst = 0;
    uint32_t s = 1;
    for (uint64_t x = 32768; x < (1ull << 32); x += x / 3001 + 1) {
        const uint32_t want = (uint32_t)((1ull << 32) / x), got = omni_recip_q16((uint32_t)x);
        const uint32_t d = got > want ? got - want : want - got;
        if (d > worst) worst = d;
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    }
    printf("recip_q16: worst %u ulp\n", (unsigned)worst);
    TEST_ASSERT_TRUE(worst <= 1);
    TEST_ASSERT_UINT32_WITHIN(1, 65536, omni_recip_q16(65536));
    TEST_ASSERT_UINT32_WITHIN(1, 131072, omni_recip_q16(32768));
}

/* Within 1 strip entry of atan2f at N 4096, over every half-pixel centre of the screen and the magnifier's scaled
 * points; the measured worst is pinned (entries at N 4096). */
#define ATAN_WORST_ENTRIES 0.008   /* measured 0.0078 (1.19e-5 rad) */
static void test_atan2_within_entry(void)
{
    double worst = 0;
    for (int y = 0; y < 466; y++)
        for (int x = 0; x < 466; x++)
            for (int k = 0; k < 2; k++) {
                const float sc = k ? 1 / 1.4f : 1;
                const float X = (x + 0.5f - 233) * sc, Y = (233 - y - 0.5f) * sc;
                double d = fabs((double)omni_atan2_tab(X, Y) - atan2f(X, Y));
                if (d > PI) d = 2 * PI - d;
                if (d > worst) worst = d;
            }
    const double entries = worst * 4096 / (2 * PI);
    printf("atan2_tab: worst %.3g rad = %.4f entries at N 4096\n", worst, entries);
    TEST_ASSERT_TRUE(entries <= 1.0);
    TEST_ASSERT_TRUE(entries <= ATAN_WORST_ENTRIES);
}

static void test_atan2_quadrants(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 0, omni_atan2_tab(0, 1));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, PI / 2, omni_atan2_tab(1, 0));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, PI, omni_atan2_tab(0, -1));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, -PI / 2, omni_atan2_tab(-1, 0));
    TEST_ASSERT_FLOAT_WITHIN(1e-5, -3 * PI / 4, omni_atan2_tab(-2.5f, -2.5f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5, PI / 4, omni_atan2_tab(0.5f, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(3e-5, atan2f(0.25f, 200.0f), omni_atan2_tab(0.25f, 200.0f));   /* 0.02 entries at 4096 */
}

/* T2 m12: outside the table's domain (|x| or |y| >= 256, both below 2^-16, NaN) the result is atan2f's, not a silent
 * 0 or an undefined float-to-int conversion. */
static void test_atan2_outside_domain(void)
{
    const float pts[][2] = { { 300, 400 }, { -1000, 3 }, { 5, -256 }, { 2e5f, -7e4f }, { 1e-6f, 3e-6f }, { -4e-6f, -1e-7f } };
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++)
        TEST_ASSERT_EQUAL_FLOAT(atan2f(pts[i][0], pts[i][1]), omni_atan2_tab(pts[i][0], pts[i][1]));
    TEST_ASSERT_TRUE(isnan(omni_atan2_tab(NAN, 1)));
    TEST_ASSERT_EQUAL_FLOAT(0, omni_atan2_tab(0, 0));
    TEST_ASSERT_FLOAT_WITHIN(3e-5f, atan2f(100, 255.9f), omni_atan2_tab(100, 255.9f));   /* still the table inside */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sin_table);
    RUN_TEST(test_recip_q16);
    RUN_TEST(test_atan2_within_entry);
    RUN_TEST(test_atan2_quadrants);
    RUN_TEST(test_atan2_outside_domain);
    return UNITY_END();
}
