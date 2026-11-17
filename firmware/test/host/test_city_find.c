#include "unity.h"
#include "omni/city_find.h"
#include "omni/cities.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void lower(char *d, const char *s, size_t cap)
{
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++) d[i] = (s[i] >= 'A' && s[i] <= 'Z') ? (char)(s[i] + 32) : s[i];
    d[i] = 0;
}

/* The group of city i for q (lower case): 0 name starts, 1 name contains, 2 country starts, 3 none. */
static int group(size_t i, const char *q)
{
    char nm[48], co[48];
    lower(nm, omni_city_at(i)->name, sizeof nm);
    lower(co, omni_country_name(omni_city_at(i)->country), sizeof co);
    if (strncmp(nm, q, strlen(q)) == 0) return 0;
    if (strstr(nm, q)) return 1;
    if (strncmp(co, q, strlen(q)) == 0) return 2;
    return 3;
}

/* The expected list, built the other way round: group by group over the table, uncapped, then cut to 16. */
static int oracle(const char *q, uint16_t *out)
{
    int k = 0;
    for (int g = 0; g < 3; g++)
        for (size_t i = 0; i < omni_city_count(); i++)
            if (group(i, q) == g && k < OMNI_CITY_FIND_MAX) out[k++] = (uint16_t)i;
    return k;
}

static int name_is(uint16_t i, const char *s) { return strcmp(omni_city_at(i)->name, s) == 0; }

void test_city_find_order(void)
{
    uint16_t o[OMNI_CITY_FIND_MAX], w[OMNI_CITY_FIND_MAX];
    int k = omni_city_find_all("co", o);
    TEST_ASSERT_EQUAL_INT(oracle("co", w), k);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(w, o, k);
    /* the names starting "Co" first, in table order */
    TEST_ASSERT_TRUE(name_is(o[0], "Colombo"));
    TEST_ASSERT_TRUE(name_is(o[1], "Columbus"));
    TEST_ASSERT_TRUE(name_is(o[2], "Copenhagen"));
    int last_g = 0, seen[3] = { 0, 0, 0 };
    for (int i = 0; i < k; i++) {
        int g = group(o[i], "co");
        TEST_ASSERT_TRUE(g >= last_g && g < 3);
        last_g = g; seen[g]++;
        for (int j = 0; j < i; j++) TEST_ASSERT_NOT_EQUAL(o[j], o[i]);   /* no city twice */
    }
    TEST_ASSERT_EQUAL_INT(3, seen[0]);
    TEST_ASSERT_TRUE(seen[1] > 0);          /* names containing "co" elsewhere */
    /* a country-only match, uncapped: "Costa" is no city's name; San Jose's country is Costa Rica */
    int n = omni_city_find_all("costa", o);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_TRUE(name_is(o[0], "San Jose"));
    /* all three groups appear in one list when it is short enough: "mo" */
    n = omni_city_find_all("mo", o);
    TEST_ASSERT_EQUAL_INT(oracle("mo", w), n);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(w, o, n);
}

void test_city_find_fold(void)
{
    uint16_t a[OMNI_CITY_FIND_MAX], b[OMNI_CITY_FIND_MAX], c[OMNI_CITY_FIND_MAX];
    int na = omni_city_find_all("TOK", a), nb = omni_city_find_all("tok", b), nc = omni_city_find_all("tOk", c);
    TEST_ASSERT_TRUE(na > 0);
    TEST_ASSERT_EQUAL_INT(na, nb);
    TEST_ASSERT_EQUAL_INT(na, nc);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(a, b, na);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(a, c, na);
    TEST_ASSERT_TRUE(name_is(a[0], "Tokyo"));
    /* the query's non-letters are exact: a space matches a space */
    int k = omni_city_find_all("san j", a);
    TEST_ASSERT_TRUE(k >= 1 && name_is(a[0], "San Jose"));
}

void test_city_find_cap(void)
{
    uint16_t o[OMNI_CITY_FIND_MAX + 4], w[OMNI_CITY_FIND_MAX];
    memset(o, 0xFF, sizeof o);
    TEST_ASSERT_EQUAL_INT(16, omni_city_find_all("a", o));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, o[16]);       /* nothing written past 16 */
    TEST_ASSERT_EQUAL_INT(16, oracle("a", w));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(w, o, 16);
}

void test_city_find_none(void)
{
    uint16_t o[OMNI_CITY_FIND_MAX];
    TEST_ASSERT_EQUAL_INT(0, omni_city_find_all("xyzq", o));
    TEST_ASSERT_EQUAL_INT(0, omni_city_find_all("", o));
    TEST_ASSERT_EQUAL_INT(0, omni_city_find_all(NULL, o));
    /* longer than any name */
    TEST_ASSERT_EQUAL_INT(0, omni_city_find_all("san franciscoooooooooooo", o));
}

void test_city_find_whole_name(void)
{
    uint16_t o[OMNI_CITY_FIND_MAX];
    int k = omni_city_find_all("Tokyo", o);
    TEST_ASSERT_TRUE(k >= 1 && name_is(o[0], "Tokyo"));
    k = omni_city_find_all("jap", o);          /* a country prefix finds its cities */
    TEST_ASSERT_TRUE(k >= 1);
    for (int i = 0; i < k; i++) TEST_ASSERT_EQUAL_STRING("JP", omni_city_at(o[i])->country);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_city_find_order);
    RUN_TEST(test_city_find_fold);
    RUN_TEST(test_city_find_cap);
    RUN_TEST(test_city_find_none);
    RUN_TEST(test_city_find_whole_name);
    return UNITY_END();
}
