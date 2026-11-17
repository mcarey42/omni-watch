#include "unity.h"
#include "omni/blend565.h"
#include "omni/iris.h"
#include "omni/omni_model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The brass iris (Stage 3 Task 10): the timeline and the geometry against the mockup's numbers (faces-omni.js 736 and
 * 949-956, computed with node from the mockup's own formulas), the regions as a partition, the procedural draw
 * against a 16 x 16 supersampled float oracle of drawIris, and the model's iris state. */

void setUp(void) {}
void tearDown(void) {}

#define W 466
#define D2R 0.017453292519943295
#define CX 233.0
#define PI_D 3.14159265358979323846

static uint8_t g_grain_file[131084];
static const uint8_t *g_grain;   /* omni_blade's A8 plane */

static void load_grain(void)
{
    if (g_grain) return;
    char path[512];
    snprintf(path, sizeof path, "%s/omni_blade.bin", ASSETS_DIR);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    const size_t n = fread(g_grain_file, 1, sizeof g_grain_file, f);
    fclose(f);
    TEST_ASSERT_EQUAL_size_t(sizeof g_grain_file, n);
    TEST_ASSERT_EQUAL_HEX8(0x19, g_grain_file[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0e, g_grain_file[1]);   /* A8 */
    TEST_ASSERT_EQUAL(256, g_grain_file[4] | g_grain_file[5] << 8);
    TEST_ASSERT_EQUAL(512, g_grain_file[6] | g_grain_file[7] << 8);
    g_grain = g_grain_file + 12;
    for (int y = 1; y < 512; y++)   /* the mockup's stripes run the texture's full height: every row is row 0 */
        TEST_ASSERT_EQUAL_MEMORY(g_grain, g_grain + y * 256, 256);
}

/* ---- The timeline and the geometry */

static void test_iris_timeline(void)
{
    static const struct { uint32_t t; float ap, phi_deg; } T[] = {
        { 0, 0, 0 }, { 300, 0, -3.3169f }, { 430, 0, -6 }, { 560, 0.510f, 0.1529f }, { 950, 103.5f, 31 }, { 1420, 207, 62 },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        float ap, phi;
        omni_iris_at(T[i].t, false, &ap, &phi);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, T[i].ap, ap);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, T[i].phi_deg, phi / (float)D2R);
    }
    /* after the end it holds; reduced motion runs at t x 2.2 (node: 300 -> ap 5.81386, phi 1.74135 deg) */
    float ap, phi;
    omni_iris_at(5000, false, &ap, &phi);
    TEST_ASSERT_EQUAL_FLOAT(207, ap);
    omni_iris_at(300, true, &ap, &phi);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 5.81386f, ap);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.74135f, phi / (float)D2R);
}

static void test_iris_vertices(void)
{
    omni_pt_t p[18];
    TEST_ASSERT_EQUAL(18, omni_iris_region(100, 0, 0, p));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 333.000f, p[0].x);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 196.603f, p[0].y);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 333.000f, p[1].x);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 269.397f, p[1].y);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 337.000f, p[2].x);   /* ray point 1 from v1 */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 329.397f, p[2].y);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 589.000f, p[9].x);   /* ray point 8 */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 749.397f, p[9].y);
    /* then the neighbour's ray (edge 8 continued from v0), point 8 first, point 1 last */
    omni_pt_t q[18];
    omni_iris_region(100, 0, 8, q);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, q[9].x, p[10].x);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, q[9].y, p[10].y);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, q[2].x, p[17].x);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, q[2].y, p[17].y);
}

/* ---- Exact area of a polygon inside a pixel (Sutherland-Hodgman against the square: exact for any simple subject) */

typedef struct { double x, y; } dpt;

static int clip_half(const dpt *in, int n, dpt *out, int axis, double v, bool keep_below)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        const dpt a = in[i], b = in[(i + 1) % n];
        const double ca = axis ? a.y : a.x, cb = axis ? b.y : b.x;
        const bool ia = keep_below ? ca <= v : ca >= v, ib = keep_below ? cb <= v : cb >= v;
        if (ia) out[m++] = a;
        if (ia != ib) {
            const double t = (v - ca) / (cb - ca);
            out[m++] = (dpt){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
        }
    }
    return m;
}

