#include "omni/raster.h"
#include <math.h>
#include <string.h>
#include "omni/blend565.h"

/* AA raster (raster.h). Shapes are contours of vertices in 1/256 px; per pixel row each edge's part inside the row is
 * walked across the columns it crosses, and each piece adds its signed height to a cell accumulator row (FreeType's
 * gray rasterizer, font-rs's form): cell c gets dy (512 - f1 - f2) and cell c + 1 dy (f1 + f2), f1, f2 the piece's x
 * within the cell, so the running sum along the row is the pixel's covered area x 2^17. Every crossing is computed from
 * the edge and the row alone by integer division, and pieces left of the clip fold exactly into its first column (the
 * sums telescope), so any clip gives the same pixels. The float work (the vertices) is single precision in a fixed
 * order (-ffp-contract=off). */

#define SCR 466
#define C 233.0f
#define FULL 131072   /* the accumulated area of a whole pixel: 256 x 512 */

typedef struct { int32_t x, y; } fpt_t;
typedef struct { const fpt_t *p; int n; } contour_t;
typedef struct { contour_t c[2]; int nc; int32_t xmin, ymin, xmax, ymax; } shape_t;

static bool to_fix(float v, int32_t *o)
{
    if (!(v == v)) return false;
    v = v < -30000.0f ? -30000.0f : v > 30000.0f ? 30000.0f : v;
    *o = (int32_t)floorf(v * 256.0f + 0.5f);
    return true;
}

static bool fix_pts(const omni_pt_t *p, int n, fpt_t *o)
{
    for (int i = 0; i < n; i++)
        if (!to_fix(p[i].x, &o[i].x) || !to_fix(p[i].y, &o[i].y)) return false;
    return true;
}

/* shape of up to two contours (the second may be NULL); bounds from both */
static bool shape_init(shape_t *s, const fpt_t *a, int na, const fpt_t *b, int nb)
{
    s->nc = 0;
    if (na < 3) return false;
    s->c[s->nc++] = (contour_t){ a, na };
    if (b && nb >= 3) s->c[s->nc++] = (contour_t){ b, nb };
    s->xmin = s->xmax = a[0].x;
    s->ymin = s->ymax = a[0].y;
    for (int k = 0; k < s->nc; k++)
        for (int i = 0; i < s->c[k].n; i++) {
            const fpt_t q = s->c[k].p[i];
            s->xmin = q.x < s->xmin ? q.x : s->xmin;
            s->xmax = q.x > s->xmax ? q.x : s->xmax;
            s->ymin = q.y < s->ymin ? q.y : s->ymin;
            s->ymax = q.y > s->ymax ? q.y : s->ymax;
        }
    return true;
}

static inline int32_t floor_div256(int32_t v) { return v >> 8; }   /* arithmetic shift: floor (globe.h checks it) */

/* One piece in cell c, x f1 -> f2 within it (0..256), signed height dy; acc[0] is column xa, acc has xb - xa + 1. */
static inline void piece(int32_t *acc, int xa, int xb, int32_t c, int32_t f1, int32_t f2, int32_t dy)
{
    if (c >= xb || !dy) return;
    if (c < xa) {
        acc[0] += dy * 512;
        return;
    }
    const int i = (int)(c - xa);
    acc[i] += dy * (512 - f1 - f2);
    acc[i + 1] += dy * (f1 + f2);
}

