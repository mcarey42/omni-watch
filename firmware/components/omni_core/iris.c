#include "omni/iris.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#include "omni/blend565.h"
#ifndef ESP_PLATFORM
#include <assert.h>   /* host builds: a capacity overrun fails the test that hit it (R101 M2) */
#endif

/* The procedural iris (iris.h), a scanline renderer. The picture is made of 81 straight lines: the nine aperture edges
 * and the rays' 72 segments. Each line, on the rows it can matter on, gives every pixel its signed distance d (and its
 * position u along the segment) as one multiply-add of the column, so a row costs a few operations per active line and
 * a pixel nothing beyond what is near it:
 *  - pixels no line is near (beyond a ray's shadow, 9.5 px; beyond the aperture's shadow ring, 14.5 px inside; a pixel
 *    either side) are runs: a blade's colour table (gradient and grain by the distance along its normal), or the dial
 *    under the aperture times the dim;
 *  - the others are composed from the lines over them: the blade's table times the previous ray's shadow, or the dial
 *    times the dim and the ring; mixed across the nearest boundary by the pixel's exact area either side; the polished
 *    edges over that; the disc's edge.
 * A row's labels (which blade, or the aperture) come from a sweep: the blade at the disc's edge (from the rays' angles
 * there, which repeat every 40 deg) changed at each ray the row crosses. No division, square root or libm call per
 * pixel (the S3's are library calls: the first cut, per-row segment scans and a general per-pixel path, spent 230 ms a
 * frame). Single precision in a fixed order (-ffp-contract=off); every pixel's value depends on the frame and the
 * pixel alone (each value is computed from the pixel's column, never stepped from a run's start), so any clip gives
 * the same pixels. */

#define N OMNI_IRIS_N
#define C 233.0f
#define TAU_F 6.28318530717958647692f
#define D2R_F 0.01745329251994329577f
#define RO 199.71f        /* pixel centres beyond it are untouched (the disc's antialiasing reaches 199 + 0.71) */
#define RI 198.29f        /* inside it the disc's coverage is whole */
#define NL 224            /* the colour tables: u (the distance along the blade normal) from U0, a pixel a step */
#define U0 (-8.0f)
#define NEAR_RAY 9.5f     /* a pixel farther than this from a ray (on its shadow side): no shadow, edge or antialiasing */
#define NEAR_IN 14.5f     /* inside the aperture, an edge matters to this depth (the shadow ring, 13) */
#define NEAR_OUT 1.5f     /* and outside a line (antialiasing, polished edge) to this */
#define MAXACT 32         /* lines active on a row (a bit each) */
#define OPQ_AP 2.5f       /* omni_iris_opaque's hole: the aperture grown by 2.5 */
#define OPQ_R 197.5f      /* and the disc it calls opaque */
#define AP_LABEL (-1)

typedef struct { float h, m, ia, i2ab; } cov_t;            /* a line's pixel coverage for one normal (cov_half) */
enum { L_RAY, L_EDGE };
typedef struct {
    float nx, ny, c;          /* d(P) = n . P - c (P about the centre): + is blade k's side */
    float ex, ey, cu, len;    /* u(P) = e . P - cu: along the segment from its start */
    float inx, iex;           /* 1 / n.x, 1 / e.x (0 when near 0) */
    float dlo, dhi, ulo, uhi; /* where the line matters */
    float ax, ay, by, kx;     /* the segment's start, end row and dx / dy: its crossings of a row's centre */
    int16_t y0, y1;           /* the rows it can matter on */
    uint8_t kind, k;
    cov_t cv;
} line_t;
typedef struct {
    float ap, phi, s, vr, dimk;
    int dim8;
    bool blades, ring, glint, edges;   /* edges: the aperture is a polygon (ap > 0) */
    float glint_a;
    float nx[N], ny[N], inx[N], vx[N], vy[N];   /* n_i (inx = 1 / n_i.x, 0 when n_i is vertical); V(i) about the centre */
    line_t line[N + N * 8];            /* the edges, then ray k's segments at N + 8 k + j */
    float beta0;                       /* ray 0's angle where it crosses r = RO */
    float bx[N], by[N];                /* ray k's crossing of r = RO, as a unit vector (at beta0 + k 40 deg) */
    float ea[N];                       /* the polished edge's alpha */
    uint16_t lut[N][NL];
} iris_t;
typedef char omni_iris_size_check[sizeof(iris_t) <= sizeof(omni_iris_t) ? 1 : -1];
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static float ph(float t, float a, float b)
{
    if (t <= a) return 0;
    if (t >= b) return 1;
    return (t - a) / (b - a);
}
static float out_cubic(float s) { const float u = 1 - s; return 1 - u * u * u; }
static float in_out_cubic(float s)
{
    if (s < 0.5f) return 4 * s * s * s;
    const float u = -2 * s + 2;
    return 1 - u * u * u / 2;
}

void omni_iris_at(uint32_t t_ms, bool reduced, float *ap, float *phi_rad)
{
    const float tt = reduced ? (float)t_ms * 2.2f : (float)t_ms;
    const float anticip = out_cubic(ph(tt, 260, 430)) - in_out_cubic(ph(tt, 430, 560));
    const float s = in_out_cubic(ph(tt, 480, 1420));
    *ap = s * (float)OMNI_IRIS_AP_OPEN;
    *phi_rad = (-6.0f * anticip + 62.0f * s) * D2R_F;
}

