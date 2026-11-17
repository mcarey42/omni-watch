/* The rd face ids, keys and default carousel, pinned: the ids are saved in the owner's carousel and offered bits, the
 * keys in the console and the saved face, so a rename of a title must never move them. */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "rd/faces.h"

void setUp(void) {}
void tearDown(void) {}

static void test_rd_ids_frozen(void)
{
    TEST_ASSERT_EQUAL_INT(0, RD_F_STASIS);
    TEST_ASSERT_EQUAL_INT(1, RD_F_TERMINAL);
    TEST_ASSERT_EQUAL_INT(2, RD_F_DECK);
    TEST_ASSERT_EQUAL_INT(3, RD_F_HULL);
    TEST_ASSERT_EQUAL_INT(4, RD_F_BUNK);
    TEST_ASSERT_EQUAL_INT(5, RD_F_CURRY);
    TEST_ASSERT_EQUAL_INT(6, RD_F_DIGITAL);
    TEST_ASSERT_EQUAL_INT(7, RD_F_BAY);
    TEST_ASSERT_EQUAL_INT(8, RD_F_DRIFT);
    TEST_ASSERT_EQUAL_INT(9, RD_F_CONSOLE);
    TEST_ASSERT_EQUAL_INT(10, RD_F_SCOPE);
    TEST_ASSERT_EQUAL_INT(11, RD_F_VENDING);
    TEST_ASSERT_EQUAL_INT(12, RD_F_BOWL);
    TEST_ASSERT_EQUAL_INT(13, RD_F_MOPBOT);
    TEST_ASSERT_EQUAL_INT(14, RD_F__COUNT);
}

static void test_rd_keys_by_id(void)
{
    static const char *const want[14] = {"stasis", "terminal", "deck",    "hull", "bunk",   "curry",  "digital",
                                         "bay",    "drift",    "console", "scope", "vending", "bowl", "mopbot"};
    for (int i = 0; i < 14; i++) TEST_ASSERT_EQUAL_STRING(want[i], RD_FACE_KEYS[i]);
}

/* the value at the pre-track code (d477532 = 0b8c28f's rd sources) */
static void test_rd_default_carousel_frozen(void)
{
    static const uint8_t want[13] = {0, 7, 8, 9, 10, 2, 1, 3, 11, 5, 12, 4, 13};
    TEST_ASSERT_EQUAL_INT(13, RD_DEF_CAROUSEL_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, RD_DEF_CAROUSEL, 13);
}

static void test_rd_bowl_title(void)
{
    const char *title = NULL, *group = NULL;
#define T(id, key, ttl, grp) \
    if ((id) == RD_F_BOWL) title = (ttl), group = (grp);
    RD_FACE_TABLE(T)
#undef T
    TEST_ASSERT_EQUAL_STRING("Bowl", title);
    TEST_ASSERT_EQUAL_STRING("Quarters", group);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rd_ids_frozen);
    RUN_TEST(test_rd_keys_by_id);
    RUN_TEST(test_rd_default_carousel_frozen);
    RUN_TEST(test_rd_bowl_title);
    return UNITY_END();
}
