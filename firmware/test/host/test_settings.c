#include <string.h>
#include "unity.h"
#include "omni/cities.h"
#include "omni/settings.h"
#include "omni/local.h"
#include "omni/shared.h"

/* The Omni's face ids (apps/omni/main/omni_faces.h), test-local: omni_core's face ids are plain uint8_t (plan PD2). */
enum { FACE_OMNI, FACE_GLOBE_LIVE, FACE_GLOBE_ANALOG, FACE_DIGITAL, FACE_WORLD, FACE_ALMANAC, FACE_TIMER, FACE_GLOBE_SPIN,
       FACE__COUNT };

/* the Omni theme's limits (apps/omni/main/omni_theme.c): the carousel and reveal belong to t_<id>/local (Task 6) */
static const uint8_t k_def[] = { FACE_OMNI, FACE_GLOBE_LIVE, FACE_GLOBE_ANALOG, FACE_DIGITAL,
                                 FACE_WORLD, FACE_ALMANAC, FACE_TIMER, FACE_GLOBE_SPIN };
static const omni_theme_limits_t k_lim = { FACE__COUNT, 3, OMNI_REVEAL_FLIP, sizeof k_def, k_def };

void setUp(void) {}
void tearDown(void) {}

static void test_defaults_london(void)
{
    omni_settings_t s;
    omni_settings_defaults(&s);
    const omni_city_t *c = omni_city_at(s.city);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_STRING("London", c->name);
    TEST_ASSERT_EQUAL_STRING("GB", c->country);
    TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", c->posix_tz);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 51.51f, c->lat);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -0.13f, c->lon);
    TEST_ASSERT_EQUAL_UINT8(70, s.brightness);
    TEST_ASSERT_EQUAL_UINT8(15, s.sleep_s);
    TEST_ASSERT_TRUE(s.sound_on);
    TEST_ASSERT_EQUAL_UINT8(30, s.volume);
    TEST_ASSERT_TRUE(omni_settings_validate(&s));
}

static void test_shared_defaults_london(void)
{
    omni_shared_t sh;
    omni_shared_defaults(&sh);
    const omni_city_t *c = omni_city_at(sh.city);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_STRING("London", c->name);
    TEST_ASSERT_EQUAL_STRING("GB", c->country);
    TEST_ASSERT_EQUAL_UINT16(omni_city_default(), sh.city);
}

static void test_validate_bad_city_london(void)
{
    omni_shared_t sh;
    omni_shared_defaults(&sh);
    sh.city = (uint16_t)omni_city_count();
    TEST_ASSERT_FALSE(omni_shared_validate(&sh));
    TEST_ASSERT_EQUAL_STRING("London", omni_city_at(sh.city)->name);
}

/* The default never applies over a saved record: a shared v1 record saved with Columbus/US stays Columbus. */
static void test_saved_columbus_kept(void)
{
    omni_shared_t s, d;
    omni_shared_defaults(&s);
    s.city = (uint16_t)omni_city_find_key("Columbus", "US");
    uint8_t b[OMNI_REC_MAX];
    size_t n = omni_shared_encode(&s, b, sizeof b);
    TEST_ASSERT_EQUAL(OMNI_SHARED_V1_SIZE, n);
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, omni_shared_decode(&d, b, n));
    TEST_ASSERT_EQUAL_STRING("Columbus", omni_city_at(d.city)->name);
    TEST_ASSERT_EQUAL_STRING("US", omni_city_at(d.city)->country);
}

/* Task 6: the carousel is the theme's (t_<id>/local), not the shared defaults'. The joined RAM view gets the Omni
 * limits' default, today's order. */
