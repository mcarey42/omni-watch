/* The dw face ids, keys and default carousel, pinned: the ids are saved in the owner's carousel and offered bits, the
 * keys in the console and the saved face, so a rename of a title or an identifier must never move them. Links dw_core
 * only (a theme's id test links its own core; the rd ids are test_rd_face_ids). */
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "dw/faces.h"
#include "dw/prefs.h"

void setUp(void) {}
void tearDown(void) {}

static void test_dw_ids_frozen(void)
{
    TEST_ASSERT_EQUAL_INT(0, DW_F_CRYSTAL);
    TEST_ASSERT_EQUAL_INT(1, DW_F_ROUNDEL);
    TEST_ASSERT_EQUAL_INT(2, DW_F_CALLBOX);
    TEST_ASSERT_EQUAL_INT(3, DW_F_ROTOR);
    TEST_ASSERT_EQUAL_INT(4, DW_F_CORAL);
    TEST_ASSERT_EQUAL_INT(5, DW_F_CIRCLES);
    TEST_ASSERT_EQUAL_INT(6, DW_F_DIGITAL);
    TEST_ASSERT_EQUAL_INT(7, DW_F_LEARN);
    TEST_ASSERT_EQUAL_INT(8, DW_F_ROOM);
    TEST_ASSERT_EQUAL_INT(9, DW_F_AMBER);
    TEST_ASSERT_EQUAL_INT(10, DW_F_FOB);
    TEST_ASSERT_EQUAL_INT(11, DW_F_CONSOLE);
    TEST_ASSERT_EQUAL_INT(12, DW_F_WALL);
    TEST_ASSERT_EQUAL_INT(13, DW_F_DOORWAY);
    TEST_ASSERT_EQUAL_INT(14, DW_F_BROADCAST);
    TEST_ASSERT_EQUAL_INT(15, DW_F_VORTEX);
    TEST_ASSERT_EQUAL_INT(16, DW_F__COUNT);
}

static const char *const WANT_KEYS[16] = {"crystal", "roundel", "callbox", "rotor",   "coral", "circles",
                                          "digital", "learn",   "room",    "amber",   "fob",   "console",
                                          "wall",    "doorway", "broadcast", "vortex"};

static void test_dw_keys_by_id(void)
{
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_STRING(WANT_KEYS[i], DW_FACE_KEYS[i]);
}

/* every built face's registry entry answers to its id's key; the shared Digital is the shell's, not dw_core's */
static void test_dw_registry_keys(void)
{
    for (int i = 0; i < DW_F__COUNT; i++) {
        if (i == DW_F_DIGITAL) {
            TEST_ASSERT_NULL(dw_face_by_key(DW_FACE_KEYS[i]));
            continue;
        }
        const fk_face_ops_t *ops = dw_face_by_key(DW_FACE_KEYS[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(ops, DW_FACE_KEYS[i]);
        TEST_ASSERT_EQUAL_STRING(DW_FACE_KEYS[i], ops->key);
    }
}

/* the value at the pre-track code (d477532) */
static void test_dw_default_carousel_frozen(void)
{
    static const uint8_t want[15] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    TEST_ASSERT_EQUAL_INT(15, DW_DEF_CAROUSEL_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, DW_DEF_CAROUSEL, 15);
}

/* the table's NAME is the key upper-cased (Task 9's generated names header relies on it) */
static void test_dw_table_names(void)
{
    int n = 0;
#define T(id, key, NAME, GROUP, thumb)                                            \
    do {                                                                          \
        char up[sizeof #key] = {0};                                               \
        const char *k = #key;                                                     \
        for (size_t j = 0; k[j]; j++) up[j] = (char)toupper((unsigned char)k[j]); \
        TEST_ASSERT_EQUAL_STRING(up, #NAME);                                      \
        TEST_ASSERT_EQUAL_STRING(WANT_KEYS[id], #key);                            \
        n++;                                                                      \
    } while (0);
    DW_FACE_TABLE(T)
#undef T
    TEST_ASSERT_EQUAL_INT(16, n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dw_ids_frozen);
    RUN_TEST(test_dw_keys_by_id);
    RUN_TEST(test_dw_registry_keys);
    RUN_TEST(test_dw_default_carousel_frozen);
    RUN_TEST(test_dw_table_names);
    return UNITY_END();
}