static double area_in_px(const dpt *poly, int n, int px, int py)
{
    dpt a[64], b[64];
    int m = clip_half(poly, n, a, 0, px, false);
    m = clip_half(a, m, b, 0, px + 1, true);
    m = clip_half(b, m, a, 1, py, false);
    m = clip_half(a, m, b, 1, py + 1, true);
    double s = 0;
    for (int i = 0; i < m; i++) s += b[i].x * b[(i + 1) % m].y - b[(i + 1) % m].x * b[i].y;
    return fabs(s) / 2;
}

static bool pip(const dpt *p, int n, double x, double y)
{
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if ((p[i].y > y) != (p[j].y > y) && x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x) in = !in;
    return in;
}

static double seg_dist(dpt p, dpt a, dpt b)
{
    const double ex = b.x - a.x, ey = b.y - a.y, l2 = ex * ex + ey * ey;
    double t = l2 > 0 ? ((p.x - a.x) * ex + (p.y - a.y) * ey) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypot(p.x - a.x - ex * t, p.y - a.y - ey * t);
}

/* The nine regions at ap, phi as double polygons (from omni_iris_region) and their boxes. */
typedef struct { dpt p[18]; double x0, y0, x1, y1; } dreg_t;
static void regions(float ap, float phi, dreg_t R[9])
{
    for (int i = 0; i < 9; i++) {
        omni_pt_t q[18];
        omni_iris_region(ap, phi, i, q);
        R[i].x0 = R[i].y0 = 1e9;
        R[i].x1 = R[i].y1 = -1e9;
        for (int k = 0; k < 18; k++) {
            R[i].p[k] = (dpt){ q[k].x, q[k].y };
            R[i].x0 = fmin(R[i].x0, q[k].x);
            R[i].y0 = fmin(R[i].y0, q[k].y);
            R[i].x1 = fmax(R[i].x1, q[k].x);
            R[i].y1 = fmax(R[i].y1, q[k].y);
        }
    }
}

static uint16_t g_f0[W * W], g_f1[W * W];

/* The renderer's own partition (R101 M1): the frame drawn over two fills (all 0, all 0xffff). A pixel more than 2 px
 * outside the aperture and inside r 197.5 must not depend on the fill (a blade covers it: no gap in the renderer's
 * labels); one more than 2 px inside the aperture must (the dial shows: no blade drawn there). */
static void renderer_partition(float ap, float phi)
{
    for (int i = 0; i < W * W; i++) g_f0[i] = 0, g_f1[i] = 0xffff;
    omni_iris_draw(g_f0, W, NULL, ap, phi, ap / OMNI_IRIS_AP_OPEN, g_grain);
    omni_iris_draw(g_f1, W, NULL, ap, phi, ap / OMNI_IRIS_AP_OPEN, g_grain);
    int out = 0, in = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const double px = x + 0.5 - CX, py = y + 0.5 - CX;
            double dmax = -1e9;
            for (int k = 0; k < 9; k++) {
                const double al = (40 * k) * D2R + phi;
                dmax = fmax(dmax, px * cos(al) + py * sin(al));
            }
            const bool same = g_f0[y * W + x] == g_f1[y * W + x];
            if (hypot(px, py) < 197.5 && dmax > ap + 2 && ap < OMNI_IRIS_GONE) {
                out++;
                if (!same) printf("ap %.2f: (%d, %d) outside the aperture reads the frame\n", ap, x, y);
                TEST_ASSERT_TRUE(same);
            } else if (dmax < ap - 2 && hypot(px, py) < 199.71) {
                in++;
                if (same) printf("ap %.2f: (%d, %d) inside the aperture is covered\n", ap, x, y);
                TEST_ASSERT_FALSE(same);
            }
        }
    TEST_ASSERT_TRUE(out + in > 100000);
}

/* At 12 apertures, every pixel wholly inside the disc r < 199 is covered 255 +- 2 by the nine regions and the aperture
 * together (each pixel's exact area in each, rounded to 255): no gap, no double cover; and a pixel wholly inside the
 * aperture (inradius ap) has 0 blade coverage. The same apertures through the renderer itself (renderer_partition),
 * including 199.4 (blades can still antialias the disc's edge) and 207 (open: nothing drawn). */
