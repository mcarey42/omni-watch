#include "unity.h"
#include "omni/rects.h"
#include <stdint.h>
#include <string.h>

/* omni/rects.h (Stage 3 Task 7): the dial view's rect lists. omni_rects_disjoint carries the two-core race fix (both
 * cores render chunks of one list at once, so no pixel may be in two rects): every output must be pairwise disjoint and
 * cover exactly the input's pixels, checked on a pixel grid, for overlapping, nested, touching and identical rects. */

void setUp(void) {}
void tearDown(void) {}

#define G 96   /* the grid the rects live on */

static uint32_t g_seed = 0x2545f491u;
static int rnd(int n)
{
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return (int)(g_seed % (uint32_t)n);
}

static omni_rect_t R(int x0, int y0, int x1, int y1) { return (omni_rect_t){ (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1 }; }

/* Each grid pixel's count of rects covering it. */
static void cover(const omni_rect_t *r, int n, uint8_t c[G][G])
{
    memset(c, 0, (size_t)G * G);
    for (int i = 0; i < n; i++)
        for (int y = r[i].y0; y < r[i].y1; y++)
            for (int x = r[i].x0; x < r[i].x1; x++) c[y][x]++;
}

/* out is pairwise disjoint and covers exactly the pixels of in (or, exact false, a superset of them). */
static void check(const omni_rects_t *in, const omni_rects_t *out, bool exact, const char *what)
{
    static uint8_t ci[G][G], co[G][G];
    cover(in->r, in->n, ci);
    cover(out->r, out->n, co);
    for (int i = 0; i < out->n; i++) TEST_ASSERT_TRUE_MESSAGE(omni_rect_area(&out->r[i]) > 0, what);
    for (int y = 0; y < G; y++)
        for (int x = 0; x < G; x++) {
            TEST_ASSERT_TRUE_MESSAGE(co[y][x] <= 1, what);   /* disjoint */
            if (exact) TEST_ASSERT_EQUAL_INT_MESSAGE(ci[y][x] > 0, co[y][x], what);
            else if (ci[y][x]) TEST_ASSERT_EQUAL_INT_MESSAGE(1, co[y][x], what);
        }
}

static void disjoint_case(const omni_rect_t *r, int n, const char *what)
{
    omni_rects_t in = { .n = n }, out;
    if (n) memcpy(in.r, r, sizeof r[0] * (size_t)n);
    omni_rects_disjoint(&in, &out);
    check(&in, &out, true, what);
}

static void test_disjoint_shapes(void)
{
    const omni_rect_t overlap[] = { R(10, 10, 50, 40), R(30, 20, 70, 60) };
    disjoint_case(overlap, 2, "overlapping");
    const omni_rect_t nested[] = { R(10, 10, 80, 80), R(20, 30, 40, 50) };
    disjoint_case(nested, 2, "nested, big first");
    const omni_rect_t nested2[] = { R(20, 30, 40, 50), R(10, 10, 80, 80) };
    disjoint_case(nested2, 2, "nested, small first");
    const omni_rect_t touch[] = { R(10, 10, 30, 30), R(30, 10, 50, 30), R(10, 30, 30, 50), R(30, 30, 50, 50) };
    disjoint_case(touch, 4, "touching");
    const omni_rect_t same[] = { R(5, 6, 40, 41), R(5, 6, 40, 41), R(5, 6, 40, 41) };
    disjoint_case(same, 3, "identical");
    const omni_rect_t cross[] = { R(40, 0, 56, G), R(0, 40, G, 56) };
    disjoint_case(cross, 2, "a cross (the second cut into two)");
    const omni_rect_t hole[] = { R(30, 30, 60, 60), R(0, 0, G, G), R(31, 31, 59, 59) };
    disjoint_case(hole, 3, "a frame round a hole, then the hole");
    disjoint_case(NULL, 0, "empty");
    /* as the view makes them: the globe box over the disc's bands and a hand's box across both */
    const omni_rect_t dial[] = { R(20, 20, 76, 76), R(24, 20, 72, 28), R(20, 28, 76, 36), R(44, 4, 52, 90) };
    disjoint_case(dial, 4, "the globe box, bands and a hand");
}

static void test_disjoint_random(void)
{
    for (int t = 0; t < 4000; t++) {
        omni_rects_t in = { .n = 1 + rnd(8) }, out;
        for (int i = 0; i < in.n; i++) {
            if (i && rnd(5) == 0) {   /* identical or nested in an earlier one */
                const omni_rect_t p = in.r[rnd(i)];
                in.r[i] = rnd(2) ? p : R(p.x0 + (p.x1 - p.x0) / 4, p.y0, p.x1 - (p.x1 - p.x0) / 4, p.y1);
                continue;
            }
            const int x0 = rnd(G - 1), y0 = rnd(G - 1);
            in.r[i] = R(x0, y0, x0 + 1 + rnd(G - x0), y0 + 1 + rnd(G - y0));
        }
        omni_rects_disjoint(&in, &out);
        check(&in, &out, true, "random lists");
    }
}

/* A list too fragmented for OMNI_RECTS_MAX pieces falls back to its bounding rect: still disjoint, and a superset. */
static void test_disjoint_overflow(void)
{
    omni_rects_t in = { .n = 0 }, out;
    for (int i = 0; i < 40; i++) in.r[in.n++] = R(2 * i, 0, 2 * i + 1, 2 + i);   /* thin columns */
    in.r[in.n++] = R(0, 1, 80, 2);   /* a row across all of them: each column splits */
    for (int i = 0; i < 20; i++) in.r[in.n++] = R(1 + 2 * i, 40 + i, 2 + 2 * i, 60);
    omni_rects_disjoint(&in, &out);   /* 40 columns + the row in 41 pieces + 20 columns: more than OMNI_RECTS_MAX */
    check(&in, &out, false, "overflow");
    TEST_ASSERT_EQUAL_INT(1, out.n);
    TEST_ASSERT_EQUAL_INT(0, out.r[0].x0);
    TEST_ASSERT_EQUAL_INT(0, out.r[0].y0);
    TEST_ASSERT_EQUAL_INT(80, out.r[0].x1);
    TEST_ASSERT_EQUAL_INT(60, out.r[0].y1);
}

static void test_minus(void)
{
    for (int t = 0; t < 4000; t++) {
        const int ax = rnd(G - 1), ay = rnd(G - 1), bx = rnd(G - 1), by = rnd(G - 1);
        const omni_rect_t a = R(ax, ay, ax + 1 + rnd(G - ax), ay + 1 + rnd(G - ay)),
                          b = rnd(8) ? R(bx, by, bx + 1 + rnd(G - bx), by + 1 + rnd(G - by)) : R(0, 0, 0, 0);
        omni_rects_t out = { .n = 0 };
        TEST_ASSERT_TRUE(omni_rect_minus(&a, &b, &out));
        TEST_ASSERT_TRUE(out.n <= 4);
        static uint8_t c[G][G];
        cover(out.r, out.n, c);
        for (int y = 0; y < G; y++)
            for (int x = 0; x < G; x++) {
                const bool in_a = x >= a.x0 && x < a.x1 && y >= a.y0 && y < a.y1,
                           in_b = x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1;
                TEST_ASSERT_EQUAL_INT(in_a && !in_b, c[y][x]);   /* a less b, each pixel once */
            }
    }
    omni_rects_t full = { .n = OMNI_RECTS_MAX };
    const omni_rect_t a = R(0, 0, 10, 10), b = R(2, 2, 4, 4);
    TEST_ASSERT_FALSE(omni_rect_minus(&a, &b, &full));   /* out full */
}

/* Final review M7: a list less a hole (the dial's chord or disc bands less the open loupe), appended to out; a list
 * that does not fit leaves out as it was and says so, for the caller's whole-render fallback. */
static void test_rects_minus_list(void)
{
    omni_rects_t A = { .n = 0 }, out = { .n = 0 };
    for (int y = 0; y < 88; y += 8) A.r[A.n++] = R(10 + y / 4, y, 86 - y / 4, y + 8);   /* bands, as the chord's */
    const omni_rect_t hole = R(30, 20, 60, 50);
    out.r[out.n++] = R(0, 90, 4, 94);   /* already in out: kept */
    TEST_ASSERT_TRUE(omni_rects_minus(&A, &hole, &out));
    static uint8_t c[G][G];
    cover(out.r, out.n, c);
    for (int y = 0; y < G; y++)
        for (int x = 0; x < G; x++) {
            bool in_a = false;
            for (int i = 0; i < A.n; i++) in_a |= x >= A.r[i].x0 && x < A.r[i].x1 && y >= A.r[i].y0 && y < A.r[i].y1;
            const bool in_h = x >= hole.x0 && x < hole.x1 && y >= hole.y0 && y < hole.y1;
            const bool first = x < 4 && y >= 90 && y < 94;
            TEST_ASSERT_EQUAL_INT((in_a && !in_h) || first, c[y][x]);
        }
    /* too many pieces: out unchanged, false */
    omni_rects_t full = { .n = 0 };
    for (int i = 0; i < OMNI_RECTS_MAX - 6; i++) full.r[full.n++] = R(i, 0, i + 1, 1);
    const omni_rects_t before = full;
    TEST_ASSERT_FALSE(omni_rects_minus(&A, &hole, &full));
    TEST_ASSERT_EQUAL_INT(before.n, full.n);
    TEST_ASSERT_EQUAL_MEMORY(before.r, full.r, sizeof full.r[0] * (size_t)full.n);
    /* no hole: the list itself */
    omni_rects_t plain = { .n = 0 };
    const omni_rect_t none = R(0, 0, 0, 0);
    TEST_ASSERT_TRUE(omni_rects_minus(&A, &none, &plain));
    TEST_ASSERT_EQUAL_INT(A.n, plain.n);
    TEST_ASSERT_EQUAL_MEMORY(A.r, plain.r, sizeof A.r[0] * (size_t)A.n);
}

/* omni_rects_add: the pixels added stay covered (merging only ever grows a rect), whatever the merges. */
static void test_add_covers(void)
{
    for (int t = 0; t < 2000; t++) {
        omni_rects_t L = { .n = 0 }, in = { .n = 0 };
        const int n = 1 + rnd(40);
        for (int i = 0; i < n; i++) {
            const int x0 = rnd(G - 1), y0 = rnd(G - 1);
            const omni_rect_t r = R(x0, y0, x0 + 1 + rnd(12), y0 + 1 + rnd(12));
            const omni_rect_t c = R(r.x0, r.y0, r.x1 < G ? r.x1 : G, r.y1 < G ? r.y1 : G);
            in.r[in.n++] = c;
            omni_rects_add(&L, c);
        }
        TEST_ASSERT_TRUE(L.n >= 1 && L.n <= OMNI_RECTS_MAX);
        static uint8_t ci[G][G], cl[G][G];
        cover(in.r, in.n, ci);
        cover(L.r, L.n, cl);
        for (int y = 0; y < G; y++)
            for (int x = 0; x < G; x++)
                if (ci[y][x]) TEST_ASSERT_TRUE(cl[y][x] > 0);
    }
    omni_rects_t L = { .n = 0 };
    omni_rects_add(&L, R(0, 0, 10, 10));
    omni_rects_add(&L, R(2, 2, 5, 5));      /* covered: not added */
    omni_rects_add(&L, R(10, 0, 20, 10));   /* touching, the union costs no more: merged */
    omni_rects_add(&L, R(3, 3, 3, 9));      /* empty: ignored */
    TEST_ASSERT_EQUAL_INT(1, L.n);
    TEST_ASSERT_EQUAL_INT(200, (int)omni_rects_px(&L));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_disjoint_shapes);
    RUN_TEST(test_disjoint_random);
    RUN_TEST(test_disjoint_overflow);
    RUN_TEST(test_minus);
    RUN_TEST(test_rects_minus_list);
    RUN_TEST(test_add_covers);
    return UNITY_END();
}
