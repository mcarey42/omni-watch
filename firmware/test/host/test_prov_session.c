#include "unity.h"
#include "omni/prov_session.h"

void setUp(void) {}
void tearDown(void) {}

/* Leaving the QR screen or the watch going to sleep ends the session, except while credentials are being tried. */
static void test_stop_on_leave_and_sleep(void)
{
    const omni_prov_why_t why[] = {PROV_WHY_LEFT, PROV_WHY_SLEEP};
    for (int i = 0; i < 2; i++) {
        TEST_ASSERT_TRUE(omni_prov_stop_on(PROV_PH_WAITING, why[i]));
        TEST_ASSERT_TRUE(omni_prov_stop_on(PROV_PH_PHONE, why[i]));
        TEST_ASSERT_TRUE(omni_prov_stop_on(PROV_PH_FAILED, why[i]));
        TEST_ASSERT_TRUE(omni_prov_stop_on(PROV_PH_DONE, why[i]));
        TEST_ASSERT_FALSE(omni_prov_stop_on(PROV_PH_JOINING, why[i])); /* the result follows within the join */
    }
}

/* The hard cap from the session start, whatever the phase; wrap-safe. */
static void test_expires_at_the_cap(void)
{
    TEST_ASSERT_FALSE(omni_prov_expired(1000, 1000));
    TEST_ASSERT_FALSE(omni_prov_expired(1000, 1000 + OMNI_PROV_MAX_MS - 1));
    TEST_ASSERT_TRUE(omni_prov_expired(1000, 1000 + OMNI_PROV_MAX_MS));
    const uint32_t t0 = 0xFFFFFF00u; /* the ms clock wraps during the session */
    TEST_ASSERT_FALSE(omni_prov_expired(t0, t0 + 1000u));
    TEST_ASSERT_FALSE(omni_prov_expired(t0, t0 + OMNI_PROV_MAX_MS - 1u));
    TEST_ASSERT_TRUE(omni_prov_expired(t0, t0 + OMNI_PROV_MAX_MS));
}

/* The QR screen's long timeout (the owner needs time to scan and to type the password) applies only while the
 * QR is up or a phone is linked; an error or a background-sync screen sleeps on the normal timeout. */
static void test_long_screen_timeout_only_for_qr_and_phone(void)
{
    TEST_ASSERT_TRUE(omni_prov_long_timeout(PROV_PH_WAITING));
    TEST_ASSERT_TRUE(omni_prov_long_timeout(PROV_PH_PHONE));
    TEST_ASSERT_FALSE(omni_prov_long_timeout(PROV_PH_JOINING));
    TEST_ASSERT_FALSE(omni_prov_long_timeout(PROV_PH_FAILED));
    TEST_ASSERT_FALSE(omni_prov_long_timeout(PROV_PH_DONE));
    TEST_ASSERT_FALSE(omni_prov_long_timeout(PROV_PH_NONE));
}

/* I1: every way a session ends without CRED_OK, after the manager wrote (CRED_RECV), restores the saved network */
static void test_prov_restore_every_end(void)
{
    for (int w = PROV_END_FAILED; w <= PROV_END_SUCCESS; w++) {
        if (w == PROV_END_SUCCESS) continue;
        omni_prov_end_plan_t p = omni_prov_end_plan(true, true, false, (omni_prov_end_t)w);
        TEST_ASSERT_TRUE(p.restore);
        TEST_ASSERT_TRUE(p.provisioned);
        TEST_ASSERT_TRUE(p.name_from_snapshot);
    }
}

/* I1: no CRED_RECV, nothing to undo; an empty snapshot never restores; CRED_OK keeps the new network. `why` changes
 * nothing (Minor 7). */
static void test_prov_restore_only_after_cred_recv(void)
{
    for (int w = PROV_END_FAILED; w <= PROV_END_SUCCESS; w++) {
        const omni_prov_end_t why = (omni_prov_end_t)w;
        for (int had = 0; had < 2; had++) {
            omni_prov_end_plan_t p = omni_prov_end_plan(had, false, false, why);   /* the phone sent nothing */
            TEST_ASSERT_FALSE(p.restore);
            TEST_ASSERT_EQUAL(had, p.provisioned);   /* as before the session */
            TEST_ASSERT_EQUAL(had, p.name_from_snapshot);
            p = omni_prov_end_plan(had, true, true, why);   /* CRED_OK: the new network stays */
            TEST_ASSERT_FALSE(p.restore);
            TEST_ASSERT_TRUE(p.provisioned);
            TEST_ASSERT_FALSE(p.name_from_snapshot);
            p = omni_prov_end_plan(had, false, true, why);
            TEST_ASSERT_FALSE(p.restore);
            TEST_ASSERT_TRUE(p.provisioned);
            TEST_ASSERT_FALSE(p.name_from_snapshot);
        }
        /* an empty snapshot is never restored, even after the manager wrote */
        omni_prov_end_plan_t p = omni_prov_end_plan(false, true, false, why);
        TEST_ASSERT_FALSE(p.restore);
        TEST_ASSERT_FALSE(p.provisioned);
        TEST_ASSERT_FALSE(p.name_from_snapshot);
    }
    /* restore is exactly snap && recv && !ok */
    for (int m = 0; m < 8; m++)
        TEST_ASSERT_EQUAL((m & 1) && (m & 2) && !(m & 4),
                          omni_prov_end_plan(m & 1, m & 2, m & 4, PROV_END_LEFT).restore);
}