/* V(i) and ray i (edge i continued from V(i + 1)) about the centre */
static void vertex(float ap, float phi, int i, float *x, float *y)
{
    const float vr = ap / cosf(TAU_F / (2 * N)), a = (float)(i % N) * (TAU_F / N) + phi - TAU_F / (2 * N);
    *x = vr * cosf(a);
    *y = vr * sinf(a);
}
static void ray(float ap, float phi, int i, float x[9], float y[9])
{
    const float a = (float)(i % N) * (TAU_F / N) + phi, nx = cosf(a), ny = sinf(a), tx = -ny, ty = nx;
    float x0, y0;
    vertex(ap, phi, i + 1, &x0, &y0);
    for (int k = 0; k <= 8; k++) {
        const float s = (float)k * 60.0f, q = s * s / 900.0f;
        x[k] = x0 + tx * s + nx * q;
        y[k] = y0 + ty * s + ny * q;
    }
}

int omni_iris_region(float ap, float phi, int i, omni_pt_t out[18])
{
    i = ((i % N) + N) % N;
    float x0, y0, x1, y1, rx[9], ry[9], px[9], py[9];
    vertex(ap, phi, i, &x0, &y0);
    vertex(ap, phi, i + 1, &x1, &y1);
    ray(ap, phi, i, rx, ry);
    ray(ap, phi, i + N - 1, px, py);
    out[0] = (omni_pt_t){ C + x0, C + y0 };
    out[1] = (omni_pt_t){ C + x1, C + y1 };
    for (int k = 1; k <= 8; k++) out[1 + k] = (omni_pt_t){ C + rx[k], C + ry[k] };
    for (int k = 8; k >= 1; k--) out[18 - k] = (omni_pt_t){ C + px[k], C + py[k] };
    return 18;
}

/* brass(l) of the mockup (its BR ramp, each channel `| 0`) */
static void brass(float l, float out[3])
{
    static const float BR[6][4] = { { 0, 8, 6, 3 },        { .22f, 38, 28, 13 },   { .45f, 80, 62, 31 },
                                    { .65f, 128, 101, 54 }, { .83f, 178, 146, 90 }, { 1, 230, 208, 152 } };
    l = clampf(l, 0, 1);
    int i = 1;
    while (i < 5 && l > BR[i][0]) i++;
    const float t = (l - BR[i - 1][0]) / (BR[i][0] - BR[i - 1][0]);
    for (int k = 0; k < 3; k++) out[k] = floorf(BR[i - 1][k + 1] + (BR[i][k + 1] - BR[i - 1][k + 1]) * t);
}

static uint16_t pack_round(float r, float g, float b)
{
    const int R = (int)(clampf(r, 0, 255) * (31.0f / 255.0f) + 0.5f), G = (int)(clampf(g, 0, 255) * (63.0f / 255.0f) + 0.5f),
              B = (int)(clampf(b, 0, 255) * (31.0f / 255.0f) + 0.5f);
    return (uint16_t)(R << 11 | G << 5 | B);
}

/* Blade i's colour at u: the gradient from V(i) along n_i over 230 px (stops lum + .12, lum at .25, lum - .14), then
 * the grain (the texture drawn 260 px wide from V(i): bilinear in its columns) as the luminance offset it stores. */
static void blade_rgb(const float g3[3][3], const uint8_t *grain, float u, float out[3])
{
    const float t = clampf(u / 230.0f, 0, 1);
    const int j = t < 0.25f ? 0 : 1;
    const float f = j ? (t - 0.25f) / 0.75f : t / 0.25f;
    for (int k = 0; k < 3; k++) out[k] = g3[j][k] + (g3[j + 1][k] - g3[j][k]) * f;
    if (!grain || u < 0 || u >= 260) return;
    const float tc = clampf(u * (256.0f / 260.0f) - 0.5f, 0, 255);
    const int i0 = (int)tc, i1 = i0 < 255 ? i0 + 1 : 255;
    const float g = (float)grain[i0] + (float)(grain[i1] - grain[i0]) * (tc - (float)i0) - 128.0f;
    static const float LIGHT[3] = { 255, 232, 180 };
    for (int k = 0; k < 3; k++) out[k] = g > 0 ? out[k] + (LIGHT[k] - out[k]) * (g / 107.0f) : out[k] * (1 + g / 128.0f);
}

static float fmn(float a, float b) { return a < b ? a : b; }
static float fmx(float a, float b) { return a > b ? a : b; }
static int ifloor(float v) { const int i = (int)v; return (float)i > v ? i - 1 : i; }

static cov_t cov_of(float nx, float ny)
{
    float a = fabsf(nx), b = fabsf(ny);
    if (a < b) {
        const float q = a;
        a = b;
        b = q;
    }
    return (cov_t){ (a + b) * 0.5f, (a - b) * 0.5f, 1.0f / a, b > 1e-6f ? 1.0f / (2 * a * b) : 0 };
}

/* the exact area of the unit pixel on the side z <= t of a line through its centre's offset (the projection of the
 * square onto the line's normal: a trapezoid) */
static float cov_half(float t, const cov_t *c)
{
    if (t <= -c->h) return 0;
    if (t >= c->h) return 1;
    if (t < -c->m) {
        const float q = t + c->h;
        return q * q * c->i2ab;
    }
    if (t > c->m) {
        const float q = c->h - t;
        return 1 - q * q * c->i2ab;
    }
    return 0.5f + t * c->ia;
}

/* A line from a to b (about the centre), n = (ey, -ex) its + side; or, given en (an edge's exact normal) and ec, the
 * line n . P = ec (d = n . P - ap for an edge) */
