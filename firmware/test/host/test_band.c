#include "unity.h"
#include "omni/band.h"
#include "omni/blend565.h"
#include <stdint.h>
#include <string.h>

/* omni/band.h (Stage 4a Task 3): chunking of dirty lists into even bands, in-place pack, sprite overlay and the direct
 * path's admission rule. Random lists live on a 96-pixel grid; the seed is test_rects.c's. */

void setUp(void) {}
void tearDown(void) {}

#define G 96

static uint32_t g_seed = 0x2545f491u;
static int rnd(int n)
{
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return (int)(g_seed % (uint32_t)n);
}
static omni_rect_t R(int x0, int y0, int x1, int y1) { return (omni_rect_t){ (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1 }; }
static bool eq(omni_rect_t a, omni_rect_t b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

static void test_even_grows_outward(void)
{
    TEST_ASSERT_TRUE(eq(R(2, 4, 10, 10), omni_band_even(R(3, 5, 10, 9), 466, 466)));
    TEST_ASSERT_TRUE(eq(R(0, 0, 466, 466), omni_band_even(R(0, 0, 466, 466), 466, 466)));
    TEST_ASSERT_TRUE(eq(R(464, 464, 466, 466), omni_band_even(R(465, 465, 466, 466), 466, 466)));
}

static bool even_rect(omni_rect_t c) { return !((c.x0 | c.y0 | c.x1 | c.y1) & 1); }

static void test_chunks_even_disjoint_cover(void)
{
    static uint8_t in[G][G], ev[G][G], bb[G][G], got[G][G];
    for (int rows = 8; rows <= 16; rows += 8)
        for (int it = 0; it < 2000; it++) {
            omni_rects_t L = { .n = 1 + rnd(8) };
            memset(in, 0, sizeof in); memset(ev, 0, sizeof ev); memset(bb, 0, sizeof bb); memset(got, 0, sizeof got);
            omni_rect_t u = R(G, G, 0, 0);
            for (int i = 0; i < L.n; i++) {
                int x0 = rnd(G - 1), y0 = rnd(G - 1);
                L.r[i] = R(x0, y0, x0 + 1 + rnd(G - x0 - 1 + 1 > 40 ? 40 : G - x0), y0 + 1 + rnd(G - y0 > 40 ? 40 : G - y0));
                for (int y = L.r[i].y0; y < L.r[i].y1; y++) for (int x = L.r[i].x0; x < L.r[i].x1; x++) in[y][x] = 1;
                omni_rect_t e = omni_band_even(L.r[i], G, G);
                for (int y = e.y0; y < e.y1; y++) for (int x = e.x0; x < e.x1; x++) ev[y][x] = 1;
                if (e.x0 < u.x0) u.x0 = e.x0;
                if (e.y0 < u.y0) u.y0 = e.y0;
                if (e.x1 > u.x1) u.x1 = e.x1;
                if (e.y1 > u.y1) u.y1 = e.y1;
            }
            for (int y = u.y0; y < u.y1; y++) for (int x = u.x0; x < u.x1; x++) bb[y][x] = 1;
            omni_rect_t out[256];
            bool fb = false;
            int n = omni_band_chunks(&L, rows, G, G, out, 256, &fb);
            TEST_ASSERT_TRUE(n > 0);
            for (int i = 0; i < n; i++) {
                TEST_ASSERT_TRUE(even_rect(out[i]));
                TEST_ASSERT_TRUE(out[i].y1 - out[i].y0 <= rows && out[i].y1 > out[i].y0 && out[i].x1 > out[i].x0);
                for (int y = out[i].y0; y < out[i].y1; y++)
                    for (int x = out[i].x0; x < out[i].x1; x++) {
                        TEST_ASSERT_EQUAL_UINT8(0, got[y][x]);   /* pairwise disjoint */
                        got[y][x] = 1;
                        TEST_ASSERT_TRUE(fb ? bb[y][x] : ev[y][x]);
                    }
            }
            for (int y = 0; y < G; y++) for (int x = 0; x < G; x++) if (in[y][x]) TEST_ASSERT_EQUAL_UINT8(1, got[y][x]);
        }
}

static void test_chunks_disjoint_fallback(void)
{
    /* 16 horizontal and 16 vertical thin bars (odd edges, so evening matters, with 2-px gaps): crossing them splits the later
     * bars into far more than OMNI_RECTS_MAX pieces */
    omni_rects_t L = { .n = 0 }, chk;
    for (int k = 0; k < 16; k++) L.r[L.n++] = R(1, 4 * k + 1, 78, 4 * k + 2);
    for (int k = 0; k < 16; k++) L.r[L.n++] = R(4 * k + 1, 1, 4 * k + 2, 78);
    omni_rects_t ev = { .n = 0 };
    for (int i = 0; i < L.n; i++) ev.r[ev.n++] = omni_band_even(L.r[i], G, G);
    omni_rects_disjoint(&ev, &chk);
    TEST_ASSERT_EQUAL_INT(1, chk.n);   /* the list really does fall back */
    omni_rect_t out[64];
    bool fb = false;
    int n = omni_band_chunks(&L, 8, G, G, out, 64, &fb);
    TEST_ASSERT_TRUE(fb);
    omni_rect_t u = R(G, G, 0, 0);
    for (int i = 0; i < L.n; i++) {
        omni_rect_t e = omni_band_even(L.r[i], G, G);
        if (e.x0 < u.x0) u.x0 = e.x0;
        if (e.y0 < u.y0) u.y0 = e.y0;
        if (e.x1 > u.x1) u.x1 = e.x1;
        if (e.y1 > u.y1) u.y1 = e.y1;
    }
    long px = 0;
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(out[i].x0 == u.x0 && out[i].x1 == u.x1);
        px += (long)(out[i].x1 - out[i].x0) * (out[i].y1 - out[i].y0);
    }
    TEST_ASSERT_EQUAL_INT((u.x1 - u.x0) * (u.y1 - u.y0), (int)px);
    TEST_ASSERT_EQUAL_INT(u.y0, out[0].y0);
    TEST_ASSERT_EQUAL_INT(u.y1, out[n - 1].y1);
}

static void test_chunks_overflow(void)
{
    omni_rects_t L = { .n = 3 };
    L.r[0] = R(0, 0, 4, 4); L.r[1] = R(10, 10, 14, 14); L.r[2] = R(20, 20, 24, 24);
    omni_rect_t out[2];
    TEST_ASSERT_EQUAL_INT(-1, omni_band_chunks(&L, 8, G, G, out, 2, NULL));
    omni_rect_t out3[3];
    TEST_ASSERT_EQUAL_INT(3, omni_band_chunks(&L, 8, G, G, out3, 3, NULL));
}

static void test_pack_swaps_and_compacts(void)
{
    static uint16_t b[466 * 2];
    for (int i = 0; i < 466 * 2; i++) b[i] = (uint16_t)(0x0100 + i);
    uint16_t want[8];
    for (int i = 0; i < 4; i++) { uint16_t v = b[10 + i]; want[i] = (uint16_t)(v << 8 | v >> 8); v = b[466 + 10 + i]; want[4 + i] = (uint16_t)(v << 8 | v >> 8); }
    omni_band_pack(b, 466, R(10, 0, 14, 2));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(want, b, 8);
    for (int i = 0; i < 466 * 2; i++) b[i] = (uint16_t)(0x0100 + i);
    omni_band_pack(b, 466, R(0, 0, 466, 2));
    for (int i = 0; i < 466 * 2; i++) TEST_ASSERT_EQUAL_UINT16((uint16_t)((0x0100 + i) << 8 | (0x0100 + i) >> 8), b[i]);
}

static void test_overlay_blend_rule(void)
{
    uint16_t b[4] = { 0x001F, 0x001F, 0x001F, 0x001F };
    uint32_t px[4] = { 0x80FF0000u, 0x00FF0000u, 0xFF123456u, 0xFF000000u };
    omni_band_overlay_t o = { R(0, 0, 4, 1), px, 4 };
    omni_band_overlay(b, 4, R(0, 0, 4, 1), &o, 1);
    TEST_ASSERT_EQUAL_UINT16(omni_blend565(0x001F, 0xF800, 0x80), b[0]);
    TEST_ASSERT_EQUAL_UINT16(0x001F, b[1]);
    TEST_ASSERT_EQUAL_UINT16(omni_pack565(0x12, 0x34, 0x56), b[2]);
    TEST_ASSERT_EQUAL_UINT16(0x0000, b[3]);
}

static void test_overlay_clip(void)
{
    enum { S = 16 };
    uint16_t b[S * 4];
    for (int i = 0; i < S * 4; i++) b[i] = 0x1234;
    uint32_t px[4 * 4];
    for (int i = 0; i < 16; i++) px[i] = 0xFFFF0000u;
    omni_band_overlay_t o = { R(12, 10, 16, 14), px, 4 };   /* c = x 8..14, y 8..12: overlap x 12..14, y 10..12 */
    omni_band_overlay(b, S, R(8, 8, 14, 12), &o, 1);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < S; x++) {
            bool in = (x >= 12 && x < 14 && y >= 2 && y < 4);   /* band row 0 = screen y 8 */
            TEST_ASSERT_EQUAL_UINT16(in ? 0xF800 : 0x1234, b[y * S + x]);
        }
}