static void acc_edge(int32_t *acc, int xa, int xb, int y, fpt_t a, fpt_t b)
{
    if (a.y == b.y) return;
    int32_t s = 1;
    if (a.y > b.y) {
        const fpt_t t = a;
        a = b;
        b = t;
        s = -1;
    }
    const int32_t ry0 = (int32_t)y * 256, ry1 = ry0 + 256;
    if (b.y <= ry0 || a.y >= ry1) return;
    if (floor_div256(a.x < b.x ? a.x : b.x) >= xb) return;   /* wholly right of the clip */
    const int32_t ya = a.y > ry0 ? a.y : ry0, yb = b.y < ry1 ? b.y : ry1, dyt = yb - ya;
    const int64_t ex = (int64_t)b.x - a.x, ey = (int64_t)b.y - a.y;
    const int32_t xs = a.x + (int32_t)(ex * (ya - a.y) / ey), xe = a.x + (int32_t)(ex * (yb - a.y) / ey);
    int32_t c = floor_div256(xs);
    const int32_t c1 = floor_div256(xe);
    if (c == c1) {
        piece(acc, xa, xb, c, xs - c * 256, xe - c * 256, s * dyt);
        return;
    }
    if (c1 < xa && c < xa) {   /* wholly left of the clip: its whole height folds into the first column */
        acc[0] += s * dyt * 512;
        return;
    }
    const int step = xe > xs ? 1 : -1;
    int32_t px = xs, py = ya;
    while (c != c1) {
        const int32_t bx = step > 0 ? (c + 1) * 256 : c * 256;
        const int32_t by = ya + (int32_t)((int64_t)(bx - xs) * dyt / (xe - xs));
        piece(acc, xa, xb, c, px - c * 256, bx - c * 256, s * (by - py));
        px = bx;
        py = by;
        c += step;
        if (step > 0 && c >= xb) return;
    }
    piece(acc, xa, xb, c, px - c * 256, xe - c * 256, s * (yb - py));
}

static void shape_row(const shape_t *s, int y, int xa, int xb, int32_t *acc)
{
    memset(acc, 0, (size_t)(xb - xa + 1) * sizeof *acc);
    if (s->ymax <= (int32_t)y * 256 || s->ymin >= (int32_t)(y + 1) * 256) return;
    for (int k = 0; k < s->nc; k++) {
        const contour_t *c = &s->c[k];
        for (int i = 0; i < c->n; i++) acc_edge(acc, xa, xb, y, c->p[i], c->p[i + 1 < c->n ? i + 1 : 0]);
    }
}

/* The accumulator row as coverage 0..FULL (the running sum, its magnitude, clamped), in place. */
static void resolve(int32_t *acc, int n)
{
    int32_t s = 0;
    for (int i = 0; i < n; i++) {
        s += acc[i];
        const int32_t c = s < 0 ? -s : s;
        acc[i] = c > FULL ? FULL : c;
    }
}

static inline int cov_alpha(int a, int32_t cov) { return (int)(((int64_t)a * cov + FULL / 2) >> 17); }

static void composite(uint16_t *d, const int32_t *cov, int n, uint16_t rgb, int a)
{
    for (int i = 0; i < n; i++)
        if (cov[i]) {
            const int al = cov_alpha(a, cov[i]);
            if (al) d[i] = omni_blend565(d[i], rgb, al);
        }
}

/* ---- Circles and rounded rects as contours */

#define CIRCLE_MAX 64
static int circle(float cx, float cy, float r, bool reverse, fpt_t *o)
{
    int n = (int)(r * 3.0f) + 8;
    n = n < 16 ? 16 : n > CIRCLE_MAX ? CIRCLE_MAX : n;
    const float d = 6.28318530717958647692f / (float)n, re = r * sqrtf(d / sinf(d));   /* the polygon's area: the circle's */
    for (int i = 0; i < n; i++) {
        const float t = d * (float)(reverse ? n - i : i);
        if (!to_fix(cx + re * cosf(t), &o[i].x) || !to_fix(cy + re * sinf(t), &o[i].y)) return 0;
    }
    return n;
}

#define ARC_SEGS 10
#define RRECT_PTS (4 * (ARC_SEGS + 1))
static int rrect(float x0, float y0, float x1, float y1, float r, fpt_t *o)
{
    const float hw = (x1 - x0) * 0.5f, hh = (y1 - y0) * 0.5f;
    if (!(hw > 0) || !(hh > 0)) return 0;
    r = r > hw ? hw : r;
    r = r > hh ? hh : r;
    r = r < 0 ? 0 : r;
    const float cxs[4] = { x1 - r, x1 - r, x0 + r, x0 + r }, cys[4] = { y0 + r, y1 - r, y1 - r, y0 + r };
    int n = 0;
    for (int k = 0; k < 4; k++)   /* corners clockwise on screen from the top right, each from -90 + 90 k degrees */
        for (int j = 0; j <= ARC_SEGS; j++) {
            const float t = 1.57079632679489662f * ((float)k - 1 + (float)j / ARC_SEGS);
            if (!to_fix(cxs[k] + r * cosf(t), &o[n].x) || !to_fix(cys[k] + r * sinf(t), &o[n].y)) return 0;
            n++;
        }
    return n;
}