static void line_set(line_t *l, int kind, int k, float ax, float ay, float bx, float by, float dlo, float dhi, float ulo,
                     float uhi_extra, const float *en, float ec)
{
    const float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
    l->kind = (uint8_t)kind;
    l->k = (uint8_t)k;
    l->len = len;
    l->ex = len > 0 ? dx / len : 1;
    l->ey = len > 0 ? dy / len : 0;
    l->nx = en ? en[0] : l->ey;
    l->ny = en ? en[1] : -l->ex;
    l->c = en ? ec : l->nx * ax + l->ny * ay;
    l->cu = l->ex * ax + l->ey * ay;
    l->inx = fabsf(l->nx) > 1e-4f ? 1.0f / l->nx : 0;
    l->iex = fabsf(l->ex) > 1e-4f ? 1.0f / l->ex : 0;
    l->dlo = dlo;
    l->dhi = dhi;
    l->ulo = ulo;
    l->uhi = len + uhi_extra;
    l->ax = ax;
    l->ay = ay;
    l->by = by;
    l->kx = fabsf(dy) > 1e-6f ? dx / dy : 0;
    const float m = fmx(-dlo, dhi) + 1;
    l->y0 = (int16_t)ifloor(C + fmn(ay, by) - m);
    l->y1 = (int16_t)(ifloor(C + fmx(ay, by) + m) + 1);
    l->cv = cov_of(l->nx, l->ny);
}

bool omni_iris_prepare(omni_iris_t *ipu, float ap, float phi, float s, const uint8_t *grain)
{
    iris_t *I = (iris_t *)(void *)ipu->b;
    memset(I, 0, sizeof *I);
    if (!(s < 1) || !(ap >= 0) || !(phi == phi)) return false;
    I->ap = ap;
    I->phi = phi;
    I->s = s;
    I->vr = ap / cosf(TAU_F / (2 * N));
    I->dimk = 1 - 0.55f * (1 - clampf(s, 0, 1));
    I->dim8 = (int)floorf((1 - I->dimk) * 255 + 0.5f);
    I->blades = ap < (float)OMNI_IRIS_GONE;
    I->ring = ap > 1;
    I->edges = ap > 0;
    I->glint = ap < 30;
    I->glint_a = 0.22f * (1 - ap / 30.0f);
    for (int i = 0; i < N; i++) {
        const float a = (float)i * (TAU_F / N) + phi;
        I->nx[i] = cosf(a);
        I->ny[i] = sinf(a);
        I->inx[i] = fabsf(I->nx[i]) < 1e-6f ? 0 : 1.0f / I->nx[i];
        vertex(ap, phi, i, &I->vx[i], &I->vy[i]);
        const float c = cosf(a + 135.0f * D2R_F);
        I->ea[i] = 0.25f + 0.45f * (0.5f + 0.5f * c);
        float g3[3][3];
        const float lum = 0.3f + 0.17f * c;
        brass(lum + 0.12f, g3[0]);
        brass(lum, g3[1]);
        brass(lum - 0.14f, g3[2]);
        for (int j = 0; j < NL; j++) {
            float rgb[3];
            blade_rgb((const float (*)[3])g3, grain, U0 + (float)j + 0.5f, rgb);
            I->lut[i][j] = pack_round(rgb[0], rgb[1], rgb[2]);
        }
    }
    /* the edges: + is outside (blade k), the shadow ring reaches 13 inside; the polished path starts at V(k) (butt) and
     * joins ray k at V(k + 1). With no aperture (ap 0) there are none. */
    for (int k = 0; k < N; k++) {
        const int j = (k + 1) % N;
        const float en[2] = { I->nx[k], I->ny[k] };   /* exactly the edge's normal: d = n . P - ap */
        line_set(&I->line[k], L_EDGE, k, I->vx[k], I->vy[k], I->vx[j], I->vy[j], -NEAR_IN, NEAR_OUT, 0, NEAR_OUT, en, ap);
    }
    /* the rays: + is blade k, the shadow falls on blade k + 1 (the - side) to 8; segment 0 starts at V(k + 1) (butt:
     * behind it the edge is the line); the joints overlap by NEAR_OUT so the bends leave no notch */
    for (int k = 0; k < N; k++) {
        float rx[9], ry[9];
        ray(ap, phi, k, rx, ry);
        for (int j = 0; j < 8; j++)
            line_set(&I->line[N + 8 * k + j], L_RAY, k, rx[j], ry[j], rx[j + 1], ry[j + 1], -NEAR_RAY, NEAR_OUT,
                     j ? -NEAR_OUT : 0, NEAR_OUT, NULL, 0);
    }
    /* ray 0 crosses r = RO once (its radius grows along it); ray k crosses at beta0 + k 40 deg */
    I->beta0 = 0;
    for (int j = 0; j < 8; j++) {
        const line_t *l = &I->line[N + j];
        const float ax = l->ax, ay = l->ay, dx = l->ex * l->len, dy = l->ey * l->len;
        if (ax * ax + ay * ay >= RO * RO || (ax + dx) * (ax + dx) + (ay + dy) * (ay + dy) < RO * RO) continue;
        const float A = dx * dx + dy * dy, B = 2 * (ax * dx + ay * dy), Cc = ax * ax + ay * ay - RO * RO;
        const float t = (-B + sqrtf(B * B - 4 * A * Cc)) / (2 * A);
        I->beta0 = atan2f(ay + dy * t, ax + dx * t);
        break;
    }
    for (int k = 0; k < N; k++) {
        I->bx[k] = cosf(I->beta0 + (float)k * (TAU_F / N));
        I->by[k] = sinf(I->beta0 + (float)k * (TAU_F / N));
    }
    return true;
}

/* ---- A row */

typedef struct { float x; int k; bool n_right; } cross_t;     /* a ray crossing the row's centre line */
typedef struct { const line_t *l; float dA, dB, uA, uB; } act_t;   /* d(x) = dA + x dB, u(x) = uA + x uB (screen x) */