static void test_admit_reasons(void)
{
    const uint32_t BB = 14912, RES = 8192;
    omni_direct_env_t ok = { true, false, true, true, false, 100000, 40000 };
    TEST_ASSERT_EQUAL_INT(OMNI_DD_OK, omni_direct_admit(&ok, BB, BB, RES));
    omni_direct_env_t e;
    e = ok; e.enabled = false;         TEST_ASSERT_EQUAL_INT(OMNI_DD_OFF, omni_direct_admit(&e, BB, BB, RES));
    e = ok; e.provisioning = true;     TEST_ASSERT_EQUAL_INT(OMNI_DD_PROV, omni_direct_admit(&e, BB, BB, RES));
    e = ok; e.display_on = false;      TEST_ASSERT_EQUAL_INT(OMNI_DD_DISPLAY, omni_direct_admit(&e, BB, BB, RES));
    e = ok; e.view_shown = false;      TEST_ASSERT_EQUAL_INT(OMNI_DD_HIDDEN, omni_direct_admit(&e, BB, BB, RES));
    e = ok; e.unknown_overlay = true;  TEST_ASSERT_EQUAL_INT(OMNI_DD_OVERLAY, omni_direct_admit(&e, BB, BB, RES));
    e = ok; e.int_free = 10;           TEST_ASSERT_EQUAL_INT(OMNI_DD_RAM, omni_direct_admit(&e, BB, BB, RES));
    e = ok; e.enabled = false; e.provisioning = true; e.int_free = 0;
    TEST_ASSERT_EQUAL_INT(OMNI_DD_OFF, omni_direct_admit(&e, BB, BB, RES));
    e = ok; e.provisioning = true; e.view_shown = false;
    TEST_ASSERT_EQUAL_INT(OMNI_DD_PROV, omni_direct_admit(&e, BB, BB, RES));
    e = ok; e.int_free = BB + RES;     TEST_ASSERT_EQUAL_INT(OMNI_DD_OK, omni_direct_admit(&e, BB, BB, RES));
    e = ok; e.int_free = BB + RES - 1; TEST_ASSERT_EQUAL_INT(OMNI_DD_RAM, omni_direct_admit(&e, BB, BB, RES));
    e = ok; e.dma_largest = BB - 1;    TEST_ASSERT_EQUAL_INT(OMNI_DD_RAM, omni_direct_admit(&e, BB, BB, RES));
    e = ok; e.dma_largest = BB;        TEST_ASSERT_EQUAL_INT(OMNI_DD_OK, omni_direct_admit(&e, BB, BB, RES));
    e = ok; e.int_free = 1000;         TEST_ASSERT_EQUAL_INT(OMNI_DD_RAM, omni_direct_admit(&e, BB, BB, 0));   /* A7: int_free < need */
    e = ok; e.int_free = RES; e.dma_largest = 0;   /* a check (need 0): the held bands are already out of int_free */
    TEST_ASSERT_EQUAL_INT(OMNI_DD_OK, omni_direct_admit(&e, 0, BB, RES));
    e.int_free = RES - 1;              TEST_ASSERT_EQUAL_INT(OMNI_DD_RAM, omni_direct_admit(&e, 0, BB, RES));
    e = ok; e.int_free = 0xFFFFFFF0u;  TEST_ASSERT_EQUAL_INT(OMNI_DD_OK, omni_direct_admit(&e, 0x20, BB, 0));   /* no wrap */
    e = ok; e.int_free = 0x10;         TEST_ASSERT_EQUAL_INT(OMNI_DD_RAM, omni_direct_admit(&e, 0xFFFFFFF0u, BB, 0x20));
}