/* ---- Fills */

typedef struct { int x0, y0, x1, y1, stride; uint16_t *fb; } clip_t;

static void clip_init(clip_t *c, uint16_t *fb, int stride, const omni_rect_t *r)
{
    if (r) *c = (clip_t){ r->x0, r->y0, r->x1, r->y1, stride, fb };
    else *c = (clip_t){ 0, 0, SCR, SCR, stride, fb };
}

static inline uint16_t *clip_px(const clip_t *c, int x, int y) { return c->fb + (y - c->y0) * c->stride + (x - c->x0); }

/* The pixel rows [*y0, *y1) and columns [*x0, *x1) a shape's bounds (1/256 px) touch inside the clip. */
static bool clip_box(const clip_t *c, int32_t fx0, int32_t fy0, int32_t fx1, int32_t fy1, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = floor_div256(fx0);
    *y0 = floor_div256(fy0);
    *x1 = floor_div256(fx1) + 1;
    *y1 = floor_div256(fy1) + 1;
    *x0 = *x0 < c->x0 ? c->x0 : *x0;
    *y0 = *y0 < c->y0 ? c->y0 : *y0;
    *x1 = *x1 > c->x1 ? c->x1 : *x1;
    *y1 = *y1 > c->y1 ? c->y1 : *y1;
    return *x0 < *x1 && *y0 < *y1;
}

static void fill_shape(const clip_t *c, const shape_t *s, uint16_t rgb, int a)
{
    int x0, y0, x1, y1;
    if (!a || !clip_box(c, s->xmin, s->ymin, s->xmax, s->ymax, &x0, &y0, &x1, &y1)) return;
    for (int y = y0; y < y1; y++)
        for (int xa = x0; xa < x1; xa += OMNI_COVER_W) {
            const int xb = xa + OMNI_COVER_W < x1 ? xa + OMNI_COVER_W : x1;
            int32_t acc[OMNI_COVER_W + 1];
            shape_row(s, y, xa, xb, acc);
            resolve(acc, xb - xa);
            composite(clip_px(c, xa, y), acc, xb - xa, rgb, a);
        }
}

void omni_fill_poly(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, uint16_t rgb, uint8_t a)
{
    fpt_t q[OMNI_POLY_MAX];
    shape_t s;
    if (n < 3 || n > OMNI_POLY_MAX || !fix_pts(p, n, q) || !shape_init(&s, q, n, NULL, 0)) return;
    clip_t c;
    clip_init(&c, fb, stride, clip);
    fill_shape(&c, &s, rgb, a);
}

static bool ring_shape(shape_t *s, float cx, float cy, float r0, float r1, fpt_t *o, fpt_t *i)
{
    if (!(r1 > 0) || !(r1 > r0)) return false;
    const int no = circle(cx, cy, r1, false, o), ni = r0 > 0 ? circle(cx, cy, r0, true, i) : 0;
    return no && shape_init(s, o, no, ni ? i : NULL, ni);
}

/* circle()'s polygon for radius r: its vertices lie on radius *re (its circumcircle), its edges no nearer than *ri. */
static void circle_radii(float r, float *re, float *ri)
{
    int n = (int)(r * 3.0f) + 8;
    n = n < 16 ? 16 : n > CIRCLE_MAX ? CIRCLE_MAX : n;
    const float d = 6.28318530717958647692f / (float)n;
    *re = r * sqrtf(d / sinf(d));
    *ri = *re * cosf(d * 0.5f);
}

/* fill_shape for a ring, walking per row only the columns its pixels can touch: those within the outer polygon's
 * circumcircle, less those wholly inside the hole polygon's incircle (each with a 2 px margin for the 1/256 px vertex
 * rounding). Coverage there is exactly 0 (the outer and inner areas cancel in integers), composite writes nothing for
 * it, and a chunk's coverage depends only on its own columns (pieces to its left fold into its first), so the pixels
 * are fill_shape's. A whole-ring fill over a 430 px box walked every chunk of every row through all 128 edges. */
