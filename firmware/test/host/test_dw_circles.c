/* dw_core's circle script numerals (dw/circles.h, dw/clock.h; dw wave 1 Task 4, spec §12): the glyph table, the
 * 80/40/20 px geometry, the dot that nothing touches, pinned bytes, band identity and the 24 h digits.
 * DW_GLYPH_PPM_DIR=<dir> also writes each pinned glyph as a PPM, for the by-eye review against the test sheet. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "dw/clock.h"
#include "dw/circles.h"

void setUp(void) {}
void tearDown(void) {}

#define N 128
#define INK 0xF6B2   /* #F3D594 through RGB565: (30 << 11) | (53 << 5) | 18, asserted in test_ink */
static uint16_t s_a[N * N], s_b[N * N];
static const float k_r[3] = {40.f, 20.f, 10.f};

static uint32_t crc32(const uint8_t *p, size_t n) /* zlib's CRC-32 */
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

static void draw_still(uint16_t *fb, int digit, float r)
{
    memset(fb, 0, sizeof s_a);
    dw_glyph_draw(fb, N, NULL, digit, 64.f, 64.f, r, INK, 255, 1.f);
}

void test_ink(void) { TEST_ASSERT_EQUAL_HEX16(INK, rgb565(0xF3, 0xD5, 0x94)); }

void test_glyph_table(void)
{
    dw_glyph_t g = dw_glyph(0);
    TEST_ASSERT_EQUAL_UINT8(0, g.lines); TEST_ASSERT_FALSE(g.dot); TEST_ASSERT_TRUE(g.zero);
    for (int d = 1; d <= 4; d++) {
        g = dw_glyph(d);
        TEST_ASSERT_EQUAL_UINT8(d, g.lines); TEST_ASSERT_FALSE(g.dot); TEST_ASSERT_FALSE(g.zero);
    }
    g = dw_glyph(5);
    TEST_ASSERT_EQUAL_UINT8(0, g.lines); TEST_ASSERT_TRUE(g.dot); TEST_ASSERT_FALSE(g.zero);
    for (int d = 6; d <= 9; d++) {
        g = dw_glyph(d);
        TEST_ASSERT_EQUAL_UINT8(d - 5, g.lines); TEST_ASSERT_TRUE(g.dot); TEST_ASSERT_FALSE(g.zero);
    }
    int ring_only[3] = {DW_G_EMPTY, -1, 11};
    for (int i = 0; i < 3; i++) {
        g = dw_glyph(ring_only[i]);
        TEST_ASSERT_EQUAL_UINT8(0, g.lines); TEST_ASSERT_FALSE(g.dot); TEST_ASSERT_FALSE(g.zero);
    }
}

/* a glyph's mark signature: the 30-degree positions its lines sit on (a 12-bit mask), the dot, the zero ring */
typedef struct { unsigned mask; bool dot, zero; } sig_t;

static sig_t sig_of(int digit)
{
    dw_glyph_t g = dw_glyph(digit);
    sig_t s = {0, g.dot, g.zero};
    for (int k = 0; k < g.lines; k++) s.mask |= 1u << (int)lroundf(dw_glyph_line_deg(g.lines, k) / 30.f);
    return s;
}
static unsigned rot12(unsigned m, int steps) { return ((m << steps) | (m >> (12 - steps))) & 0xFFFu; }
static unsigned mirror12(unsigned m)         /* about the vertical axis: angle a -> -a */
{
    unsigned o = 0;
    for (int i = 0; i < 12; i++) if (m & (1u << i)) o |= 1u << ((12 - i) % 12);
    return o;
}