static void test_iris_partition(void)
{
    static const float AP[12] = { 0, 0.51f, 8, 29, 60, 103.5f, 150, 186, 196, 199.4f, 202.5f, 207 };
    load_grain();
    for (int a = 0; a < 12; a++) {
        const float ap = AP[a], phi = (float)((-6 + 68 * a / 11.0) * D2R);
        renderer_partition(ap, phi);
        dreg_t R[9];
        regions(ap, phi, R);
        const double vr = ap / cos(PI_D / 9);
        dpt apoly[9];
        for (int k = 0; k < 9; k++) {
            const double al = (40 * k - 20) * D2R + phi;
            apoly[k] = (dpt){ CX + vr * cos(al), CX + vr * sin(al) };
        }
        int checked = 0, inside = 0;
        for (int py = 34; py < 432; py++)
            for (int px = 34; px < 432; px++) {
                int in_disc = 0;
                for (int c = 0; c < 4; c++) in_disc += hypot(px + (c & 1) - CX, py + (c >> 1) - CX) < 199;
                if (in_disc != 4) continue;
                const dpt c = { px + 0.5, py + 0.5 };
                double apc = 0;
                if (ap > 0) {
                    bool near = false;
                    for (int k = 0; k < 9 && !near; k++) near = seg_dist(c, apoly[k], apoly[(k + 1) % 9]) < 0.75;
                    apc = near ? area_in_px(apoly, 9, px, py) : pip(apoly, 9, c.x, c.y) ? 1 : 0;
                }
                int sum = 0;
                for (int i = 0; i < 9; i++) {
                    if (px + 1 < R[i].x0 || px > R[i].x1 || py + 1 < R[i].y0 || py > R[i].y1) continue;
                    bool near = false;
                    for (int k = 0; k < 18 && !near; k++) near = seg_dist(c, R[i].p[k], R[i].p[(k + 1) % 18]) < 0.75;
                    const double cov = near ? area_in_px(R[i].p, 18, px, py) : pip(R[i].p, 18, c.x, c.y) ? 1 : 0;
                    sum += (int)floor(cov * 255 + 0.5);
                }
                if (apc >= 1) {
                    inside++;
                    if (sum != 0) printf("ap %.2f: (%d, %d) in the aperture has blade coverage %d\n", ap, px, py, sum);
                    TEST_ASSERT_EQUAL(0, sum);
                    continue;
                }
                checked++;
                const int total = sum + (int)floor(apc * 255 + 0.5);
                if (abs(total - 255) > 2) printf("ap %.2f: (%d, %d) covered %d + aperture %.3f (vr %.2f)\n", ap, px, py, sum, apc, vr);
                TEST_ASSERT_INT_WITHIN(2, 255, total);
            }
        TEST_ASSERT_TRUE(ap > 190 || checked > 1000);   /* past 199 the aperture holds the whole disc */
        if (ap > 20) TEST_ASSERT_TRUE(inside > 0);
    }
}

/* ---- The oracle: drawIris (faces-omni.js 736) and the dim before it (956), per sample point, in double: the canvas
 * operations as an ideal (unantialiased) picture, averaged over 16 x 16 samples a pixel where any edge of it passes
 * near; elsewhere the pixel's centre. */

static void brass(double l, double out[3])
{
    static const double BR[6][4] = { { 0, 8, 6, 3 },        { .22, 38, 28, 13 },   { .45, 80, 62, 31 },
                                     { .65, 128, 101, 54 }, { .83, 178, 146, 90 }, { 1, 230, 208, 152 } };
    l = l < 0 ? 0 : l > 1 ? 1 : l;
    int i = 1;
    while (i < 5 && l > BR[i][0]) i++;
    const double t = (l - BR[i - 1][0]) / (BR[i][0] - BR[i - 1][0]);
    for (int k = 0; k < 3; k++) out[k] = floor(BR[i - 1][k + 1] + (BR[i][k + 1] - BR[i - 1][k + 1]) * t);
}

