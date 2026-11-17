#include "unity.h"
#include "omni/input.h"

void setUp(void) {}
void tearDown(void) {}

static int g_claim_calls;
static bool g_claim_ret;
static bool claim(int16_t x0, int16_t y0, void *ctx)
{
    (void)x0; (void)y0; (void)ctx;
    g_claim_calls++;
    return g_claim_ret;
}

static omni_gesture_sm_t sm;
static omni_gesture_t out[2];
static int feed(uint32_t t, bool p, int x, int y)
{
    return omni_gesture_feed(&sm, t, p, (int16_t)x, (int16_t)y, claim, NULL, out);
}
static void start(void)
{
    omni_gesture_reset(&sm);
    g_claim_calls = 0;
    g_claim_ret = false;
}

static void test_tap(void)
{
    start();
    TEST_ASSERT_EQUAL(0, feed(0, true, 233, 233));
    TEST_ASSERT_EQUAL(0, feed(100, true, 236, 234));
    TEST_ASSERT_EQUAL(1, feed(200, false, 236, 234));
    TEST_ASSERT_EQUAL(G_TAP, out[0].kind);
    TEST_ASSERT_EQUAL(233, out[0].x0);
    TEST_ASSERT_EQUAL(236, out[0].x);
    TEST_ASSERT_FALSE(out[0].edge_start);
    /* 300..600 ms release without movement: nothing */
    start();
    feed(0, true, 233, 233);
    TEST_ASSERT_EQUAL(0, feed(450, false, 233, 233));
    TEST_ASSERT_EQUAL(0, g_claim_calls);
}

static void test_long_press_600ms(void)
{
    start();
    feed(0, true, 233, 233);
    TEST_ASSERT_EQUAL(0, feed(599, true, 233, 233));
    TEST_ASSERT_EQUAL(1, feed(600, true, 233, 233));
    TEST_ASSERT_EQUAL(G_LONG_PRESS, out[0].kind);
    TEST_ASSERT_EQUAL(0, feed(700, true, 233, 233));       /* once only */
    TEST_ASSERT_EQUAL(0, feed(800, false, 233, 233));      /* nothing on release */
    /* moving past 12 px first prevents long press */
    start();
    feed(0, true, 233, 233);
    feed(100, true, 260, 233);
    TEST_ASSERT_EQUAL(0, feed(700, true, 260, 233));
}

static void test_swipe_left_right(void)
{
    start();
    feed(0, true, 300, 233);
    TEST_ASSERT_EQUAL(0, feed(50, true, 270, 235));
    TEST_ASSERT_EQUAL(0, feed(100, true, 220, 235));
    TEST_ASSERT_EQUAL(1, feed(120, false, 220, 235));
    TEST_ASSERT_EQUAL(G_SWIPE_L, out[0].kind);
    TEST_ASSERT_EQUAL(1, g_claim_calls);

    start();
    feed(0, true, 150, 233);
    feed(60, true, 230, 240);
    TEST_ASSERT_EQUAL(1, feed(80, false, 230, 240));
    TEST_ASSERT_EQUAL(G_SWIPE_R, out[0].kind);

    start();
    feed(0, true, 233, 300);
    feed(60, true, 233, 200);
    TEST_ASSERT_EQUAL(1, feed(80, false, 233, 200));
    TEST_ASSERT_EQUAL(G_SWIPE_UP, out[0].kind);

    /* too short (40 px) -> nothing */
    start();
    feed(0, true, 233, 233);
    feed(60, true, 273, 233);
    TEST_ASSERT_EQUAL(0, feed(80, false, 273, 233));
}

static void test_swipe_down_needs_top_start(void)
{
    start();
    feed(0, true, 233, 20);
    feed(60, true, 233, 120);
    TEST_ASSERT_EQUAL(1, feed(80, false, 233, 120));
    TEST_ASSERT_EQUAL(G_SWIPE_DOWN_FROM_TOP, out[0].kind);
    TEST_ASSERT_TRUE(out[0].edge_start);

    start();
    feed(0, true, 233, 200);
    feed(60, true, 233, 300);
    TEST_ASSERT_EQUAL(0, feed(80, false, 233, 300));
}