static void test_settings_defaults_no_carousel(void)
{
    omni_settings_t s;
    omni_settings_defaults(&s);
    TEST_ASSERT_EQUAL_UINT8(0, s.carousel_len);
    omni_shared_t sh;
    omni_local_t lo;
    omni_shared_defaults(&sh);
    omni_local_defaults(&lo, &k_lim);
    omni_settings_join(&sh, &lo, &s);
    const uint8_t want[] = { FACE_OMNI, FACE_GLOBE_LIVE, FACE_GLOBE_ANALOG, FACE_DIGITAL,
                             FACE_WORLD, FACE_ALMANAC, FACE_TIMER, FACE_GLOBE_SPIN };
    TEST_ASSERT_EQUAL_UINT8(sizeof want, s.carousel_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, s.carousel, sizeof want);
    TEST_ASSERT_EQUAL_UINT8(OMNI_REVEAL_FLIP, s.reveal_style);
    TEST_ASSERT_EQUAL_STRING("London", omni_city_at(s.city)->name);
    TEST_ASSERT_EQUAL_UINT8(70, s.brightness);
}

static void test_validate_clamps(void)
{
    omni_settings_t s;
    omni_settings_defaults(&s);
    uint16_t def = s.city;
    s.brightness = 0;
    s.sleep_s = 200;
    s.city = 9999;
    TEST_ASSERT_FALSE(omni_settings_validate(&s));
    TEST_ASSERT_EQUAL_UINT8(5, s.brightness);
    TEST_ASSERT_EQUAL_UINT8(60, s.sleep_s);
    TEST_ASSERT_EQUAL_UINT16(def, s.city);
    TEST_ASSERT_TRUE(omni_settings_validate(&s));
}

/* The carousel cases (a bad id, a duplicate, n == 0) moved from omni_settings_validate to omni_local_validate (Task 6,
 * preflight A2): omni_settings_validate checks the shared fields only and leaves the carousel and reveal alone. */
static void test_validate_carousel(void)
{
    omni_local_t l;
    omni_local_defaults(&l, &k_lim);
    l.carousel[1] = 99;       /* unknown face */
    l.carousel[2] = FACE_OMNI; /* duplicate */
    TEST_ASSERT_FALSE(omni_local_validate(&l, &k_lim));
    TEST_ASSERT_EQUAL_UINT8(6, l.carousel_len);
    TEST_ASSERT_EQUAL_UINT8(FACE_OMNI, l.carousel[0]);
    TEST_ASSERT_EQUAL_UINT8(FACE_DIGITAL, l.carousel[1]);
    l.carousel_len = 0;
    TEST_ASSERT_FALSE(omni_local_validate(&l, &k_lim));
    TEST_ASSERT_EQUAL_UINT8(FACE__COUNT, l.carousel_len);

    omni_settings_t s;
    omni_settings_defaults(&s);
    s.carousel[0] = 99;
    s.carousel_len = 1;
    s.reveal_style = 9;
    TEST_ASSERT_TRUE(omni_settings_validate(&s));
    TEST_ASSERT_EQUAL_UINT8(99, s.carousel[0]);
    TEST_ASSERT_EQUAL_UINT8(1, s.carousel_len);
    TEST_ASSERT_EQUAL_UINT8(9, s.reveal_style);
}

static void test_city_count_range(void)
{
    size_t n = omni_city_count();
    TEST_ASSERT_TRUE(n >= 120 && n <= 200);
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            TEST_ASSERT_NOT_EQUAL_MESSAGE(0, strcmp(omni_city_at(i)->name, omni_city_at(j)->name),
                                          omni_city_at(i)->name);
    TEST_ASSERT_EQUAL_INT(-1, omni_city_find("Nowhereville"));
    TEST_ASSERT_NULL(omni_city_at(n));
}

static void test_every_city_has_posix(void)
{
    for (size_t i = 0; i < omni_city_count(); i++) {
        const omni_city_t *c = omni_city_at(i);
        TEST_ASSERT_TRUE_MESSAGE(c->posix_tz && c->posix_tz[0], c->name);
        TEST_ASSERT_TRUE_MESSAGE(c->tz_name && c->tz_name[0] && c->country && c->country[0], c->name);
        TEST_ASSERT_TRUE_MESSAGE(c->lat >= -90 && c->lat <= 90, c->name);
        TEST_ASSERT_TRUE_MESSAGE(c->lon >= -180 && c->lon <= 180, c->name);
        TEST_ASSERT_EQUAL_INT((int)i, omni_city_find(c->name));
    }
}

