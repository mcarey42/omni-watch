/* The faces' small shapes (fk/shapes.h; from the first theme's hands.c): raster polygons (omni/raster.h, exact area
 * coverage, so band-exact); discs and glows analytic per pixel with only +, -, x, / and sqrtf (correctly rounded on host
 * and device); every angle through the in-tree sine table (omni/trig.h), never libm's sinf/cosf, so host and device
 * draw the same bytes. Pure, no allocation. */
#include "fk/shapes.h"
#include <math.h>
#include "omni/blend565.h"
#include "omni/raster.h"
#include "omni/trig.h"

#define CX 233.f
#define CY 233.f
#define TWO_PI 6.28318530717958647692f
#define MITER_LIMIT 10.f   /* canvas's default */

static const omni_rect_t SCREEN = {0, 0, 466, 466};

void fk_sincos(float a, float *s, float *c)
{
    /* the nearest table entry and a second-order step from it (as the first theme's numerals do it in degrees):
     * |error| < 2e-6 (the float table's own rounding; test_sincos_table measures it against double libm) */
    const float t = a * ((float)OMNI_TRIG_N / TWO_PI);
    const float f = floorf(t + 0.5f);
    const float d = (t - f) * (TWO_PI / (float)OMNI_TRIG_N);
    const unsigned i = (unsigned)(int)f;
    const float s0 = omni_sin_tab(i), c0 = omni_cos_tab(i), hd2 = 0.5f * d * d;
    *s = s0 + c0 * d - s0 * hd2;
    *c = c0 - s0 * d - c0 * hd2;
}

int16_t fk_to16(float v)
{
    if (!(v > -32000.f)) return -32000;
    if (v > 32000.f) return 32000;
    return (int16_t)v;
}

/* clip (NULL: the screen with fb at its (0, 0)) cut by the box r: the rows and columns to visit; false if empty */
bool fk_span(const omni_rect_t *clip, const omni_rect_t *r, int *x0, int *y0, int *x1, int *y1)
{
    const omni_rect_t *c = clip ? clip : &SCREEN;
    *x0 = r->x0 > c->x0 ? r->x0 : c->x0;
    *y0 = r->y0 > c->y0 ? r->y0 : c->y0;
    *x1 = r->x1 < c->x1 ? r->x1 : c->x1;
    *y1 = r->y1 < c->y1 ? r->y1 : c->y1;
    return *x0 < *x1 && *y0 < *y1;
}

uint16_t *fk_px_at(uint16_t *fb, int stride, const omni_rect_t *clip, int x, int y)
{
    const omni_rect_t *c = clip ? clip : &SCREEN;
    return fb + (y - c->y0) * stride + (x - c->x0);
}

