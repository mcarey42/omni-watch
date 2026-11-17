/* face_kit's face input (fk/face.h; rd wave 2 Task 6): the last good sync's age, appended to fk_face_in_t (RW5's deferred
 * field) and filled by fk_face.c from fk_sync_age(now, shell_status()->sync.last_ok). The first theme leaves it 0: an
 * appended field, so its positional initialisers and every input zeroed by memset (its stills) keep their bytes. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "fk/face.h"

void setUp(void) {}
void tearDown(void) {}

/* last_ok 0 (never) -> -1; now before last_ok -> 0; else the difference, capped at INT32_MAX */
static void test_fk_sync_age(void)
{
    TEST_ASSERT_EQUAL_INT32(-1, fk_sync_age(1762000000, 0));
    TEST_ASSERT_EQUAL_INT32(-1, fk_sync_age(0, 0));
    TEST_ASSERT_EQUAL_INT32(0, fk_sync_age(1000, 1200));
    TEST_ASSERT_EQUAL_INT32(0, fk_sync_age(1000, 1000));
    TEST_ASSERT_EQUAL_INT32(4000, fk_sync_age(5000, 1000));
    TEST_ASSERT_EQUAL_INT32(754, fk_sync_age(1762000754, 1762000000));
    const int64_t y70 = (int64_t)70 * 365 * 86400;   /* a 70-year gap: past INT32_MAX s (68 years) */
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, fk_sync_age(1000 + y70, 1000));
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, fk_sync_age(1000 + (int64_t)INT32_MAX + 1, 1000));
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, fk_sync_age(1000 + (int64_t)INT32_MAX, 1000));
    TEST_ASSERT_EQUAL_INT32(INT32_MAX - 1, fk_sync_age(999 + (int64_t)INT32_MAX, 1000));
    TEST_ASSERT_EQUAL_INT32(0, fk_sync_age(-5, 3));   /* an unset clock before any sync: never negative */
}

/* appended: after every field the first theme reads (its positional initialisers and memset stills keep their bytes) */
static void test_fk_face_in_appended(void)
{
    TEST_ASSERT_TRUE(offsetof(fk_face_in_t, sync_age_s) > offsetof(fk_face_in_t, slept_s));
    TEST_ASSERT_TRUE(offsetof(fk_face_in_t, sync_age_s) > offsetof(fk_face_in_t, click));
    TEST_ASSERT_TRUE(offsetof(fk_face_in_t, sync_age_s) > offsetof(fk_face_in_t, variant));
    TEST_ASSERT_EQUAL_UINT(sizeof(int32_t), sizeof(((fk_face_in_t *)0)->sync_age_s));
    fk_face_in_t in;
    memset(&in, 0, sizeof in);
    TEST_ASSERT_EQUAL_INT32(0, in.sync_age_s);   /* a zeroed input (the first theme's stills): 0 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fk_sync_age);
    RUN_TEST(test_fk_face_in_appended);
    return UNITY_END();
}
