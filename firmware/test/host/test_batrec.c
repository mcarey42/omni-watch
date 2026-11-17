#include "unity.h"
#include "omni/batrec.h"
#include <string.h>

#define T0 1792627200LL

void setUp(void) {}
void tearDown(void) {}

/* dt: seconds since the start of this boot, used for both the wall clock and uptime */
static omni_bsnap_t snap(omni_batrec_t *r, int64_t dt, int pct, int mv) { return omni_batrec_snap(r, T0 + dt, (uint32_t)dt, pct, mv); }

static void test_session_unplug_replug(void)
{
    omni_batrec_t r;
    omni_batrec_init(&r);
    omni_batrec_wake(&r, WAKE_LIFT); /* before the unplug: not in the session */
    TEST_ASSERT_TRUE(omni_batrec_vbus(&r, false, snap(&r, 0, 90, 4100)));
    for (int i = 0; i < 40; i++) omni_batrec_wake(&r, WAKE_LIFT);
    for (int i = 0; i < 10; i++) omni_batrec_wake(&r, WAKE_TOUCH);
    for (int i = 0; i < 70; i++) omni_batrec_wake(&r, WAKE_GLANCE);
    TEST_ASSERT_TRUE(omni_batrec_vbus(&r, true, snap(&r, 5 * 3600, 70, 3900)));
    omni_bsession_report_t o;
    omni_batrec_report(&r, &o);
    TEST_ASSERT_TRUE(o.have);
    TEST_ASSERT_FALSE(o.in_progress);
    TEST_ASSERT_EQUAL_INT(90, o.start_pct);
    TEST_ASSERT_EQUAL_INT(70, o.end_pct);
    TEST_ASSERT_EQUAL_UINT32(40, o.lift);
    TEST_ASSERT_EQUAL_UINT32(10, o.touch);
    TEST_ASSERT_EQUAL_UINT32(70, o.glance);
    TEST_ASSERT_EQUAL_UINT32(50, o.wakes);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, o.hours);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, o.hours_per_100_wakes);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, o.pct_per_hour);
}

static void test_edges_without_change_are_ignored(void)
{
    omni_batrec_t r;
    omni_batrec_init(&r);
    TEST_ASSERT_FALSE(omni_batrec_vbus(&r, true, snap(&r, 0, 50, 3800))); /* replug with no session */
    omni_bsession_report_t o;
    omni_batrec_report(&r, &o);
    TEST_ASSERT_FALSE(o.have);
    omni_batrec_vbus(&r, false, snap(&r, 0, 50, 3800));
    TEST_ASSERT_FALSE(omni_batrec_vbus(&r, false, snap(&r, 60, 49, 3790))); /* second unplug: ignored */
}

static void test_completed_session_kept_until_next_unplug(void)
{
    omni_batrec_t r;
    omni_batrec_init(&r);
    omni_batrec_vbus(&r, false, snap(&r, 0, 90, 4100));
    omni_batrec_vbus(&r, true, snap(&r, 3600, 85, 4050));
    omni_batrec_wake(&r, WAKE_LIFT); /* charging-time wakes do not touch the closed record */
    omni_bsession_report_t o;
    omni_batrec_report(&r, &o);
    TEST_ASSERT_EQUAL_INT(85, o.end_pct);
    TEST_ASSERT_EQUAL_UINT32(0, o.wakes);
    omni_batrec_vbus(&r, false, snap(&r, 7200, 100, 4200));
    omni_batrec_report(&r, &o);
    TEST_ASSERT_TRUE(o.in_progress);
    TEST_ASSERT_EQUAL_INT(100, o.start_pct);
}

static void test_checkpoint_interval_and_brownout(void)
{
    omni_batrec_t r;
    omni_batrec_init(&r);
    omni_batrec_vbus(&r, false, snap(&r, 0, 90, 4100));
    TEST_ASSERT_FALSE(omni_batrec_checkpoint(&r, snap(&r, 600, 89, 4090), 1800));
    omni_batrec_wake(&r, WAKE_LIFT);
    TEST_ASSERT_TRUE(omni_batrec_checkpoint(&r, snap(&r, 1800, 88, 4050), 1800));
    omni_batrec_wake(&r, WAKE_LIFT); /* lost: after the last checkpoint */
    /* reboot (brown-out), then plugged in at boot */
    TEST_ASSERT_TRUE(omni_batrec_boot(&r, true, snap(&r, 7200, 5, 3300)));
    omni_bsession_report_t o;
    omni_batrec_report(&r, &o);
    TEST_ASSERT_TRUE(o.have);
    TEST_ASSERT_TRUE(o.partial);
    TEST_ASSERT_EQUAL_INT(88, o.end_pct);
    TEST_ASSERT_EQUAL_UINT32(1, o.lift);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, o.hours);
}

