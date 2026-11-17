/* rd wave 1 Task 3: the faces' text helpers (fk/text.h): the 12/24 h time string (12 h hours with no leading zero,
 * spec section 10.0, R154), the date line and the AM/PM suffix (Latin-1 middle dot, as the glyph atlases take it). */
#include <string.h>
#include "unity.h"
#include "fk/text.h"

void setUp(void) {}
void tearDown(void) {}

static fk_clock_t at(int h, int m)
{
    fk_clock_t c;
    memset(&c, 0, sizeof c);
    c.set = true;
    c.h = (uint8_t)h;
    c.m = (uint8_t)m;
    return c;
}

static void time_is(int h, int m, bool h24, const char *want, const char *ampm)
{
    const fk_clock_t c = at(h, m);
    char out[6];
    memset(out, 'x', sizeof out);
    const int n = fk_time_str(&c, h24, out);
    TEST_ASSERT_EQUAL_STRING(want, out);
    TEST_ASSERT_EQUAL_INT((int)strlen(want), n);
    TEST_ASSERT_EQUAL_STRING(ampm, fk_ampm(&c, h24));
}

static void test_fk_text_12h(void)
{
    time_is(1, 0, false, "1:00", " \267 AM");
    time_is(9, 59, false, "9:59", " \267 AM");
    time_is(10, 0, false, "10:00", " \267 AM");
    time_is(12, 0, false, "12:00", " \267 PM");   /* noon (Task 3 review M3) */
    time_is(12, 59, false, "12:59", " \267 PM");
    time_is(0, 30, false, "12:30", " \267 AM");
    time_is(23, 59, false, "11:59", " \267 PM");
    time_is(13, 5, false, "1:05", " \267 PM");
    /* 24 h: two digits, no suffix */
    time_is(9, 59, true, "09:59", "");
    time_is(0, 30, true, "00:30", "");
    time_is(23, 59, true, "23:59", "");
    /* unset */
    fk_clock_t u;
    memset(&u, 0, sizeof u);
    u.h = 9;   /* ignored while unset */
    char out[6];
    TEST_ASSERT_EQUAL_INT(5, fk_time_str(&u, false, out));
    TEST_ASSERT_EQUAL_STRING("--:--", out);
    TEST_ASSERT_EQUAL_INT(5, fk_time_str(&u, true, out));
    TEST_ASSERT_EQUAL_STRING("--:--", out);
    TEST_ASSERT_EQUAL_STRING("", fk_ampm(&u, false));
    TEST_ASSERT_EQUAL_STRING("", fk_ampm(&u, true));
}

static void test_fk_text_date(void)
{
    fk_clock_t c = at(10, 9);
    c.wday = 0;    /* Sunday (tm_wday) */
    c.mday = 1;
    c.mon = 10;    /* November (tm_mon) */
    char out[16];
    memset(out, 'x', sizeof out);
    fk_date_str(&c, out);
    TEST_ASSERT_EQUAL_STRING("SUN 01 NOV", out);
    c.wday = 6, c.mday = 31, c.mon = 11;
    fk_date_str(&c, out);
    TEST_ASSERT_EQUAL_STRING("SAT 31 DEC", out);
    c.wday = 3, c.mday = 9, c.mon = 0;
    fk_date_str(&c, out);
    TEST_ASSERT_EQUAL_STRING("WED 09 JAN", out);
    fk_clock_t u;
    memset(&u, 0, sizeof u);
    fk_date_str(&u, out);
    TEST_ASSERT_EQUAL_STRING("--- -- ---", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fk_text_12h);
    RUN_TEST(test_fk_text_date);
    return UNITY_END();
}