/* A row's events: where a line's zone (a span of its distance) begins or ends, and where the label may change */
enum { EV_BREAK, EV_SLOW, EV_LV1, EV_LV2, EV_DISC };
enum { ZT_BOUND, ZT_BAND };   /* a slow zone: across the line itself, or across a shadow band's edge */
typedef struct { int16_t col; uint8_t kind, idx; int8_t delta; uint8_t zt; } ev_t;
#define MAXEV 320
#define MAXCROSS 24

typedef struct {
    const iris_t *I;
    float yc, xo;                 /* the row's centre (about the centre) and the disc's half chord at RO */
    bool has_ap;
    float apl, apr;               /* the aperture's centre interval (about the centre) */
    cross_t cr[MAXCROSS];
    int nc;
    act_t act[MAXACT];
    int na;
    int ci, label;                /* the sweep's cursor */
    bool right;                   /* the sweep is past the aperture */
    ev_t ev[MAXEV];
    int ne;
} row_t;

/* the first column whose centre (about the centre) is past x (> x), and the first at or past it (>= x) */
static int col_past(float x) { return ifloor(x + C - 0.5f) + 1; }
static int col_from(float x) { const float v = x + C - 0.5f; const int i = ifloor(v); return (float)i == v ? i : i + 1; }

/* The aperture's interval on this row at inradius r: centres x (about the centre) with x n_k.x + yc n_k.y <= r */
static bool ap_interval(const iris_t *I, float yc, float r, float *lo, float *hi)
{
    *lo = -1e9f;
    *hi = 1e9f;
    for (int k = 0; k < N; k++) {
        const float c = yc * I->ny[k];
        if (I->inx[k] == 0) {
            if (c > r) return false;
            continue;
        }
        const float b = (r - c) * I->inx[k];
        if (I->nx[k] > 0) *hi = fmn(*hi, b);
        else *lo = fmx(*lo, b);
    }
    return *lo <= *hi;
}

/* The blade at a point on the circle r = RO (or the aperture), for the sweep's anchors */
static int anchor(const iris_t *I, float x, float y)
{
    if (I->vr >= RO) {   /* every vertex outside the disc: the disc outside the aperture is the edges' segments */
        int k = 0;
        float best = -1e9f;
        for (int i = 0; i < N; i++) {
            const float d = x * I->nx[i] + y * I->ny[i];
            if (d > best) best = d, k = i;
        }
        return best <= I->ap ? AP_LABEL : k;
    }
    /* between ray k's crossing and ray k + 1's (sectors of 40 deg, by cross products: no atan2 per row): blade k + 1 */
    for (int k = 0; k < N; k++) {
        const int j = (k + 1) % N;
        if (I->bx[k] * y - I->by[k] * x >= 0 && x * I->by[j] - y * I->bx[j] > 0) return j;
    }
    return 0;   /* (unreached: the nine sectors cover the circle) */
}

/* A row's capacities (events, active lines, crossings) overrun: counted, never silent (R101 M2). Measured maxima over
 * 8 343 frames: 125 events, 20 lines, 6 crossings. A relaxed counter: two cores may race an increment (it only has
 * to become nonzero). */
static volatile uint32_t s_overflows;
uint32_t omni_iris_overflows(void) { return s_overflows; }
static void overflow(void)
{
    s_overflows++;
#ifndef ESP_PLATFORM
    assert(!"iris: a row's capacity overran (MAXEV, MAXACT or MAXCROSS)");
#endif
}

static void ev_add(row_t *R, int col, int kind, int idx, int delta, int zt)
{
    if (R->ne < MAXEV) R->ev[R->ne++] = (ev_t){ (int16_t)col, (uint8_t)kind, (uint8_t)idx, (int8_t)delta, (uint8_t)zt };
    else overflow();
}

/* The columns [*c0, *c1) whose value v(x) = A + x B (x the column) lies in [lo, hi]; ib = 1 / B (0: B is near 0, v is
 * taken as A) */
static void span_cols(float A, float ib, float lo, float hi, int *c0, int *c1)
{
    if (ib == 0) {
        if (A >= lo && A <= hi) *c0 = -100000, *c1 = 100000;
        else *c0 = *c1 = 0;
        return;
    }
    const float p = (lo - A) * ib, q = (hi - A) * ib, mn = fmn(p, q), mx = fmx(p, q);
    const int a = ifloor(mn);
    *c0 = (float)a == mn ? a : a + 1;
    *c1 = ifloor(mx) + 1;
}

/* a zone of line ia over the columns its d is in [lo, hi] (and its u in range), as two events */
static void zone(row_t *R, int ia, int ua, int ub, int x0, int x1, float lo, float hi, int kind, int idx, int zt)
{
    const act_t *a = &R->act[ia];
    int c0, c1;
    span_cols(a->dA, a->l->inx, lo, hi, &c0, &c1);
    c0 = imax(imax(c0, ua), x0);
    c1 = imin(imin(c1, ub), x1);
    if (c0 >= c1) return;
    ev_add(R, c0, kind, idx, 1, zt);
    ev_add(R, c1, kind, idx, -1, zt);
}

/* The row's active lines and their zones, its ray crossings, the aperture and the disc's edge, as events in column
 * order. A ray's zones: slow across itself (|d| <= 1.3: its antialiasing and polished edge) and across its shadow
 * bands' edges (d -3 and -8, +-0.75); the bands themselves (level 2 to d -3, level 1 to d -8) on blade k + 1's side. An
 * edge's: slow across itself, and across the shadow ring's edges (d -5 and -13); the ring's levels inside. */