static void test_chunks_bad_rows(void)
{
    omni_rects_t L = { .n = 1 };
    L.r[0] = R(0, 0, 10, 10);
    omni_rect_t out[8];
    TEST_ASSERT_EQUAL_INT(-1, omni_band_chunks(&L, 0, G, G, out, 8, NULL));
    TEST_ASSERT_EQUAL_INT(-1, omni_band_chunks(&L, -2, G, G, out, 8, NULL));
    TEST_ASSERT_EQUAL_INT(-1, omni_band_chunks(&L, 3, G, G, out, 8, NULL));
    TEST_ASSERT_EQUAL_INT(1, omni_band_chunks(&L, 16, G, G, out, 8, NULL));
}

static void test_overlay_stride_wider_than_width(void)
{
    uint32_t px[2 * 8];   /* 4 wide, stride 8; the pad words are poison that must never be read */
    for (int i = 0; i < 16; i++) px[i] = 0xFF00FF00u;
    for (int y = 0; y < 2; y++) for (int x = 0; x < 4; x++) px[y * 8 + x] = 0xFF000000u | (uint32_t)(y * 4 + x + 1) << 19;
    uint16_t b[2 * 6] = { 0 };
    omni_band_overlay_t o = { R(1, 0, 5, 2), px, 8 };
    omni_band_overlay(b, 6, R(0, 0, 6, 2), &o, 1);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 6; x++) {
            uint16_t want = (x >= 1 && x < 5) ? omni_pack565((y * 4 + x) << 3, 0, 0) : 0;
            TEST_ASSERT_EQUAL_UINT16(want, b[y * 6 + x]);
        }
}

