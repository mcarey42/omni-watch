#include "unity.h"
#include "omni/blend565.h"
#include "omni/raster.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* omni_raster (Task 6): AA coverage and the mockup's hand() (faces-omni.js 433) against a 16x16 supersampled float
 * oracle of the same canvas operations. */

void setUp(void) {}
void tearDown(void) {}

#define W 466
#define PI 3.14159265358979323846
#define GREY 0x8410

static uint16_t g_fb[W * W];

static void fill(uint16_t v)
{
    for (int i = 0; i < W * W; i++) g_fb[i] = v;
}

/* Summed coverage of a polygon at alpha 255, in px^2 (each pixel's alpha / 255). */
static double poly_area(const omni_pt_t *p, int n)
{
    omni_rect_t b;
    omni_poly_bbox(p, n, &b);
    double s = 0;
    for (int y = b.y0; y < b.y1; y++)
        for (int x = b.x0; x < b.x1; x += OMNI_COVER_W) {
            const int xb = x + OMNI_COVER_W < b.x1 ? x + OMNI_COVER_W : b.x1;
            uint8_t c[OMNI_COVER_W];
            omni_cover_poly(p, n, y, x, xb, c);
            for (int i = 0; i < xb - x; i++) s += c[i] / 255.0;
        }
    return s;
}

/* The mockup's hand shape at angle 0 about (233, 233): r0 118, r1 178 / 214, w 11 / 5.2, tip 16 / 12. */
static int hand_shape(bool hour, omni_pt_t p[5])
{
    const double r0 = 118, r1 = hour ? 178 : 214, w = hour ? 11 : 5.2, tip = hour ? 16 : 12;
    const double lx[5] = { hour ? -w * .32 : -w / 2, hour ? -w / 2 : -w * .38, 0, hour ? w / 2 : w * .38,
                           hour ? w * .32 : w / 2 };
    const double ly[5] = { -r0, -(r1 - tip), -r1, -(r1 - tip), -r0 };
    for (int i = 0; i < 5; i++) p[i] = (omni_pt_t){ (float)(233 + lx[i]), (float)(233 + ly[i]) };
    return 5;
}

static void test_poly_coverage(void)
{
    /* a 10 x 10 square at integer corners: exactly 100 pixels at a, nothing outside */
    fill(0);
    const omni_pt_t sq[4] = { { 20, 30 }, { 30, 30 }, { 30, 40 }, { 20, 40 } };
    omni_fill_poly(g_fb, W, NULL, sq, 4, 0xffff, 200);
    const uint16_t want = omni_blend565(0, 0xffff, 200);
    int in = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const bool inside = x >= 20 && x < 30 && y >= 30 && y < 40;
            TEST_ASSERT_EQUAL_HEX16(inside ? want : 0, g_fb[y * W + x]);
            in += inside;
        }
    TEST_ASSERT_EQUAL(100, in);
    /* the hands' areas (484.88 and 408.10 px^2) within 1 % */
    omni_pt_t h[5];
    const double ah = poly_area(h, hand_shape(true, h)), am = poly_area(h, hand_shape(false, h));
    printf("coverage: hour %.2f px^2 (484.88), minute %.2f (408.10)\n", ah, am);
    TEST_ASSERT_TRUE(fabs((double)(ah) - (484.88)) <= (4.8488));
    TEST_ASSERT_TRUE(fabs((double)(am) - (408.10)) <= (4.0810));
    /* a vertical edge at x + 0.5 covers its column half */
    const omni_pt_t half[4] = { { 50.5f, 10 }, { 60, 10 }, { 60, 20 }, { 50.5f, 20 } };
    for (int y = 10; y < 20; y++) {
        uint8_t m[8];
        omni_cover_poly(half, 4, y, 48, 56, m);
        TEST_ASSERT_TRUE(fabs(m[2] / 255.0 - 0.5) <= 0.04);
        TEST_ASSERT_EQUAL(0, m[1]);
        TEST_ASSERT_EQUAL(255, m[3]);
    }
    /* winding: the same square drawn clockwise or not covers the same */
    const omni_pt_t sq_ccw[4] = { { 20, 30 }, { 20, 40 }, { 30, 40 }, { 30, 30 } };
    TEST_ASSERT_TRUE(fabs((double)(poly_area(sq_ccw, 4)) - (100)) <= (1e-9));
}

