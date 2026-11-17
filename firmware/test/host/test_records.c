#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "omni/cities.h"
#include "omni/local.h"
#include "omni/record.h"
#include "omni/settings.h"
#include "omni/shared.h"

void setUp(void) {}
void tearDown(void) {}

/* the Omni theme's limits as Task 5 will declare them: 8 faces, 3 reveals, the default order */
static const uint8_t k_def[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const omni_theme_limits_t k_lim = { 8, 3, 0, sizeof k_def, k_def };

static size_t read_hex(const char *name, uint8_t *out, size_t cap)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", FIXTURES_DIR, name);
    FILE *f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    size_t n = 0;
    unsigned v;
    while (fscanf(f, "%x", &v) == 1) {
        TEST_ASSERT_TRUE(n < cap);
        out[n++] = (uint8_t)v;
    }
    fclose(f);
    return n;
}

static void owner_shared(omni_shared_t *s)
{
    omni_shared_defaults(s);
    s->city = (uint16_t)omni_city_find_key("Columbus", "US");
    s->brightness = 45; s->sleep_s = 15; s->h24 = false; s->sound_on = true; s->volume = 30; s->reduced_motion = false;
}

static void owner_local(omni_local_t *l)
{
    omni_local_defaults(l, &k_lim);
    l->reveal_style = 0;
    const uint8_t c[] = { 0, 1, 2, 3, 7 };
    memset(l->carousel, 0, sizeof l->carousel);
    memcpy(l->carousel, c, sizeof c);
    l->carousel_len = 5;
    l->face_idx = 0;
}