static void test_pack_overlapping_rows(void)
{
    uint16_t b[6 * 3], want[12];
    for (int i = 0; i < 18; i++) b[i] = (uint16_t)(0x0102 * (i + 1));
    int k = 0;
    for (int y = 0; y < 3; y++) for (int x = 2; x < 6; x++) { uint16_t v = b[y * 6 + x]; want[k++] = (uint16_t)(v << 8 | v >> 8); }
    omni_band_pack(b, 6, R(2, 0, 6, 3));   /* dest row 1 (4..7) overlaps source row 0 (2..5) */
    TEST_ASSERT_EQUAL_UINT16_ARRAY(want, b, 12);
}

/* D14: the dropped-flush slots. Merge when the union costs no more than the two; else a free slot; else the slot whose
 * union grows least. */
static void test_drop_keep_merge_rules(void)
{
    omni_rect_t s[4];
    int n = 0;
    omni_band_drop_keep(s, &n, 4, R(0, 0, 10, 10));
    TEST_ASSERT_EQUAL_INT(1, n);
    omni_band_drop_keep(s, &n, 4, R(10, 0, 20, 10));   /* adjacent: union 200 == 100 + 100, merged */
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_TRUE(eq(s[0], R(0, 0, 20, 10)));
    omni_band_drop_keep(s, &n, 4, R(5, 2, 8, 6));      /* inside: merged, unchanged */
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_TRUE(eq(s[0], R(0, 0, 20, 10)));
    omni_band_drop_keep(s, &n, 4, R(100, 100, 110, 110));   /* far: union costs more, a free slot */
    omni_band_drop_keep(s, &n, 4, R(200, 0, 210, 10));
    omni_band_drop_keep(s, &n, 4, R(0, 300, 10, 310));
    TEST_ASSERT_EQUAL_INT(4, n);
    /* full: a far rect joins the slot whose union grows least (slot 1, 100..110: grows to 100..120 x 100..110) */
    omni_band_drop_keep(s, &n, 4, R(112, 100, 120, 110));
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_TRUE(eq(s[1], R(100, 100, 120, 110)));
    TEST_ASSERT_TRUE(eq(s[0], R(0, 0, 20, 10)));
    TEST_ASSERT_TRUE(eq(s[2], R(200, 0, 210, 10)));
    TEST_ASSERT_TRUE(eq(s[3], R(0, 300, 10, 310)));
    omni_band_drop_keep(s, &n, 4, R(0, 0, 0, 0));       /* empty: nothing */
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_TRUE(eq(s[0], R(0, 0, 20, 10)));
}