/* An annulus and a disc: the summed coverage within 1 % of the area; the rrect likewise. */
static void test_ring_and_rrect_coverage(void)
{
    double disc = 0, ann = 0, rr = 0;
    for (int y = 80; y < 160; y++) {
        uint8_t m[OMNI_COVER_W], a[OMNI_COVER_W], q[OMNI_COVER_W];
        omni_cover_ring(120.3f, 119.7f, 0, 10, y, 60, 180, m);
        omni_cover_ring(120.3f, 119.7f, 20.5f, 30, y, 60, 180, a);
        omni_cover_rrect(70.25f, 90.5f, 170.75f, 150.25f, 8, y, 60, 180, q);
        for (int i = 0; i < 120; i++) {
            disc += m[i] / 255.0;
            ann += a[i] / 255.0;
            rr += q[i] / 255.0;
        }
    }
    const double rra = 100.5 * 59.75 - (4 - PI) * 64;
    printf("coverage: disc %.2f (%.2f), annulus %.2f (%.2f), rrect %.2f (%.2f)\n", disc, PI * 100, ann,
           PI * (900 - 20.5 * 20.5), rr, rra);
    TEST_ASSERT_TRUE(fabs((double)(disc) - (PI * 100)) <= (PI));
    TEST_ASSERT_TRUE(fabs((double)(ann) - (PI * (900 - 20.5 * 20.5))) <= (PI * (900 - 20.5 * 20.5) * 0.01));
    TEST_ASSERT_TRUE(fabs((double)(rr) - (rra)) <= (rra * 0.01));
}

/* ---- The hand oracle: the canvas operations of hand(), each a coverage (16 x 16 samples a pixel, exact point tests
 * in double) composited in float: shadow, lit and dark facets (clipped to the shape), the red tip (clipped), the
 * 0.7 px bevel stroke (every point within 0.35 of the outline) and the 0.5 px centre line. */

typedef struct { double x, y; } dpt_t;

static bool in_convex(const dpt_t *p, int n, double x, double y)   /* either winding; half-open edges do not matter */
{
    int pos = 0, neg = 0;
    for (int i = 0; i < n; i++) {
        const dpt_t a = p[i], b = p[(i + 1) % n];
        const double c = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
        pos += c > 0;
        neg += c < 0;
    }
    return !(pos && neg);
}

static double seg_dist(dpt_t a, dpt_t b, double x, double y)
{
    const double vx = b.x - a.x, vy = b.y - a.y;
    double t = ((x - a.x) * vx + (y - a.y) * vy) / (vx * vx + vy * vy);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypot(x - a.x - t * vx, y - a.y - t * vy);
}