static void test_shared_golden(void)
{
    uint8_t want[OMNI_REC_MAX], got[OMNI_REC_MAX];
    size_t wn = read_hex("shared_v1.hex", want, sizeof want);
    TEST_ASSERT_EQUAL(46, wn);
    const uint8_t head[] = { 0x01, 0x00, 0x2e, 0x00 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(head, want, 4);
    omni_shared_t s;
    owner_shared(&s);
    TEST_ASSERT_EQUAL(46, omni_shared_encode(&s, got, sizeof got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, wn);
    omni_shared_t d;
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, omni_shared_decode(&d, want, wn));
    TEST_ASSERT_EQUAL_UINT16(s.city, d.city);
    TEST_ASSERT_EQUAL_UINT8(45, d.brightness);
    TEST_ASSERT_EQUAL_UINT8(15, d.sleep_s);
    TEST_ASSERT_FALSE(d.h24);
    TEST_ASSERT_TRUE(d.sound_on);
    TEST_ASSERT_EQUAL_UINT8(30, d.volume);
    TEST_ASSERT_FALSE(d.reduced_motion);
    TEST_ASSERT_EQUAL_UINT16(0, d.tail_len);
}

static void test_local_golden(void)
{
    uint8_t want[OMNI_REC_MAX], got[OMNI_REC_MAX];
    size_t wn = read_hex("local_v1.hex", want, sizeof want);
    TEST_ASSERT_EQUAL(23, wn);
    const uint8_t head[] = { 0x01, 0x00, 0x17, 0x00 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(head, want, 4);
    omni_local_t l;
    owner_local(&l);
    TEST_ASSERT_EQUAL(23, omni_local_encode(&l, got, sizeof got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, wn);
    omni_local_t d;
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, omni_local_decode(&d, want, wn, &k_lim));
    TEST_ASSERT_EQUAL_UINT8(0, d.reveal_style);
    TEST_ASSERT_EQUAL_UINT8(5, d.carousel_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(l.carousel, d.carousel, OMNI_CAROUSEL_MAX);
    TEST_ASSERT_EQUAL_UINT8(0, d.face_idx);
}

/* Stage 5: a saved t_omni/local as a Stage 4b watch holds it (Stage 4b Task 9), carousel [0..7], read with the Omni's
 * limits (8 faces, 3 reveals). Stage 5 adds no field: it loads whole and re-encodes byte for byte. */
static void test_local_owner_stage5(void)
{
    const uint8_t owner[23] = { 0x01, 0x00, 0x17, 0x00, 0x00, 0x08, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    omni_local_t d;
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, omni_local_decode(&d, owner, sizeof owner, &k_lim));
    TEST_ASSERT_EQUAL_UINT8(8, d.carousel_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_def, d.carousel, 8);
    TEST_ASSERT_EQUAL_UINT8(0, d.reveal_style);
    TEST_ASSERT_EQUAL_UINT8(0, d.face_idx);
    TEST_ASSERT_EQUAL_UINT16(0, d.tail_len);
    uint8_t got[OMNI_REC_MAX];
    TEST_ASSERT_EQUAL(23, omni_local_encode(&d, got, sizeof got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(owner, got, sizeof owner);
}

static void test_shared_tail_kept(void)
{
    uint8_t in[56], out[OMNI_REC_MAX];
    read_hex("shared_v1.hex", in, 46);
    in[2] = 56;
    for (int i = 0; i < 10; i++) in[46 + i] = (uint8_t)(0xA0 + i);
    omni_shared_t s;
    TEST_ASSERT_EQUAL(OMNI_REC_TAIL_KEPT, omni_shared_decode(&s, in, sizeof in));
    TEST_ASSERT_EQUAL_UINT16(10, s.tail_len);
    s.brightness = 80;
    TEST_ASSERT_EQUAL(56, omni_shared_encode(&s, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&in[46], &out[46], 10);
    TEST_ASSERT_EQUAL_UINT8(56, out[2]);
    TEST_ASSERT_EQUAL_UINT8(80, out[40]);
    TEST_ASSERT_EQUAL(0, omni_shared_encode(&s, out, 55)); /* cap short */
}

static void test_local_tail_kept(void)
{
    uint8_t in[30], out[OMNI_REC_MAX];
    read_hex("local_v1.hex", in, 23);
    in[2] = 30;
    for (int i = 0; i < 7; i++) in[23 + i] = (uint8_t)(0x50 + i);
    omni_local_t l;
    TEST_ASSERT_EQUAL(OMNI_REC_TAIL_KEPT, omni_local_decode(&l, in, sizeof in, &k_lim));
    TEST_ASSERT_EQUAL(30, omni_local_encode(&l, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 30);
}

static void test_shared_unknown_version(void)
{
    uint8_t in[46];
    read_hex("shared_v1.hex", in, 46);
    in[0] = 2;
    omni_shared_t s, d;
    omni_shared_defaults(&d);
    TEST_ASSERT_EQUAL(OMNI_REC_UNKNOWN_VERSION, omni_shared_decode(&s, in, sizeof in));
    TEST_ASSERT_EQUAL_UINT8(d.brightness, s.brightness);
    TEST_ASSERT_EQUAL_UINT16(d.city, s.city);
    TEST_ASSERT_EQUAL_UINT16(0, s.tail_len);
}

static void test_shared_short_or_lying_size(void)
{
    uint8_t in[OMNI_REC_MAX] = { 0 };
    omni_shared_t s;
    read_hex("shared_v1.hex", in, 46);
    TEST_ASSERT_EQUAL(OMNI_REC_UNUSABLE, omni_shared_decode(&s, in, 45));
    in[2] = 47; /* the field says 47, n is 46 */
    TEST_ASSERT_EQUAL(OMNI_REC_UNUSABLE, omni_shared_decode(&s, in, 46));
    uint8_t big[300] = { 1, 0, (uint8_t)(300 & 0xff), (uint8_t)(300 >> 8) }; /* a matching size, over the cap */
    TEST_ASSERT_EQUAL(OMNI_REC_UNUSABLE, omni_shared_decode(&s, big, sizeof big));
    /* a self-consistent record whose size is below V1 */
    const uint8_t tiny[] = { 1, 0, 10, 0, 0, 0, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL(OMNI_REC_UNUSABLE, omni_shared_decode(&s, tiny, sizeof tiny));
    uint16_t v, z;
    TEST_ASSERT_FALSE(omni_rec_header(in, 3, &v, &z));
    TEST_ASSERT_TRUE(omni_rec_header(tiny, sizeof tiny, &v, &z));
    TEST_ASSERT_EQUAL_UINT16(1, v);
    TEST_ASSERT_EQUAL_UINT16(10, z);
    TEST_ASSERT_EQUAL(OMNI_REC_UNUSABLE, omni_local_decode((omni_local_t[1]){ 0 }, tiny, sizeof tiny, &k_lim));
}

static void test_shared_city_unknown(void)
{
    uint8_t in[46];
    read_hex("shared_v1.hex", in, 46);
    memset(&in[4], 0, 32);
    memcpy(&in[4], "Atlantis", 8);
    omni_shared_t s, d;
    omni_shared_defaults(&d);
    TEST_ASSERT_EQUAL(OMNI_REC_CITY_UNKNOWN, omni_shared_decode(&s, in, sizeof in));
    TEST_ASSERT_EQUAL_UINT16(d.city, s.city);
    TEST_ASSERT_EQUAL_UINT8(45, s.brightness);
    TEST_ASSERT_EQUAL_UINT8(30, s.volume);
    TEST_ASSERT_TRUE(s.city_kept);

    /* I1: another field changed, the stored city bytes are re-encoded verbatim, never the default's */
    s.brightness = 80;
    uint8_t out[OMNI_REC_MAX];
    TEST_ASSERT_EQUAL(46, omni_shared_encode(&s, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&in[4], &out[4], OMNI_SHARED_CITY_BYTES);
    TEST_ASSERT_EQUAL_UINT8(80, out[40]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&in[41], &out[41], 5);
    /* and it decodes again as the same unknown city */
    TEST_ASSERT_EQUAL(OMNI_REC_CITY_UNKNOWN, omni_shared_decode(&d, out, 46));

    /* choosing another city in RAM replaces it */
    s.city = (uint16_t)omni_city_find("Columbus");
    TEST_ASSERT_EQUAL(46, omni_shared_encode(&s, out, sizeof out));
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, omni_shared_decode(&d, out, 46));
    TEST_ASSERT_EQUAL_STRING("Columbus", omni_city_at(d.city)->name);
    TEST_ASSERT_FALSE(d.city_kept);
}

static void test_split_clears_kept_city(void)
{
    uint8_t in[46];
    read_hex("shared_v1.hex", in, 46);
    memset(&in[4], 0, 32);
    memcpy(&in[4], "Atlantis", 8);
    omni_shared_t sh;
    TEST_ASSERT_EQUAL(OMNI_REC_CITY_UNKNOWN, omni_shared_decode(&sh, in, sizeof in));
    TEST_ASSERT_TRUE(sh.city_kept);
    omni_settings_t s;
    omni_settings_defaults(&s);
    omni_local_t lo;
    omni_local_defaults(&lo, &k_lim);

    /* another city chosen: the kept city is dropped */
    s.city = (uint16_t)omni_city_find("Columbus");
    omni_settings_split(&s, &sh, &lo);
    TEST_ASSERT_FALSE(sh.city_kept);

    /* then the default city: the encode carries its name and country, not the Atlantis bytes */
    omni_shared_t d;
    omni_shared_defaults(&d);
    s.city = d.city;
    omni_settings_split(&s, &sh, &lo);
    uint8_t out[OMNI_REC_MAX];
    TEST_ASSERT_EQUAL(46, omni_shared_encode(&sh, out, sizeof out));
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, omni_shared_decode(&d, out, 46));
    TEST_ASSERT_EQUAL_UINT16(sh.city, d.city);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(&in[4], &out[4], 8));
}

static void test_shared_bools_normalised_and_validated(void)
{
    uint8_t in[46];
    read_hex("shared_v1.hex", in, 46);
    in[42] = 7; in[43] = 0; in[45] = 200; in[40] = 0; in[41] = 255; in[44] = 250;
    omni_shared_t s;
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, omni_shared_decode(&s, in, sizeof in));
    TEST_ASSERT_TRUE(s.h24);
    TEST_ASSERT_FALSE(s.sound_on);
    TEST_ASSERT_TRUE(s.reduced_motion);
    TEST_ASSERT_EQUAL_UINT8(5, s.brightness);
    TEST_ASSERT_EQUAL_UINT8(60, s.sleep_s);
    TEST_ASSERT_EQUAL_UINT8(100, s.volume);
}

static void test_local_validate_clamps(void)
{
    omni_local_t l;
    owner_local(&l);
    const uint8_t a[] = { 0, 1, 9, 3 };
    memcpy(l.carousel, a, sizeof a); l.carousel_len = 4;
    TEST_ASSERT_FALSE(omni_local_validate(&l, &k_lim)); /* 9 >= face_count */
    const uint8_t a_want[] = { 0, 1, 3 };
    TEST_ASSERT_EQUAL_UINT8(3, l.carousel_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a_want, l.carousel, 3);

    const uint8_t b[] = { 0, 1, 1, 3 };
    memcpy(l.carousel, b, sizeof b); l.carousel_len = 4;
    TEST_ASSERT_FALSE(omni_local_validate(&l, &k_lim));
    TEST_ASSERT_EQUAL_UINT8(3, l.carousel_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a_want, l.carousel, 3);

    l.carousel_len = 0;
    TEST_ASSERT_FALSE(omni_local_validate(&l, &k_lim));
    TEST_ASSERT_EQUAL_UINT8(8, l.carousel_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_def, l.carousel, 8);

    l.reveal_style = 3; /* >= reveal_count */
    TEST_ASSERT_FALSE(omni_local_validate(&l, &k_lim));
    TEST_ASSERT_EQUAL_UINT8(0, l.reveal_style);

    l.face_idx = 8; /* >= len */
    TEST_ASSERT_FALSE(omni_local_validate(&l, &k_lim));
    TEST_ASSERT_EQUAL_UINT8(0, l.face_idx);

    l.carousel_len = 200; /* past the array */
    TEST_ASSERT_FALSE(omni_local_validate(&l, &k_lim));
    TEST_ASSERT_TRUE(l.carousel_len <= OMNI_CAROUSEL_MAX);

    omni_local_defaults(&l, &k_lim);
    TEST_ASSERT_TRUE(omni_local_validate(&l, &k_lim));
}

static void test_split_join_roundtrip(void)
{
    omni_settings_t s, r;
    omni_shared_t sh;
    omni_local_t lo;
    omni_settings_defaults(&s);
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            s.city = (uint16_t)omni_city_find_key("Columbus", "US");
            s.brightness = 45; s.sleep_s = 15; s.volume = 30; s.reveal_style = 0;
            const uint8_t c[] = { 0, 1, 2, 3, 7 };
            memset(s.carousel, 0, sizeof s.carousel);
            memcpy(s.carousel, c, sizeof c);
            s.carousel_len = 5;
        }
        omni_settings_split(&s, &sh, &lo);
        memset(&r, 0xAA, sizeof r);
        omni_settings_join(&sh, &lo, &r);
        TEST_ASSERT_EQUAL_UINT16(s.city, r.city);
        TEST_ASSERT_EQUAL_UINT8(s.brightness, r.brightness);
        TEST_ASSERT_EQUAL_UINT8(s.sleep_s, r.sleep_s);
        TEST_ASSERT_EQUAL(s.h24, r.h24);
        TEST_ASSERT_EQUAL(s.sound_on, r.sound_on);
        TEST_ASSERT_EQUAL_UINT8(s.volume, r.volume);
        TEST_ASSERT_EQUAL(s.reduced_motion, r.reduced_motion);
        TEST_ASSERT_EQUAL_UINT8(s.reveal_style, r.reveal_style);
        TEST_ASSERT_EQUAL_UINT8(s.carousel_len, r.carousel_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(s.carousel, r.carousel, OMNI_CAROUSEL_MAX);
    }
}

static void test_join_leaves_tails_alone(void)
{
    omni_shared_t sh;
    omni_local_t lo;
    omni_shared_defaults(&sh);
    omni_local_defaults(&lo, &k_lim);
    sh.tail_len = 3; sh.tail[0] = 9;
    lo.tail_len = 2; lo.tail[1] = 8;
    omni_settings_t s;
    omni_settings_defaults(&s);
    omni_settings_split(&s, &sh, &lo);
    TEST_ASSERT_EQUAL_UINT16(3, sh.tail_len);
    TEST_ASSERT_EQUAL_UINT8(9, sh.tail[0]);
    TEST_ASSERT_EQUAL_UINT16(2, lo.tail_len);
    TEST_ASSERT_EQUAL_UINT8(8, lo.tail[1]);
}

static void test_local_unknown_version(void)
{
    uint8_t in[23];
    read_hex("local_v1.hex", in, 23);
    in[0] = 2;
    omni_local_t l, d;
    omni_local_defaults(&d, &k_lim);
    TEST_ASSERT_EQUAL(OMNI_REC_UNKNOWN_VERSION, omni_local_decode(&l, in, sizeof in, &k_lim));
    TEST_ASSERT_EQUAL_UINT8(d.carousel_len, l.carousel_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(d.carousel, l.carousel, OMNI_CAROUSEL_MAX);
    TEST_ASSERT_EQUAL_UINT16(0, l.tail_len);
}

static void test_local_validate_zeroes_past_len(void)
{
    omni_local_t l;
    owner_local(&l);
    memset(&l.carousel[5], 0x5A, OMNI_CAROUSEL_MAX - 5);
    TEST_ASSERT_TRUE(omni_local_validate(&l, &k_lim));
    for (int i = 5; i < OMNI_CAROUSEL_MAX; i++) TEST_ASSERT_EQUAL_UINT8(0, l.carousel[i]);
    /* a record with stale bytes past its length re-encodes to the golden bytes */
    uint8_t in[23], want[23], out[OMNI_REC_MAX];
    read_hex("local_v1.hex", want, 23);
    memcpy(in, want, 23);
    memset(&in[11], 0x5A, 11); /* carousel[5..15] */
    omni_local_t d;
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, omni_local_decode(&d, in, sizeof in, &k_lim));
    TEST_ASSERT_EQUAL(23, omni_local_encode(&d, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 23);
}

static void test_record_size_boundaries(void)
{
    uint8_t in[257];
    memset(in, 0, sizeof in);
    omni_shared_t s;
    read_hex("shared_v1.hex", in, 46);
    in[2] = 0x00; in[3] = 0x01; /* 256 */
    TEST_ASSERT_EQUAL(OMNI_REC_TAIL_KEPT, omni_shared_decode(&s, in, 256));
    TEST_ASSERT_EQUAL_UINT16(256 - OMNI_SHARED_V1_SIZE, s.tail_len);
    uint8_t out[OMNI_REC_MAX];
    TEST_ASSERT_EQUAL(256, omni_shared_encode(&s, out, sizeof out));
    in[2] = 0x01; in[3] = 0x01; /* 257 */
    TEST_ASSERT_EQUAL(OMNI_REC_UNUSABLE, omni_shared_decode(&s, in, 257));
    uint8_t lin[257] = { 1, 0, 0x00, 0x01 };
    omni_local_t l;
    TEST_ASSERT_EQUAL(OMNI_REC_TAIL_KEPT, omni_local_decode(&l, lin, 256, &k_lim));
    lin[2] = 0x01;
    TEST_ASSERT_EQUAL(OMNI_REC_UNUSABLE, omni_local_decode(&l, lin, 257, &k_lim));
}

static void test_encode_refuses_oversize_tail(void)
{
    uint8_t out[OMNI_REC_MAX + 8];
    omni_shared_t s;
    owner_shared(&s);
    s.tail_len = (uint16_t)(sizeof s.tail + 1);
    TEST_ASSERT_EQUAL(0, omni_shared_encode(&s, out, sizeof out));
    omni_local_t l;
    owner_local(&l);
    l.tail_len = (uint16_t)(sizeof l.tail + 1);
    TEST_ASSERT_EQUAL(0, omni_local_encode(&l, out, sizeof out));
    l.tail_len = sizeof l.tail; /* exactly full is fine */
    TEST_ASSERT_EQUAL(OMNI_REC_MAX, omni_local_encode(&l, out, sizeof out));
}

static void test_local_lying_size(void)
{
    uint8_t in[24];
    read_hex("local_v1.hex", in, 23);
    omni_local_t l;
    in[2] = 24; /* says 24, n is 23 */
    TEST_ASSERT_EQUAL(OMNI_REC_UNUSABLE, omni_local_decode(&l, in, 23, &k_lim));
    in[2] = 23; /* says 23, n is 24 */
    TEST_ASSERT_EQUAL(OMNI_REC_UNUSABLE, omni_local_decode(&l, in, 24, &k_lim));
    in[2] = 22; /* below V1, self-consistent */
    TEST_ASSERT_EQUAL(OMNI_REC_UNUSABLE, omni_local_decode(&l, in, 22, &k_lim));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_shared_golden);
    RUN_TEST(test_local_golden);
    RUN_TEST(test_local_owner_stage5);
    RUN_TEST(test_shared_tail_kept);
    RUN_TEST(test_local_tail_kept);
    RUN_TEST(test_shared_unknown_version);
    RUN_TEST(test_shared_short_or_lying_size);
    RUN_TEST(test_shared_city_unknown);
    RUN_TEST(test_shared_bools_normalised_and_validated);
    RUN_TEST(test_local_validate_clamps);
    RUN_TEST(test_local_unknown_version);
    RUN_TEST(test_local_validate_zeroes_past_len);
    RUN_TEST(test_record_size_boundaries);
    RUN_TEST(test_encode_refuses_oversize_tail);
    RUN_TEST(test_local_lying_size);
    RUN_TEST(test_split_join_roundtrip);
    RUN_TEST(test_join_leaves_tails_alone);
    RUN_TEST(test_split_clears_kept_city);
    return UNITY_END();
}