void test_glyph_no_rotation_or_mirror(void)
{
    const int steps[] = {0, 3, 4, 6, 8, 9};   /* 0, 90, 120, 180, 240, 270 degrees: the line angles */
    for (int a = 0; a <= DW_G_EMPTY; a++)
        for (int b = 0; b <= DW_G_EMPTY; b++) {
            if (a == b) continue;
            sig_t sa = sig_of(a), sb = sig_of(b);
            for (int m = 0; m < 2; m++)
                for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; i++) {
                    unsigned t = rot12(m ? mirror12(sa.mask) : sa.mask, steps[i]);
                    bool same = t == sb.mask && sa.dot == sb.dot && sa.zero == sb.zero;
                    char msg[64];
                    snprintf(msg, sizeof msg, "digit %d is digit %d (mirror %d, rot step %d)", a, b, m, steps[i]);
                    TEST_ASSERT_FALSE_MESSAGE(same, msg);
                }
        }
}

void test_glyph_line_angles(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.f, dw_glyph_line_deg(1, 0));
    TEST_ASSERT_EQUAL_FLOAT(180.f, dw_glyph_line_deg(2, 1));
    TEST_ASSERT_EQUAL_FLOAT(120.f, dw_glyph_line_deg(3, 1));
    TEST_ASSERT_EQUAL_FLOAT(240.f, dw_glyph_line_deg(3, 2));
    TEST_ASSERT_EQUAL_FLOAT(90.f, dw_glyph_line_deg(4, 1));
    TEST_ASSERT_EQUAL_FLOAT(270.f, dw_glyph_line_deg(4, 3));
}

void test_glyph_geometry_80_40_20(void)
{
    const float stroke[3] = {4.2f, 2.1f, 1.6f};
    for (int i = 0; i < 3; i++) {
        float r = k_r[i];
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, stroke[i], dw_glyph_stroke(r));
        for (int lines = 1; lines <= 4; lines++)
            for (int k = 0; k < lines; k++) {
                omni_pt_t q[4];
                dw_glyph_line_quad(50.f, 60.f, r, lines, k, q);
                float deg = dw_glyph_line_deg(lines, k), rad = deg * 3.14159265358979f / 180.f;
                float ux = sinf(rad), uy = -cosf(rad);   /* the line's direction */
                float sx = 0.5f * (q[0].x + q[1].x), sy = 0.5f * (q[0].y + q[1].y);
                float ex = 0.5f * (q[2].x + q[3].x), ey = 0.5f * (q[2].y + q[3].y);
                TEST_ASSERT_FLOAT_WITHIN(1e-4f, 50.f + 0.56f * r * ux, sx);
                TEST_ASSERT_FLOAT_WITHIN(1e-4f, 60.f + 0.56f * r * uy, sy);
                TEST_ASSERT_FLOAT_WITHIN(1e-4f, 50.f + 1.28f * r * ux, ex);
                TEST_ASSERT_FLOAT_WITHIN(1e-4f, 60.f + 1.28f * r * uy, ey);
                /* the width is the stroke, across the line */
                float wx = q[1].x - q[0].x, wy = q[1].y - q[0].y;
                TEST_ASSERT_FLOAT_WITHIN(1e-4f, stroke[i], sqrtf(wx * wx + wy * wy));
                TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.f, wx * ux + wy * uy);
            }
    }
}

void test_glyph_bbox_covers_every_pixel(void)
{
    for (int i = 0; i < 3; i++)
        for (int d = 0; d <= 9; d++) {
            draw_still(s_a, d, k_r[i]);
            omni_rect_t bb;
            dw_glyph_bbox(64.f, 64.f, k_r[i], &bb);
            for (int y = 0; y < N; y++)
                for (int x = 0; x < N; x++)
                    if (s_a[y * N + x] && (x < bb.x0 || x >= bb.x1 || y < bb.y0 || y >= bb.y1))
                        TEST_FAIL_MESSAGE("a pixel outside the bbox");
        }
    /* ... and it is not slack: the 4-line glyph's flat line ends sit at 1.28 r, within the margin and the half stroke */
    draw_still(s_a, 4, 40.f);
    omni_rect_t bb;
    dw_glyph_bbox(64.f, 64.f, 40.f, &bb);
    int minx = N, maxx = -1;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
            if (s_a[y * N + x]) { if (x < minx) minx = x; if (x > maxx) maxx = x; }
    TEST_ASSERT_LESS_OR_EQUAL_INT(bb.x0 + 4, minx);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(bb.x1 - 5, maxx);
}