static void fill_ring_rows(const clip_t *c, const shape_t *s, float cx, float cy, float r0, float r1, uint16_t rgb, int a)
{
    int x0, y0, x1, y1;
    if (!a || !clip_box(c, s->xmin, s->ymin, s->xmax, s->ymax, &x0, &y0, &x1, &y1)) return;
    float ro, ri_unused, re_in = 0, ri = -1;
    circle_radii(r1, &ro, &ri_unused);
    if (r0 > 0) circle_radii(r0, &re_in, &ri);
    ro += 2.0f;
    ri -= 2.0f;
    for (int y = y0; y < y1; y++) {
        const float ya = (float)y - cy, yb = ya + 1.0f;
        const float dmin = ya > 0 ? ya : yb < 0 ? -yb : 0, dmax = fabsf(ya) > fabsf(yb) ? fabsf(ya) : fabsf(yb);
        if (dmin >= ro) continue;
        const float ho = sqrtf(ro * ro - dmin * dmin);
        int xs = (int)floorf(cx - ho), xe = (int)ceilf(cx + ho);
        xs = xs < x0 ? x0 : xs;
        xe = xe > x1 ? x1 : xe;
        int hl = xe, hr = xe;   /* the hole's columns [hl, hr): none */
        if (ri > dmax) {
            const float hi = sqrtf(ri * ri - dmax * dmax);
            hl = (int)ceilf(cx - hi);
            hr = (int)floorf(cx + hi);
            hl = hl < xs ? xs : hl > xe ? xe : hl;
            hr = hr < hl ? hl : hr > xe ? xe : hr;
        }
        const int seg[4] = { xs, hl, hr, xe };
        for (int k = 0; k < 2; k++)
            for (int xa = seg[2 * k]; xa < seg[2 * k + 1]; xa += OMNI_COVER_W) {
                const int xb = xa + OMNI_COVER_W < seg[2 * k + 1] ? xa + OMNI_COVER_W : seg[2 * k + 1];
                int32_t acc[OMNI_COVER_W + 1];
                shape_row(s, y, xa, xb, acc);
                resolve(acc, xb - xa);
                composite(clip_px(c, xa, y), acc, xb - xa, rgb, a);
            }
    }
}

void omni_fill_ring(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float r0, float r1,
                    uint16_t rgb, uint8_t a)
{
    fpt_t o[CIRCLE_MAX], i[CIRCLE_MAX];
    shape_t s;
    if (!ring_shape(&s, cx, cy, r0, r1, o, i)) return;
    clip_t c;
    clip_init(&c, fb, stride, clip);
    fill_ring_rows(&c, &s, cx, cy, r0, r1, rgb, a);
}

static void bbox_of(int32_t x0, int32_t y0, int32_t x1, int32_t y1, omni_rect_t *out)
{
    *out = (omni_rect_t){ (int16_t)(floor_div256(x0) - 1), (int16_t)(floor_div256(y0) - 1), (int16_t)(floor_div256(x1) + 2),
                          (int16_t)(floor_div256(y1) + 2) };
}

void omni_poly_bbox(const omni_pt_t *p, int n, omni_rect_t *out)
{
    fpt_t q[OMNI_POLY_MAX];
    shape_t s;
    if (n < 3 || n > OMNI_POLY_MAX || !fix_pts(p, n, q) || !shape_init(&s, q, n, NULL, 0)) {
        *out = (omni_rect_t){ 0, 0, 0, 0 };
        return;
    }
    bbox_of(s.xmin, s.ymin, s.xmax, s.ymax, out);
}

/* ---- Coverage rows */

static void cover_out(const shape_t *s, int y, int xa, int xb, uint8_t *cov)
{
    int32_t acc[OMNI_COVER_W + 1];
    shape_row(s, y, xa, xb, acc);
    resolve(acc, xb - xa);
    for (int i = 0; i < xb - xa; i++) cov[i] = (uint8_t)cov_alpha(255, acc[i]);
}

void omni_cover_poly(const omni_pt_t *p, int n, int y, int xa, int xb, uint8_t *cov)
{
    fpt_t q[OMNI_POLY_MAX];
    shape_t s;
    if (xb - xa > OMNI_COVER_W || xb <= xa) return;
    memset(cov, 0, (size_t)(xb - xa));
    if (n < 3 || n > OMNI_POLY_MAX || !fix_pts(p, n, q) || !shape_init(&s, q, n, NULL, 0)) return;
    cover_out(&s, y, xa, xb, cov);
}