/* fk_add_a8's additive rule for one pixel: a' = amul(alpha) x amul(gain) >> 8, each channel + c x a' >> 8, saturating */
static uint16_t add_px(uint16_t dst, uint16_t rgb, int alpha, int gain)
{
    const int ap = (omni_amul(alpha) * omni_amul(gain)) >> 8;
    if (ap == 0) return dst;
    int r = omni_ex_r(dst) + ((omni_ex_r(rgb) * ap) >> 8);
    int g = omni_ex_g(dst) + ((omni_ex_g(rgb) * ap) >> 8);
    int b = omni_ex_b(dst) + ((omni_ex_b(rgb) * ap) >> 8);
    return omni_pack565(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

/* ---- polygons */

/* p offset by d along its outward normals, mitred (each miter at most MITER_LIMIT x |d| long, as canvas's limit) */
void fk_offset_poly(const omni_pt_t *p, int n, float d, omni_pt_t *out)
{
    float area = 0;
    for (int i = 0; i < n; i++) {
        const omni_pt_t *a = &p[i], *b = &p[(i + 1) % n];
        area += a->x * b->y - b->x * a->y;
    }
    const float sg = area >= 0 ? 1.f : -1.f;   /* y down: positive area winds clockwise on screen */
    for (int i = 0; i < n; i++) {
        const omni_pt_t *a = &p[(i + n - 1) % n], *b = &p[i], *c = &p[(i + 1) % n];
        float e1x = b->x - a->x, e1y = b->y - a->y, e2x = c->x - b->x, e2y = c->y - b->y;
        const float l1 = sqrtf(e1x * e1x + e1y * e1y), l2 = sqrtf(e2x * e2x + e2y * e2y);
        if (l1 > 0) e1x /= l1, e1y /= l1;
        if (l2 > 0) e2x /= l2, e2y /= l2;
        const float n1x = sg * e1y, n1y = -sg * e1x, n2x = sg * e2y, n2y = -sg * e2x;
        float k = 1.f + n1x * n2x + n1y * n2y;
        if (k < 2.f / (MITER_LIMIT * MITER_LIMIT)) k = 2.f / (MITER_LIMIT * MITER_LIMIT);
        out[i].x = b->x + (n1x + n2x) * d / k;
        out[i].y = b->y + (n1y + n2y) * d / k;
    }
}

/* 'lighten' (Task 13, the glances' ink): per field the larger of the pixel and rgb at alpha a over black, so strokes
 * that cross never add up past their own level */
static uint16_t lighten(uint16_t d, uint16_t rgb, int a)
{
    const uint16_t s = omni_blend565(0, rgb, a);
    const uint16_t r = (d & 0xf800u) > (s & 0xf800u) ? d & 0xf800u : s & 0xf800u;
    const uint16_t g = (d & 0x07e0u) > (s & 0x07e0u) ? d & 0x07e0u : s & 0x07e0u;
    const uint16_t b = (d & 0x001fu) > (s & 0x001fu) ? d & 0x001fu : s & 0x001fu;
    return (uint16_t)(r | g | b);
}

static void poly_stroke(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, float w,
                        uint16_t rgb, uint8_t a8, bool max);

void fk_poly_stroke(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, float w,
                    uint16_t rgb, uint8_t a8)
{
    poly_stroke(fb, stride, clip, p, n, w, rgb, a8, false);
}

void fk_poly_stroke_max(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, float w,
                        uint16_t rgb, uint8_t a8)
{
    poly_stroke(fb, stride, clip, p, n, w, rgb, a8, true);
}

static void poly_stroke(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, float w,
                        uint16_t rgb, uint8_t a8, bool max)
{
    if (n < 3 || n > OMNI_POLY_MAX || a8 == 0 || !(w > 0)) return;
    omni_pt_t o[OMNI_POLY_MAX], in[OMNI_POLY_MAX];
    fk_offset_poly(p, n, 0.5f * w, o);
    fk_offset_poly(p, n, -0.5f * w, in);
    omni_rect_t bb;
    omni_poly_bbox(o, n, &bb);
    int x0, y0, x1, y1;
    if (!fk_span(clip, &bb, &x0, &y0, &x1, &y1)) return;
    uint8_t co[OMNI_COVER_W], ci[OMNI_COVER_W];
    for (int y = y0; y < y1; y++)
        for (int xa = x0; xa < x1; xa += OMNI_COVER_W) {
            const int xb = xa + OMNI_COVER_W < x1 ? xa + OMNI_COVER_W : x1;
            omni_cover_poly(o, n, y, xa, xb, co);
            omni_cover_poly(in, n, y, xa, xb, ci);
            uint16_t *d = fk_px_at(fb, stride, clip, xa, y);
            for (int x = 0; x < xb - xa; x++) {
                const int c = (int)co[x] - (int)ci[x];
                if (c <= 0) continue;
                const int a = (c * a8 + 127) / 255;
                d[x] = max ? lighten(d[x], rgb, a) : omni_blend565(d[x], rgb, a);
            }
        }
}

void fk_poly_add(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, uint16_t rgb, uint8_t a8)
{
    if (n < 3 || n > OMNI_POLY_MAX || a8 == 0) return;
    omni_rect_t bb;
    omni_poly_bbox(p, n, &bb);
    int x0, y0, x1, y1;
    if (!fk_span(clip, &bb, &x0, &y0, &x1, &y1)) return;
    uint8_t cov[OMNI_COVER_W];
    for (int y = y0; y < y1; y++)
        for (int xa = x0; xa < x1; xa += OMNI_COVER_W) {
            const int xb = xa + OMNI_COVER_W < x1 ? xa + OMNI_COVER_W : x1;
            omni_cover_poly(p, n, y, xa, xb, cov);
            uint16_t *d = fk_px_at(fb, stride, clip, xa, y);
            for (int x = 0; x < xb - xa; x++)
                if (cov[x]) d[x] = add_px(d[x], rgb, cov[x], a8);
        }
}

/* ---- discs and glows */

void fk_disc_box(float cx, float cy, float r, omni_rect_t *bb)
{
    bb->x0 = fk_to16(floorf(cx - r) - 1.f), bb->y0 = fk_to16(floorf(cy - r) - 1.f);
    bb->x1 = fk_to16(ceilf(cx + r) + 2.f), bb->y1 = fk_to16(ceilf(cy + r) + 2.f);
}

void fk_disc(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float r, uint16_t rgb, uint8_t a8)
{
    if (!(r > 0) || a8 == 0) return;
    omni_rect_t bb;
    fk_disc_box(cx, cy, r, &bb);
    int x0, y0, x1, y1;
    if (!fk_span(clip, &bb, &x0, &y0, &x1, &y1)) return;
    for (int y = y0; y < y1; y++) {
        uint16_t *d = fk_px_at(fb, stride, clip, x0, y);
        const float dy = (float)y + 0.5f - cy;
        for (int x = x0; x < x1; x++, d++) {
            const float dx = (float)x + 0.5f - cx;
            float cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            *d = omni_blend565(*d, rgb, (int)(cov * (float)a8 + 0.5f));
        }
    }
}

/* glowColumn's radial ramp at distance f = d / r (0 <= f < 1), as an alpha 0..255 */
static int glow_alpha(float f)
{
    const float al = f < 0.25f ? 1.f - 0.55f * (f / 0.25f)
                   : f < 0.6f  ? 0.45f - 0.33f * ((f - 0.25f) / 0.35f)
                               : 0.12f * ((1.f - f) / 0.4f);
    return (int)(al * 255.f + 0.5f);
}

/* fk_glow_add with no ring about a whole-pixel centre (cx, cy integers): pixel x and its mirror 2 cx - 1 - x lie at
 * dx and -dx exactly (half-integers, exact in single precision), and so for rows, so the one sqrtf and divide a pixel
 * costs (software routines on the device) serve up to four pixels with the same bytes. Each pixel is visited once. */
static void glow_sym(uint16_t *fb, int stride, const omni_rect_t *clip, int x0, int y0, int x1, int y1, float cx,
                     float cy, float r, uint16_t rgb, uint8_t a8)
{
    const int mx = 2 * (int)cx - 1, my = 2 * (int)cy - 1;   /* x + mirror(x) */
    /* rows from column x0 (indexed by x - x0: no pointer before the buffer) */
    for (int y = y0; y < y1; y++) {
        const int ym = my - y;
        const bool yin = ym >= y0 && ym < y1;
        if (yin && ym < y) continue;   /* done with its mirror row */
        uint16_t *d = fk_px_at(fb, stride, clip, x0, y), *dm = yin ? fk_px_at(fb, stride, clip, x0, ym) : NULL;
        const float dy = (float)y + 0.5f - cy;
        for (int x = x0; x < x1; x++) {
            const int xm = mx - x;
            const bool xin = xm >= x0 && xm < x1;
            if (xin && xm < x) continue;
            const float dx = (float)x + 0.5f - cx;
            const float f = sqrtf(dx * dx + dy * dy) / r;
            if (f >= 1.f) continue;
            const int al = glow_alpha(f);
            d[x - x0] = add_px(d[x - x0], rgb, al, a8);
            if (xin) d[xm - x0] = add_px(d[xm - x0], rgb, al, a8);
            if (dm) {
                dm[x - x0] = add_px(dm[x - x0], rgb, al, a8);
                if (xin) dm[xm - x0] = add_px(dm[xm - x0], rgb, al, a8);
            }
        }
    }
}

void fk_glow_lut_build(uint8_t *lut, int r)
{
    const float rf = (float)r;
    const size_t n = FK_GLOW_LUT_N(r);
    const bool coarse = r >= FK_GLOW_COARSE_R;
    for (size_t i = 0; i < n; i++) {
        /* dx dx + dy dy, exactly (quarter-integers below 2^22): fine entry i at N = 8 i + 2, coarse entry i at
         * N = 64 i */
        const float d2 = (float)(coarse ? 64 * i : 8 * i + 2) * 0.25f;
        const float f = sqrtf(d2) / rf;
        lut[i] = f >= 1.f ? 0 : (uint8_t)glow_alpha(f);
    }
}

/* add_px at a fixed gain, as tables (wave 2 Task 7 fix round, R148 (d)): for each alpha the three channels' additions,
 * ap = amul(alpha) x amul(gain) >> 8 and c x ap >> 8, so a glow's pixel costs three lookups; the bytes are add_px's */
typedef struct { uint8_t r[256], g[256], b[256]; } gain_tab_t;
static void gain_tab(gain_tab_t *t, uint16_t rgb, uint8_t gain)
{
    const int cr = omni_ex_r(rgb), cg = omni_ex_g(rgb), cb = omni_ex_b(rgb), gp = omni_amul(gain);
    for (int a = 0; a < 256; a++) {
        const int ap = (omni_amul(a) * gp) >> 8;
        t->r[a] = (uint8_t)((cr * ap) >> 8), t->g[a] = (uint8_t)((cg * ap) >> 8), t->b[a] = (uint8_t)((cb * ap) >> 8);
    }
}
/* one pixel's addition from the table: the bytes add_px gives (nothing to add, add_px's ap 0 among it, leaves dst) */
static inline uint16_t add_tab(uint16_t dst, const gain_tab_t *t, int al)
{
    if (!(t->r[al] | t->g[al] | t->b[al])) return dst;
    const int r = omni_ex_r(dst) + t->r[al], g = omni_ex_g(dst) + t->g[al], b = omni_ex_b(dst) + t->b[al];
    return omni_pack565(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

/* the table's alpha at N = (2 dx)^2 + (2 dy)^2 (2 <= N < 4 r^2): a fine table's entry (N - 2) / 8; a coarse one's
 * linear interpolation between entries N >> 6 and N >> 6 + 1, (a0 (64 - f) + a1 f + 32) >> 6, f = N & 63 */
static inline int lut_alpha(const uint8_t *lut, int32_t N, bool coarse)
{
    if (!coarse) return lut[(N - 2) >> 3];
    const int32_t k = N >> 6, f = N & 63;
    return (int)((lut[k] * (64 - f) + lut[k + 1] * f + 32) >> 6);
}

void fk_glow_add_lut(uint16_t *fb, int stride, const omni_rect_t *clip, int cx, int cy, int r, const uint8_t *lut,
                     uint16_t rgb, uint8_t a8)
{
    if (r <= 0 || a8 == 0) return;
    omni_rect_t bb;
    fk_disc_box((float)cx, (float)cy, (float)r, &bb);
    int x0, y0, x1, y1;
    if (!fk_span(clip, &bb, &x0, &y0, &x1, &y1)) return;
    const int32_t n4 = 4 * r * r;
    const bool coarse = r >= FK_GLOW_COARSE_R;
    gain_tab_t t;
    gain_tab(&t, rgb, a8);
    /* R148 (d): pixel x and its mirror 2 cx - 1 - x (and so for rows) have the same N, so one lookup serves up to four
     * pixels inside the fk_span; each pixel is visited once (glow_sym's walk), indexed from column x0 */
    const int mx = 2 * cx - 1, my = 2 * cy - 1;
    for (int y = y0; y < y1; y++) {
        const int ym = my - y;
        const bool yin = ym >= y0 && ym < y1;
        if (yin && ym < y) continue;   /* done with its mirror row */
        uint16_t *d = fk_px_at(fb, stride, clip, x0, y), *dm = yin && ym != y ? fk_px_at(fb, stride, clip, x0, ym) : NULL;
        const int32_t ty = 2 * (y - cy) + 1, ty2 = ty * ty;
        if (ty2 + 1 >= n4) continue;   /* the row misses the disc (N >= ty^2 + 1) */
        for (int x = x0; x < x1; x++) {
            const int xm = mx - x;
            const bool xin = xm >= x0 && xm < x1 && xm != x;
            if (xin && xm < x) continue;
            const int32_t tx = 2 * (x - cx) + 1, N = tx * tx + ty2;
            if (N >= n4) continue;
            const int al = lut_alpha(lut, N, coarse);
            if (!al) continue;
            d[x - x0] = add_tab(d[x - x0], &t, al);
            if (xin) d[xm - x0] = add_tab(d[xm - x0], &t, al);
            if (dm) {
                dm[x - x0] = add_tab(dm[x - x0], &t, al);
                if (xin) dm[xm - x0] = add_tab(dm[xm - x0], &t, al);
            }
        }
    }
}

void fk_glow_add(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float r, uint16_t rgb,
                 uint8_t a8, float ring_r0, float ring_r1)
{
    if (!(r > 0) || a8 == 0) return;
    omni_rect_t bb;
    fk_disc_box(cx, cy, r, &bb);
    int x0, y0, x1, y1;
    if (!fk_span(clip, &bb, &x0, &y0, &x1, &y1)) return;
    const bool ring = ring_r0 < ring_r1;
    if (!ring && cx > -4096.f && cx < 4096.f && cy > -4096.f && cy < 4096.f && (float)(int)cx == cx &&
        (float)(int)cy == cy) {   /* Task 12: the symmetric case (the crystal's glows) */
        glow_sym(fb, stride, clip, x0, y0, x1, y1, cx, cy, r, rgb, a8);
        return;
    }
    const float q0 = ring_r0 * ring_r0, q1 = ring_r1 * ring_r1;
    for (int y = y0; y < y1; y++) {
        uint16_t *d = fk_px_at(fb, stride, clip, x0, y);
        const float dy = (float)y + 0.5f - cy, Y = (float)y + 0.5f - CY;
        for (int x = x0; x < x1; x++, d++) {
            if (ring) {
                const float X = (float)x + 0.5f - CX, q = X * X + Y * Y;
                if (q < q0 || q >= q1) continue;
            }
            const float dx = (float)x + 0.5f - cx;
            const float f = sqrtf(dx * dx + dy * dy) / r;
            if (f >= 1.f) continue;
            *d = add_px(*d, rgb, glow_alpha(f), a8);
        }
    }
}

/* ---- Task 13: rings, lines and gradient fills */
#define PI_F 3.14159265358979f

/* stop colours (ns 1: solid) at u in [0, 1], each 8-bit channel interpolated and rounded */
static uint16_t grad_at(const uint16_t *stops, int ns, float u)
{
    if (ns < 2) return stops[0];
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    const float f = u * (float)(ns - 1);
    int i = (int)f;
    if (i > ns - 2) i = ns - 2;
    const float t = f - (float)i;
    const uint16_t a = stops[i], b = stops[i + 1];
    const int r = (int)((float)omni_ex_r(a) + (float)(omni_ex_r(b) - omni_ex_r(a)) * t + 0.5f);
    const int g = (int)((float)omni_ex_g(a) + (float)(omni_ex_g(b) - omni_ex_g(a)) * t + 0.5f);
    const int bl = (int)((float)omni_ex_b(a) + (float)(omni_ex_b(b) - omni_ex_b(a)) * t + 0.5f);
    return omni_pack565(r, g, bl);
}

/* (wave 2 Task 6) the stops at positions pos[0] = 0 < .. < pos[ns - 1] = 1 (canvas addColorStop), each channel
 * interpolated and rounded as grad_at does */
static uint16_t grad_at_pos(const uint16_t *stops, const float *pos, int ns, float u)
{
    if (u <= 0) return stops[0];
    if (u >= 1) return stops[ns - 1];
    int i = 0;
    while (i < ns - 2 && u >= pos[i + 1]) i++;
    const float t = (u - pos[i]) / (pos[i + 1] - pos[i]);
    const uint16_t a = stops[i], b = stops[i + 1];
    const int r = (int)((float)omni_ex_r(a) + (float)(omni_ex_r(b) - omni_ex_r(a)) * t + 0.5f);
    const int g = (int)((float)omni_ex_g(a) + (float)(omni_ex_g(b) - omni_ex_g(a)) * t + 0.5f);
    const int bl = (int)((float)omni_ex_b(a) + (float)(omni_ex_b(b) - omni_ex_b(a)) * t + 0.5f);
    return omni_pack565(r, g, bl);
}

/* pos: NULL, the stops evenly spaced (grad_at); else their positions (grad_at_pos, wave 2 Task 6) */

fk_grad_t fk_grad_of(float gx0, float gy0, float gx1, float gy1, const uint16_t *stops, int ns)
{
    const float vx = gx1 - gx0, vy = gy1 - gy0, l2 = vx * vx + vy * vy;
    return (fk_grad_t){gx0, gy0, vx, vy, l2 > 0 ? 1.f / l2 : 0, stops, ns, NULL};
}

static uint16_t grad_px(const fk_grad_t *g, int x, int y)
{
    if (g->ns < 2) return g->stops[0];
    const float u = (((float)x + 0.5f - g->x0) * g->vx + ((float)y + 0.5f - g->y0) * g->vy) * g->inv;
    return g->pos ? grad_at_pos(g->stops, g->pos, g->ns, u) : grad_at(g->stops, g->ns, u);
}

/* the annulus [r0, r1) about (cx, cy), each pixel's coverage by its centre's distance (box filtered), in g's colours */
void fk_ring_fill(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float r0, float r1,
                  const fk_grad_t *g, uint8_t a8, bool max)
{
    if (!(r1 > r0) || a8 == 0) return;
    omni_rect_t bb;
    fk_disc_box(cx, cy, r1, &bb);
    int x0, y0, x1, y1;
    if (!fk_span(clip, &bb, &x0, &y0, &x1, &y1)) return;
    for (int y = y0; y < y1; y++) {
        uint16_t *d = fk_px_at(fb, stride, clip, x0, y);
        const float dy = (float)y + 0.5f - cy;
        for (int x = x0; x < x1; x++, d++) {
            const float dx = (float)x + 0.5f - cx, dist = sqrtf(dx * dx + dy * dy);
            float co = r1 + 0.5f - dist, ci = r0 + 0.5f - dist;
            co = co < 0 ? 0 : co > 1 ? 1 : co;
            ci = ci < 0 ? 0 : ci > 1 ? 1 : ci;
            const float cov = co - ci;
            if (cov <= 0) continue;
            const int a = (int)(cov * (float)a8 + 0.5f);
            *d = max ? lighten(*d, grad_px(g, x, y), a) : omni_blend565(*d, grad_px(g, x, y), a);
        }
    }
}

void fk_ring(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float r, float w, uint16_t rgb,
             uint8_t a8)
{
    const fk_grad_t g = {0, 0, 0, 0, 0, &rgb, 1, NULL};
    fk_ring_fill(fb, stride, clip, cx, cy, r - 0.5f * w, r + 0.5f * w, &g, a8, false);
}

void fk_ring_max(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float r, float w, uint16_t rgb,
                 uint8_t a8)
{
    const fk_grad_t g = {0, 0, 0, 0, 0, &rgb, 1, NULL};
    fk_ring_fill(fb, stride, clip, cx, cy, r - 0.5f * w, r + 0.5f * w, &g, a8, true);
}

static bool line_quad(float x0, float y0, float x1, float y1, float w, omni_pt_t q[4])
{
    const float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (!(l > 0) || !(w > 0)) return false;
    const float nx = -dy / l * 0.5f * w, ny = dx / l * 0.5f * w;
    q[0] = (omni_pt_t){x0 + nx, y0 + ny}, q[1] = (omni_pt_t){x1 + nx, y1 + ny};
    q[2] = (omni_pt_t){x1 - nx, y1 - ny}, q[3] = (omni_pt_t){x0 - nx, y0 - ny};
    return true;
}

void fk_line(uint16_t *fb, int stride, const omni_rect_t *clip, float x0, float y0, float x1, float y1, float w,
             uint16_t rgb, uint8_t a8)
{
    omni_pt_t q[4];
    if (a8 && line_quad(x0, y0, x1, y1, w, q)) omni_fill_poly(fb, stride, clip, q, 4, rgb, a8);
}

void fk_line_max(uint16_t *fb, int stride, const omni_rect_t *clip, float x0, float y0, float x1, float y1, float w,
                 uint16_t rgb, uint8_t a8)
{
    omni_pt_t q[4];
    if (!a8 || !line_quad(x0, y0, x1, y1, w, q)) return;
    omni_rect_t bb;
    omni_poly_bbox(q, 4, &bb);
    int xa0, ya0, xa1, ya1;
    if (!fk_span(clip, &bb, &xa0, &ya0, &xa1, &ya1)) return;
    uint8_t cov[OMNI_COVER_W];
    for (int y = ya0; y < ya1; y++)
        for (int xa = xa0; xa < xa1; xa += OMNI_COVER_W) {
            const int xb = xa + OMNI_COVER_W < xa1 ? xa + OMNI_COVER_W : xa1;
            omni_cover_poly(q, 4, y, xa, xb, cov);
            uint16_t *d = fk_px_at(fb, stride, clip, xa, y);
            for (int x = 0; x < xb - xa; x++)
                if (cov[x]) d[x] = lighten(d[x], rgb, (cov[x] * a8 + 127) / 255);
        }
}

void fk_poly_grad_g(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, const fk_grad_t *g,
                    uint8_t a8)
{
    if (n < 3 || n > OMNI_POLY_MAX || a8 == 0) return;
    omni_rect_t bb;
    omni_poly_bbox(p, n, &bb);
    int x0, y0, x1, y1;
    if (!fk_span(clip, &bb, &x0, &y0, &x1, &y1)) return;
    uint8_t cov[OMNI_COVER_W];
    for (int y = y0; y < y1; y++)
        for (int xa = x0; xa < x1; xa += OMNI_COVER_W) {
            const int xb = xa + OMNI_COVER_W < x1 ? xa + OMNI_COVER_W : x1;
            omni_cover_poly(p, n, y, xa, xb, cov);
            uint16_t *d = fk_px_at(fb, stride, clip, xa, y);
            for (int x = 0; x < xb - xa; x++)
                if (cov[x]) d[x] = omni_blend565(d[x], grad_px(g, xa + x, y), (cov[x] * a8 + 127) / 255);
        }
}

void fk_poly_grad(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, float gx0, float gy0,
                  float gx1, float gy1, const uint16_t *stops, int ns, uint8_t a8)
{
    if (ns < 2 || ns > 4) return;
    const fk_grad_t g = fk_grad_of(gx0, gy0, gx1, gy1, stops, ns);
    fk_poly_grad_g(fb, stride, clip, p, n, &g, a8);
}