static int clampi(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void oracle_hand(double a, bool hour, int x, int y, double out[3])
{
    const double r0 = 118, r1 = hour ? 178 : 214, w = hour ? 11 : 5.2, tip = hour ? 16 : 12;
    dpt_t M[5];
    if (hour) {
        M[0] = (dpt_t){ -w * .32, -r0 }; M[1] = (dpt_t){ -w / 2, -(r1 - tip) }; M[2] = (dpt_t){ 0, -r1 };
        M[3] = (dpt_t){ w / 2, -(r1 - tip) }; M[4] = (dpt_t){ w * .32, -r0 };
    } else {
        M[0] = (dpt_t){ -w / 2, -r0 }; M[1] = (dpt_t){ -w * .38, -(r1 - tip) }; M[2] = (dpt_t){ 0, -r1 };
        M[3] = (dpt_t){ w * .38, -(r1 - tip) }; M[4] = (dpt_t){ w / 2, -r0 };
    }
    const dpt_t T[4] = { { 0, -r1 }, { -w * .3, -(r1 - 9) }, { 0, -(r1 - 22) }, { w * .3, -(r1 - 9) } };
    const double lit = 0.5 + 0.5 * cos(a + 45 * PI / 180);
    const double cl[3] = { (int)(34 + lit * 30), (int)(30 + lit * 26), (int)(26 + lit * 20) };
    const double cd[3] = { (int)(14 + (1 - lit) * 16), (int)(12 + (1 - lit) * 14), (int)(10 + (1 - lit) * 10) };
    const double ca = cos(a), sa = sin(a);
    int n[6] = { 0 };   /* shadow, lit, dark, red, stroke, centre */
    for (int j = 0; j < 16; j++)
        for (int i = 0; i < 16; i++) {
            const double qx = x + (i + 0.5) / 16 - 233, qy = y + (j + 0.5) / 16 - 233;
            const double lx = qx * ca + qy * sa, ly = -qx * sa + qy * ca;            /* local: rotate back by a */
            const double sx = (qx - 1.4) * ca + (qy - 2.6) * sa, sy = -(qx - 1.4) * sa + (qy - 2.6) * ca;
            n[0] += in_convex(M, 5, sx, sy);
            const bool m = in_convex(M, 5, lx, ly);
            n[1] += m && lx < 0;
            n[2] += m && lx >= 0;
            n[3] += m && (hour ? in_convex(T, 4, lx, ly) : (ly >= -r1 - 2 && ly < -r1 + 9));
            double sd = 1e9;   /* the stroke: within 0.35 of the outline (round joins; canvas mitres them) */
            for (int e = 0; e < 5; e++) sd = fmin(sd, seg_dist(M[e], M[(e + 1) % 5], lx, ly));
            n[4] += sd <= 0.35;
            n[5] += fabs(lx) <= 0.25 && ly <= -r0 && ly >= -r1 + 1;
        }
    double d[3] = { omni_ex_r(GREY), omni_ex_g(GREY), omni_ex_b(GREY) };
    const double col[6][3] = { { 0, 0, 0 }, { cl[0], cl[1], cl[2] }, { cd[0], cd[1], cd[2] }, { 180, 42, 23 },
                               { 214, 180, 118 }, { 255, 236, 190 } };
    const double al[6] = { .55, 1, 1, 1, .85, .35 };
    for (int k = 0; k < 6; k++) {
        const double c = al[k] * n[k] / 256.0;
        for (int ch = 0; ch < 3; ch++) d[ch] = d[ch] * (1 - c) + col[k][ch] * c;
    }
    memcpy(out, d, sizeof d);
}

static void test_hand_vs_oracle(void)
{
    uint8_t lit[3], dark[3];
    omni_hand_facets(0, lit, dark);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(((uint8_t[]){ 59, 52, 43 }), lit, 3);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(((uint8_t[]){ 16, 14, 11 }), dark, 3);
    omni_hand_facets((float)(225 * PI / 180), lit, dark);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(((uint8_t[]){ 49, 43, 36 }), lit, 3);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(((uint8_t[]){ 22, 19, 15 }), dark, 3);
    const double angles[3] = { 0, 90, 225 };
    int fails = 0;
    for (int h = 0; h < 2; h++)
        for (int k = 0; k < 3; k++) {
            const double a = angles[k] * PI / 180;
            fill(GREY);
            omni_draw_hand(g_fb, W, NULL, (float)a, h == 0, true, 1, 0, 0, 0, 0);
            omni_rect_t b;
            omni_hand_bbox((float)a, h == 0, &b);
            int n = 0, ok = 0, worst = 0, outside = 0, wx = 0, wy = 0;
            double wo[3] = { 0 };
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++) {
                    const uint16_t v = g_fb[y * W + x];
                    if (x < b.x0 || x >= b.x1 || y < b.y0 || y >= b.y1) {
                        outside += v != GREY;
                        continue;
                    }
                    double o[3];
                    oracle_hand(a, h == 0, x, y, o);
                    const int e[3] = { abs(omni_ex_r(v) - clampi((int)lround(o[0]))),
                                       abs(omni_ex_g(v) - clampi((int)lround(o[1]))),
                                       abs(omni_ex_b(v) - clampi((int)lround(o[2]))) };
                    const int m = e[0] > e[1] ? (e[0] > e[2] ? e[0] : e[2]) : (e[1] > e[2] ? e[1] : e[2]);
                    n++;
                    ok += m <= 10;
                    if (m > worst) {
                        worst = m;
                        wx = x;
                        wy = y;
                        memcpy(wo, o, sizeof wo);
                    }
                }
            const uint16_t wv = g_fb[wy * W + wx];
            printf("hand %s %3.0f deg: bbox %d px, %.4f within 10, worst %d at (%d, %d): %d %d %d, oracle %.1f %.1f %.1f\n",
                   h ? "minute" : "hour", angles[k], n, (double)ok / n, worst, wx, wy, omni_ex_r(wv), omni_ex_g(wv),
                   omni_ex_b(wv), wo[0], wo[1], wo[2]);
            TEST_ASSERT_EQUAL(0, outside);   /* the bbox holds everything drawn */
            if ((double)ok / n < 0.99) fails++;
        }
    TEST_ASSERT_EQUAL(0, fails);
}

/* The affine: a hand drawn at k 1.4 about (233, 70.5) is the k 1 hand scaled there (coverage area x k^2), and a clip
 * cuts it without changing the pixels inside. */
