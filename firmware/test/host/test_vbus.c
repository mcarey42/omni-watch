#include "unity.h"
#include "omni/vbus.h"
#include <string.h>

static omni_vbus_filter_t f;

void setUp(void) { memset(&f, 0, sizeof f); }
void tearDown(void) {}

static void test_first_good_read_sets_state(void)
{
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL)); /* nothing known yet: no VBUS assumed */
    TEST_ASSERT_FALSE(f.known);
    TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT));
    TEST_ASSERT_TRUE(f.known);
}

static void test_failed_read_changes_nothing(void)
{
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    for (int i = 0; i < 10; i++) TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL));
    omni_vbus_filter_t g = {0};
    omni_vbus_feed(&g, OMNI_VBUS_ABSENT);
    for (int i = 0; i < 10; i++) TEST_ASSERT_FALSE(omni_vbus_feed(&g, OMNI_VBUS_READ_FAIL));
}

static void test_one_opposite_read_is_not_an_edge(void)
{
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_ABSENT));
    TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT)); /* the glitch is forgotten */
    TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_ABSENT));  /* one again: still no edge */
    TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT));
}

static void test_two_agreeing_reads_make_an_edge(void)
{
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_ABSENT));
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_ABSENT)); /* unplugged */
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT));
    TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT)); /* plugged again */
}

static void test_failed_read_between_agreeing_reads_is_skipped(void)
{
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_ABSENT));
    TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL));
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_ABSENT));
}

/* m7: a read that fails every time must not freeze "plugged" (light sleep would never come back on battery).
 * After OMNI_VBUS_FAIL_MAX consecutive failures the filter assumes battery, once; the next good read decides. */
static void test_persistent_failure_falls_back_to_battery(void)
{
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    for (int i = 0; i < OMNI_VBUS_FAIL_MAX - 1; i++) TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL));
    TEST_ASSERT_FALSE(f.failsafe);
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL)); /* the K-th: battery assumed */
    TEST_ASSERT_TRUE(f.failsafe);
    for (int i = 0; i < 3 * OMNI_VBUS_FAIL_MAX; i++) TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL));
}

static void test_good_read_between_failures_restarts_the_count(void)
{
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < OMNI_VBUS_FAIL_MAX - 1; i++) omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL);
        TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT));
    }
    TEST_ASSERT_FALSE(f.failsafe);
    TEST_ASSERT_TRUE(f.state);
}

/* N3: leaving the fallback takes OMNI_VBUS_AGREE good reads in a row that agree (a failure between restarts it) */
static void test_recovery_needs_two_agreeing_good_reads(void)
{
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    for (int i = 0; i < OMNI_VBUS_FAIL_MAX; i++) omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL);
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT)); /* one good read: still the fallback */
    TEST_ASSERT_TRUE(f.failsafe);
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL));
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT)); /* the failure restarted the count */
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_ABSENT));  /* disagreeing: restarted again */
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT));
    TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT));  /* two in a row */
    TEST_ASSERT_FALSE(f.failsafe);
    TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_ABSENT)); /* and the glitch rule is back */
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_ABSENT));
}

/* N3: bursts of failures between single good reads stay in the fallback: no flip (and no D+ pulse) per burst */
static void test_bursts_do_not_flip(void)
{
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    for (int k = 0; k < 5; k++) {
        for (int i = 0; i < OMNI_VBUS_FAIL_MAX; i++) omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL);
        TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT));
        TEST_ASSERT_TRUE(f.failsafe);
    }
}

/* N3: the fallback is for light sleep only. The record's view (batrec, the clock's power source) keeps the last
 * confirmed value through it, so a fallback on USB opens no battery session; a real change shows at recovery. */
static void test_fallback_keeps_the_confirmed_value(void)
{
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    TEST_ASSERT_TRUE(f.confirmed);
    for (int i = 0; i < OMNI_VBUS_FAIL_MAX; i++) omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL);
    TEST_ASSERT_FALSE(f.state);
    TEST_ASSERT_TRUE(f.confirmed);
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    TEST_ASSERT_TRUE(f.confirmed);
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    TEST_ASSERT_TRUE(f.state && f.confirmed);
    for (int i = 0; i < OMNI_VBUS_FAIL_MAX; i++) omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL);
    omni_vbus_feed(&f, OMNI_VBUS_ABSENT);   /* unplugged during the fallback: seen at the recovery */
    TEST_ASSERT_TRUE(f.confirmed);
    omni_vbus_feed(&f, OMNI_VBUS_ABSENT);
    TEST_ASSERT_FALSE(f.confirmed);
    /* outside the fallback the two agree */
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    omni_vbus_feed(&f, OMNI_VBUS_PRESENT);
    TEST_ASSERT_TRUE(f.state && f.confirmed);
}

/* N2: a light sleep configured for an assumed battery (the fallback) while USB was in fact present may have failed
 * the host's enumeration: once VBUS is confirmed present again, D+ is re-pulsed. Not for a real battery config. */
static void test_reenumerate_after_an_assumed_battery(void)
{
    TEST_ASSERT_TRUE(omni_vbus_reenumerate(false, true, true));
    TEST_ASSERT_FALSE(omni_vbus_reenumerate(false, false, true)); /* a real battery sleep: the asleep poll's edge */
    TEST_ASSERT_FALSE(omni_vbus_reenumerate(false, true, false)); /* still no VBUS */
    TEST_ASSERT_FALSE(omni_vbus_reenumerate(true, false, true));  /* configured for USB: no light sleep happened */
}

static void test_failsafe_on_battery_stays_battery(void)
{
    omni_vbus_feed(&f, OMNI_VBUS_ABSENT);
    for (int i = 0; i < OMNI_VBUS_FAIL_MAX; i++) TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL));
    TEST_ASSERT_TRUE(f.failsafe);
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_ABSENT));
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_ABSENT));
    TEST_ASSERT_FALSE(f.failsafe);
}

static void test_never_read_counts_too(void)
{
    for (int i = 0; i < OMNI_VBUS_FAIL_MAX; i++) TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_READ_FAIL));
    TEST_ASSERT_TRUE(f.failsafe);
    TEST_ASSERT_FALSE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT));
    TEST_ASSERT_TRUE(omni_vbus_feed(&f, OMNI_VBUS_PRESENT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_good_read_sets_state);
    RUN_TEST(test_failed_read_changes_nothing);
    RUN_TEST(test_one_opposite_read_is_not_an_edge);
    RUN_TEST(test_two_agreeing_reads_make_an_edge);
    RUN_TEST(test_failed_read_between_agreeing_reads_is_skipped);
    RUN_TEST(test_persistent_failure_falls_back_to_battery);
    RUN_TEST(test_good_read_between_failures_restarts_the_count);
    RUN_TEST(test_recovery_needs_two_agreeing_good_reads);
    RUN_TEST(test_bursts_do_not_flip);
    RUN_TEST(test_fallback_keeps_the_confirmed_value);
    RUN_TEST(test_reenumerate_after_an_assumed_battery);
    RUN_TEST(test_failsafe_on_battery_stays_battery);
    RUN_TEST(test_never_read_counts_too);
    return UNITY_END();
}
