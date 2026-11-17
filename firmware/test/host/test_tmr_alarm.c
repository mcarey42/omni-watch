#include "unity.h"
#include "omni/tmr_alarm.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define DONE OMNI_TMR_DONE

static omni_tmr_bell_out_t poll(omni_tmr_bell_t *b, omni_tmr_mode_t m, uint32_t done, bool stepped, bool awake,
                                bool woke, uint32_t now)
{
    omni_tmr_bell_out_t o;
    memset(&o, 0x5a, sizeof o);   /* every field is written */
    omni_tmr_bell_poll(b, m, done, stepped, awake, woke, now, &o);
    return o;
}

static omni_tmr_bell_t fresh(void)
{
    omni_tmr_bell_t b;
    memset(&b, 0, sizeof b);
    return b;
}

void test_tmr_retry_fast_then_slow_never_gives_up(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, omni_tmr_retry_us(true, 0));
    TEST_ASSERT_EQUAL_UINT32(0, omni_tmr_retry_us(true, 70));
    for (uint32_t n = 1; n <= OMNI_TMR_RETRY_FAST_N; n++) TEST_ASSERT_EQUAL_UINT32(100000, omni_tmr_retry_us(false, n));
    TEST_ASSERT_EQUAL_UINT32(1000000, omni_tmr_retry_us(false, OMNI_TMR_RETRY_FAST_N + 1));
    TEST_ASSERT_EQUAL_UINT32(1000000, omni_tmr_retry_us(false, 100000));
    TEST_ASSERT_EQUAL_UINT32(1000000, omni_tmr_retry_us(false, UINT32_MAX));
}

void test_tmr_woke_by_gap(void)
{
    TEST_ASSERT_FALSE(omni_tmr_woke(false, 0, 50000));        /* the first poll */
    TEST_ASSERT_FALSE(omni_tmr_woke(true, 1000, 1250));
    TEST_ASSERT_FALSE(omni_tmr_woke(true, 1000, 1999));
    TEST_ASSERT_TRUE(omni_tmr_woke(true, 1000, 2000));
    TEST_ASSERT_TRUE(omni_tmr_woke(true, 0xFFFFFF00u, 0x00000400u));   /* across the 32-bit wrap */
    TEST_ASSERT_FALSE(omni_tmr_woke(true, 0xFFFFFF00u, 0x00000010u));
}

void test_tmr_bell_step_rings_once(void)
{
    omni_tmr_bell_t b = fresh();
    b.handled = true;   /* from an earlier DONE */
    omni_tmr_bell_out_t o = poll(&b, DONE, 10000, true, false, false, 10100);
    TEST_ASSERT_TRUE(o.ring);
    TEST_ASSERT_FALSE(o.keep);
    TEST_ASSERT_FALSE(o.post_alarm);
    TEST_ASSERT_FALSE(o.silence);
    TEST_ASSERT_FALSE(b.handled);
    TEST_ASSERT_TRUE(b.ringing);
    /* the face not awake: no replay, however long */
    o = poll(&b, DONE, 10000, false, false, false, 14000);
    TEST_ASSERT_FALSE(o.ring);
    TEST_ASSERT_FALSE(o.keep);
}

void test_tmr_bell_replay_every_2s_awake(void)
{
    omni_tmr_bell_t b = fresh();
    omni_tmr_bell_out_t o = poll(&b, DONE, 10000, true, true, false, 10000);
    TEST_ASSERT_TRUE(o.ring);
    TEST_ASSERT_FALSE(o.keep);   /* the step's bell is the alarm itself */
    TEST_ASSERT_TRUE(o.handled_now);
    TEST_ASSERT_TRUE(b.handled);
    o = poll(&b, DONE, 10000, false, true, false, 11999);
    TEST_ASSERT_FALSE(o.ring);
    TEST_ASSERT_FALSE(o.handled_now);   /* once */
    o = poll(&b, DONE, 10000, false, true, false, 12000);
    TEST_ASSERT_TRUE(o.ring);
    TEST_ASSERT_TRUE(o.keep);
    o = poll(&b, DONE, 10000, false, true, false, 12250);
    TEST_ASSERT_FALSE(o.ring);
    o = poll(&b, DONE, 10000, false, true, false, 14100);
    TEST_ASSERT_TRUE(o.ring);
    TEST_ASSERT_TRUE(o.keep);
}

void test_tmr_bell_cap_30s(void)
{
    omni_tmr_bell_t b = fresh();
    poll(&b, DONE, 10000, true, true, false, 10000);
    uint32_t rings = 1;
    bool silenced = false;
    for (uint32_t now = 10250; now <= 50000; now += 250) {
        const omni_tmr_bell_out_t o = poll(&b, DONE, 10000, false, true, false, now);
        if (o.ring) {
            TEST_ASSERT_TRUE(now - 10000 < OMNI_TMR_RING_FOR_MS);
            rings++;
        }
        if (o.silence) {
            TEST_ASSERT_FALSE(silenced);   /* once */
            TEST_ASSERT_EQUAL_UINT32(10000 + OMNI_TMR_RING_FOR_MS, now);
            silenced = true;
        }
    }
    TEST_ASSERT_TRUE(silenced);
    TEST_ASSERT_EQUAL_UINT32(15, rings);   /* 0, 2, ..., 28 s */
    TEST_ASSERT_FALSE(b.ringing);
    TEST_ASSERT_TRUE(b.handled);   /* still DONE: the face shows it, silent */
}

