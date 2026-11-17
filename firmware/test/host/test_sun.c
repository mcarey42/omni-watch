#include "unity.h"
#include "omni/sun.h"

void setUp(void) {}
void tearDown(void) {}

#define COL_LAT 39.96
#define COL_LON (-83.0)
#define TOL_S (3 * 60)

/* Unix seconds at 12:00 UTC (the rise/set input) and the reference instants. */
#define OCT22_NOON 1792670400LL
#define JUN21_NOON 1782043200LL
#define DEC21_NOON 1797854400LL
#define MAR20_NOON 1774008000LL

static void check_day(int64_t noon, double decl, int64_t rise_ref, int64_t set_ref)
{
    omni_sun_t s;
    omni_sun_at(noon, &s);
    TEST_ASSERT_FLOAT_WITHIN(0.05, decl, s.decl_deg);
    int64_t rise = 0, set = 0;
    TEST_ASSERT_EQUAL(SUN_NORMAL, omni_sun_rise_set(noon, COL_LAT, COL_LON, &rise, &set));
    TEST_ASSERT_INT64_WITHIN(TOL_S, rise_ref, rise);
    TEST_ASSERT_INT64_WITHIN(TOL_S, set_ref, set);
}

/* R47: external reference, Meeus ch. 25 (controller-computed), 2026-10-22. */
static void test_decl_reference(void)
{
    omni_sun_t s;
    omni_sun_at(OCT22_NOON - 12 * 3600, &s);
    TEST_ASSERT_FLOAT_WITHIN(0.05, -10.978, s.decl_deg);
    omni_sun_at(OCT22_NOON, &s);
    TEST_ASSERT_FLOAT_WITHIN(0.05, -11.154, s.decl_deg);
}

/* Values of the mockup sunPos (design/mockups/faces-practical.js) at 12:00 UTC. */
static void test_sunpos_matches_mockup(void)
{
    omni_sun_t s;
    omni_sun_at(OCT22_NOON, &s);
    TEST_ASSERT_FLOAT_WITHIN(0.02, -11.151, s.decl_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.05, 15.539, s.eot_min);
    TEST_ASSERT_FLOAT_WITHIN(0.05, -3.885, s.subsolar_lon);
    omni_sun_at(JUN21_NOON, &s);
    TEST_ASSERT_FLOAT_WITHIN(0.02, 23.435, s.decl_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.05, -1.827, s.eot_min);
    omni_sun_at(DEC21_NOON, &s);
    TEST_ASSERT_FLOAT_WITHIN(0.02, -23.435, s.decl_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.05, 1.937, s.eot_min);
    omni_sun_at(MAR20_NOON, &s);
    TEST_ASSERT_FLOAT_WITHIN(0.05, 1.866, s.subsolar_lon);
}

static void test_light_unset_is_studio(void)
{
    omni_sun_t s;
    TEST_ASSERT_FALSE(omni_sun_light(0, &s));
    TEST_ASSERT_TRUE(omni_sun_light(OCT22_NOON, &s));
    TEST_ASSERT_FLOAT_WITHIN(0.02, -11.151, s.decl_deg);
}

static void test_sun_columbus_oct22(void)
{
    omni_sun_t s;
    omni_sun_at(OCT22_NOON, &s);
    /* R47: decl and EoT follow the Meeus-checked formula (decl -11.15, EoT 15.54), not the NOAA fit. */
    TEST_ASSERT_FLOAT_WITHIN(0.05, 15.54, s.eot_min);
    check_day(OCT22_NOON, -11.15, 1792669740LL, 1792709040LL);   /* 11:49, 22:44 */
}

static void test_sun_columbus_jun21(void)
{
    check_day(JUN21_NOON, 23.45, 1782036180LL, 1782090240LL);    /* 10:03, 01:04 +1d */
}

static void test_sun_columbus_dec21(void)
{
    check_day(DEC21_NOON, -23.42, 1797857400LL, 1797891000LL);   /* 12:50, 22:10 */
}

static void test_sun_polar_night(void)
{
    int64_t r, s, when;
    bool rise;
    TEST_ASSERT_EQUAL(SUN_ALWAYS_DOWN, omni_sun_rise_set(DEC21_NOON, 78.22, 15.65, &r, &s));
    TEST_ASSERT_FALSE(omni_sun_next_event(DEC21_NOON, 78.22, 15.65, &when, &rise));
}

static void test_sun_polar_day(void)
{
    int64_t r, s, when;
    bool rise;
    TEST_ASSERT_EQUAL(SUN_ALWAYS_UP, omni_sun_rise_set(JUN21_NOON, 78.22, 15.65, &r, &s));
    TEST_ASSERT_FALSE(omni_sun_next_event(JUN21_NOON, 78.22, 15.65, &when, &rise));
}

static void test_subsolar_noon(void)
{
    omni_sun_t s;
    omni_sun_at(MAR20_NOON, &s);
    TEST_ASSERT_FLOAT_WITHIN(3.0, 2.0, s.subsolar_lon);
    TEST_ASSERT_FLOAT_WITHIN(0.5, s.decl_deg, s.subsolar_lat);
}

static void test_next_event(void)
{
    int64_t when = 0;
    bool rise = false;
    /* Oct 22 10:00 UTC at Columbus: next is sunrise ~11:49 */
    TEST_ASSERT_TRUE(omni_sun_next_event(OCT22_NOON - 2 * 3600, COL_LAT, COL_LON, &when, &rise));
    TEST_ASSERT_TRUE(rise);
    TEST_ASSERT_INT64_WITHIN(TOL_S, 1792669740LL, when);
    /* 20:00 UTC: next is sunset ~22:44 */
    TEST_ASSERT_TRUE(omni_sun_next_event(OCT22_NOON + 8 * 3600, COL_LAT, COL_LON, &when, &rise));
    TEST_ASSERT_FALSE(rise);
    TEST_ASSERT_INT64_WITHIN(TOL_S, 1792709040LL, when);
    /* 23:30 UTC: next is the following sunrise, ~24 h after the first */
    TEST_ASSERT_TRUE(omni_sun_next_event(OCT22_NOON + 11 * 3600 + 1800, COL_LAT, COL_LON, &when, &rise));
    TEST_ASSERT_TRUE(rise);
    TEST_ASSERT_INT64_WITHIN(TOL_S + 120, 1792669740LL + 86400, when);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_decl_reference);
    RUN_TEST(test_sunpos_matches_mockup);
    RUN_TEST(test_light_unset_is_studio);
    RUN_TEST(test_sun_columbus_oct22);
    RUN_TEST(test_sun_columbus_jun21);
    RUN_TEST(test_sun_columbus_dec21);
    RUN_TEST(test_sun_polar_night);
    RUN_TEST(test_sun_polar_day);
    RUN_TEST(test_subsolar_noon);
    RUN_TEST(test_next_event);
    return UNITY_END();
}