static void test_boot_on_battery_starts_session_and_no_wakes_gives_no_ratio(void)
{
    omni_batrec_t r;
    omni_batrec_init(&r);
    TEST_ASSERT_TRUE(omni_batrec_boot(&r, false, snap(&r, 0, 60, 3950)));
    omni_batrec_vbus(&r, true, snap(&r, 7200, 55, 3900));
    omni_bsession_report_t o;
    omni_batrec_report(&r, &o);
    TEST_ASSERT_EQUAL_UINT32(0, o.wakes);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, o.hours_per_100_wakes);
}

static void test_clock_unset_duration_from_uptime(void)
{
    omni_batrec_t r;
    omni_batrec_init(&r);
    omni_batrec_vbus(&r, false, omni_batrec_snap(&r, 0, 0, 90, 4100));
    omni_batrec_wake(&r, WAKE_LIFT);
    omni_batrec_vbus(&r, true, omni_batrec_snap(&r, 0, 10, 80, 4000));
    omni_bsession_report_t o;
    omni_batrec_report(&r, &o);
    TEST_ASSERT_TRUE(o.have);
    TEST_ASSERT_EQUAL_INT64(10, o.duration_s); /* uptime, not the wall clock */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f / 3600.0f * 100.0f, o.hours_per_100_wakes);
}

static omni_bsnap_t snap_unset(omni_batrec_t *r, uint32_t up_s, int pct) { return omni_batrec_snap(r, 0, up_s, pct, 3900); }

static void test_checkpoint_gate_with_clock_unset(void)
{
    omni_batrec_t r;
    omni_batrec_init(&r);
    omni_batrec_boot(&r, false, snap_unset(&r, 5, 90)); /* battery boot, clock unset */
    int taken = 0;
    for (uint32_t t = 6; t < 1800 + 5; t++) /* every second for 30 min less one second */
        taken += omni_batrec_checkpoint(&r, snap_unset(&r, t, 90), 1800);
    TEST_ASSERT_EQUAL_INT(0, taken);
    TEST_ASSERT_FALSE(omni_batrec_checkpoint_due(&r, 1804, 1800));
    TEST_ASSERT_TRUE(omni_batrec_checkpoint(&r, snap_unset(&r, 1805, 88), 1800));
    TEST_ASSERT_FALSE(omni_batrec_checkpoint(&r, snap_unset(&r, 1806, 88), 1800));
    TEST_ASSERT_FALSE(omni_batrec_checkpoint(&r, snap_unset(&r, 1807, 88), 1800));
    TEST_ASSERT_EQUAL_INT(88, r.last.pct);
}

static void test_checkpoint_not_due_off_battery(void)
{
    omni_batrec_t r;
    omni_batrec_init(&r);
    TEST_ASSERT_FALSE(omni_batrec_checkpoint_due(&r, 100000, 1800));
}

static void test_reboot_mid_session_continues_on_battery(void)
{
    omni_batrec_t r;
    omni_batrec_init(&r);
    omni_batrec_boot(&r, false, snap(&r, 0, 90, 4100)); /* boot 1: session starts */
    omni_batrec_wake(&r, WAKE_LIFT);
    omni_batrec_checkpoint(&r, snap(&r, 1800, 85, 4000), 1800);
    omni_bsnap_t start = r.start;
    uint32_t lift = r.lift;
    /* reboot while still on battery: record reloaded from NVS, boot() sees VBUS absent */
    TEST_ASSERT_FALSE(omni_batrec_boot(&r, false, snap(&r, 10, 84, 3990)));
    TEST_ASSERT_TRUE(r.on_battery);
    TEST_ASSERT_EQUAL_INT64(start.unix_s, r.start.unix_s);
    TEST_ASSERT_EQUAL_INT(90, r.start.pct);
    TEST_ASSERT_EQUAL_UINT32(lift, r.lift);
    TEST_ASSERT_EQUAL_INT(85, r.last.pct); /* checkpoint kept */
    TEST_ASSERT_EQUAL_UINT16(2, r.boot);
    /* the first checkpoint of the new boot is due at once (the old one is from boot 1) */
    TEST_ASSERT_TRUE(omni_batrec_checkpoint_due(&r, 11, 1800));
    omni_batrec_wake(&r, WAKE_LIFT);
    /* replug closes it live; duration falls back to the wall clock across the reboot */
    omni_bsnap_t end = omni_batrec_snap(&r, T0 + 7200, 5000, 70, 3850);
    TEST_ASSERT_TRUE(omni_batrec_vbus(&r, true, end));
    omni_bsession_report_t o;
    omni_batrec_report(&r, &o);
    TEST_ASSERT_FALSE(o.partial);
    TEST_ASSERT_EQUAL_INT64(7200, o.duration_s);
    TEST_ASSERT_EQUAL_UINT32(2, o.lift);
    TEST_ASSERT_EQUAL_INT(90, o.start_pct);
}