typedef struct {
    double ap, phi, s, vr;
    dpt V[9];
    dpt B[9][10];         /* B[i]: V(i), V(i + 1), ray i's points 1..8: the polished edge's path; ray i is B[i][1..9] */
    double n[9][2], t[9][2], lum[9], ea[9];
    double grad[9][3][3]; /* blade i's gradient stops (0, .25, 1) */
    dreg_t R[9];
    double ly_min, ly_max;
} oracle_t;

static void oracle_init(oracle_t *o, double ap, double phi, double s)
{
    memset(o, 0, sizeof *o);
    o->ap = ap;
    o->phi = phi;
    o->s = s;
    o->vr = ap / cos(PI_D / 9);
    for (int i = 0; i < 9; i++) {
        const double a = i * 40 * D2R + phi - 20 * D2R;
        o->V[i] = (dpt){ CX + o->vr * cos(a), CX + o->vr * sin(a) };
    }
    for (int i = 0; i < 9; i++) {
        const double a = i * 40 * D2R + phi;
        o->n[i][0] = cos(a);
        o->n[i][1] = sin(a);
        o->t[i][0] = cos(a + PI_D / 2);
        o->t[i][1] = sin(a + PI_D / 2);
        o->B[i][0] = o->V[i];
        const dpt v1 = o->V[(i + 1) % 9];
        for (int k = 0; k <= 8; k++) {
            const double sk = k * 60.0;
            o->B[i][1 + k] = (dpt){ v1.x + o->t[i][0] * sk + o->n[i][0] * sk * sk / 900,
                                    v1.y + o->t[i][1] * sk + o->n[i][1] * sk * sk / 900 };
        }
        o->lum[i] = .3 + .17 * cos(a + 135 * D2R);
        const double lit = .5 + .5 * cos(a + 135 * D2R);
        o->ea[i] = .25 + .45 * lit;
        brass(o->lum[i] + .12, o->grad[i][0]);
        brass(o->lum[i], o->grad[i][1]);
        brass(o->lum[i] - .14, o->grad[i][2]);
    }
    regions((float)ap, (float)phi, o->R);
    o->ly_min = 1e9;
    o->ly_max = -1e9;
}

/* A canvas stroke of width w along path p[0..n) (butt caps, joins rounded: the mitre at these 7.6 deg bends differs by
 * well under a hundredth of a pixel), as a distance test. */
static bool in_stroke(const dpt *p, int n, dpt q, double w)
{
    for (int k = 0; k + 1 < n; k++) {
        const dpt a = p[k], b = p[k + 1];
        const double ex = b.x - a.x, ey = b.y - a.y, l = hypot(ex, ey);
        if (l <= 0) continue;
        const double u = ((q.x - a.x) * ex + (q.y - a.y) * ey) / l, d = fabs((q.x - a.x) * ey - (q.y - a.y) * ex) / l;
        if (u >= 0 && u <= l && d <= w / 2) return true;
        if (k > 0 && hypot(q.x - a.x, q.y - a.y) <= w / 2) return true;   /* an interior join */
    }
    return false;
}

static double grain_at(double u)   /* the texture drawn 260 px wide: bilinear in its columns */
{
    if (!g_grain || u < 0 || u >= 260) return 128;
    double tc = u * 256 / 260 - 0.5;
    if (tc < 0) tc = 0;
    if (tc > 255) tc = 255;
    const int i0 = (int)floor(tc), i1 = i0 < 255 ? i0 + 1 : 255;
    return g_grain[i0] + (g_grain[i1] - g_grain[i0]) * (tc - i0);
}

/* One sample: base colour in, the composite out (8-bit floats). cand: the regions to test (bit mask), strokes: the
 * polylines worth a distance test (bit mask). */