static void test_drag_claimed_suppresses_swipe(void)
{
    start();
    g_claim_ret = true;
    feed(0, true, 100, 233);
    TEST_ASSERT_EQUAL(1, feed(50, true, 120, 233));
    TEST_ASSERT_EQUAL(G_DRAG_START, out[0].kind);
    TEST_ASSERT_EQUAL(100, out[0].x0);
    TEST_ASSERT_EQUAL(1, feed(70, true, 180, 233));
    TEST_ASSERT_EQUAL(G_DRAG_MOVE, out[0].kind);
    TEST_ASSERT_EQUAL(180, out[0].x);
    TEST_ASSERT_EQUAL(0, feed(90, true, 180, 233));        /* no movement -> nothing */
    TEST_ASSERT_EQUAL(1, feed(110, false, 180, 233));
    TEST_ASSERT_EQUAL(G_DRAG_END, out[0].kind);
    TEST_ASSERT_EQUAL(1, g_claim_calls);                   /* asked exactly once */

    /* no claim callback at all -> treated as unclaimed */
    omni_gesture_reset(&sm);
    omni_gesture_feed(&sm, 0, true, 100, 233, NULL, NULL, out);
    TEST_ASSERT_EQUAL(0, omni_gesture_feed(&sm, 50, true, 200, 233, NULL, NULL, out));
    TEST_ASSERT_EQUAL(1, omni_gesture_feed(&sm, 60, false, 200, 233, NULL, NULL, out));
    TEST_ASSERT_EQUAL(G_SWIPE_R, out[0].kind);
}

static void test_edge_band_flag(void)
{
    start();
    feed(0, true, 10, 233);                                /* distance 223 */
    TEST_ASSERT_EQUAL(1, feed(50, false, 10, 233));
    TEST_ASSERT_EQUAL(G_TAP, out[0].kind);
    TEST_ASSERT_TRUE(out[0].edge_start);

    start();
    feed(0, true, 233, 233 - 192);                         /* distance 192 */
    feed(50, false, 233, 233 - 192);
    TEST_ASSERT_FALSE(out[0].edge_start);

    start();
    feed(0, true, 233, 233 - 193);                         /* distance 193 */
    feed(50, false, 233, 233 - 193);
    TEST_ASSERT_TRUE(out[0].edge_start);
}

static void test_button_short_double_long(void)
{
    omni_button_sm_t b = {0};
    /* short: press/release, then B_SHORT only after 350 ms */
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 0, true));
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 100, false));
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 300, false));
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 449, false));
    TEST_ASSERT_EQUAL(B_SHORT, omni_button_feed(&b, 450, false));
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 500, false));

    /* double: second press within 350 ms of the release */
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 1000, true));
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 1080, false));
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 1200, true));
    TEST_ASSERT_EQUAL(B_DOUBLE, omni_button_feed(&b, 1280, false));
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 2000, false));

    /* long: once at 800 ms while held, nothing on release, no later short */
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 3000, true));
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 3799, true));
    TEST_ASSERT_EQUAL(B_LONG, omni_button_feed(&b, 3800, true));
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 3900, true));
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 4000, false));
    TEST_ASSERT_EQUAL(B_NONE, omni_button_feed(&b, 5000, false));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tap);
    RUN_TEST(test_long_press_600ms);
    RUN_TEST(test_swipe_left_right);
    RUN_TEST(test_swipe_down_needs_top_start);
    RUN_TEST(test_drag_claimed_suppresses_swipe);
    RUN_TEST(test_edge_band_flag);
    RUN_TEST(test_button_short_double_long);
    return UNITY_END();
}
