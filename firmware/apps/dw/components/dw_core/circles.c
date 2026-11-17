/* Circle script numerals, drawn live (dw/circles.h). Pure, no allocation. */
#include "dw/circles.h"

#include <math.h>
#include "omni/raster.h"
#include "omni/trig.h"

#define SWEEP_STEP_DEG 7.5f   /* a growing ring is drawn as quads of this many degrees (sagitta 0.002 r) */
#define PIECE_RING 0.5f       /* progress at which the ring is whole and the marks start to grow */

#if defined(DW_TEST_HOOKS)
unsigned long dw_test_glyph_calls;   /* the host tests' count (circles.h); absent from the firmware */
#endif

dw_glyph_t dw_glyph(int digit)
{
    dw_glyph_t g = {0, false, false};
    if (digit == 0) g.zero = true;
    else if (digit >= 1 && digit <= 4) g.lines = (uint8_t)digit;
    else if (digit == 5) g.dot = true;
    else if (digit >= 6 && digit <= 9) { g.lines = (uint8_t)(digit - 5); g.dot = true; }
    return g;
}

float dw_glyph_stroke(float r)
{
    float s = 0.105f * r;
    return s > 1.6f ? s : 1.6f;
}

float dw_glyph_line_deg(int lines, int k)
{
    if (lines < 1 || lines > 4 || k < 0 || k >= lines) return 0.f;
    return 360.f * (float)k / (float)lines;
}

/* sin and cos of an angle in degrees: the nearest table entry and a second-order step from it (the error is below
 * 1e-8, so a line end 51 px out is within 1e-6 px of exact) */
static void sincos_deg(float deg, float *s, float *c)
{
    float t = deg * ((float)OMNI_TRIG_N / 360.f);
    float f = floorf(t + 0.5f);
    float d = (t - f) * (6.28318530718f / (float)OMNI_TRIG_N);   /* radians from the entry, |d| <= 0.0031 */
    unsigned i = (unsigned)(int)f;
    float s0 = omni_sin_tab(i), c0 = omni_cos_tab(i);
    float hd2 = 0.5f * d * d;
    *s = s0 + c0 * d - s0 * hd2;
    *c = c0 - s0 * d - c0 * hd2;
}

/* line k's quad from 0.56 r out to radius `out` */
static void line_quad(float cx, float cy, float r, float s, int lines, int k, float out, omni_pt_t q[4])
{
    float sn, cs;
    sincos_deg(dw_glyph_line_deg(lines, k), &sn, &cs);
    float dx = sn, dy = -cs;       /* clockwise from 12 o'clock, y down */
    float px = cs, py = sn;        /* the perpendicular */
    float h = 0.5f * s;
    float a = DW_G_INNER * r;
    q[0].x = cx + dx * a - px * h; q[0].y = cy + dy * a - py * h;
    q[1].x = cx + dx * a + px * h; q[1].y = cy + dy * a + py * h;
    q[2].x = cx + dx * out + px * h; q[2].y = cy + dy * out + py * h;
    q[3].x = cx + dx * out - px * h; q[3].y = cy + dy * out - py * h;
}

void dw_glyph_line_quad(float cx, float cy, float r, int lines, int k, omni_pt_t q[4])
{
    line_quad(cx, cy, r, dw_glyph_stroke(r), lines, k, DW_G_LINE_OUT * r, q);
}

static int16_t clamp16(float v)
{
    if (!(v > -32768.f)) return -32768;   /* also NaN */
    if (v > 32767.f) return 32767;
    return (int16_t)v;
}

void dw_glyph_bbox(float cx, float cy, float r, omni_rect_t *out)
{
    float e = DW_G_LINE_OUT * r + 0.5f * dw_glyph_stroke(r);   /* a quad corner is at most this far out */
    out->x0 = clamp16(floorf(cx - e) - 1.f);
    out->y0 = clamp16(floorf(cy - e) - 1.f);
    out->x1 = clamp16(ceilf(cx + e) + 1.f);
    out->y1 = clamp16(ceilf(cy + e) + 1.f);
}

