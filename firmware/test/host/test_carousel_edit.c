#include "unity.h"
#include "omni/carousel_edit.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* The three themes' real limits and default carousels, copied as data (drift shows as a failing test here):
 *   omni: apps/omni/main/omni_theme.c DEF_CAROUSEL (FACE_OMNI .. FACE_GLOBE_SPIN = ids 0..7), face_count FACE__COUNT 8
 *   rd:   apps/rd/components/rd_core/faces.c RD_DEF_CAROUSEL (comment in rd/faces.h), face_count RD_F__COUNT 14
 *   dw:   apps/dw/components/dw_core/prefs.c DW_DEF_CAROUSEL, face_count DW_F__COUNT 16 (the same count in the generic
 *         and the private build: the overlay changes art, not ids; the default leaves out DW_F_DIGITAL = 6) */
static const uint8_t OMNI_DEF[] = {0, 1, 2, 3, 4, 5, 6, 7};
static const uint8_t RD_DEF[] = {0, 7, 8, 9, 10, 2, 1, 3, 11, 5, 12, 4, 13};
static const uint8_t DW_DEF[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static const omni_theme_limits_t TH[3] = {
    {.face_count = 8, .reveal_count = 3, .reveal_default = 0, .def_len = sizeof OMNI_DEF, .def_carousel = OMNI_DEF},
    {.face_count = 14, .reveal_count = 3, .reveal_default = 0, .def_len = sizeof RD_DEF, .def_carousel = RD_DEF},
    {.face_count = 16, .reveal_count = 3, .reveal_default = 0, .def_len = sizeof DW_DEF, .def_carousel = DW_DEF},
};
#define EACH for (int t = 0; t < 3; t++)

static omni_carousel_t fresh(const omni_theme_limits_t *lim)
{
    omni_carousel_t c = {0};
    c.face = lim->def_carousel[0];
    omni_carousel_restore(&c, lim);
    return c;
}

void test_carousel_edit_toggle_ends(void)
{
    EACH {
        const omni_theme_limits_t *l = &TH[t];
        omni_carousel_t c = fresh(l);
        uint8_t n = c.len, first = c.ids[0], last = c.ids[n - 1];
        TEST_ASSERT_TRUE(omni_carousel_toggle(&c, first, l));
        TEST_ASSERT_EQUAL_UINT8(n - 1, c.len);
        TEST_ASSERT_TRUE(omni_carousel_toggle(&c, last, l));
        TEST_ASSERT_EQUAL_UINT8(n - 2, c.len);
        TEST_ASSERT_TRUE(omni_carousel_toggle(&c, last, l));    /* shown again: appended at the end */
        TEST_ASSERT_EQUAL_UINT8(last, c.ids[c.len - 1]);
        TEST_ASSERT_TRUE(omni_carousel_toggle(&c, first, l));
        TEST_ASSERT_EQUAL_UINT8(first, c.ids[c.len - 1]);
        TEST_ASSERT_EQUAL_UINT8(n, c.len);
        TEST_ASSERT_FALSE(omni_carousel_toggle(&c, l->face_count, l));   /* not a face of this theme */
        TEST_ASSERT_FALSE(omni_carousel_toggle(&c, 255, l));
        TEST_ASSERT_EQUAL_UINT8(n, c.len);
    }
    /* dw: show everything (16), then there is nothing left to show; a 17th is refused by the bound itself */
    omni_carousel_t c = fresh(&TH[2]);
    TEST_ASSERT_TRUE(omni_carousel_toggle(&c, 6, &TH[2]));
    TEST_ASSERT_EQUAL_UINT8(16, c.len);
    omni_theme_limits_t wide = TH[2];
    wide.face_count = 255;
    TEST_ASSERT_FALSE(omni_carousel_toggle(&c, 16, &wide));
    TEST_ASSERT_EQUAL_UINT8(16, c.len);
}

void test_carousel_edit_last_shown(void)
{
    EACH {
        const omni_theme_limits_t *l = &TH[t];
        omni_carousel_t c = fresh(l);
        while (c.len > 1) TEST_ASSERT_TRUE(omni_carousel_toggle(&c, c.ids[0], l));
        uint8_t only = c.ids[0];
        TEST_ASSERT_FALSE(omni_carousel_toggle(&c, only, l));
        TEST_ASSERT_EQUAL_UINT8(1, c.len);
        TEST_ASSERT_EQUAL_UINT8(only, c.ids[0]);
        TEST_ASSERT_EQUAL_UINT8(only, c.face);
    }
}

void test_carousel_edit_move_ends(void)
{
    EACH {
        const omni_theme_limits_t *l = &TH[t];
        omni_carousel_t c = fresh(l);
        omni_carousel_t before = c;
        TEST_ASSERT_FALSE(omni_carousel_move(&c, c.ids[0], -1));
        TEST_ASSERT_FALSE(omni_carousel_move(&c, c.ids[c.len - 1], +1));
        TEST_ASSERT_FALSE(omni_carousel_move(&c, c.ids[1], 0));
        TEST_ASSERT_FALSE(omni_carousel_move(&c, c.ids[1], 2));
        TEST_ASSERT_EQUAL_MEMORY(&before, &c, sizeof c);
        uint8_t a = c.ids[1], b = c.ids[2];
        TEST_ASSERT_TRUE(omni_carousel_move(&c, a, +1));
        TEST_ASSERT_EQUAL_UINT8(b, c.ids[1]);
        TEST_ASSERT_EQUAL_UINT8(a, c.ids[2]);
        TEST_ASSERT_TRUE(omni_carousel_move(&c, a, -1));
        TEST_ASSERT_EQUAL_MEMORY(before.ids, c.ids, OMNI_CAROUSEL_MAX);
        /* a hidden id cannot move */
        omni_carousel_toggle(&c, a, l);
        TEST_ASSERT_FALSE(omni_carousel_move(&c, a, +1));
        TEST_ASSERT_FALSE(omni_carousel_move(&c, a, -1));
    }
}

void test_carousel_edit_face_follows(void)
{
    EACH {
        const omni_theme_limits_t *l = &TH[t];
        omni_carousel_t c = fresh(l);
        c.face = c.ids[2];
        uint8_t f = c.face;
        TEST_ASSERT_TRUE(omni_carousel_move(&c, f, +1));
        TEST_ASSERT_EQUAL_UINT8(f, c.face);
        TEST_ASSERT_EQUAL_UINT8(3, omni_carousel_face_idx(&c));
        TEST_ASSERT_TRUE(omni_carousel_move(&c, f, -1));
        TEST_ASSERT_TRUE(omni_carousel_move(&c, f, -1));
        TEST_ASSERT_EQUAL_UINT8(1, omni_carousel_face_idx(&c));
        /* moving another face past it shifts its index, not its id */
        TEST_ASSERT_TRUE(omni_carousel_move(&c, c.ids[0], +1));
        TEST_ASSERT_EQUAL_UINT8(f, c.face);
        TEST_ASSERT_EQUAL_UINT8(0, omni_carousel_face_idx(&c));
    }
}

void test_carousel_edit_hide_current(void)
{
    EACH {
        const omni_theme_limits_t *l = &TH[t];
        omni_carousel_t c = fresh(l);
        c.face = c.ids[1];                      /* the slot's successor takes over */
        uint8_t next = c.ids[2];
        TEST_ASSERT_TRUE(omni_carousel_toggle(&c, c.face, l));
        TEST_ASSERT_EQUAL_UINT8(next, c.face);
        TEST_ASSERT_EQUAL_UINT8(1, omni_carousel_face_idx(&c));
        c.face = c.ids[c.len - 1];              /* last: the one before it */
        uint8_t prev = c.ids[c.len - 2];
        TEST_ASSERT_TRUE(omni_carousel_toggle(&c, c.face, l));
        TEST_ASSERT_EQUAL_UINT8(prev, c.face);
        c.face = c.ids[0];                      /* first: the new first */
        next = c.ids[1];
        TEST_ASSERT_TRUE(omni_carousel_toggle(&c, c.face, l));
        TEST_ASSERT_EQUAL_UINT8(next, c.face);
        /* hiding another face leaves the current one */
        uint8_t keep = c.face;
        TEST_ASSERT_TRUE(omni_carousel_toggle(&c, c.ids[c.len - 1], l));
        TEST_ASSERT_EQUAL_UINT8(keep, c.face);
    }
}

void test_carousel_edit_restore(void)
{
    EACH {
        const omni_theme_limits_t *l = &TH[t];
        omni_carousel_t c = fresh(l), d;
        omni_carousel_toggle(&c, c.ids[0], l);
        omni_carousel_move(&c, c.ids[1], +1);
        c.face = c.ids[3];
        uint8_t f = c.face;
        omni_carousel_restore(&c, l);
        TEST_ASSERT_EQUAL_UINT8(l->def_len, c.len);
        TEST_ASSERT_EQUAL_MEMORY(l->def_carousel, c.ids, l->def_len);
        TEST_ASSERT_EQUAL_UINT8(f, c.face);     /* in the default: kept */
        /* a face the default lacks falls back to def_carousel[0] */
        d = fresh(l);
        if (t == 2) {                           /* dw: Digital (6) is outside the default */
            omni_carousel_toggle(&d, 6, l);
            d.face = 6;
            omni_carousel_restore(&d, l);
            TEST_ASSERT_EQUAL_UINT8(l->def_carousel[0], d.face);
        }
        d.face = 200;
        omni_carousel_restore(&d, l);
        TEST_ASSERT_EQUAL_UINT8(l->def_carousel[0], d.face);
        TEST_ASSERT_EQUAL_UINT8(0, omni_carousel_face_idx(&d));
    }
}

void test_carousel_edit_ring_order(void)
{
    EACH {
        const omni_theme_limits_t *l = &TH[t];
        omni_carousel_t c = fresh(l);
        omni_carousel_toggle(&c, c.ids[4], l);
        omni_carousel_toggle(&c, c.ids[0], l);
        uint8_t out[OMNI_CAROUSEL_MAX], shown = 99;
        int n = omni_carousel_ring(&c, l->face_count, out, &shown);
        TEST_ASSERT_EQUAL_INT(l->face_count, n);
        TEST_ASSERT_EQUAL_UINT8(c.len, shown);
        TEST_ASSERT_EQUAL_MEMORY(c.ids, out, c.len);
        for (int i = c.len + 1; i < n; i++) TEST_ASSERT_TRUE(out[i] > out[i - 1]);   /* hidden ascending */
        uint8_t seen[OMNI_CAROUSEL_MAX] = {0};
        for (int i = 0; i < n; i++) { TEST_ASSERT_TRUE(out[i] < l->face_count); seen[out[i]]++; }
        for (int i = 0; i < l->face_count; i++) TEST_ASSERT_EQUAL_UINT8(1, seen[i]);
        for (int i = c.len; i < n; i++) {
            int in = 0;
            for (int k = 0; k < c.len; k++) in |= c.ids[k] == out[i];
            TEST_ASSERT_FALSE(in);
        }
    }
    /* nothing hidden: the ring is the carousel */
    omni_carousel_t c = fresh(&TH[0]);
    uint8_t out[OMNI_CAROUSEL_MAX], shown;
    TEST_ASSERT_EQUAL_INT(8, omni_carousel_ring(&c, 8, out, &shown));
    TEST_ASSERT_EQUAL_UINT8(8, shown);
}

/* xorshift32: seeded, so a failure repeats */
static uint32_t rng(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

void test_carousel_edit_validates(void)
{
    EACH {
        const omni_theme_limits_t *l = &TH[t];
        uint32_t seed = 0x1234567u + (uint32_t)t;
        omni_carousel_t c = fresh(l);
        for (int step = 0; step < 500; step++) {
            uint32_t r = rng(&seed);
            uint8_t id = (uint8_t)((r >> 8) % l->face_count);
            switch (r % 3) {
            case 0: omni_carousel_toggle(&c, id, l); break;
            case 1: omni_carousel_move(&c, id, (r >> 16) & 1 ? 1 : -1); break;
            default: if (((r >> 20) & 31) == 0) omni_carousel_restore(&c, l); break;
            }
            omni_local_t lo;
            omni_local_defaults(&lo, l);
            lo.carousel_len = c.len;
            memcpy(lo.carousel, c.ids, sizeof lo.carousel);
            lo.face_idx = omni_carousel_face_idx(&c);
            omni_local_t was = lo;
            TEST_ASSERT_TRUE(omni_local_validate(&lo, l));
            TEST_ASSERT_EQUAL_UINT8(was.carousel_len, lo.carousel_len);
            TEST_ASSERT_EQUAL_MEMORY(was.carousel, lo.carousel, OMNI_CAROUSEL_MAX);
            TEST_ASSERT_EQUAL_UINT8(c.face, lo.carousel[lo.face_idx]);
        }
    }
}

void test_carousel_edit_alarm_faces_hideable(void)
{
    omni_carousel_t c = fresh(&TH[0]);          /* the Omni's Timer is id 6 (O2, B4) */
    c.face = 0;
    TEST_ASSERT_TRUE(omni_carousel_toggle(&c, 6, &TH[0]));
    TEST_ASSERT_EQUAL_UINT8(7, c.len);
    TEST_ASSERT_EQUAL_UINT8(0, c.face);
    uint8_t out[OMNI_CAROUSEL_MAX], shown;
    omni_carousel_ring(&c, 8, out, &shown);
    TEST_ASSERT_EQUAL_UINT8(6, out[7]);
    TEST_ASSERT_TRUE(omni_carousel_toggle(&c, 6, &TH[0]));
    TEST_ASSERT_EQUAL_UINT8(6, c.ids[7]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_carousel_edit_toggle_ends);
    RUN_TEST(test_carousel_edit_last_shown);
    RUN_TEST(test_carousel_edit_move_ends);
    RUN_TEST(test_carousel_edit_face_follows);
    RUN_TEST(test_carousel_edit_hide_current);
    RUN_TEST(test_carousel_edit_restore);
    RUN_TEST(test_carousel_edit_ring_order);
    RUN_TEST(test_carousel_edit_validates);
    RUN_TEST(test_carousel_edit_alarm_faces_hideable);
    return UNITY_END();
}
