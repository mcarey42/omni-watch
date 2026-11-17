/* The face harness's shared helpers (fk_test_util.h; dw wave 2 Task 2, R147 PR10). */
#include "fk_test_util.h"
#include <stdio.h>
#include <string.h>
#include "unity.h"

#define W DW_TEST_W

const int8_t ORB9[9][2] = {{0, 0}, {3, 1}, {1, 3}, {-2, 2}, {-3, -1}, {-1, -3}, {2, -2}, {4, 0}, {0, 4}};

void fk_test_frame(const fk_face_ops_t *ops, const void *ctx, const fk_face_in_t *in, uint16_t *fb)
{
    memset(fb, 0x5A, (size_t)W * W * sizeof fb[0]);
    (in->glance ? ops->glance : ops->draw)(ctx, in, (omni_rect_t){0, 0, W, W}, fb, W);
}

void fk_test_in_at(fk_face_in_t *in, int h, int m, int s, int ms, bool reduced, bool glance)
{
    memset(in, 0, sizeof *in);
    in->clk.set = true;
    in->clk.h = (uint8_t)h, in->clk.m = (uint8_t)m, in->clk.s = (uint8_t)s, in->clk.ms = (uint16_t)ms;
    in->clk.mday = 1, in->clk.mon = 10, in->clk.wday = 0;
    in->wake_ms = UINT32_MAX;
    in->battery = -1;
    in->reduced = reduced;
    in->glance = glance;
}

void fk_test_lit(const uint16_t *fb, float *pct, uint8_t *max_ch) { fk_test_lit_th(fb, 12, pct, max_ch); }

void fk_test_lit_th(const uint16_t *fb, int th, float *pct, uint8_t *max_ch)
{
    long lit = 0, disc = 0;
    int maxc = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const float dx = x + 0.5f - 233.f, dy = y + 0.5f - 233.f;
            if (dx * dx + dy * dy > 233.f * 233.f) continue;
            disc++;
            const uint16_t p = fb[y * W + x];
            const int R = ((p >> 11) & 31) * 255 / 31, G = ((p >> 5) & 63) * 255 / 63, B = (p & 31) * 255 / 31;
            const int c = R > G ? (R > B ? R : B) : (G > B ? G : B);
            if (c > maxc) maxc = c;
            lit += c > th;
        }
    *pct = (float)(100.0 * (double)lit / (double)disc);
    *max_ch = (uint8_t)maxc;
}

static void tap_at(const fk_face_ops_t *ops, const void *ctx, int x, int y, fk_tap_t expect, bool in, const char *what)
{
    const fk_tap_t got = ops->tap(ctx, (int16_t)x, (int16_t)y);
    if ((got == expect) != in) {
        char m[128];
        snprintf(m, sizeof m, "%s tap at (%d, %d), %s: got %d, expected %s%d", ops->key, x, y, what, (int)got,
                 in ? "" : "anything but ", (int)expect);
        TEST_FAIL_MESSAGE(m);
    }
}

void fk_test_tap_edges(const fk_face_ops_t *ops, const void *ctx, omni_rect_t r, fk_tap_t expect)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(ops->tap, ops->key);
    TEST_ASSERT_TRUE_MESSAGE(r.x0 < r.x1 && r.y0 < r.y1, "an empty tap rect");
    const int xs[3] = {r.x0, (r.x0 + r.x1 - 1) / 2, r.x1 - 1}, ys[3] = {r.y0, (r.y0 + r.y1 - 1) / 2, r.y1 - 1};
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) tap_at(ops, ctx, xs[i], ys[j], expect, true, "a corner, an edge or the middle");
    tap_at(ops, ctx, r.x0 - 1, ys[1], expect, false, "one px left of the rect");
    tap_at(ops, ctx, r.x1, ys[1], expect, false, "one px right of the rect");
    tap_at(ops, ctx, xs[1], r.y0 - 1, expect, false, "one px above the rect");
    tap_at(ops, ctx, xs[1], r.y1, expect, false, "one px below the rect");
}