typedef struct { fpt_t o[CIRCLE_MAX], i[CIRCLE_MAX]; shape_t s; bool ok; } ring_t;
typedef char omni_ring_size_check[sizeof(ring_t) <= OMNI_RING_BYTES ? 1 : -1];

bool omni_ring_prepare(omni_ring_t *rg, float cx, float cy, float r0, float r1)
{
    ring_t *p = (ring_t *)(void *)rg->b;
    p->ok = ring_shape(&p->s, cx, cy, r0, r1, p->o, p->i);
    return p->ok;
}

void omni_cover_ring_prepared(const omni_ring_t *rg, int y, int xa, int xb, uint8_t *cov)
{
    const ring_t *p = (const ring_t *)(const void *)rg->b;
    if (xb - xa > OMNI_COVER_W || xb <= xa) return;
    memset(cov, 0, (size_t)(xb - xa));
    if (p->ok) cover_out(&p->s, y, xa, xb, cov);
}

void omni_cover_ring(float cx, float cy, float r0, float r1, int y, int xa, int xb, uint8_t *cov)
{
    omni_ring_t rg;
    if (xb - xa > OMNI_COVER_W || xb <= xa) return;
    omni_ring_prepare(&rg, cx, cy, r0, r1);
    omni_cover_ring_prepared(&rg, y, xa, xb, cov);
}

typedef struct { fpt_t o[RRECT_PTS]; shape_t s; bool ok; } rrect_t;
typedef char omni_rrect_size_check[sizeof(rrect_t) <= OMNI_RRECT_BYTES ? 1 : -1];

bool omni_rrect_prepare(omni_rrect_t *rr, float x0, float y0, float x1, float y1, float r)
{
    rrect_t *p = (rrect_t *)(void *)rr->b;
    const int n = rrect(x0, y0, x1, y1, r, p->o);
    p->ok = n && shape_init(&p->s, p->o, n, NULL, 0);
    return p->ok;
}

void omni_cover_rrect_prepared(const omni_rrect_t *rr, int y, int xa, int xb, uint8_t *cov)
{
    const rrect_t *p = (const rrect_t *)(const void *)rr->b;
    if (xb - xa > OMNI_COVER_W || xb <= xa) return;
    memset(cov, 0, (size_t)(xb - xa));
    if (p->ok) cover_out(&p->s, y, xa, xb, cov);
}

void omni_cover_rrect(float x0, float y0, float x1, float y1, float r, int y, int xa, int xb, uint8_t *cov)
{
    omni_rrect_t rr;
    if (xb - xa > OMNI_COVER_W || xb <= xa) return;
    omni_rrect_prepare(&rr, x0, y0, x1, y1, r);
    omni_cover_rrect_prepared(&rr, y, xa, xb, cov);
}

/* ---- The hand (faces-omni.js hand(), 433) */

#define HAND_R0 118.0f
#define SHADOW_A 140   /* .55 */
#define STROKE_A 217   /* .85 */
#define CENTRE_A 89    /* .35 */
#define DEG45 0.785398163397448310f

void omni_hand_facets(float a, uint8_t lit[3], uint8_t dark[3])
{
    const float l = 0.5f + 0.5f * cosf(a + DEG45), d = 1 - l;
    lit[0] = (uint8_t)(34 + l * 30);
    lit[1] = (uint8_t)(30 + l * 26);
    lit[2] = (uint8_t)(26 + l * 20);
    dark[0] = (uint8_t)(14 + d * 16);
    dark[1] = (uint8_t)(12 + d * 14);
    dark[2] = (uint8_t)(10 + d * 10);
}

typedef struct { float ca, sa, k, cx, cy, sx, sy, ox, oy; } xf_t;   /* (ox, oy): a dial-space offset (the shadow) */

static bool xf_pts(const xf_t *t, const float *lx, const float *ly, int n, fpt_t *o)
{
    for (int i = 0; i < n; i++) {
        const float qx = C + lx[i] * t->ca - ly[i] * t->sa + t->ox, qy = C + lx[i] * t->sa + ly[i] * t->ca + t->oy;
        if (!to_fix(t->cx + t->k * (qx - t->sx), &o[i].x) || !to_fix(t->cy + t->k * (qy - t->sy), &o[i].y)) return false;
    }
    return true;
}