void test_glyph_dot_never_touched(void)
{
    for (int i = 0; i < 3; i++) {
        float r = k_r[i], lim = DW_G_INNER * r - 0.5f * dw_glyph_stroke(r);
        draw_still(s_a, 9, r);    /* four lines and the dot */
        draw_still(s_b, 5, r);    /* the dot alone */
        int dot_px = 0;
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                float dx = (float)x + 0.5f - 64.f, dy = (float)y + 0.5f - 64.f;
                if (sqrtf(dx * dx + dy * dy) <= lim) {
                    TEST_ASSERT_EQUAL_HEX16(s_b[y * N + x], s_a[y * N + x]);
                    if (s_a[y * N + x]) dot_px++;
                }
            }
        TEST_ASSERT_GREATER_THAN_INT(5, dot_px);   /* the dot itself is there */
    }
}

/* the glyph's pinned bytes, [digit 0..9, EMPTY][r 40, 20, 10]; generated once, reviewed by eye against
 * the designers' numerals test sheet (DW_GLYPH_PPM_DIR) */
static const uint32_t k_crc[11][3] = {
    {0x8F3D7BA1, 0xD1D64C4F, 0x9425B4CD},   /* 0 */
    {0x01463A5D, 0xB906C501, 0x8383C134},   /* 1 */
    {0x32419BDC, 0xBA2AD218, 0x37C9223D},   /* 2 */
    {0x03A88A6B, 0xDC66CDB1, 0xEBB68C1B},   /* 3 */
    {0x17F67CC5, 0x98FE697E, 0xD09746DD},   /* 4 */
    {0x95B831B8, 0xF97AD009, 0xCBC50769},   /* 5 */
    {0xCDE65B50, 0x7A5D6899, 0x91D09E60},   /* 6 */
    {0xFEE1FAD1, 0x79717F80, 0x259A7D69},   /* 7 */
    {0xCF08EB66, 0x1F3D6029, 0xF9E5D34F},   /* 8 */
    {0xDB561DC8, 0x5BA5C4E6, 0xC2C41989},   /* 9 */
    {0x591850B5, 0x3A217D91, 0xD996583D},   /* EMPTY */
};

void test_glyph_crc_pinned(void)
{
    const char *dir = getenv("DW_GLYPH_PPM_DIR");
    for (int d = 0; d <= DW_G_EMPTY; d++)
        for (int i = 0; i < 3; i++) {
            draw_still(s_a, d, k_r[i]);
            uint32_t c = crc32((const uint8_t *)s_a, sizeof s_a);
            if (dir) {
                char path[512];
                snprintf(path, sizeof path, "%s/g%02d_r%02d.ppm", dir, d, (int)k_r[i]);
                FILE *f = fopen(path, "wb");
                if (f) {
                    fprintf(f, "P6\n%d %d\n255\n", N, N);
                    for (int p = 0; p < N * N; p++) {
                        uint16_t v = s_a[p];
                        uint8_t rgb[3] = {(uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63),
                                          (uint8_t)((v & 31) * 255 / 31)};
                        fwrite(rgb, 1, 3, f);
                    }
                    fclose(f);
                }
                printf("    {%d, %d} 0x%08X\n", d, (int)k_r[i], (unsigned)c);
            }
            if (dir) continue;   /* a dump run lists every CRC instead of asserting */
            char msg[48];
            snprintf(msg, sizeof msg, "glyph %d at r %d", d, (int)k_r[i]);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(k_crc[d][i], c, msg);
        }
}

void test_glyph_band_exact(void)
{
    for (int d = 0; d <= DW_G_EMPTY; d++)
        for (int i = 0; i < 3; i++) {
            draw_still(s_a, d, k_r[i]);
            memset(s_b, 0, sizeof s_b);
            for (int y = 0; y < N; y += 8) {
                omni_rect_t clip = {0, (int16_t)y, N, (int16_t)(y + 8)};
                dw_glyph_draw(s_b + y * N, N, &clip, d, 64.f, 64.f, k_r[i], INK, 255, 1.f);
            }
            TEST_ASSERT_EQUAL_MEMORY(s_a, s_b, sizeof s_a);
        }
}

