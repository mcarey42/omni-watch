#include "unity.h"
#include "omni/settings_rows.h"
#include "omni/nav.h"

void setUp(void) {}
void tearDown(void) {}

static int rows(uint8_t reveal, int themes, int8_t *o) { return omni_rows_order(reveal, themes, o, 32); }

static void expect(const int8_t *o, int n, const int *want, int wn)
{
    TEST_ASSERT_EQUAL_INT(wn, n);
    for (int i = 0; i < wn; i++) TEST_ASSERT_EQUAL_INT(want[i], o[i]);
}

void test_settings_rows_omni(void)
{
    int8_t o[32];
    const int want[] = {OMNI_ROW_CITY, OMNI_ROW_BRIGHT, OMNI_ROW_SLEEP, OMNI_ROW_CLOCK, OMNI_ROW_SOUND, OMNI_ROW_VOLUME,
                        OMNI_ROW_REVEAL, OMNI_ROW_MOTION, OMNI_ROW_FACES, OMNI_ROW_WIFI, OMNI_ROW_THEME, OMNI_ROW_ABOUT};
    expect(o, rows(3, 0, o), want, 12);
    TEST_ASSERT_EQUAL_INT(12, OMNI_ROW__COUNT);
}

void test_settings_rows_themes(void)
{
    int8_t o[32];
    const int want[] = {OMNI_ROW_CITY, OMNI_ROW_BRIGHT, OMNI_ROW_SLEEP, OMNI_ROW_CLOCK, OMNI_ROW_SOUND, OMNI_ROW_VOLUME,
                        OMNI_ROW_REVEAL, OMNI_ROW_MOTION,
                        OMNI_ROW__COUNT, OMNI_ROW__COUNT + 1, OMNI_ROW__COUNT + 2, OMNI_ROW__COUNT + 3,
                        OMNI_ROW_FACES, OMNI_ROW_WIFI, OMNI_ROW_THEME, OMNI_ROW_ABOUT};
    expect(o, rows(3, 4, o), want, 16);
    /* reveal_count 1 (and 0) drops Reveal */
    int n = rows(1, 4, o);
    TEST_ASSERT_EQUAL_INT(15, n);
    for (int i = 0; i < n; i++) TEST_ASSERT_NOT_EQUAL(OMNI_ROW_REVEAL, o[i]);
    TEST_ASSERT_EQUAL_INT(OMNI_ROW_MOTION, o[6]);
    TEST_ASSERT_EQUAL_INT(OMNI_ROW__COUNT, o[7]);
    TEST_ASSERT_EQUAL_INT(14, rows(0, 3, o));
    /* max cuts the list without overrunning */
    TEST_ASSERT_EQUAL_INT(5, omni_rows_order(3, 4, o, 5));
    TEST_ASSERT_EQUAL_INT(OMNI_ROW_SOUND, o[4]);
    TEST_ASSERT_EQUAL_INT(9, omni_rows_order(3, 4, o, 9));
    TEST_ASSERT_EQUAL_INT(OMNI_ROW__COUNT, o[8]);
    TEST_ASSERT_EQUAL_INT(0, omni_rows_order(3, 4, o, 0));
    TEST_ASSERT_EQUAL_INT(12, rows(3, -2, o));   /* a negative count is none */
}

void test_settings_rows_links(void)
{
    TEST_ASSERT_EQUAL_INT(OMNI_ROWK_JOG, omni_rows_kind(OMNI_ROW_CITY));
    TEST_ASSERT_EQUAL_INT(OMNI_ROWK_LEVEL, omni_rows_kind(OMNI_ROW_BRIGHT));
    TEST_ASSERT_EQUAL_INT(OMNI_ROWK_LEVEL, omni_rows_kind(OMNI_ROW_VOLUME));
    TEST_ASSERT_EQUAL_INT(OMNI_ROWK_LINK, omni_rows_kind(OMNI_ROW_FACES));
    TEST_ASSERT_EQUAL_INT(OMNI_ROWK_LINK, omni_rows_kind(OMNI_ROW_WIFI));
    TEST_ASSERT_EQUAL_INT(OMNI_ROWK_LINK, omni_rows_kind(OMNI_ROW_ABOUT));
    TEST_ASSERT_EQUAL_INT(OMNI_ROWK_INFO, omni_rows_kind(OMNI_ROW_THEME));
    const int opts[] = {OMNI_ROW_SLEEP, OMNI_ROW_CLOCK, OMNI_ROW_SOUND, OMNI_ROW_REVEAL, OMNI_ROW_MOTION,
                        OMNI_ROW__COUNT, OMNI_ROW__COUNT + 7};
    for (unsigned i = 0; i < sizeof opts / sizeof opts[0]; i++) TEST_ASSERT_EQUAL_INT(OMNI_ROWK_OPTS, omni_rows_kind(opts[i]));
    TEST_ASSERT_EQUAL_INT(OMNI_LINK_FIND_CITY, omni_rows_link(OMNI_ROW_CITY));
    TEST_ASSERT_EQUAL_INT(OMNI_LINK_FACES, omni_rows_link(OMNI_ROW_FACES));
    TEST_ASSERT_EQUAL_INT(OMNI_LINK_WIFI_SHEET, omni_rows_link(OMNI_ROW_WIFI));
    TEST_ASSERT_EQUAL_INT(OMNI_LINK_ABOUT_SHEET, omni_rows_link(OMNI_ROW_ABOUT));
    for (int id = 0; id < OMNI_ROW__COUNT + 8; id++) {
        if (id == OMNI_ROW_CITY || id == OMNI_ROW_FACES || id == OMNI_ROW_WIFI || id == OMNI_ROW_ABOUT) continue;
        TEST_ASSERT_EQUAL_INT(OMNI_LINK_NONE, omni_rows_link(id));
    }
}

void test_settings_rows_clamp(void)
{
    omni_theme_row_t tr[9];
    for (int k = 0; k < 9; k++) tr[k] = (omni_theme_row_t){.get = true, .opts = true, .shorts = true, .n = 2};
    uint8_t keep[8];
    int kept = omni_theme_rows(tr, 9, 8, keep);
    TEST_ASSERT_EQUAL_INT(8, kept);
    int8_t o[32];
    int n = rows(3, kept, o);
    TEST_ASSERT_EQUAL_INT(20, n);
    TEST_ASSERT_EQUAL_INT(OMNI_ROW_MOTION, o[7]);
    for (int k = 0; k < 8; k++) TEST_ASSERT_EQUAL_INT(OMNI_ROW__COUNT + k, o[8 + k]);
    TEST_ASSERT_EQUAL_INT(OMNI_ROW_FACES, o[16]);
    TEST_ASSERT_EQUAL_INT(OMNI_ROW_ABOUT, o[19]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_settings_rows_omni);
    RUN_TEST(test_settings_rows_themes);
    RUN_TEST(test_settings_rows_links);
    RUN_TEST(test_settings_rows_clamp);
    return UNITY_END();
}