static void oracle_sample(oracle_t *o, dpt q, const double base[3], unsigned cand, double out[3])
{
    double D[3] = { base[0], base[1], base[2] };
    const double x = q.x - CX, y = q.y - CX;
    if (hypot(x, y) >= 199) {
        memcpy(out, D, sizeof D);
        return;
    }
    for (int k = 0; k < 3; k++) D[k] *= 1 - .55 * (1 - o->s);
    double din = 1e9;   /* inside the aperture: the distance to its outline */
    for (int i = 0; i < 9; i++) din = fmin(din, o->ap - (x * o->n[i][0] + y * o->n[i][1]));
    if (o->ap > 1 && o->ap < OMNI_IRIS_GONE && din >= 0) {   /* drawIris returns before its ring from ap 203 on */
        if (din <= 13) for (int k = 0; k < 3; k++) D[k] *= .45;
        if (din <= 5) for (int k = 0; k < 3; k++) D[k] *= .5;
    }
    int reg = -1;
    for (int i = 0; i < 9 && reg < 0; i++)
        if ((cand >> i & 1) && pip(o->R[i].p, 18, q.x, q.y)) reg = i;
    if (reg >= 0 && o->ap < OMNI_IRIS_GONE) {
        const double u = x * o->n[reg][0] + y * o->n[reg][1] - o->ap;
        const double ly = (q.x - o->V[reg].x) * o->t[reg][0] + (q.y - o->V[reg].y) * o->t[reg][1];
        o->ly_min = fmin(o->ly_min, ly);
        o->ly_max = fmax(o->ly_max, ly);
        const double tt = fmin(fmax(u / 230, 0), 1);
        const int j = tt < .25 ? 0 : 1;
        const double f = j ? (tt - .25) / .75 : tt / .25;
        for (int k = 0; k < 3; k++) D[k] = o->grad[reg][j][k] + (o->grad[reg][j + 1][k] - o->grad[reg][j][k]) * f;
        if (ly >= -40 && ly < 480) {
            const double g = grain_at(u) - 128;
            static const double LIGHT[3] = { 255, 232, 180 };
            for (int k = 0; k < 3; k++) D[k] = g > 0 ? D[k] + (LIGHT[k] - D[k]) * g / 107 : D[k] * (1 + g / 128);
        }
    }
    if (o->ap < OMNI_IRIS_GONE)
        for (int i = 0; i < 9; i++) {
            const int nx = (i + 1) % 9;
            if (reg == nx && in_stroke(&o->B[i][1], 9, q, 16)) {
                for (int k = 0; k < 3; k++) D[k] *= .65;
                if (in_stroke(&o->B[i][1], 9, q, 6))
                    for (int k = 0; k < 3; k++) D[k] *= .55;
            }
            if (in_stroke(o->B[i], 10, q, 1.1)) {
                static const double E[3] = { 255, 228, 170 };
                for (int k = 0; k < 3; k++) D[k] += (E[k] - D[k]) * o->ea[i];
            }
        }
    if (o->ap < 30 && q.x >= CX - 40 && q.x < CX + 40 && q.y >= CX - 40 && q.y < CX + 40) {
        /* createRadialGradient(C - 8, C - 10, 0, C, C, 40): the largest t >= 0 with |q - c(t)| = 40 t */
        const double qx = q.x - (CX - 8), qy = q.y - (CX - 10), qd = qx * 8 + qy * 10, a = 164 - 1600;
        const double t = (qd - sqrt(qd * qd - a * (qx * qx + qy * qy))) / a;
        if (t <= 1) {
            const double al = .22 * (1 - o->ap / 30) * (1 - t);
            static const double G[3] = { 255, 236, 190 };
            for (int k = 0; k < 3; k++) D[k] += (G[k] - D[k]) * al;
        }
    }
    memcpy(out, D, sizeof D);
}

static uint16_t base_px(int x, int y) { return omni_pack565(x * 255 / 465, y * 255 / 465, (x + y) * 255 / 930); }

/* The oracle's pixel: 16 x 16 samples when any edge (a region boundary, a stroke's or band's edge, the disc, the glint
 * box) passes within 0.75 px of the centre, else the centre. */