void test_glyph_progress_one_is_still(void)
{
    /* the still glyph is progress 1.0 by definition; the wake's last frame (progress just under 1 is not exact) must
     * land on it, and an earlier progress must not */
    draw_still(s_a, 8, 40.f);
    memset(s_b, 0, sizeof s_b);
    dw_glyph_draw(s_b, N, NULL, 8, 64.f, 64.f, 40.f, INK, 255, 1.0f);
    TEST_ASSERT_EQUAL_MEMORY(s_a, s_b, sizeof s_a);
    memset(s_b, 0, sizeof s_b);
    dw_glyph_draw(s_b, N, NULL, 8, 64.f, 64.f, 40.f, INK, 255, 2.0f);   /* clamped */
    TEST_ASSERT_EQUAL_MEMORY(s_a, s_b, sizeof s_a);
    memset(s_b, 0, sizeof s_b);
    dw_glyph_draw(s_b, N, NULL, 8, 64.f, 64.f, 40.f, INK, 255, 0.f);
    for (int p = 0; p < N * N; p++) TEST_ASSERT_EQUAL_HEX16(0, s_b[p]);
}

static int lit(const uint16_t *fb) { int n = 0; for (int p = 0; p < N * N; p++) n += fb[p] != 0; return n; }

void test_glyph_progress_draws_on(void)
{
    /* the ring sweeps first, then the marks grow: coverage only ever grows, the first half has the ring alone, and the
     * half-way frame (ring whole, marks not started) is the empty ring */
    int prev = 0;
    for (int i = 1; i <= 20; i++) {
        memset(s_b, 0, sizeof s_b);
        dw_glyph_draw(s_b, N, NULL, 9, 64.f, 64.f, 40.f, INK, 255, (float)i / 20.f);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(prev - 40, lit(s_b));   /* seams may blur a few edge pixels, never a lot */
        prev = lit(s_b);
    }
    draw_still(s_a, DW_G_EMPTY, 40.f);
    memset(s_b, 0, sizeof s_b);
    dw_glyph_draw(s_b, N, NULL, 9, 64.f, 64.f, 40.f, INK, 255, 0.5f);
    TEST_ASSERT_EQUAL_MEMORY(s_a, s_b, sizeof s_a);
    /* a quarter progress is a quarter ring: nothing in the left half */
    memset(s_b, 0, sizeof s_b);
    dw_glyph_draw(s_b, N, NULL, 9, 64.f, 64.f, 40.f, INK, 255, 0.125f);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < 63; x++) TEST_ASSERT_EQUAL_HEX16(0, s_b[y * N + x]);
    TEST_ASSERT_GREATER_THAN_INT(100, lit(s_b));
}

static fk_clock_t clk(bool set, int h, int m)
{
    fk_clock_t c;
    memset(&c, 0, sizeof c);
    c.set = set; c.h = (uint8_t)h; c.m = (uint8_t)m;
    return c;
}

void test_digits24_ignores_h12(void)
{
    int d[4];
    fk_clock_t c = clk(true, 21, 7);
    dw_digits24(&c, d);
    TEST_ASSERT_EQUAL_INT(2, d[0]); TEST_ASSERT_EQUAL_INT(1, d[1]); TEST_ASSERT_EQUAL_INT(0, d[2]); TEST_ASSERT_EQUAL_INT(7, d[3]);
    c = clk(true, 0, 0);
    dw_digits24(&c, d);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_INT(0, d[i]);
    c = clk(true, 23, 59);
    dw_digits24(&c, d);
    TEST_ASSERT_EQUAL_INT(2, d[0]); TEST_ASSERT_EQUAL_INT(3, d[1]); TEST_ASSERT_EQUAL_INT(5, d[2]); TEST_ASSERT_EQUAL_INT(9, d[3]);
    c = clk(false, 21, 7);
    dw_digits24(&c, d);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_INT(DW_G_EMPTY, d[i]);
    c = clk(true, 24, 0);   /* garbage is not a time */
    dw_digits24(&c, d);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_INT(DW_G_EMPTY, d[i]);
    dw_digits24(NULL, d);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_INT(DW_G_EMPTY, d[i]);
}

