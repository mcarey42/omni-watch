#include "unity.h"
#include "omni/blend565.h"
#include "omni/rle.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define W 466
#define FW 97
#define FH 61
#define OX 120
#define OY 77

static uint8_t g_rgba[FW * FH * 4];
static uint8_t g_frame[OMNI_RLE_HDR + FH * 2 + FW * FH * 7];
static uint16_t g_fb[W * W], g_want[W * W];

static uint32_t xs32(uint32_t *x)
{
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}

/* Text-like: runs of ink of random lengths and alphas (a third opaque) on a transparent ground. */
static void make_rgba(void)
{
    uint32_t s = 7;
    memset(g_rgba, 0, sizeof g_rgba);
    for (int y = 0; y < FH; y++) {
        int x = (int)(xs32(&s) % 9);
        while (x < FW) {
            const int len = 1 + (int)(xs32(&s) % 7);
            for (int i = 0; i < len && x + i < FW; i++) {
                uint8_t *p = g_rgba + ((size_t)y * FW + x + i) * 4;
                p[0] = (uint8_t)xs32(&s); p[1] = (uint8_t)xs32(&s); p[2] = (uint8_t)xs32(&s);
                const uint32_t k = xs32(&s) % 3;
                p[3] = k == 0 ? 255 : (uint8_t)(1 + xs32(&s) % 254);
            }
            x += len + 1 + (int)(xs32(&s) % 11);
        }
    }
    for (int x = 0; x < FW; x++) g_rgba[((size_t)(FH - 1) * FW + x) * 4 + 3] = 255;   /* a full row: one long run */
}

static uint16_t rgb565_round(const uint8_t *p)
{
    return (uint16_t)(((p[0] * 31 + 127) / 255) << 11 | ((p[1] * 63 + 127) / 255) << 5 | ((p[2] * 31 + 127) / 255));
}

/* Over black: each pixel blended with the one rule (the encoder rounds colour to RGB565). */
static void want_over(uint16_t *fb, uint16_t ground)
{
    for (int i = 0; i < W * W; i++) fb[i] = ground;
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++) {
            const uint8_t *p = g_rgba + ((size_t)y * FW + x) * 4;
            uint16_t *d = &fb[(OY + y) * W + OX + x];
            *d = omni_blend565(*d, rgb565_round(p), p[3]);
        }
}

static void test_rle_roundtrip(void)
{
    make_rgba();
    const size_t n = omni_rle_encode(g_rgba, FW, FH, OX, OY, g_frame, sizeof g_frame);
    TEST_ASSERT_TRUE(n > OMNI_RLE_HDR);
    TEST_ASSERT_EQUAL(n, omni_rle_size(g_frame, n));
    TEST_ASSERT_EQUAL(0, omni_rle_size(g_frame, n - 1));
    TEST_ASSERT_EQUAL(0, omni_rle_encode(g_rgba, FW, FH, OX, OY, g_frame, n - 1));   /* cap short */
    for (int i = 0; i < W * W; i++) g_fb[i] = 0;
    omni_rle_draw(g_frame, g_fb, W, NULL);
    want_over(g_want, 0);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(g_want, g_fb, W * W);

    /* Composited over black: each pixel equals its RGBA over black within the RGB565 rounding of the colour and the
     * blend's truncating pack (one step each: 11 at worst for red and blue, 5 for green). */
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++) {
            const uint8_t *p = g_rgba + ((size_t)y * FW + x) * 4;
            const uint16_t c = g_fb[(OY + y) * W + OX + x];
            TEST_ASSERT_INT_WITHIN(12, p[0] * p[3] / 255, omni_ex_r(c));
            TEST_ASSERT_INT_WITHIN(6, p[1] * p[3] / 255, omni_ex_g(c));
            TEST_ASSERT_INT_WITHIN(12, p[2] * p[3] / 255, omni_ex_b(c));
        }

    /* Clipped to sub-rects: inside equals the whole draw, nothing outside is touched. */
    const omni_rect_t clips[] = { { 130, 80, 170, 101 }, { 0, 0, 125, 466 }, { 200, 120, 466, 466 }, { 300, 0, 310, 9 } };
    for (size_t k = 0; k < sizeof clips / sizeof clips[0]; k++) {
        const omni_rect_t *c = &clips[k];
        for (int i = 0; i < W * W; i++) g_fb[i] = 0x5aa5;
        omni_rle_draw(g_frame, g_fb + c->y0 * W + c->x0, W, c);
        want_over(g_want, 0x5aa5);
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                const bool in = x >= c->x0 && x < c->x1 && y >= c->y0 && y < c->y1;
                TEST_ASSERT_EQUAL_HEX16(in ? g_want[y * W + x] : 0x5aa5, g_fb[y * W + x]);
            }
    }

    /* The empty layer: the header and one zero run count per row. */
    static uint8_t empty[FW * FH * 4];
    TEST_ASSERT_EQUAL(OMNI_RLE_HDR + FH * 2, omni_rle_encode(empty, FW, FH, OX, OY, g_frame, sizeof g_frame));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rle_roundtrip);
    return UNITY_END();
}