static void oracle_px(oracle_t *o, int px, int py, double out[3])
{
    const uint16_t b = base_px(px, py);
    const double base[3] = { omni_ex_r(b), omni_ex_g(b), omni_ex_b(b) };
    const dpt c = { px + 0.5, py + 0.5 };
    const double x = c.x - CX, y = c.y - CX;
    double fd = fabs(hypot(x, y) - 199);
    unsigned cand = 0;
    for (int i = 0; i < 9; i++) {
        double dr = 1e9;   /* to ray i and to the polished path */
        for (int k = 1; k < 9; k++) dr = fmin(dr, seg_dist(c, o->B[i][k], o->B[i][k + 1]));
        const double de = fmin(dr, seg_dist(c, o->B[i][0], o->B[i][1]));
        fd = fmin(fd, fmin(fabs(dr - 8), fabs(dr - 3)));
        fd = fmin(fd, fmin(de, fabs(de - 0.55)));
        fd = fmin(fd, fmin(seg_dist(c, o->B[i][1], (dpt){ o->B[i][1].x - 16 * o->n[i][0], o->B[i][1].y - 16 * o->n[i][1] }),
                           seg_dist(c, o->B[i][0], (dpt){ o->B[i][0].x - 16 * o->n[i][0], o->B[i][0].y - 16 * o->n[i][1] })));
        bool near = c.x + 2 >= o->R[i].x0 && c.x - 2 <= o->R[i].x1 && c.y + 2 >= o->R[i].y0 && c.y - 2 <= o->R[i].y1;
        if (near) {
            double dreg = 1e9;
            for (int k = 0; k < 18; k++) dreg = fmin(dreg, seg_dist(c, o->R[i].p[k], o->R[i].p[(k + 1) % 18]));
            near = dreg < 2 || pip(o->R[i].p, 18, c.x, c.y);
        }
        if (near) cand |= 1u << i;
    }
    double din = 1e9;
    for (int i = 0; i < 9; i++) din = fmin(din, o->ap - (x * o->n[i][0] + y * o->n[i][1]));
    fd = fmin(fd, fmin(fabs(din), fmin(fabs(din - 5), fabs(din - 13))));
    if (o->ap < 30) fd = fmin(fd, fmin(fabs(fabs(x) - 40), fabs(fabs(y) - 40)));
    if (fd > 0.75) {
        oracle_sample(o, c, base, cand, out);
        return;
    }
    double acc[3] = { 0, 0, 0 };
    for (int j = 0; j < 16; j++)
        for (int i = 0; i < 16; i++) {
            double v[3];
            oracle_sample(o, (dpt){ px + (i + 0.5) / 16, py + (j + 0.5) / 16 }, base, cand, v);
            for (int k = 0; k < 3; k++) acc[k] += v[k];
        }
    for (int k = 0; k < 3; k++) out[k] = acc[k] / 256;
}

static uint16_t g_fb[W * W];

/* drawIris at (ap, phi, s) against the oracle on the disc r < 199: the share of pixels within +-12 in every channel */
static double vs_oracle(float ap, float phi, float s, int *worst)
{
    oracle_t *o = malloc(sizeof *o);
    TEST_ASSERT_NOT_NULL(o);
    oracle_init(o, ap, phi, s);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) g_fb[y * W + x] = base_px(x, y);
    omni_iris_draw(g_fb, W, NULL, ap, phi, s, g_grain);
    int n = 0, ok = 0;
    *worst = 0;
    for (int py = 34; py < 432; py++)
        for (int px = 34; px < 432; px++) {
            if (hypot(px + 0.5 - CX, py + 0.5 - CX) >= 199) continue;
            double want[3];
            oracle_px(o, px, py, want);
            const uint16_t g = g_fb[py * W + px];
            const int got[3] = { omni_ex_r(g), omni_ex_g(g), omni_ex_b(g) };
            int e = 0;
            for (int k = 0; k < 3; k++) {
                const int d = abs(got[k] - (int)floor(want[k] + 0.5));
                e = d > e ? d : e;
            }
            n++;
            ok += e <= 12;
            *worst = e > *worst ? e : *worst;
        }
    if (ap < OMNI_IRIS_GONE) {   /* the grain's rows: every blade sample in the disc is inside [-40, 480) */
        TEST_ASSERT_TRUE(o->ly_min >= -40);
        TEST_ASSERT_TRUE(o->ly_max < 480);
    }
    printf("iris vs oracle at ap %.1f phi %.2f deg s %.3f: %.2f %% within 12 (worst %d), local y %.1f .. %.1f\n", ap,
           phi / D2R, s, 100.0 * ok / n, *worst, o->ly_min, o->ly_max);
    free(o);
    return (double)ok / n;
}

