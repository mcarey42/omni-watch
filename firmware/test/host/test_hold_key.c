#include "unity.h"
#include "omni/hold_key.h"

void setUp(void) {}
void tearDown(void) {}

/* the editor's Restore default: 1 s */
#define FIRE 1000

static omni_gesture_t ev(omni_gesture_kind_t k, uint16_t hold_ms)
{
    omni_gesture_t g = {.kind = k, .x = 233, .y = 318, .x0 = 233, .y0 = 318, .hold_ms = hold_ms};
    return g;
}

static omni_hold_key_rc_t feed(omni_hold_key_t *h, omni_gesture_kind_t k, uint16_t hold_ms)
{
    const omni_gesture_t g = ev(k, hold_ms);
    return omni_hold_key_gesture(h, &g, FIRE);
}

/* The recogniser's ticks (450 + 85 k): nothing before 1 s, FIRE at the first tick at or past it (1 045), once. */
void test_hold_key_fires_once_at_first_tick_past_1s(void)
{
    omni_hold_key_t h = {0};
    omni_hold_key_down(&h, 5000);
    int fires = 0;
    for (uint32_t age = OMNI_HOLD_FIRST_MS; age < 3000; age += OMNI_HOLD_STEP_MS) {
        omni_hold_key_rc_t rc = feed(&h, G_HOLD, (uint16_t)age);
        if (age < FIRE) TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_NONE, rc);
        if (rc == OMNI_HOLD_KEY_FIRE) {
            fires++;
            TEST_ASSERT_EQUAL_UINT32(1045, age);
        }
    }
    TEST_ASSERT_EQUAL(1, fires);
    TEST_ASSERT_FALSE(h.active);
    TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_NONE, feed(&h, G_LIFT_STILL, 0));   /* the lift after acting does nothing */
}

/* A lift at 999 ms (the last tick at 960) cancels; a later tick does nothing. Also an early G_TAP. */
void test_hold_key_lift_cancels(void)
{
    omni_hold_key_t h = {0};
    omni_hold_key_down(&h, 0);
    TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_NONE, feed(&h, G_HOLD, 960));
    TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_CANCEL, feed(&h, G_LIFT_STILL, 0));
    TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_NONE, feed(&h, G_HOLD, 1045));
    omni_hold_key_down(&h, 0);
    TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_CANCEL, feed(&h, G_TAP, 0));
}

/* A drag (or a swipe) ends the press; G_DOWN is the caller's and leaves it as it is. */
void test_hold_key_drag_cancels(void)
{
    static const omni_gesture_kind_t K[] = {G_DRAG_START, G_DRAG_MOVE, G_DRAG_END, G_SWIPE_L, G_SWIPE_R, G_LONG_PRESS};
    for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) {
        omni_hold_key_t h = {0};
        omni_hold_key_down(&h, 0);
        TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_NONE, feed(&h, G_HOLD, 535));
        TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_CANCEL, feed(&h, K[i], 0));
        TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_NONE, feed(&h, G_HOLD, 1130));
    }
    omni_hold_key_t h = {0};
    omni_hold_key_down(&h, 0);
    TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_NONE, feed(&h, G_DOWN, 0));
    TEST_ASSERT_TRUE(h.active);
}

/* A busy event loop: the first tick delivered is already past 1 s (a late sample skips slots, R208), and nothing in
 * between cancels: it still fires. The fill is by time, full from 1 s on. */
void test_hold_key_late_first_tick_fires(void)
{
    omni_hold_key_t h = {0};
    omni_hold_key_down(&h, 100);
    TEST_ASSERT_EQUAL_UINT16(1000, omni_hold_key_fill(&h, 100 + 1700, FIRE));
    TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_FIRE, feed(&h, G_HOLD, 1720));
    TEST_ASSERT_EQUAL_UINT16(0, omni_hold_key_fill(&h, 2000, FIRE));   /* inactive: no fill */
}

/* The fill by signed difference: at t near 0 and near 2^32 (the press straddling the wrap), and a clock read just
 * before t_down (no huge unsigned age). */
void test_hold_key_fill_no_wrap(void)
{
    static const uint32_t T0[] = {0, 1, 0xFFFFFFFFu - 300, 0xFFFFFFFFu, 0x7FFFFFFFu};
    for (unsigned i = 0; i < sizeof T0 / sizeof T0[0]; i++) {
        omni_hold_key_t h = {0};
        omni_hold_key_down(&h, T0[i]);
        TEST_ASSERT_EQUAL_UINT16(0, omni_hold_key_fill(&h, T0[i], FIRE));
        TEST_ASSERT_EQUAL_UINT16(0, omni_hold_key_fill(&h, T0[i] - 16, FIRE));   /* before the press */
        TEST_ASSERT_EQUAL_UINT16(450, omni_hold_key_fill(&h, T0[i] + 450, FIRE));
        TEST_ASSERT_EQUAL_UINT16(999, omni_hold_key_fill(&h, T0[i] + 999, FIRE));
        TEST_ASSERT_EQUAL_UINT16(1000, omni_hold_key_fill(&h, T0[i] + 1000, FIRE));
        TEST_ASSERT_EQUAL_UINT16(1000, omni_hold_key_fill(&h, T0[i] + 60000, FIRE));
        TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_NONE, feed(&h, G_HOLD, 960));
        TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_FIRE, feed(&h, G_HOLD, 1045));
    }
}

/* Not pressed: every gesture is NONE (the key never fires from a stray tick). */
void test_hold_key_inactive_ignores(void)
{
    omni_hold_key_t h = {0};
    TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_NONE, feed(&h, G_HOLD, 2000));
    TEST_ASSERT_EQUAL(OMNI_HOLD_KEY_NONE, feed(&h, G_TAP, 0));
    TEST_ASSERT_EQUAL_UINT16(0, omni_hold_key_fill(&h, 5000, FIRE));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hold_key_fires_once_at_first_tick_past_1s);
    RUN_TEST(test_hold_key_lift_cancels);
    RUN_TEST(test_hold_key_drag_cancels);
    RUN_TEST(test_hold_key_late_first_tick_fires);
    RUN_TEST(test_hold_key_fill_no_wrap);
    RUN_TEST(test_hold_key_inactive_ignores);
    return UNITY_END();
}