/* The local outline (rotation 0, about the centre, along -y): 5 points. */
static void hand_local(bool hour, float lx[5], float ly[5], float *r1, float *w)
{
    *r1 = hour ? 178.0f : 214.0f;
    *w = hour ? 11.0f : 5.2f;
    const float tip = hour ? 16.0f : 12.0f, W = *w;
    lx[0] = hour ? -W * 0.32f : -W / 2;
    lx[1] = hour ? -W / 2 : -W * 0.38f;
    lx[2] = 0;
    lx[3] = -lx[1];
    lx[4] = -lx[0];
    ly[0] = ly[4] = -HAND_R0;
    ly[1] = ly[3] = -(*r1 - tip);
    ly[2] = -*r1;
}

/* The outline grown by d (mitred; d < 0 shrinks): each vertex moves to the meeting of its two edges' offset lines. */
static void offset_local(const float lx[5], const float ly[5], float d, float ox[5], float oy[5])
{
    float area = 0;
    for (int i = 0; i < 5; i++) area += lx[i] * ly[(i + 1) % 5] - lx[(i + 1) % 5] * ly[i];
    const float s = area > 0 ? 1.0f : -1.0f;   /* (dy, -dx) of an edge points out of a positive-area outline */
    for (int i = 0; i < 5; i++) {
        const int a = (i + 4) % 5, c = (i + 1) % 5;
        float n1x = ly[i] - ly[a], n1y = -(lx[i] - lx[a]), n2x = ly[c] - ly[i], n2y = -(lx[c] - lx[i]);
        const float l1 = sqrtf(n1x * n1x + n1y * n1y), l2 = sqrtf(n2x * n2x + n2y * n2y);
        n1x = n1x / l1 * s;
        n1y = n1y / l1 * s;
        n2x = n2x / l2 * s;
        n2y = n2y / l2 * s;
        const float k = d / (1 + n1x * n2x + n1y * n2y);
        ox[i] = lx[i] + (n1x + n2x) * k;
        oy[i] = ly[i] + (n1y + n2y) * k;
    }
}

/* Sutherland-Hodgman: the convex polygon p (n points) cut by the convex polygon q (m points), into o (<= n + m). */
static int clip_convex(const float *px, const float *py, int n, const float *qx, const float *qy, int m, float *ox, float *oy)
{
    float ax[OMNI_POLY_MAX * 2], ay[OMNI_POLY_MAX * 2], bx[OMNI_POLY_MAX * 2], by[OMNI_POLY_MAX * 2];
    float area = 0;
    for (int i = 0; i < m; i++) area += qx[i] * qy[(i + 1) % m] - qx[(i + 1) % m] * qy[i];
    const float sg = area > 0 ? 1.0f : -1.0f;
    int cnt = n;
    memcpy(ax, px, (size_t)n * sizeof *ax);
    memcpy(ay, py, (size_t)n * sizeof *ay);
    for (int e = 0; e < m && cnt; e++) {
        const float ex0 = qx[e], ey0 = qy[e], ex1 = qx[(e + 1) % m], ey1 = qy[(e + 1) % m];
        int k = 0;
        for (int i = 0; i < cnt; i++) {
            const int j = (i + 1) % cnt;
            const float di = sg * ((ex1 - ex0) * (ay[i] - ey0) - (ey1 - ey0) * (ax[i] - ex0));
            const float dj = sg * ((ex1 - ex0) * (ay[j] - ey0) - (ey1 - ey0) * (ax[j] - ex0));
            if (di >= 0) {
                bx[k] = ax[i];
                by[k++] = ay[i];
            }
            if ((di >= 0) != (dj >= 0) && k < OMNI_POLY_MAX * 2) {
                const float t = di / (di - dj);
                bx[k] = ax[i] + (ax[j] - ax[i]) * t;
                by[k++] = ay[i] + (ay[j] - ay[i]) * t;
            }
            if (k >= OMNI_POLY_MAX * 2) break;
        }
        cnt = k;
        memcpy(ax, bx, (size_t)k * sizeof *ax);
        memcpy(ay, by, (size_t)k * sizeof *ay);
    }
    memcpy(ox, ax, (size_t)cnt * sizeof *ox);
    memcpy(oy, ay, (size_t)cnt * sizeof *oy);
    return cnt;
}