void test_clearance(void)
{
    for (int i = 0; i < 3; i++) {
        float r = k_r[i], s = dw_glyph_stroke(r), dw = 2.f;
        /* a circle through the line ends (the stroke's centre on 1.28 r) touches a glyph with lines */
        TEST_ASSERT_LESS_OR_EQUAL_FLOAT(0.f, dw_glyph_clearance(4, 50.f, 60.f, r, 50.f, 60.f, DW_G_LINE_OUT * r, 1.f));
        /* the stroke's inner edge 3 px out from the line ends: the clearance is 3 less the half stroke */
        float out = DW_G_LINE_OUT * r + 3.f + 0.5f * dw;
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 3.f - 0.5f * s, dw_glyph_clearance(9, 50.f, 60.f, r, 50.f, 60.f, out, dw));
        /* the ring-only glyphs end at r + s / 2: the same circle clears them by the lines' extra reach */
        const int ring_only[3] = {0, 5, DW_G_EMPTY};
        for (int k = 0; k < 3; k++)
            TEST_ASSERT_FLOAT_WITHIN(1e-3f, 3.f - 0.5f * s + (DW_G_LINE_OUT - 1.f) * r,
                                     dw_glyph_clearance(ring_only[k], 50.f, 60.f, r, 50.f, 60.f, out, dw));
        /* a circle through the ring touches it; one well inside touches; one off to the side is clear */
        TEST_ASSERT_LESS_OR_EQUAL_FLOAT(0.f, dw_glyph_clearance(DW_G_EMPTY, 50.f, 60.f, r, 50.f, 60.f, r, 1.f));
        TEST_ASSERT_LESS_OR_EQUAL_FLOAT(0.f, dw_glyph_clearance(7, 50.f, 60.f, r, 50.f, 60.f, 0.5f * r, 1.f));
        /* off centre: the glyph 150 px right of a 200 px circle's centre, whose inner edge is 49 px away */
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 49.f - (DW_G_LINE_OUT * r + 0.5f * s),
                                 dw_glyph_clearance(7, 250.f, 100.f, r, 100.f, 100.f, 200.f, 2.f));
        /* and well outside the circle, clear by the gap */
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, (500.f - 200.f - 1.f) - (DW_G_LINE_OUT * r + 0.5f * s),
                                 dw_glyph_clearance(7, 600.f, 100.f, r, 100.f, 100.f, 200.f, 2.f));
    }
}

/* Edge, corner, column-clip and sub-pixel draws into a guard-banded buffer: the guard rings (G px of 0xA5A5 on every
 * side) must stay untouched, and each result must equal the whole-buffer draw cropped. The buffer is a window onto
 * a larger virtual screen so glyphs can straddle its edges. */
#define W 96
#define G 16
#define GS (W + 2 * G)
static uint16_t s_g[GS * GS], s_ref[GS * GS];
#define GUARD 0xA5A5

static void check_guard(const uint16_t *g, const char *msg)
{
    for (int y = 0; y < GS; y++)
        for (int x = 0; x < GS; x++) {
            bool in = x >= G && x < G + W && y >= G && y < G + W;
            if (!in && g[y * GS + x] != GUARD) TEST_FAIL_MESSAGE(msg);
        }
}

static void guard_fill(uint16_t *g)
{
    for (int p = 0; p < GS * GS; p++) g[p] = GUARD;
    for (int y = 0; y < W; y++) memset(g + (G + y) * GS + G, 0, W * sizeof(uint16_t));
}