/* Several city names are ambiguous (San Jose, Kingston, Washington): the settings drum shows the country too. */
static void test_country_names(void)
{
    TEST_ASSERT_EQUAL_STRING("Japan", omni_country_name("JP"));
    TEST_ASSERT_EQUAL_STRING("Costa Rica", omni_country_name(omni_city_at((size_t)omni_city_find("San Jose"))->country));
    TEST_ASSERT_EQUAL_STRING("Jamaica", omni_country_name(omni_city_at((size_t)omni_city_find("Kingston"))->country));
    TEST_ASSERT_EQUAL_STRING("United States", omni_country_name("US"));
    TEST_ASSERT_EQUAL_STRING("United Kingdom", omni_country_name("GB"));
    TEST_ASSERT_EQUAL_STRING("South Korea", omni_country_name("KR"));
    TEST_ASSERT_EQUAL_STRING("ZZ", omni_country_name("ZZ")); /* unknown: the code itself */
    for (size_t i = 0; i < omni_city_count(); i++) { /* every city's country has a plain-ASCII name */
        const char *n = omni_country_name(omni_city_at(i)->country);
        TEST_ASSERT_TRUE_MESSAGE(strlen(n) > 2, omni_city_at(i)->name);
        for (const char *p = n; *p; p++) TEST_ASSERT_TRUE_MESSAGE(*p >= 0x20 && *p < 0x7f, n);
    }
}

/* ------------------------------------------------------------ persisted form (m4) */

/* A STORE_VERSION 1 blob byte for byte as Stage 1 wrote it: uint16 version 1, then the raw omni_settings_t of that
 * layout (city index, brightness, sleep_s, h24, sound_on, volume, reduced_motion, reveal_style, carousel[16],
 * carousel_len; 26 bytes, no padding). */
static size_t v1_blob(uint8_t *b, uint16_t city, uint8_t h24, uint8_t sound, uint8_t rm)
{
    memset(b, 0, OMNI_SETTINGS_V1_BLOB_SIZE);
    b[0] = 1; b[1] = 0;
    b[2] = (uint8_t)(city & 0xff); b[3] = (uint8_t)(city >> 8);
    b[4] = 70; b[5] = 15; b[6] = h24; b[7] = sound; b[8] = 30; b[9] = rm; b[10] = OMNI_REVEAL_FLIP;
    for (uint8_t i = 0; i < 8; i++) b[11 + i] = i;
    b[27] = 8;
    return OMNI_SETTINGS_V1_BLOB_SIZE;
}

static void test_v1_blob_layout_matches_the_struct(void)
{
    TEST_ASSERT_EQUAL_size_t(28, OMNI_SETTINGS_V1_BLOB_SIZE);
    TEST_ASSERT_EQUAL_size_t(OMNI_SETTINGS_V1_BLOB_SIZE, sizeof(uint16_t) + sizeof(omni_settings_t));
}