void test_tmr_bell_late_step_is_silent(void)
{
    omni_tmr_bell_t b = fresh();
    const omni_tmr_bell_out_t o = poll(&b, DONE, 10000, true, true, false, 10000 + OMNI_TMR_RING_FOR_MS);
    TEST_ASSERT_FALSE(o.ring);
    TEST_ASSERT_FALSE(o.keep);
    TEST_ASSERT_FALSE(o.silence);   /* nothing was sounding */
    TEST_ASSERT_TRUE(o.handled_now);
    TEST_ASSERT_FALSE(b.ringing);
}

void test_tmr_bell_wake_repost_until_handled(void)
{
    omni_tmr_bell_t b = fresh();
    omni_tmr_bell_out_t o = poll(&b, DONE, 10000, true, false, false, 10050);
    TEST_ASSERT_FALSE(o.post_alarm);   /* the one-shot posts the step itself */
    o = poll(&b, DONE, 10000, false, false, false, 10300);
    TEST_ASSERT_FALSE(o.post_alarm);   /* no wake */
    o = poll(&b, DONE, 10000, false, false, true, 90000);
    TEST_ASSERT_TRUE(o.post_alarm);    /* a wake, long after (the QR refused the pick) */
    o = poll(&b, DONE, 10000, false, false, true, 190000);
    TEST_ASSERT_TRUE(o.post_alarm);    /* every wake until the face is seen */
    o = poll(&b, DONE, 10000, false, true, true, 200000);
    TEST_ASSERT_FALSE(o.post_alarm);   /* seen: handled */
    TEST_ASSERT_TRUE(o.handled_now);
    o = poll(&b, DONE, 10000, false, false, true, 300000);
    TEST_ASSERT_FALSE(o.post_alarm);
    TEST_ASSERT_FALSE(o.handled_now);
}

void test_tmr_bell_other_modes_silence(void)
{
    omni_tmr_bell_t b = fresh();
    poll(&b, DONE, 10000, true, true, false, 10000);
    omni_tmr_bell_out_t o = poll(&b, OMNI_TMR_IDLE, 0, false, true, false, 10500);   /* dismissed */
    TEST_ASSERT_TRUE(o.silence);
    TEST_ASSERT_FALSE(o.ring);
    TEST_ASSERT_FALSE(o.post_alarm);
    TEST_ASSERT_FALSE(b.ringing);
    o = poll(&b, OMNI_TMR_COUNT, 0, false, true, true, 11000);
    TEST_ASSERT_FALSE(o.silence);   /* once */
    TEST_ASSERT_FALSE(o.post_alarm);
}

void test_tmr_bell_leave_silences(void)
{
    omni_tmr_bell_t b = fresh();
    poll(&b, DONE, 10000, true, true, false, 10000);
    TEST_ASSERT_TRUE(omni_tmr_bell_silence(&b));
    TEST_ASSERT_FALSE(omni_tmr_bell_silence(&b));   /* nothing left to stop */
    /* back on the face within 30 s: it rings again (the replay is due) */
    const omni_tmr_bell_out_t o = poll(&b, DONE, 10000, false, true, false, 13000);
    TEST_ASSERT_TRUE(o.ring);
    TEST_ASSERT_TRUE(o.keep);
}

void test_tmr_bell_wrap(void)
{
    omni_tmr_bell_t b = fresh();
    const uint32_t done = 0xFFFFF000u;
    poll(&b, DONE, done, true, true, false, done);
    omni_tmr_bell_out_t o = poll(&b, DONE, done, false, true, false, done + 2000u);   /* wraps past 0 */
    TEST_ASSERT_TRUE(o.ring);
    o = poll(&b, DONE, done, false, true, false, done + OMNI_TMR_RING_FOR_MS);
    TEST_ASSERT_TRUE(o.silence);
}

void test_tmr_parse_seconds(void)
{
    uint32_t s = 77;
    TEST_ASSERT_TRUE(omni_tmr_parse_seconds("90", &s));
    TEST_ASSERT_EQUAL_UINT32(90, s);
    TEST_ASSERT_TRUE(omni_tmr_parse_seconds("0", &s));
    TEST_ASSERT_EQUAL_UINT32(0, s);
    TEST_ASSERT_TRUE(omni_tmr_parse_seconds("999999999", &s));
    TEST_ASSERT_EQUAL_UINT32(999999999u, s);
    s = 77;
    static const char *const BAD[] = {"", "abc", "1a", "a1", "-5", "+5", " 5", "5 ", "1.5", "1e3", "1234567890", "0x10"};
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        TEST_ASSERT_FALSE_MESSAGE(omni_tmr_parse_seconds(BAD[i], &s), BAD[i]);
        TEST_ASSERT_EQUAL_UINT32(77, s);
    }
    TEST_ASSERT_FALSE(omni_tmr_parse_seconds(NULL, &s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tmr_retry_fast_then_slow_never_gives_up);
    RUN_TEST(test_tmr_woke_by_gap);
    RUN_TEST(test_tmr_bell_step_rings_once);
    RUN_TEST(test_tmr_bell_replay_every_2s_awake);
    RUN_TEST(test_tmr_bell_cap_30s);
    RUN_TEST(test_tmr_bell_late_step_is_silent);
    RUN_TEST(test_tmr_bell_wake_repost_until_handled);
    RUN_TEST(test_tmr_bell_other_modes_silence);
    RUN_TEST(test_tmr_bell_leave_silences);
    RUN_TEST(test_tmr_bell_wrap);
    RUN_TEST(test_tmr_parse_seconds);
    return UNITY_END();
}