static void test_hand_affine_and_clip(void)
{
    static uint16_t full[W * W];
    fill(GREY);
    omni_draw_hand(g_fb, W, NULL, 0.3f, false, true, 1.4f, 233, 70.5f, 233, 70.5f);
    memcpy(full, g_fb, sizeof full);
    const omni_rect_t clips[3] = { { 230, 0, 260, 40 }, { 0, 37, 466, 38 }, { 241, 0, 242, 466 } };
    for (int c = 0; c < 3; c++) {
        fill(GREY);
        const omni_rect_t *r = &clips[c];
        omni_draw_hand(g_fb + r->y0 * W + r->x0, W, r, 0.3f, false, true, 1.4f, 233, 70.5f, 233, 70.5f);
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                const bool in = x >= r->x0 && x < r->x1 && y >= r->y0 && y < r->y1;
                TEST_ASSERT_EQUAL_HEX16(in ? full[y * W + x] : GREY, g_fb[y * W + x]);
            }
    }
    /* scaled: the darkened area (shadow and hand) grows by about k^2 */
    double s1 = 0, s2 = 0;
    fill(0xffff);
    omni_draw_hand(g_fb, W, NULL, 0.3f, false, true, 1, 0, 0, 0, 0);
    for (int i = 0; i < W * W; i++) s1 += 255 - omni_ex_g(g_fb[i]);
    fill(0xffff);
    const float mx = 233 + 166 * sinf(0.3f), my = 233 - 166 * cosf(0.3f);   /* about the hand's middle: all on screen */
    omni_draw_hand(g_fb, W, NULL, 0.3f, false, true, 1.4f, mx, my, mx, my);
    for (int i = 0; i < W * W; i++) s2 += 255 - omni_ex_g(g_fb[i]);
    printf("hand affine: darkening x %.3f at k 1.4 (k^2 1.96)\n", s2 / s1);
    TEST_ASSERT_TRUE(fabs((double)(s2 / s1) - (1.96)) <= (0.04));
}

/* Task 7 (T6 M4): a hand prepared once and drawn band by band (16 rows, as the dial draws) is omni_draw_hand's whole
 * draw byte for byte, at the dial's k 1 and the magnifier's affine, with and without the shadow. */
static void test_hand_prepared_bands(void)
{
    static uint16_t full[W * W];
    const struct { float a, k, cx, cy; bool hour, shadow; } C[] = {
        { 5.3291f, 1, 0, 0, true, true }, { 1.1167f, 1, 0, 0, false, true }, { 0.3f, 1.4f, 233, 70.5f, false, true },
        { 2.5f, 1, 0, 0, true, false },
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        fill(GREY);
        omni_draw_hand(g_fb, W, NULL, C[i].a, C[i].hour, C[i].shadow, C[i].k, C[i].cx, C[i].cy, C[i].cx, C[i].cy);
        memcpy(full, g_fb, sizeof full);
        omni_hand_t h;
        TEST_ASSERT_TRUE(omni_hand_prepare(&h, C[i].a, C[i].hour, C[i].shadow, C[i].k, C[i].cx, C[i].cy, C[i].cx, C[i].cy));
        fill(GREY);
        for (int y = 0; y < W; y += 16) {
            const omni_rect_t b = { 0, (int16_t)y, W, (int16_t)(y + 16 < W ? y + 16 : W) };
            omni_hand_draw(&h, g_fb + (size_t)y * W, W, &b);
        }
        TEST_ASSERT_EQUAL_MEMORY(full, g_fb, sizeof full);
    }
}

/* Task 7: a rounded rect prepared once gives omni_cover_rrect's coverage on every row and column chunk. */
static void test_rrect_prepared(void)
{
    omni_rrect_t rr;
    TEST_ASSERT_TRUE(omni_rrect_prepare(&rr, 190.3f, 145.7f, 275.9f, 333.2f, 8));
    for (int y = 140; y < 340; y++)
        for (int xa = 186; xa < 280; xa += 37) {
            const int xb = xa + 37 < 280 ? xa + 37 : 280;
            uint8_t a[OMNI_COVER_W], b[OMNI_COVER_W];
            omni_cover_rrect(190.3f, 145.7f, 275.9f, 333.2f, 8, y, xa, xb, a);
            omni_cover_rrect_prepared(&rr, y, xa, xb, b);
            TEST_ASSERT_EQUAL_MEMORY(a, b, (size_t)(xb - xa));
        }
}