static void row_init(row_t *R, const iris_t *I, int y, int x0, int x1)
{
    R->I = I;
    const float yc = (float)y + 0.5f - C;
    R->yc = yc;
    R->xo = sqrtf(RO * RO - yc * yc);
    R->has_ap = I->edges && ap_interval(I, yc, I->ap, &R->apl, &R->apr);
    R->nc = R->na = R->ne = 0;
    for (int i = (I->edges ? 0 : N); i < N + 8 * N; i++) {
        const line_t *l = &I->line[i];
        if (y < l->y0 || y >= l->y1) continue;
        if (l->kind == L_RAY && (l->ay <= yc) != (l->by <= yc)) {   /* a crossing of the centre line */
            const float x = l->ax + (yc - l->ay) * l->kx;
            if (x > -R->xo && x < R->xo && R->nc >= MAXCROSS) overflow();
            else if (x > -R->xo && x < R->xo) {
                int c = R->nc++;   /* in order of x */
                while (c > 0 && R->cr[c - 1].x > x) R->cr[c] = R->cr[c - 1], c--;
                R->cr[c] = (cross_t){ x, l->k, l->nx > 0 };   /* the + side (blade k) on the right */
                ev_add(R, col_past(x), EV_BREAK, 0, 0, 0);
            }
        }
        if (R->na >= MAXACT - 1) {   /* bit 31 is the disc's edge */
            overflow();
            continue;
        }
        /* d and u along the row, from the screen column: P.x = x + 0.5 - C */
        const act_t a = { l, l->ny * yc - l->c + l->nx * (0.5f - C), l->nx, l->ey * yc - l->cu + l->ex * (0.5f - C), l->ex };
        int ua, ub;
        span_cols(a.uA, l->iex, l->ulo, l->uhi, &ua, &ub);
        ua = imax(ua, x0);
        ub = imin(ub, x1);
        if (ua >= ub) continue;
        const int ia = R->na++, ne0 = R->ne;
        R->act[ia] = a;
        if (l->kind == L_RAY) {
            zone(R, ia, ua, ub, x0, x1, -1.3f, 1.3f, EV_SLOW, ia, ZT_BOUND);
            zone(R, ia, ua, ub, x0, x1, -3.75f, -2.25f, EV_SLOW, ia, ZT_BAND);
            zone(R, ia, ua, ub, x0, x1, -8.75f, -7.25f, EV_SLOW, ia, ZT_BAND);
            zone(R, ia, ua, ub, x0, x1, -3, 0, EV_LV2, l->k, 0);
            zone(R, ia, ua, ub, x0, x1, -8, -3, EV_LV1, l->k, 0);
        } else {
            zone(R, ia, ua, ub, x0, x1, -1.3f, 1.3f, EV_SLOW, ia, ZT_BOUND);
            if (I->ring) {
                zone(R, ia, ua, ub, x0, x1, -5.75f, -4.25f, EV_SLOW, ia, ZT_BAND);
                zone(R, ia, ua, ub, x0, x1, -13.75f, -12.25f, EV_SLOW, ia, ZT_BAND);
                zone(R, ia, ua, ub, x0, x1, -5, 0, EV_LV2, N, 0);   /* N: the ring's slot */
                zone(R, ia, ua, ub, x0, x1, -13, -5, EV_LV1, N, 0);
            }
        }
        if (R->ne == ne0) R->na--;   /* no zone on this row: not active */
    }
    if (R->has_ap) {
        ev_add(R, col_from(R->apl), EV_BREAK, 0, 0, 0);
        ev_add(R, col_past(R->apr), EV_BREAK, 0, 0, 0);
    }
    {   /* the disc's edge: centres beyond RI */
        const float xi = yc * yc < RI * RI ? sqrtf(RI * RI - yc * yc) : 0;
        const int a = imin(col_past(-xi), x1), b = imax(col_from(xi), x0);
        if (a > x0) ev_add(R, x0, EV_DISC, 0, 1, 0), ev_add(R, a, EV_DISC, 0, -1, 0);
        if (b < x1) ev_add(R, b, EV_DISC, 0, 1, 0), ev_add(R, x1, EV_DISC, 0, -1, 0);
    }
    for (int i = 1; i < R->ne; i++) {   /* in column order */
        const ev_t e = R->ev[i];
        int j = i;
        while (j > 0 && R->ev[j - 1].col > e.col) R->ev[j] = R->ev[j - 1], j--;
        R->ev[j] = e;
    }
    R->ci = 0;
    R->label = anchor(I, -R->xo, yc);
    R->right = false;
}

/* The label at centre xc (about the centre); xc never decreases along a row */
static int row_label(row_t *R, float xc)
{
    if (R->has_ap && xc >= R->apl && xc <= R->apr) return AP_LABEL;
    if (R->has_ap && xc > R->apr && !R->right) {   /* past the aperture: from the right anchor, leftward */
        R->right = true;
        int l = anchor(R->I, R->xo, R->yc);
        for (int c = R->nc - 1; c >= 0 && R->cr[c].x > xc; c--) l = R->cr[c].n_right ? (R->cr[c].k + 1) % N : R->cr[c].k;
        R->label = l;
        R->ci = 0;
        while (R->ci < R->nc && R->cr[R->ci].x < xc) R->ci++;
        return l;
    }
    while (R->ci < R->nc && R->cr[R->ci].x < xc) {
        R->label = R->cr[R->ci].n_right ? R->cr[R->ci].k : (R->cr[R->ci].k + 1) % N;
        R->ci++;
    }
    return R->label;
}