void test_glyph_guard_bands(void)
{
    /* centres relative to the window: on and past every edge and corner, plus sub-pixel offsets */
    const float pos[][2] = {{-30.f, 5.f}, {5.f, -30.f}, {W + 30.f, 40.f}, {40.f, W + 30.f}, {0.f, 0.f}, {W, 0.f},
                            {0.f, W}, {W, W}, {48.3f, 47.7f}, {10.37f, 88.81f}, {-0.5f, 48.25f}, {95.5f, 95.5f}};
    const int digits[] = {0, 4, 9, DW_G_EMPTY};
    for (int ri = 0; ri < 3; ri++)
        for (unsigned pi = 0; pi < sizeof pos / sizeof pos[0]; pi++)
            for (unsigned di = 0; di < sizeof digits / sizeof digits[0]; di++) {
                float r = k_r[ri], cx = pos[pi][0], cy = pos[pi][1];
                int d = digits[di];
                char msg[96];
                snprintf(msg, sizeof msg, "digit %d r %d at (%.2f, %.2f)", d, (int)r, cx, cy);
                /* reference: the whole window as one clip */
                guard_fill(s_ref);
                omni_rect_t whole = {0, 0, W, W};
                dw_glyph_draw(s_ref + G * GS + G, GS, &whole, d, cx, cy, r, INK, 255, 1.f);
                check_guard(s_ref, msg);
                /* 16-column strips (a column clip), each with fb at its own origin */
                guard_fill(s_g);
                for (int x = 0; x < W; x += 16) {
                    omni_rect_t c = {(int16_t)x, 0, (int16_t)(x + 16), W};
                    dw_glyph_draw(s_g + G * GS + G + x, GS, &c, d, cx, cy, r, INK, 255, 1.f);
                }
                check_guard(s_g, msg);
                TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_ref, s_g, sizeof s_g, msg);
                /* a 7 x 5 tile clip somewhere in the middle, checked against the reference crop */
                guard_fill(s_g);
                omni_rect_t t = {33, 21, 40, 26};
                dw_glyph_draw(s_g + (G + t.y0) * GS + G + t.x0, GS, &t, d, cx, cy, r, INK, 255, 1.f);
                check_guard(s_g, msg);
                for (int y = 0; y < W; y++)
                    for (int x = 0; x < W; x++) {
                        bool in = x >= t.x0 && x < t.x1 && y >= t.y0 && y < t.y1;
                        TEST_ASSERT_EQUAL_HEX16_MESSAGE(in ? s_ref[(G + y) * GS + G + x] : 0, s_g[(G + y) * GS + G + x], msg);
                    }
            }
}

void test_glyph_bbox_clamps(void)
{
    omni_rect_t bb;
    dw_glyph_bbox(1e30f, -1e30f, 40.f, &bb);
    TEST_ASSERT_EQUAL_INT(32767, bb.x0); TEST_ASSERT_EQUAL_INT(-32768, bb.y0);
    TEST_ASSERT_EQUAL_INT(32767, bb.x1); TEST_ASSERT_EQUAL_INT(-32768, bb.y1);
    dw_glyph_bbox(NAN, 0.f, 10.f, &bb);   /* no UB, and some value */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ink);
    RUN_TEST(test_glyph_table);
    RUN_TEST(test_glyph_no_rotation_or_mirror);
    RUN_TEST(test_glyph_line_angles);
    RUN_TEST(test_glyph_geometry_80_40_20);
    RUN_TEST(test_glyph_bbox_covers_every_pixel);
    RUN_TEST(test_glyph_dot_never_touched);
    RUN_TEST(test_glyph_crc_pinned);
    RUN_TEST(test_glyph_band_exact);
    RUN_TEST(test_glyph_progress_one_is_still);
    RUN_TEST(test_glyph_progress_draws_on);
    RUN_TEST(test_digits24_ignores_h12);
    RUN_TEST(test_clearance);
    RUN_TEST(test_glyph_guard_bands);
    RUN_TEST(test_glyph_bbox_clamps);
    return UNITY_END();
}