/* The prepared ring (fix wave R140), checked independently of omni_cover_ring (which is now built on it: review M1):
 * the coverage summed over a whole ring is the annulus' area, since circle()'s polygon keeps the circle's area
 * (re = r sqrt(d / sin d)); within 0.2 % for a band, a hairline and a disc. That omni_cover_ring's bytes are unchanged
 * rests on the pinned digests (alm_ring, tmr_ring, the dial's), test_ring_and_rrect_coverage and test_fill_ring_spans.
 * A ring with no area covers nothing. */
static void test_ring_prepared(void)
{
    static const float R[3][2] = { { 190.0f, 214.0f }, { 214.0f, 215.0f }, { 0.0f, 33.3f } };
    for (int k = 0; k < 3; k++) {
        omni_ring_t rg;
        TEST_ASSERT_TRUE(omni_ring_prepare(&rg, 219.0f, 218.6f, R[k][0], R[k][1]));
        double sum = 0;
        for (int y = 0; y < 440; y++)
            for (int xa = 0; xa < 440; xa += OMNI_COVER_W) {
                const int xb = xa + OMNI_COVER_W < 440 ? xa + OMNI_COVER_W : 440;
                uint8_t c[OMNI_COVER_W];
                omni_cover_ring_prepared(&rg, y, xa, xb, c);
                for (int x = 0; x < xb - xa; x++) sum += c[x];
            }
        const double area = PI * ((double)R[k][1] * R[k][1] - (double)R[k][0] * R[k][0]);
        TEST_ASSERT_FLOAT_WITHIN((float)(area * 0.002 + 1.0), (float)area, (float)(sum / 255.0));
    }
    omni_ring_t bad;
    TEST_ASSERT_FALSE(omni_ring_prepare(&bad, 10, 10, 5, 5));   /* no area: nothing to cover */
    uint8_t c[8];
    memset(c, 0x5a, sizeof c);
    omni_cover_ring_prepared(&bad, 10, 6, 14, c);
    for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_UINT8(0, c[i]);
}

/* omni_fill_ring skips the columns its ring cannot touch (outside the outer polygon, inside the hole) row by row: at
 * alpha 255 white over black each pixel must still be exactly the generic coverage path's (omni_cover_ring), with any
 * clip, and nothing written where the coverage is 0. */
static void test_fill_ring_spans(void)
{
    static const float R[4][4] = { { 219.0f, 219.0f, 214.0f, 215.0f }, { 219.0f, 219.0f, 175.0f, 176.0f },
                                   { 233.3f, 232.6f, 120.2f, 160.9f }, { 100.0f, 90.0f, 0.0f, 40.5f } };
    static const omni_rect_t CL[3] = { { 0, 0, 466, 466 }, { 37, 11, 301, 222 }, { 200, 300, 466, 466 } };
    const uint16_t WHITE = omni_pack565(255, 255, 255), MARK = 0x0841;
    for (int k = 0; k < 4; k++)
        for (int c = 0; c < 3; c++) {
            const omni_rect_t *cl = &CL[c];
            const int cw = cl->x1 - cl->x0;
            for (int i = 0; i < W * W; i++) g_fb[i] = MARK;
            omni_fill_ring(g_fb, cw, cl, R[k][0], R[k][1], R[k][2], R[k][3], WHITE, 255);
            for (int y = cl->y0; y < cl->y1; y++)
                for (int xa = cl->x0; xa < cl->x1; xa += OMNI_COVER_W) {
                    const int xb = xa + OMNI_COVER_W < cl->x1 ? xa + OMNI_COVER_W : cl->x1;
                    uint8_t cov[OMNI_COVER_W];
                    omni_cover_ring(R[k][0], R[k][1], R[k][2], R[k][3], y, xa, xb, cov);
                    for (int x = xa; x < xb; x++) {
                        const uint16_t got = g_fb[(y - cl->y0) * cw + (x - cl->x0)];
                        const uint8_t a = cov[x - xa];
                        TEST_ASSERT_EQUAL_HEX16(a ? omni_blend565(MARK, WHITE, a) : MARK, got);
                    }
                }
        }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rrect_prepared);
    RUN_TEST(test_ring_prepared);
    RUN_TEST(test_fill_ring_spans);
    RUN_TEST(test_hand_prepared_bands);
    RUN_TEST(test_poly_coverage);
    RUN_TEST(test_ring_and_rrect_coverage);
    RUN_TEST(test_hand_vs_oracle);
    RUN_TEST(test_hand_affine_and_clip);
    return UNITY_END();
}