static void test_boot_plugged_with_no_session_is_noop(void)
{
    omni_batrec_t r;
    omni_batrec_init(&r);
    TEST_ASSERT_FALSE(omni_batrec_boot(&r, true, snap(&r, 0, 100, 4200)));
    omni_bsession_report_t o;
    omni_batrec_report(&r, &o);
    TEST_ASSERT_FALSE(o.have);
}

static void test_replug_without_unplug_keeps_completed_session(void)
{
    omni_batrec_t r;
    omni_batrec_init(&r);
    omni_batrec_vbus(&r, false, snap(&r, 0, 90, 4100));
    omni_batrec_wake(&r, WAKE_LIFT);
    omni_batrec_vbus(&r, true, snap(&r, 3600, 80, 4000));
    omni_batrec_wake(&r, WAKE_LIFT);
    TEST_ASSERT_FALSE(omni_batrec_vbus(&r, true, snap(&r, 4000, 100, 4200))); /* second plug edge */
    TEST_ASSERT_FALSE(omni_batrec_boot(&r, true, snap(&r, 0, 100, 4200)));     /* reboot while plugged */
    omni_bsession_report_t o;
    omni_batrec_report(&r, &o);
    TEST_ASSERT_EQUAL_INT(80, o.end_pct);
    TEST_ASSERT_EQUAL_INT64(3600, o.duration_s);
    TEST_ASSERT_EQUAL_UINT32(1, o.lift);
}

static void test_blob_round_trip(void)
{
    omni_batrec_t r, back;
    omni_batrec_init(&r);
    omni_batrec_vbus(&r, false, snap(&r, 0, 90, 4100));
    omni_batrec_wake(&r, WAKE_LIFT);
    omni_batrec_wake(&r, WAKE_TOUCH);
    omni_batrec_wake(&r, WAKE_GLANCE);
    omni_batrec_vbus(&r, true, snap(&r, 3600, 80, 4000));
    uint8_t blob[OMNI_BATREC_BLOB_SIZE];
    omni_batrec_encode(&r, blob);
    TEST_ASSERT_TRUE(omni_batrec_decode(&back, blob, sizeof blob));
    TEST_ASSERT_EQUAL_MEMORY(&r, &back, sizeof r);
}

static void test_blob_rejects_bad_input(void)
{
    omni_batrec_t r, back;
    omni_batrec_init(&r);
    omni_batrec_vbus(&r, false, snap(&r, 0, 90, 4100));
    uint8_t blob[OMNI_BATREC_BLOB_SIZE];
    omni_batrec_encode(&r, blob);
    TEST_ASSERT_FALSE(omni_batrec_decode(&back, blob, sizeof blob - 1)); /* size */
    uint8_t bad[OMNI_BATREC_BLOB_SIZE];
    memcpy(bad, blob, sizeof bad);
    bad[0] ^= 0xff; /* version */
    TEST_ASSERT_FALSE(omni_batrec_decode(&back, bad, sizeof bad));
    TEST_ASSERT_FALSE(back.on_battery); /* left empty */
    omni_batrec_t t = r;
    t.on_battery = 7; /* flag out of range */
    omni_batrec_encode(&t, bad);
    TEST_ASSERT_FALSE(omni_batrec_decode(&back, bad, sizeof bad));
    t = r;
    t.start.pct = 120; /* pct out of range */
    omni_batrec_encode(&t, bad);
    TEST_ASSERT_FALSE(omni_batrec_decode(&back, bad, sizeof bad));
    t = r;
    t.start.valid = 0; /* on battery with no start */
    omni_batrec_encode(&t, bad);
    TEST_ASSERT_FALSE(omni_batrec_decode(&back, bad, sizeof bad));
    TEST_ASSERT_TRUE(omni_batrec_decode(&back, blob, sizeof blob));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_session_unplug_replug);
    RUN_TEST(test_edges_without_change_are_ignored);
    RUN_TEST(test_completed_session_kept_until_next_unplug);
    RUN_TEST(test_checkpoint_interval_and_brownout);
    RUN_TEST(test_boot_on_battery_starts_session_and_no_wakes_gives_no_ratio);
    RUN_TEST(test_clock_unset_duration_from_uptime);
    RUN_TEST(test_checkpoint_gate_with_clock_unset);
    RUN_TEST(test_checkpoint_not_due_off_battery);
    RUN_TEST(test_reboot_mid_session_continues_on_battery);
    RUN_TEST(test_boot_plugged_with_no_session_is_noop);
    RUN_TEST(test_replug_without_unplug_keeps_completed_session);
    RUN_TEST(test_blob_round_trip);
    RUN_TEST(test_blob_rejects_bad_input);
    return UNITY_END();
}