/* I5: "Type it instead" only while the session would stop for it: never while joining */
static void test_prov_type_instead_not_joining(void)
{
    TEST_ASSERT_TRUE(omni_prov_type_instead_ok(PROV_PH_WAITING));
    TEST_ASSERT_TRUE(omni_prov_type_instead_ok(PROV_PH_PHONE));
    TEST_ASSERT_TRUE(omni_prov_type_instead_ok(PROV_PH_FAILED));
    TEST_ASSERT_FALSE(omni_prov_type_instead_ok(PROV_PH_JOINING));
    TEST_ASSERT_FALSE(omni_prov_type_instead_ok(PROV_PH_DONE));
    TEST_ASSERT_FALSE(omni_prov_type_instead_ok(PROV_PH_NONE));
    for (int ph = PROV_PH_NONE; ph <= PROV_PH_FAILED; ph++)
        if (omni_prov_type_instead_ok((omni_prov_phase_t)ph))
            TEST_ASSERT_TRUE(omni_prov_stop_on((omni_prov_phase_t)ph, PROV_WHY_LEFT));
}

/* F6 (R221): manager_wrote is CRED_RECV seen at dispatch or a non-empty driver config after deinit; with the plan,
 * a recorded CRED_OK always wins, an empty snapshot never restores, and a session where the phone sent nothing
 * (no CRED_RECV, the config still the manager's empty one) never restores */
static void test_prov_manager_wrote_witnesses(void)
{
    TEST_ASSERT_FALSE(omni_prov_manager_wrote(false, false));
    TEST_ASSERT_TRUE(omni_prov_manager_wrote(true, false));
    TEST_ASSERT_TRUE(omni_prov_manager_wrote(false, true));
    TEST_ASSERT_TRUE(omni_prov_manager_wrote(true, true));
    for (int m = 0; m < 16; m++) {
        const bool snap = m & 1, recv = m & 2, cfg = m & 4, ok = m & 8;
        omni_prov_end_plan_t p = omni_prov_end_plan(snap, omni_prov_manager_wrote(recv, cfg), ok, PROV_END_CAP);
        if (ok) TEST_ASSERT_FALSE(p.restore);              /* cred_ok wins */
        if (!snap) TEST_ASSERT_FALSE(p.restore);           /* an empty snapshot never restores */
        if (!recv && !cfg) TEST_ASSERT_FALSE(p.restore);   /* nothing sent: nothing to undo */
        TEST_ASSERT_EQUAL(snap && (recv || cfg) && !ok, p.restore);
    }
}

/* F4: a phone session starts only with a snapshot read from a fresh driver init */
static void test_prov_start_needs_fresh_snapshot(void)
{
    TEST_ASSERT_TRUE(omni_prov_start_ok(true, true));
    TEST_ASSERT_FALSE(omni_prov_start_ok(false, true));
    TEST_ASSERT_FALSE(omni_prov_start_ok(true, false));
    TEST_ASSERT_FALSE(omni_prov_start_ok(false, false));
}

/* F5: after a restore that failed (twice), provisioned follows what the driver reports; otherwise the plan */
static void test_prov_end_provisioned_after_restore(void)
{
    for (int m = 0; m < 16; m++) {
        const bool snap = m & 1, wrote = m & 2, ok = m & 4, drv = m & 8;
        omni_prov_end_plan_t p = omni_prov_end_plan(snap, wrote, ok, PROV_END_LEFT);
        TEST_ASSERT_EQUAL(p.provisioned, omni_prov_end_provisioned(p, true, drv));
        TEST_ASSERT_EQUAL(p.restore ? drv : p.provisioned, omni_prov_end_provisioned(p, false, drv));
    }
    omni_prov_end_plan_t p = omni_prov_end_plan(true, true, false, PROV_END_FAILED);
    TEST_ASSERT_TRUE(p.restore);
    TEST_ASSERT_FALSE(omni_prov_end_provisioned(p, false, false));   /* the driver holds nothing: not provisioned */
    TEST_ASSERT_TRUE(omni_prov_end_provisioned(p, false, true));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stop_on_leave_and_sleep);
    RUN_TEST(test_expires_at_the_cap);
    RUN_TEST(test_long_screen_timeout_only_for_qr_and_phone);
    RUN_TEST(test_prov_restore_every_end);
    RUN_TEST(test_prov_restore_only_after_cred_recv);
    RUN_TEST(test_prov_type_instead_not_joining);
    RUN_TEST(test_prov_manager_wrote_witnesses);
    RUN_TEST(test_prov_start_needs_fresh_snapshot);
    RUN_TEST(test_prov_end_provisioned_after_restore);
    return UNITY_END();
}