static void test_iris_vs_oracle(void)
{
    load_grain();
    int worst;
    TEST_ASSERT_TRUE(vs_oracle(202, (float)(61 * D2R), 202 / 207.0f, &worst) >= 0.98);   /* the last blades */
    TEST_ASSERT_TRUE(vs_oracle(205, (float)(62 * D2R), 205 / 207.0f, &worst) >= 0.98);   /* the dim alone */
    TEST_ASSERT_TRUE(vs_oracle(103.5f, (float)(31 * D2R), 0.5f, &worst) >= 0.98);
    /* not gated by the brief, held to the same line: closed (the glint), early, wide */
    TEST_ASSERT_TRUE(vs_oracle(0, (float)(-6 * D2R), 0, &worst) >= 0.98);
    TEST_ASSERT_TRUE(vs_oracle(12, (float)(3 * D2R), 12 / 207.0f, &worst) >= 0.98);
    TEST_ASSERT_TRUE(vs_oracle(180, (float)(54 * D2R), 180 / 207.0f, &worst) >= 0.98);
}

/* The grain's whole range (a ramp 0 .. 255 in place of the shipped 118 .. 133): the decode's two halves, no wrap or
 * overflow in the colour tables (Task 10 review: the device showed garbage, which was an unrendered buffer, not this; the
 * test pins that the colour path itself holds at every grain value and aperture). */
static void test_iris_full_grain(void)
{
    static uint8_t ramp[256 * 512];
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 256; x++) ramp[y * 256 + x] = (uint8_t)x;
    load_grain();
    const uint8_t *keep = g_grain;
    g_grain = ramp;
    int worst;
    static const float AP[4] = { 0, 60, 150, 198 };
    for (int i = 0; i < 4; i++) TEST_ASSERT_TRUE(vs_oracle(AP[i], (float)((10 + 13 * i) * D2R), AP[i] / 207, &worst) >= 0.98);
    g_grain = keep;
}

/* Nothing outside the disc's pixels is touched, and a clip gives the same pixels as the whole draw. */
static void test_iris_clip(void)
{
    load_grain();
    static uint16_t a[W * W];
    for (int i = 0; i < W * W; i++) a[i] = base_px(i % W, i / W);
    omni_iris_draw(a, W, NULL, 60, 0.3f, 0.29f, g_grain);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            if (hypot(x + 0.5 - CX, y + 0.5 - CX) > 199.71) TEST_ASSERT_EQUAL_HEX16(base_px(x, y), a[y * W + x]);
    const int xs[5] = { 0, 101, 233, 234, W }, ys[5] = { 0, 77, 233, 300, W };
    for (int i = 0; i < W * W; i++) g_fb[i] = base_px(i % W, i / W);
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) {
            const omni_rect_t r = { (int16_t)xs[i], (int16_t)ys[j], (int16_t)xs[i + 1], (int16_t)ys[j + 1] };
            omni_iris_draw(g_fb + ys[j] * W + xs[i], W, &r, 60, 0.3f, 0.29f, g_grain);
        }
    TEST_ASSERT_EQUAL_MEMORY(a, g_fb, sizeof a);
}

/* omni_iris_opaque: inside [o0, h0) and [h1, o1) the result does not depend on what fb held. */
static void test_iris_opaque(void)
{
    load_grain();
    static uint16_t a[W * W];
    static omni_iris_t ip;
    static const float AP[4] = { 0, 40, 103.5f, 190 };
    for (int k = 0; k < 4; k++) {
        TEST_ASSERT_TRUE(omni_iris_prepare(&ip, AP[k], 0.4f, AP[k] / 207, g_grain));
        for (int i = 0; i < W * W; i++) a[i] = base_px(i % W, i / W), g_fb[i] = 0xf81f;
        omni_iris_draw_prepared(&ip, a, W, NULL);
        omni_iris_draw_prepared(&ip, g_fb, W, NULL);
        int opaque = 0;
        for (int y0 = 0; y0 < W; y0 += 16) {
            int16_t o[4];
            omni_iris_opaque(&ip, y0, y0 + 16 < W ? y0 + 16 : W, 0, W, o);
            for (int y = y0; y < y0 + 16 && y < W; y++)
                for (int x = o[0]; x < o[1]; x++) {
                    if (x >= o[2] && x < o[3]) continue;
                    opaque++;
                    TEST_ASSERT_EQUAL_HEX16(a[y * W + x], g_fb[y * W + x]);
                }
        }
        printf("iris opaque at ap %.1f: %d px\n", AP[k], opaque);
        TEST_ASSERT_TRUE(opaque > (AP[k] < 150 ? 40000 : 0));
    }
    TEST_ASSERT_FALSE(omni_iris_prepare(&ip, 207, 1.08f, 1, g_grain));   /* fully open: nothing */
}