static int lut_idx(float u)
{
    const float v = u - U0;
    if (!(v > 0)) return 0;
    const int j = (int)v;   /* truncation: the floor of a positive value */
    return j >= NL ? NL - 1 : j;
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* A pixel some line is near, composed from those lines: the colour of each side of the nearest boundary (the
 * aperture: the dial dimmed and shadowed by the ring; a blade: its table, shadowed by the previous ray), mixed by the
 * pixel's area either side; the polished edges over it; the disc's edge. */
static uint16_t px_lines(const row_t *R, int x, uint32_t bm, int label, uint16_t dst)
{
    const iris_t *I = R->I;
    const float fx = (float)x, xc = fx + 0.5f - C, yc = R->yc;
    float rd[N], sd[N];   /* per ray: |d| to it on its shadow side; per polished path: |d| */
    uint32_t rk = 0, sk = 0;
    float bd = 1.0f, bdn = 0, dmax = -1e9f;
    const cov_t *bcv = NULL;
    int bpos = -2, bneg = -2;
    for (uint32_t m = bm & 0x7fffffffu; m; m &= m - 1) {
        const act_t *a = &R->act[__builtin_ctz(m)];
        const line_t *l = a->l;
        const float d = a->dA + fx * a->dB, u = a->uA + fx * a->uB;
        if (u < l->ulo || u > l->uhi) continue;
        const float ad = fabsf(d);
        const int k = l->k;
        if (l->kind == L_EDGE) {
            if (d > dmax) dmax = d;
        } else if (d < 0.5f) {   /* the shadow side (and the line itself) */
            const float sdist = d < 0 ? -d : 0;
            if (!(rk >> k & 1) || sdist < rd[k]) rd[k] = sdist;
            rk |= 1u << k;
        }
        if (ad < 1.3f) {
            if (!(sk >> k & 1) || ad < sd[k]) sd[k] = ad;
            sk |= 1u << k;
        }
        if (ad < bd && u >= 0 && u <= l->len) {
            bd = ad;
            bdn = d;
            bcv = &l->cv;
            bpos = k;
            bneg = l->kind == L_EDGE ? AP_LABEL : (k + 1) % N;
        }
    }
    float side[2][3];
    const int labs[2] = { bpos, bneg };
    const bool mix = bcv && (label == bpos || label == bneg);
    for (int q = 0; q < (mix ? 2 : 1); q++) {
        const int L = mix ? labs[q] : label;
        float *o = side[q];
        float f;
        uint16_t c;
        if (L == AP_LABEL) {
            c = dst;
            f = I->dimk;
            if (I->ring && dmax > -1e8f) {
                const float din = -dmax;
                f *= (1 - 0.55f * clamp01(13.5f - din)) * (1 - 0.5f * clamp01(5.5f - din));
            }
        } else {
            c = I->lut[L][lut_idx(xc * I->nx[L] + yc * I->ny[L] - I->ap)];
            const int kp = (L + N - 1) % N;
            f = 1;
            if (rk >> kp & 1) f = (1 - 0.35f * clamp01(8.5f - rd[kp])) * (1 - 0.45f * clamp01(3.5f - rd[kp]));
        }
        o[0] = (float)omni_ex_r(c) * f;
        o[1] = (float)omni_ex_g(c) * f;
        o[2] = (float)omni_ex_b(c) * f;
    }
    float col[3] = { side[0][0], side[0][1], side[0][2] };
    if (mix) {
        const float cp = cov_half(bdn, bcv);   /* the pixel's share on the + side (bpos) */
        for (int q = 0; q < 3; q++) col[q] = side[1][q] + (side[0][q] - side[1][q]) * cp;
    }
    for (uint32_t m = sk; m; m &= m - 1) {
        const int k = __builtin_ctz(m);
        const float c = clamp01(1.05f - sd[k]) * I->ea[k];
        col[0] += (255 - col[0]) * c;
        col[1] += (228 - col[1]) * c;
        col[2] += (170 - col[2]) * c;
    }
    if (bm >> 31) {   /* the disc's edge */
        const float cd = clamp01((float)OMNI_IRIS_R + 0.5f - sqrtf(xc * xc + yc * yc));
        const float d0[3] = { (float)omni_ex_r(dst), (float)omni_ex_g(dst), (float)omni_ex_b(dst) };
        for (int q = 0; q < 3; q++) col[q] = d0[q] + (col[q] - d0[q]) * cd;
    }
    return pack_round(col[0], col[1], col[2]);
}

/* the dim alone (no blades: ap >= OMNI_IRIS_GONE), the disc's edge antialiased */
static void row_dim(const iris_t *I, uint16_t *d, float yc, int x0, int x1)
{
    for (int x = x0; x < x1; x++) {
        const float xc = (float)x + 0.5f - C, r2 = xc * xc + yc * yc;
        int a = I->dim8;
        if (r2 > RI * RI) a = (int)((1 - I->dimk) * clamp01((float)OMNI_IRIS_R + 0.5f - sqrtf(r2)) * 255 + 0.5f);
        if (a) d[x] = omni_blend565(d[x], 0, a);
    }
}

/* The closed centre's glint: createRadialGradient(C - 8, C - 10, 0, C, C, 40) over fillRect(C - 40, C - 40, 80, 80):
 * the largest t >= 0 with |q - c(t)| = 40 t, alpha glint_a (1 - t) of (255, 236, 190) */
static void row_glint(const iris_t *I, uint16_t *d, float yc, int x0, int x1)
{
    const float qy = yc + 10, ia = 1.0f / (164 - 1600);
    x0 = imax(x0, 193);
    x1 = imin(x1, 273);
    if (yc + C < 193 || yc + C >= 273) return;
    for (int x = x0; x < x1; x++) {
        const float qx = (float)x + 0.5f - C + 8, qd = qx * 8 + qy * 10;
        const float t = (qd - sqrtf(qd * qd - (164 - 1600) * (qx * qx + qy * qy))) * ia;
        if (t >= 1) continue;
        const int al = (int)(I->glint_a * (1 - t) * 255 + 0.5f);
        if (al) d[x] = omni_blend565(d[x], omni_pack565(255, 236, 190), al);
    }
}

/* The colour a pixel of label L has away from every line, from the levels the row's sweep holds: a blade's table times
 * the previous ray's shadow, or the dial times the dim and the shadow ring */
typedef struct { uint8_t l1[N + 1], l2[N + 1]; } lv_t;
static float blade_f(const lv_t *v, int k)
{
    const int kp = (k + N - 1) % N;
    return v->l2[kp] ? 0.65f * 0.55f : v->l1[kp] ? 0.65f : 1;
}
static float ap_f(const iris_t *I, const lv_t *v)
{
    return I->dimk * (!I->ring ? 1 : v->l2[N] ? 0.45f * 0.5f : v->l1[N] ? 0.45f : 1);
}
static int a8_of(float f) { return (int)((1 - f) * 255 + 0.5f); }

/* the blade table's index along a row in Q16 from the row's origin: u(x) = base + x step (integers, so a pixel's index
 * never depends on where its run began) */
typedef struct { int32_t base, step; const uint16_t *lut; } bidx_t;
static bidx_t bidx(const row_t *R, int k)
{
    const iris_t *I = R->I;
    const float nx = I->nx[k];
    return (bidx_t){ (int32_t)floorf((R->yc * I->ny[k] - I->ap - U0 + (0.5f - C) * nx) * 65536.0f + 0.5f),
                     (int32_t)floorf(nx * 65536.0f + 0.5f), I->lut[k] };
}
static inline uint16_t bpx(const bidx_t *b, int x)
{
    int j = (int)((b->base + x * b->step) >> 16);
    j = j < 0 ? 0 : j >= NL ? NL - 1 : j;
    return b->lut[j];
}
/* columns [x, e) with no line near: runs */
static void seg_fast(const row_t *R, const lv_t *v, int label, uint16_t *d, int x, int e)
{
    const iris_t *I = R->I;
    if (label == AP_LABEL) {
        const int a = a8_of(ap_f(I, v));
        if (a)
            for (int q = x; q < e; q++) d[q] = omni_blend565(d[q], 0, a);
        return;
    }
    const int a = a8_of(blade_f(v, label));
    const bidx_t b = bidx(R, label);
    if (a)
        for (int q = x; q < e; q++) d[q] = omni_blend565(bpx(&b, q), 0, a);
    else
        for (int q = x; q < e; q++) d[q] = bpx(&b, q);
}

static const uint16_t E565 = (255 >> 3) << 11 | (228 >> 2) << 5 | (170 >> 3);   /* the polished edge's (255, 228, 170) */
static int a8f(float a) { return (int)(a * 255 + 0.5f); }

/* Across one line alone (the common case): its boundary (two colours mixed by the pixel's area either side, the
 * polished edge over them) or one of its bands' edges (the shadow or ring from its distance); false: not this case.
 * Integer blends (omni_blend565) with float weights only. */
static bool seg_line(const row_t *R, const lv_t *v, int label, const act_t *a, int zt, uint16_t *d, int x, int e)
{
    const iris_t *I = R->I;
    const line_t *l = a->l;
    const int k = l->k;
    if (zt == ZT_BAND) {
        if (l->kind == L_RAY) {
            if (label != (k + 1) % N) return false;
            const bidx_t b = bidx(R, label);
            for (int q = x; q < e; q++) {
                const float dist = -(a->dA + (float)q * a->dB);
                const float f = (1 - 0.35f * clamp01(8.5f - dist)) * (1 - 0.45f * clamp01(3.5f - dist));
                d[q] = omni_blend565(bpx(&b, q), 0, a8f(1 - f));
            }
        } else {
            if (label != AP_LABEL || v->l1[N] + v->l2[N] > 1) return false;   /* another edge's ring here too */
            for (int q = x; q < e; q++) {
                const float din = -(a->dA + (float)q * a->dB);
                const float f = I->dimk * (1 - 0.55f * clamp01(13.5f - din)) * (1 - 0.5f * clamp01(5.5f - din));
                d[q] = omni_blend565(d[q], 0, a8f(1 - f));
            }
        }
        return true;
    }
    const int neg = l->kind == L_RAY ? (k + 1) % N : AP_LABEL;
    if (label != k && label != neg) return false;
    const bidx_t bk = bidx(R, k), bn = neg == AP_LABEL ? bk : bidx(R, neg);
    const int ak = a8_of(blade_f(v, k)), an = a8_of(neg == AP_LABEL ? I->dimk * (I->ring ? 0.45f * 0.5f : 1) : 0.65f * 0.55f);
    const float ea = I->ea[k];
    for (int q = x; q < e; q++) {
        const float dd = a->dA + (float)q * a->dB;
        const uint16_t cp = omni_blend565(bpx(&bk, q), 0, ak);
        const uint16_t cn = omni_blend565(neg == AP_LABEL ? d[q] : bpx(&bn, q), 0, an);
        const uint16_t c = omni_blend565(cn, cp, a8f(cov_half(dd, &l->cv)));
        d[q] = omni_blend565(c, E565, a8f(clamp01(1.05f - fabsf(dd)) * ea));
    }
    return true;
}

/* Row y, screen columns [x0, x1) (d is the row's screen column 0) */
/* R: the row's scratch (2.9 KB), the caller's, kept off this frame so its locals stay within the loads' reach */
static void draw_row(const iris_t *I, uint16_t *d, int y, int x0, int x1, row_t *R)
{
    const float yc = (float)y + 0.5f - C;
    if (yc * yc >= RO * RO) return;
    const float xo = sqrtf(RO * RO - yc * yc);
    x0 = imax(x0, col_from(-xo));
    x1 = imin(x1, col_past(xo - 1e-4f));
    if (x0 >= x1) return;
    if (!I->blades) {
        row_dim(I, d, yc, x0, x1);
        return;
    }
    row_init(R, I, y, x0, x1);
    lv_t v;
    memset(&v, 0, sizeof v);
    uint8_t zt[MAXACT];
    uint32_t slow = 0, any = 0;
    uint8_t cnt[MAXACT];   /* each line's slow zones open here (two of one line meet at a column) */
    memset(cnt, 0, sizeof cnt);
    int disc = 0, ei = 0;
    for (int x = x0; x < x1;) {
        while (ei < R->ne && R->ev[ei].col <= x) {   /* the events at x */
            const ev_t *e = &R->ev[ei++];
            switch (e->kind) {
            case EV_SLOW:
                cnt[e->idx] = (uint8_t)(cnt[e->idx] + e->delta);
                if (e->delta > 0) zt[e->idx] = e->zt;
                if (cnt[e->idx]) slow |= 1u << e->idx;
                else slow &= ~(1u << e->idx);
                break;
            case EV_LV1:
                v.l1[e->idx] = (uint8_t)(v.l1[e->idx] + e->delta);
                break;
            case EV_LV2:
                v.l2[e->idx] = (uint8_t)(v.l2[e->idx] + e->delta);
                break;
            case EV_DISC:
                disc += e->delta;
                break;
            default:
                break;
            }
        }
        const int e = ei < R->ne ? imin(R->ev[ei].col, x1) : x1;
        if (e <= x) continue;
        const int label = row_label(R, (float)x + 0.5f - C);
        if (!slow && !disc) {
            seg_fast(R, &v, label, d, x, e);
        } else if (!disc && !(slow & (slow - 1)) && seg_line(R, &v, label, &R->act[__builtin_ctz(slow)], zt[__builtin_ctz(slow)], d, x, e)) {
        } else if (!slow) {   /* the disc's edge alone */
            const bool ap = label == AP_LABEL;
            const bidx_t b = bidx(R, ap ? 0 : label);
            const int af = a8_of(ap ? ap_f(I, &v) : blade_f(&v, label));
            for (int q = x; q < e; q++) {
                const float xc = (float)q + 0.5f - C, r = sqrtf(xc * xc + yc * yc), cd = clamp01((float)OMNI_IRIS_R + 0.5f - r);
                const uint16_t c = omni_blend565(ap ? d[q] : bpx(&b, q), 0, af);
                d[q] = omni_blend565(d[q], c, a8f(cd));
            }
        } else {
            any = slow;   /* the lines with any zone here: those the general case reads */
            for (int i = 0; i < R->na; i++) {
                const line_t *l = R->act[i].l;
                const int sl = l->kind == L_RAY ? l->k : N;
                if (v.l1[sl] || v.l2[sl]) any |= 1u << i;
            }
            const uint32_t bm = (any & 0x7fffffffu) | (disc ? 1u << 31 : 0);
            for (int q = x; q < e; q++) d[q] = px_lines(R, q, bm, label, d[q]);
        }
        x = e;
    }
    if (I->glint) row_glint(I, d, yc, x0, x1);
}

void omni_iris_draw_prepared(const omni_iris_t *ip, uint16_t *fb, int stride, const omni_rect_t *clip)
{
    const iris_t *I = (const iris_t *)(const void *)ip->b;
    const omni_rect_t all = { 0, 0, 466, 466 };
    const omni_rect_t *c = clip ? clip : &all;
    const int y0 = imax(c->y0, OMNI_IRIS_BOX.y0), y1 = imin(c->y1, OMNI_IRIS_BOX.y1);
    const int x0 = imax(c->x0, OMNI_IRIS_BOX.x0), x1 = imin(c->x1, OMNI_IRIS_BOX.x1);
    if (!(I->s < 1) || x0 >= x1) return;
    row_t R;
    for (int y = y0; y < y1; y++) draw_row(I, fb + (ptrdiff_t)(y - c->y0) * stride - c->x0, y, x0, x1, &R);
}

void omni_iris_draw(uint16_t *fb, int stride, const omni_rect_t *clip, float ap, float phi, float s, const uint8_t *grain)
{
    omni_iris_t ip;
    if (omni_iris_prepare(&ip, ap, phi, s, grain)) omni_iris_draw_prepared(&ip, fb, stride, clip);
}

void omni_iris_opaque(const omni_iris_t *ip, int y0, int y1, int x0, int x1, int16_t out[4])
{
    const iris_t *I = (const iris_t *)(const void *)ip->b;
    int o0 = x0, o1 = x1, h0 = x1, h1 = x0;
    if (!(I->s < 1) || !I->blades) o1 = o0;
    for (int y = y0; y < y1 && o0 < o1; y++) {
        const float yc = (float)y + 0.5f - C;
        if (yc * yc >= OPQ_R * OPQ_R) {
            o1 = o0;
            break;
        }
        const float xi = sqrtf(OPQ_R * OPQ_R - yc * yc);
        o0 = imax(o0, col_from(-xi));
        o1 = imin(o1, col_past(xi));
        float lo, hi;
        if (I->edges && ap_interval(I, yc, I->ap + OPQ_AP, &lo, &hi)) {
            h0 = imin(h0, col_from(lo) - 1);
            h1 = imax(h1, col_past(hi) + 1);
        }
    }
    if (o0 >= o1) o0 = o1 = x0;
    if (h0 >= h1) h0 = h1 = o0;
    out[0] = (int16_t)o0;
    out[1] = (int16_t)o1;
    out[2] = (int16_t)imax(h0, o0);
    out[3] = (int16_t)imax(imin(h1, o1), imax(h0, o0));
}