/* A saved v1 record: Columbus (index 41 in the table v1 was written with), 70 %, 15 s, 12 h, sound on. */
static void test_v1_migrates_the_city_by_index(void)
{
    uint8_t b[OMNI_SETTINGS_V1_BLOB_SIZE];
    omni_settings_t s;
    TEST_ASSERT_EQUAL(OMNI_SETTINGS_MIGRATED_V1, omni_settings_decode(&s, b, v1_blob(b, 41, 0, 1, 0)));
    TEST_ASSERT_EQUAL_STRING("Columbus", omni_city_at(s.city)->name);
    TEST_ASSERT_EQUAL_STRING("US", omni_city_at(s.city)->country);
    TEST_ASSERT_EQUAL_UINT8(70, s.brightness);
    TEST_ASSERT_EQUAL_UINT8(15, s.sleep_s);
    TEST_ASSERT_FALSE(s.h24);
    TEST_ASSERT_TRUE(s.sound_on);
    TEST_ASSERT_EQUAL_UINT8(30, s.volume);
    TEST_ASSERT_FALSE(s.reduced_motion);
    TEST_ASSERT_EQUAL_UINT8(8, s.carousel_len);
    /* re-encoded as version 2 it reads back as the same city */
    uint8_t v2[OMNI_SETTINGS_BLOB_SIZE];
    omni_settings_t t;
    TEST_ASSERT_EQUAL_size_t(OMNI_SETTINGS_BLOB_SIZE, omni_settings_encode(&s, v2));
    TEST_ASSERT_EQUAL(OMNI_SETTINGS_LOADED, omni_settings_decode(&t, v2, sizeof v2));
    TEST_ASSERT_EQUAL_UINT16(s.city, t.city);
}

/* v1 indices are Stage 1's table order (185 cities, unchanged from 77e1124 to 0cfbb17), not the current table's:
 * a city added since (Longyearbyen, R48/C11) must not move the cities sorted after it. Literal v1 indices. */
static void test_v1_indices_use_the_stage1_table(void)
{
    static const struct { uint16_t v1; const char *name, *country; } K[] = {
        {0, "Abidjan", "CI"}, {41, "Columbus", "US"}, {91, "Ljubljana", "SI"}, {92, "London", "GB"},
        {93, "Los Angeles", "US"}, {94, "Louisville", "US"}, {184, "Zurich", "CH"},
    };
    uint8_t b[OMNI_SETTINGS_V1_BLOB_SIZE];
    omni_settings_t s;
    for (size_t i = 0; i < sizeof K / sizeof K[0]; i++) {
        TEST_ASSERT_EQUAL(OMNI_SETTINGS_MIGRATED_V1, omni_settings_decode(&s, b, v1_blob(b, K[i].v1, 0, 1, 0)));
        TEST_ASSERT_EQUAL_STRING_MESSAGE(K[i].name, omni_city_at(s.city)->name, K[i].name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(K[i].country, omni_city_at(s.city)->country, K[i].name);
    }
    /* past the end of the v1 table: the default city */
    omni_settings_decode(&s, b, v1_blob(b, 185, 0, 1, 0));
    TEST_ASSERT_EQUAL_STRING("London", omni_city_at(s.city)->name);
}

static void test_bools_are_normalised_on_load(void)
{
    uint8_t b[OMNI_SETTINGS_V1_BLOB_SIZE];
    omni_settings_t s;
    omni_settings_decode(&s, b, v1_blob(b, 0, 0x02, 0xff, 0x80));
    uint8_t raw[3];
    memcpy(&raw[0], &s.h24, 1);
    memcpy(&raw[1], &s.sound_on, 1);
    memcpy(&raw[2], &s.reduced_motion, 1);
    TEST_ASSERT_EQUAL_UINT8(1, raw[0]);
    TEST_ASSERT_EQUAL_UINT8(1, raw[1]);
    TEST_ASSERT_EQUAL_UINT8(1, raw[2]);
    uint8_t v2[OMNI_SETTINGS_BLOB_SIZE];
    omni_settings_encode(&s, v2);
    v2[40] = 0x40; /* the h24 byte (layout in settings.h) */
    omni_settings_decode(&s, v2, sizeof v2);
    memcpy(&raw[0], &s.h24, 1);
    TEST_ASSERT_EQUAL_UINT8(1, raw[0]);
}

/* The key is name + country: every city round-trips to its own index; a regenerated table that moved the city
 * (simulated here by decoding against the same table after the index changed meaning) is found by name. */
static void test_city_key_round_trips_for_every_city(void)
{
    for (size_t i = 0; i < omni_city_count(); i++) {
        const omni_city_t *c = omni_city_at(i);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)i, omni_city_find_key(c->name, c->country), c->name);
        omni_settings_t s, t;
        omni_settings_defaults(&s);
        s.city = (uint16_t)i;
        uint8_t b[OMNI_SETTINGS_BLOB_SIZE];
        omni_settings_encode(&s, b);
        TEST_ASSERT_EQUAL(OMNI_SETTINGS_LOADED, omni_settings_decode(&t, b, sizeof b));
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(i, t.city, c->name);
    }
    TEST_ASSERT_EQUAL_INT(-1, omni_city_find_key("Columbus", "GB"));
    TEST_ASSERT_EQUAL_INT(-1, omni_city_find_key("Atlantis", "US"));
    TEST_ASSERT_EQUAL_INT(omni_city_find("Columbus"), omni_city_find_key("Columbus", "US"));
}