typedef struct {
    fpt_t ps[5], pl[4], pr[4], pt[OMNI_POLY_MAX * 2], po[5], pi[5], pq[4];
    shape_t S, L, R, T, B, Q;   /* shadow, lit, dark, tip, bevel (grown, minus shrunk), centre line */
    bool shadow;
    uint8_t lit[3], dark[3];
} hand_t;

static bool hand_build(hand_t *h, float a, bool hour, bool shadow, float k, float cx, float cy, float sx, float sy)
{
    float lx[5], ly[5], ox[5], oy[5], r1, w;
    hand_local(hour, lx, ly, &r1, &w);
    xf_t t = { cosf(a), sinf(a), k, cx, cy, sx, sy, 0, 0 };
    bool ok = true;
    h->shadow = shadow;
    if (shadow) {
        xf_t ts = t;
        ts.ox = 1.4f;
        ts.oy = 2.6f;
        ok = ok && xf_pts(&ts, lx, ly, 5, h->ps) && shape_init(&h->S, h->ps, 5, NULL, 0);
    }
    /* the facets: the outline's halves either side of its axis (the mockup's two fillRects in its clip) */
    const float hlx[4] = { lx[0], lx[1], 0, 0 }, hly[4] = { ly[0], ly[1], ly[2], -HAND_R0 };
    const float hrx[4] = { 0, 0, lx[3], lx[4] }, hry[4] = { -HAND_R0, ly[2], ly[3], ly[4] };
    ok = ok && xf_pts(&t, hlx, hly, 4, h->pl) && shape_init(&h->L, h->pl, 4, NULL, 0);
    ok = ok && xf_pts(&t, hrx, hry, 4, h->pr) && shape_init(&h->R, h->pr, 4, NULL, 0);
    /* the red tip, cut by the outline */
    float tx[OMNI_POLY_MAX * 2], ty[OMNI_POLY_MAX * 2];
    int nt;
    if (hour) {
        const float qx[4] = { 0, -w * 0.3f, 0, w * 0.3f }, qy[4] = { -r1, -(r1 - 9), -(r1 - 22), -(r1 - 9) };
        nt = clip_convex(qx, qy, 4, lx, ly, 5, tx, ty);
    } else {
        const float qx[4] = { -w, w, w, -w }, qy[4] = { -r1 - 2, -r1 - 2, -r1 + 9, -r1 + 9 };   /* fillRect(-w, -r1-2, 2w, 11) */
        nt = clip_convex(lx, ly, 5, qx, qy, 4, tx, ty);
    }
    ok = ok && nt >= 3 && xf_pts(&t, tx, ty, nt, h->pt) && shape_init(&h->T, h->pt, nt, NULL, 0);
    /* the bevel: grown by 0.35 minus shrunk by 0.35 (wound the other way round: a hole) */
    offset_local(lx, ly, 0.35f, ox, oy);
    ok = ok && xf_pts(&t, ox, oy, 5, h->po);
    offset_local(lx, ly, -0.35f, ox, oy);
    float rx[5], ry[5];
    for (int i = 0; i < 5; i++) {
        rx[i] = ox[4 - i];
        ry[i] = oy[4 - i];
    }
    ok = ok && xf_pts(&t, rx, ry, 5, h->pi) && shape_init(&h->B, h->po, 5, h->pi, 5);
    const float cxl[4] = { -0.25f, 0.25f, 0.25f, -0.25f }, cyl[4] = { -HAND_R0, -HAND_R0, -r1 + 1, -r1 + 1 };
    ok = ok && xf_pts(&t, cxl, cyl, 4, h->pq) && shape_init(&h->Q, h->pq, 4, NULL, 0);
    omni_hand_facets(a, h->lit, h->dark);
    return ok;
}