/* the ring of radius rad and width s, swept clockwise from 12 o'clock through `deg` (>= 360: the whole annulus) */
static void ring_sweep(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float rad, float s,
                       float deg, uint16_t rgb, uint8_t a)
{
    float r0 = rad - 0.5f * s, r1 = rad + 0.5f * s;
    if (deg >= 360.f) { omni_fill_ring(fb, stride, clip, cx, cy, r0, r1, rgb, a); return; }
    for (float d0 = 0.f; d0 < deg; d0 += SWEEP_STEP_DEG) {
        float d1 = d0 + SWEEP_STEP_DEG;
        if (d1 > deg) d1 = deg;
        float s0, c0, s1, c1;
        sincos_deg(d0, &s0, &c0);
        sincos_deg(d1, &s1, &c1);
        omni_pt_t p[4] = {
            {cx + s0 * r1, cy - c0 * r1}, {cx + s1 * r1, cy - c1 * r1},
            {cx + s1 * r0, cy - c1 * r0}, {cx + s0 * r0, cy - c0 * r0},
        };
        omni_fill_poly(fb, stride, clip, p, 4, rgb, a);
    }
}

void dw_glyph_draw(uint16_t *fb, int stride, const omni_rect_t *clip, int digit, float cx, float cy, float r,
                   uint16_t rgb, uint8_t a, float progress)
{
    dw_glyph_draw_w(fb, stride, clip, digit, cx, cy, r, dw_glyph_stroke(r), rgb, a, progress);
}

void dw_glyph_draw_w(uint16_t *fb, int stride, const omni_rect_t *clip, int digit, float cx, float cy, float r,
                     float s, uint16_t rgb, uint8_t a, float progress)
{
#if defined(DW_TEST_HOOKS)
    dw_test_glyph_calls++;   /* dw_glyph_draw comes through here too: one count a glyph */
#endif
    dw_glyph_t g = dw_glyph(digit);
    if (!(s > 0.f) || s > dw_glyph_stroke(r)) s = dw_glyph_stroke(r);   /* never wider than the still glyph's */
    if (!(progress > 0.f)) return;                 /* also NaN */
    float ring_t = progress >= PIECE_RING ? 1.f : progress / PIECE_RING;
    float mark_t = progress >= 1.f ? 1.f : (progress - PIECE_RING) / (1.f - PIECE_RING);

    ring_sweep(fb, stride, clip, cx, cy, r, s, 360.f * ring_t, rgb, a);
    if (mark_t <= 0.f) return;
    if (g.zero) ring_sweep(fb, stride, clip, cx, cy, DW_G_INNER * r, s, 360.f * mark_t, rgb, a);
    for (int k = 0; k < g.lines; k++) {
        omni_pt_t q[4];
        float out = mark_t >= 1.f ? DW_G_LINE_OUT * r : (DW_G_INNER + (DW_G_LINE_OUT - DW_G_INNER) * mark_t) * r;
        line_quad(cx, cy, r, s, g.lines, k, out, q);
        omni_fill_poly(fb, stride, clip, q, 4, rgb, a);
    }
    if (g.dot) omni_fill_ring(fb, stride, clip, cx, cy, 0.f, DW_G_DOT * r * mark_t, rgb, a);
}

float dw_glyph_clearance(int digit, float gx, float gy, float r, float dcx, float dcy, float dr, float dw)
{
    float e = (dw_glyph(digit).lines ? DW_G_LINE_OUT * r : r) + 0.5f * dw_glyph_stroke(r);   /* ring-only: no lines */
    float d = sqrtf((dcx - gx) * (dcx - gx) + (dcy - gy) * (dcy - gy));
    float near = fabsf(d - dr) - 0.5f * dw;       /* the glyph's centre to the stroke's nearest edge */
    if (near < 0.f) near = 0.f;
    return near - e;
}

void dw_digits24(const fk_clock_t *c, int out[4])
{
    if (!c || !c->set || c->h > 23 || c->m > 59) {
        for (int i = 0; i < 4; i++) out[i] = DW_G_EMPTY;
        return;
    }
    out[0] = c->h / 10; out[1] = c->h % 10;
    out[2] = c->m / 10; out[3] = c->m % 10;
}