/* Every kept pixel of each input stays covered, whatever the order (random lists on the grid). */
static void test_drop_keep_covers_inputs(void)
{
    for (int t = 0; t < 200; t++) {
        omni_rect_t s[4], in[12];
        int n = 0;
        for (int i = 0; i < 12; i++) {
            const int x = rnd(G - 8), y = rnd(G - 8);
            in[i] = R(x, y, x + 1 + rnd(8), y + 1 + rnd(8));
            omni_band_drop_keep(s, &n, 4, in[i]);
            TEST_ASSERT_TRUE(n >= 1 && n <= 4);
        }
        for (int i = 0; i < 12; i++) {
            bool in_one = false;
            for (int k = 0; k < n; k++)
                in_one |= in[i].x0 >= s[k].x0 && in[i].y0 >= s[k].y0 && in[i].x1 <= s[k].x1 && in[i].y1 <= s[k].y1;
            TEST_ASSERT_TRUE(in_one);
        }
    }
}

/* OMNI_DD_BACKOFF (Task 4 fix round 2, N1) is the claim's own result, appended after the admission reasons: the
 * admission rule never returns it, whatever the environment. */
static void test_admit_backoff_is_claim_only(void)
{
    TEST_ASSERT_EQUAL_INT(OMNI_DD_RAM + 1, OMNI_DD_BACKOFF);
    TEST_ASSERT_EQUAL_INT(OMNI_DD_BACKOFF + 1, OMNI_DD__COUNT);
    for (int m = 0; m < 128; m++) {
        const omni_direct_env_t e = { m & 1, m & 2, m & 4, m & 8, m & 16, (m & 32) ? 0u : 100000u,
                                      (m & 64) ? 0u : 100000u };
        TEST_ASSERT_NOT_EQUAL(OMNI_DD_BACKOFF, omni_direct_admit(&e, 1000, 1000, 1000));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_even_grows_outward);
    RUN_TEST(test_chunks_even_disjoint_cover);
    RUN_TEST(test_chunks_disjoint_fallback);
    RUN_TEST(test_chunks_overflow);
    RUN_TEST(test_pack_swaps_and_compacts);
    RUN_TEST(test_overlay_blend_rule);
    RUN_TEST(test_overlay_clip);
    RUN_TEST(test_chunks_bad_rows);
    RUN_TEST(test_overlay_stride_wider_than_width);
    RUN_TEST(test_pack_overlapping_rows);
    RUN_TEST(test_admit_reasons);
    RUN_TEST(test_drop_keep_merge_rules);
    RUN_TEST(test_admit_backoff_is_claim_only);
    RUN_TEST(test_drop_keep_covers_inputs);
    return UNITY_END();
}