/* ---- The model: the iris entry steps the iris into the state until it is open */

static void test_model_iris_state(void)
{
    omni_model_t m;
    omni_model_init(&m, 39.96, -83.0);
    struct tm tm = { .tm_year = 126, .tm_mon = 9, .tm_mday = 22, .tm_hour = 10, .tm_min = 10, .tm_sec = 40 };
    omni_dial_state_t st;
    const uint32_t w = 0xfffffe00u;   /* across the u32 wrap */
    omni_model_wake(&m, w, &OMNI_ENTRY_IRIS, false);
    omni_model_step(&m, w + 950, true, &tm, OMNI_LENS_OFF, &st);
    TEST_ASSERT_TRUE(st.iris);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 103.5f, st.iris_ap);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, (float)(31 * D2R), st.iris_phi);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.5f, st.iris_s);
    TEST_ASSERT_EQUAL_FLOAT(0, st.fade);
    TEST_ASSERT_FALSE(st.lid);
    omni_model_step(&m, w + 100, true, &tm, OMNI_LENS_OFF, &st);   /* the fade from black, closed */
    TEST_ASSERT_TRUE(st.iris);
    TEST_ASSERT_EQUAL_FLOAT(0, st.iris_ap);
    TEST_ASSERT_TRUE(st.fade > 0.2f);
    omni_model_step(&m, w + 1420, true, &tm, OMNI_LENS_OFF, &st);   /* open: gone */
    TEST_ASSERT_FALSE(st.iris);
    omni_model_step(&m, w + 1400, true, &tm, OMNI_LENS_OFF, &st);   /* (at 1419 s rounds to 1 in float) */
    TEST_ASSERT_TRUE(st.iris);
    TEST_ASSERT_TRUE(st.iris_s < 1);
    omni_model_wake(&m, w, &OMNI_ENTRY_IRIS, true);   /* reduced: t x 2.2 */
    omni_model_step(&m, w + 300, true, &tm, OMNI_LENS_OFF, &st);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 5.81386f, st.iris_ap);
    omni_model_step(&m, w + 646, true, &tm, OMNI_LENS_OFF, &st);
    TEST_ASSERT_FALSE(st.iris);
    omni_model_wake(&m, w, &OMNI_ENTRY_IRIS, false);   /* a tap skips it */
    omni_model_skip(&m, w + 600);
    omni_model_step(&m, w + 700, true, &tm, OMNI_LENS_OFF, &st);
    TEST_ASSERT_FALSE(st.iris);
    omni_model_wake(&m, w, &OMNI_ENTRY_FLIP, false);   /* other entries: no iris */
    omni_model_step(&m, w + 400, true, &tm, OMNI_LENS_OFF, &st);
    TEST_ASSERT_FALSE(st.iris);
    omni_model_wake(&m, w, &OMNI_ENTRY_DIAL, false);
    omni_model_step(&m, w + 400, true, &tm, OMNI_LENS_OFF, &st);
    TEST_ASSERT_FALSE(st.iris);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_iris_timeline);
    RUN_TEST(test_iris_vertices);
    RUN_TEST(test_iris_partition);
    RUN_TEST(test_iris_vs_oracle);
    RUN_TEST(test_iris_full_grain);
    RUN_TEST(test_iris_clip);
    RUN_TEST(test_iris_opaque);
    RUN_TEST(test_model_iris_state);
    return UNITY_END();
}