static void hand_extent(const hand_t *h, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    *x0 = h->B.xmin;
    *y0 = h->B.ymin;
    *x1 = h->B.xmax;
    *y1 = h->B.ymax;
    if (h->shadow) {
        *x0 = h->S.xmin < *x0 ? h->S.xmin : *x0;
        *y0 = h->S.ymin < *y0 ? h->S.ymin : *y0;
        *x1 = h->S.xmax > *x1 ? h->S.xmax : *x1;
        *y1 = h->S.ymax > *y1 ? h->S.ymax : *y1;
    }
}

typedef char omni_hand_size_check[sizeof(hand_t) <= OMNI_HAND_BYTES ? 1 : -1];

bool omni_hand_prepare(omni_hand_t *h, float a, bool hour, bool shadow, float k, float cx, float cy, float sx, float sy)
{
    return hand_build((hand_t *)(void *)h->b, a, hour, shadow, k, cx, cy, sx, sy);
}

void omni_draw_hand(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, bool shadow,
                    float k, float cx, float cy, float sx, float sy)
{
    omni_hand_t h;
    if (omni_hand_prepare(&h, a, hour, shadow, k, cx, cy, sx, sy)) omni_hand_draw(&h, fb, stride, clip);
}

void omni_hand_draw(const omni_hand_t *hp, uint16_t *fb, int stride, const omni_rect_t *clip)
{
    const hand_t *h = (const hand_t *)(const void *)hp->b;
    clip_t c;
    clip_init(&c, fb, stride, clip);
    int32_t fx0, fy0, fx1, fy1;
    int x0, y0, x1, y1;
    hand_extent(h, &fx0, &fy0, &fx1, &fy1);
    if (!clip_box(&c, fx0, fy0, fx1, fy1, &x0, &y0, &x1, &y1)) return;
    const shape_t *steps[6] = { h->shadow ? &h->S : NULL, &h->L, &h->R, &h->T, &h->B, &h->Q };
    const uint8_t col[6][3] = { { 0, 0, 0 }, { h->lit[0], h->lit[1], h->lit[2] }, { h->dark[0], h->dark[1], h->dark[2] },
                                { 180, 42, 23 }, { 214, 180, 118 }, { 255, 236, 190 } };   /* #b42a17 tip, bevel, centre */
    static const uint8_t al[6] = { SHADOW_A, 255, 255, 255, STROKE_A, CENTRE_A };
    for (int y = y0; y < y1; y++)
        for (int xa = x0; xa < x1; xa += OMNI_COVER_W) {
            const int xb = xa + OMNI_COVER_W < x1 ? xa + OMNI_COVER_W : x1, n = xb - xa;
            uint16_t *d = clip_px(&c, xa, y);
            /* the six steps in the mockup's order over 8-bit channels, by the one rule (blend565.h) but packed once at
             * the end: six truncating packs in a row would darken the hand by up to 20 levels */
            uint8_t w[3][OMNI_COVER_W];
            for (int i = 0; i < n; i++) {
                w[0][i] = (uint8_t)omni_ex_r(d[i]);
                w[1][i] = (uint8_t)omni_ex_g(d[i]);
                w[2][i] = (uint8_t)omni_ex_b(d[i]);
            }
            bool any = false;
            for (int s = 0; s < 6; s++) {
                if (!steps[s]) continue;
                int32_t acc[OMNI_COVER_W + 1];
                shape_row(steps[s], y, xa, xb, acc);
                resolve(acc, n);
                for (int i = 0; i < n; i++) {
                    if (!acc[i]) continue;
                    const int ap = omni_amul(cov_alpha(al[s], acc[i]));
                    for (int k = 0; k < 3; k++) w[k][i] = (uint8_t)omni_blend8(col[s][k], w[k][i], ap);
                    any = true;
                }
            }
            if (any)
                for (int i = 0; i < n; i++) d[i] = omni_pack565(w[0][i], w[1][i], w[2][i]);
        }
}

void omni_hand_bbox(float a, bool hour, omni_rect_t *out)
{
    hand_t h;
    if (!hand_build(&h, a, hour, true, 1, 0, 0, 0, 0)) {
        *out = (omni_rect_t){ 0, 0, 0, 0 };
        return;
    }
    int32_t x0, y0, x1, y1;
    hand_extent(&h, &x0, &y0, &x1, &y1);
    bbox_of(x0, y0, x1, y1, out);
}