static void test_unknown_city_key_falls_back_to_the_default(void)
{
    omni_settings_t s, t;
    omni_settings_defaults(&s);
    s.city = (uint16_t)omni_city_find("Tokyo");
    s.brightness = 55;
    uint8_t b[OMNI_SETTINGS_BLOB_SIZE];
    omni_settings_encode(&s, b);
    memcpy(b + 2, "Atlantis\0\0\0", 11); /* a city a later table dropped */
    TEST_ASSERT_EQUAL(OMNI_SETTINGS_CITY_UNKNOWN, omni_settings_decode(&t, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("London", omni_city_at(t.city)->name);
    TEST_ASSERT_EQUAL_UINT8(55, t.brightness); /* the rest is kept */
}

static void test_unusable_blobs_give_the_defaults(void)
{
    omni_settings_t s, d;
    omni_settings_defaults(&d);
    uint8_t b[OMNI_SETTINGS_BLOB_SIZE] = {0};
    TEST_ASSERT_EQUAL(OMNI_SETTINGS_UNUSABLE, omni_settings_decode(&s, b, sizeof b)); /* version 0 */
    TEST_ASSERT_EQUAL_MEMORY(&d, &s, sizeof s);
    omni_settings_encode(&d, b);
    TEST_ASSERT_EQUAL(OMNI_SETTINGS_UNUSABLE, omni_settings_decode(&s, b, sizeof b - 1)); /* size */
    b[0] = 9;
    TEST_ASSERT_EQUAL(OMNI_SETTINGS_UNUSABLE, omni_settings_decode(&s, b, sizeof b)); /* version 9 */
    uint8_t v1[OMNI_SETTINGS_V1_BLOB_SIZE];
    v1_blob(v1, 9999, 0, 1, 0); /* index out of range: validated to the default */
    TEST_ASSERT_EQUAL(OMNI_SETTINGS_MIGRATED_V1, omni_settings_decode(&s, v1, sizeof v1));
    TEST_ASSERT_EQUAL_STRING("London", omni_city_at(s.city)->name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_london);
    RUN_TEST(test_shared_defaults_london);
    RUN_TEST(test_validate_bad_city_london);
    RUN_TEST(test_saved_columbus_kept);
    RUN_TEST(test_settings_defaults_no_carousel);
    RUN_TEST(test_validate_clamps);
    RUN_TEST(test_validate_carousel);
    RUN_TEST(test_city_count_range);
    RUN_TEST(test_every_city_has_posix);
    RUN_TEST(test_country_names);
    RUN_TEST(test_v1_blob_layout_matches_the_struct);
    RUN_TEST(test_v1_migrates_the_city_by_index);
    RUN_TEST(test_v1_indices_use_the_stage1_table);
    RUN_TEST(test_bools_are_normalised_on_load);
    RUN_TEST(test_city_key_round_trips_for_every_city);
    RUN_TEST(test_unknown_city_key_falls_back_to_the_default);
    RUN_TEST(test_unusable_blobs_give_the_defaults);
    return UNITY_END();
}
