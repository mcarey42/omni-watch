#include <math.h>
#include "unity.h"
#include "omni/dial.h"
#include "../../apps/omni/main/ui/assets_meta.h"   /* the hands' sizes and pivots as exported (pure #defines) */

#define PI_F 3.14159265358979f

void setUp(void) {}
void tearDown(void) {}

static uint32_t px(float a, int spare)
{
    int32_t b[4];
    omni_dial_sec_box(a, 233, 233, b);
    return (uint32_t)(b[2] - b[0] + 1 + spare) * (uint32_t)(b[3] - b[1] + 1 + spare);
}

/* The constant is the search the face used to run at create: 0.01 deg steps, a row and a column to spare. */
static void test_sec_box_bound_is_the_search(void)
{
    uint32_t most = 0;
    for (int i = 0; i < 36000; i++) {
        const uint32_t n = px((float)i * PI_F / 18000, 1);
        if (n > most) most = n;
    }
    TEST_ASSERT_EQUAL_UINT32(OMNI_DIAL_SEC_BOX_PX, most);
}

/* Every angle the hand can take holds: the 480 beats, and the settle's sweep (finer than any tick gives). */
static void test_sec_box_fits_every_angle(void)
{
    for (int i = 0; i < 480; i++) TEST_ASSERT_TRUE(px((float)i / 480 * 2 * PI_F, 0) <= OMNI_DIAL_SEC_BOX_PX);
    for (int i = 0; i < 360000; i++) TEST_ASSERT_TRUE(px((float)i * PI_F / 180000, 0) <= OMNI_DIAL_SEC_BOX_PX);
}

/* m1: the hour and minute sprites' bounds, the search the face ran at create (3600 rotations x 2 hands, about
 * 100 ms on the LVGL context): the largest (w + 1)(h + 1) over the whole 0.1 deg rotations, shadow included (a row
 * and a column to spare, as the seconds bound, for a 1-ulp sinf/cosf difference between newlib and glibc). */
static const omni_dial_hand_t HOUR = {ASSET_ANA_HOUR_W, ASSET_ANA_HOUR_H, ASSET_ANA_HOUR_PX, ASSET_ANA_HOUR_PY,
                                      ASSET_ANA_HOUR_SH_DX, ASSET_ANA_HOUR_SH_DY};
static const omni_dial_hand_t MIN = {ASSET_ANA_MIN_W, ASSET_ANA_MIN_H, ASSET_ANA_MIN_PX, ASSET_ANA_MIN_PY,
                                     ASSET_ANA_MIN_SH_DX, ASSET_ANA_MIN_SH_DY};

static uint32_t hand_most(const omni_dial_hand_t *h, int spare)
{
    uint32_t most = 0;
    for (int32_t r = 0; r < 3600; r++) {
        int32_t b[4];
        omni_dial_hand_box(h, r, true, 233, 233, b);
        const uint32_t n = (uint32_t)(b[2] - b[0] + 1 + spare) * (uint32_t)(b[3] - b[1] + 1 + spare);
        if (n > most) most = n;
    }
    return most;
}

static void test_hand_box_bounds_are_the_search(void)
{
    TEST_ASSERT_EQUAL_UINT32(OMNI_DIAL_HOUR_BOX_PX, hand_most(&HOUR, 1));
    TEST_ASSERT_EQUAL_UINT32(OMNI_DIAL_MIN_BOX_PX, hand_most(&MIN, 1));
}

/* the shadow only widens the box; without it (the settle's sweep) it fits too */
static void test_hand_box_without_shadow_fits(void)
{
    for (int32_t r = 0; r < 3600; r++) {
        int32_t b[4];
        const uint32_t n = omni_dial_hand_box(&HOUR, r, false, 233, 233, b);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)(b[2] - b[0] + 1) * (uint32_t)(b[3] - b[1] + 1), n);
        TEST_ASSERT_TRUE(n <= OMNI_DIAL_HOUR_BOX_PX);
        TEST_ASSERT_TRUE(omni_dial_hand_box(&MIN, r, false, 233, 233, b) <= OMNI_DIAL_MIN_BOX_PX);
    }
}

/* at 12 o'clock the hour hand's box is its image at the pivot, its shadow at (+3, +4), and 2 px of anti-aliasing */
static void test_hand_box_at_12(void)
{
    int32_t b[4];
    omni_dial_hand_box(&HOUR, 0, true, 233, 233, b);
    TEST_ASSERT_EQUAL_INT32(233 - ASSET_ANA_HOUR_PX - 2, b[0]);
    TEST_ASSERT_EQUAL_INT32(233 - ASSET_ANA_HOUR_PY - 2, b[1]);
    TEST_ASSERT_EQUAL_INT32(233 - ASSET_ANA_HOUR_PX + ASSET_ANA_HOUR_W + ASSET_ANA_HOUR_SH_DX + 2, b[2]);
    TEST_ASSERT_EQUAL_INT32(233 - ASSET_ANA_HOUR_PY + ASSET_ANA_HOUR_H + ASSET_ANA_HOUR_SH_DY + 2, b[3]);
}

static void test_rot_clockwise_from_12(void)
{
    int32_t x, y;
    omni_dial_rot(PI_F / 2, 0, -100, 233, 233, &x, &y);   /* 15 s: right of the centre */
    TEST_ASSERT_EQUAL_INT32(333, x);
    TEST_ASSERT_EQUAL_INT32(233, y);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sec_box_bound_is_the_search);
    RUN_TEST(test_sec_box_fits_every_angle);
    RUN_TEST(test_rot_clockwise_from_12);
    RUN_TEST(test_hand_box_bounds_are_the_search);
    RUN_TEST(test_hand_box_without_shadow_fits);
    RUN_TEST(test_hand_box_at_12);
    return UNITY_END();
}
